#include "cbz_document.h"
#include <SDL.h>
#include <SDL_image.h>
#include <iostream>
#include <algorithm>
#include <cctype>
#include <regex>

#ifdef __APPLE__
#include <zip.h>
#elif defined(__linux__)
#include <zip.h>
#else
#include <libzip/zip.h>
#endif

CbzDocument::CbzDocument() {
}

CbzDocument::~CbzDocument() {
    close();
}

bool CbzDocument::open(const std::string& filePath) {
    close();
    
    m_archivePath = filePath;
    
    if (!extractArchive(filePath)) {
        std::cerr << "Failed to extract CBZ archive: " << filePath << std::endl;
        return false;
    }
    
    if (m_images.empty()) {
        std::cerr << "No images found in CBZ archive: " << filePath << std::endl;
        return false;
    }
    
    // Sort images by filename (natural sort for comic pages)
    std::sort(m_images.begin(), m_images.end(), 
              [](const ImageInfo& a, const ImageInfo& b) {
                  return compareImageFilenames(a.filename, b.filename);
              });
    
    std::cout << "Loaded CBZ with " << m_images.size() << " images" << std::endl;
    return true;
}

void CbzDocument::close() {
    m_images.clear();
    m_archivePath.clear();
}

int CbzDocument::getPageCount() const {
    return static_cast<int>(m_images.size());
}

std::vector<unsigned char> CbzDocument::renderPage(int page, int& width, int& height, int scale) {
    if (page < 0 || page >= static_cast<int>(m_images.size())) {
        width = height = 0;
        return {};
    }
    
    ImageInfo& imageInfo = m_images[page];
    
    // Load image data if not already loaded
    if (!imageInfo.loaded) {
        if (!loadImageFromMemory(imageInfo)) {
            width = height = 0;
            return {};
        }
    }
    
    // Calculate scaled dimensions
    int targetWidth = static_cast<int>(imageInfo.width * (scale / 100.0));
    int targetHeight = static_cast<int>(imageInfo.height * (scale / 100.0));
    
    width = targetWidth;
    height = targetHeight;
    
    // If scale is 100%, return original data
    if (scale == 100 && !imageInfo.data.empty()) {
        return imageInfo.data;
    }
    
    // Otherwise, scale the image
    return scaleImage(imageInfo.data, imageInfo.width, imageInfo.height, 
                     targetWidth, targetHeight);
}

int CbzDocument::getPageWidthNative(int page) {
    if (page < 0 || page >= static_cast<int>(m_images.size())) {
        return 0;
    }
    
    ImageInfo& imageInfo = m_images[page];
    if (!imageInfo.loaded) {
        loadImageFromMemory(imageInfo);
    }
    
    return imageInfo.width;
}

int CbzDocument::getPageHeightNative(int page) {
    if (page < 0 || page >= static_cast<int>(m_images.size())) {
        return 0;
    }
    
    ImageInfo& imageInfo = m_images[page];
    if (!imageInfo.loaded) {
        loadImageFromMemory(imageInfo);
    }
    
    return imageInfo.height;
}

bool CbzDocument::extractArchive(const std::string& cbzPath) {
    zip_t* archive = zip_open(cbzPath.c_str(), ZIP_RDONLY, nullptr);
    if (!archive) {
        std::cerr << "Failed to open CBZ archive: " << cbzPath << std::endl;
        return false;
    }
    
    zip_int64_t numEntries = zip_get_num_entries(archive, 0);
    
    for (zip_int64_t i = 0; i < numEntries; i++) {
        const char* name = zip_get_name(archive, i, 0);
        if (!name) continue;
        
        std::string filename(name);
        
        // Skip directories and non-image files
        if (filename.back() == '/' || !isImageFile(filename)) {
            continue;
        }
        
        // Get file info
        struct zip_stat fileStat;
        if (zip_stat_index(archive, i, 0, &fileStat) != 0) {
            continue;
        }
        
        // Read file data
        zip_file_t* file = zip_fopen_index(archive, i, 0);
        if (!file) {
            continue;
        }
        
        std::vector<uint8_t> fileData(fileStat.size);
        zip_int64_t bytesRead = zip_fread(file, fileData.data(), fileStat.size);
        zip_fclose(file);
        
        if (bytesRead != static_cast<zip_int64_t>(fileStat.size)) {
            continue;
        }
        
        // Create image info
        ImageInfo imageInfo;
        imageInfo.filename = filename;
        imageInfo.data = std::move(fileData);
        imageInfo.loaded = false;  // Will load dimensions when needed
        
        m_images.push_back(std::move(imageInfo));
    }
    
    zip_close(archive);
    return !m_images.empty();
}

bool CbzDocument::loadImageFromMemory(ImageInfo& imageInfo) {
    if (imageInfo.loaded || imageInfo.data.empty()) {
        return imageInfo.loaded;
    }
    
    // Create SDL_RWops from memory
    SDL_RWops* rw = SDL_RWFromMem(imageInfo.data.data(), 
                                  static_cast<int>(imageInfo.data.size()));
    if (!rw) {
        std::cerr << "Failed to create SDL_RWops for image: " << imageInfo.filename << std::endl;
        return false;
    }
    
    // Load image using SDL_image
    SDL_Surface* surface = IMG_Load_RW(rw, 1); // 1 = free RWops automatically
    if (!surface) {
        std::cerr << "Failed to load image: " << imageInfo.filename 
                  << " - " << IMG_GetError() << std::endl;
        return false;
    }
    
    imageInfo.width = surface->w;
    imageInfo.height = surface->h;
    
    // Convert to RGB24 format
    SDL_Surface* rgbSurface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    
    if (!rgbSurface) {
        std::cerr << "Failed to convert image to RGB24: " << imageInfo.filename << std::endl;
        return false;
    }
    
    // Copy pixel data
    int dataSize = rgbSurface->w * rgbSurface->h * 3; // 3 bytes per pixel (RGB)
    imageInfo.data.resize(dataSize);
    
    if (SDL_MUSTLOCK(rgbSurface)) {
        SDL_LockSurface(rgbSurface);
    }
    
    std::memcpy(imageInfo.data.data(), rgbSurface->pixels, dataSize);
    
    if (SDL_MUSTLOCK(rgbSurface)) {
        SDL_UnlockSurface(rgbSurface);
    }
    
    SDL_FreeSurface(rgbSurface);
    imageInfo.loaded = true;
    
    return true;
}

std::vector<uint8_t> CbzDocument::scaleImage(const std::vector<uint8_t>& originalData,
                                            int originalWidth, int originalHeight,
                                            int targetWidth, int targetHeight) {
    if (originalData.empty() || originalWidth <= 0 || originalHeight <= 0 ||
        targetWidth <= 0 || targetHeight <= 0) {
        return {};
    }
    
    std::vector<uint8_t> scaledData(targetWidth * targetHeight * 3);
    
    // Simple nearest neighbor scaling
    for (int y = 0; y < targetHeight; y++) {
        for (int x = 0; x < targetWidth; x++) {
            int srcX = (x * originalWidth) / targetWidth;
            int srcY = (y * originalHeight) / targetHeight;
            
            if (srcX >= originalWidth) srcX = originalWidth - 1;
            if (srcY >= originalHeight) srcY = originalHeight - 1;
            
            int srcIndex = (srcY * originalWidth + srcX) * 3;
            int dstIndex = (y * targetWidth + x) * 3;
            
            scaledData[dstIndex + 0] = originalData[srcIndex + 0]; // R
            scaledData[dstIndex + 1] = originalData[srcIndex + 1]; // G
            scaledData[dstIndex + 2] = originalData[srcIndex + 2]; // B
        }
    }
    
    return scaledData;
}

bool CbzDocument::isImageFile(const std::string& filename) {
    std::string lowercaseFilename = filename;
    std::transform(lowercaseFilename.begin(), lowercaseFilename.end(), 
                   lowercaseFilename.begin(), ::tolower);
    
    size_t nameLen = lowercaseFilename.length();
    
    return (nameLen >= 4 && lowercaseFilename.substr(nameLen - 4) == ".jpg") ||
           (nameLen >= 5 && lowercaseFilename.substr(nameLen - 5) == ".jpeg") ||
           (nameLen >= 4 && lowercaseFilename.substr(nameLen - 4) == ".png") ||
           (nameLen >= 4 && lowercaseFilename.substr(nameLen - 4) == ".gif") ||
           (nameLen >= 4 && lowercaseFilename.substr(nameLen - 4) == ".bmp") ||
           (nameLen >= 5 && lowercaseFilename.substr(nameLen - 5) == ".webp") ||
           (nameLen >= 5 && lowercaseFilename.substr(nameLen - 5) == ".tiff") ||
           (nameLen >= 4 && lowercaseFilename.substr(nameLen - 4) == ".tga");
}

bool CbzDocument::compareImageFilenames(const std::string& a, const std::string& b) {
    // Natural sort for comic book pages
    // This handles cases like: page1.jpg, page2.jpg, page10.jpg correctly
    
    // Extract numbers from filenames for proper numeric sorting
    std::regex numberRegex(R"((\d+))");
    std::smatch matchA, matchB;
    
    if (std::regex_search(a, matchA, numberRegex) && 
        std::regex_search(b, matchB, numberRegex)) {
        // Both have numbers, compare numerically
        int numA = std::stoi(matchA[1].str());
        int numB = std::stoi(matchB[1].str());
        if (numA != numB) {
            return numA < numB;
        }
    }
    
    // Fall back to lexicographic comparison
    return a < b;
}

#ifndef CBZ_DOCUMENT_H
#define CBZ_DOCUMENT_H

#include "document.h"
#include <vector>
#include <string>
#include <memory>

// Forward declarations for image loading
struct SDL_Surface;

class CbzDocument : public Document {
public:
    CbzDocument();
    ~CbzDocument() override;

    std::vector<unsigned char> renderPage(int page, int& width, int& height, int scale) override;
    int getPageWidthNative(int page) override;
    int getPageHeightNative(int page) override;
    bool open(const std::string& filePath) override;
    int getPageCount() const override;
    void close() override;

private:
    struct ImageInfo {
        std::string filename;
        std::vector<uint8_t> data;
        int width;
        int height;
        bool loaded;
        
        ImageInfo() : width(0), height(0), loaded(false) {}
    };

    std::string m_archivePath;
    std::vector<ImageInfo> m_images;
    
    // Helper methods
    bool extractArchive(const std::string& cbzPath);
    bool loadImageFromMemory(ImageInfo& imageInfo);
    std::vector<uint8_t> scaleImage(const std::vector<uint8_t>& originalData, 
                                   int originalWidth, int originalHeight, 
                                   int targetWidth, int targetHeight);
    static bool isImageFile(const std::string& filename);
    static bool compareImageFilenames(const std::string& a, const std::string& b);
};

#endif // CBZ_DOCUMENT_H

#include "font_manager.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <sstream>

// Simple JSON handling for config - in a real project you might use a proper JSON library
namespace {
    /**
     * @brief Simple JSON writer for config
     */
    std::string configToJson(const FontConfig& config) {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"fontPath\": \"" << config.fontPath << "\",\n";
        oss << "  \"fontName\": \"" << config.fontName << "\",\n";
        oss << "  \"fontSize\": " << config.fontSize << "\n";
        oss << "}";
        return oss.str();
    }

    /**
     * @brief Simple JSON parser for config
     */
    FontConfig jsonToConfig(const std::string& json) {
        FontConfig config;
        
        // Very basic JSON parsing - find values between quotes or numbers
        auto findStringValue = [&json](const std::string& key) -> std::string {
            std::string searchKey = "\"" + key + "\": \"";
            size_t start = json.find(searchKey);
            if (start == std::string::npos) return "";
            start += searchKey.length();
            size_t end = json.find("\"", start);
            if (end == std::string::npos) return "";
            return json.substr(start, end - start);
        };
        
        auto findIntValue = [&json](const std::string& key) -> int {
            std::string searchKey = "\"" + key + "\": ";
            size_t start = json.find(searchKey);
            if (start == std::string::npos) return 12;
            start += searchKey.length();
            size_t end = json.find_first_of(",\n}", start);
            if (end == std::string::npos) return 12;
            std::string valueStr = json.substr(start, end - start);
            try {
                return std::stoi(valueStr);
            } catch (...) {
                return 12;
            }
        };
        
        config.fontPath = findStringValue("fontPath");
        config.fontName = findStringValue("fontName");
        config.fontSize = findIntValue("fontSize");
        
        return config;
    }
}

FontManager::FontManager() {
    // Constructor - scan fonts on creation
    scanFonts();
}

void FontManager::scanFonts(const std::string& fontsDir) {
    m_availableFonts.clear();
    
    try {
        if (!std::filesystem::exists(fontsDir)) {
            std::cerr << "Fonts directory does not exist: " << fontsDir << std::endl;
            return;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(fontsDir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (isSupportedFontFile(filename)) {
                    FontInfo fontInfo;
                    fontInfo.fileName = filename;
                    fontInfo.filePath = entry.path().string();
                    
                    // Try to extract real font name, fall back to filename
                    fontInfo.displayName = extractFontName(fontInfo.filePath);
                    if (fontInfo.displayName.empty()) {
                        fontInfo.displayName = filenameToDisplayName(filename);
                    }
                    
                    m_availableFonts.push_back(fontInfo);
                }
            }
        }
        
        // Sort fonts by display name for better UI
        std::sort(m_availableFonts.begin(), m_availableFonts.end(),
                  [](const FontInfo& a, const FontInfo& b) {
                      return a.displayName < b.displayName;
                  });
        
        std::cout << "Found " << m_availableFonts.size() << " fonts in " << fontsDir << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error scanning fonts directory: " << e.what() << std::endl;
    }
}

const FontInfo* FontManager::findFontByName(const std::string& displayName) const {
    auto it = std::find_if(m_availableFonts.begin(), m_availableFonts.end(),
                          [&displayName](const FontInfo& font) {
                              return font.displayName == displayName;
                          });
    return (it != m_availableFonts.end()) ? &(*it) : nullptr;
}

std::string FontManager::generateCSS(const FontConfig& config) const {
    if (config.fontPath.empty()) {
        return "";
    }
    
    std::ostringstream css;
    
    // Generate font face declaration
    css << "@font-face {\n";
    css << "  font-family: 'UserSelectedFont';\n";
    css << "  src: url('file://" << config.fontPath << "');\n";
    css << "}\n\n";
    
    // Apply font to all text elements with high priority
    css << "body, *, p, div, span, h1, h2, h3, h4, h5, h6 {\n";
    css << "  font-family: 'UserSelectedFont' !important;\n";
    css << "  font-size: " << config.fontSize << "pt !important;\n";
    css << "}\n";
    
    return css.str();
}

bool FontManager::saveConfig(const FontConfig& config, const std::string& configPath) const {
    try {
        // Create directory if it doesn't exist
        std::filesystem::path path(configPath);
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file for writing: " << configPath << std::endl;
            return false;
        }
        
        file << configToJson(config);
        file.close();
        
        std::cout << "Font configuration saved to: " << configPath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error saving font config: " << e.what() << std::endl;
        return false;
    }
}

FontConfig FontManager::loadConfig(const std::string& configPath) const {
    FontConfig defaultConfig;
    
    try {
        if (!std::filesystem::exists(configPath)) {
            std::cout << "Config file not found, using defaults: " << configPath << std::endl;
            return defaultConfig;
        }
        
        std::ifstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << configPath << std::endl;
            return defaultConfig;
        }
        
        std::string json((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        file.close();
        
        FontConfig config = jsonToConfig(json);
        
        // Validate that the font file still exists
        if (!config.fontPath.empty() && !std::filesystem::exists(config.fontPath)) {
            std::cout << "Configured font file no longer exists: " << config.fontPath << std::endl;
            return defaultConfig;
        }
        
        std::cout << "Font configuration loaded from: " << configPath << std::endl;
        return config;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading font config: " << e.what() << std::endl;
        return defaultConfig;
    }
}

std::string FontManager::extractFontName(const std::string& fontPath) const {
    // For now, we'll use a simple approach and just clean up the filename
    // In a more sophisticated implementation, you could parse the TTF/OTF file
    // to extract the actual font family name from the name table
    
    std::filesystem::path path(fontPath);
    std::string filename = path.stem().string(); // Remove extension
    
    return filenameToDisplayName(filename);
}

bool FontManager::isSupportedFontFile(const std::string& filename) const {
    std::string lowercaseFilename = filename;
    std::transform(lowercaseFilename.begin(), lowercaseFilename.end(), 
                   lowercaseFilename.begin(), ::tolower);
    
    return (lowercaseFilename.size() >= 4 && 
            (lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".ttf" ||
             lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".otf"));
}

std::string FontManager::filenameToDisplayName(const std::string& filename) const {
    std::string displayName = filename;
    
    // Remove extension if present
    size_t dotPos = displayName.find_last_of('.');
    if (dotPos != std::string::npos) {
        displayName = displayName.substr(0, dotPos);
    }
    
    // Replace underscores and hyphens with spaces
    std::replace(displayName.begin(), displayName.end(), '_', ' ');
    std::replace(displayName.begin(), displayName.end(), '-', ' ');
    
    // Basic title case conversion
    bool capitalizeNext = true;
    for (char& c : displayName) {
        if (capitalizeNext && std::isalpha(c)) {
            c = std::toupper(c);
            capitalizeNext = false;
        } else if (std::isspace(c)) {
            capitalizeNext = true;
        } else {
            c = std::tolower(c);
        }
    }
    
    return displayName;
}
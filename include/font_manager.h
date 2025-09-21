#ifndef FONT_MANAGER_H
#define FONT_MANAGER_H

#include <vector>
#include <string>
#include <map>

/**
 * @brief Structure to hold font information
 */
struct FontInfo {
    std::string displayName;    // Font name to display in UI
    std::string filePath;      // Absolute path to font file
    std::string fileName;      // Just the filename for easier identification
};

/**
 * @brief Font configuration settings
 */
struct FontConfig {
    std::string fontPath;      // Path to selected font file
    std::string fontName;      // Display name of selected font
    int fontSize = 12;         // Font size in points
    int zoomStep = 10;         // Zoom increment/decrement step
    
    // Default constructor
    FontConfig() = default;
    
    // Constructor with parameters
    FontConfig(const std::string& path, const std::string& name, int size, int zoom = 10)
        : fontPath(path), fontName(name), fontSize(size), zoomStep(zoom) {}
};

/**
 * @brief Manages font discovery, configuration, and CSS generation
 */
class FontManager {
public:
    FontManager();
    ~FontManager() = default;

    /**
     * @brief Scan the fonts directory for available fonts
     * @param fontsDir Path to the fonts directory (default: "./fonts")
     */
    void scanFonts(const std::string& fontsDir = "./fonts");

    /**
     * @brief Get list of available fonts
     * @return Vector of FontInfo structures
     */
    const std::vector<FontInfo>& getAvailableFonts() const { return m_availableFonts; }

    /**
     * @brief Find font info by display name
     * @param displayName The display name to search for
     * @return Pointer to FontInfo if found, nullptr otherwise
     */
    const FontInfo* findFontByName(const std::string& displayName) const;

    /**
     * @brief Generate CSS string for the given font configuration
     * @param config Font configuration to generate CSS for
     * @return CSS string ready to be applied via fz_set_user_css
     */
    std::string generateCSS(const FontConfig& config) const;

    /**
     * @brief Install custom font loader in MuPDF context
     * @param ctx MuPDF context to install loader in
     * @return true if successful, false otherwise
     */
    bool installFontLoader(void* ctx);

    /**
     * @brief Save font configuration to file
     * @param config Configuration to save
     * @param configPath Path to save configuration (default: "./config.json")
     * @return true if successful, false otherwise
     */
    bool saveConfig(const FontConfig& config, const std::string& configPath = "./config.json") const;

    /**
     * @brief Load font configuration from file
     * @param configPath Path to load configuration from (default: "./config.json")
     * @return FontConfig if successful, default config otherwise
     */
    FontConfig loadConfig(const std::string& configPath = "./config.json") const;

    /**
     * @brief Get font file path by display name (public accessor for font loader)
     * @param displayName The display name to search for
     * @return Font file path if found, empty string otherwise
     */
    std::string getFontPathByName(const std::string& displayName) const;

    /**
     * @brief Extract font name from TTF/OTF file
     * @param fontPath Path to the font file
     * @return Display name extracted from font, or filename if extraction fails
     */
    std::string extractFontName(const std::string& fontPath) const;

private:
    std::vector<FontInfo> m_availableFonts;
    std::map<std::string, std::string> m_fontNameMap; // Maps display names to file paths
    
    /**
     * @brief Check if file has a supported font extension
     * @param filename The filename to check
     * @return true if file is .ttf or .otf
     */
    bool isSupportedFontFile(const std::string& filename) const;
    
    /**
     * @brief Convert filename to display name (remove extension and format nicely)
     * @param filename The filename to convert
     * @return Formatted display name
     */
    std::string filenameToDisplayName(const std::string& filename) const;
};

#endif // FONT_MANAGER_H
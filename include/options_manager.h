#ifndef OPTIONS_MANAGER_H
#define OPTIONS_MANAGER_H

#include "path_utils.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/**
 * @brief Structure to hold font information
 */
struct FontInfo
{
    std::string displayName; // Font name to display in UI
    std::string filePath;    // Absolute path to font file
    std::string fileName;    // Just the filename for easier identification
};

/**
 * @brief Reading style/theme options for ebook display
 */
enum class ReadingStyle
{
    Default = 0,  // No custom styling (use document defaults)
    Sepia,        // Warm sepia tone background
    DarkMode,     // Dark background with light text
    HighContrast, // Black text on white background
    PaperTexture, // Subtle paper-like background
    SoftGray,     // Soft gray background for reduced eye strain
    NightMode     // Very dark mode optimized for night reading
};

/**
 * @brief Font configuration settings
 */
struct FontConfig
{
    std::string fontPath;                                      // Path to selected font file
    std::string fontName;                                      // Display name of selected font
    int fontSize = 12;                                         // Font size in points
    int zoomStep = 10;                                         // Zoom increment/decrement step
    std::string lastBrowseDirectory = getDefaultLibraryRoot(); // Last browsed directory for file browser
    ReadingStyle readingStyle = ReadingStyle::Default;         // Reading style/theme
    bool disableEdgeProgressBar = false;                       // Disable edge nudge progress bar for instant page turns
    bool showDocumentMinimap = true;                           // Display minimap overlay when zoomed in
    bool keepPanningPosition = false;                          // Keep panning position when changing pages (vs. align to top)
    bool showPageIndicatorOverlay = true;                      // Display page indicator overlay when page changes
    bool showScaleOverlay = true;                              // Display zoom/scale overlay during zoom changes

    // Default constructor
    FontConfig() = default;

    // Constructor with parameters
    FontConfig(const std::string& path, const std::string& name, int size, int zoom = 10, const std::string& browseDir = getDefaultLibraryRoot(), ReadingStyle style = ReadingStyle::Default, bool disableEdgeBar = false, bool showMinimap = true, bool keepPanning = false)
        : fontPath(path), fontName(name), fontSize(size), zoomStep(zoom), lastBrowseDirectory(browseDir), readingStyle(style),
          disableEdgeProgressBar(disableEdgeBar), showDocumentMinimap(showMinimap), keepPanningPosition(keepPanning)
    {
    }
};

/**
 * @brief Manages application options including font discovery, configuration, and CSS generation
 */
class OptionsManager
{
public:
    OptionsManager();
    ~OptionsManager();

    /**
     * @brief Scan the fonts directory for available fonts
     * @param fontsDir Path to the fonts directory (default: "./fonts")
     */
    void scanFonts(const std::string& fontsDir = "./fonts");

    /**
     * @brief Get list of available fonts
     * @return Vector of FontInfo structures
     */
    const std::vector<FontInfo>& getAvailableFonts() const
    {
        return m_availableFonts;
    }

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
     * @param configPath Optional override path (defaults to the reader state directory)
     * @return true if successful, false otherwise
     */
    bool saveConfig(const FontConfig& config, std::string configPath = {}) const;

    /**
     * @brief Load font configuration from file
     * @param configPath Optional override path (defaults to the reader state directory)
     * @return FontConfig if successful, default config otherwise
     */
    FontConfig loadConfig(std::string configPath = {}) const;

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

    /**
     * @brief Get display name for a reading style
     * @param style The reading style enum value
     * @return Human-readable name for the style
     */
    static const char* getReadingStyleName(ReadingStyle style);

    /**
     * @brief Get all available reading styles
     * @return Vector of all reading style enum values
     */
    static std::vector<ReadingStyle> getAllReadingStyles();

    /**
     * @brief Get the background color for a reading style
     * @param style The reading style enum value
     * @param r Output red component (0-255)
     * @param g Output green component (0-255)
     * @param b Output blue component (0-255)
     */
    static void getReadingStyleBackgroundColor(ReadingStyle style, uint8_t& r, uint8_t& g, uint8_t& b);

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

#endif // OPTIONS_MANAGER_H

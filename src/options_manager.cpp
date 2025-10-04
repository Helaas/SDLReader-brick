#include "options_manager.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

// MuPDF includes for font loading
extern "C"
{
#include <mupdf/fitz.h>
}

// Simple JSON handling for config - in a real project you might use a proper JSON library
namespace
{
/**
 * @brief Simple JSON writer for config
 */
std::string configToJson(const FontConfig& config)
{
    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"fontPath\": \"" << config.fontPath << "\",\n";
    oss << "  \"fontName\": \"" << config.fontName << "\",\n";
    oss << "  \"fontSize\": " << config.fontSize << ",\n";
    oss << "  \"zoomStep\": " << config.zoomStep << ",\n";
    oss << "  \"lastBrowseDirectory\": \"" << config.lastBrowseDirectory << "\"\n";
    oss << "}";
    return oss.str();
}

/**
 * @brief Simple JSON parser for config
 */
FontConfig jsonToConfig(const std::string& json)
{
    FontConfig config;

    // Very basic JSON parsing - find values between quotes or numbers
    auto findStringValue = [&json](const std::string& key) -> std::string
    {
        std::string searchKey = "\"" + key + "\": \"";
        size_t start = json.find(searchKey);
        if (start == std::string::npos)
            return "";
        start += searchKey.length();
        size_t end = json.find("\"", start);
        if (end == std::string::npos)
            return "";
        return json.substr(start, end - start);
    };

    auto findIntValue = [&json](const std::string& key) -> int
    {
        std::string searchKey = "\"" + key + "\": ";
        size_t start = json.find(searchKey);
        if (start == std::string::npos)
        {
            // Return appropriate defaults for different keys
            if (key == "fontSize")
                return 12;
            if (key == "zoomStep")
                return 10;
            return 12;
        }
        start += searchKey.length();
        size_t end = json.find_first_of(",\n}", start);
        if (end == std::string::npos)
        {
            if (key == "fontSize")
                return 12;
            if (key == "zoomStep")
                return 10;
            return 12;
        }
        std::string valueStr = json.substr(start, end - start);
        try
        {
            return std::stoi(valueStr);
        }
        catch (...)
        {
            if (key == "fontSize")
                return 12;
            if (key == "zoomStep")
                return 10;
            return 12;
        }
    };

    config.fontPath = findStringValue("fontPath");
    config.fontName = findStringValue("fontName");
    config.fontSize = findIntValue("fontSize");
    config.zoomStep = findIntValue("zoomStep");
    config.lastBrowseDirectory = findStringValue("lastBrowseDirectory");
    if (config.lastBrowseDirectory.empty())
    {
#ifdef TG5040_PLATFORM
        config.lastBrowseDirectory = "/mnt/SDCARD";
#else
        const char* home = getenv("HOME");
        config.lastBrowseDirectory = home ? home : "/";
#endif
    }

    return config;
}

// Global pointer to OptionsManager instance for the callback
OptionsManager* g_optionsManagerInstance = nullptr;

/**
 * @brief Custom font loader callback for MuPDF
 * This function is called by MuPDF when it needs to load a font
 */
fz_font* customFontLoader(fz_context* ctx, const char* name, int bold, int italic, int needs_exact_metrics)
{
    (void) needs_exact_metrics; // Suppress unused parameter warning
    if (!g_optionsManagerInstance || !name)
    {
        return nullptr;
    }

    std::cout << "MuPDF requesting font: " << name
              << " (bold=" << bold << ", italic=" << italic << ")" << std::endl;

    // Use the public method to get the font path
    std::string fontPath = g_optionsManagerInstance->getFontPathByName(name);
    if (!fontPath.empty())
    {
        std::cout << "Loading custom font: " << fontPath << std::endl;

        fz_font* volatile font = nullptr;
        fz_try(ctx)
        {
            font = fz_new_font_from_file(ctx, name, fontPath.c_str(), 0, 1);
        }
        fz_catch(ctx)
        {
            std::cerr << "Failed to load font from file: " << fontPath << std::endl;
            return nullptr;
        }

        if (font)
        {
            std::cout << "Successfully loaded custom font: " << name << std::endl;
            return font;
        }
    }

    std::cout << "Font not found in custom loader: " << name << std::endl;
    return nullptr;
}
} // namespace

OptionsManager::OptionsManager()
{
    // Set global instance for callback
    g_optionsManagerInstance = this;

    // Constructor - scan fonts on creation
    scanFonts();
}

void OptionsManager::scanFonts(const std::string& fontsDir)
{
    m_availableFonts.clear();
    m_fontNameMap.clear();

    try
    {
        if (!std::filesystem::exists(fontsDir))
        {
            std::cerr << "Fonts directory does not exist: " << fontsDir << std::endl;
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(fontsDir))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                if (isSupportedFontFile(filename))
                {
                    FontInfo fontInfo;
                    fontInfo.fileName = filename;
                    fontInfo.filePath = entry.path().string();

                    // Try to extract real font name, fall back to filename
                    fontInfo.displayName = extractFontName(fontInfo.filePath);
                    if (fontInfo.displayName.empty())
                    {
                        fontInfo.displayName = filenameToDisplayName(filename);
                    }

                    m_availableFonts.push_back(fontInfo);

                    // Add to font name map for the loader
                    // Use both the display name and a simplified version
                    m_fontNameMap[fontInfo.displayName] = fontInfo.filePath;

                    // Also add a simplified version without spaces for CSS compatibility
                    std::string simpleName = fontInfo.displayName;
                    std::replace(simpleName.begin(), simpleName.end(), ' ', '-');
                    std::transform(simpleName.begin(), simpleName.end(), simpleName.begin(), ::tolower);
                    m_fontNameMap[simpleName] = fontInfo.filePath;
                }
            }
        }

        // Sort fonts by display name for better UI
        std::sort(m_availableFonts.begin(), m_availableFonts.end(),
                  [](const FontInfo& a, const FontInfo& b)
                  {
                      return a.displayName < b.displayName;
                  });

        std::cout << "Found " << m_availableFonts.size() << " fonts in " << fontsDir << std::endl;
        std::cout << "Font name map contains " << m_fontNameMap.size() << " entries" << std::endl;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error scanning fonts directory: " << e.what() << std::endl;
    }
}

const FontInfo* OptionsManager::findFontByName(const std::string& displayName) const
{
    auto it = std::find_if(m_availableFonts.begin(), m_availableFonts.end(),
                           [&displayName](const FontInfo& font)
                           {
                               return font.displayName == displayName;
                           });
    return (it != m_availableFonts.end()) ? &(*it) : nullptr;
}

std::string OptionsManager::getFontPathByName(const std::string& displayName) const
{
    auto it = m_fontNameMap.find(displayName);
    if (it != m_fontNameMap.end())
    {
        return it->second;
    }
    return ""; // Return empty string if not found
}

bool OptionsManager::installFontLoader(void* ctx)
{
    if (!ctx)
    {
        std::cerr << "Invalid MuPDF context provided to installFontLoader" << std::endl;
        return false;
    }

    fz_context* fz_ctx = static_cast<fz_context*>(ctx);

    std::cout << "Installing custom font loader in MuPDF context" << std::endl;

    fz_try(fz_ctx)
    {
        // Install our custom font loader
        fz_install_load_system_font_funcs(fz_ctx, customFontLoader, nullptr, nullptr);
        std::cout << "Custom font loader installed successfully" << std::endl;
        return true;
    }
    fz_catch(fz_ctx)
    {
        std::cerr << "Failed to install custom font loader: " << fz_caught_message(fz_ctx) << std::endl;
        return false;
    }
    return false; // Should never reach here, but needed to satisfy compiler
}

std::string OptionsManager::generateCSS(const FontConfig& config) const
{
    if (config.fontPath.empty() || config.fontName.empty())
    {
        return "";
    }

    std::ostringstream css;

    // Create a simplified font name for CSS (remove spaces, lowercase)
    std::string cssFontName = config.fontName;
    std::replace(cssFontName.begin(), cssFontName.end(), ' ', '-');
    std::transform(cssFontName.begin(), cssFontName.end(), cssFontName.begin(), ::tolower);

    // Apply font to all text elements with very high specificity
    // Use the font name that our custom loader will recognize
    css << "* {\n";
    css << "  font-family: '" << config.fontName << "', '" << cssFontName << "', serif !important;\n";
    css << "  font-size: " << config.fontSize << "pt !important;\n";
    css << "  line-height: 1.4 !important;\n";
    css << "}\n\n";

    // More specific selectors for common EPUB/MOBI elements
    css << "body, p, div, span, h1, h2, h3, h4, h5, h6 {\n";
    css << "  font-family: '" << config.fontName << "', '" << cssFontName << "', serif !important;\n";
    css << "  font-size: " << config.fontSize << "pt !important;\n";
    css << "}\n\n";

    // Try to override any existing font-family declarations
    css << "[data-font], [style] {\n";
    css << "  font-family: '" << config.fontName << "', '" << cssFontName << "', serif !important;\n";
    css << "  font-size: " << config.fontSize << "pt !important;\n";
    css << "}\n";

    return css.str();
}

bool OptionsManager::saveConfig(const FontConfig& config, const std::string& configPath) const
{
    try
    {
        // Create directory if it doesn't exist
        std::filesystem::path path(configPath);
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream file(configPath);
        if (!file.is_open())
        {
            std::cerr << "Failed to open config file for writing: " << configPath << std::endl;
            return false;
        }

        file << configToJson(config);
        file.close();

        std::cout << "Font configuration saved to: " << configPath << std::endl;
        return true;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error saving font config: " << e.what() << std::endl;
        return false;
    }
}

FontConfig OptionsManager::loadConfig(const std::string& configPath) const
{
    FontConfig defaultConfig;

    try
    {
        if (!std::filesystem::exists(configPath))
        {
            std::cout << "Config file not found, using defaults: " << configPath << std::endl;
            return defaultConfig;
        }

        std::ifstream file(configPath);
        if (!file.is_open())
        {
            std::cerr << "Failed to open config file: " << configPath << std::endl;
            return defaultConfig;
        }

        std::string json((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
        file.close();

        FontConfig config = jsonToConfig(json);

        // Validate that the font file still exists
        if (!config.fontPath.empty() && !std::filesystem::exists(config.fontPath))
        {
            std::cout << "Configured font file no longer exists: " << config.fontPath << std::endl;
            return defaultConfig;
        }

        std::cout << "Font configuration loaded from: " << configPath << std::endl;
        return config;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error loading font config: " << e.what() << std::endl;
        return defaultConfig;
    }
}

std::string OptionsManager::extractFontName(const std::string& fontPath) const
{
    // For now, we'll use a simple approach and just clean up the filename
    // In a more sophisticated implementation, you could parse the TTF/OTF file
    // to extract the actual font family name from the name table

    std::filesystem::path path(fontPath);
    std::string filename = path.stem().string(); // Remove extension

    return filenameToDisplayName(filename);
}

bool OptionsManager::isSupportedFontFile(const std::string& filename) const
{
    std::string lowercaseFilename = filename;
    std::transform(lowercaseFilename.begin(), lowercaseFilename.end(),
                   lowercaseFilename.begin(), ::tolower);

    return (lowercaseFilename.size() >= 4 &&
            (lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".ttf" ||
             lowercaseFilename.substr(lowercaseFilename.size() - 4) == ".otf"));
}

std::string OptionsManager::filenameToDisplayName(const std::string& filename) const
{
    std::string displayName = filename;

    // Remove extension if present
    size_t dotPos = displayName.find_last_of('.');
    if (dotPos != std::string::npos)
    {
        displayName = displayName.substr(0, dotPos);
    }

    // Replace underscores and hyphens with spaces
    std::replace(displayName.begin(), displayName.end(), '_', ' ');
    std::replace(displayName.begin(), displayName.end(), '-', ' ');

    // Basic title case conversion
    bool capitalizeNext = true;
    for (char& c : displayName)
    {
        if (capitalizeNext && std::isalpha(c))
        {
            c = std::toupper(c);
            capitalizeNext = false;
        }
        else if (std::isspace(c))
        {
            capitalizeNext = true;
        }
        else
        {
            c = std::tolower(c);
        }
    }

    return displayName;
}

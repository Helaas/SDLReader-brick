#ifndef TEXT_DOCUMENT_H
#define TEXT_DOCUMENT_H

#include "document.h"
#include "options_manager.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <vector>

/**
 * @brief Simple text-only document renderer that bypasses MuPDF.
 *        Renders plain text using SDL_ttf with basic word wrapping.
 */
class TextDocument : public Document
{
public:
    using ArgbBufferPtr = std::shared_ptr<const std::vector<uint32_t>>;

    TextDocument();
    ~TextDocument() override;

    bool open(const std::string& filename) override;
    void close() override;

    int getPageCount() const override
    {
        return m_pageCount;
    }

    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override;

    int getPageWidthNative(int pageNum) override;
    int getPageHeightNative(int pageNum) override;

    // Return the exact rendered page dimensions for a given zoom scale.
    std::pair<int, int> getPageDimensionsForScale(int scale);

    void setBackgroundColor(uint8_t r, uint8_t g, uint8_t b);
    void setFontConfig(const FontConfig& config);

    // ARGB fast-path for render manager
    bool tryGetCachedPageARGB(int pageNumber, int scale, ArgbBufferPtr& buffer, int& width, int& height);
    ArgbBufferPtr renderPageARGB(int pageNumber, int& width, int& height, int scale);

private:
    struct Layout
    {
        int fontSize = 0;
        int charWidth = 8;
        int lineHeight = 16;
        int charsPerLine = 80;
        int linesPerPage = 40;
        int pageWidth = 0;
        int pageHeight = 0;
        int pageCount = 0;
        std::vector<std::string> wrappedLines;
    };

    struct TtfFontDeleter
    {
        void operator()(TTF_Font* font) const;
    };

    bool ensureFont(int pointSize);
    void clearCaches();
    void clearLayouts();
    const Layout& ensureLayoutForSize(int fontSize);
    Layout buildLayoutForSize(int fontSize);
    int computePageWidth(const Layout& layout) const;
    int computePageHeight(const Layout& layout) const;

    std::string m_filePath;
    std::string m_rawContent;
    std::unordered_map<int, std::unique_ptr<TTF_Font, TtfFontDeleter>> m_fontCache;
    std::unordered_map<int, Layout> m_layoutCache;

    std::map<std::pair<int, int>, std::tuple<ArgbBufferPtr, int, int>> m_argbCache;
    std::mutex m_cacheMutex;

    int m_pageCount = 0;
    int m_baseFontSize = 16;
    int m_lastLayoutFontSizeUsed = 0;

    uint8_t m_bgR = 255;
    uint8_t m_bgG = 255;
    uint8_t m_bgB = 255;

    std::string m_fontPath = "fonts/JetBrainsMono-Regular.ttf";
};

#endif // TEXT_DOCUMENT_H

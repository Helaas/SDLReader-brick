#include "text_document.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

namespace
{
bool ensureTtfInitialized()
{
    static std::once_flag flag;
    static bool ok = false;
    std::call_once(flag, []()
                   {
        if (TTF_Init() != 0)
        {
            std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
            return;
        }
        ok = true; });
    return ok;
}

int computeLuminance(uint8_t r, uint8_t g, uint8_t b)
{
    return (static_cast<int>(r) * 299 + static_cast<int>(g) * 587 + static_cast<int>(b) * 114) / 1000;
}
} // namespace

void TextDocument::TtfFontDeleter::operator()(TTF_Font* font) const
{
    if (font)
    {
        TTF_CloseFont(font);
    }
}

TextDocument::TextDocument() = default;

TextDocument::~TextDocument()
{
    close();
}

bool TextDocument::ensureFont(int pointSize)
{
    if (!ensureTtfInitialized())
    {
        return false;
    }

    if (pointSize <= 0)
    {
        pointSize = m_baseFontSize;
    }

    auto it = m_fontCache.find(pointSize);
    if (it != m_fontCache.end())
    {
        return it->second != nullptr;
    }

    TTF_Font* font = TTF_OpenFont(m_fontPath.c_str(), pointSize);
    if (!font && m_fontPath != "fonts/Roboto-Regular.ttf")
    {
        font = TTF_OpenFont("fonts/Roboto-Regular.ttf", pointSize);
    }

    if (!font)
    {
        std::cerr << "TextDocument: failed to load font " << m_fontPath << " size " << pointSize << ": " << TTF_GetError() << std::endl;
        return false;
    }

    TTF_SetFontHinting(font, TTF_HINTING_MONO);
    TTF_SetFontKerning(font, 1);
    m_fontCache[pointSize] = std::unique_ptr<TTF_Font, TtfFontDeleter>(font);
    return true;
}

void TextDocument::setFontConfig(const FontConfig& config)
{
    bool pathChanged = false;
    bool sizeChanged = false;

    if (!config.fontPath.empty())
    {
        if (config.fontPath != m_fontPath)
        {
            pathChanged = true;
        }
        m_fontPath = config.fontPath;
    }
    if (config.fontSize > 0)
    {
        if (config.fontSize != m_baseFontSize)
        {
            sizeChanged = true;
        }
        m_baseFontSize = config.fontSize;
    }

    if (!m_rawContent.empty() && (pathChanged || sizeChanged))
    {
        clearCaches();
        clearLayouts();
        m_fontCache.clear();
        m_lastLayoutFontSizeUsed = 0;
        const auto& layout = ensureLayoutForSize(m_baseFontSize);
        m_pageCount = layout.pageCount;
    }
}

void TextDocument::setBackgroundColor(uint8_t r, uint8_t g, uint8_t b)
{
    if (r == m_bgR && g == m_bgG && b == m_bgB)
    {
        return;
    }

    m_bgR = r;
    m_bgG = g;
    m_bgB = b;

    // Theme change should invalidate cached renders so the new background is applied immediately
    clearCaches();
}

void TextDocument::clearCaches()
{
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_argbCache.clear();
}

void TextDocument::clearLayouts()
{
    m_layoutCache.clear();
    m_lastLayoutFontSizeUsed = 0;
}

bool TextDocument::open(const std::string& filename)
{
    close();
    m_filePath = filename;

    std::ifstream in(filename);
    if (!in.is_open())
    {
        std::cerr << "TextDocument: failed to open " << filename << std::endl;
        return false;
    }

    std::ostringstream ss;
    ss << in.rdbuf();
    m_rawContent = ss.str();

    clearLayouts();
    // Precompute base layout so page count is available immediately
    const auto& layout = ensureLayoutForSize(m_baseFontSize);
    m_pageCount = layout.pageCount;
    return m_pageCount > 0;
}

void TextDocument::close()
{
    clearCaches();
    m_fontCache.clear();
    clearLayouts();
    m_pageCount = 0;
    m_rawContent.clear();
}

TextDocument::Layout TextDocument::buildLayoutForSize(int fontSize)
{
    Layout layout;
    layout.fontSize = fontSize;

    if (!ensureFont(fontSize))
    {
        return layout;
    }

    TTF_Font* font = m_fontCache[fontSize].get();

    int cw = 0;
    int ch = 0;
    const char* avgSample = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (TTF_SizeUTF8(font, avgSample, &cw, &ch) == 0)
    {
        int sampleLen = static_cast<int>(std::strlen(avgSample));
        if (sampleLen > 0)
        {
            layout.charWidth = std::max(4, cw / sampleLen);
        }
    }

    if (layout.charWidth <= 0)
    {
        if (TTF_SizeUTF8(font, "M", &cw, &ch) != 0)
        {
            cw = 8;
            ch = fontSize + 2;
        }

        layout.charWidth = std::max(4, cw);
    }

    layout.lineHeight = std::max(TTF_FontLineSkip(font), std::max(ch, fontSize + 2));

    // Grow page dimensions when zooming in, but keep the base size when zooming out
    // so chars-per-line still increases at smaller font sizes
    double scaleFactor = static_cast<double>(fontSize) / static_cast<double>(m_baseFontSize);
    double grow = std::max(1.0, scaleFactor);
    int targetWidth = static_cast<int>(std::lround(720.0 * grow));
    int targetHeight = static_cast<int>(std::lround(1024.0 * grow));
    int marginX = std::max(8, layout.charWidth / 2);
    int marginY = std::max(8, layout.lineHeight / 4);

    constexpr int MIN_CHARS_PER_LINE = 96;
    layout.charsPerLine = std::clamp((targetWidth - marginX * 2) / layout.charWidth, 40, 160);
    layout.charsPerLine = std::clamp(std::max(layout.charsPerLine, MIN_CHARS_PER_LINE), 40, 160);
    layout.linesPerPage = std::clamp((targetHeight - marginY * 2) / layout.lineHeight, 25, 200);

    int contentWidthPx = std::max(targetWidth - marginX * 2, layout.charsPerLine * layout.charWidth);
    int maxLineWidthPx = std::max(1, contentWidthPx);
    int maxObservedLineWidth = 0;
    std::unordered_map<std::string, int> widthCache;

    // Measure actual text width so proportional fonts don't wrap prematurely
    auto measureWidth = [&](const std::string& text)
    {
        if (text.empty())
        {
            return 0;
        }

        auto it = widthCache.find(text);
        if (it != widthCache.end())
        {
            return it->second;
        }

        int w = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &w, nullptr) == 0)
        {
            widthCache.emplace(text, w);
            return w;
        }

        w = static_cast<int>(text.size()) * layout.charWidth;
        widthCache.emplace(text, w);
        return w;
    };

    auto wrapLine = [&](const std::string& line)
    {
        if (line.empty())
        {
            layout.wrappedLines.emplace_back("");
            return;
        }

        std::vector<std::pair<std::string, int>> tokens;
        tokens.reserve(line.size() / 2 + 1);

        // Tokenize into runs of spaces and non-spaces so we can reuse measurements
        for (size_t i = 0; i < line.size();)
        {
            size_t start = i;
            if (line[i] == ' ')
            {
                while (i < line.size() && line[i] == ' ')
                {
                    ++i;
                }
            }
            else
            {
                while (i < line.size() && line[i] != ' ')
                {
                    ++i;
                }
            }

            std::string token = line.substr(start, i - start);
            int w = measureWidth(token);
            tokens.emplace_back(std::move(token), w);
        }

        std::string current;
        int currentWidth = 0;

        auto flushCurrent = [&]()
        {
            layout.wrappedLines.emplace_back(std::move(current));
            maxObservedLineWidth = std::max(maxObservedLineWidth, currentWidth);
            current.clear();
            currentWidth = 0;
        };

        for (auto& [token, width] : tokens)
        {
            // Extremely long token without spaces: hard-wrap it
            if (width > maxLineWidthPx && token.find(' ') == std::string::npos)
            {
                if (!current.empty())
                {
                    flushCurrent();
                }

                size_t pos = 0;
                while (pos < token.size())
                {
                    size_t hardLen = std::max<size_t>(1, static_cast<size_t>(maxLineWidthPx / std::max(layout.charWidth, 1)));
                    size_t take = std::min(hardLen, token.size() - pos);
                    std::string piece = token.substr(pos, take);
                    int pieceWidth = measureWidth(piece);

                    // If the measured width is still too large, shrink until it fits
                    while (pieceWidth > maxLineWidthPx && take > 1)
                    {
                        --take;
                        piece = token.substr(pos, take);
                        pieceWidth = measureWidth(piece);
                    }

                    layout.wrappedLines.emplace_back(std::move(piece));
                    maxObservedLineWidth = std::max(maxObservedLineWidth, pieceWidth);
                    pos += take;
                }

                continue;
            }

            // Normal case: add token to current line or wrap
            if (currentWidth + width <= maxLineWidthPx || current.empty())
            {
                current += token;
                currentWidth += width;
            }
            else
            {
                flushCurrent();
                current = token;
                currentWidth = width;
            }
        }

        if (!current.empty())
        {
            flushCurrent();
        }
    };

    std::string current;
    current.reserve(256);
    for (char ch : m_rawContent)
    {
        if (ch == '\r')
        {
            continue;
        }
        if (ch == '\n')
        {
            wrapLine(current);
            current.clear();
            continue;
        }
        if (ch == '\t')
        {
            current.push_back(' ');
            current.push_back(' ');
            current.push_back(' ');
            current.push_back(' ');
            continue;
        }
        current.push_back(ch);
    }
    wrapLine(current);

    int totalLines = static_cast<int>(layout.wrappedLines.size());
    layout.pageCount = std::max(1, (totalLines + layout.linesPerPage - 1) / layout.linesPerPage);
    int effectiveLineWidth = maxObservedLineWidth > 0 ? maxObservedLineWidth : maxLineWidthPx;
    int basePageWidth = std::max(targetWidth, contentWidthPx + marginX * 2);
    layout.pageWidth = std::max(basePageWidth, effectiveLineWidth + marginX * 2);
    int heightFromLines = layout.linesPerPage * layout.lineHeight + marginY * 2;
    layout.pageHeight = std::max(targetHeight, heightFromLines);

    return layout;
}

const TextDocument::Layout& TextDocument::ensureLayoutForSize(int fontSize)
{
    auto it = m_layoutCache.find(fontSize);
    if (it != m_layoutCache.end())
    {
        m_pageCount = it->second.pageCount;
        return it->second;
    }

    Layout layout = buildLayoutForSize(fontSize);
    m_pageCount = layout.pageCount;
    auto [insertIt, _] = m_layoutCache.emplace(fontSize, std::move(layout));
    clearCaches(); // invalidate cached renders when layout changes
    m_lastLayoutFontSizeUsed = fontSize;
    return insertIt->second;
}

int TextDocument::computePageWidth(const Layout& layout) const
{
    if (layout.pageWidth > 0)
    {
        return layout.pageWidth;
    }

    int marginX = std::max(8, layout.charWidth / 2);
    return std::max(1, layout.charsPerLine * layout.charWidth + marginX * 2);
}

int TextDocument::computePageHeight(const Layout& layout) const
{
    if (layout.pageHeight > 0)
    {
        return layout.pageHeight;
    }

    int marginY = std::max(8, layout.lineHeight / 4);
    return std::max(1, layout.linesPerPage * layout.lineHeight + marginY * 2);
}

int TextDocument::getPageWidthNative(int /*pageNum*/)
{
    return computePageWidth(ensureLayoutForSize(m_baseFontSize));
}

int TextDocument::getPageHeightNative(int /*pageNum*/)
{
    return computePageHeight(ensureLayoutForSize(m_baseFontSize));
}

std::pair<int, int> TextDocument::getPageDimensionsForScale(int scale)
{
    int fontSize = std::max(8, m_baseFontSize * scale / 100);
    const Layout& layout = ensureLayoutForSize(fontSize);
    return {layout.pageWidth, layout.pageHeight};
}

bool TextDocument::tryGetCachedPageARGB(int pageNumber, int scale, ArgbBufferPtr& buffer, int& width, int& height)
{
    auto key = std::make_pair(pageNumber, scale);
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_argbCache.find(key);
    if (it == m_argbCache.end())
    {
        return false;
    }

    auto& [buf, w, h] = it->second;
    buffer = buf;
    width = w;
    height = h;
    return static_cast<bool>(buffer);
}

TextDocument::ArgbBufferPtr TextDocument::renderPageARGB(int pageNumber, int& width, int& height, int scale)
{
    if (pageNumber < 0)
    {
        throw std::runtime_error("Invalid page number: " + std::to_string(pageNumber));
    }

    int fontSize = std::max(8, m_baseFontSize * scale / 100);
    const Layout& layout = ensureLayoutForSize(fontSize);
    m_pageCount = layout.pageCount;

    if (fontSize != m_lastLayoutFontSizeUsed)
    {
        clearCaches();
        m_lastLayoutFontSizeUsed = fontSize;
    }

    if (layout.pageCount <= 0)
    {
        throw std::runtime_error("TextDocument has no pages");
    }

    if (pageNumber >= layout.pageCount)
    {
        pageNumber = layout.pageCount - 1;
    }

    if (fontSize != m_lastLayoutFontSizeUsed)
    {
        clearCaches();
        m_lastLayoutFontSizeUsed = fontSize;
    }

    auto key = std::make_pair(pageNumber, scale);
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        auto it = m_argbCache.find(key);
        if (it != m_argbCache.end())
        {
            auto& [buf, w, h] = it->second;
            width = w;
            height = h;
            return buf;
        }
    }

    if (!ensureFont(fontSize))
    {
        throw std::runtime_error("Failed to load font for text rendering");
    }

    TTF_Font* font = m_fontCache[fontSize].get();
    int marginX = std::max(8, layout.charWidth / 2);
    int marginY = std::max(8, layout.lineHeight / 4);

    width = layout.pageWidth;
    height = layout.pageHeight;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface)
    {
        throw std::runtime_error(std::string("TextDocument: failed to allocate surface: ") + SDL_GetError());
    }

    SDL_FillRect(surface, nullptr, SDL_MapRGB(surface->format, m_bgR, m_bgG, m_bgB));

    SDL_Color textColor{};
    bool darkBg = computeLuminance(m_bgR, m_bgG, m_bgB) < 128;
    textColor.r = textColor.g = textColor.b = darkBg ? 245 : 15;
    textColor.a = 255;

    int startLine = pageNumber * layout.linesPerPage;
    int endLine = std::min(static_cast<int>(layout.wrappedLines.size()), startLine + layout.linesPerPage);

    int y = marginY;
    for (int i = startLine; i < endLine; ++i)
    {
        const std::string& line = layout.wrappedLines[static_cast<size_t>(i)];
        SDL_Surface* lineSurf = TTF_RenderUTF8_Blended(font, line.c_str(), textColor);
        if (!lineSurf)
        {
            y += layout.lineHeight;
            continue;
        }

        SDL_Rect dest{};
        dest.x = marginX;
        dest.y = y;
        SDL_BlitSurface(lineSurf, nullptr, surface, &dest);
        y += lineSurf->h > 0 ? lineSurf->h : layout.lineHeight;

        SDL_FreeSurface(lineSurf);
    }

    std::vector<uint32_t> argb(static_cast<size_t>(width) * static_cast<size_t>(height));
    uint8_t* pixels = static_cast<uint8_t*>(surface->pixels);
    int pitch = surface->pitch;
    SDL_PixelFormat* fmt = surface->format;

    for (int row = 0; row < height; ++row)
    {
        uint8_t* src = pixels + static_cast<size_t>(row) * pitch;
        for (int col = 0; col < width; ++col)
        {
            uint32_t pixel = 0;
            memcpy(&pixel, src + col * 4, sizeof(uint32_t));
            uint8_t r, g, b, a;
            SDL_GetRGBA(pixel, fmt, &r, &g, &b, &a);
            argb[static_cast<size_t>(row) * width + col] = (static_cast<uint32_t>(a) << 24) |
                                                           (static_cast<uint32_t>(r) << 16) |
                                                           (static_cast<uint32_t>(g) << 8) |
                                                           static_cast<uint32_t>(b);
        }
    }

    SDL_FreeSurface(surface);

    auto bufferPtr = std::make_shared<std::vector<uint32_t>>(std::move(argb));
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        if (m_argbCache.size() >= 5)
        {
            m_argbCache.erase(m_argbCache.begin());
        }
        m_argbCache[key] = std::make_tuple(bufferPtr, width, height);
    }

    return bufferPtr;
}

std::vector<uint8_t> TextDocument::renderPage(int pageNum, int& outWidth, int& outHeight, int scale)
{
    auto argb = renderPageARGB(pageNum, outWidth, outHeight, scale);
    std::vector<uint8_t> rgb(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 3);
    if (argb)
    {
        for (int i = 0; i < outWidth * outHeight; ++i)
        {
            uint32_t px = (*argb)[static_cast<size_t>(i)];
            rgb[static_cast<size_t>(i) * 3] = static_cast<uint8_t>((px >> 16) & 0xFF);
            rgb[static_cast<size_t>(i) * 3 + 1] = static_cast<uint8_t>((px >> 8) & 0xFF);
            rgb[static_cast<size_t>(i) * 3 + 2] = static_cast<uint8_t>(px & 0xFF);
        }
    }
    return rgb;
}

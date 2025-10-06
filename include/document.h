#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <cstdint>
#include <string>
#include <vector>

// Helper function: Converts 24-bit RGB (R, G, B) to 32-bit ARGB (A, R, G, B)
// Assumes input is R G B byte order and output should be 0xAARRGGBB
inline unsigned int rgb24_to_argb32(uint8_t r, uint8_t g, uint8_t b)
{
    return (0xFF000000 | (static_cast<unsigned int>(r) << 16) | (static_cast<unsigned int>(g) << 8) | static_cast<unsigned int>(b));
}

class Document
{
public:
    // Lifecycle
    virtual bool open(const std::string& filename) = 0;
    virtual void close() = 0;

    virtual int getPageCount() const = 0;

    // Renders a specific page and returns RGB24 pixel data (3 bytes per pixel).
    // outWidth and outHeight will be set to the dimensions of the rendered page.
    // scale is a percentage (e.g., 100 for 100% original size).
    virtual std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) = 0;

    // New: Get the native (unscaled) width and height of a specific page.
    virtual int getPageWidthNative(int pageNum) = 0;
    virtual int getPageHeightNative(int pageNum) = 0;

    virtual ~Document() = default;
};

#endif // DOCUMENT_H

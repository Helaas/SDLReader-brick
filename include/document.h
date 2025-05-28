#pragma once

#include <vector>
#include <string>
#include <cstdint> // For uint8_t

// Forward declarations to avoid including heavy headers in this base class header
// although unique_ptr needs to be included for the smart pointers.
// In this case, Document interface doesn't need specific fz_context or ddjvu_context
// but implementers will.

// Helper function: Converts 24-bit RGB (R, G, B) to 32-bit ARGB (A, R, G, B)
// Assumes input is R G B byte order and output should be 0xAARRGGBB
inline unsigned int rgb24_to_argb32(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFF000000 | (static_cast<unsigned int>(r) << 16) | (static_cast<unsigned int>(g) << 8) | static_cast<unsigned int>(b));
}

// --- Document Abstract Base Class ---
class Document {
public:
    // Opens a document from the given filename. Returns true on success.
    virtual bool open(const std::string& filename) = 0;

    // Returns the total number of pages in the document.
    virtual int getPageCount() const = 0;

    // Renders a specific page and returns RGB24 pixel data (3 bytes per pixel).
    // outWidth and outHeight will be set to the dimensions of the rendered page.
    // scale is a percentage (e.g., 100 for 100% original size).
    virtual std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) = 0;

    // Virtual destructor to ensure proper cleanup of derived classes.
    virtual ~Document() = default;
};

#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <vector>
#include <string>

class Document {
public:
    virtual ~Document() = default;

    // Lifecycle
    virtual bool open(const std::string& path) = 0;
    virtual void close() = 0;

    // Document info
    virtual int getPageCount() const = 0;

    // Rendering
    virtual std::vector<unsigned char> renderPage(int page, int& width, int& height, int scale) = 0;
    virtual int getPageWidthNative(int page) = 0;
    virtual int getPageHeightNative(int page) = 0;
};

#endif // DOCUMENT_H

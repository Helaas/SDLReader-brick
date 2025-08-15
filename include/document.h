#ifndef DOCUMENT_H
#define DOCUMENT_H

#include <vector>
#include <string>

class Document {
public:
    virtual ~Document() = default;  // Only one destructor

    virtual std::vector<unsigned char> renderPage(int page, int& width, int& height, int scale) = 0;
    virtual int getPageWidthNative(int page) = 0;
    virtual int getPageHeightNative(int page) = 0;

    // Add other pure virtual methods as needed
};

#endif // DOCUMENT_H

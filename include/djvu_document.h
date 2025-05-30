#ifndef DJVU_DOCUMENT_H
#define DJVU_DOCUMENT_H

#include "document.h"
#include <libdjvu/ddjvuapi.h>
#include <memory>
#include <string>
#include <vector>

// Custom deleter for ddjvu_context
struct DdjvuContextDeleter {
    void operator()(ddjvu_context_t* ctx) const;
};

// Custom deleter for ddjvu_document
struct DdjvuDocumentDeleter {
    void operator()(ddjvu_document_t* doc) const;
};

class DjvuDocument : public Document {
public:
    DjvuDocument();
    ~DjvuDocument() override = default;

    bool open(const std::string& filename) override;
    int getPageCount() const override;
    int getPageWidthNative(int pageNum) override;
    int getPageHeightNative(int pageNum) override;

    // Corrected declaration for renderPage to match Document base class
    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override;

private:
    std::unique_ptr<ddjvu_context_t, DdjvuContextDeleter> m_ctx;
    std::unique_ptr<ddjvu_document_t, DdjvuDocumentDeleter> m_doc;

    void processDjvuMessages();
};

#endif // DJVU_DOCUMENT_H
#pragma once

#include "document.h"
#include <memory>


typedef struct ddjvu_context_s ddjvu_context_t;
typedef struct ddjvu_document_s ddjvu_document_t;
typedef struct ddjvu_page_s ddjvu_page_t;
typedef struct ddjvu_rect_s ddjvu_rect_t;
typedef struct ddjvu_format_s ddjvu_format_t;

// Custom deleters for DjVuLibre resources
struct DdjvuContextDeleter {
    void operator()(ddjvu_context_t* ctx) const;
};

struct DdjvuDocumentDeleter {
    void operator()(ddjvu_document_t* doc) const;
};

// --- DjvuDocument Class (DjVuLibre Implementation) ---

class DjvuDocument : public Document {
public:
    DjvuDocument();

    bool open(const std::string& filename) override;

    int getPageCount() const override;

    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override;

private:
    std::unique_ptr<ddjvu_context_t, DdjvuContextDeleter> m_ctx;
    std::unique_ptr<ddjvu_document_t, DdjvuDocumentDeleter> m_doc;

    // Helper to process DjVu messages (errors, warnings, etc.)
    void processDjvuMessages();
};


#pragma once

#include "document.h" // Include the base Document interface
#include <memory>      // For std::unique_ptr

// Forward declarations from DjVuLibre
// Removed forward declaration for ddjvu_message_s to avoid redefinition error.
// The actual definition will come from <libdjvu/ddjvuapi.h> in the .cpp file.
typedef struct ddjvu_context_s ddjvu_context_t;
typedef struct ddjvu_document_s ddjvu_document_t;
typedef struct ddjvu_page_s ddjvu_page_t;
// typedef struct ddjvu_message_s ddjvu_message_t; // Removed
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
// Concrete implementation of the Document interface for DjVu files using DjVuLibre.
class DjvuDocument : public Document {
public:
    // Constructor initializes member pointers to nullptr.
    DjvuDocument();

    // No custom destructor needed here, unique_ptr handles resource release.
    // The base class virtual destructor will correctly call unique_ptr destructors.

    // Implements the open method from the Document interface.
    bool open(const std::string& filename) override;

    // Implements the getPageCount method from the Document interface.
    int getPageCount() const override;

    // Implements the renderPage method from the Document interface.
    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override;

private:
    // Smart pointers for DjVuLibre context and document, using custom deleters.
    std::unique_ptr<ddjvu_context_t, DdjvuContextDeleter> m_ctx;
    std::unique_ptr<ddjvu_document_t, DdjvuDocumentDeleter> m_doc;

    // Helper to process DjVu messages (errors, warnings, etc.)
    void processDjvuMessages();
};


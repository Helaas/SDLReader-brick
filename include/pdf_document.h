#pragma once

#include "document.h" // Include the base Document interface
#include <memory>      // For std::unique_ptr


typedef struct fz_context fz_context;
typedef struct fz_document fz_document;
typedef struct fz_page fz_page;
typedef struct fz_pixmap fz_pixmap;



struct FzContextDeleter {
    void operator()(fz_context* ctx) const; // Definition in .cpp
};

// --- PdfDocument Class (MuPDF Implementation) ---
class PdfDocument : public Document {
public:

    PdfDocument();

    ~PdfDocument() override;

    bool open(const std::string& filename) override;

    int getPageCount() const override;

    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override;

private:
    std::unique_ptr<fz_context, FzContextDeleter> m_ctx;
    // Raw pointer for MuPDF document. Managed manually in destructor because
    // fz_drop_document requires the fz_context, which unique_ptr would drop first.
    fz_document* m_doc;
};

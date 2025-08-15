#ifndef PDF_DOCUMENT_H
#define PDF_DOCUMENT_H

#include "document.h"
#include <mupdf/fitz.h>
#include <vector>

class PdfDocument : public Document {
public:
    PdfDocument();
    ~PdfDocument() override = default;  // Ensure override
    std::vector<unsigned char> renderPage(int page, int& width, int& height, int scale) override;
    int getPageWidthNative(int page) override;
    int getPageHeightNative(int page) override;

private:
    // Use smart pointers to manage MuPDF types safely
    struct ContextDeleter {
        void operator()(fz_context* ctx) const { if (ctx) fz_drop_context(ctx); }
    };
    struct DocumentDeleter {
        fz_context* ctx;
        void operator()(fz_document* doc) const { if (doc) fz_drop_document(ctx, doc); }
    };
    struct PixmapDeleter {
        fz_context* ctx;
        void operator()(fz_pixmap* pix) const { if (pix) fz_drop_pixmap(ctx, pix); }
    };

    std::unique_ptr<fz_context, ContextDeleter> m_ctx;
    std::unique_ptr<fz_document, DocumentDeleter> m_doc;
};

#endif // PDF_DOCUMENT_H

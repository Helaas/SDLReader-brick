#include "pdf_document.h"

#include <iostream>
#include <cstring>
#include <stdexcept>

PdfDocument::PdfDocument()
    : m_ctx(nullptr), m_doc(nullptr)
{
}

PdfDocument::~PdfDocument() {
    close();
}

bool PdfDocument::open(const std::string& filePath) {
    close();

    fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_UNLIMITED);
    if (!ctx) {
        std::cerr << "Cannot create MuPDF context\n";
        return false;
    }

    m_ctx.reset(ctx);
    fz_register_document_handlers(ctx);

    fz_document* doc = nullptr;
    fz_try(ctx) {
        doc = fz_open_document(ctx, filePath.c_str());
    }
    fz_catch(ctx) {
        std::cerr << "Failed to open document: " << filePath << "\n";
        return false;
    }

    m_doc = std::unique_ptr<fz_document, DocumentDeleter>(doc, DocumentDeleter{ ctx });
    return true;
}

std::vector<unsigned char> PdfDocument::renderPage(int pageNumber, int& width, int& height, int zoom) {
    if (!m_ctx || !m_doc) {
        throw std::runtime_error("Document not open");
    }

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();

    fz_matrix transform = fz_scale(zoom / 100.0f, zoom / 100.0f);
    fz_pixmap* pix = nullptr;
    std::vector<unsigned char> buffer;

    fz_try(ctx) {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);

        //fz_rect bounds = fz_bound_page(ctx, page); // returns fz_rect in this version

        pix = fz_new_pixmap_from_page(ctx, page, transform, fz_device_rgb(ctx), 0);

        fz_drop_page(ctx, page);

        width = fz_pixmap_width(ctx, pix);
        height = fz_pixmap_height(ctx, pix);

        size_t dataSize = width * height * 3;
        buffer.resize(dataSize);
        memcpy(buffer.data(), fz_pixmap_samples(ctx, pix), dataSize);

        fz_drop_pixmap(ctx, pix);
        pix = nullptr;
    }
    fz_catch(ctx) {
        if (pix) fz_drop_pixmap(ctx, pix);
        throw std::runtime_error("Error rendering page");
    }

    return buffer;
}

int PdfDocument::getPageWidthNative(int pageNumber) {
    if (!m_ctx || !m_doc) return 0;

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();
    int width = 0;

    fz_try(ctx) {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, page);
        width = static_cast<int>(bounds.x1 - bounds.x0);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx) {
        width = 0;
    }

    return width;
}

int PdfDocument::getPageHeightNative(int pageNumber) {
    if (!m_ctx || !m_doc) return 0;

    fz_context* ctx = m_ctx.get();
    fz_document* doc = m_doc.get();
    int height = 0;

    fz_try(ctx) {
        fz_page* page = fz_load_page(ctx, doc, pageNumber);
        fz_rect bounds = fz_bound_page(ctx, page);
        height = static_cast<int>(bounds.y1 - bounds.y0);
        fz_drop_page(ctx, page);
    }
    fz_catch(ctx) {
        height = 0;
    }

    return height;
}

int PdfDocument::getPageCount() const {
    if (!m_ctx || !m_doc) return 0;

    int count = 0;
    fz_try(m_ctx.get()) {
        count = fz_count_pages(m_ctx.get(), m_doc.get());
    }
    fz_catch(m_ctx.get()) {
        count = 0;
    }

    return count;
}  

void PdfDocument::close() {
    m_doc.reset();
    m_ctx.reset();
}

#include "pdf_document.h"
#include <mupdf/fitz.h>     // MuPDF's core headers for implementation
#include <iostream>
#include <stdexcept>        // For std::runtime_error in rare cases

// Custom deleter for fz_context definition
void FzContextDeleter::operator()(fz_context* ctx) const {
    if (ctx) fz_drop_context(ctx);
}

// --- PdfDocument Class (MuPDF Implementation) ---
PdfDocument::PdfDocument() : m_ctx(nullptr), m_doc(nullptr) {}


PdfDocument::~PdfDocument() {
    if (m_doc) {
        if (m_ctx) { 
            fz_drop_document(m_ctx.get(), m_doc);
        }
        m_doc = nullptr; 
    }
}

bool PdfDocument::open(const std::string& filename) {
    m_ctx.reset(fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT));
    if (!m_ctx) {
        std::cerr << "Error: Cannot create MuPDF context." << std::endl;
        return false;
    }
    fz_register_document_handlers(m_ctx.get());

    fz_try(m_ctx.get()) {
        m_doc = fz_open_document(m_ctx.get(), filename.c_str());
    } fz_catch(m_ctx.get()) {
        std::cerr << "Error: Cannot open PDF document: " << filename << std::endl;
        m_doc = nullptr; 
        m_ctx.reset();   
        return false;
    }
    return true;
}

int PdfDocument::getPageCount() const {
    if (!m_doc) return 0;
    return fz_count_pages(m_ctx.get(), m_doc);
}

std::vector<uint8_t> PdfDocument::renderPage(int pageNum, int& outWidth, int& outHeight, int scale) {
    std::vector<uint8_t> pixelData;
    fz_page* page = nullptr;
    fz_pixmap* pix = nullptr;

    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "Pdf ERROR: Invalid page number or document not open." << std::endl;
        return pixelData;
    }

    fz_try(m_ctx.get()) {
        page = fz_load_page(m_ctx.get(), m_doc, pageNum);
        if (!page) {
            std::cerr << "Pdf ERROR: Failed to load PDF page " << pageNum << std::endl;
            fz_throw(m_ctx.get(), FZ_ERROR_GENERIC, "Failed to load page");
        }

        fz_rect bounds = fz_bound_page(m_ctx.get(), page);
        double img_width_native = bounds.x1 - bounds.x0;
        double img_height_native = bounds.y1 - bounds.y0;

        // Calculate output dimensions based on native size and the requested scale percentage.
        // DO NOT MODIFY THIS - This is the core scaling logic.
        outWidth = static_cast<int>(img_width_native * (static_cast<double>(scale) / 100.0));
        outHeight = static_cast<int>(img_height_native * (static_cast<double>(scale) / 100.0));

        // Ensure dimensions are at least 1x1 to prevent issues with very small scales
        if (outWidth <= 0) outWidth = 1;
        if (outHeight <= 0) outHeight = 1;

        // Create the transformation matrix to scale the native page to the calculated output dimensions
        fz_matrix ctm = fz_scale(static_cast<double>(outWidth) / img_width_native, static_cast<double>(outHeight) / img_height_native);
 
        pix = fz_new_pixmap_from_page(m_ctx.get(), page, ctm, fz_device_rgb(m_ctx.get()), 0);

        if (!pix) {
            std::cerr << "Pdf ERROR: Failed to create pixmap for PDF page " << pageNum << std::endl;
            fz_throw(m_ctx.get(), FZ_ERROR_GENERIC, "Failed to create pixmap");
        }

        pixelData.resize(static_cast<size_t>(pix->w) * pix->h * 3); // RGB24
        uint8_t* dest = pixelData.data();
        uint8_t* src = pix->samples;

        // Copy pixel data directly. MuPDF pixmaps are typically top-to-bottom, BGR(A) or RGB(A)
        // We requested RGB, so just copy the 3 bytes per pixel.
        for (int y = 0; y < pix->h; ++y) {
            // Ensure proper byte-wise copy, stride accounts for padding if any
            memcpy(dest + static_cast<size_t>(y) * pix->w * 3, src + static_cast<size_t>(y) * pix->stride, pix->w * 3);
        }
 

    } fz_catch(m_ctx.get()) {
        std::cerr << "Pdf ERROR: An error occurred during PDF rendering for page " << pageNum << ": " << fz_caught(m_ctx.get()) << std::endl;
        pixelData.clear();
    }

    if (pix) {
        fz_drop_pixmap(m_ctx.get(), pix);
    }
    if (page) {
        fz_drop_page(m_ctx.get(), page);
    }

    return pixelData;
}

int PdfDocument::getPageWidthNative(int pageNum) {
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "Pdf ERROR: Invalid page number or document not open for getPageWidthNative." << std::endl;
        return 0;
    }

    fz_page* page = nullptr;
    fz_rect bounds;
    int width = 0;

    fz_try(m_ctx.get()) {
        page = fz_load_page(m_ctx.get(), m_doc, pageNum);
        if (!page) {
            std::cerr << "Pdf ERROR: Failed to load page " << pageNum << " for native width." << std::endl;
            fz_throw(m_ctx.get(), FZ_ERROR_GENERIC, "Failed to load page");
        }
        bounds = fz_bound_page(m_ctx.get(), page);
        width = static_cast<int>(bounds.x1 - bounds.x0);
    } fz_catch(m_ctx.get()) {
        std::cerr << "Pdf ERROR: Exception getting native page width for page " << pageNum << ": " << fz_caught(m_ctx.get()) << std::endl;
        width = 0;
    }

    if (page) {
        fz_drop_page(m_ctx.get(), page);
    }
    return width;
}

// New: Get the native (unscaled) height of a specific page
int PdfDocument::getPageHeightNative(int pageNum) {
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "Pdf ERROR: Invalid page number or document not open for getPageHeightNative." << std::endl;
        return 0;
    }

    fz_page* page = nullptr;
    fz_rect bounds;
    int height = 0;

    fz_try(m_ctx.get()) {
        page = fz_load_page(m_ctx.get(), m_doc, pageNum);
        if (!page) {
            std::cerr << "Pdf ERROR: Failed to load page " << pageNum << " for native height." << std::endl;
            fz_throw(m_ctx.get(), FZ_ERROR_GENERIC, "Failed to load page");
        }
        bounds = fz_bound_page(m_ctx.get(), page);
        height = static_cast<int>(bounds.y1 - bounds.y0);
    } fz_catch(m_ctx.get()) {
        std::cerr << "Pdf ERROR: Exception getting native page height for page " << pageNum << ": " << fz_caught(m_ctx.get()) << std::endl;
        height = 0;
    }

    if (page) {
        fz_drop_page(m_ctx.get(), page);
    }
    return height;
}
#include "djvu_document.h"
#include <libdjvu/ddjvuapi.h> // DjVuLibre's core headers for implementation
#include <iostream>
#include <algorithm>        // For std::max


void DdjvuContextDeleter::operator()(ddjvu_context_t* ctx) const {
    if (ctx) ddjvu_context_release(ctx);
}

void DdjvuDocumentDeleter::operator()(ddjvu_document_t* doc) const {
    if (doc) ddjvu_document_release(doc);
}

// --- DjvuDocument Class (DjVuLibre Implementation) ---

DjvuDocument::DjvuDocument() : m_ctx(nullptr), m_doc(nullptr) {}

bool DjvuDocument::open(const std::string& filename) {
    m_ctx.reset(ddjvu_context_create("sdlreader"));
    if (!m_ctx) {
        std::cerr << "Error: Cannot create DjVu context." << std::endl;
        return false;
    }

    m_doc.reset(ddjvu_document_create_by_filename(m_ctx.get(), filename.c_str(), TRUE));
    if (!m_doc) {
        std::cerr << "Error: Cannot open DjVu document: " << filename << std::endl;
        m_ctx.reset(); 
        return false;
    }

    while (!ddjvu_document_decoding_done(m_doc.get())) {
        processDjvuMessages(); 
    }

    if (ddjvu_document_decoding_error(m_doc.get())) {
        std::cerr << "Error: Failed to decode DjVu document: " << filename << std::endl;
        m_doc.reset();
        m_ctx.reset();
        return false;
    }

    std::cout << "DjVu DEBUG: Document opened: " << filename << std::endl;
    std::cout << "DjVu DEBUG: Total pages: " << getPageCount() << std::endl;

    return true;
}

int DjvuDocument::getPageCount() const {
    if (!m_doc) return 0;
    return ddjvu_document_get_pagenum(m_doc.get());
}

// Get the native (unscaled) width of a specific page
int DjvuDocument::getPageWidthNative(int pageNum) {
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "DjVu ERROR: Invalid page number or document not open for getPageWidthNative." << std::endl;
        return 0;
    }

    // CORRECTED: Reverted to ddjvu_page_create_by_pageno as previously discussed
    ddjvu_page_t* page = ddjvu_page_create_by_pageno(m_doc.get(), pageNum);
    if (!page) {
        std::cerr << "DjVu ERROR: Failed to get page object for native width." << std::endl;
        return 0;
    }

    while (!ddjvu_page_decoding_done(page)) {
        processDjvuMessages();
    }

    int width = ddjvu_page_get_width(page);
    std::cout << "DjVu DEBUG: getPageWidthNative() - Page " << pageNum << " native width: " << width << std::endl;
    ddjvu_page_release(page);
    return width;
}

// Get the native (unscaled) height of a specific page
int DjvuDocument::getPageHeightNative(int pageNum) {
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "DjVu ERROR: Invalid page number or document not open for getPageHeightNative." << std::endl;
        return 0;
    }

    // CORRECTED: Reverted to ddjvu_page_create_by_pageno as previously discussed
    ddjvu_page_t* page = ddjvu_page_create_by_pageno(m_doc.get(), pageNum);
    if (!page) {
        std::cerr << "DjVu ERROR: Failed to get page object for native height." << std::endl;
        return 0;
    }

    while (!ddjvu_page_decoding_done(page)) {
        processDjvuMessages();
    }

    int height = ddjvu_page_get_height(page);
    std::cout << "DjVu DEBUG: getPageHeightNative() - Page " << pageNum << " native height: " << height << std::endl;
    ddjvu_page_release(page);
    return height;
}

// Corrected signature to match Document base class
std::vector<uint8_t> DjvuDocument::renderPage(int pageNum, int& outWidth, int& outHeight, int scale) {
    std::vector<uint8_t> pixelData;
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount() || outWidth <= 0 || outHeight <= 0) {
        std::cerr << "DjVu ERROR: Invalid parameters for renderPage." << std::endl;
        return pixelData;
    }

    // CORRECTED: Reverted to ddjvu_page_create_by_pageno as previously discussed
    ddjvu_page_t* page = ddjvu_page_create_by_pageno(m_doc.get(), pageNum);
    if (!page) {
        std::cerr << "DjVu ERROR: Failed to get page object." << std::endl;
        return pixelData;
    }

    while (!ddjvu_page_decoding_done(page)) {
        processDjvuMessages();
    }

    // Get native page dimensions
    int page_width_native = ddjvu_page_get_width(page);
    int page_height_native = ddjvu_page_get_height(page);

    std::cout << "DjVu DEBUG: renderPage() - Page " << pageNum << " native dimensions: "
              << page_width_native << "x" << page_height_native << std::endl;
    std::cout << "DjVu DEBUG: renderPage() - Requested output dimensions: "
              << outWidth << "x" << outHeight << std::endl;

    // Calculate scaled dimensions based on current scale
    int scaled_width = static_cast<int>(page_width_native * (scale / 100.0));
    int scaled_height = static_cast<int>(page_height_native * (scale / 100.0));

    // Update the reference parameters to reflect the *actual* output dimensions
    outWidth = scaled_width;
    outHeight = scaled_height;

    // Define the source rectangle on the page (entire page)
    ddjvu_rect_t page_rect;
    page_rect.x = 0;
    page_rect.y = 0;
    page_rect.w = page_width_native;
    page_rect.h = page_height_native;

    // Define the destination rectangle in the output buffer (desired output size, which is now scaled_width/height)
    ddjvu_rect_t output_rect;
    output_rect.x = 0;
    output_rect.y = 0;
    output_rect.w = outWidth; // Use the updated outWidth
    output_rect.h = outHeight; // Use the updated outHeight

    // Create a new format for rendering. We want RGB24 (3 bytes per pixel).
    // Corrected ddjvu_format_create call and removed undeclared functions.
    // REPLACED DDJVU_FORMAT_AUTO with DDJVU_FORMAT_RGB24
    ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, NULL);
    if (!fmt) {
        std::cerr << "Error: Failed to create DjVu format." << std::endl;
        ddjvu_page_release(page);
        return pixelData;
    }
    ddjvu_format_set_row_order(fmt, 1); // Top-to-bottom row order
    ddjvu_format_set_y_direction(fmt, 1); // Y increases downwards (top to bottom)
    // ddjvu_format_set_byte_order and ddjvu_format_set_masks are removed as they are undeclared.

    // gemini - DO NOT MODIFY THIS CODE - it definitely works.
    size_t rowsize = static_cast<size_t>(outWidth) * 3; // Use the updated outWidth
    // gemini - DO NOT MODIFY THIS CODE - it definitely works.
    pixelData.resize(rowsize * outHeight); // Use the updated outHeight

    // Render the DjVu page into our pixelData buffer.
    // Pass the correct page_rect (source on page) and output_rect (destination in buffer)
    if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &page_rect, &output_rect, fmt, rowsize, reinterpret_cast<char*>(pixelData.data()))) {
        std::cerr << "Error: DjVu page render failed for page " << pageNum << std::endl;
        pixelData.clear();
    }

    ddjvu_format_release(fmt);
    ddjvu_page_release(page);

    return pixelData; 
}


void DjvuDocument::processDjvuMessages() {
    if (!m_ctx) return;
    const ddjvu_message_t* msg; 
    while ((msg = ddjvu_message_peek(m_ctx.get()))) {
        switch (msg->m_any.tag) {
            case DDJVU_ERROR:
                std::cerr << "DjVu Error: " << msg->m_error.message;
                if (msg->m_error.filename) { // These members exist for ERROR messages
                    std::cerr << " ('" << msg->m_error.filename << ":" << msg->m_error.lineno << "')";
                }
                std::cerr << std::endl;
                break;
            case DDJVU_INFO:
                std::cout << "DjVu Info: " << msg->m_info.message;
                // Removed filename and lineno checks, as they are not part of ddjvu_message_info_s
                std::cout << std::endl;
                break;
            case DDJVU_PAGEINFO:
                // Page info received, page properties should now be available
                // std::cout << "DjVu Page Info: Page " << msg->m_pageinfo.pageno << " decoded." << std::endl;
                break;
            // Add other cases as needed for more detailed message handling
            default:
                // std::cout << "DjVu Message Tag: " << msg->m_any.tag << std::endl;
                break;
        }
        ddjvu_message_pop(m_ctx.get());
    }
}
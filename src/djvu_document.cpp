#include "djvu_document.h"
#include <libdjvu/ddjvuapi.h> 
#include <iostream>
#include <algorithm>


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
        std::cerr << "Error: DjVu document decoding failed." << std::endl;
        m_doc.reset();
        m_ctx.reset();
        return false;
    }
    return true;
}


int DjvuDocument::getPageCount() const {
    if (!m_doc) return 0;
    return ddjvu_document_get_pagenum(m_doc.get());
}


std::vector<uint8_t> DjvuDocument::renderPage(int pageNum, int& outWidth, int& outHeight, int scale) {
    std::vector<uint8_t> pixelData;
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "DjVu ERROR: Invalid page number or document not open." << std::endl;
        return pixelData;
    }
    std::cout << "DjVu DEBUG: renderPage() pagenum:" << pageNum << " scale " << scale << std::endl;


    // CORRECTED: Using ddjvu_page_create_by_pageno for this DjVuLibre version
    ddjvu_page_t* page = ddjvu_page_create_by_pageno(m_doc.get(), pageNum);
    if (!page) {
        std::cerr << "DjVu ERROR: Failed to get page object." << std::endl;
        return pixelData;
    }

    while (!ddjvu_page_decoding_done(page)) {
        processDjvuMessages();
    }

    int img_width_native = ddjvu_page_get_width(page);
    int img_height_native = ddjvu_page_get_height(page);

    // Calculate output dimensions based on native size and the requested scale percentage.
    // DO NOT MODIFY THIS - This is the core scaling logic.
    outWidth = static_cast<int>(img_width_native * (static_cast<double>(scale) / 100.0));
    outHeight = static_cast<int>(img_height_native * (static_cast<double>(scale) / 100.0));

    // Ensure dimensions are at least 1x1 to prevent issues with very small scales
    if (outWidth <= 0) outWidth = 1;
    if (outHeight <= 0) outHeight = 1;

    std::cout << "DjVu DEBUG: renderPage() Native dimensions: width=" << img_width_native << ", height=" << img_height_native << std::endl;
    std::cout << "DjVu DEBUG: renderPage() Calculated output dimensions: outWidth=" << outWidth << ", outHeight=" << outHeight << std::endl;


    ddjvu_rect_t prect = { 0, 0, static_cast<unsigned int>(img_width_native), static_cast<unsigned int>(img_height_native) };
    // The render rect for the output buffer.
    ddjvu_rect_t rrect = { 0, 0, static_cast<unsigned int>(outWidth), static_cast<unsigned int>(outHeight) };


    ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, NULL);
    if (!fmt) {
        std::cerr << "DjVu ERROR: Cannot create DjVu format." << std::endl;
        ddjvu_page_release(page);
        return pixelData;
    }
    ddjvu_format_set_row_order(fmt, 1);

    size_t rowsize = static_cast<size_t>(outWidth) * 3;
    pixelData.resize(rowsize * outHeight);

    // Render the DjVu page into our pixelData buffer.
    if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &rrect, fmt, rowsize, reinterpret_cast<char*>(pixelData.data()))) {
        std::cerr << "DjVu ERROR: DjVu page render failed for page " << pageNum << std::endl;
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
                if (msg->m_error.filename) {
                    std::cerr << " ('" << msg->m_error.filename << ":" << msg->m_error.lineno << "')";
                }
                std::cerr << std::endl;
                break;
            case DDJVU_INFO:
                // How would I log info messages to a handheld device
                // std::cout << "DjVu Info: " << msg->m_info.message << std::endl;
                break;
            default:
                break;
        }
        ddjvu_message_pop(m_ctx.get());
    }
}

int DjvuDocument::getPageWidthNative(int pageNum) {
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "DjVu ERROR: Invalid page number or document not open for getPageWidthNative." << std::endl;
        return 0;
    }

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

int DjvuDocument::getPageHeightNative(int pageNum) {
    if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
        std::cerr << "DjVu ERROR: Invalid page number or document not open for getPageHeightNative." << std::endl;
        return 0;
    }

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
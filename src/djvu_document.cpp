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
        return pixelData;
    }

    ddjvu_page_t* page = ddjvu_page_create_by_pageno(m_doc.get(), pageNum);
    if (!page) {
        std::cerr << "Error: Cannot create DjVu page " << pageNum << std::endl;
        return pixelData;
    }

    while (!ddjvu_page_decoding_done(page)) {
        processDjvuMessages(); 
    }

    if (ddjvu_page_decoding_error(page)) {
        std::cerr << "Error: DjVu page decoding failed for page " << pageNum << std::endl;
        ddjvu_page_release(page); 
        return pixelData;
    }

    int img_width = ddjvu_page_get_width(page);
    int img_height = ddjvu_page_get_height(page);
    int dpi = ddjvu_page_get_resolution(page);


    outWidth = static_cast<int>(static_cast<double>(img_width) * scale / dpi);
    outHeight = static_cast<int>(static_cast<double>(img_height) * scale / dpi);


    ddjvu_rect_t prect = { 0, 0, (unsigned int)outWidth, (unsigned int)outHeight };
    ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, 0);
    if (!fmt) {
        std::cerr << "Error: Cannot create DjVu format." << std::endl;
        ddjvu_page_release(page);
        return pixelData;
    }
    ddjvu_format_set_row_order(fmt, 1);

    size_t rowsize = static_cast<size_t>(outWidth) * 3;
    pixelData.resize(rowsize * outHeight);

    // Render the DjVu page into our pixelData buffer.
    if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &prect, fmt, rowsize, reinterpret_cast<char*>(pixelData.data()))) {
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

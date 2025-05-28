#include <SDL.h>
#include <SDL_ttf.h>
#include <libdjvu/ddjvuapi.h> // For DjVu support
#include <mupdf/fitz.h>     // For PDF support
#include <iostream>
#include <vector>
#include <string>
#include <memory> // For std::unique_ptr
#include <algorithm> // For std::min/max

// --- SDL Resource Management (RAII) ---
// Custom deleters for SDL resources to use with std::unique_ptr
struct SDL_Window_Deleter {
    void operator()(SDL_Window* window) const {
        if (window) SDL_DestroyWindow(window);
    }
};

struct SDL_Renderer_Deleter {
    void operator()(SDL_Renderer* renderer) const {
        if (renderer) SDL_DestroyRenderer(renderer);
    }
};

struct SDL_Texture_Deleter {
    void operator()(SDL_Texture* texture) const {
        if (texture) SDL_DestroyTexture(texture);
    }
};

struct TTF_Font_Deleter {
    void operator()(TTF_Font* font) const {
        if (font) TTF_CloseFont(font);
    }
};

// Custom deleters for document library contexts
// MuPDF's fz_drop_document now requires the context, so we'll manage fz_document* manually
// and drop it in the PdfDocument destructor.
struct FzContextDeleter {
    void operator()(fz_context* ctx) const {
        if (ctx) fz_drop_context(ctx);
    }
};

// DjVuLibre deleters
struct DdjvuContextDeleter {
    void operator()(ddjvu_context_t* ctx) const {
        if (ctx) ddjvu_context_release(ctx);
    }
};

struct DdjvuDocumentDeleter {
    void operator()(ddjvu_document_t* doc) const {
        if (doc) ddjvu_document_release(doc);
    }
};

// --- Helper Functions ---

// Converts 24-bit RGB (R, G, B) to 32-bit ARGB (A, R, G, B)
// Assumes input is R G B byte order and output should be 0xAARRGGBB
inline unsigned int rgb24_to_argb32(uint8_t r, uint8_t g, uint8_t b) {
    return (0xFF000000 | (r << 16) | (g << 8) | b);
}

// --- Renderer Class ---
class Renderer {
public:
    // Constructor: Initializes SDL window, renderer, and texture
    Renderer(int width, int height, const std::string& title) {
        // Initialize SDL Video subsystem
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            throw std::runtime_error("SDL could not initialize! SDL_Error: " + std::string(SDL_GetError()));
        }

        // Create SDL Window
        m_window.reset(SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            width, // Use initial width
            height, // Use initial height
            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
        ));
        if (!m_window) {
            throw std::runtime_error("Window could not be created! SDL_Error: " + std::string(SDL_GetError()));
        }

        // Create SDL Renderer (hardware accelerated)
        m_renderer.reset(SDL_CreateRenderer(m_window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
        if (!m_renderer) {
            throw std::runtime_error("Renderer could not be created! SDL_Error: " + std::string(SDL_GetError()));
        }

        // Create streaming texture for page content
        // Initial texture size can be arbitrary, it will be resized in renderPage if needed
        m_texture.reset(SDL_CreateTexture(
            m_renderer.get(),
            SDL_PIXELFORMAT_ARGB8888, // Expecting ARGB format for the texture
            SDL_TEXTUREACCESS_STREAMING,
            width, // Initial texture width
            height // Initial texture height
        ));
        if (!m_texture) {
            throw std::runtime_error("Texture could not be created! SDL_Error: " + std::string(SDL_GetError()));
        }

        // Set initial draw color (e.g., white background)
        SDL_SetRenderDrawColor(m_renderer.get(), 255, 255, 255, 255);
        SDL_RenderClear(m_renderer.get());
        SDL_RenderPresent(m_renderer.get());
    }

    // Destructor: SDL resources are automatically destroyed by unique_ptr.
    // SDL_Quit() is now handled in main.
    ~Renderer() {
        // No SDL_Quit() here
    }

    // Clears the renderer with a specified color
    void clear(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
        SDL_SetRenderDrawColor(m_renderer.get(), r, g, b, a);
        SDL_RenderClear(m_renderer.get());
    }

    // Presents the rendered content to the screen
    void present() {
        SDL_RenderPresent(m_renderer.get());
    }

    // Renders page pixel data to the main texture and copies it to the renderer
    void renderPage(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight,
                    int destX, int destY, int destWidth, int destHeight) {
        if (pixelData.empty() || srcWidth <= 0 || srcHeight <= 0) {
            std::cerr << "Error: Invalid pixel data or dimensions for rendering page." << std::endl;
            return;
        }

        // Update texture dimensions if needed (e.g., page size is larger than current texture)
        // This is important to avoid re-creating texture constantly if page sizes vary
        int currentTexWidth, currentTexHeight;
        SDL_QueryTexture(m_texture.get(), NULL, NULL, &currentTexWidth, &currentTexHeight);

        if (srcWidth > currentTexWidth || srcHeight > currentTexHeight) {
            m_texture.reset(SDL_CreateTexture(
                m_renderer.get(),
                SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING,
                std::max(srcWidth, currentTexWidth),
                std::max(srcHeight, currentTexHeight)
            ));
            if (!m_texture) {
                std::cerr << "Error: Failed to re-create texture: " << SDL_GetError() << std::endl;
                return;
            }
        }

        void* pixels;
        int pitch; // Pitch is in bytes

        // Lock texture for pixel access
        if (SDL_LockTexture(m_texture.get(), NULL, &pixels, &pitch) != 0) {
            std::cerr << "Error: Failed to lock texture: " << SDL_GetError() << std::endl;
            return;
        }

        // Convert RGB24 to ARGB8888 and copy to texture
        // This is the direct, no-flip copy as per your feedback.
        for (int y = 0; y < srcHeight; ++y) {
            const uint8_t* srcRow = pixelData.data() + (static_cast<size_t>(y) * srcWidth * 3);
            uint32_t* destRow = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + (static_cast<size_t>(y) * pitch));
            for (int x = 0; x < srcWidth; ++x) {
                destRow[x] = rgb24_to_argb32(srcRow[x * 3], srcRow[x * 3 + 1], srcRow[x * 3 + 2]);
            }
        }

        SDL_UnlockTexture(m_texture.get());

        // Define destination rectangle for copying texture to renderer
        SDL_Rect destRect = { destX, destY, destWidth, destHeight };
        SDL_RenderCopy(m_renderer.get(), m_texture.get(), NULL, &destRect);
    }

    // Getters for window dimensions - now directly query SDL
    int getWindowWidth() const {
        int w, h;
        SDL_GetWindowSize(m_window.get(), &w, &h);
        return w;
    }
    int getWindowHeight() const {
        int w, h;
        SDL_GetWindowSize(m_window.get(), &w, &h);
        return h;
    }
    SDL_Renderer* getSDLRenderer() const { return m_renderer.get(); }
    SDL_Window* getSDLWindow() const { return m_window.get(); } // Added getter for raw SDL_Window*

    // Removed handleResize as dimensions are queried directly now.
    // The SDL_WINDOWEVENT_RESIZED event will still trigger a re-render,
    // which will then query the new size.

    // Toggle fullscreen
    void toggleFullscreen() {
        Uint32 fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
        bool is_fullscreen = (SDL_GetWindowFlags(m_window.get()) & fullscreen_flag) != 0;
        SDL_SetWindowFullscreen(m_window.get(), is_fullscreen ? 0 : fullscreen_flag);
    }

private:
    std::unique_ptr<SDL_Window, SDL_Window_Deleter> m_window;
    std::unique_ptr<SDL_Renderer, SDL_Renderer_Deleter> m_renderer;
    std::unique_ptr<SDL_Texture, SDL_Texture_Deleter> m_texture;
    // Removed m_windowWidth and m_windowHeight member variables
};

// --- TextRenderer Class ---
class TextRenderer {
public:
    // Added fontPath as a member to allow reopening font at different sizes
    TextRenderer(SDL_Renderer* renderer, const std::string& fontPath, int fontSize)
        : m_sdlRenderer(renderer), m_fontPath(fontPath), m_baseFontSize(fontSize), m_currentFontSize(0) { // Initialize m_currentFontSize
        if (TTF_Init() == -1) {
            throw std::runtime_error("SDL_ttf could not initialize! TTF_Error: " + std::string(TTF_GetError()));
        }
        // Font will be opened on the first call to setFontSize in renderUIOverlay
    }

    // Destructor: TTF_Quit() is now handled in main.
    ~TextRenderer() {
        // No TTF_Quit() here
    }

    // New method to set font size dynamically
    void setFontSize(int scale) {
        int newFontSize = static_cast<int>(m_baseFontSize * (scale / 100.0));
        newFontSize = std::max(8, newFontSize); // Ensure a minimum legible font size (e.g., 8 pixels)

        // Always attempt to open if m_font is null, or if the size has changed
        if (!m_font || newFontSize != m_currentFontSize) {
            if (m_font) {
                TTF_CloseFont(m_font.release()); // Release current font if it exists
            }
            m_font.reset(TTF_OpenFont(m_fontPath.c_str(), newFontSize));
            if (!m_font) {
                std::cerr << "Error: Failed to load font: " << m_fontPath << " at size: " << newFontSize << "! TTF_Error: " << TTF_GetError() << std::endl;
                // Attempt to load base size as a last resort
                m_font.reset(TTF_OpenFont(m_fontPath.c_str(), m_baseFontSize));
                if (!m_font) {
                     throw std::runtime_error("Failed to load font: " + m_fontPath + " at base size after error!");
                }
                m_currentFontSize = m_baseFontSize; // Update current size to base if fallback used
            } else {
                m_currentFontSize = newFontSize; // Update current size to new size if successful
            }
        }
    }

    // Renders text to the SDL renderer
    void renderText(const std::string& text, int x, int y, SDL_Color color) {
        if (text.empty()) return;
        if (!m_font) {
            std::cerr << "Error: Font not loaded for rendering text." << std::endl;
            return;
        }

        std::unique_ptr<SDL_Surface, void(*)(SDL_Surface*)> textSurface(
            TTF_RenderText_Blended(m_font.get(), text.c_str(), color),
            SDL_FreeSurface
        );
        if (!textSurface) {
            std::cerr << "Error: Unable to render text surface! TTF_Error: " << TTF_GetError() << std::endl;
            return;
        }

        std::unique_ptr<SDL_Texture, SDL_Texture_Deleter> textTexture(
            SDL_CreateTextureFromSurface(m_sdlRenderer, textSurface.get())
        );
        if (!textTexture) {
            std::cerr << "Error: Unable to create texture from rendered text! SDL_Error: " << SDL_GetError() << std::endl;
            return;
        }

        SDL_Rect renderQuad = { x, y, textSurface->w, textSurface->h };
        SDL_RenderCopy(m_sdlRenderer, textTexture.get(), NULL, &renderQuad);
    }

private:
    SDL_Renderer* m_sdlRenderer;
    std::unique_ptr<TTF_Font, TTF_Font_Deleter> m_font;
    std::string m_fontPath; // Store font path to reopen at different sizes
    int m_baseFontSize; // Store the original base font size
    int m_currentFontSize; // Track current font size to avoid unnecessary reloads
};

// --- Document Abstract Base Class ---
class Document {
public:
    virtual bool open(const std::string& filename) = 0;
    virtual int getPageCount() const = 0;
    // Renders a page and returns RGB24 pixel data (3 bytes per pixel)
    virtual std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) = 0;
    virtual ~Document() = default;
};

// --- PdfDocument Class (MuPDF Implementation) ---
class PdfDocument : public Document {
public:
    PdfDocument() : m_ctx(nullptr), m_doc(nullptr) {}

    // Destructor: Manually drop the document before the context
    ~PdfDocument() override {
        if (m_doc) {
            fz_drop_document(m_ctx.get(), m_doc);
            m_doc = nullptr; // Nullify the pointer after dropping
        }
        // m_ctx will be dropped by its unique_ptr automatically
    }

    bool open(const std::string& filename) override {
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
            m_doc = nullptr; // Ensure doc is null if creation failed
            m_ctx.reset(); // Ensure context is null if creation failed
            return false;
        }
        return true;
    }

    int getPageCount() const override {
        if (!m_doc) return 0;
        return fz_count_pages(m_ctx.get(), m_doc);
    }

    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override {
        std::vector<uint8_t> pixelData;
        if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
            return pixelData;
        }

        fz_page* page = nullptr;
        fz_pixmap* pix = nullptr;

        fz_try(m_ctx.get()) {
            page = fz_load_page(m_ctx.get(), m_doc, pageNum);
            if (!page) {
                std::cerr << "Error: Failed to load PDF page " << pageNum << std::endl;
                fz_throw(m_ctx.get(), FZ_ERROR_GENERIC, "Failed to load page"); // Throw to catch block
            }

            fz_rect bounds = fz_bound_page(m_ctx.get(), page);
            double img_width = bounds.x1 - bounds.x0;
            double img_height = bounds.y1 - bounds.y0;

            // Calculate scaled dimensions
            int dpi = 72; // Standard PDF DPI
            outWidth = static_cast<int>(img_width * scale / dpi);
            outHeight = static_cast<int>(img_height * scale / dpi);

            fz_matrix ctm = fz_scale(static_cast<double>(outWidth) / img_width, static_cast<double>(outHeight) / img_height);
            pix = fz_new_pixmap_from_page(m_ctx.get(), page, ctm, fz_device_rgb(m_ctx.get()), 0);

            if (!pix) {
                std::cerr << "Error: Failed to create pixmap for PDF page " << pageNum << std::endl;
                fz_throw(m_ctx.get(), FZ_ERROR_GENERIC, "Failed to create pixmap"); // Throw to catch block
            }

            pixelData.resize(static_cast<size_t>(pix->w) * pix->h * 3); // RGB24
            uint8_t* dest = pixelData.data();
            uint8_t* src = pix->samples;

            for (int y = 0; y < pix->h; ++y) {
                memcpy(dest + static_cast<size_t>(y) * pix->w * 3, src + static_cast<size_t>(y) * pix->stride, static_cast<size_t>(pix->w) * 3);
            }

        } fz_catch(m_ctx.get()) {
            // MuPDF errors are often printed to stderr by default when caught.
            // fz_error_message is not directly available in newer MuPDF versions this way.
            std::cerr << "Error rendering PDF page." << std::endl;
            pixelData.clear(); // Clear data on error
        }

        if (page) fz_drop_page(m_ctx.get(), page);
        if (pix) fz_drop_pixmap(m_ctx.get(), pix);

        return pixelData; // Returns RGB24 data
    }

private:
    std::unique_ptr<fz_context, FzContextDeleter> m_ctx;
    fz_document* m_doc; // Raw pointer for manual management with fz_drop_document
};

// --- DjvuDocument Class (DjVuLibre Implementation) ---
class DjvuDocument : public Document {
public:
    DjvuDocument() : m_ctx(nullptr), m_doc(nullptr) {}

    bool open(const std::string& filename) override {
        m_ctx.reset(ddjvu_context_create("sdlbook"));
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

        // Wait for document decoding to finish
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

    int getPageCount() const override {
        if (!m_doc) return 0;
        return ddjvu_document_get_pagenum(m_doc.get());
    }

    std::vector<uint8_t> renderPage(int pageNum, int& outWidth, int& outHeight, int scale) override {
        std::vector<uint8_t> pixelData;
        if (!m_doc || pageNum < 0 || pageNum >= getPageCount()) {
            return pixelData;
        }

        ddjvu_page_t* page = ddjvu_page_create_by_pageno(m_doc.get(), pageNum);
        if (!page) {
            std::cerr << "Error: Cannot create DjVu page " << pageNum << std::endl;
            return pixelData;
        }

        // Wait for page decoding to finish
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

        // Calculate scaled dimensions
        outWidth = static_cast<int>(static_cast<double>(img_width) * scale / dpi);
        outHeight = static_cast<int>(static_cast<double>(img_height) * scale / dpi);

        ddjvu_rect_t prect = { 0, 0, (unsigned int)outWidth, (unsigned int)outHeight };
        ddjvu_format_t* fmt = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, 0); // Request RGB24
        if (!fmt) {
            std::cerr << "Error: Cannot create DjVu format." << std::endl;
            ddjvu_page_release(page);
            return pixelData;
        }
        ddjvu_format_set_row_order(fmt, 1); // Top-to-bottom row order

        size_t rowsize = static_cast<size_t>(outWidth) * 3;
        pixelData.resize(rowsize * outHeight);

        // Fix: Cast pixelData.data() to char* as required by ddjvu_page_render
        if (!ddjvu_page_render(page, DDJVU_RENDER_COLOR, &prect, &prect, fmt, rowsize, reinterpret_cast<char*>(pixelData.data()))) {
            std::cerr << "Error: DjVu page render failed for page " << pageNum << std::endl;
            pixelData.clear(); // Clear data on error
        }

        ddjvu_format_release(fmt);
        ddjvu_page_release(page);

        return pixelData; // Returns RGB24 data
    }

private:
    std::unique_ptr<ddjvu_context_t, DdjvuContextDeleter> m_ctx;
    std::unique_ptr<ddjvu_document_t, DdjvuDocumentDeleter> m_doc;

    // Helper to process DjVu messages (errors, warnings, etc.)
    void processDjvuMessages() {
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
                    // Optionally log info messages
                    break;
                // Fix: DDJVU_WARNING enum member seems to be removed or renamed in this DjVuLibre version.
                // Removed 'case DDJVU_WARNING:'
                default:
                    break;
            }
            ddjvu_message_pop(m_ctx.get());
        }
    }
};

// --- App Class ---
class App {
public:
    App(const std::string& filename, int initialWidth, int initialHeight)
        : m_running(true), m_currentPage(0), m_currentScale(100),
          m_scrollX(0), m_scrollY(0), m_pageWidth(0), m_pageHeight(0) {

        // Initialize Renderer
        m_renderer = std::make_unique<Renderer>(initialWidth, initialHeight, "SDLBook C++");

        // Initialize TextRenderer with a base font size
        m_textRenderer = std::make_unique<TextRenderer>(m_renderer->getSDLRenderer(), "./romfs/res/Roboto-Regular.ttf", 16);
        // NOTE: Replace "./Inter-Regular.ttf" with the actual path to your .ttf font file.

        // Determine document type and open
        if (filename.length() >= 4 && filename.substr(filename.length() - 4) == ".pdf") {
            m_document = std::make_unique<PdfDocument>();
        } else if (filename.length() >= 5 && filename.substr(filename.length() - 5) == ".djvu") {
            m_document = std::make_unique<DjvuDocument>();
        } else {
            throw std::runtime_error("Unsupported file format. Please provide a .pdf or .djvu file.");
        }

        if (!m_document->open(filename)) {
            throw std::runtime_error("Failed to open document: " + filename);
        }

        m_pageCount = m_document->getPageCount();
        if (m_pageCount == 0) {
            throw std::runtime_error("Document contains no pages.");
        }

        // Initial page render to get dimensions
        renderCurrentPage();
    }

    // Main application loop
    void run() {
        SDL_Event event;
        while (m_running) {
            // Event handling
            while (SDL_PollEvent(&event)) {
                handleEvent(event);
            }

            // Update (e.g., animations, game logic - not much for a book reader)
            update();

            // Rendering
            render();

            // Small delay to prevent busy-waiting
            SDL_Delay(1);
        }
    }

private:
    bool m_running;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<TextRenderer> m_textRenderer;
    std::unique_ptr<Document> m_document;

    int m_currentPage;
    int m_pageCount;
    int m_currentScale; // Percentage, e.g., 100 for 100%
    int m_scrollX;
    int m_scrollY;

    int m_pageWidth;  // Rendered width of the current page at m_currentScale
    int m_pageHeight; // Rendered height of the current page at m_currentScale

    // --- Event Handling ---
    void handleEvent(const SDL_Event& event) {
        switch (event.type) {
            case SDL_QUIT:
                m_running = false;
                break;
            case SDL_WINDOWEVENT:
                // We no longer need to update m_windowWidth/Height here,
                // as we query them directly in renderCurrentPage.
                // However, we still want to re-render on resize/expose.
                if (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                    event.window.event == SDL_WINDOWEVENT_EXPOSED) {
                    render(); // Re-render on resize/expose
                }
                break;
            case SDL_KEYDOWN:
                // Fix: Cast event.key.keysym.mod to SDL_Keymod
                handleKeyDown(event.key.keysym.sym, static_cast<SDL_Keymod>(event.key.keysym.mod));
                break;
            case SDL_MOUSEWHEEL:
                // Pass SDL_GetModState() to get the current modifier key state
                handleMouseWheel(event.wheel.y, SDL_GetModState());
                break;
            case SDL_MOUSEMOTION:
                handleMouseMotion(event.motion.xrel, event.motion.yrel, event.motion.state);
                break;
            default:
                break;
        }
    }

    void handleKeyDown(SDL_Keycode key, SDL_Keymod mod) {
        bool needsRender = false;
        // The 'mod' parameter is received but not directly used in this function's current logic.
        // It's kept for consistency with SDL event structure and potential future use for key combinations.
        switch (key) {
            case SDLK_q: // Quit
            case SDLK_ESCAPE:
                m_running = false;
                break;
            case SDLK_RIGHT: // Scroll right
                needsRender = changeScroll(32, 0);
                break;
            case SDLK_LEFT: // Scroll left
                needsRender = changeScroll(-32, 0);
                break;
            case SDLK_UP: // Scroll up
                needsRender = changeScroll(0, -32);
                break;
            case SDLK_DOWN: // Scroll down
                needsRender = changeScroll(0, 32);
                break;
            case SDLK_PAGEDOWN: // Next page
                needsRender = changePage(1);
                break;
            case SDLK_PAGEUP: // Previous page
                needsRender = changePage(-1);
                break;
            case SDLK_EQUALS: // Zoom in (for '+' key, often on same key as '=')
                needsRender = changeScale(10);
                break;
            case SDLK_MINUS: // Zoom out (for '-' key)
                needsRender = changeScale(-10);
                break;
            case SDLK_f: // Toggle fullscreen
                m_renderer->toggleFullscreen();
                needsRender = true; // Re-render after fullscreen change
                break;
            case SDLK_g: // Jump to page
                // This would typically involve a text input dialog,
                // which is beyond the scope of this basic example's UI.
                // For now, let's just print a message.
                // std::cout << "Jump to page feature not implemented in this basic UI." << std::endl;
                break;
            default:
                break;
        }
        if (needsRender) {
            render();
        }
    }

    void handleMouseWheel(int y_delta, SDL_Keymod mod) {
        bool needsRender = false;
        if (mod & (KMOD_LCTRL | KMOD_RCTRL)) { // Ctrl + Mouse Wheel for Zoom
            needsRender = changeScale(y_delta * 5); // Adjust zoom sensitivity
        } else { // Regular Mouse Wheel for Vertical Scroll
            needsRender = changeScroll(0, -y_delta * 32); // Adjust scroll sensitivity
        }
        if (needsRender) {
            render();
        }
    }

    void handleMouseMotion(int x_rel, int y_rel, Uint32 mouse_state) {
        if (mouse_state & SDL_BUTTON_LMASK) { // Left mouse button held down for dragging
            bool needsRender = changeScroll(-x_rel, -y_rel); // Invert for natural drag
            if (needsRender) {
                render();
            }
        }
    }

    // --- State Update Methods ---
    bool changePage(int delta) {
        int newPage = m_currentPage + delta;
        if (newPage >= 0 && newPage < m_pageCount) {
            m_currentPage = newPage;
            m_scrollX = 0; // Reset scroll on page change
            m_scrollY = 0;
            renderCurrentPage(); // Re-render the new page content
            return true;
        }
        return false;
    }

    bool jumpToPage(int pageNum) {
        if (pageNum >= 0 && pageNum < m_pageCount) {
            m_currentPage = pageNum;
            m_scrollX = 0; // Reset scroll on page change
            m_scrollY = 0;
            renderCurrentPage();
            return true;
        }
        return false;
    }

    bool changeScale(int delta) {
        int newScale = m_currentScale + delta;
        if (newScale >= 10 && newScale <= 500) { // Limit zoom from 10% to 500%
            m_currentScale = newScale;
            // Update the font size for the UI overlay based on the new scale
            m_textRenderer->setFontSize(m_currentScale);
            // Re-render page at new scale
            renderCurrentPage();
            // The clamping of scroll values will now happen in changeScroll
            return true;
        }
        return false;
    }

    // Moved clamping logic into changeScroll for more robust state management
    bool changeScroll(int deltaX, int deltaY) {
        int currentWindowWidth = m_renderer->getWindowWidth();
        int currentWindowHeight = m_renderer->getWindowHeight();

        // Calculate potential new scroll values
        int newScrollX = m_scrollX + deltaX;
        int newScrollY = m_scrollY + deltaY;

        // Calculate maximum scroll values based on current page and window size
        // This is the maximum positive scroll value, meaning the content is shifted left/up by this amount.
        // It should be 0 if the page is smaller than the window.
        int maxScrollX = std::max(0, m_pageWidth - currentWindowWidth);
        int maxScrollY = std::max(0, m_pageHeight - currentWindowHeight);

        // Clamp new scroll values within valid range [0, maxScroll]
        newScrollX = std::max(0, std::min(newScrollX, maxScrollX));
        newScrollY = std::max(0, std::min(newScrollY, maxScrollY));

        // Only update and trigger render if scroll values actually changed
        if (newScrollX != m_scrollX || newScrollY != m_scrollY) {
            m_scrollX = newScrollX;
            m_scrollY = newScrollY;
            return true;
        }
        return false;
    }

    void update() {
        // No continuous updates needed for a static book reader
        // All updates are event-driven.
    }

    // --- Rendering Methods ---
    void renderCurrentPage() {
        // Render the current page from the document backend
        std::vector<uint8_t> pagePixels = m_document->renderPage(m_currentPage, m_pageWidth, m_pageHeight, m_currentScale);

        // Clear the renderer to white background
        m_renderer->clear(255, 255, 255, 255);

        // Get actual window size directly from SDL before calculations
        int currentWindowWidth = m_renderer->getWindowWidth();
        int currentWindowHeight = m_renderer->getWindowHeight();

        // Scroll values are now clamped in changeScroll, so no need to re-clamp here.
        // They should always be within valid range.

        int destX, destY;

        // Horizontal positioning
        if (m_pageWidth <= currentWindowWidth) {
            // Page is smaller than or equal to window width, center it horizontally
            destX = (currentWindowWidth - m_pageWidth) / 2;
        } else {
            // Page is wider than window, position based on scrollX
            // A positive m_scrollX means the content is shifted left relative to the window.
            destX = -m_scrollX;
        }

        // Vertical positioning
        if (m_pageHeight <= currentWindowHeight) {
            // Page is smaller than or equal to window height, center it vertically
            destY = (currentWindowHeight - m_pageHeight) / 2;
        } else {
            // Page is taller than window, position based on scrollY
            // A positive m_scrollY means the content is shifted up relative to the window.
            destY = -m_scrollY;
        }


        // Render the page image
        m_renderer->renderPage(pagePixels, m_pageWidth, m_pageHeight,
                               destX, destY, m_pageWidth, m_pageHeight);

        // Render UI overlay (needs to be adjusted for currentWindowWidth/Height directly)
        renderUIOverlay();

        // Present to screen
        m_renderer->present();
    }

    void renderUIOverlay() {
        // Ensure the font size is updated based on the current scale before rendering text
        m_textRenderer->setFontSize(m_currentScale);

        SDL_Color textColor = { 0, 0, 0, 255 }; // Black text
        std::string pageInfo = "Page: " + std::to_string(m_currentPage + 1) + "/" + std::to_string(m_pageCount);
        std::string scaleInfo = "Scale: " + std::to_string(m_currentScale) + "%";

        int currentWindowWidth = m_renderer->getWindowWidth();
        int currentWindowHeight = m_renderer->getWindowHeight();

        // Render page info at bottom center
        m_textRenderer->renderText(pageInfo,
                                   (currentWindowWidth - (int)pageInfo.length() * 8) / 2, // Simple estimate for text width
                                   currentWindowHeight - 30, textColor);

        // Render scale info at top right
        m_textRenderer->renderText(scaleInfo,
                                   currentWindowWidth - (int)scaleInfo.length() * 8 - 10, // Simple estimate for text width
                                   10, textColor);
    }

    // Main render loop, called when an explicit redraw is needed
    void render() {
        renderCurrentPage();
    }
};

// --- Main Function ---
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <document_file.pdf/.djvu>" << std::endl;
        return 1;
    }

    try {
        App app(argv[1], 800, 600); // Initial window size 800x600
        app.run();
    } catch (const std::runtime_error& e) {
        std::cerr << "Application Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "An unexpected error occurred: " << e.what() << std::endl;
        return 1;
    }

    // Ensure SDL and TTF are quit after all resources are released
    TTF_Quit();
    SDL_Quit();

    return 0;
}

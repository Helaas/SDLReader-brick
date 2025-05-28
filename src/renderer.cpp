#include "renderer.h"
#include "document.h" 
#include <iostream>
#include <stdexcept> 
#include <algorithm> 

void SDL_Window_Deleter::operator()(SDL_Window* window) const {
    if (window) SDL_DestroyWindow(window);
}

void SDL_Renderer_Deleter::operator()(SDL_Renderer* renderer) const {
    if (renderer) SDL_DestroyRenderer(renderer);
}



// --- Renderer Class ---


Renderer::Renderer(int width, int height, const std::string& title) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        throw std::runtime_error("SDL could not initialize! SDL_Error: " + std::string(SDL_GetError()));
    }

    m_window.reset(SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_UNDEFINED, // Let SDL decide initial X position
        SDL_WINDOWPOS_UNDEFINED, // Let SDL decide initial Y position
        width,                   // Initial window width
        height,                  // Initial window height
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE // Window is shown and can be resized
    ));
    if (!m_window) {
        throw std::runtime_error("Window could not be created! SDL_Error: " + std::string(SDL_GetError()));
    }

    // Create SDL Renderer.
    // -1: Use the first available rendering driver.
    // SDL_RENDERER_ACCELERATED: Request hardware acceleration.
    // SDL_RENDERER_PRESENTVSYNC: Enable VSync to cap frame rate to monitor refresh rate.
    m_renderer.reset(SDL_CreateRenderer(m_window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!m_renderer) {
        throw std::runtime_error("Renderer could not be created! SDL_Error: " + std::string(SDL_GetError()));
    }

    // Create a streaming texture. This texture will hold the pixel data of the rendered page.
    // SDL_PIXELFORMAT_ARGB8888: The pixel format we expect (Alpha, Red, Green, Blue, 8 bits each).
    // SDL_TEXTUREACCESS_STREAMING: Allows us to lock and update the texture's pixel data frequently.
    m_texture.reset(SDL_CreateTexture(
        m_renderer.get(),
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        width,  // Initial texture width 
        height  // Initial texture height 
    ));
    if (!m_texture) {
        throw std::runtime_error("Texture could not be created! SDL_Error: " + std::string(SDL_GetError()));
    }

    SDL_SetRenderDrawColor(m_renderer.get(), 255, 255, 255, 255);
    SDL_RenderClear(m_renderer.get());
    SDL_RenderPresent(m_renderer.get());
}


Renderer::~Renderer() {
    // No SDL_Quit() here
    // SDL_Quit() is called in main.cpp for proper shutdown order.
}

void Renderer::clear(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(m_renderer.get(), r, g, b, a);
    SDL_RenderClear(m_renderer.get());
}

void Renderer::present() {
    SDL_RenderPresent(m_renderer.get());
}

void Renderer::renderPage(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight,
                          int destX, int destY, int destWidth, int destHeight) {
    if (pixelData.empty() || srcWidth <= 0 || srcHeight <= 0) {
        std::cerr << "Error: Invalid pixel data or dimensions for rendering page." << std::endl;
        return;
    }

    int currentTexWidth, currentTexHeight;
    SDL_QueryTexture(m_texture.get(), NULL, NULL, &currentTexWidth, &currentTexHeight);

    // Recreate the texture if the source image is larger than the current texture.
    // This prevents clipping and ensures the texture can hold the entire page.
    if (srcWidth > currentTexWidth || srcHeight > currentTexHeight) {
        m_texture.reset(SDL_CreateTexture(
            m_renderer.get(),
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            std::max(srcWidth, currentTexWidth),   // New width: at least srcWidth, but not smaller than current
            std::max(srcHeight, currentTexHeight)  // New height: at least srcHeight, but not smaller than current
        ));
        if (!m_texture) {
            std::cerr << "Error: Failed to re-create texture: " << SDL_GetError() << std::endl;
            return;
        }
    }

    void* pixels; // Pointer to the raw pixel data within the locked texture
    int pitch;    // Pitch (bytes per row) of the locked texture

    // Lock the texture for direct pixel access.
    if (SDL_LockTexture(m_texture.get(), NULL, &pixels, &pitch) != 0) {
        std::cerr << "Error: Failed to lock texture: " << SDL_GetError() << std::endl;
        return;
    }

    // Convert RGB24 (3 bytes per pixel) from source to ARGB8888 (4 bytes per pixel) for texture.
    // This loop iterates through each row and pixel, performing the conversion.
    for (int y = 0; y < srcHeight; ++y) {
        const uint8_t* srcRow = pixelData.data() + (static_cast<size_t>(y) * srcWidth * 3);
        uint32_t* destRow = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + (static_cast<size_t>(y) * pitch));
        for (int x = 0; x < srcWidth; ++x) {
            destRow[x] = rgb24_to_argb32(srcRow[x * 3], srcRow[x * 3 + 1], srcRow[x * 3 + 2]);
        }
    }

    SDL_UnlockTexture(m_texture.get()); // Unlock the texture after pixel data is updated

    // Define the destination rectangle on the renderer where the texture will be copied.
    SDL_Rect destRect = { destX, destY, destWidth, destHeight };
    // Copy the entire texture (NULL for source rectangle) to the specified destination rectangle on the renderer.
    SDL_RenderCopy(m_renderer.get(), m_texture.get(), NULL, &destRect);
}

int Renderer::getWindowWidth() const {
    int w, h;
    SDL_GetWindowSize(m_window.get(), &w, &h);
    return w;
}

int Renderer::getWindowHeight() const {
    int w, h;
    SDL_GetWindowSize(m_window.get(), &w, &h);
    return h;
}

SDL_Renderer* Renderer::getSDLRenderer() const {
    return m_renderer.get();
}

SDL_Window* Renderer::getSDLWindow() const {
    return m_window.get();
}

void Renderer::toggleFullscreen() {
    Uint32 fullscreen_flag = SDL_WINDOW_FULLSCREEN_DESKTOP;
    bool is_fullscreen = (SDL_GetWindowFlags(m_window.get()) & fullscreen_flag) != 0;
    SDL_SetWindowFullscreen(m_window.get(), is_fullscreen ? 0 : fullscreen_flag);
}

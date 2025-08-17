#include "renderer.h"
#include "document.h" // Assuming document.h provides rgb24_to_argb32
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <vector>
#include <numeric>

// Removed custom deleters for SDL_Window and SDL_Renderer as they are no longer owned by Renderer.
// void SDL_Window_Deleter::operator()(SDL_Window* window) const {
//     if (window) SDL_DestroyWindow(window);
// }
//
// void SDL_Renderer_Deleter::operator()(SDL_Renderer* renderer) const {
//     if (renderer) SDL_DestroyRenderer(renderer);
// }

// --- Renderer Class ---

// Constructor now accepts pre-initialized SDL_Window and SDL_Renderer
Renderer::Renderer(SDL_Window* window, SDL_Renderer* renderer)
    : m_window(window), m_renderer(renderer),
      m_currentTexWidth(0), m_currentTexHeight(0), m_isFullscreen(false) {

    if (!m_window) {
        throw std::runtime_error("Renderer received a null SDL_Window pointer.");
    }
    if (!m_renderer) {
        throw std::runtime_error("Renderer received a null SDL_Renderer pointer.");
    }

    // SDL_Init will now happen outside the Renderer class
    // SDL_SetRenderDrawColor(m_renderer.get(), 255, 255, 255, 255); // Use raw pointer
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    // SDL_SetRenderDrawColor(m_renderer.get(), r, g, b, a); // Use raw pointer
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    // SDL_RenderClear(m_renderer.get()); // Use raw pointer
    SDL_RenderClear(m_renderer);
}

void Renderer::present() {
    // SDL_RenderPresent(m_renderer.get()); // Use raw pointer
    SDL_RenderPresent(m_renderer);
}

SDL_Renderer* Renderer::getSDLRenderer() const {
    return m_renderer; // Return raw pointer
}

void Renderer::renderPage(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight) {
    if (pixelData.empty() || srcWidth == 0 || srcHeight == 0) {
        std::cerr << "Warning: Attempted to render empty or zero-dimension pixel data." << std::endl;
        return;
    }

    // Check if texture needs to be recreated (e.g., if dimensions change)
    if (!m_texture || srcWidth != m_currentTexWidth || srcHeight != m_currentTexHeight) {
        if (m_texture) {
            m_texture.reset();
        }
        // m_texture.reset(SDL_CreateTexture(m_renderer.get(), // Use raw pointer
        m_texture.reset(SDL_CreateTexture(m_renderer,
                                         SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING,
                                         srcWidth, srcHeight));
        if (!m_texture) {
            std::cerr << "Error: Unable to create texture! SDL_Error: " << SDL_GetError() << std::endl;
            return;
        }
        m_currentTexWidth = srcWidth;
        m_currentTexHeight = srcHeight;
    }

    void* pixels;
    int pitch;
    if (SDL_LockTexture(m_texture.get(), NULL, &pixels, &pitch) != 0) {
        std::cerr << "Error: Unable to lock texture! SDL_Error: " << SDL_GetError() << std::endl;
        return;
    }

    for (int y = 0; y < srcHeight; ++y) {
        const uint8_t* srcRow = pixelData.data() + (static_cast<size_t>(y) * srcWidth * 3);
        uint32_t* destRow = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + (static_cast<size_t>(y) * pitch));

        std::fill(destRow, destRow + (pitch / sizeof(uint32_t)), 0xFFFFFFFF);

        for (int x = 0; x < srcWidth; ++x) {
            // Assuming rgb24_to_argb32 is defined in document.h or a common utility
            destRow[x] = rgb24_to_argb32(srcRow[x * 3], srcRow[x * 3 + 1], srcRow[x * 3 + 2]);
        }
    }

    SDL_UnlockTexture(m_texture.get());

    SDL_Rect destRect = { destX, destY, destWidth, destHeight };
    // SDL_RenderCopy(m_renderer.get(), m_texture.get(), NULL, &destRect); // Use raw pointer
    SDL_RenderCopy(m_renderer, m_texture.get(), NULL, &destRect);
}

int Renderer::getWindowWidth() const {
    int w, h;
    // SDL_GetWindowSize(m_window.get(), &w, &h); // Use raw pointer
    SDL_GetWindowSize(m_window, &w, &h);
    return w;
}

int Renderer::getWindowHeight() const {
    int w, h;
    // SDL_GetWindowSize(m_window.get(), &w, &h); // Use raw pointer
    SDL_GetWindowSize(m_window, &w, &h);
    return h;
}

void Renderer::toggleFullscreen() {
    Uint32 fullscreen_flag = m_isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
    // if (SDL_SetWindowFullscreen(m_window.get(), fullscreen_flag) < 0) { // Use raw pointer
    if (SDL_SetWindowFullscreen(m_window, fullscreen_flag) < 0) {
        std::cerr << "Error toggling fullscreen: " << SDL_GetError() << std::endl;
    } else {
        m_isFullscreen = !m_isFullscreen;
    }
}


Uint32 Renderer::getRequiredSDLInitFlags() {
    return SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER;
}
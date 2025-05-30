#include "renderer.h"
#include "document.h" 
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <vector>     
#include <numeric>    


void SDL_Window_Deleter::operator()(SDL_Window* window) const {
    if (window) SDL_DestroyWindow(window);
}

void SDL_Renderer_Deleter::operator()(SDL_Renderer* renderer) const {
    if (renderer) SDL_DestroyRenderer(renderer);
}

// --- Renderer Class ---

Renderer::Renderer(int width, int height, const std::string& title)
    : m_currentTexWidth(0), m_currentTexHeight(0) // Initialize new members
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0) {
        throw std::runtime_error("SDL could not initialize! SDL_Error: " + std::string(SDL_GetError()));
    }

    m_window.reset(SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_UNDEFINED, // Let SDL decide initial X position
        SDL_WINDOWPOS_UNDEFINED, // Let SDL decide initial Y position
        width,                   // Initial window width
        height,                  // Initial window height
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE 
    ));
    if (!m_window) {
        throw std::runtime_error("Window could not be created! SDL_Error: " + std::string(SDL_GetError()));
    }

    m_renderer.reset(SDL_CreateRenderer(
        m_window.get(),
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    ));
    if (!m_renderer) {
        throw std::runtime_error("Renderer could not be created! SDL_Error: " + std::string(SDL_GetError()));
    }

    SDL_SetRenderDrawColor(m_renderer.get(), 255, 255, 255, 255);
}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    SDL_SetRenderDrawColor(m_renderer.get(), r, g, b, a);
    SDL_RenderClear(m_renderer.get());
}

void Renderer::present() {
    SDL_RenderPresent(m_renderer.get());
}

SDL_Renderer* Renderer::getSDLRenderer() const {
    return m_renderer.get();
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
        m_texture.reset(SDL_CreateTexture(m_renderer.get(),
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

    // Convert RGB24 (3 bytes per pixel) from source to ARGB8888 (4 bytes per pixel) for texture.
    // This loop iterates through each row and pixel, performing the conversion.
    for (int y = 0; y < srcHeight; ++y) {
        const uint8_t* srcRow = pixelData.data() + (static_cast<size_t>(y) * srcWidth * 3);
        uint32_t* destRow = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + (static_cast<size_t>(y) * pitch));

        // Fill the entire texture row with opaque white (0xFFFFFFFF) to handle padding
        // This ensures uninitialized padding bytes appear white, not black.
        std::fill(destRow, destRow + (pitch / sizeof(uint32_t)), 0xFFFFFFFF);

        // Copy the actual image data pixels onto the row
        for (int x = 0; x < srcWidth; ++x) {
            destRow[x] = rgb24_to_argb32(srcRow[x * 3], srcRow[x * 3 + 1], srcRow[x * 3 + 2]);
        }
    }

    SDL_UnlockTexture(m_texture.get());

    SDL_Rect destRect = { destX, destY, destWidth, destHeight };
    SDL_RenderCopy(m_renderer.get(), m_texture.get(), NULL, &destRect);
}

int Renderer::getWindowWidth() const {
    int w, h;
    SDL_GetWindowSize(m_window.get(), &w, &h);
    return w;
}

int Renderer::getWindowHeight() const {
    int w, h;
    SDL_GetWindowSize(m_window.get(), &w, &h); // Corrected: arguments are now w, h
    return h; // Corrected: return h for height
}

void Renderer::toggleFullscreen() {
    Uint32 fullscreen_flag = m_isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
    // SDL_WINDOW_FULLSCREEN_DESKTOP: This makes the window cover the entire desktop
    // without borders, which is generally a smoother transition.
    // Use SDL_WINDOW_FULLSCREEN for true fullscreen (might change resolution).
    if (SDL_SetWindowFullscreen(m_window.get(), fullscreen_flag) < 0) {
        std::cerr << "Error toggling fullscreen: " << SDL_GetError() << std::endl;
    } else {
        m_isFullscreen = !m_isFullscreen;
    }
}
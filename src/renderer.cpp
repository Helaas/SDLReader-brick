#include "renderer.h"
#include "document.h" // Assuming document.h provides rgb24_to_argb32
#include <algorithm>
#include <cmath>
#include <cstring> // For memcpy
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace
{
inline int roundUpTextureDimension(int value)
{
    constexpr int GRANULARITY = 64;
    return ((value + GRANULARITY - 1) / GRANULARITY) * GRANULARITY;
}

#if !SDL_VERSION_ATLEAST(2, 0, 10)
inline SDL_Rect makeSDLRectFromFloat(float x, float y, float w, float h)
{
    SDL_Rect rect;
    rect.x = static_cast<int>(std::lround(x));
    rect.y = static_cast<int>(std::lround(y));
    rect.w = std::max(1, static_cast<int>(std::lround(w)));
    rect.h = std::max(1, static_cast<int>(std::lround(h)));
    return rect;
}

inline SDL_Point makeSDLPointFromFloat(float x, float y)
{
    SDL_Point pt;
    pt.x = static_cast<int>(std::lround(x));
    pt.y = static_cast<int>(std::lround(y));
    return pt;
}
#endif
} // namespace

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
      m_currentTexWidth(0), m_currentTexHeight(0), m_isFullscreen(false)
{

    if (!m_window)
    {
        throw std::runtime_error("Renderer received a null SDL_Window pointer.");
    }
    if (!m_renderer)
    {
        throw std::runtime_error("Renderer received a null SDL_Renderer pointer.");
    }

    // SDL_Init will now happen outside the Renderer class
    // SDL_SetRenderDrawColor(m_renderer.get(), 255, 255, 255, 255); // Use raw pointer
    SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
}

// renderer.cpp

void Renderer::renderPageEx(const std::vector<uint8_t>& pixelData,
                            int srcWidth, int srcHeight,
                            float destX, float destY, float destWidth, float destHeight,
                            double angleDeg, SDL_RendererFlip flip)
{
    if (pixelData.empty() || srcWidth == 0 || srcHeight == 0)
    {
        std::cerr << "Warning: Attempted to render empty or zero-dimension pixel data." << std::endl;
        return;
    }

    if (!m_texture || srcWidth > m_currentTexWidth || srcHeight > m_currentTexHeight)
    {
        if (m_texture)
            m_texture.reset();
        int allocWidth = roundUpTextureDimension(std::max(srcWidth, m_currentTexWidth));
        int allocHeight = roundUpTextureDimension(std::max(srcHeight, m_currentTexHeight));
        m_texture.reset(SDL_CreateTexture(m_renderer,
                                          SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          allocWidth, allocHeight));
        if (!m_texture)
        {
            std::cerr << "Error: Unable to create texture! SDL_Error: " << SDL_GetError() << std::endl;
            return;
        }
        // Force nearest-neighbor scaling to avoid blur during rotation/scaling
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(m_texture.get(), SDL_ScaleModeNearest);
#else
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
        m_currentTexWidth = allocWidth;
        m_currentTexHeight = allocHeight;
    }

    void* pixels;
    int pitch;
    if (SDL_LockTexture(m_texture.get(), NULL, &pixels, &pitch) != 0)
    {
        std::cerr << "Error: Unable to lock texture! SDL_Error: " << SDL_GetError() << std::endl;
        return;
    }

    for (int y = 0; y < srcHeight; ++y)
    {
        const uint8_t* srcRow = pixelData.data() + (static_cast<size_t>(y) * srcWidth * 3);
        uint32_t* destRow = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + (static_cast<size_t>(y) * pitch));

        std::fill(destRow, destRow + (pitch / sizeof(uint32_t)), 0xFFFFFFFF);

        for (int x = 0; x < srcWidth; ++x)
        {
            destRow[x] = rgb24_to_argb32(srcRow[x * 3], srcRow[x * 3 + 1], srcRow[x * 3 + 2]);
        }
    }

    SDL_UnlockTexture(m_texture.get());

    SDL_Rect srcRect = {0, 0, srcWidth, srcHeight};
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_FRect destRect = {destX, destY, destWidth, destHeight};
    SDL_FPoint center{destRect.w / 2.0f, destRect.h / 2.0f}; // keep true center (avoids odd-size blur)
    SDL_RenderCopyExF(m_renderer, m_texture.get(), &srcRect, &destRect, angleDeg, &center, flip);
#else
    SDL_Rect destRect = makeSDLRectFromFloat(destX, destY, destWidth, destHeight);
    SDL_Point center = makeSDLPointFromFloat(destRect.w / 2.0f, destRect.h / 2.0f);
    SDL_RenderCopyEx(m_renderer, m_texture.get(), &srcRect, &destRect, angleDeg, &center, flip);
#endif
}

void Renderer::renderPageExARGB(const std::vector<uint32_t>& argbData,
                                int srcWidth, int srcHeight,
                                float destX, float destY, float destWidth, float destHeight,
                                double angleDeg, SDL_RendererFlip flip, const void* bufferToken)
{
    if (argbData.empty() || srcWidth == 0 || srcHeight == 0)
    {
        std::cerr << "Warning: Attempted to render empty or zero-dimension ARGB data." << std::endl;
        return;
    }

    bool textureResized = false;
    if (!m_texture || srcWidth > m_currentTexWidth || srcHeight > m_currentTexHeight)
    {
        if (m_texture)
            m_texture.reset();
        int allocWidth = roundUpTextureDimension(std::max(srcWidth, m_currentTexWidth));
        int allocHeight = roundUpTextureDimension(std::max(srcHeight, m_currentTexHeight));
        m_texture.reset(SDL_CreateTexture(m_renderer,
                                          SDL_PIXELFORMAT_ARGB8888,
                                          SDL_TEXTUREACCESS_STREAMING,
                                          allocWidth, allocHeight));
        if (!m_texture)
        {
            std::cerr << "Error: Unable to create texture! SDL_Error: " << SDL_GetError() << std::endl;
            return;
        }
        // Force nearest-neighbor scaling to avoid blur during rotation/scaling
#if SDL_VERSION_ATLEAST(2, 0, 12)
        SDL_SetTextureScaleMode(m_texture.get(), SDL_ScaleModeNearest);
#else
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
#endif
        m_currentTexWidth = allocWidth;
        m_currentTexHeight = allocHeight;
        textureResized = true;
        m_lastBufferToken = nullptr;
        m_lastBufferWidth = 0;
        m_lastBufferHeight = 0;
    }

    bool needsUpload = textureResized || bufferToken == nullptr || bufferToken != m_lastBufferToken ||
                       srcWidth != m_lastBufferWidth || srcHeight != m_lastBufferHeight;

    if (needsUpload)
    {
        void* pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(m_texture.get(), NULL, &pixels, &pitch) != 0)
        {
            std::cerr << "Error: Unable to lock texture! SDL_Error: " << SDL_GetError() << std::endl;
            return;
        }

        // Direct copy of ARGB data - much faster than RGB conversion
        const uint32_t* srcData = argbData.data();
        for (int y = 0; y < srcHeight; ++y)
        {
            uint32_t* destRow = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(pixels) + (static_cast<size_t>(y) * pitch));
            const uint32_t* srcRow = srcData + (static_cast<size_t>(y) * srcWidth);
            memcpy(destRow, srcRow, srcWidth * sizeof(uint32_t));
        }

        SDL_UnlockTexture(m_texture.get());

        m_lastBufferToken = bufferToken;
        m_lastBufferWidth = srcWidth;
        m_lastBufferHeight = srcHeight;
    }

    SDL_Rect srcRect = {0, 0, srcWidth, srcHeight};
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_FRect destRect = {destX, destY, destWidth, destHeight};
    SDL_FPoint center{destRect.w / 2.0f, destRect.h / 2.0f}; // float center prevents 0.5px drift on odd sizes
    SDL_RenderCopyExF(m_renderer, m_texture.get(), &srcRect, &destRect, angleDeg, &center, flip);
#else
    SDL_Rect destRect = makeSDLRectFromFloat(destX, destY, destWidth, destHeight);
    SDL_Point center = makeSDLPointFromFloat(destRect.w / 2.0f, destRect.h / 2.0f);
    SDL_RenderCopyEx(m_renderer, m_texture.get(), &srcRect, &destRect, angleDeg, &center, flip);
#endif
}

void Renderer::clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    // SDL_SetRenderDrawColor(m_renderer.get(), r, g, b, a); // Use raw pointer
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    // SDL_RenderClear(m_renderer.get()); // Use raw pointer
    SDL_RenderClear(m_renderer);
}

void Renderer::present()
{
    // SDL_RenderPresent(m_renderer.get()); // Use raw pointer
    SDL_RenderPresent(m_renderer);
}

SDL_Renderer* Renderer::getSDLRenderer() const
{
    return m_renderer; // Return raw pointer
}

int Renderer::getWindowWidth() const
{
    int w, h;
    // SDL_GetWindowSize(m_window.get(), &w, &h); // Use raw pointer
    SDL_GetWindowSize(m_window, &w, &h);
    return w;
}

int Renderer::getWindowHeight() const
{
    int w, h;
    // SDL_GetWindowSize(m_window.get(), &w, &h); // Use raw pointer
    SDL_GetWindowSize(m_window, &w, &h);
    return h;
}

void Renderer::toggleFullscreen()
{
    Uint32 fullscreen_flag = m_isFullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
    // if (SDL_SetWindowFullscreen(m_window.get(), fullscreen_flag) < 0) { // Use raw pointer
    if (SDL_SetWindowFullscreen(m_window, fullscreen_flag) < 0)
    {
        std::cerr << "Error toggling fullscreen: " << SDL_GetError() << std::endl;
    }
    else
    {
        m_isFullscreen = !m_isFullscreen;
    }
}

Uint32 Renderer::getRequiredSDLInitFlags()
{
    return SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER;
}

#pragma once

#include <SDL.h>     // Core SDL library
#include <string>    // For std::string
#include <vector>    // For std::vector<uint8_t> pixel data
#include <memory>    // For std::unique_ptr
#include <cstdint>   // For uint8_t

// Forward declarations for SDL types used in deleters or as raw pointers
struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

// Custom deleters for SDL resources to use with std::unique_ptr
// These ensure that SDL resources are properly destroyed when their unique_ptr goes out of scope.
struct SDL_Window_Deleter {
    void operator()(SDL_Window* window) const; // Definition in .cpp
};

struct SDL_Renderer_Deleter {
    void operator()(SDL_Renderer* renderer) const; // Definition in .cpp
};

// Renamed SDL_Texture_Deleter to MySDLTextureDeleter to avoid potential name conflicts.
struct MySDLTextureDeleter {
    inline void operator()(SDL_Texture* texture) const {
        if (texture) SDL_DestroyTexture(texture);
    }
};

// --- Renderer Class ---
// Manages SDL window, renderer, and texture for displaying document pages.
class Renderer {
public:
    // Constructor: Initializes SDL window, renderer, and a streaming texture.
    // Throws std::runtime_error if SDL initialization or resource creation fails.
    Renderer(int width, int height, const std::string& title);

    // Destructor: No explicit SDL_Quit() here; it's handled in main.cpp for proper shutdown order.
    ~Renderer();

    // Clears the renderer with a specified RGBA color.
    void clear(Uint8 r, Uint8 g, Uint8 b, Uint8 a);

    // Presents the rendered content to the screen.
    void present();

    // Renders pixel data to the main texture and copies it to the renderer.
    // pixelData: RGB24 pixel data of the source image.
    // srcWidth, srcHeight: Dimensions of the source pixel data.
    // destX, destY: Top-left coordinates for rendering on the window.
    // destWidth, destHeight: Dimensions to scale the image to on the window.
    void renderPage(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight,
                    int destX, int destY, int destWidth, int destHeight);

    // Getters for current window dimensions.
    int getWindowWidth() const;
    int getWindowHeight() const;

    // Getter for the raw SDL_Renderer pointer (needed by TextRenderer).
    SDL_Renderer* getSDLRenderer() const;

    // Getter for the raw SDL_Window pointer (needed for fullscreen toggling).
    SDL_Window* getSDLWindow() const;

    // Toggles fullscreen mode for the window.
    void toggleFullscreen();

private:
    // Smart pointers to manage SDL resources using custom deleters.
    std::unique_ptr<SDL_Window, SDL_Window_Deleter> m_window;
    std::unique_ptr<SDL_Renderer, SDL_Renderer_Deleter> m_renderer;
    // Use the renamed deleter here.
    std::unique_ptr<SDL_Texture, MySDLTextureDeleter> m_texture;
};


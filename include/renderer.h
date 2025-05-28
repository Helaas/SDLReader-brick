#pragma once

#include <SDL.h>    
#include <string>    
#include <vector>    
#include <memory>    
#include <cstdint>   


struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;


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

class Renderer {
public:

    Renderer(int width, int height, const std::string& title);

    ~Renderer();

    void clear(Uint8 r, Uint8 g, Uint8 b, Uint8 a);

    void present();

    // Renders pixel data to the main texture and copies it to the renderer.
    // pixelData: RGB24 pixel data of the source image.
    // srcWidth, srcHeight: Dimensions of the source pixel data.
    // destX, destY: Top-left coordinates for rendering on the window.
    // destWidth, destHeight: Dimensions to scale the image to on the window.
    void renderPage(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight,
                    int destX, int destY, int destWidth, int destHeight);

    int getWindowWidth() const;
    int getWindowHeight() const;

    SDL_Renderer* getSDLRenderer() const;

    SDL_Window* getSDLWindow() const;

    void toggleFullscreen();

private:
    std::unique_ptr<SDL_Window, SDL_Window_Deleter> m_window;
    std::unique_ptr<SDL_Renderer, SDL_Renderer_Deleter> m_renderer;
    std::unique_ptr<SDL_Texture, MySDLTextureDeleter> m_texture;
};


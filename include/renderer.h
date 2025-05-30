#ifndef RENDERER_H
#define RENDERER_H

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

// Custom deleters for SDL unique_ptrs
struct SDL_Window_Deleter {
    void operator()(SDL_Window* window) const;
};

struct SDL_Renderer_Deleter {
    void operator()(SDL_Renderer* renderer) const;
};

struct MySDLTextureDeleter {
    void operator()(SDL_Texture* texture) const {
        if (texture) SDL_DestroyTexture(texture);
    }
};

class Renderer {
public:
    Renderer(int width, int height, const std::string& title);

    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void present();

    SDL_Renderer* getSDLRenderer() const;

    void renderPage(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight);

    int getWindowWidth() const;
    int getWindowHeight() const;

    void toggleFullscreen(); // ADD THIS LINE

private:
    std::unique_ptr<SDL_Window, SDL_Window_Deleter> m_window;
    std::unique_ptr<SDL_Renderer, SDL_Renderer_Deleter> m_renderer;
    std::unique_ptr<SDL_Texture, MySDLTextureDeleter> m_texture;

    int m_currentTexWidth = 0;
    int m_currentTexHeight = 0;
    bool m_isFullscreen = false; // ADD THIS LINE
};

#endif // RENDERER_H
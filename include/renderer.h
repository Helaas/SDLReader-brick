#ifndef RENDERER_H
#define RENDERER_H

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

// Custom deleter for SDL_Texture
struct MySDLTextureDeleter
{
    void operator()(SDL_Texture* texture) const
    {
        if (texture)
            SDL_DestroyTexture(texture);
    }
};

class Renderer
{
public:
    // Constructor now takes pre-initialized SDL_Window and SDL_Renderer
    Renderer(SDL_Window* window, SDL_Renderer* renderer);
    // Removed width, height, and title as they are handled by the window creation externally

    void clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
    void present();

    SDL_Renderer* getSDLRenderer() const; // This method remains useful

    void renderPageEx(const std::vector<uint8_t>& pixelData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight, double angleDeg, SDL_RendererFlip flip);

    void renderPageExARGB(const std::vector<uint32_t>& argbData, int srcWidth, int srcHeight, int destX, int destY, int destWidth, int destHeight, double angleDeg, SDL_RendererFlip flip);

    int getWindowWidth() const;
    int getWindowHeight() const;

    void toggleFullscreen();

    // Static method to get required SDL initialization flags
    static Uint32 getRequiredSDLInitFlags();

private:
    // Now holds raw pointers as ownership is external
    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    std::unique_ptr<SDL_Texture, MySDLTextureDeleter> m_texture;

    int m_currentTexWidth = 0;
    int m_currentTexHeight = 0;
    bool m_isFullscreen = false;
};

#endif // RENDERER_H

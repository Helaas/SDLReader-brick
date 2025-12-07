#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <memory>
#include <string>

#include "renderer.h"

struct SDL_Renderer;

struct TTF_Font_Deleter
{

    inline void operator()(TTF_Font* font) const
    {
        if (font)
            TTF_CloseFont(font);
    }
};

// --- TextRenderer Class ---

class TextRenderer
{
public:
    TextRenderer(SDL_Renderer* renderer, const std::string& fontPath, int fontSize);

    // Destructor: No explicit TTF_Quit() here; it's handled in main.cpp for proper shutdown order.
    ~TextRenderer();

    void setFontSize(int scale);

    void renderText(const std::string& text, int x, int y, SDL_Color color);

    // Measure text dimensions at current font size; returns false on error.
    bool measureText(const std::string& text, int& width, int& height) const;

    // Render text with rotation applied around its center (or an optional pivot).
    void renderTextRotated(const std::string& text, float x, float y, SDL_Color color,
                           double angleDeg, const SDL_Point* centerOverride = nullptr);

private:
    SDL_Renderer* m_sdlRenderer;
    std::unique_ptr<TTF_Font, TTF_Font_Deleter> m_font;
    std::string m_fontPath;
    int m_baseFontSize;
    int m_currentFontSize;
};

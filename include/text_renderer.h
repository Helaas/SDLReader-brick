#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <memory>
#include <string>

#include "renderer.h"

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
    TextRenderer(Renderer* renderer, const std::string& fontPath, int fontSize);

    // Destructor: No explicit TTF_Quit() here; it's handled in main.cpp for proper shutdown order.
    ~TextRenderer();

    void setFontSize(int scale);

    void renderText(const std::string& text, int x, int y, SDL_Color color);

private:
    Renderer* m_renderer;
    std::unique_ptr<TTF_Font, TTF_Font_Deleter> m_font;
    std::string m_fontPath;
    int m_baseFontSize;
    int m_currentFontSize;
};

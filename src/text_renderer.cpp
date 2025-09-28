#include "text_renderer.h"
#include <algorithm> // For std::max
#include <iostream>
#include <stdexcept> // For std::runtime_error

// --- TextRenderer Class ---

TextRenderer::TextRenderer(SDL_Renderer* renderer, const std::string& fontPath, int fontSize)
    : m_sdlRenderer(renderer), m_fontPath(fontPath), m_baseFontSize(fontSize), m_currentFontSize(0)
{
    if (TTF_Init() == -1)
    {
        // TBD - change to overlay warning message, prompting user to exit.
        throw std::runtime_error("SDL_ttf could not initialize! TTF_Error: " + std::string(SDL_GetError()));
    }
}

TextRenderer::~TextRenderer()
{
    // No TTF_Quit() here, segfault
}

// Sets the font size dynamically.
// This function re-opens the font if the new size is different from the current one,
// ensuring text clarity at various zoom levels.
void TextRenderer::setFontSize(int scale)
{
    // Calculate the new font size based on the base size and the current scale.
    int newFontSize = static_cast<int>(m_baseFontSize * (scale / 100.0));
    // Ensure a minimum legible font size to prevent text from becoming too small.
    newFontSize = std::max(8, newFontSize); // Minimum font size of 8 pixels

    // Only re-open the font if it's not currently loaded OR if the size has changed.
    if (!m_font || newFontSize != m_currentFontSize)
    {
        if (m_font)
        {
            m_font.release();
        }

        m_font.reset(TTF_OpenFont(m_fontPath.c_str(), newFontSize));
        if (!m_font)
        {
            // TBD - change to overlay warning message, prompting user to exit.
            std::cerr << "Error: Failed to load font: " << m_fontPath << " at size: " << newFontSize << "! TTF_Error: " << TTF_GetError() << std::endl;
            // As a fallback, try to load the font at its base size.
            m_font.reset(TTF_OpenFont(m_fontPath.c_str(), m_baseFontSize));
            if (!m_font)
            {
                // TBD - change to overlay warning message, prompting user to exit.
                throw std::runtime_error("Failed to load font: " + m_fontPath + " at base size after error!");
            }
            m_currentFontSize = m_baseFontSize;
        }
        else
        {
            m_currentFontSize = newFontSize;
        }
    }
}

void TextRenderer::renderText(const std::string& text, int x, int y, SDL_Color color)
{
    if (text.empty())
        return; // Nothing to render if text is empty
    if (!m_font)
    {
        std::cerr << "Error: Font not loaded for rendering text." << std::endl;
        return;
    }

    std::unique_ptr<SDL_Surface, void (*)(SDL_Surface*)> textSurface(
        TTF_RenderText_Blended(m_font.get(), text.c_str(), color),
        SDL_FreeSurface);
    if (!textSurface)
    {
        std::cerr << "Error: Unable to render text surface! TTF_Error: " << TTF_GetError() << std::endl;
        return;
    }

    std::unique_ptr<SDL_Texture, MySDLTextureDeleter> textTexture(
        SDL_CreateTextureFromSurface(m_sdlRenderer, textSurface.get()));
    if (!textTexture)
    {
        std::cerr << "Error: Unable to create texture from rendered text! SDL_Error: " << SDL_GetError() << std::endl;
        return;
    }

    SDL_Rect renderQuad = {x, y, textSurface->w, textSurface->h};

    SDL_RenderCopy(m_sdlRenderer, textTexture.get(), NULL, &renderQuad);
}

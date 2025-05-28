#pragma once

#include <SDL.h>     // Core SDL library
#include <SDL_ttf.h> // SDL_ttf library for font rendering
#include <string>    // For std::string
#include <memory>    // For std::unique_ptr

#include "renderer.h" // Include renderer.h to get the definition of MySDLTextureDeleter

// Forward declaration for SDL_Renderer (used by TextRenderer)
// Note: SDL_Renderer is already defined via SDL.h, but keeping this forward declaration
// is harmless and can be useful if SDL.h were not included here.
struct SDL_Renderer;
// Forward declaration for TTF_Font (used by unique_ptr)
// struct _TTF_Font; // SDL_ttf uses this internal struct name - No longer needed as TTF_Font is used directly.

// Custom deleter for TTF_Font to use with std::unique_ptr.
// Ensures the font is properly closed when its unique_ptr goes out of scope.
struct TTF_Font_Deleter {
    // Changed parameter type to TTF_Font* to match std::unique_ptr's expected type.
    // Made the definition inline to ensure it's available wherever unique_ptr is instantiated.
    inline void operator()(TTF_Font* font) const {
        if (font) TTF_CloseFont(font);
    }
};

// --- TextRenderer Class ---
// Manages font loading and rendering text onto an SDL_Renderer.
class TextRenderer {
public:
    // Constructor: Initializes SDL_ttf and loads a font.
    // Throws std::runtime_error if SDL_ttf initialization or font loading fails.
    TextRenderer(SDL_Renderer* renderer, const std::string& fontPath, int fontSize);

    // Destructor: No explicit TTF_Quit() here; it's handled in main.cpp for proper shutdown order.
    ~TextRenderer();

    // Sets the font size dynamically.
    // This method will re-open the font at the new size if it differs from the current size.
    // It also enforces a minimum legible font size.
    void setFontSize(int scale);

    // Renders text to the specified coordinates on the SDL_Renderer with a given color.
    void renderText(const std::string& text, int x, int y, SDL_Color color);

private:
    SDL_Renderer* m_sdlRenderer; // Raw pointer to the SDL renderer (owned by Renderer class)
    std::unique_ptr<TTF_Font, TTF_Font_Deleter> m_font; // Smart pointer for the loaded font
    std::string m_fontPath;      // Path to the font file, stored to allow re-opening
    int m_baseFontSize;          // The initial/base font size
    int m_currentFontSize;       // The currently active font size, used to avoid unnecessary reloads
};

#pragma once

#include <string>    // For std::string
#include <memory>    // For std::unique_ptr
#include <SDL.h>     // For SDL_Event, SDL_Keycode, SDL_Keymod, Uint32

#include "document.h"      // Include the full definition of the Document base class
#include "renderer.h"      // Include the full definition of the Renderer class
#include "text_renderer.h" // Include the full definition of the TextRenderer class

// Forward declarations for classes used as unique_ptr members
// Note: Document, Renderer, and TextRenderer no longer need forward declarations here
// as their full definitions are now included.
// class Renderer; // Removed
// class TextRenderer; // Removed

// --- App Class ---
// The main application class, orchestrating the document loading, rendering,
// user input handling, and overall application loop.
class App {
public:
    // Constructor: Initializes the application with a document filename and window dimensions.
    // Throws std::runtime_error if initialization fails (e.g., file not found, unsupported format).
    App(const std::string& filename, int initialWidth, int initialHeight);

    // Main application loop. This method keeps the application running until a quit event occurs.
    void run();

private:
    bool m_running; // Flag to control the main application loop

    // Smart pointers to manage the core components of the application.
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<TextRenderer> m_textRenderer;
    std::unique_ptr<Document> m_document; // Polymorphic pointer to the specific document type

    int m_currentPage;  // Current page number (0-indexed)
    int m_pageCount;    // Total number of pages in the document
    int m_currentScale; // Current zoom scale percentage (e.g., 100 for 100%)
    int m_scrollX;      // Current horizontal scroll offset
    int m_scrollY;      // Current vertical scroll offset

    int m_pageWidth;    // Rendered width of the current page at m_currentScale
    int m_pageHeight;   // Rendered height of the current page at m_currentScale

    // --- Event Handling ---
    // Dispatches SDL events to appropriate handlers.
    void handleEvent(const SDL_Event& event);

    // Handles keyboard key down events.
    void handleKeyDown(SDL_Keycode key, SDL_Keymod mod);

    // Handles mouse wheel scroll events.
    void handleMouseWheel(int y_delta, SDL_Keymod mod);

    // Handles mouse motion events (for dragging).
    void handleMouseMotion(int x_rel, int y_rel, Uint32 mouse_state);

    // --- State Update Methods ---
    // Changes the current page by a given delta (+1 for next, -1 for previous).
    // Returns true if the page changed, false otherwise.
    bool changePage(int delta);

    // Jumps directly to a specific page number.
    // Returns true if the page changed, false otherwise.
    bool jumpToPage(int pageNum);

    // Changes the zoom scale by a given delta.
    // Returns true if the scale changed, false otherwise.
    bool changeScale(int delta);

    // Changes the scroll position by given deltas.
    // Returns true if the scroll position changed, false otherwise.
    bool changeScroll(int deltaX, int deltaY);

    // Placeholder for any continuous update logic (not heavily used in this reader).
    void update();

    // --- Rendering Methods ---
    // Renders the current page and updates the display.
    void renderCurrentPage();

    // Renders the UI overlay (page number, scale info) on top of the page.
    void renderUIOverlay();

    // Main render function, calls renderCurrentPage.
    void render();
};

#pragma once

#include <string>    // For std::string
#include <memory>    // For std::unique_ptr
#include <SDL.h>     // For SDL_Event, SDL_Keycode, SDL_Keymod, Uint32

#include "document.h"      // Include the full definition of the Document base class
#include "renderer.h"      // Include the full definition of the Renderer class
#include "text_renderer.h" // Include the full definition of the TextRenderer class

// --- App Class ---

class App {
public:
    App(const std::string& filename, int initialWidth, int initialHeight);
    void run();

private:
    bool m_running; 

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

    // Touch/Mouse state for dragging
    bool m_isDragging;
    float m_lastTouchX; // Stored in normalized coordinates (0.0-1.0) for touch, pixel for mouse
    float m_lastTouchY; // Stored in normalized coordinates (0.0-1.0) for touch, pixel for mouse

    // Game Controller management
    SDL_GameController* m_gameController; // Pointer to the currently active game controller
    SDL_JoystickID m_gameControllerInstanceID; // Instance ID of the active controller's joystick

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
    bool changePage(int delta);

    // Jumps directly to a specific page number.
    bool jumpToPage(int pageNum);

    // Changes the zoom scale by a given delta.
    bool changeScale(int delta);

    // Changes the scroll position by given deltas.
    bool changeScroll(int deltaX, int deltaY);

    void update();

    // --- Rendering Methods ---
    void renderCurrentPage();

    // Renders the UI overlay (page number, scale info) on top of the page.
    void renderUIOverlay();

    void render();
};

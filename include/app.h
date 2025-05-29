#ifndef APP_H
#define APP_H

#include "renderer.h"
#include "document.h"
#include "text_renderer.h"

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

// Define AppAction enum outside the class for global scope or within a namespace
// if not preferred as a nested enum. For simplicity, nesting it here.
class App {
public:
    // Define an enum for high-level application actions
    enum class AppAction {
        None,
        Quit,
        Resize,
        ScrollUp,
        ScrollDown,
        ScrollLeft,
        ScrollRight,
        PageNext,
        PagePrevious,
        ZoomIn,
        ZoomOut,
        ToggleFullscreen,
        DragStart, // Indicate a drag operation has started
        DragEnd    // Indicate a drag operation has ended
    };

    App(const std::string& filename, int initialWidth, int initialHeight);
    ~App() = default; // Use default destructor for smart pointers

    void run();

private:
    // Event Handling
    void handleEvent(const SDL_Event& event);
    AppAction mapMouseWheelToAppAction(int y_delta, SDL_Keymod mod, int& deltaValue);
    AppAction mapControllerAxisToAppAction(SDL_GameControllerAxis axis, Sint16 value, int& deltaValue);
    bool processAppAction(AppAction action, int deltaValue);

    // State Update Methods
    bool changePage(int delta);
    bool jumpToPage(int pageNum); // Added for completeness, though not currently used in event handling
    bool changeScale(int delta);
    bool changeScroll(int deltaX, int deltaY);
    void update(); // Placeholder for continuous update logic

    // Rendering Methods
    void renderCurrentPage();
    void renderUIOverlay();
    void render();

    bool m_running;
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<Document> m_document;
    std::unique_ptr<TextRenderer> m_textRenderer;

    int m_currentPage;
    int m_pageCount;
    int m_currentScale;
    int m_scrollX;
    int m_scrollY;
    int m_pageWidth;
    int m_pageHeight;

    bool m_isDragging;
    float m_lastTouchX; // Store normalized coordinates for touch, pixel for mouse
    float m_lastTouchY;

    SDL_GameController* m_gameController;
    SDL_JoystickID m_gameControllerInstanceID;
};

#endif // APP_H

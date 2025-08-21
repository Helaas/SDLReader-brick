#ifndef APP_H
#define APP_H

#include "renderer.h"
#include "document.h"
#include "text_renderer.h"

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

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
        DragStart,
        DragEnd
    };

    // Constructor now accepts pre-initialized SDL_Window* and SDL_Renderer*
    App(const std::string& filename, SDL_Window* window, SDL_Renderer* renderer);
    ~App();

    void run();

private:
    // Document and Rendering Management
    void loadDocument();
    void renderCurrentPage();
    void renderUI();

    // Event Handling
    void handleEvent(const SDL_Event& event);

    // Page Navigation
    void goToNextPage();
    void goToPreviousPage();
    void goToPage(int pageNum);

    // Zoom and Scaling
    void zoom(int delta);
    void zoomTo(int scale);
    void fitPageToWindow();
    void recenterScrollOnZoom(int oldScale, int newScale);

    // Scrolling
    void clampScroll();

    // State Management
    void resetPageView();
    void printAppState();

    bool m_running;

    // Renderer now owns a unique_ptr to Renderer, which internally holds raw pointers
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
    float m_lastTouchX;
    float m_lastTouchY;

    // R2 State
    bool m_r2Held;

    // Game Controller
    SDL_GameController* m_gameController;
    SDL_JoystickID m_gameControllerInstanceID;

    // Helper to initialize and close game controllers
    void initializeGameControllers();
    void closeGameControllers();
};

#endif // APP_H
#ifndef APP_H
#define APP_H

#include "renderer.h"
#include "document.h"
#include "text_renderer.h"

#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

class App
{
public:
    // Define an enum for high-level application actions
    enum class AppAction
    {
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
    App(const std::string &filename, SDL_Window *window, SDL_Renderer *renderer);
    ~App();

    void run();

private:
    // Document and Rendering Management
    void loadDocument();
    void renderCurrentPage();
    void renderUI();

    // Event Handling
    void handleEvent(const SDL_Event &event);

    // Page Navigation
    void goToNextPage();
    void goToPreviousPage();
    void goToPage(int pageNum);
    void jumpPages(int delta);

    // Zoom and Scaling
    void zoom(int delta);
    void zoomTo(int scale);
    void fitPageToWindow();
    void recenterScrollOnZoom(int oldScale, int newScale);
    void fitPageToWidth();

    // Scrolling
    void clampScroll();

    // State Management
    void resetPageView();
    void printAppState();

    // Rotation and mirroring
    void rotateClockwise();
    void toggleMirrorVertical();
    void toggleMirrorHorizontal();

    // D-pad hold state
    bool m_dpadLeftHeld{false};
    bool m_dpadRightHeld{false};
    bool m_dpadUpHeld{false};
    bool m_dpadDownHeld{false};

    // Pan speed (pixels per second)
    float m_dpadPanSpeed{600.0f};

    // Simple timestep
    Uint64 m_prevTick{0};

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

    // Game Controller
    SDL_GameController *m_gameController;
    SDL_JoystickID m_gameControllerInstanceID;

    // Helper to initialize and close game controllers
    void initializeGameControllers();
    void closeGameControllers();

    // Per-frame panning when D-pad is held
    void updateHeldPanning(float dt);

    // --- edge-turn timing ---
    float m_edgeTurnHoldRight{0.0f};
    float m_edgeTurnHoldLeft{0.0f};
    float m_edgeTurnHoldUp{0.0f};
    float m_edgeTurnHoldDown{0.0f};

    float m_edgeTurnThreshold{0.15f}; // seconds to dwell at edge before flipping

    // Try a one-shot nudge; if at the edge, flip the page instead.
    void handleDpadNudgeRight();
    void handleDpadNudgeLeft();
    void handleDpadNudgeUp();
    void handleDpadNudgeDown();

    // Scroll extents helpers
    int getMaxScrollX() const;
    int getMaxScrollY() const; // if you later want vertical logic

    // Recompute scaled page dims for current page at the existing zoom and clamp scroll.
    void onPageChangedKeepZoom();

    // Top alignment control when page height <= window height
    bool m_topAlignWhenFits{true};         // prefer top-align instead of centering when it fits
    bool m_forceTopAlignNextRender{false}; // one-shot flag after page changes

    // Align the current page so its top is visible
    void alignToTopOfCurrentPage();

    // --- View transforms ---
    int m_rotation{0}; // 0, 90, 180, 270
    bool m_mirrorH{false};
    bool m_mirrorV{false};

    // Effective (native) page size with rotation
    int effectiveNativeWidth() const;
    int effectiveNativeHeight() const;

    // Compose SDL flip flags
    SDL_RendererFlip currentFlipFlags() const;
};

#endif // APP_H
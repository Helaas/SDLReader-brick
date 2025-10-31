#ifndef RENDER_MANAGER_H
#define RENDER_MANAGER_H

#include <SDL.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Forward declarations
class App;
class Renderer;
class TextRenderer;
class Document;
class MuPdfDocument;
class ViewportManager;
class NavigationManager;

/**
 * @brief Structure to hold render state and timing information
 */
struct RenderState
{
    bool needsRedraw = true;         // Flag to indicate when screen needs to be redrawn
    Uint32 lastRenderDuration = 300; // Last render time in milliseconds (default 300ms)

    // UI display timers
    Uint32 scaleDisplayTime = 0;
    Uint32 pageDisplayTime = 0;
    Uint32 errorMessageTime = 0;

    // UI display durations
    static constexpr Uint32 SCALE_DISPLAY_DURATION = 2000; // 2 seconds
    static constexpr Uint32 PAGE_DISPLAY_DURATION = 2000;  // 2 seconds
    static constexpr Uint32 ERROR_MESSAGE_DURATION = 3000; // 3 seconds

    // Error message state
    std::string errorMessage;

    // Fake sleep mode state
    bool inFakeSleep = false;
};

/**
 * @brief Manages all rendering operations including page rendering and UI overlay
 */
class RenderManager
{
public:
    RenderManager(SDL_Window* window, SDL_Renderer* renderer);
    ~RenderManager() = default;

    // Initialization
    bool initialize();

    // Core rendering methods
    void renderCurrentPage(Document* document, NavigationManager* navigationManager,
                           ViewportManager* viewportManager, std::mutex& documentMutex,
                           bool isDragging);
    void renderUI(class App* app, NavigationManager* navigationManager, ViewportManager* viewportManager);
    void renderFakeSleepScreen();

    // Render state management
    void markDirty()
    {
        m_state.needsRedraw = true;
    }
    bool needsRedraw() const
    {
        return m_state.needsRedraw;
    }
    void clearDirtyFlag()
    {
        m_state.needsRedraw = false;
    }

    // Timing and performance
    Uint32 getLastRenderDuration() const
    {
        return m_state.lastRenderDuration;
    }
    void setLastRenderDuration(Uint32 duration)
    {
        m_state.lastRenderDuration = duration;
    }

    // UI display timing
    void updateScaleDisplayTime()
    {
        m_state.scaleDisplayTime = SDL_GetTicks();
    }
    void updatePageDisplayTime()
    {
        m_state.pageDisplayTime = SDL_GetTicks();
    }

    // Error message display
    void showErrorMessage(const std::string& message);
    void clearErrorMessage()
    {
        m_state.errorMessage.clear();
    }

    // Fake sleep mode
    void setFakeSleepMode(bool enabled)
    {
        m_state.inFakeSleep = enabled;
        markDirty();
    }
    bool isInFakeSleepMode() const
    {
        return m_state.inFakeSleep;
    }

    // Renderer access
    Renderer* getRenderer() const
    {
        return m_renderer.get();
    }
    TextRenderer* getTextRenderer() const
    {
        return m_textRenderer.get();
    }

    // Present the rendered frame
    void present();

    // Set background color for margins
    void setBackgroundColor(uint8_t r, uint8_t g, uint8_t b)
    {
        m_bgColorR = r;
        m_bgColorG = g;
        m_bgColorB = b;
    }

    // Clear cached render and dimension cache (for document load/reset)
    void clearLastRender(Document* document);

private:
    // Rendering resources
    std::unique_ptr<Renderer> m_renderer;
    std::unique_ptr<TextRenderer> m_textRenderer;

    // Render state
    RenderState m_state;

    // Background color for document margins
    uint8_t m_bgColorR = 255;
    uint8_t m_bgColorG = 255;
    uint8_t m_bgColorB = 255;

    // Cached render for rapid zoom previews
    std::shared_ptr<const std::vector<uint32_t>> m_lastArgbBuffer;
    int m_lastArgbWidth = 0;
    int m_lastArgbHeight = 0;
    int m_lastArgbPage = -1;
    int m_lastArgbScale = 0;
    bool m_lastArgbValid = false;
    bool m_previewActive = false;

    // UI rendering methods
    void renderPageInfo(NavigationManager* navigationManager, int windowWidth, int windowHeight);
    void renderScaleInfo(ViewportManager* viewportManager, int windowWidth, int windowHeight);
    void renderZoomProcessingIndicator(ViewportManager* viewportManager, int windowWidth, int windowHeight);
    void renderErrorMessage(int windowWidth, int windowHeight);
    void renderPageJumpInput(NavigationManager* navigationManager, int windowWidth, int windowHeight);
    void renderEdgeTurnProgressIndicator(class App* app, NavigationManager* navigationManager,
                                         ViewportManager* viewportManager, int windowWidth, int windowHeight);

    // Helper methods
    uint32_t rgb24_to_argb32(uint8_t r, uint8_t g, uint8_t b);
    void renderProgressBar(int x, int y, int width, int height, float progress, SDL_Color bgColor, SDL_Color fillColor);
    SDL_Color getContrastingTextColor() const;
    void storeLastRender(int page, int scale, std::shared_ptr<const std::vector<uint32_t>> buffer, int width, int height);
};

#endif // RENDER_MANAGER_H

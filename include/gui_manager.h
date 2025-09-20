#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include "font_manager.h"
#include <SDL.h>
#include <string>
#include <functional>
#include <iostream>

// Forward declarations
struct ImGuiContext;
struct ImFont;

/**
 * @brief Manages Dear ImGui integration and font selection GUI
 */
class GuiManager {
public:
    GuiManager();
    ~GuiManager();

    /**
     * @brief Initialize ImGui with SDL
     * @param window SDL window
     * @param renderer SDL renderer
     * @return true if successful
     */
    bool initialize(SDL_Window* window, SDL_Renderer* renderer);

    /**
     * @brief Cleanup ImGui resources
     */
    void cleanup();

    /**
     * @brief Handle SDL events for ImGui
     * @param event SDL event
     * @return true if event was handled by ImGui
     */
    bool handleEvent(const SDL_Event& event);

    /**
     * @brief Start a new ImGui frame
     */
    void newFrame();

    /**
     * @brief Render all ImGui windows and finish frame
     */
    void render();

    /**
     * @brief Check if the font menu is currently visible
     * @return true if font menu is visible
     */
    bool isFontMenuVisible() const;

    /**
     * @brief Toggle the font menu visibility
     */
    void toggleFontMenu() { 
        m_showFontMenu = !m_showFontMenu; 
        std::cout << "Font menu " << (m_showFontMenu ? "opened" : "closed") << std::endl;
    }

    /**
     * @brief Set callback for when font configuration changes
     * @param callback Function to call when Apply is pressed
     */
    void setFontApplyCallback(std::function<void(const FontConfig&)> callback) {
        m_fontApplyCallback = callback;
    }

    /**
     * @brief Set callback for when font menu is closed
     * @param callback Function to call when Close is pressed
     */
    void setFontCloseCallback(std::function<void()> callback) {
        m_closeCallback = callback;
    }

    /**
     * @brief Set the current font configuration (for initialization)
     * @param config Current font config
     */
    void setCurrentFontConfig(const FontConfig& config);

    /**
     * @brief Get the current font configuration
     */
    const FontConfig& getCurrentFontConfig() const { return m_currentConfig; }

    /**
     * @brief Check if ImGui wants to capture mouse input
     */
    bool wantsCaptureMouse() const;

    /**
     * @brief Check if ImGui wants to capture keyboard input
     */
    bool wantsCaptureKeyboard() const;

private:
    bool m_initialized = false;
    bool m_showFontMenu = false;
    SDL_Renderer* m_renderer = nullptr; // Store renderer for ImGui
    FontManager m_fontManager;
    FontConfig m_currentConfig;
    FontConfig m_tempConfig; // Temporary config for UI editing
    // Callbacks
    std::function<void(const FontConfig&)> m_fontApplyCallback;
    std::function<void()> m_closeCallback;
    
    // ImGui font for preview
    ImFont* m_previewFont = nullptr;
    std::string m_lastPreviewFontPath;
    int m_lastPreviewFontSize = 0;
    
    // UI state
    int m_selectedFontIndex = 0;
    char m_fontSizeInput[16] = "12";  // Increased buffer size for safety
    bool m_fontSizeChanged = false;
    
    /**
     * @brief Render the font selection menu
     */
    void renderFontMenu();
    
    /**
     * @brief Update preview font if needed
     */
    void updatePreviewFont();
    
    /**
     * @brief Find index of font in available fonts list
     * @param fontName Display name to find
     * @return Index or 0 if not found
     */
    int findFontIndex(const std::string& fontName) const;
};

#endif // GUI_MANAGER_H
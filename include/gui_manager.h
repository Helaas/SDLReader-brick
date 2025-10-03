#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include "options_manager.h"
#include <SDL.h>
#include <functional>
#include <iostream>
#include <string>

// Forward declarations
struct ImGuiContext;
struct ImFont;

/**
 * @brief Manages Dear ImGui integration and font selection GUI
 */
class GuiManager
{
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
    void toggleFontMenu()
    {
        m_showFontMenu = !m_showFontMenu;
        if (m_showFontMenu)
        {
            m_justOpenedFontMenu = true; // Set focus flag when opening
        }
        std::cout << "Font menu " << (m_showFontMenu ? "opened" : "closed") << std::endl;
    }

    /**
     * @brief Check if the font menu is currently open
     */
    bool isFontMenuOpen() const
    {
        return m_showFontMenu;
    }

    /**
     * @brief Set callback for when font configuration changes
     * @param callback Function to call when Apply is pressed
     */
    void setFontApplyCallback(std::function<void(const FontConfig&)> callback)
    {
        m_fontApplyCallback = callback;
    }

    /**
     * @brief Set callback for when font menu is closed
     * @param callback Function to call when Close is pressed
     */
    void setFontCloseCallback(std::function<void()> callback)
    {
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
    const FontConfig& getCurrentFontConfig() const
    {
        return m_currentConfig;
    }

    /**
     * @brief Check if ImGui wants to capture mouse input
     */
    bool wantsCaptureMouse() const;

    /**
     * @brief Check if ImGui wants to capture keyboard input
     */
    bool wantsCaptureKeyboard() const;

    /**
     * @brief Set callback for when jump to page is requested
     * @param callback Function to call when page jump is requested
     */
    void setPageJumpCallback(std::function<void(int)> callback)
    {
        m_pageJumpCallback = callback;
    }

    /**
     * @brief Set the total page count for page jump validation
     * @param pageCount Total number of pages in document
     */
    void setPageCount(int pageCount)
    {
        m_pageCount = pageCount;
    }

    /**
     * @brief Set current page number for display in page jump
     * @param currentPage Current page number (0-based)
     */
    void setCurrentPage(int currentPage)
    {
        m_currentPage = currentPage;
    }

    /**
     * @brief Check if the number pad is currently visible
     */
    bool isNumberPadVisible() const
    {
        return m_showNumberPad;
    }

private:
    bool m_initialized = false;
    bool m_showFontMenu = false;
    SDL_Renderer* m_renderer = nullptr; // Store renderer for ImGui
    OptionsManager m_optionsManager;
    FontConfig m_currentConfig;
    FontConfig m_tempConfig; // Temporary config for UI editing

    // Page navigation
    int m_pageCount = 0;
    int m_currentPage = 0;

    // Callbacks
    std::function<void(const FontConfig&)> m_fontApplyCallback;
    std::function<void()> m_closeCallback;
    std::function<void(int)> m_pageJumpCallback;

    // UI state
    int m_selectedFontIndex = 0;
    char m_fontSizeInput[16] = "12"; // Increased buffer size for safety
    char m_zoomStepInput[16] = "10"; // Zoom step input
    char m_pageJumpInput[16] = "1";  // Page jump input
    bool m_fontSizeChanged = false;
    bool m_justOpenedFontMenu = false; // Flag to set focus on font dropdown when menu opens

    // On-screen number pad state
    bool m_showNumberPad = false;
    int m_numberPadSelectedRow = 0; // Selected row in number pad (0-4)
    int m_numberPadSelectedCol = 0; // Selected column in number pad (0-2)

    // Debouncing for number pad input
    Uint32 m_lastButtonPressTime = 0;
    const Uint32 BUTTON_DEBOUNCE_MS = 100;

    // Font names for dropdown (persistent to avoid lifetime issues)
    std::vector<std::string> m_fontNames;

    /**
     * @brief Render the font selection menu
     */
    void renderFontMenu();

    /**
     * @brief Render the on-screen number pad
     */
    void renderNumberPad();

    /**
     * @brief Handle controller input for number pad navigation
     * @param event SDL event to process
     * @return true if event was handled by number pad
     */
    bool handleNumberPadInput(const SDL_Event& event);

    /**
     * @brief Show the on-screen number pad
     */
    void showNumberPad();

    /**
     * @brief Hide the on-screen number pad
     */
    void hideNumberPad();

    /**
     * @brief Find index of font in available fonts list
     * @param fontName Display name to find
     * @return Index or 0 if not found
     */
    int findFontIndex(const std::string& fontName) const;
};

#endif // GUI_MANAGER_H

#ifndef NUKLEAR_GUI_MANAGER_H
#define NUKLEAR_GUI_MANAGER_H

#include "base_gui_manager.h"
#include "options_manager.h"
#include <SDL.h>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Forward declaration for Nuklear context
struct nk_context;

/**
 * @brief Manages Nuklear GUI integration for font selection and controls
 */
class NuklearGuiManager : public BaseGuiManager
{
public:
    NuklearGuiManager();
    ~NuklearGuiManager();

    /**
     * @brief Initialize Nuklear with SDL
     * @param window SDL window
     * @param renderer SDL renderer
     * @return true if successful
     */
    bool initialize(SDL_Window* window, SDL_Renderer* renderer) override;

    /**
     * @brief Cleanup Nuklear resources
     */
    void cleanup() override;

    /**
     * @brief Handle SDL events for Nuklear
     * @param event SDL event
     * @return true if event was handled by Nuklear
     */
    bool handleEvent(const SDL_Event& event) override;

    /**
     * @brief Start a new Nuklear frame
     */
    void newFrame() override;

    /**
     * @brief Render all Nuklear windows and finish frame
     */
    void render() override;

    /**
     * @brief Check if the font menu is currently visible
     * @return true if font menu is visible
     */
    bool isFontMenuVisible() const override;

    /**
     * @brief Toggle the font menu visibility
     */
    void toggleFontMenu()
    {
        m_showFontMenu = !m_showFontMenu;
        std::cout << "Font menu " << (m_showFontMenu ? "opened" : "closed") << std::endl;
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
     * @brief Check if Nuklear wants to capture mouse input
     */
    bool wantsCaptureMouse() const;

    /**
     * @brief Check if Nuklear wants to capture keyboard input
     */
    bool wantsCaptureKeyboard() const;

    /**
     * @brief Set callback for when jump to page is requested
     * @param callback Function to call when page jump is requested
     */
    void setPageSelectionCallback(std::function<void(int)> callback) override
    {
        m_pageJumpCallback = callback;
    }

    /**
     * @brief Set callback for font selection
     * @param callback Function to call when font is selected
     */
    void setFontSelectionCallback(std::function<void(const std::string&)> callback) override;

    /**
     * @brief Set callback for page jump (alias for setPageSelectionCallback)
     * @param callback Function to call when page jump is requested
     */
    void setPageJumpCallback(std::function<void(int)> callback)
    {
        setPageSelectionCallback(callback);
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
    void setCurrentPage(int currentPage) override
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
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;

    // Nuklear context (managed by SDL renderer backend)
    struct nk_context* m_ctx = nullptr;

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
    char m_fontSizeInput[16] = "12";
    char m_zoomStepInput[16] = "10";
    char m_pageJumpInput[16] = "1";
    bool m_fontSizeChanged = false;

    // On-screen number pad state
    bool m_showNumberPad = false;
    int m_numberPadSelectedRow = 0;
    int m_numberPadSelectedCol = 0;

    // Debouncing for number pad input
    Uint32 m_lastButtonPressTime = 0;
    const Uint32 BUTTON_DEBOUNCE_MS = 100;

    // Font names for dropdown
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
    void showNumberPad() override;

    /**
     * @brief Hide the on-screen number pad
     */
    void hideNumberPad() override;

    /**
     * @brief Find index of font in available fonts list
     * @param fontName Display name to find
     * @return Index or 0 if not found
     */
    int findFontIndex(const std::string& fontName) const;

    /**
     * @brief Set up an attractive color scheme for the GUI
     */
    void setupColorScheme();

    /**
     * @brief Handle controller input for general GUI navigation
     * @param event SDL event to process
     * @return true if event was handled
     */
    bool handleControllerInput(const SDL_Event& event);
};

#endif // NUKLEAR_GUI_MANAGER_H

#ifndef BASE_GUI_MANAGER_H
#define BASE_GUI_MANAGER_H

#include <SDL.h>
#include <functional>

/**
 * @brief Base interface for GUI managers
 */
class BaseGuiManager
{
public:
    virtual ~BaseGuiManager() = default;

    /**
     * @brief Initialize GUI with SDL
     * @param window SDL window
     * @param renderer SDL renderer
     * @return true if successful
     */
    virtual bool initialize(SDL_Window* window, SDL_Renderer* renderer) = 0;

    /**
     * @brief Cleanup GUI resources
     */
    virtual void cleanup() = 0;

    /**
     * @brief Handle SDL events for GUI
     * @param event SDL event
     * @return true if event was handled by GUI
     */
    virtual bool handleEvent(const SDL_Event& event) = 0;

    /**
     * @brief Start a new GUI frame
     */
    virtual void newFrame() = 0;

    /**
     * @brief Render all GUI windows and finish frame
     */
    virtual void render() = 0;

    /**
     * @brief Check if the font menu is currently visible
     * @return true if font menu is visible
     */
    virtual bool isFontMenuVisible() const = 0;

    /**
     * @brief Toggle the font menu visibility
     */
    virtual void toggleFontMenu() = 0;

    /**
     * @brief Set the current page number
     * @param currentPage Current page number
     */
    virtual void setCurrentPage(int currentPage) = 0;

    /**
     * @brief Check if the on-screen number pad is visible
     * @return true if number pad is visible
     */
    virtual bool isNumberPadVisible() const = 0;

    /**
     * @brief Show the on-screen number pad for page input
     */
    virtual void showNumberPad() = 0;

    /**
     * @brief Hide the on-screen number pad
     */
    virtual void hideNumberPad() = 0;

    /**
     * @brief Set callback for page number selection
     * @param callback Function to call when page is selected
     */
    virtual void setPageSelectionCallback(std::function<void(int)> callback) = 0;

    /**
     * @brief Set callback for font selection
     * @param callback Function to call when font is selected
     */
    virtual void setFontSelectionCallback(std::function<void(const std::string&)> callback) = 0;
};

#endif // BASE_GUI_MANAGER_H
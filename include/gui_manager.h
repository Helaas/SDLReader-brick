#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include "button_mapper.h"
#include "options_manager.h"
#include <SDL.h>
#include <array>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

struct nk_context;

class GuiManager
{
public:
    GuiManager();
    ~GuiManager();

    bool initialize(SDL_Window* window, SDL_Renderer* renderer);
    void cleanup();

    bool handleEvent(const SDL_Event& event);
    void newFrame();
    void render();

    bool isFontMenuVisible() const;
    bool isFontMenuOpen() const
    {
        return m_showFontMenu;
    }
    void toggleFontMenu();

    void setFontApplyCallback(std::function<void(const FontConfig&)> callback)
    {
        m_fontApplyCallback = callback;
    }
    void setFontCloseCallback(std::function<void()> callback)
    {
        m_closeCallback = callback;
    }

    void setCurrentFontConfig(const FontConfig& config);
    const FontConfig& getCurrentFontConfig() const
    {
        return m_currentConfig;
    }

    bool wantsCaptureMouse() const;
    bool wantsCaptureKeyboard() const;

    void setPageJumpCallback(std::function<void(int)> callback)
    {
        m_pageJumpCallback = callback;
    }
    void setPageCount(int pageCount)
    {
        m_pageCount = pageCount;
    }
    void setCurrentPage(int currentPage)
    {
        m_currentPage = currentPage;
    }

    bool isNumberPadVisible() const
    {
        return m_showNumberPad;
    }

    void closeFontMenu();
    void closeNumberPad();
    bool closeAllUIWindows();

    void setButtonMapper(const ButtonMapper* mapper)
    {
        m_buttonMapper = mapper;
    }

    void showNumberPad();
    void hideNumberPad();

private:
    bool m_initialized = false;
    bool m_showFontMenu = false;
    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    nk_context* m_ctx = nullptr;

    OptionsManager m_optionsManager;
    FontConfig m_currentConfig;
    FontConfig m_tempConfig;

    int m_pageCount = 0;
    int m_currentPage = 0;

    std::function<void(const FontConfig&)> m_fontApplyCallback;
    std::function<void()> m_closeCallback;
    std::function<void(int)> m_pageJumpCallback;

    int m_selectedFontIndex = 0;
    int m_selectedStyleIndex = 0;
    char m_fontSizeInput[16] = "12";
    char m_zoomStepInput[16] = "10";
    char m_pageJumpInput[16] = "1";

    bool m_showNumberPad = false;
    int m_numberPadSelectedRow = 0;
    int m_numberPadSelectedCol = 0;

    const ButtonMapper* m_buttonMapper = nullptr;

    enum MainScreenWidget
    {
        WIDGET_FONT_DROPDOWN = 0,
        WIDGET_FONT_SIZE_INPUT,
        WIDGET_FONT_SIZE_SLIDER,
        WIDGET_READING_STYLE_DROPDOWN,
        WIDGET_ZOOM_STEP_INPUT,
        WIDGET_ZOOM_STEP_SLIDER,
        WIDGET_EDGE_PROGRESS_CHECKBOX,
        WIDGET_EDGE_PROGRESS_INFO_BUTTON,
        WIDGET_MINIMAP_CHECKBOX,
        WIDGET_MINIMAP_INFO_BUTTON,
        WIDGET_PAGE_JUMP_INPUT,
        WIDGET_GO_BUTTON,
        WIDGET_NUMPAD_BUTTON,
        WIDGET_APPLY_BUTTON,
        WIDGET_CLOSE_BUTTON,
        WIDGET_RESET_BUTTON,
        WIDGET_COUNT
    };

    struct WidgetBounds
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool valid = false;
    };

    struct PendingTooltip
    {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        std::string text;
        float padding = 8.0f;
    };

    int m_mainScreenFocusIndex = 0;
    bool m_fontDropdownOpen = false;
    int m_fontDropdownHighlightedIndex = 0;
    bool m_fontDropdownSelectRequested = false;
    bool m_fontDropdownCancelRequested = false;

    bool m_styleDropdownOpen = false;
    int m_styleDropdownHighlightedIndex = 0;
    bool m_styleDropdownSelectRequested = false;
    bool m_styleDropdownCancelRequested = false;

    Uint32 m_lastButtonPressTime = 0;
    static constexpr Uint32 BUTTON_DEBOUNCE_MS = 100;

    std::vector<std::string> m_fontNames;
    std::array<WidgetBounds, WIDGET_COUNT> m_widgetBounds{};
    bool m_focusScrollPending = false;
    bool m_scrollToTopPending = false;
    std::vector<PendingTooltip> m_pendingTooltips;
    float m_windowClipY = 0.0f;
    float m_windowClipHeight = 0.0f;
    static constexpr float kScrollPadding = 12.0f;

    void endFrame();
    void setupColorScheme();
    void renderFontMenu();
    void renderNumberPad();
    bool handleNumberPadInput(const SDL_Event& event);
    bool handleKeyboardNavigation(const SDL_Event& event);
    bool handleControllerInput(const SDL_Event& event);
    void adjustFocusedWidget(int direction);
    void activateFocusedWidget();
    int findFontIndex(const std::string& fontName) const;
    void rememberWidgetBounds(MainScreenWidget widget);
    void requestFocusScroll();
    void scrollFocusedWidgetIntoView();
    void scrollSettingsToTop();
    void showInfoTooltip(MainScreenWidget widget, const char* text);
    bool moveFocusInGroup(const MainScreenWidget* group, size_t count, int direction);
    bool handleHorizontalNavigation(int direction);
    bool isInfoWidget(MainScreenWidget widget) const;
    bool stepFocusVertical(int direction);
    void renderPendingTooltips();
};

#endif // GUI_MANAGER_H

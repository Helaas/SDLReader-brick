#include "gui_manager.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <cstring>
#include <cstdio>
#include <cstdlib>

// Define Nuklear implementation
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_FONT_BAKING
#define NK_IMPLEMENTATION
#include "nuklear.h"

#define NK_SDL_RENDERER_IMPLEMENTATION
// Include the SDL renderer implementation
#include "demo/sdl_renderer/nuklear_sdl_renderer.h"

#ifdef TG5040_PLATFORM
static constexpr SDL_GameControllerButton kAcceptButton = SDL_CONTROLLER_BUTTON_B;
static constexpr SDL_GameControllerButton kCancelButton = SDL_CONTROLLER_BUTTON_A;
static constexpr SDL_GameControllerButton kApplySettingsButton = SDL_CONTROLLER_BUTTON_X; // Physical Y
static constexpr SDL_GameControllerButton kResetSettingsButton = SDL_CONTROLLER_BUTTON_Y; // Physical X
#else
static constexpr SDL_GameControllerButton kAcceptButton = SDL_CONTROLLER_BUTTON_A;
static constexpr SDL_GameControllerButton kCancelButton = SDL_CONTROLLER_BUTTON_B;
static constexpr SDL_GameControllerButton kApplySettingsButton = SDL_CONTROLLER_BUTTON_Y;
static constexpr SDL_GameControllerButton kResetSettingsButton = SDL_CONTROLLER_BUTTON_X;
#endif

namespace
{
static nk_bool nk_combo_begin_label_controller(struct nk_context* ctx, const char* selected, struct nk_vec2 size, bool force_open)
{
    if (!ctx || !selected || !ctx->current || !ctx->current->layout)
    {
        return 0;
    }

    struct nk_window* win = ctx->current;
    struct nk_style* style = &ctx->style;

    enum nk_widget_layout_states state;
    struct nk_rect header;
    int is_clicked = nk_false;
    const struct nk_input* in;
    const struct nk_style_item* background;
    struct nk_text text;

    state = nk_widget(&header, ctx);
    if (state == NK_WIDGET_INVALID)
    {
        return 0;
    }

    in = (win->layout->flags & NK_WINDOW_ROM || state == NK_WIDGET_DISABLED || state == NK_WIDGET_ROM) ? nullptr : &ctx->input;
    if (nk_button_behavior(&ctx->last_widget_state, header, in, NK_BUTTON_DEFAULT))
    {
        is_clicked = nk_true;
    }

    if (force_open)
    {
        ctx->last_widget_state |= NK_WIDGET_STATE_ACTIVED;
        ctx->last_widget_state |= NK_WIDGET_STATE_HOVER;
        is_clicked = nk_true;
    }

    if (ctx->last_widget_state & NK_WIDGET_STATE_ACTIVED)
    {
        background = &style->combo.active;
        text.text = style->combo.label_active;
    }
    else if (ctx->last_widget_state & NK_WIDGET_STATE_HOVER)
    {
        background = &style->combo.hover;
        text.text = style->combo.label_hover;
    }
    else
    {
        background = &style->combo.normal;
        text.text = style->combo.label_normal;
    }

    text.text = nk_rgb_factor(text.text, style->combo.color_factor);

    switch (background->type)
    {
    case NK_STYLE_ITEM_IMAGE:
        text.background = nk_rgba(0, 0, 0, 0);
        nk_draw_image(&win->buffer, header, &background->data.image, nk_rgb_factor(nk_white, style->combo.color_factor));
        break;
    case NK_STYLE_ITEM_NINE_SLICE:
        text.background = nk_rgba(0, 0, 0, 0);
        nk_draw_nine_slice(&win->buffer, header, &background->data.slice, nk_rgb_factor(nk_white, style->combo.color_factor));
        break;
    case NK_STYLE_ITEM_COLOR:
        text.background = background->data.color;
        nk_fill_rect(&win->buffer, header, style->combo.rounding, nk_rgb_factor(background->data.color, style->combo.color_factor));
        nk_stroke_rect(&win->buffer, header, style->combo.rounding, style->combo.border, nk_rgb_factor(style->combo.border_color, style->combo.color_factor));
        break;
    }

    {
        struct nk_rect label;
        struct nk_rect button;
        struct nk_rect content;
        int draw_button_symbol;

        enum nk_symbol_type sym;
        if (ctx->last_widget_state & NK_WIDGET_STATE_HOVER)
        {
            sym = style->combo.sym_hover;
        }
        else if (is_clicked)
        {
            sym = style->combo.sym_active;
        }
        else
        {
            sym = style->combo.sym_normal;
        }

        draw_button_symbol = sym != NK_SYMBOL_NONE;

        button.w = header.h - 2 * style->combo.button_padding.y;
        button.x = (header.x + header.w - header.h) - style->combo.button_padding.x;
        button.y = header.y + style->combo.button_padding.y;
        button.h = button.w;

        content.x = button.x + style->combo.button.padding.x;
        content.y = button.y + style->combo.button.padding.y;
        content.w = button.w - 2 * style->combo.button.padding.x;
        content.h = button.h - 2 * style->combo.button.padding.y;

        text.padding = nk_vec2(0, 0);
        label.x = header.x + style->combo.content_padding.x;
        label.y = header.y + style->combo.content_padding.y;
        label.h = header.h - 2 * style->combo.content_padding.y;
        if (draw_button_symbol)
        {
            label.w = button.x - (style->combo.content_padding.x + style->combo.spacing.x) - label.x;
        }
        else
        {
            label.w = header.w - 2 * style->combo.content_padding.x;
        }

        nk_widget_text(&win->buffer, label, selected, nk_strlen(selected), &text, NK_TEXT_LEFT, ctx->style.font);

        if (draw_button_symbol)
        {
            nk_draw_button_symbol(&win->buffer, &button, &content, ctx->last_widget_state, &ctx->style.combo.button, sym, style->font);
        }
    }

    return nk_combo_begin(ctx, win, size, is_clicked, header);
}
} // namespace

// GuiManager implementation
GuiManager::GuiManager()
{
    // Initialize font size input with default value
    int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
    if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
    {
        std::cerr << "Warning: Font size input buffer may be truncated" << std::endl;
    }

    // Initialize reading style index
    auto allStyles = OptionsManager::getAllReadingStyles();
    m_selectedStyleIndex = 0;
    for (size_t i = 0; i < allStyles.size(); i++)
    {
        if (allStyles[i] == m_currentConfig.readingStyle)
        {
            m_selectedStyleIndex = static_cast<int>(i);
            break;
        }
    }
}

GuiManager::~GuiManager()
{
    cleanup();
}

bool GuiManager::initialize(SDL_Window* window, SDL_Renderer* renderer)
{
    if (m_initialized)
    {
        return true;
    }

    m_window = window;
    m_renderer = renderer;

    // Initialize Nuklear with proper SDL renderer backend
    m_ctx = nk_sdl_init(window, renderer);
    if (!m_ctx)
    {
        std::cerr << "Failed to initialize Nuklear SDL context" << std::endl;
        return false;
    }

    // Setup font atlas and load a reliable UI font
    struct nk_font_atlas* atlas = nullptr;
    nk_sdl_font_stash_begin(&atlas);
    struct nk_font* uiFont = nullptr;
    constexpr float kUiFontSize = 24.0f; // 20% larger for better readability

    if (atlas)
    {
        auto fontExists = [](const std::string& path) -> bool
        {
            if (path.empty())
            {
                return false;
            }
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        };

        const auto& availableFonts = m_optionsManager.getAvailableFonts();
        for (const auto& fontInfo : availableFonts)
        {
            if (fontExists(fontInfo.filePath))
            {
                uiFont = nk_font_atlas_add_from_file(atlas, fontInfo.filePath.c_str(), kUiFontSize, nullptr);
                if (uiFont)
                {
                    atlas->default_font = uiFont;
                    std::cout << "Loaded UI font from options list: " << fontInfo.displayName << std::endl;
                    break;
                }
            }
        }

        if (!uiFont)
        {
            static const char* fallbackFonts[] = {
                "fonts/Inter-Regular.ttf",
                "fonts/Roboto-Regular.ttf",
                "fonts/JetBrainsMono-Regular.ttf"};

            for (const char* path : fallbackFonts)
            {
                if (fontExists(path))
                {
                    uiFont = nk_font_atlas_add_from_file(atlas, path, kUiFontSize, nullptr);
                    if (uiFont)
                    {
                        atlas->default_font = uiFont;
                        std::cout << "Loaded fallback UI font: " << path << std::endl;
                        break;
                    }
                }
            }
        }

        if (!uiFont)
        {
            uiFont = nk_font_atlas_add_default(atlas, kUiFontSize, nullptr);
            if (uiFont)
            {
                atlas->default_font = uiFont;
                std::cout << "Loaded Nuklear default font for UI fallback" << std::endl;
            }
            else
            {
                std::cerr << "Failed to load any UI font; interface text may be unavailable" << std::endl;
            }
        }
    }

    nk_sdl_font_stash_end();
    if (uiFont)
    {
        nk_style_set_font(m_ctx, &uiFont->handle);
    }

    // Set up a modern, attractive color scheme
    setupColorScheme();

    // Load available fonts from options manager
    auto fonts = m_optionsManager.getAvailableFonts();
    m_fontNames.clear();
    for (const auto& font : fonts)
    {
        m_fontNames.push_back(font.displayName);
    }

    // Find current font index
    m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);
    m_fontDropdownHighlightedIndex = m_selectedFontIndex;

    m_initialized = true;
    std::cout << "Nuklear GUI initialized successfully" << std::endl;
    return true;
}

void GuiManager::cleanup()
{
    if (!m_initialized)
    {
        return;
    }

    nk_sdl_shutdown();
    m_ctx = nullptr;

    m_initialized = false;
}

bool GuiManager::handleEvent(const SDL_Event& event)
{
    if (!m_initialized || !m_ctx)
    {
        return false;
    }

    // Handle number pad input if visible
    if (m_showNumberPad && handleNumberPadInput(event))
    {
        return true;
    }

    // Handle keyboard navigation for GUI (before nuklear gets the events)
    if (handleKeyboardNavigation(event))
    {
        return true;
    }

    // Handle controller input for general GUI navigation
    if (handleControllerInput(event))
    {
        return true;
    }

    // Only let Nuklear consume events when a menu is actually visible
    // Otherwise, keyboard input bleeds through and app can't process it
    if (!m_showFontMenu && !m_showNumberPad)
    {
        // No menu visible - don't consume the event
        return false;
    }

    // Menu is visible - let Nuklear handle the event
    bool nuklearHandled = nk_sdl_handle_event(const_cast<SDL_Event*>(&event));
    return nuklearHandled;
}

void GuiManager::newFrame()
{
    if (!m_initialized || !m_ctx)
    {
        return;
    }

    // Begin input processing - handleEvent will add input during event processing
    nk_input_begin(m_ctx);
}

void GuiManager::endFrame()
{
    if (!m_initialized || !m_ctx)
    {
        return;
    }

    // End input processing after all events have been handled
    nk_input_end(m_ctx);
}

void GuiManager::render()
{
    if (!m_initialized || !m_ctx || !m_renderer)
    {
        return;
    }

    // End input processing before rendering
    endFrame();

    // Render font menu
    if (m_showFontMenu)
    {
        renderFontMenu();
    }

    // Render number pad if visible
    if (m_showNumberPad)
    {
        renderNumberPad();
    }

    // Render Nuklear with proper anti-aliasing
    nk_sdl_render(NK_ANTI_ALIASING_ON);

    // Handle mouse grab state
    nk_sdl_handle_grab();
}

bool GuiManager::isFontMenuVisible() const
{
    return m_showFontMenu;
}

void GuiManager::toggleFontMenu()
{
    if (m_showFontMenu)
    {
        closeFontMenu();
        return;
    }

    m_showFontMenu = true;
    setCurrentFontConfig(m_currentConfig);
    m_fontDropdownHighlightedIndex = m_selectedFontIndex;
    m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
    m_fontDropdownOpen = false;
    m_fontDropdownSelectRequested = false;
    m_fontDropdownCancelRequested = false;
    m_styleDropdownOpen = false;
    m_styleDropdownSelectRequested = false;
    m_styleDropdownCancelRequested = false;
    m_mainScreenFocusIndex = WIDGET_FONT_DROPDOWN;
    m_scrollToTopPending = true;
    requestFocusScroll();
    std::cout << "Font menu opened" << std::endl;
}

void GuiManager::setCurrentFontConfig(const FontConfig& config)
{
    m_currentConfig = config;
    m_tempConfig = config;

    // Update UI elements
    int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", config.fontSize);
    if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
    {
        std::cerr << "Warning: Font size input buffer may be truncated" << std::endl;
    }

    result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", config.zoomStep);
    if (result < 0 || result >= (int) sizeof(m_zoomStepInput))
    {
        std::cerr << "Warning: Zoom step input buffer may be truncated" << std::endl;
    }

    m_selectedFontIndex = findFontIndex(config.fontName);
    m_fontDropdownHighlightedIndex = m_selectedFontIndex;

    // Update reading style index
    auto allStyles = OptionsManager::getAllReadingStyles();
    m_selectedStyleIndex = 0;
    for (size_t i = 0; i < allStyles.size(); i++)
    {
        if (allStyles[i] == config.readingStyle)
        {
            m_selectedStyleIndex = static_cast<int>(i);
            break;
        }
    }
    m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
}

bool GuiManager::wantsCaptureMouse() const
{
    // Simple implementation - capture mouse when any window is open
    return m_showFontMenu || m_showNumberPad;
}

bool GuiManager::wantsCaptureKeyboard() const
{
    // Simple implementation - capture keyboard when any window is open
    return m_showFontMenu || m_showNumberPad;
}

void GuiManager::renderFontMenu()
{
    if (!m_ctx)
        return;

    m_pendingTooltips.clear();

    // Nuklear state debug disabled (was too spammy)
    /*
    static int debugCounter = 0;
    if (debugCounter++ % 60 == 0)
    {
        std::cout << "[DEBUG] Nuklear state - mouse: ("
                  << m_ctx->input.mouse.pos.x << ", " << m_ctx->input.mouse.pos.y << ")"
                  << ", mouse down: " << (m_ctx->input.mouse.buttons[0].down ? "YES" : "NO")
                  << ", keys pressed: " << m_ctx->input.keyboard.text_len << std::endl;
    }
    */

    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

    // Center the window and make it appropriately sized
    float centerX = windowWidth * 0.5f;
    float centerY = windowHeight * 0.5f;
    float windowW = 680.0f;
    float windowH = 880.0f;

    // Create settings window with scrollbar support
    if (nk_begin(m_ctx, "Settings", nk_rect(centerX - windowW / 2, centerY - windowH / 2, windowW, windowH),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE))
    {
        // Set initial focus to enable keyboard navigation
        static bool initialFocusSet = false;
        if (!initialFocusSet)
        {
            // This helps with initial focus for keyboard/controller navigation
            initialFocusSet = true;
        }

        for (auto& bounds : m_widgetBounds)
        {
            bounds.valid = false;
        }

        // Store original styles for highlighting focused widgets
        struct nk_style_button originalButtonStyle = m_ctx->style.button;
        struct nk_style_combo originalComboStyle = m_ctx->style.combo;
        struct nk_style_edit originalEditStyle = m_ctx->style.edit;
        struct nk_style_slider originalSliderStyle = m_ctx->style.slider;
        struct nk_style_selectable originalSelectableStyle = m_ctx->style.selectable;

        // === FONT SETTINGS SECTION ===
        nk_layout_row_dynamic(m_ctx, 25, 1);
        nk_label(m_ctx, "Font Settings", NK_TEXT_LEFT);

        // Separator line
        nk_layout_row_dynamic(m_ctx, 1, 1);
        struct nk_rect bounds = nk_widget_bounds(m_ctx);
        struct nk_command_buffer* canvas = nk_window_get_canvas(m_ctx);
        nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f, nk_rgb(100, 100, 100));

        const auto& fonts = m_optionsManager.getAvailableFonts();
        if (fonts.empty())
        {
            nk_layout_row_dynamic(m_ctx, 60, 1);
            nk_label_colored(m_ctx, "No fonts found in /fonts directory", NK_TEXT_CENTERED, nk_rgb(255, 255, 0));
            nk_label(m_ctx, "Please add .ttf or .otf files to the fonts folder", NK_TEXT_CENTERED);
        }
        else
        {
            // Font Family Dropdown
            nk_layout_row_dynamic(m_ctx, 20, 1);
            nk_label(m_ctx, "Font Family:", NK_TEXT_LEFT);

            // Ensure selected index is valid
            if (m_selectedFontIndex < 0 || m_selectedFontIndex >= (int) fonts.size())
            {
                m_selectedFontIndex = 0;
            }

            // Create dropdown
            nk_layout_row_dynamic(m_ctx, 25, 1);

            // Highlight font dropdown if focused
            if (m_mainScreenFocusIndex == WIDGET_FONT_DROPDOWN)
            {
                m_ctx->style.combo.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
                m_ctx->style.combo.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
                m_ctx->style.combo.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
                m_ctx->style.combo.normal = nk_style_item_color(nk_rgb(0, 122, 255));
                m_ctx->style.combo.hover = nk_style_item_color(nk_rgb(30, 142, 255));
                m_ctx->style.combo.active = nk_style_item_color(nk_rgb(0, 102, 235));
            }
            const char* currentFont = fonts[m_selectedFontIndex].displayName.c_str();

            if (m_fontDropdownHighlightedIndex < 0 || m_fontDropdownHighlightedIndex >= (int) fonts.size())
            {
                m_fontDropdownHighlightedIndex = (m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size()) ? m_selectedFontIndex : 0;
            }

            bool forceOpen = m_fontDropdownOpen || m_fontDropdownSelectRequested || m_fontDropdownCancelRequested;
            bool comboOpened = nk_combo_begin_label_controller(m_ctx, currentFont, nk_vec2(nk_widget_width(m_ctx), 200), forceOpen);
            rememberWidgetBounds(WIDGET_FONT_DROPDOWN);
            if (comboOpened)
            {
                nk_layout_row_dynamic(m_ctx, 20, 1);
                for (size_t i = 0; i < fonts.size(); ++i)
                {
                    m_ctx->style.selectable = originalSelectableStyle;

                    bool isHighlighted = m_fontDropdownOpen && (m_fontDropdownHighlightedIndex == static_cast<int>(i));
                    if (isHighlighted)
                    {
                        m_ctx->style.selectable.normal = nk_style_item_color(nk_rgb(0, 122, 255));
                        m_ctx->style.selectable.hover = nk_style_item_color(nk_rgb(30, 142, 255));
                        m_ctx->style.selectable.pressed = nk_style_item_color(nk_rgb(0, 102, 235));
                        m_ctx->style.selectable.text_normal = nk_rgb(255, 255, 255);
                        m_ctx->style.selectable.text_hover = nk_rgb(255, 255, 255);
                        m_ctx->style.selectable.text_pressed = nk_rgb(255, 255, 255);
                    }

                    nk_bool isSelected = (m_selectedFontIndex == static_cast<int>(i));
                    nk_bool selectionChanged = nk_selectable_label(m_ctx, fonts[i].displayName.c_str(), NK_TEXT_LEFT, &isSelected);
                    if (selectionChanged && isSelected)
                    {
                        std::cout << "[DEBUG] Font selected: " << fonts[i].displayName << std::endl;
                        m_selectedFontIndex = static_cast<int>(i);
                        m_tempConfig.fontPath = fonts[i].filePath;
                        m_tempConfig.fontName = fonts[i].displayName;
                        m_fontDropdownHighlightedIndex = m_selectedFontIndex;
                        nk_combo_close(m_ctx);
                        m_fontDropdownOpen = false;
                        m_fontDropdownSelectRequested = false;
                        m_fontDropdownCancelRequested = false;
                    }
                }

                if (m_fontDropdownSelectRequested)
                {
                    if (m_fontDropdownHighlightedIndex >= 0 && m_fontDropdownHighlightedIndex < (int) fonts.size())
                    {
                        int chosenIndex = m_fontDropdownHighlightedIndex;
                        std::cout << "[DEBUG] Font selected (controller): " << fonts[chosenIndex].displayName << std::endl;
                        m_selectedFontIndex = chosenIndex;
                        m_tempConfig.fontPath = fonts[chosenIndex].filePath;
                        m_tempConfig.fontName = fonts[chosenIndex].displayName;
                    }
                    nk_combo_close(m_ctx);
                    m_fontDropdownOpen = false;
                    m_fontDropdownSelectRequested = false;
                    m_fontDropdownHighlightedIndex = m_selectedFontIndex;
                }

                if (m_fontDropdownCancelRequested)
                {
                    nk_combo_close(m_ctx);
                    m_fontDropdownOpen = false;
                    m_fontDropdownCancelRequested = false;
                    m_fontDropdownHighlightedIndex = m_selectedFontIndex;
                }

                nk_combo_end(m_ctx);
                m_ctx->style.selectable = originalSelectableStyle;
            }
            else
            {
                if (m_fontDropdownOpen || m_fontDropdownSelectRequested || m_fontDropdownCancelRequested)
                {
                    m_fontDropdownOpen = false;
                    m_fontDropdownSelectRequested = false;
                    m_fontDropdownCancelRequested = false;
                    m_fontDropdownHighlightedIndex = m_selectedFontIndex;
                }
                m_ctx->style.selectable = originalSelectableStyle;
            }

            // Restore combo style
            m_ctx->style.combo = originalComboStyle;
            nk_layout_row_dynamic(m_ctx, 10, 1); // Spacing

            // Font Size Section
            nk_layout_row_dynamic(m_ctx, 20, 1);
            nk_label(m_ctx, "Font Size (pt):", NK_TEXT_LEFT);

            // Font size input field
            nk_layout_row_dynamic(m_ctx, 25, 1);

            // Highlight font size input if focused
            if (m_mainScreenFocusIndex == WIDGET_FONT_SIZE_INPUT)
            {
                m_ctx->style.edit.normal = nk_style_item_color(nk_rgb(0, 122, 255));
                m_ctx->style.edit.hover = nk_style_item_color(nk_rgb(30, 142, 255));
                m_ctx->style.edit.active = nk_style_item_color(nk_rgb(0, 102, 235));
            }

            int editFlags = nk_edit_string_zero_terminated(m_ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_SELECTABLE,
                                                           m_fontSizeInput, sizeof(m_fontSizeInput), nk_filter_decimal);
            rememberWidgetBounds(WIDGET_FONT_SIZE_INPUT);

            // Restore edit style
            m_ctx->style.edit = originalEditStyle;
            if (editFlags & NK_EDIT_ACTIVATED)
            {
                std::cout << "[DEBUG] Font size edit field ACTIVATED" << std::endl;
            }
            if (editFlags & NK_EDIT_DEACTIVATED)
            {
                std::cout << "[DEBUG] Font size edit field DEACTIVATED" << std::endl;
            }
            if (editFlags & NK_EDIT_COMMITED)
            {
                std::cout << "[DEBUG] Font size edit field COMMITTED: " << m_fontSizeInput << std::endl;
                int newSize = std::atoi(m_fontSizeInput);
                if (newSize >= 8 && newSize <= 72)
                {
                    m_tempConfig.fontSize = newSize;
                }
            }
            // Also update if the value changes during editing
            if (editFlags & NK_EDIT_ACTIVATED || editFlags & NK_EDIT_DEACTIVATED)
            {
                int newSize = std::atoi(m_fontSizeInput);
                if (newSize >= 8 && newSize <= 72)
                {
                    m_tempConfig.fontSize = newSize;
                }
            }

            // Font size slider
            nk_layout_row_dynamic(m_ctx, 20, 1);

            // Highlight font size slider if focused
            if (m_mainScreenFocusIndex == WIDGET_FONT_SIZE_SLIDER)
            {
                m_ctx->style.slider.normal = nk_style_item_color(nk_rgb(0, 122, 255));
                m_ctx->style.slider.hover = nk_style_item_color(nk_rgb(30, 142, 255));
                m_ctx->style.slider.active = nk_style_item_color(nk_rgb(0, 102, 235));
                m_ctx->style.slider.cursor_normal = nk_style_item_color(nk_rgb(0, 122, 255));
                m_ctx->style.slider.cursor_hover = nk_style_item_color(nk_rgb(30, 142, 255));
                m_ctx->style.slider.cursor_active = nk_style_item_color(nk_rgb(0, 102, 235));
            }
            float fontSize = static_cast<float>(m_tempConfig.fontSize);
            if (nk_slider_float(m_ctx, 8.0f, &fontSize, 72.0f, 1.0f))
            {
                m_tempConfig.fontSize = static_cast<int>(fontSize);
                snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_tempConfig.fontSize);
            }
            rememberWidgetBounds(WIDGET_FONT_SIZE_SLIDER);

            // Restore slider style
            m_ctx->style.slider = originalSliderStyle;

            nk_layout_row_dynamic(m_ctx, 15, 1); // Spacing
        }

        // === READING STYLE SECTION ===
        nk_layout_row_dynamic(m_ctx, 25, 1);
        nk_label(m_ctx, "Reading Style", NK_TEXT_LEFT);

        // Separator line
        nk_layout_row_dynamic(m_ctx, 1, 1);
        bounds = nk_widget_bounds(m_ctx);
        canvas = nk_window_get_canvas(m_ctx);
        nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f, nk_rgb(100, 100, 100));

        // Informational notice
        nk_layout_row_dynamic(m_ctx, 35, 1);
        nk_label_colored_wrap(m_ctx, "Choose a color theme for comfortable reading. Applies to EPUB/MOBI only.", nk_rgb(102, 178, 255));

        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label(m_ctx, "Color Theme:", NK_TEXT_LEFT);

        // Get all available reading styles
        auto allStyles = OptionsManager::getAllReadingStyles();

        // Ensure selected index is valid
        if (m_selectedStyleIndex < 0 || m_selectedStyleIndex >= (int) allStyles.size())
        {
            m_selectedStyleIndex = 0;
        }

        // Create dropdown
        nk_layout_row_dynamic(m_ctx, 25, 1);

        // Highlight reading style dropdown if focused
        if (m_mainScreenFocusIndex == WIDGET_READING_STYLE_DROPDOWN)
        {
            m_ctx->style.combo.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.combo.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.combo.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
            m_ctx->style.combo.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.combo.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.combo.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        const char* currentStyle = OptionsManager::getReadingStyleName(allStyles[m_selectedStyleIndex]);

        if (m_styleDropdownHighlightedIndex < 0 || m_styleDropdownHighlightedIndex >= (int) allStyles.size())
        {
            m_styleDropdownHighlightedIndex = (m_selectedStyleIndex >= 0 && m_selectedStyleIndex < (int) allStyles.size()) ? m_selectedStyleIndex : 0;
        }

        bool forceStyleOpen = m_styleDropdownOpen || m_styleDropdownSelectRequested || m_styleDropdownCancelRequested;
        bool readingComboOpened = nk_combo_begin_label_controller(m_ctx, currentStyle, nk_vec2(nk_widget_width(m_ctx), 200), forceStyleOpen);
        rememberWidgetBounds(WIDGET_READING_STYLE_DROPDOWN);
        if (readingComboOpened)
        {
            nk_layout_row_dynamic(m_ctx, 20, 1);
            for (size_t i = 0; i < allStyles.size(); ++i)
            {
                m_ctx->style.selectable = originalSelectableStyle;

                bool isHighlighted = m_styleDropdownOpen && (m_styleDropdownHighlightedIndex == static_cast<int>(i));
                if (isHighlighted)
                {
                    m_ctx->style.selectable.normal = nk_style_item_color(nk_rgb(0, 122, 255));
                    m_ctx->style.selectable.hover = nk_style_item_color(nk_rgb(30, 142, 255));
                    m_ctx->style.selectable.pressed = nk_style_item_color(nk_rgb(0, 102, 235));
                    m_ctx->style.selectable.text_normal = nk_rgb(255, 255, 255);
                    m_ctx->style.selectable.text_hover = nk_rgb(255, 255, 255);
                    m_ctx->style.selectable.text_pressed = nk_rgb(255, 255, 255);
                }

                nk_bool isSelected = (m_selectedStyleIndex == static_cast<int>(i));
                nk_bool selectionChanged = nk_selectable_label(m_ctx, OptionsManager::getReadingStyleName(allStyles[i]), NK_TEXT_LEFT, &isSelected);
                if (selectionChanged && isSelected)
                {
                    std::cout << "[DEBUG] Reading style selected: " << OptionsManager::getReadingStyleName(allStyles[i]) << std::endl;
                    m_selectedStyleIndex = static_cast<int>(i);
                    m_tempConfig.readingStyle = allStyles[i];
                    m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
                    nk_combo_close(m_ctx);
                    m_styleDropdownOpen = false;
                    m_styleDropdownSelectRequested = false;
                    m_styleDropdownCancelRequested = false;
                }
            }

            if (m_styleDropdownSelectRequested)
            {
                if (m_styleDropdownHighlightedIndex >= 0 && m_styleDropdownHighlightedIndex < (int) allStyles.size())
                {
                    int chosenIndex = m_styleDropdownHighlightedIndex;
                    std::cout << "[DEBUG] Reading style selected (controller): " << OptionsManager::getReadingStyleName(allStyles[chosenIndex]) << std::endl;
                    m_selectedStyleIndex = chosenIndex;
                    m_tempConfig.readingStyle = allStyles[chosenIndex];
                }
                nk_combo_close(m_ctx);
                m_styleDropdownOpen = false;
                m_styleDropdownSelectRequested = false;
                m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
            }

            if (m_styleDropdownCancelRequested)
            {
                nk_combo_close(m_ctx);
                m_styleDropdownOpen = false;
                m_styleDropdownCancelRequested = false;
                m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
            }

            nk_combo_end(m_ctx);
            m_ctx->style.selectable = originalSelectableStyle;
        }
        else
        {
            if (m_styleDropdownOpen || m_styleDropdownSelectRequested || m_styleDropdownCancelRequested)
            {
                m_styleDropdownOpen = false;
                m_styleDropdownSelectRequested = false;
                m_styleDropdownCancelRequested = false;
                m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
            }
            m_ctx->style.selectable = originalSelectableStyle;
        }

        // Restore combo style
        m_ctx->style.combo = originalComboStyle;
        nk_layout_row_dynamic(m_ctx, 15, 1); // Spacing

        // === ZOOM SETTINGS SECTION ===
        nk_layout_row_dynamic(m_ctx, 25, 1);
        nk_label(m_ctx, "Zoom Settings", NK_TEXT_LEFT);

        // Separator line
        nk_layout_row_dynamic(m_ctx, 1, 1);
        bounds = nk_widget_bounds(m_ctx);
        canvas = nk_window_get_canvas(m_ctx);
        nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f, nk_rgb(100, 100, 100));

        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label(m_ctx, "Zoom Step (%) - Amount to zoom in/out with +/- keys:", NK_TEXT_LEFT);

        // Zoom step input
        nk_layout_row_dynamic(m_ctx, 25, 1);

        // Highlight zoom step input if focused
        if (m_mainScreenFocusIndex == WIDGET_ZOOM_STEP_INPUT)
        {
            m_ctx->style.edit.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.edit.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.edit.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        int zoomEditFlags = nk_edit_string_zero_terminated(m_ctx, NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_SELECTABLE,
                                                           m_zoomStepInput, sizeof(m_zoomStepInput), nk_filter_decimal);
        rememberWidgetBounds(WIDGET_ZOOM_STEP_INPUT);

        // Restore edit style
        m_ctx->style.edit = originalEditStyle;
        if (zoomEditFlags & NK_EDIT_COMMITED)
        {
            int newStep = std::atoi(m_zoomStepInput);
            if (newStep >= 1 && newStep <= 50)
            {
                m_tempConfig.zoomStep = newStep;
            }
        }
        // Also update if the value changes during editing
        if (zoomEditFlags & NK_EDIT_ACTIVATED || zoomEditFlags & NK_EDIT_DEACTIVATED)
        {
            int newStep = std::atoi(m_zoomStepInput);
            if (newStep >= 1 && newStep <= 50)
            {
                m_tempConfig.zoomStep = newStep;
            }
        }

        // Zoom step slider
        nk_layout_row_dynamic(m_ctx, 20, 1);

        // Highlight zoom step slider if focused
        if (m_mainScreenFocusIndex == WIDGET_ZOOM_STEP_SLIDER)
        {
            m_ctx->style.slider.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.slider.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.slider.active = nk_style_item_color(nk_rgb(0, 102, 235));
            m_ctx->style.slider.cursor_normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.slider.cursor_hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.slider.cursor_active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        float zoomStep = static_cast<float>(m_tempConfig.zoomStep);
        if (nk_slider_float(m_ctx, 1.0f, &zoomStep, 50.0f, 1.0f))
        {
            m_tempConfig.zoomStep = static_cast<int>(zoomStep);
            snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_tempConfig.zoomStep);
        }
        rememberWidgetBounds(WIDGET_ZOOM_STEP_SLIDER);

        // Restore zoom slider style
        m_ctx->style.slider = originalSliderStyle;

        nk_layout_row_dynamic(m_ctx, 15, 1); // Spacing

        // === PAGE NAVIGATION SECTION ===
        nk_layout_row_dynamic(m_ctx, 25, 1);
        nk_label(m_ctx, "Page Navigation", NK_TEXT_LEFT);

        // Separator line
        nk_layout_row_dynamic(m_ctx, 1, 1);
        bounds = nk_widget_bounds(m_ctx);
        canvas = nk_window_get_canvas(m_ctx);
        nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f, nk_rgb(100, 100, 100));

        // Current page display
        char pageInfo[64];
        snprintf(pageInfo, sizeof(pageInfo), "Current Page: %d / %d", m_currentPage + 1, m_pageCount);
        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label(m_ctx, pageInfo, NK_TEXT_LEFT);

        nk_layout_row_dynamic(m_ctx, 10, 1); // Spacing

        // Edge Progress Bar checkbox + info button
        nk_layout_row_template_begin(m_ctx, 25);
        nk_layout_row_template_push_dynamic(m_ctx);
        nk_layout_row_template_push_static(m_ctx, 32);
        nk_layout_row_template_end(m_ctx);

        // Highlight checkbox if focused
        struct nk_style_toggle originalToggleStyle = m_ctx->style.checkbox;
        const struct nk_color focusBorderColor = nk_rgb(0, 190, 255);
        const struct nk_color focusCursorColor = nk_rgb(0, 180, 255);
        const struct nk_color focusBackground = nk_rgba(70, 75, 85, 255);
        if (m_mainScreenFocusIndex == WIDGET_EDGE_PROGRESS_CHECKBOX)
        {
            m_ctx->style.checkbox.normal = nk_style_item_color(focusBackground);
            m_ctx->style.checkbox.hover = nk_style_item_color(focusBackground);
            m_ctx->style.checkbox.cursor_normal = nk_style_item_color(focusCursorColor);
            m_ctx->style.checkbox.cursor_hover = nk_style_item_color(focusCursorColor);
            m_ctx->style.checkbox.border = 2.0f;
            m_ctx->style.checkbox.border_color = focusBorderColor;
        }

        nk_bool disableEdgeBar = m_tempConfig.disableEdgeProgressBar ? nk_true : nk_false;
        if (nk_checkbox_label(m_ctx, "Disable Edge Progress Bar", &disableEdgeBar))
        {
            m_tempConfig.disableEdgeProgressBar = (disableEdgeBar == nk_true);
        }
        rememberWidgetBounds(WIDGET_EDGE_PROGRESS_CHECKBOX);

        // Restore checkbox style
        m_ctx->style.checkbox = originalToggleStyle;

        // Info button for edge progress description
        struct nk_style_button infoButtonStyle = m_ctx->style.button;
        if (m_mainScreenFocusIndex == WIDGET_EDGE_PROGRESS_INFO_BUTTON)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(70, 70, 75));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(90, 90, 95));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(60, 60, 70));
        }
        nk_button_label(m_ctx, "(?)");
        bool edgeInfoHovered = nk_widget_is_hovered(m_ctx);
        rememberWidgetBounds(WIDGET_EDGE_PROGRESS_INFO_BUTTON);
        if (m_mainScreenFocusIndex == WIDGET_EDGE_PROGRESS_INFO_BUTTON || edgeInfoHovered)
        {
            showInfoTooltip(WIDGET_EDGE_PROGRESS_INFO_BUTTON, "When enabled, panning at page edges changes pages instantly.\nWhen disabled, hold at the edge for 300ms.");
        }
        m_ctx->style.button = infoButtonStyle;

        nk_layout_row_dynamic(m_ctx, 10, 1); // Spacing

        // Document Minimap checkbox + info button
        nk_layout_row_template_begin(m_ctx, 25);
        nk_layout_row_template_push_dynamic(m_ctx);
        nk_layout_row_template_push_static(m_ctx, 32);
        nk_layout_row_template_end(m_ctx);

        // Highlight checkbox if focused
        if (m_mainScreenFocusIndex == WIDGET_MINIMAP_CHECKBOX)
        {
            m_ctx->style.checkbox.normal = nk_style_item_color(focusBackground);
            m_ctx->style.checkbox.hover = nk_style_item_color(focusBackground);
            m_ctx->style.checkbox.cursor_normal = nk_style_item_color(focusCursorColor);
            m_ctx->style.checkbox.cursor_hover = nk_style_item_color(focusCursorColor);
            m_ctx->style.checkbox.border = 2.0f;
            m_ctx->style.checkbox.border_color = focusBorderColor;
        }

        nk_bool showMinimap = m_tempConfig.showDocumentMinimap ? nk_true : nk_false;
        if (nk_checkbox_label(m_ctx, "Show Document Minimap", &showMinimap))
        {
            m_tempConfig.showDocumentMinimap = (showMinimap == nk_true);
        }
        rememberWidgetBounds(WIDGET_MINIMAP_CHECKBOX);

        // Restore checkbox style
        m_ctx->style.checkbox = originalToggleStyle;

        // Info button for minimap description
        infoButtonStyle = m_ctx->style.button;
        if (m_mainScreenFocusIndex == WIDGET_MINIMAP_INFO_BUTTON)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(70, 70, 75));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(90, 90, 95));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(60, 60, 70));
        }
        nk_button_label(m_ctx, "(?)");
        bool minimapInfoHovered = nk_widget_is_hovered(m_ctx);
        rememberWidgetBounds(WIDGET_MINIMAP_INFO_BUTTON);
        if (m_mainScreenFocusIndex == WIDGET_MINIMAP_INFO_BUTTON || minimapInfoHovered)
        {
            showInfoTooltip(WIDGET_MINIMAP_INFO_BUTTON, "Show a miniature page overlay when zoomed in to visualize which part is visible.");
        }
        m_ctx->style.button = infoButtonStyle;

        nk_layout_row_dynamic(m_ctx, 10, 1); // Spacing

        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label(m_ctx, "Jump to Page:", NK_TEXT_LEFT);

        // Page jump input and buttons
        nk_layout_row_template_begin(m_ctx, 30);
        nk_layout_row_template_push_static(m_ctx, 160);
        nk_layout_row_template_push_static(m_ctx, 90);
        nk_layout_row_template_push_static(m_ctx, 160);
        nk_layout_row_template_end(m_ctx);

        // Highlight page jump input if focused
        if (m_mainScreenFocusIndex == WIDGET_PAGE_JUMP_INPUT)
        {
            m_ctx->style.edit.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.edit.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.edit.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        nk_edit_string_zero_terminated(m_ctx, NK_EDIT_FIELD | NK_EDIT_SELECTABLE, m_pageJumpInput, sizeof(m_pageJumpInput), nk_filter_decimal);
        rememberWidgetBounds(WIDGET_PAGE_JUMP_INPUT);

        // Restore edit style
        m_ctx->style.edit = originalEditStyle;

        // Validate page input
        bool validPageInput = false;
        int targetPage = std::atoi(m_pageJumpInput);
        if (targetPage >= 1 && targetPage <= m_pageCount)
        {
            validPageInput = true;
        }

        // Go button
        if (!validPageInput)
        {
            nk_widget_disable_begin(m_ctx);
        }

        // Highlight Go button if focused
        if (m_mainScreenFocusIndex == WIDGET_GO_BUTTON)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        if (nk_button_label(m_ctx, "Go"))
        {
            if (validPageInput && m_pageJumpCallback)
            {
                m_pageJumpCallback(targetPage - 1); // Convert to 0-based
            }
        }
        rememberWidgetBounds(WIDGET_GO_BUTTON);
        if (!validPageInput)
        {
            nk_widget_disable_end(m_ctx);
        }

        // Restore Go button style
        m_ctx->style.button = originalButtonStyle;

        // Highlight Number Pad button if focused
        if (m_mainScreenFocusIndex == WIDGET_NUMPAD_BUTTON)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        // Number pad button
        if (nk_button_label(m_ctx, "Number Pad"))
        {
            showNumberPad();
        }
        rememberWidgetBounds(WIDGET_NUMPAD_BUTTON);

        // Restore Number Pad button style
        m_ctx->style.button = originalButtonStyle;

        // Show validation message if needed
        if (!validPageInput && targetPage != 0)
        {
            nk_layout_row_dynamic(m_ctx, 20, 1);
            nk_label_colored(m_ctx, "Invalid page number", NK_TEXT_LEFT, nk_rgb(255, 100, 100));
        }

        nk_layout_row_dynamic(m_ctx, 15, 1); // Spacing

        // === CONTROLLER HINTS ===
        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label_colored(m_ctx, "Controller: D-Pad=Navigate, A=Select, B=Close/Unfocus, Y=Apply, X=Reset", NK_TEXT_CENTERED, nk_rgb(150, 150, 150));
        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label_colored(m_ctx, "L/R Shoulder=Tab Between Fields, Start=NumberPad", NK_TEXT_CENTERED, nk_rgb(150, 150, 150));

        // === BUTTONS SECTION ===
        // Separator line
        nk_layout_row_dynamic(m_ctx, 1, 1);
        bounds = nk_widget_bounds(m_ctx);
        canvas = nk_window_get_canvas(m_ctx);
        nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f, nk_rgb(100, 100, 100));

        nk_layout_row_dynamic(m_ctx, 10, 1); // Spacing

        bool hasValidFont = !fonts.empty() && m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size();

        nk_layout_row_template_begin(m_ctx, 35);
        nk_layout_row_template_push_static(m_ctx, 120); // Apply button width
        nk_layout_row_template_push_static(m_ctx, 20);
        nk_layout_row_template_push_static(m_ctx, 120); // Close button width
        nk_layout_row_template_push_static(m_ctx, 20);
        nk_layout_row_template_push_static(m_ctx, 190); // Reset to default button width
        nk_layout_row_template_end(m_ctx);

        // Apply button
        if (!hasValidFont)
        {
            nk_widget_disable_begin(m_ctx);
        }

        // Highlight Apply button if focused
        if (m_mainScreenFocusIndex == WIDGET_APPLY_BUTTON)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        if (nk_button_label(m_ctx, "Apply"))
        {
            if (hasValidFont && m_fontApplyCallback)
            {
                // Update current config
                m_currentConfig = m_tempConfig;

                // Save config to file
                m_optionsManager.saveConfig(m_currentConfig);

                // Call callback to apply changes
                m_fontApplyCallback(m_currentConfig);
            }
        }
        rememberWidgetBounds(WIDGET_APPLY_BUTTON);

        // Restore Apply button style
        m_ctx->style.button = originalButtonStyle;
        if (!hasValidFont)
        {
            nk_widget_disable_end(m_ctx);
        }

        nk_spacing(m_ctx, 1); // Empty column

        // Close button
        if (m_mainScreenFocusIndex == WIDGET_CLOSE_BUTTON)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }
        if (nk_button_label(m_ctx, "Close"))
        {
            closeFontMenu();
        }
        rememberWidgetBounds(WIDGET_CLOSE_BUTTON);
        m_ctx->style.button = originalButtonStyle;

        nk_spacing(m_ctx, 1); // Empty column

        // Reset to Default button
        if (m_mainScreenFocusIndex == WIDGET_RESET_BUTTON)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }
        if (nk_button_label(m_ctx, "Reset to Default"))
        {
            // Reset to default config
            m_tempConfig = FontConfig();
            m_selectedFontIndex = 0;
            m_selectedStyleIndex = 0;
            m_fontDropdownHighlightedIndex = m_selectedFontIndex;
            m_styleDropdownHighlightedIndex = m_selectedStyleIndex;

            // Reset input fields to defaults
            strcpy(m_fontSizeInput, "12");
            strcpy(m_zoomStepInput, "10");
            strcpy(m_pageJumpInput, "1");
        }
        rememberWidgetBounds(WIDGET_RESET_BUTTON);
        m_ctx->style.button = originalButtonStyle;

        if (m_focusScrollPending)
        {
            scrollFocusedWidgetIntoView();
        }
        if (m_scrollToTopPending)
        {
            nk_uint scrollX = 0;
            nk_uint scrollY = 0;
            nk_window_get_scroll(m_ctx, &scrollX, &scrollY);
            if (scrollY != 0)
            {
                nk_window_set_scroll(m_ctx, scrollX, 0);
            }
            m_scrollToTopPending = false;
        }
        renderPendingTooltips();
    }
    nk_end(m_ctx);
}

void GuiManager::renderNumberPad()
{
    if (!m_ctx)
        return;

    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

    // Center the number pad window
    float centerX = windowWidth * 0.5f;
    float centerY = windowHeight * 0.5f;
    float windowW = 390.0f;
    float windowH = 520.0f;

    if (nk_begin(m_ctx, "Number Pad", nk_rect(centerX - windowW / 2, centerY - windowH / 2, windowW, windowH),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
    {
        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label(m_ctx, "Enter Page Number:", NK_TEXT_CENTERED);

        // Separator line
        nk_layout_row_dynamic(m_ctx, 1, 1);
        struct nk_rect bounds = nk_widget_bounds(m_ctx);
        struct nk_command_buffer* canvas = nk_window_get_canvas(m_ctx);
        nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f, nk_rgb(100, 100, 100));

        // Display current input
        char displayText[32];
        snprintf(displayText, sizeof(displayText), "Page: %s", strlen(m_pageJumpInput) > 0 ? m_pageJumpInput : "_");
        nk_layout_row_dynamic(m_ctx, 30, 1);
        nk_label(m_ctx, displayText, NK_TEXT_CENTERED);

        // Separator line
        nk_layout_row_dynamic(m_ctx, 1, 1);
        bounds = nk_widget_bounds(m_ctx);
        canvas = nk_window_get_canvas(m_ctx);
        nk_stroke_line(canvas, bounds.x, bounds.y, bounds.x + bounds.w, bounds.y, 1.0f, nk_rgb(100, 100, 100));

        // Number pad layout (3x4 grid)
        // Row 0: 7, 8, 9
        // Row 1: 4, 5, 6
        // Row 2: 1, 2, 3
        // Row 3: Clear, 0, Back

        const char* numberGrid[4][3] = {
            {"7", "8", "9"},
            {"4", "5", "6"},
            {"1", "2", "3"},
            {"Clear", "0", "Back"}};

        for (int row = 0; row < 4; row++)
        {
            nk_layout_row_dynamic(m_ctx, 50, 3);
            for (int col = 0; col < 3; col++)
            {
                const char* buttonText = numberGrid[row][col];

                // Highlight selected button for controller navigation
                bool isSelected = (m_numberPadSelectedRow == row && m_numberPadSelectedCol == col);

                struct nk_style_button oldStyle = m_ctx->style.button;
                if (isSelected)
                {
                    m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
                    m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
                    m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
                }

                if (nk_button_label(m_ctx, buttonText))
                {
                    if (strcmp(buttonText, "Clear") == 0)
                    {
                        // Clear all input
                        strcpy(m_pageJumpInput, "");
                    }
                    else if (strcmp(buttonText, "Back") == 0)
                    {
                        // Backspace - remove last character
                        int len = strlen(m_pageJumpInput);
                        if (len > 0)
                        {
                            m_pageJumpInput[len - 1] = '\0';
                        }
                    }
                    else
                    {
                        // Add digit if there's space
                        int len = strlen(m_pageJumpInput);
                        if (len < (int) sizeof(m_pageJumpInput) - 1)
                        {
                            m_pageJumpInput[len] = buttonText[0];
                            m_pageJumpInput[len + 1] = '\0';
                        }
                    }
                }

                // Restore original button style
                if (isSelected)
                {
                    m_ctx->style.button = oldStyle;
                }
            }
        }

        nk_layout_row_dynamic(m_ctx, 10, 1); // Spacing

        // Action buttons
        nk_layout_row_dynamic(m_ctx, 40, 2);

        // Go button
        int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
        bool validPage = (targetPage >= 1 && targetPage <= m_pageCount);

        // Highlight Go button if selected
        bool goSelected = (m_numberPadSelectedRow == 4 && m_numberPadSelectedCol == 0);
        struct nk_style_button oldStyleGo = m_ctx->style.button;
        if (goSelected)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        if (!validPage)
        {
            nk_widget_disable_begin(m_ctx);
        }
        if (nk_button_label(m_ctx, "Go"))
        {
            if (validPage && m_pageJumpCallback)
            {
                m_pageJumpCallback(targetPage - 1); // Convert to 0-based
                hideNumberPad();
            }
        }
        if (!validPage)
        {
            nk_widget_disable_end(m_ctx);
        }

        // Restore button style
        if (goSelected)
        {
            m_ctx->style.button = oldStyleGo;
        }

        // Highlight Cancel button if selected
        bool cancelSelected = (m_numberPadSelectedRow == 4 && m_numberPadSelectedCol == 1);
        if (cancelSelected)
        {
            m_ctx->style.button.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.button.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.button.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        // Cancel button
        if (nk_button_label(m_ctx, "Cancel"))
        {
            hideNumberPad();
        }

        // Restore button style
        if (cancelSelected)
        {
            m_ctx->style.button = oldStyleGo;
        }

        // Show validation message if needed
        if (targetPage != 0 && !validPage)
        {
            nk_layout_row_dynamic(m_ctx, 20, 1);
            nk_label_colored(m_ctx, "Invalid page number", NK_TEXT_CENTERED, nk_rgb(255, 100, 100));
        }

        // Controller hints
        nk_layout_row_dynamic(m_ctx, 15, 1);
        nk_label_colored(m_ctx, "Controller: D-Pad=Navigate, A=Select", NK_TEXT_CENTERED, nk_rgb(150, 150, 150));
        nk_layout_row_dynamic(m_ctx, 15, 1);
        nk_label_colored(m_ctx, "B=Cancel, Start=Go", NK_TEXT_CENTERED, nk_rgb(150, 150, 150));
    }
    nk_end(m_ctx);
}

bool GuiManager::handleNumberPadInput(const SDL_Event& event)
{
    if (!m_showNumberPad)
    {
        return false;
    }

    if (event.type == SDL_JOYBUTTONDOWN)
    {
        if (event.jbutton.button == 10)
        {
            if (m_showNumberPad)
            {
                hideNumberPad();
            }
            else if (m_showFontMenu)
            {
                closeFontMenu();
            }
            return true;
        }
    }

    if (event.type == SDL_CONTROLLERBUTTONDOWN)
    {
        // Simple time-based debouncing
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - m_lastButtonPressTime < BUTTON_DEBOUNCE_MS)
        {
            return true; // Ignore rapid repeated presses
        }
        m_lastButtonPressTime = currentTime;

        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            m_numberPadSelectedRow = (m_numberPadSelectedRow - 1 + 5) % 5; // 5 rows (4 number rows + 1 action row)
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_numberPadSelectedRow = (m_numberPadSelectedRow + 1) % 5;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            if (m_numberPadSelectedRow == 4)
            { // Action row (Go/Cancel)
                m_numberPadSelectedCol = (m_numberPadSelectedCol - 1 + 2) % 2;
            }
            else
            {
                m_numberPadSelectedCol = (m_numberPadSelectedCol - 1 + 3) % 3;
            }
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            if (m_numberPadSelectedRow == 4)
            { // Action row (Go/Cancel)
                m_numberPadSelectedCol = (m_numberPadSelectedCol + 1) % 2;
            }
            else
            {
                m_numberPadSelectedCol = (m_numberPadSelectedCol + 1) % 3;
            }
            return true;
        case kAcceptButton:
        {
            // Handle selected button press
            if (m_numberPadSelectedRow == 4)
            { // Action row
                if (m_numberPadSelectedCol == 0)
                { // Go
                    int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
                    if (targetPage >= 1 && targetPage <= m_pageCount && m_pageJumpCallback)
                    {
                        m_pageJumpCallback(targetPage - 1);
                        hideNumberPad();
                    }
                }
                else
                { // Cancel
                    hideNumberPad();
                }
            }
            else
            {
                // Number grid
                const char* numberGrid[4][3] = {
                    {"7", "8", "9"},
                    {"4", "5", "6"},
                    {"1", "2", "3"},
                    {"Clear", "0", "Back"}};

                const char* buttonText = numberGrid[m_numberPadSelectedRow][m_numberPadSelectedCol];

                if (strcmp(buttonText, "Clear") == 0)
                {
                    strcpy(m_pageJumpInput, "");
                }
                else if (strcmp(buttonText, "Back") == 0)
                {
                    int len = strlen(m_pageJumpInput);
                    if (len > 0)
                    {
                        m_pageJumpInput[len - 1] = '\0';
                    }
                }
                else
                {
                    int len = strlen(m_pageJumpInput);
                    if (len < (int) sizeof(m_pageJumpInput) - 1)
                    {
                        m_pageJumpInput[len] = buttonText[0];
                        m_pageJumpInput[len + 1] = '\0';
                    }
                }
            }
            return true;
        }
        case kCancelButton:
            hideNumberPad();
            return true;
        case SDL_CONTROLLER_BUTTON_START:
            // Go to page if valid
            {
                int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
                if (targetPage >= 1 && targetPage <= m_pageCount && m_pageJumpCallback)
                {
                    m_pageJumpCallback(targetPage - 1);
                    hideNumberPad();
                }
                return true;
            }
        default:
            break;
        }
    }

    return false;
}

void GuiManager::showNumberPad()
{
    m_showNumberPad = true;
    m_pageJumpInput[0] = '\0';
    // Reset selection to middle of number pad (5)
    m_numberPadSelectedRow = 1; // Row with 4,5,6
    m_numberPadSelectedCol = 1; // Middle column (5)
    // Reset debounce timer
    m_lastButtonPressTime = 0;
}

void GuiManager::hideNumberPad()
{
    m_showNumberPad = false;
    requestFocusScroll();
}

int GuiManager::findFontIndex(const std::string& fontName) const
{
    auto fonts = m_optionsManager.getAvailableFonts();
    for (size_t i = 0; i < fonts.size(); ++i)
    {
        if (fonts[i].displayName == fontName)
        {
            return static_cast<int>(i);
        }
    }
    return 0; // Default to first font if not found
}

void GuiManager::rememberWidgetBounds(MainScreenWidget widget)
{
    if (!m_ctx || widget < 0 || widget >= WIDGET_COUNT)
    {
        return;
    }

    struct nk_rect bounds = nk_widget_bounds(m_ctx);
    WidgetBounds& slot = m_widgetBounds[widget];
    slot.x = bounds.x;
    slot.y = bounds.y;
    slot.w = bounds.w;
    slot.h = bounds.h;
    slot.valid = (bounds.w > 0.0f && bounds.h > 0.0f);
}

void GuiManager::requestFocusScroll()
{
    m_focusScrollPending = true;
}

void GuiManager::scrollFocusedWidgetIntoView()
{
    if (!m_focusScrollPending || !m_ctx)
    {
        return;
    }

    if (m_mainScreenFocusIndex < 0 || m_mainScreenFocusIndex >= WIDGET_COUNT)
    {
        m_focusScrollPending = false;
        return;
    }

    const WidgetBounds& bounds = m_widgetBounds[m_mainScreenFocusIndex];
    if (!bounds.valid)
    {
        m_focusScrollPending = false;
        return;
    }

    struct nk_rect clip = nk_window_get_content_region(m_ctx);
    m_windowClipY = clip.y;
    m_windowClipHeight = clip.h;

    nk_uint scrollX = 0;
    nk_uint scrollY = 0;
    nk_window_get_scroll(m_ctx, &scrollX, &scrollY);

    float clipTop = clip.y;
    float clipBottom = clip.y + clip.h;
    if (clipBottom - clipTop > 2.0f * kScrollPadding)
    {
        clipTop += kScrollPadding;
        clipBottom -= kScrollPadding;
    }

    float widgetTop = bounds.y;
    float widgetBottom = bounds.y + bounds.h;
    float newScroll = static_cast<float>(scrollY);

    if (widgetTop < clipTop)
    {
        newScroll = std::max(0.0f, newScroll - (clipTop - widgetTop));
    }
    else if (widgetBottom > clipBottom)
    {
        newScroll += (widgetBottom - clipBottom);
    }

    nk_uint newScrollY = static_cast<nk_uint>(std::max(0.0f, newScroll));
    if (newScrollY != scrollY)
    {
        nk_window_set_scroll(m_ctx, scrollX, newScrollY);
    }

    m_focusScrollPending = false;
}

void GuiManager::scrollSettingsToTop()
{
    if (!m_showFontMenu)
    {
        return;
    }
    m_scrollToTopPending = true;
}

void GuiManager::showInfoTooltip(MainScreenWidget widget, const char* text)
{
    if (!m_ctx || !text || widget < 0 || widget >= WIDGET_COUNT)
    {
        return;
    }

    const WidgetBounds& bounds = m_widgetBounds[widget];
    if (!bounds.valid)
    {
        return;
    }

    struct nk_command_buffer* canvas = nk_window_get_canvas(m_ctx);
    const struct nk_user_font* font = m_ctx->style.font;
    if (!canvas || !font || !font->width)
    {
        return;
    }

    float padding = 8.0f;
    float maxLineWidth = 0.0f;
    int lineCount = 1;
    const char* lineStart = text;
    for (const char* cursor = text;; ++cursor)
    {
        if (*cursor == '\n' || *cursor == '\0')
        {
            float width = font->width(font->userdata, font->height, lineStart, cursor - lineStart);
            maxLineWidth = std::max(maxLineWidth, width);
            if (*cursor == '\0')
            {
                break;
            }
            lineCount++;
            lineStart = cursor + 1;
        }
    }

    float tooltipW = std::max(200.0f, maxLineWidth + padding * 2.0f);
    float tooltipH = lineCount * font->height + padding * 2.0f;

    struct nk_rect windowBounds = nk_window_get_bounds(m_ctx);
    float tooltipX = bounds.x + bounds.w + 10.0f;
    if (tooltipX + tooltipW > windowBounds.x + windowBounds.w - 10.0f)
    {
        tooltipX = std::max(windowBounds.x + 10.0f, bounds.x - tooltipW - 10.0f);
    }
    float tooltipY = bounds.y - 6.0f;
    float minY = windowBounds.y + m_ctx->style.font->height + 4.0f;
    if (tooltipY < minY)
    {
        tooltipY = minY;
    }
    float maxY = windowBounds.y + windowBounds.h - tooltipH - 10.0f;
    if (tooltipY > maxY)
    {
        tooltipY = maxY;
    }

    struct nk_rect tooltipRect = nk_rect(tooltipX, tooltipY, tooltipW, tooltipH);
    m_pendingTooltips.push_back(PendingTooltip{tooltipRect.x, tooltipRect.y, tooltipRect.w, tooltipRect.h, text, padding});
}

void GuiManager::renderPendingTooltips()
{
    if (!m_ctx || m_pendingTooltips.empty())
    {
        return;
    }

    struct nk_command_buffer* canvas = nk_window_get_canvas(m_ctx);
    const struct nk_user_font* font = m_ctx->style.font;
    if (!canvas || !font)
    {
        m_pendingTooltips.clear();
        return;
    }

    const struct nk_color background = nk_rgba(35, 35, 38, 240);
    const struct nk_color border = nk_rgb(0, 122, 255);
    const struct nk_color textColor = nk_rgb(230, 230, 230);

    for (const PendingTooltip& tooltip : m_pendingTooltips)
    {
        struct nk_rect rect = nk_rect(tooltip.x, tooltip.y, tooltip.w, tooltip.h);
        nk_fill_rect(canvas, rect, 4.0f, background);
        nk_stroke_rect(canvas, rect, 4.0f, 1.0f, border);

        struct nk_rect textRect = rect;
        textRect.x += tooltip.padding;
        textRect.y += tooltip.padding;
        textRect.w -= tooltip.padding * 2.0f;

        float lineY = textRect.y;
        const char* data = tooltip.text.c_str();
        const char* segmentStart = data;
        for (const char* cursor = data;; ++cursor)
        {
            if (*cursor == '\n' || *cursor == '\0')
            {
                struct nk_rect lineRect = textRect;
                lineRect.y = lineY;
                lineRect.h = font->height;
                nk_draw_text(canvas, lineRect, segmentStart, static_cast<int>(cursor - segmentStart), font, nk_rgba(0, 0, 0, 0), textColor);
                lineY += font->height;
                if (*cursor == '\0')
                {
                    break;
                }
                segmentStart = cursor + 1;
            }
        }
    }

    m_pendingTooltips.clear();
}

void GuiManager::setupColorScheme()
{
    if (!m_ctx)
        return;

    struct nk_color table[NK_COLOR_COUNT];

    // Modern dark theme with blue accents
    table[NK_COLOR_TEXT] = nk_rgb(210, 210, 210);
    table[NK_COLOR_WINDOW] = nk_rgba(45, 45, 48, 248);
    table[NK_COLOR_HEADER] = nk_rgba(40, 40, 43, 252);
    table[NK_COLOR_BORDER] = nk_rgb(65, 65, 70);
    table[NK_COLOR_BUTTON] = nk_rgba(60, 60, 65, 246);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgba(70, 70, 75, 252);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgba(50, 50, 55, 255);
    table[NK_COLOR_TOGGLE] = nk_rgba(80, 80, 85, 246);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgba(90, 90, 95, 252);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_SELECT] = nk_rgba(40, 40, 43, 246);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgb(0, 122, 255);
    table[NK_COLOR_SLIDER] = nk_rgba(60, 60, 65, 246);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(30, 142, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(0, 102, 235);
    table[NK_COLOR_PROPERTY] = nk_rgba(60, 60, 65, 246);
    table[NK_COLOR_EDIT] = nk_rgba(50, 50, 55, 246);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_COMBO] = nk_rgba(60, 60, 65, 246);
    table[NK_COLOR_CHART] = nk_rgba(60, 60, 65, 246);
    table[NK_COLOR_CHART_COLOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgb(30, 142, 255);
    table[NK_COLOR_SCROLLBAR] = nk_rgba(50, 50, 55, 240);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgba(70, 70, 75, 246);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgba(80, 80, 85, 252);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgba(60, 60, 65, 252);
    table[NK_COLOR_TAB_HEADER] = nk_rgba(50, 50, 55, 252);

    nk_style_from_table(m_ctx, table);

    // Fine-tune some specific elements
    m_ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(35, 35, 38, 244));
    m_ctx->style.window.border_color = nk_rgb(0, 122, 255);
    m_ctx->style.window.border = 2.0f;
    m_ctx->style.window.rounding = 4.0f;

    // Button styling
    m_ctx->style.button.rounding = 3.0f;
    m_ctx->style.button.border = 1.0f;
    m_ctx->style.button.border_color = nk_rgb(80, 80, 85);

    // Edit field styling
    m_ctx->style.edit.rounding = 3.0f;
    m_ctx->style.edit.border = 1.0f;
    m_ctx->style.edit.border_color = nk_rgb(80, 80, 85);

    // Combo styling
    m_ctx->style.combo.rounding = 3.0f;
    m_ctx->style.combo.border = 1.0f;
    m_ctx->style.combo.border_color = nk_rgb(80, 80, 85);
}

bool GuiManager::handleKeyboardNavigation(const SDL_Event& event)
{
    if (!m_showFontMenu && !m_showNumberPad)
    {
        return false; // Only handle keyboard input when GUI is visible
    }

    if (event.type == SDL_KEYDOWN)
    {
        // Simple time-based debouncing (same as controller)
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - m_lastButtonPressTime < BUTTON_DEBOUNCE_MS)
        {
            return true; // Ignore rapid repeated presses
        }
        m_lastButtonPressTime = currentTime;

        if (m_fontDropdownOpen)
        {
            const auto& fonts = m_optionsManager.getAvailableFonts();
            int fontCount = static_cast<int>(fonts.size());
            if (fontCount == 0)
            {
                m_fontDropdownHighlightedIndex = 0;
            }

            switch (event.key.keysym.sym)
            {
            case SDLK_UP:
                if (fontCount > 0)
                {
                    m_fontDropdownHighlightedIndex = (m_fontDropdownHighlightedIndex - 1 + fontCount) % fontCount;
                    std::cout << "[DEBUG] Keyboard dropdown UP - highlight: " << m_fontDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDLK_DOWN:
                if (fontCount > 0)
                {
                    m_fontDropdownHighlightedIndex = (m_fontDropdownHighlightedIndex + 1) % fontCount;
                    std::cout << "[DEBUG] Keyboard dropdown DOWN - highlight: " << m_fontDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDLK_RETURN:
            case SDLK_SPACE:
                std::cout << "[DEBUG] Keyboard confirm dropdown index " << m_fontDropdownHighlightedIndex << std::endl;
                m_fontDropdownSelectRequested = true;
                return true;
            case SDLK_ESCAPE:
                std::cout << "[DEBUG] Keyboard cancel dropdown" << std::endl;
                m_fontDropdownCancelRequested = true;
                return true;
            case SDLK_LEFT:
            case SDLK_RIGHT:
                return true;
            default:
                break;
            }
        }

        if (m_styleDropdownOpen)
        {
            auto allStyles = OptionsManager::getAllReadingStyles();
            int styleCount = static_cast<int>(allStyles.size());
            if (styleCount == 0)
            {
                m_styleDropdownHighlightedIndex = 0;
            }

            switch (event.key.keysym.sym)
            {
            case SDLK_UP:
                if (styleCount > 0)
                {
                    m_styleDropdownHighlightedIndex = (m_styleDropdownHighlightedIndex - 1 + styleCount) % styleCount;
                    std::cout << "[DEBUG] Keyboard style dropdown UP - highlight: " << m_styleDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDLK_DOWN:
                if (styleCount > 0)
                {
                    m_styleDropdownHighlightedIndex = (m_styleDropdownHighlightedIndex + 1) % styleCount;
                    std::cout << "[DEBUG] Keyboard style dropdown DOWN - highlight: " << m_styleDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDLK_RETURN:
            case SDLK_SPACE:
                std::cout << "[DEBUG] Keyboard confirm style dropdown index " << m_styleDropdownHighlightedIndex << std::endl;
                m_styleDropdownSelectRequested = true;
                return true;
            case SDLK_ESCAPE:
                std::cout << "[DEBUG] Keyboard cancel style dropdown" << std::endl;
                m_styleDropdownCancelRequested = true;
                return true;
            case SDLK_LEFT:
            case SDLK_RIGHT:
                return true;
            default:
                break;
            }
        }

        switch (event.key.keysym.sym)
        {
        case SDLK_UP:
            // Navigate up - move to previous widget (same as numpad logic)
            if (m_mainScreenFocusIndex == WIDGET_FONT_DROPDOWN)
            {
                scrollSettingsToTop();
            }
            else if (!stepFocusVertical(-1))
            {
                scrollSettingsToTop();
            }
            return true;
        case SDLK_DOWN:
            // Navigate down - move to next widget (same as numpad logic)
            if (m_mainScreenFocusIndex == WIDGET_RESET_BUTTON)
            {
                m_mainScreenFocusIndex = WIDGET_FONT_DROPDOWN;
                requestFocusScroll();
                scrollSettingsToTop();
            }
            else
            {
                stepFocusVertical(1);
            }
            return true;
        case SDLK_LEFT:
            // Navigate left - adjust widget value if applicable
            if (!handleHorizontalNavigation(-1))
            {
                adjustFocusedWidget(-1);
            }
            return true;
        case SDLK_RIGHT:
            // Navigate right - adjust widget value if applicable
            if (!handleHorizontalNavigation(1))
            {
                adjustFocusedWidget(1);
            }
            return true;
        case SDLK_TAB:
            if (event.key.keysym.mod & KMOD_SHIFT)
            {
                // Shift+Tab for previous widget
                m_mainScreenFocusIndex = (m_mainScreenFocusIndex - 1 + WIDGET_COUNT) % WIDGET_COUNT;
                requestFocusScroll();
            }
            else
            {
                // Tab for next widget
                m_mainScreenFocusIndex = (m_mainScreenFocusIndex + 1) % WIDGET_COUNT;
                requestFocusScroll();
            }
            return true;
        case SDLK_RETURN:
        case SDLK_SPACE:
            // Activate the focused widget (same as numpad A button logic)
            activateFocusedWidget();
            return true;
        case SDLK_ESCAPE:
            // Handle escape to close menu (same as controller B button behavior)
            nk_input_key(m_ctx, NK_KEY_TEXT_END, 1);
            nk_input_key(m_ctx, NK_KEY_TEXT_END, 0);

            if (m_showFontMenu)
            {
                closeFontMenu();
            }
            else if (m_showNumberPad)
            {
                hideNumberPad();
            }
            return true;
        default:
            break;
        }
    }

    return false; // Event not handled
}

void GuiManager::adjustFocusedWidget(int direction)
{
    std::cout << "[DEBUG] Adjusting widget " << m_mainScreenFocusIndex << " by " << direction << std::endl;
    switch (m_mainScreenFocusIndex)
    {
    case WIDGET_FONT_SIZE_SLIDER:
        // Adjust font size
        m_tempConfig.fontSize = std::clamp(m_tempConfig.fontSize + direction, 8, 72);
        snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_tempConfig.fontSize);
        std::cout << "[DEBUG] Font size adjusted to: " << m_tempConfig.fontSize << std::endl;
        break;
    case WIDGET_ZOOM_STEP_SLIDER:
        // Adjust zoom step
        m_tempConfig.zoomStep = std::clamp(m_tempConfig.zoomStep + direction, 1, 50);
        snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_tempConfig.zoomStep);
        std::cout << "[DEBUG] Zoom step adjusted to: " << m_tempConfig.zoomStep << std::endl;
        break;
    default:
        // Other widgets don't respond to left/right adjustment
        std::cout << "[DEBUG] Widget " << m_mainScreenFocusIndex << " is not adjustable" << std::endl;
        break;
    }
}

void GuiManager::activateFocusedWidget()
{
    switch (m_mainScreenFocusIndex)
    {
    case WIDGET_FONT_DROPDOWN:
        if (!m_fontDropdownOpen)
        {
            const auto& fonts = m_optionsManager.getAvailableFonts();
            if (!fonts.empty())
            {
                std::cout << "[DEBUG] Opening font dropdown" << std::endl;
                m_fontDropdownOpen = true;
                m_fontDropdownSelectRequested = false;
                m_fontDropdownCancelRequested = false;
                if (m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size())
                {
                    m_fontDropdownHighlightedIndex = m_selectedFontIndex;
                }
                else
                {
                    m_fontDropdownHighlightedIndex = 0;
                }
            }
        }
        else
        {
            std::cout << "[DEBUG] Confirming selection from font dropdown" << std::endl;
            m_fontDropdownSelectRequested = true;
        }
        break;
    case WIDGET_READING_STYLE_DROPDOWN:
        if (!m_styleDropdownOpen)
        {
            auto allStyles = OptionsManager::getAllReadingStyles();
            if (!allStyles.empty())
            {
                std::cout << "[DEBUG] Opening reading style dropdown" << std::endl;
                m_styleDropdownOpen = true;
                m_styleDropdownSelectRequested = false;
                m_styleDropdownCancelRequested = false;
                if (m_selectedStyleIndex >= 0 && m_selectedStyleIndex < (int) allStyles.size())
                {
                    m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
                }
                else
                {
                    m_styleDropdownHighlightedIndex = 0;
                }
            }
        }
        else
        {
            std::cout << "[DEBUG] Confirming selection from reading style dropdown" << std::endl;
            m_styleDropdownSelectRequested = true;
        }
        break;
    case WIDGET_EDGE_PROGRESS_CHECKBOX:
        // Toggle checkbox
        m_tempConfig.disableEdgeProgressBar = !m_tempConfig.disableEdgeProgressBar;
        std::cout << "[DEBUG] Toggle Edge Progress Bar: " << (m_tempConfig.disableEdgeProgressBar ? "disabled" : "enabled") << std::endl;
        break;
    case WIDGET_MINIMAP_CHECKBOX:
        // Toggle checkbox
        m_tempConfig.showDocumentMinimap = !m_tempConfig.showDocumentMinimap;
        std::cout << "[DEBUG] Toggle Document Minimap: " << (m_tempConfig.showDocumentMinimap ? "enabled" : "disabled") << std::endl;
        break;
    case WIDGET_GO_BUTTON:
        // Activate Go button
        if (strlen(m_pageJumpInput) > 0)
        {
            int targetPage = std::atoi(m_pageJumpInput);
            if (targetPage >= 1 && targetPage <= m_pageCount && m_pageJumpCallback)
            {
                m_pageJumpCallback(targetPage - 1);
            }
        }
        break;
    case WIDGET_NUMPAD_BUTTON:
        // Show number pad
        showNumberPad();
        break;
    case WIDGET_APPLY_BUTTON:
    {
        const auto& fonts = m_optionsManager.getAvailableFonts();
        bool hasValidFont = !fonts.empty() && m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size();
        if (hasValidFont && m_fontApplyCallback)
        {
            m_currentConfig = m_tempConfig;
            m_optionsManager.saveConfig(m_currentConfig);
            m_fontApplyCallback(m_currentConfig);
        }
        break;
    }
    case WIDGET_RESET_BUTTON:
        // Reset to defaults
        m_tempConfig = FontConfig();
        m_selectedFontIndex = 0;
        m_selectedStyleIndex = 0;
        m_fontDropdownHighlightedIndex = m_selectedFontIndex;
        m_styleDropdownHighlightedIndex = m_selectedStyleIndex;
        snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_tempConfig.fontSize);
        snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_tempConfig.zoomStep);
        strcpy(m_pageJumpInput, "1");
        break;
    case WIDGET_CLOSE_BUTTON:
    {
        // Close menu
        closeFontMenu();
        break;
    }
    default:
        // Other widgets don't have simple activation
        break;
    }
}

bool GuiManager::moveFocusInGroup(const MainScreenWidget* group, size_t count, int direction)
{
    if (!group || count == 0 || direction == 0)
    {
        return false;
    }

    MainScreenWidget current = static_cast<MainScreenWidget>(m_mainScreenFocusIndex);
    for (size_t i = 0; i < count; ++i)
    {
        if (group[i] == current)
        {
            int nextIndex = static_cast<int>(i) + direction;
            if (nextIndex < 0 || nextIndex >= static_cast<int>(count))
            {
                return false;
            }
            m_mainScreenFocusIndex = group[nextIndex];
            requestFocusScroll();
            return true;
        }
    }
    return false;
}

bool GuiManager::isInfoWidget(MainScreenWidget widget) const
{
    return widget == WIDGET_EDGE_PROGRESS_INFO_BUTTON || widget == WIDGET_MINIMAP_INFO_BUTTON;
}

bool GuiManager::stepFocusVertical(int direction)
{
    if (direction == 0)
    {
        return false;
    }

    int nextIndex = m_mainScreenFocusIndex;
    while (true)
    {
        nextIndex += direction;
        if (nextIndex < 0 || nextIndex >= WIDGET_COUNT)
        {
            return false;
        }

        MainScreenWidget candidate = static_cast<MainScreenWidget>(nextIndex);
        if (!isInfoWidget(candidate))
        {
            m_mainScreenFocusIndex = nextIndex;
            requestFocusScroll();
            return true;
        }
    }
}

bool GuiManager::handleHorizontalNavigation(int direction)
{
    static constexpr MainScreenWidget kEdgeInfoGroup[] = {
        WIDGET_EDGE_PROGRESS_CHECKBOX,
        WIDGET_EDGE_PROGRESS_INFO_BUTTON};
    static constexpr MainScreenWidget kMinimapInfoGroup[] = {
        WIDGET_MINIMAP_CHECKBOX,
        WIDGET_MINIMAP_INFO_BUTTON};
    static constexpr MainScreenWidget kPageJumpGroup[] = {
        WIDGET_PAGE_JUMP_INPUT,
        WIDGET_GO_BUTTON,
        WIDGET_NUMPAD_BUTTON};
    static constexpr MainScreenWidget kActionButtonGroup[] = {
        WIDGET_APPLY_BUTTON,
        WIDGET_CLOSE_BUTTON,
        WIDGET_RESET_BUTTON};

    if (moveFocusInGroup(kEdgeInfoGroup, sizeof(kEdgeInfoGroup) / sizeof(kEdgeInfoGroup[0]), direction))
    {
        return true;
    }
    if (moveFocusInGroup(kMinimapInfoGroup, sizeof(kMinimapInfoGroup) / sizeof(kMinimapInfoGroup[0]), direction))
    {
        return true;
    }
    if (moveFocusInGroup(kPageJumpGroup, sizeof(kPageJumpGroup) / sizeof(kPageJumpGroup[0]), direction))
    {
        return true;
    }
    if (moveFocusInGroup(kActionButtonGroup, sizeof(kActionButtonGroup) / sizeof(kActionButtonGroup[0]), direction))
    {
        return true;
    }
    return false;
}

bool GuiManager::handleControllerInput(const SDL_Event& event)
{
    if (!m_showFontMenu && !m_showNumberPad)
    {
        return false; // Only handle controller input when GUI is visible
    }

    if (event.type == SDL_CONTROLLERBUTTONDOWN)
    {
        // Simple time-based debouncing
        Uint32 currentTime = SDL_GetTicks();
        if (currentTime - m_lastButtonPressTime < BUTTON_DEBOUNCE_MS)
        {
            return true; // Ignore rapid repeated presses
        }
        m_lastButtonPressTime = currentTime;

        if (m_fontDropdownOpen)
        {
            const auto& fonts = m_optionsManager.getAvailableFonts();
            int fontCount = static_cast<int>(fonts.size());
            if (fontCount == 0)
            {
                m_fontDropdownHighlightedIndex = 0;
            }

            switch (event.cbutton.button)
            {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (fontCount > 0)
                {
                    m_fontDropdownHighlightedIndex = (m_fontDropdownHighlightedIndex - 1 + fontCount) % fontCount;
                    std::cout << "[DEBUG] Controller dropdown UP - highlight: " << m_fontDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (fontCount > 0)
                {
                    m_fontDropdownHighlightedIndex = (m_fontDropdownHighlightedIndex + 1) % fontCount;
                    std::cout << "[DEBUG] Controller dropdown DOWN - highlight: " << m_fontDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                // Ignore left/right while dropdown is open
                return true;
            case kAcceptButton:
                std::cout << "[DEBUG] Controller A - confirm dropdown index " << m_fontDropdownHighlightedIndex << std::endl;
                m_fontDropdownSelectRequested = true;
                return true;
            case kCancelButton:
                std::cout << "[DEBUG] Controller B - cancel dropdown" << std::endl;
                m_fontDropdownCancelRequested = true;
                return true;
            default:
                break;
            }
        }

        if (m_styleDropdownOpen)
        {
            auto allStyles = OptionsManager::getAllReadingStyles();
            int styleCount = static_cast<int>(allStyles.size());
            if (styleCount == 0)
            {
                m_styleDropdownHighlightedIndex = 0;
            }

            switch (event.cbutton.button)
            {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (styleCount > 0)
                {
                    m_styleDropdownHighlightedIndex = (m_styleDropdownHighlightedIndex - 1 + styleCount) % styleCount;
                    std::cout << "[DEBUG] Controller style dropdown UP - highlight: " << m_styleDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (styleCount > 0)
                {
                    m_styleDropdownHighlightedIndex = (m_styleDropdownHighlightedIndex + 1) % styleCount;
                    std::cout << "[DEBUG] Controller style dropdown DOWN - highlight: " << m_styleDropdownHighlightedIndex << std::endl;
                }
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                // Ignore left/right while dropdown is open
                return true;
            case kAcceptButton:
                std::cout << "[DEBUG] Controller A - confirm style dropdown index " << m_styleDropdownHighlightedIndex << std::endl;
                m_styleDropdownSelectRequested = true;
                return true;
            case kCancelButton:
                std::cout << "[DEBUG] Controller B - cancel style dropdown" << std::endl;
                m_styleDropdownCancelRequested = true;
                return true;
            default:
                break;
            }
        }

        // Handle main screen navigation using our custom system
        if (m_showFontMenu)
        {
            switch (event.cbutton.button)
            {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                if (m_mainScreenFocusIndex == WIDGET_FONT_DROPDOWN)
                {
                    scrollSettingsToTop();
                }
                else if (!stepFocusVertical(-1))
                {
                    scrollSettingsToTop();
                }
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                if (m_mainScreenFocusIndex == WIDGET_RESET_BUTTON)
                {
                    m_mainScreenFocusIndex = WIDGET_FONT_DROPDOWN;
                    requestFocusScroll();
                    scrollSettingsToTop();
                }
                else
                {
                    stepFocusVertical(1);
                }
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                if (!handleHorizontalNavigation(-1))
                {
                    adjustFocusedWidget(-1);
                }
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                if (!handleHorizontalNavigation(1))
                {
                    adjustFocusedWidget(1);
                }
                return true;
            case kAcceptButton:
                activateFocusedWidget();
                return true;
            }
        }

        switch (event.cbutton.button)
        {
        case kCancelButton:
            // Send escape key first to exit any text input, then close menu
            nk_input_key(m_ctx, NK_KEY_TEXT_END, 1);
            nk_input_key(m_ctx, NK_KEY_TEXT_END, 0);

            if (m_showFontMenu)
            {
                closeFontMenu();
                return true;
            }
            return false;
        case kApplySettingsButton:
            // Apply current settings (Y on desktop, swapped with X on TG5040)
            if (m_showFontMenu)
            {
                const auto& fonts = m_optionsManager.getAvailableFonts();
                bool hasValidFont = !fonts.empty() && m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size();

                if (hasValidFont && m_fontApplyCallback)
                {
                    // Update current config
                    m_currentConfig = m_tempConfig;

                    // Save config to file
                    m_optionsManager.saveConfig(m_currentConfig);

                    // Call callback to apply changes
                    m_fontApplyCallback(m_currentConfig);
                }
                return true;
            }
            return false;
        case kResetSettingsButton:
            // Reset to defaults (X on desktop, swapped with Y on TG5040)
            if (m_showFontMenu)
            {
                m_tempConfig = FontConfig();
                m_selectedFontIndex = 0;

                // Reset input fields to defaults
                strcpy(m_fontSizeInput, "12");
                strcpy(m_zoomStepInput, "10");
                strcpy(m_pageJumpInput, "1");
                return true;
            }
            return false;
        case SDL_CONTROLLER_BUTTON_START:
            // Show number pad if in font menu
            if (m_showFontMenu)
            {
                showNumberPad();
                return true;
            }
            return false;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            // Tab to next control (useful for navigating between input fields)
            nk_input_key(m_ctx, NK_KEY_TAB, 1);
            nk_input_key(m_ctx, NK_KEY_TAB, 0);
            return true;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            // Shift+Tab to previous control - simulate holding shift while pressing tab
            nk_input_key(m_ctx, NK_KEY_SHIFT, 1);
            nk_input_key(m_ctx, NK_KEY_TAB, 1);
            nk_input_key(m_ctx, NK_KEY_TAB, 0);
            nk_input_key(m_ctx, NK_KEY_SHIFT, 0);
            return true;
        default:
            break;
        }
    }

    // Handle button up events to clear key states
    if (event.type == SDL_CONTROLLERBUTTONUP)
    {
        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            nk_input_key(m_ctx, NK_KEY_UP, 0);
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            nk_input_key(m_ctx, NK_KEY_DOWN, 0);
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            nk_input_key(m_ctx, NK_KEY_LEFT, 0);
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            nk_input_key(m_ctx, NK_KEY_RIGHT, 0);
            return true;
        case kAcceptButton:
            nk_input_key(m_ctx, NK_KEY_ENTER, 0);
            return true;
        // Shoulder buttons are handled with press and release together above
        // No separate release handling needed
        default:
            break;
        }
    }

    return false;
}

void GuiManager::closeFontMenu()
{
    if (!m_showFontMenu)
    {
        return;
    }

    m_showFontMenu = false;
    m_fontDropdownOpen = false;
    m_fontDropdownSelectRequested = false;
    m_fontDropdownCancelRequested = false;
    m_styleDropdownOpen = false;
    m_styleDropdownSelectRequested = false;
    m_styleDropdownCancelRequested = false;
    m_focusScrollPending = false;

    m_tempConfig = m_currentConfig;
    m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);
    m_fontDropdownHighlightedIndex = m_selectedFontIndex;

    // Update reading style index
    auto allStyles = OptionsManager::getAllReadingStyles();
    m_selectedStyleIndex = 0;
    for (size_t i = 0; i < allStyles.size(); i++)
    {
        if (allStyles[i] == m_currentConfig.readingStyle)
        {
            m_selectedStyleIndex = static_cast<int>(i);
            break;
        }
    }
    m_styleDropdownHighlightedIndex = m_selectedStyleIndex;

    snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
    snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);
    m_scrollToTopPending = false;
    m_focusScrollPending = false;

    if (m_closeCallback)
    {
        m_closeCallback();
    }

    std::cout << "Font menu closed" << std::endl;
}

void GuiManager::closeNumberPad()
{
    hideNumberPad();
}

bool GuiManager::closeAllUIWindows()
{
    bool anyOpen = m_showFontMenu || m_showNumberPad;
    closeFontMenu();
    closeNumberPad();
    return anyOpen;
}

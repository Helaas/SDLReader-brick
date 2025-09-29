#include "nuklear_gui_manager.h"
#include <algorithm>
#include <cstring>

// Define Nuklear implementation
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_DEFAULT_FONT
#define NK_INCLUDE_FONT_BAKING
#define NK_IMPLEMENTATION
#include "../thirdparty/nuklear/nuklear.h"

#define NK_SDL_RENDERER_IMPLEMENTATION
// Include the SDL renderer implementation
#include "../thirdparty/nuklear/demo/sdl_renderer/nuklear_sdl_renderer.h"

// NuklearGuiManager implementation
NuklearGuiManager::NuklearGuiManager()
{
    // Initialize font size input with default value
    int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
    if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
    {
        std::cerr << "Warning: Font size input buffer may be truncated" << std::endl;
    }
}

NuklearGuiManager::~NuklearGuiManager()
{
    cleanup();
}

bool NuklearGuiManager::initialize(SDL_Window* window, SDL_Renderer* renderer)
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

    // Setup font atlas - use simplified approach to avoid API compatibility issues
    struct nk_font_atlas* atlas;
    nk_sdl_font_stash_begin(&atlas);
    nk_sdl_font_stash_end();

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

    m_initialized = true;
    std::cout << "Nuklear GUI initialized successfully" << std::endl;
    return true;
}

void NuklearGuiManager::cleanup()
{
    if (!m_initialized)
    {
        return;
    }

    nk_sdl_shutdown();
    m_ctx = nullptr;

    m_initialized = false;
}

bool NuklearGuiManager::handleEvent(const SDL_Event& event)
{
    if (!m_initialized || !m_ctx)
    {
        return false;
    }

    // Debug: Print event information when GUI is visible
    if (m_showFontMenu || m_showNumberPad)
    {
        if (event.type == SDL_KEYDOWN)
        {
            std::cout << "[DEBUG] Key DOWN: " << SDL_GetKeyName(event.key.keysym.sym)
                      << " (scancode: " << event.key.keysym.scancode << ")" << std::endl;
        }
        else if (event.type == SDL_KEYUP)
        {
            std::cout << "[DEBUG] Key UP: " << SDL_GetKeyName(event.key.keysym.sym) << std::endl;
        }
        else if (event.type == SDL_MOUSEBUTTONDOWN)
        {
            std::cout << "[DEBUG] Mouse DOWN at (" << event.button.x << ", " << event.button.y << ")" << std::endl;
        }
        else if (event.type == SDL_CONTROLLERBUTTONDOWN)
        {
            std::cout << "[DEBUG] Controller button DOWN: " << event.cbutton.button << std::endl;
        }
    }

    // Handle number pad input if visible
    if (m_showNumberPad && handleNumberPadInput(event))
    {
        std::cout << "[DEBUG] Number pad handled event" << std::endl;
        return true;
    }

    // Handle keyboard navigation for GUI (before nuklear gets the events)
    if (handleKeyboardNavigation(event))
    {
        std::cout << "[DEBUG] Keyboard navigation handled event" << std::endl;
        return true;
    }

    // Handle controller input for general GUI navigation
    if (handleControllerInput(event))
    {
        std::cout << "[DEBUG] Controller input handled event" << std::endl;
        return true;
    }

    // Use the proper SDL event handler for remaining events
    bool nuklearHandled = nk_sdl_handle_event(const_cast<SDL_Event*>(&event));

    if (m_showFontMenu || m_showNumberPad)
    {
        std::cout << "[DEBUG] nuklear handled event: " << (nuklearHandled ? "YES" : "NO") << std::endl;
    }

    return nuklearHandled;
}

void NuklearGuiManager::newFrame()
{
    if (!m_initialized || !m_ctx)
    {
        return;
    }

    // Begin input processing - handleEvent will add input during event processing
    nk_input_begin(m_ctx);
}

void NuklearGuiManager::endFrame()
{
    if (!m_initialized || !m_ctx)
    {
        return;
    }

    // End input processing after all events have been handled
    nk_input_end(m_ctx);
}

void NuklearGuiManager::render()
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

bool NuklearGuiManager::isFontMenuVisible() const
{
    return m_showFontMenu;
}

void NuklearGuiManager::setCurrentFontConfig(const FontConfig& config)
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
}

bool NuklearGuiManager::wantsCaptureMouse() const
{
    // Simple implementation - capture mouse when any window is open
    return m_showFontMenu || m_showNumberPad;
}

bool NuklearGuiManager::wantsCaptureKeyboard() const
{
    // Simple implementation - capture keyboard when any window is open
    return m_showFontMenu || m_showNumberPad;
}

void NuklearGuiManager::renderFontMenu()
{
    if (!m_ctx)
        return;

    // Debug: Print nuklear input state periodically
    static int debugCounter = 0;
    if (debugCounter++ % 60 == 0) // Print every 60 frames (about once per second at 60fps)
    {
        std::cout << "[DEBUG] Nuklear state - mouse: ("
                  << m_ctx->input.mouse.pos.x << ", " << m_ctx->input.mouse.pos.y << ")"
                  << ", mouse down: " << (m_ctx->input.mouse.buttons[0].down ? "YES" : "NO")
                  << ", keys pressed: " << m_ctx->input.keyboard.text_len << std::endl;
    }

    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

    // Center the window and make it appropriately sized
    float centerX = windowWidth * 0.5f;
    float centerY = windowHeight * 0.5f;
    float windowW = 450.0f;
    float windowH = 600.0f;

    // Create settings window
    if (nk_begin(m_ctx, "Settings", nk_rect(centerX - windowW / 2, centerY - windowH / 2, windowW, windowH),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR))
    {
        // Set initial focus to enable keyboard navigation
        static bool initialFocusSet = false;
        if (!initialFocusSet)
        {
            // This helps with initial focus for keyboard/controller navigation
            initialFocusSet = true;
        }

        // Store original styles for highlighting focused widgets
        struct nk_style_button originalButtonStyle = m_ctx->style.button;
        struct nk_style_combo originalComboStyle = m_ctx->style.combo;
        struct nk_style_property originalPropertyStyle = m_ctx->style.property;
        struct nk_style_edit originalEditStyle = m_ctx->style.edit;
        struct nk_style_slider originalSliderStyle = m_ctx->style.slider;

        // === KEYBOARD NAVIGATION DEBUG ===
        nk_layout_row_dynamic(m_ctx, 20, 1);
        char focusDebug[64];
        const char* widgetNames[] = {
            "Font Dropdown", "Font Size Input", "Font Size Slider", "Zoom Step Input",
            "Zoom Step Slider", "Page Jump Input", "Go Button", "Numpad Button",
            "Apply Button", "Reset Button", "Close Button"};
        snprintf(focusDebug, sizeof(focusDebug), "Focus: %s (%d)",
                 widgetNames[m_mainScreenFocusIndex % WIDGET_COUNT], m_mainScreenFocusIndex);
        nk_label_colored(m_ctx, focusDebug, NK_TEXT_LEFT, nk_rgb(255, 255, 0));

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

            // Handle font cycling when controller activates
            if (m_openDropdownNextFrame)
            {
                m_openDropdownNextFrame = false;
                // Cycle to next font
                const auto& fonts = m_optionsManager.getAvailableFonts();
                if (!fonts.empty())
                {
                    m_selectedFontIndex = (m_selectedFontIndex + 1) % fonts.size();
                    m_tempConfig.fontPath = fonts[m_selectedFontIndex].filePath;
                    m_tempConfig.fontName = fonts[m_selectedFontIndex].displayName;
                    std::cout << "[DEBUG] Font cycled to: " << fonts[m_selectedFontIndex].displayName << std::endl;
                }
            }

            // Regular combo for mouse users
            if (nk_combo_begin_label(m_ctx, currentFont, nk_vec2(nk_widget_width(m_ctx), 200)))
            {
                nk_layout_row_dynamic(m_ctx, 20, 1);
                for (size_t i = 0; i < fonts.size(); ++i)
                {
                    if (nk_combo_item_label(m_ctx, fonts[i].displayName.c_str(), NK_TEXT_LEFT))
                    {
                        std::cout << "[DEBUG] Font selected: " << fonts[i].displayName << std::endl;
                        m_selectedFontIndex = static_cast<int>(i);
                        m_tempConfig.fontPath = fonts[i].filePath;
                        m_tempConfig.fontName = fonts[i].displayName;
                    }
                }
                nk_combo_end(m_ctx);
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

            // Restore slider style
            m_ctx->style.slider = originalSliderStyle;

            nk_layout_row_dynamic(m_ctx, 15, 1); // Spacing
        }

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

        nk_layout_row_dynamic(m_ctx, 20, 1);
        nk_label(m_ctx, "Jump to Page:", NK_TEXT_LEFT);

        // Page jump input and buttons
        nk_layout_row_template_begin(m_ctx, 25);
        nk_layout_row_template_push_static(m_ctx, 100);
        nk_layout_row_template_push_static(m_ctx, 60);
        nk_layout_row_template_push_static(m_ctx, 100);
        nk_layout_row_template_end(m_ctx);

        // Highlight page jump input if focused
        if (m_mainScreenFocusIndex == WIDGET_PAGE_JUMP_INPUT)
        {
            m_ctx->style.edit.normal = nk_style_item_color(nk_rgb(0, 122, 255));
            m_ctx->style.edit.hover = nk_style_item_color(nk_rgb(30, 142, 255));
            m_ctx->style.edit.active = nk_style_item_color(nk_rgb(0, 102, 235));
        }

        nk_edit_string_zero_terminated(m_ctx, NK_EDIT_FIELD | NK_EDIT_SELECTABLE, m_pageJumpInput, sizeof(m_pageJumpInput), nk_filter_decimal);

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

        nk_layout_row_template_begin(m_ctx, 30);
        nk_layout_row_template_push_static(m_ctx, 90);
        nk_layout_row_template_push_static(m_ctx, 20);
        nk_layout_row_template_push_static(m_ctx, 90);
        nk_layout_row_template_push_static(m_ctx, 20);
        nk_layout_row_template_push_static(m_ctx, 110);
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

        // Restore Apply button style
        m_ctx->style.button = originalButtonStyle;
        if (!hasValidFont)
        {
            nk_widget_disable_end(m_ctx);
        }

        nk_spacing(m_ctx, 1); // Empty column

        // Close button
        if (nk_button_label(m_ctx, "Close"))
        {
            m_showFontMenu = false;
            // Reset temp config to current config
            m_tempConfig = m_currentConfig;
            m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);

            // Reset input fields
            snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
            snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);

            if (m_closeCallback)
            {
                m_closeCallback();
            }
        }

        nk_spacing(m_ctx, 1); // Empty column

        // Reset to Default button
        if (nk_button_label(m_ctx, "Reset to Default"))
        {
            // Reset to default config
            m_tempConfig = FontConfig();
            m_selectedFontIndex = 0;

            // Reset input fields to defaults
            strcpy(m_fontSizeInput, "12");
            strcpy(m_zoomStepInput, "10");
            strcpy(m_pageJumpInput, "1");
        }
    }
    nk_end(m_ctx);
}

void NuklearGuiManager::renderNumberPad()
{
    if (!m_ctx)
        return;

    int windowWidth, windowHeight;
    SDL_GetWindowSize(m_window, &windowWidth, &windowHeight);

    // Center the number pad window
    float centerX = windowWidth * 0.5f;
    float centerY = windowHeight * 0.5f;
    float windowW = 300.0f;
    float windowH = 400.0f;

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
        nk_label_colored(m_ctx, "Controller: D-Pad=Navigate, A=Select, B=Backspace, Start=Go", NK_TEXT_CENTERED, nk_rgb(150, 150, 150));
    }
    nk_end(m_ctx);
}

bool NuklearGuiManager::handleNumberPadInput(const SDL_Event& event)
{
    if (!m_showNumberPad)
    {
        return false;
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
        case SDL_CONTROLLER_BUTTON_A:
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
        case SDL_CONTROLLER_BUTTON_B:
            // Backspace function or cancel
            {
                int len = strlen(m_pageJumpInput);
                if (len > 0)
                {
                    m_pageJumpInput[len - 1] = '\0';
                }
                else
                {
                    hideNumberPad();
                }
                return true;
            }
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

void NuklearGuiManager::showNumberPad()
{
    m_showNumberPad = true;
    m_pageJumpInput[0] = '\0';
    // Reset selection to middle of number pad (5)
    m_numberPadSelectedRow = 1; // Row with 4,5,6
    m_numberPadSelectedCol = 1; // Middle column (5)
    // Reset debounce timer
    m_lastButtonPressTime = 0;
}

void NuklearGuiManager::hideNumberPad()
{
    m_showNumberPad = false;
}

int NuklearGuiManager::findFontIndex(const std::string& fontName) const
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

void NuklearGuiManager::setFontSelectionCallback(std::function<void(const std::string&)> callback)
{
    // For now, this can be stored for future use if needed for font callbacks
    // Currently font selection is handled through setFontApplyCallback
}

void NuklearGuiManager::setupColorScheme()
{
    if (!m_ctx)
        return;

    struct nk_color table[NK_COLOR_COUNT];

    // Modern dark theme with blue accents
    table[NK_COLOR_TEXT] = nk_rgb(210, 210, 210);
    table[NK_COLOR_WINDOW] = nk_rgb(45, 45, 48);
    table[NK_COLOR_HEADER] = nk_rgb(40, 40, 43);
    table[NK_COLOR_BORDER] = nk_rgb(65, 65, 70);
    table[NK_COLOR_BUTTON] = nk_rgb(60, 60, 65);
    table[NK_COLOR_BUTTON_HOVER] = nk_rgb(70, 70, 75);
    table[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(50, 50, 55);
    table[NK_COLOR_TOGGLE] = nk_rgb(80, 80, 85);
    table[NK_COLOR_TOGGLE_HOVER] = nk_rgb(90, 90, 95);
    table[NK_COLOR_TOGGLE_CURSOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_SELECT] = nk_rgb(40, 40, 43);
    table[NK_COLOR_SELECT_ACTIVE] = nk_rgb(0, 122, 255);
    table[NK_COLOR_SLIDER] = nk_rgb(60, 60, 65);
    table[NK_COLOR_SLIDER_CURSOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(30, 142, 255);
    table[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(0, 102, 235);
    table[NK_COLOR_PROPERTY] = nk_rgb(60, 60, 65);
    table[NK_COLOR_EDIT] = nk_rgb(50, 50, 55);
    table[NK_COLOR_EDIT_CURSOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_COMBO] = nk_rgb(60, 60, 65);
    table[NK_COLOR_CHART] = nk_rgb(60, 60, 65);
    table[NK_COLOR_CHART_COLOR] = nk_rgb(0, 122, 255);
    table[NK_COLOR_CHART_COLOR_HIGHLIGHT] = nk_rgb(30, 142, 255);
    table[NK_COLOR_SCROLLBAR] = nk_rgb(50, 50, 55);
    table[NK_COLOR_SCROLLBAR_CURSOR] = nk_rgb(70, 70, 75);
    table[NK_COLOR_SCROLLBAR_CURSOR_HOVER] = nk_rgb(80, 80, 85);
    table[NK_COLOR_SCROLLBAR_CURSOR_ACTIVE] = nk_rgb(60, 60, 65);
    table[NK_COLOR_TAB_HEADER] = nk_rgb(50, 50, 55);

    nk_style_from_table(m_ctx, table);

    // Fine-tune some specific elements
    m_ctx->style.window.fixed_background = nk_style_item_color(nk_rgb(35, 35, 38));
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

bool NuklearGuiManager::handleKeyboardNavigation(const SDL_Event& event)
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

        switch (event.key.keysym.sym)
        {
        case SDLK_UP:
            // Navigate up - move to previous widget (same as numpad logic)
            m_mainScreenFocusIndex = (m_mainScreenFocusIndex - 1 + WIDGET_COUNT) % WIDGET_COUNT;
            return true;
        case SDLK_DOWN:
            // Navigate down - move to next widget (same as numpad logic)
            m_mainScreenFocusIndex = (m_mainScreenFocusIndex + 1) % WIDGET_COUNT;
            return true;
        case SDLK_LEFT:
            // Navigate left - adjust widget value if applicable
            adjustFocusedWidget(-1);
            return true;
        case SDLK_RIGHT:
            // Navigate right - adjust widget value if applicable
            adjustFocusedWidget(1);
            return true;
        case SDLK_TAB:
            if (event.key.keysym.mod & KMOD_SHIFT)
            {
                // Shift+Tab for previous widget
                m_mainScreenFocusIndex = (m_mainScreenFocusIndex - 1 + WIDGET_COUNT) % WIDGET_COUNT;
            }
            else
            {
                // Tab for next widget
                m_mainScreenFocusIndex = (m_mainScreenFocusIndex + 1) % WIDGET_COUNT;
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
                m_showFontMenu = false;
                // Reset temp config to current config
                m_tempConfig = m_currentConfig;
                m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);

                // Reset input fields
                snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
                snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);

                if (m_closeCallback)
                {
                    m_closeCallback();
                }
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

void NuklearGuiManager::adjustFocusedWidget(int direction)
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

void NuklearGuiManager::activateFocusedWidget()
{
    switch (m_mainScreenFocusIndex)
    {
    case WIDGET_FONT_DROPDOWN:
        // Open the dropdown for font selection
        std::cout << "[DEBUG] Setting flag to open font dropdown" << std::endl;
        m_openDropdownNextFrame = true;
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
        // Apply settings
        if (m_fontApplyCallback)
        {
            m_fontApplyCallback(m_tempConfig);
            m_currentConfig = m_tempConfig;
        }
        break;
    case WIDGET_RESET_BUTTON:
        // Reset to defaults
        m_tempConfig = FontConfig();
        m_selectedFontIndex = 0;
        snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_tempConfig.fontSize);
        snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_tempConfig.zoomStep);
        break;
    case WIDGET_CLOSE_BUTTON:
        // Close menu
        m_showFontMenu = false;
        m_tempConfig = m_currentConfig;
        m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);
        snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
        snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);
        if (m_closeCallback)
        {
            m_closeCallback();
        }
        break;
    default:
        // Other widgets don't have simple activation
        break;
    }
}

bool NuklearGuiManager::handleControllerInput(const SDL_Event& event)
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

        // Handle main screen navigation using our custom system
        if (m_showFontMenu)
        {
            std::cout << "[DEBUG] Controller dpad input for font menu: " << event.cbutton.button << std::endl;
            switch (event.cbutton.button)
            {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                // Navigate up - move to previous widget
                m_mainScreenFocusIndex = (m_mainScreenFocusIndex - 1 + WIDGET_COUNT) % WIDGET_COUNT;
                std::cout << "[DEBUG] Controller UP - new focus: " << m_mainScreenFocusIndex << std::endl;
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                // Navigate down - move to next widget
                m_mainScreenFocusIndex = (m_mainScreenFocusIndex + 1) % WIDGET_COUNT;
                std::cout << "[DEBUG] Controller DOWN - new focus: " << m_mainScreenFocusIndex << std::endl;
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                std::cout << "[DEBUG] Controller LEFT - adjusting widget" << std::endl;
                adjustFocusedWidget(-1);
                return true;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                std::cout << "[DEBUG] Controller RIGHT - adjusting widget" << std::endl;
                adjustFocusedWidget(1);
                return true;
            case SDL_CONTROLLER_BUTTON_A:
                std::cout << "[DEBUG] Controller A - activating widget " << m_mainScreenFocusIndex << std::endl;
                activateFocusedWidget();
                return true;
            }
        }

        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_B:
            // Send escape key first to exit any text input, then close menu
            nk_input_key(m_ctx, NK_KEY_TEXT_END, 1);
            nk_input_key(m_ctx, NK_KEY_TEXT_END, 0);

            if (m_showFontMenu)
            {
                m_showFontMenu = false;
                // Reset temp config to current config
                m_tempConfig = m_currentConfig;
                m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);

                // Reset input fields
                snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
                snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);

                if (m_closeCallback)
                {
                    m_closeCallback();
                }
                return true;
            }
            return false;
        case SDL_CONTROLLER_BUTTON_Y:
            // Apply current settings (like Y for confirm/apply)
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
        case SDL_CONTROLLER_BUTTON_X:
            // Reset to defaults
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
        case SDL_CONTROLLER_BUTTON_A:
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

#include "gui_manager.h"
#include <algorithm>
#include <cstring> // for strcpy
#include <imgui.h>
#include <imgui_internal.h> // For navigation wrapping functions

// Platform-specific ImGui backends
#ifdef TG5040_PLATFORM
#include <imgui_impl_sdl.h>         // TG5040 uses v1.85 headers
#include <imgui_impl_sdlrenderer.h> // Compatible with SDL 2.0.9
#else
#include <imgui_impl_sdl2.h> // Modern platforms use v1.89+ headers
#include <imgui_impl_sdlrenderer2.h>
#endif

#include <iostream>

GuiManager::GuiManager()
{
    // Initialize page jump input
    strcpy(m_pageJumpInput, "1"); // Start with page 1
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

    // Store the renderer for later use
    m_renderer = renderer;

    // Setup Dear ImGui context (or reuse existing one from FileBrowser)
    bool isNewContext = (ImGui::GetCurrentContext() == nullptr);
    if (isNewContext)
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
    }

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

#ifdef TG5040_PLATFORM
    // GuiManager uses 1.98x scaling for better menu fit on 640x480 display (1.8x + 10%)
    // Always reset to default style first to avoid cumulative scaling
    ImGui::GetStyle() = ImGuiStyle();       // Reset to default
    ImGui::StyleColorsDark();               // Apply dark colors
    ImGui::GetStyle().ScaleAllSizes(1.98f); // Scale to 1.98x from base
    io.FontGlobalScale = 1.98f;
#else
    // Setup Dear ImGui style for non-TG5040 platforms
    ImGui::StyleColorsDark();
#endif

    // Setup Platform/Renderer backends
#ifdef TG5040_PLATFORM
    // TG5040 uses patched v1.85 SDL Renderer backend for framebuffer compatibility
    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL backend" << std::endl;
        return false;
    }

    if (!ImGui_ImplSDLRenderer_Init(renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL Renderer backend" << std::endl;
        ImGui_ImplSDL2_Shutdown();
        return false;
    }
#else
    // Modern platforms use SDL Renderer backend
    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL2 backend" << std::endl;
        return false;
    }

    if (!ImGui_ImplSDLRenderer2_Init(renderer))
    {
        std::cerr << "Failed to initialize ImGui SDL Renderer backend" << std::endl;
        ImGui_ImplSDL2_Shutdown();
        return false;
    }
#endif

    m_initialized = true;

    // Reset ImGui IO state to ensure clean slate (important after file browser)
    {
        ImGuiIO& ioReset = ImGui::GetIO();
        ioReset.WantCaptureKeyboard = false;
        ioReset.WantCaptureMouse = false;
        ioReset.NavActive = false;
        ioReset.NavVisible = false;
    }

    // Initialize font manager and load config
    m_currentConfig = m_optionsManager.loadConfig();
    m_tempConfig = m_currentConfig;

    // Get available fonts
    const auto& fonts = m_optionsManager.getAvailableFonts();

    // Update UI state and ensure we have a valid font selected
    if (!m_currentConfig.fontName.empty())
    {
        m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);
        // Ensure the index is valid
        if (m_selectedFontIndex < 0 || m_selectedFontIndex >= (int) fonts.size())
        {
            m_selectedFontIndex = 0;
            // If the saved font wasn't found, update tempConfig with first available font
            if (!fonts.empty())
            {
                m_tempConfig.fontPath = fonts[0].filePath;
                m_tempConfig.fontName = fonts[0].displayName;
            }
        }
        else
        {
            // Font was found, make sure tempConfig has the font path if it's missing
            if (!fonts.empty() && m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size())
            {
                const FontInfo& selectedFont = fonts[m_selectedFontIndex];
                // Update tempConfig if fontPath is empty (important for saved configs)
                if (m_tempConfig.fontPath.empty())
                {
                    m_tempConfig.fontPath = selectedFont.filePath;
                    m_tempConfig.fontName = selectedFont.displayName;
                }
            }
        }
    }
    else
    {
        // No saved config, initialize with first available font
        if (!fonts.empty())
        {
            m_selectedFontIndex = 0;
            m_tempConfig.fontPath = fonts[0].filePath;
            m_tempConfig.fontName = fonts[0].displayName;
            m_tempConfig.fontSize = 12; // default size
        }
    }

    return true;
}

void GuiManager::cleanup()
{
    if (!m_initialized)
    {
        return;
    }

#ifdef TG5040_PLATFORM
    ImGui_ImplSDLRenderer_Shutdown();
    ImGui_ImplSDL2_Shutdown();
#else
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
#endif

    // NOTE: We do NOT call ImGui::DestroyContext() here because:
    // 1. In browse mode, the file browser will need to create a new ImGui context after this
    // 2. Destroying and recreating contexts can cause issues with SDL/ImGui state
    // 3. The context will be cleaned up when the program actually exits (at cleanupSDL)

    m_initialized = false;
}

bool GuiManager::handleEvent(const SDL_Event& event)
{
    if (!m_initialized)
    {
        return false;
    }

    // Handle number pad input first if it's visible - don't let ImGui process controller events
    if (m_showNumberPad)
    {
        // Let GUIDE button pass through to app (for quit functionality)
        if (event.type == SDL_CONTROLLERBUTTONDOWN &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)
        {
            return false; // Don't handle, let it pass through
        }

#ifdef TG5040_PLATFORM
        // Handle B button to close number pad on TG5040
        // NOTE: On TG5040, SDL reports buttons swapped - SDL BUTTON_A = Physical B
        if (event.type == SDL_CONTROLLERBUTTONDOWN &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
        {
            std::cout << "[GuiManager/NumberPad] Physical B pressed (SDL reports BUTTON_A) - closing number pad" << std::endl;
            hideNumberPad();
            return true;
        }

        // Handle joystick button 10 to close number pad on TG5040
        if (event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 10)
        {
            hideNumberPad();
            return true;
        }
#endif

        // Handle controller buttons (except GUIDE and B on TG5040)
        if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP)
        {
            // Only handle if it's not GUIDE button
            if (event.cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE)
            {
#ifdef TG5040_PLATFORM
                // Skip SDL BUTTON_A on TG5040 (physical B) as it's handled above for closing
                if (event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
                {
                    return true; // Already handled
                }
#endif
                return handleNumberPadInput(event);
            }
        }
    }

    // Intercept special buttons when font menu is open
    if (m_showFontMenu)
    {
        // Let GUIDE button pass through to app (for quit functionality)
        if (event.type == SDL_CONTROLLERBUTTONDOWN &&
            event.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)
        {
            return false; // Don't handle, let it pass through
        }

#ifdef TG5040_PLATFORM
        // Handle joystick button 10 (toggle menu on TG5040) through mapper
        if (event.type == SDL_JOYBUTTONDOWN && event.jbutton.button == 10)
        {
            // Use mapper if available
            if (m_buttonMapper)
            {
                LogicalButton logicalButton = m_buttonMapper->mapJoystickButton(event.jbutton.button);
                if (logicalButton == LogicalButton::Extra2)
                {
                    toggleFontMenu();
                    if (m_closeCallback)
                    {
                        m_closeCallback();
                    }
                    return true; // Event handled
                }
            }
            else
            {
                // Fallback to direct handling if no mapper
                toggleFontMenu();
                if (m_closeCallback)
                {
                    m_closeCallback();
                }
                return true; // Event handled
            }
        }

        // Handle B button on TG5040 in font menu to close it
        // NOTE: On TG5040, SDL reports buttons swapped:
        //   - Physical A button → SDL reports as BUTTON_B
        //   - Physical B button → SDL reports as BUTTON_A
        // The ImGui backend has been patched to swap A & B so ImGui sees correct mappings.
        // Here we just need to intercept physical B (SDL BUTTON_A) to close the menu.
        if (event.type == SDL_CONTROLLERBUTTONDOWN && event.cbutton.button == SDL_CONTROLLER_BUTTON_A)
        {
            std::cout << "[GuiManager/FontMenu] Physical B pressed (SDL BUTTON_A) - closing menu" << std::endl;
            m_showFontMenu = false;
            // Reset temp config to current config
            m_tempConfig = m_currentConfig;
            m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);

            // Trigger redraw to clear menu from screen
            if (m_closeCallback)
            {
                m_closeCallback();
            }
            return true; // Event handled
        }
#endif
    }

    // IMPORTANT: Always let ImGui backend process events to maintain proper internal state
    // This is critical for gamepad navigation to work correctly when menus are opened
    // ImGui needs to track controller state even when menus are not visible
    ImGui_ImplSDL2_ProcessEvent(&event);

    // Only report event as "handled" if ImGui actually wants to capture it AND a menu is visible
    // This prevents ImGui from consuming events when no menu is open
    ImGuiIO& io = ImGui::GetIO();
    bool handled = false;

    // Only capture input if a menu is visible
    if (m_showFontMenu || m_showNumberPad)
    {
        // Check if ImGui wants to capture keyboard input
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT)
        {
            handled = io.WantCaptureKeyboard;
        }
        // Check if ImGui wants to capture mouse input
        else if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP ||
                 event.type == SDL_MOUSEMOTION || event.type == SDL_MOUSEWHEEL)
        {
            handled = io.WantCaptureMouse;
        }
        // For controller/gamepad, always capture if menu is visible
        else if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP ||
                 event.type == SDL_CONTROLLERAXISMOTION)
        {
            handled = true;
        }
    }

    return handled;
}

bool GuiManager::isFontMenuVisible() const
{
    return m_showFontMenu;
}

void GuiManager::newFrame()
{
    if (!m_initialized)
    {
        return;
    }

#ifdef TG5040_PLATFORM
    ImGui_ImplSDLRenderer_NewFrame();
    ImGui_ImplSDL2_NewFrame();
#else
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
#endif
    ImGui::NewFrame();
}

void GuiManager::render()
{
    if (!m_initialized || !m_renderer)
    {
        return;
    }

    // Render our font menu
    if (m_showFontMenu)
    {
        renderFontMenu();
    }

    // Render number pad if visible
    if (m_showNumberPad)
    {
        renderNumberPad();
    }

    // Render ImGui
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();

    // Only render draw data if there's actually something to show
    // This prevents stale menu rendering on TG5040
    if (draw_data && (m_showFontMenu || m_showNumberPad))
    {
#ifdef TG5040_PLATFORM
        ImGui_ImplSDLRenderer_RenderDrawData(draw_data);
#else
        ImGui_ImplSDLRenderer2_RenderDrawData(draw_data, m_renderer);
#endif
    }
}

void GuiManager::setCurrentFontConfig(const FontConfig& config)
{
    m_currentConfig = config;
    m_tempConfig = config;

    // Update UI state
    if (!config.fontName.empty())
    {
        m_selectedFontIndex = findFontIndex(config.fontName);
    }

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
}

bool GuiManager::wantsCaptureMouse() const
{
    if (!m_initialized)
    {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

bool GuiManager::wantsCaptureKeyboard() const
{
    if (!m_initialized)
    {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

void GuiManager::renderFontMenu()
{
    // Center the window and make it prominent
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
#ifdef TG5040_PLATFORM
    // TG5040: Use almost full screen size (640x480 display)
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.97f, io.DisplaySize.y * 0.97f), ImGuiCond_Always);
#else
    // Other platforms: Fixed width, auto height
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
#endif

    if (!ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        ImGui::End();
        return;
    }

    // Enable navigation wrapping - pressing up on first item goes to last item, and vice versa
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImGui::NavMoveRequestTryWrapping(window, ImGuiNavMoveFlags_LoopY);

    // Get available fonts for validation
    const auto& fonts = m_optionsManager.getAvailableFonts();

    // Ensure m_tempConfig has valid font data based on current selection
    // This handles cases where menu is opened and Apply is clicked without changing selection
    if (m_tempConfig.fontPath.empty() || m_tempConfig.fontName.empty())
    {
        if (!fonts.empty() && m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size())
        {
            const FontInfo& selectedFont = fonts[m_selectedFontIndex];
            m_tempConfig.fontPath = selectedFont.filePath;
            m_tempConfig.fontName = selectedFont.displayName;
        }
    }

    // === FONT SETTINGS SECTION ===
    ImGui::Text("Font Settings");
    ImGui::Separator();

    // Add informational notice about font settings
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f)); // Light blue info color
    ImGui::TextWrapped("Note: Use Document Default for original fonts. Custom fonts apply to EPUB/MOBI only; PDFs and comics use embedded fonts.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Font selection dropdown
    ImGui::Text("Font Family:");

    if (fonts.empty())
    {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "No fonts found in /fonts directory");
        ImGui::Text("Please add .ttf or .otf files to the fonts folder");
    }
    else
    {
        // Ensure selected index is valid
        if (m_selectedFontIndex < 0 || m_selectedFontIndex >= (int) fonts.size())
        {
            m_selectedFontIndex = 0;
        }

        // Build font names list directly from fonts (safer than maintaining separate cache)
        std::vector<const char*> fontNamesPtrs;
        fontNamesPtrs.reserve(fonts.size());
        for (const auto& font : fonts)
        {
            fontNamesPtrs.push_back(font.displayName.c_str());
        }

        // Set focus to font dropdown when menu is first opened
        if (m_justOpenedFontMenu)
        {
            ImGui::SetKeyboardFocusHere();
            m_justOpenedFontMenu = false; // Reset flag
        }

        if (ImGui::Combo("##FontFamily", &m_selectedFontIndex, fontNamesPtrs.data(), fontNamesPtrs.size()))
        {
            // Font selection changed - ensure index is still valid
            if (m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size())
            {
                const FontInfo& selectedFont = fonts[m_selectedFontIndex];
                m_tempConfig.fontPath = selectedFont.filePath;
                m_tempConfig.fontName = selectedFont.displayName;
            }
            else
            {
                // Reset to first font if index became invalid
                m_selectedFontIndex = 0;
                if (!fonts.empty())
                {
                    m_tempConfig.fontPath = fonts[0].filePath;
                    m_tempConfig.fontName = fonts[0].displayName;
                }
            }
        }
    }

    ImGui::Spacing();

    // Font size slider only
    ImGui::Text("Font Size: %d pt", m_tempConfig.fontSize);
    int tempSize = m_tempConfig.fontSize;
    if (ImGui::SliderInt("##FontSizeSlider", &tempSize, 8, 72))
    {
        m_tempConfig.fontSize = tempSize;
        m_fontSizeChanged = true;
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // === READING STYLE SECTION ===
    ImGui::Text("Reading Style");
    ImGui::Separator();

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f)); // Light blue info color
    ImGui::TextWrapped("Choose a color theme for comfortable reading. Applies to EPUB/MOBI only.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::Text("Color Theme:");

    // Get all available reading styles
    auto allStyles = OptionsManager::getAllReadingStyles();
    std::vector<const char*> styleNames;
    styleNames.reserve(allStyles.size());
    for (const auto& style : allStyles)
    {
        styleNames.push_back(OptionsManager::getReadingStyleName(style));
    }

    // Ensure selected index is valid
    if (m_selectedStyleIndex < 0 || m_selectedStyleIndex >= (int) allStyles.size())
    {
        m_selectedStyleIndex = 0;
    }

    if (ImGui::Combo("##ReadingStyle", &m_selectedStyleIndex, styleNames.data(), styleNames.size()))
    {
        // Style selection changed
        m_tempConfig.readingStyle = allStyles[m_selectedStyleIndex];
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // === ZOOM SETTINGS SECTION ===
    ImGui::Text("Zoom Settings");
    ImGui::Separator();

    ImGui::Text("Zoom Step: %d%%", m_tempConfig.zoomStep);
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Adjust the amount of zoom per step");

    // Zoom step slider only
    int tempZoomStep = m_tempConfig.zoomStep;
    if (ImGui::SliderInt("##ZoomStepSlider", &tempZoomStep, 1, 50))
    {
        m_tempConfig.zoomStep = tempZoomStep;
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // === PAGE NAVIGATION SECTION ===
    ImGui::Text("Page Navigation");
    ImGui::Separator();

    // Current page display
    ImGui::Text("Current Page: %d / %d", m_currentPage + 1, m_pageCount);

    ImGui::Spacing();

    // Edge progress bar option
    bool tempDisableEdgeBar = m_tempConfig.disableEdgeProgressBar;
    if (ImGui::Checkbox("Disable Edge Progress Bar", &tempDisableEdgeBar))
    {
        m_tempConfig.disableEdgeProgressBar = tempDisableEdgeBar;
    }

    ImGui::SameLine();
    // Make the help icon a small button so it can be focused with D-pad
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));                    // Transparent background
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f)); // Slight highlight on hover
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));  // Slight highlight when active
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1));             // Gray text
    if (ImGui::SmallButton("(?)##EdgeProgressBarHelp") || ImGui::IsItemFocused())
    {
        ImGui::SetTooltip("When enabled, panning at page edges will change pages instantly\nwithout delay. When disabled (default), hold at edge for 300ms.");
    }
    ImGui::PopStyleColor(4);

    ImGui::Spacing();

    bool tempShowMinimap = m_tempConfig.showDocumentMinimap;
    if (ImGui::Checkbox("Show Document Minimap", &tempShowMinimap))
    {
        m_tempConfig.showDocumentMinimap = tempShowMinimap;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 0.5f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1));
    if (ImGui::SmallButton("(?)##MinimapHelp") || ImGui::IsItemFocused())
    {
        ImGui::SetTooltip("Show a miniature page overlay when zoomed in. This helps visualize\nwhich part of the page is currently visible.");
    }
    ImGui::PopStyleColor(4);

    ImGui::Spacing();

    ImGui::Text("Jump to Page:");
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputText("##PageJump", m_pageJumpInput, sizeof(m_pageJumpInput), ImGuiInputTextFlags_CharsDecimal))
    {
        // Input is being edited, but we don't apply until Go button is pressed
    }

    // Validate page input
    bool validPageInput = false;
    int targetPage = std::atoi(m_pageJumpInput);
    if (targetPage >= 1 && targetPage <= m_pageCount)
    {
        validPageInput = true;
    }

    // Go button on same line as textbox
    ImGui::SameLine();
    if (!validPageInput)
    {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Go"))
    {
        if (validPageInput && m_pageJumpCallback)
        {
            m_pageJumpCallback(targetPage - 1); // Convert to 0-based
        }
    }
    if (!validPageInput)
    {
        ImGui::EndDisabled();
    }

    // Number pad button on same line as textbox
    ImGui::SameLine();
    if (ImGui::Button("Number Pad"))
    {
        // Show on-screen number pad for controller input
        showNumberPad();
    }

    // Show validation message if needed
    if (!validPageInput && targetPage != 0)
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid page number");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === BUTTONS SECTION ===
    bool hasValidFont = !fonts.empty() && m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size();

    // Check if font or style settings have changed
    bool fontSettingsChanged = (m_tempConfig.fontName != m_currentConfig.fontName ||
                                m_tempConfig.fontSize != m_currentConfig.fontSize ||
                                m_tempConfig.readingStyle != m_currentConfig.readingStyle);

    // Show warning if font settings changed
    if (fontSettingsChanged)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f)); // Yellow/orange warning color
#ifdef TG5040_PLATFORM
        ImGui::TextWrapped("Warning: Applying font/style changes will reload the document. This may take several seconds on TG5040.");
#else
        ImGui::TextWrapped("Note: Applying font/style changes will reload the document.");
#endif
        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    if (!hasValidFont)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Apply", ImVec2(90, 40)))
    {
        std::cout << "Apply button clicked!" << std::endl;
        if (hasValidFont && m_fontApplyCallback)
        {
            // Ensure m_tempConfig has valid font data based on current selection
            // This is critical if the user opens the menu and clicks Apply without changing the font
            if (m_tempConfig.fontPath.empty() || m_tempConfig.fontName.empty())
            {
                if (m_selectedFontIndex >= 0 && m_selectedFontIndex < (int) fonts.size())
                {
                    const FontInfo& selectedFont = fonts[m_selectedFontIndex];
                    m_tempConfig.fontPath = selectedFont.filePath;
                    m_tempConfig.fontName = selectedFont.displayName;
                    std::cout << "DEBUG: Fixed empty font config - set to: " << m_tempConfig.fontName << std::endl;
                }
            }

            std::cout << "DEBUG: Applying config - fontName: '" << m_tempConfig.fontName
                      << "', fontPath: '" << m_tempConfig.fontPath
                      << "', fontSize: " << m_tempConfig.fontSize
                      << ", readingStyle: " << static_cast<int>(m_tempConfig.readingStyle) << std::endl;

            // Update current config
            m_currentConfig = m_tempConfig;

            // Note: Config will be saved after successful application in app.cpp
            // Don't save here to avoid double-saving and saving before changes are applied

            // Call callback to apply changes
            m_fontApplyCallback(m_currentConfig);
        }
        else
        {
            std::cout << "Error: Cannot apply - invalid font or no callback" << std::endl;
        }
    }

    if (!hasValidFont)
    {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();

    if (ImGui::Button("Close", ImVec2(90, 40)))
    {
        std::cout << "Close button clicked!" << std::endl;
        m_showFontMenu = false;
        // Reset temp config to current config
        m_tempConfig = m_currentConfig;
        m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);

        // Reset reading style index
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

        // Trigger redraw to clear menu from screen
        if (m_closeCallback)
        {
            m_closeCallback();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset to Default", ImVec2(230, 40)))
    {
        // Reset to default config
        m_tempConfig = FontConfig();
        m_selectedFontIndex = 0;
        m_selectedStyleIndex = 0; // Reset to Default style

        // Set first available font as default
        if (!fonts.empty())
        {
            m_tempConfig.fontPath = fonts[0].filePath;
            m_tempConfig.fontName = fonts[0].displayName;
        }

        // Reset page jump input
        strcpy(m_pageJumpInput, "1");
    }

    ImGui::End();
}

void GuiManager::renderNumberPad()
{
    // Center the number pad window
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(310, 600), ImGuiCond_Always);

    if (!ImGui::Begin("Number Pad", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::Text("Enter Page Number:");
    ImGui::Separator();

    // Display current input
    ImGui::Text("Page: %s", m_pageJumpInput);
    ImGui::Separator();

    // Number pad layout (5x3 grid)
    // Row 0: 7, 8, 9
    // Row 1: 4, 5, 6
    // Row 2: 1, 2, 3
    // Row 3: Clear, 0, Back
    // Row 4: Go, Cancel, (empty)

    const char* buttons[5][3] = {
        {"7", "8", "9"},        // Row 0: numbers
        {"4", "5", "6"},        // Row 1: numbers
        {"1", "2", "3"},        // Row 2: numbers
        {"Clear", "0", "Back"}, // Row 3: utility buttons
        {"Go", "Cancel", ""}    // Row 4: action buttons
    };

    const float buttonSize = 85.0f;

    for (int row = 0; row < 5; row++)
    {
        for (int col = 0; col < 3; col++)
        {
            // Skip empty buttons
            if (strlen(buttons[row][col]) == 0)
                continue;

            if (col > 0 && strlen(buttons[row][col - 1]) > 0)
                ImGui::SameLine();

            // Highlight selected button
            bool isSelected = (m_numberPadSelectedRow == row && m_numberPadSelectedCol == col);
            if (isSelected)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.5f, 1.0f, 1.0f)); // Blue highlight
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.6f, 1.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.4f, 0.8f, 1.0f));
            }

            // Disable button to prevent ImGui from handling input
            // All input is handled by controller in handleNumberPadInput
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f); // Keep full opacity even when disabled

            ImGui::Button(buttons[row][col], ImVec2(buttonSize, buttonSize));

            ImGui::PopStyleVar();
            ImGui::PopItemFlag();

            if (isSelected)
            {
                ImGui::PopStyleColor(3);
            }
        }
    }

    // Show validation message if needed
    int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
    if (targetPage != 0 && (targetPage < 1 || targetPage > m_pageCount))
    {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid page number");
    }

    ImGui::End();
}

bool GuiManager::handleNumberPadInput(const SDL_Event& event)
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

        // Map physical button to logical button
        SDL_GameControllerButton physicalButton = static_cast<SDL_GameControllerButton>(event.cbutton.button);
        LogicalButton logicalButton = LogicalButton::Accept; // Default

#ifdef TG5040_PLATFORM
        // On TG5040, use direct SDL button mapping (not ButtonMapper) to avoid interference with ImGui patch
        // SDL reports buttons swapped: Physical A = SDL BUTTON_B, Physical B = SDL BUTTON_A
        // For number pad, we want:
        //   - Physical A (SDL BUTTON_B) = Accept (activate selected number pad button)
        //   - Physical B (SDL BUTTON_A) = Already handled above to close number pad
        switch (physicalButton)
        {
        case SDL_CONTROLLER_BUTTON_B:
            logicalButton = LogicalButton::Accept; // Physical A
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            logicalButton = LogicalButton::DPadUp;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            logicalButton = LogicalButton::DPadDown;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            logicalButton = LogicalButton::DPadLeft;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            logicalButton = LogicalButton::DPadRight;
            break;
        default:
            return true; // Ignore other buttons
        }
#else
        // On desktop platforms, use ButtonMapper
        if (m_buttonMapper)
        {
            logicalButton = m_buttonMapper->mapButton(physicalButton);
        }
#endif

        switch (logicalButton)
        {
        case LogicalButton::DPadUp:
            m_numberPadSelectedRow = (m_numberPadSelectedRow - 1 + 5) % 5;
            return true;
        case LogicalButton::DPadDown:
            m_numberPadSelectedRow = (m_numberPadSelectedRow + 1) % 5;
            return true;
        case LogicalButton::DPadLeft:
            // Handle navigation - row 4 has only 2 buttons (Go, Cancel)
            if (m_numberPadSelectedRow == 4)
            {
                m_numberPadSelectedCol = (m_numberPadSelectedCol - 1 + 2) % 2;
            }
            else
            {
                m_numberPadSelectedCol = (m_numberPadSelectedCol - 1 + 3) % 3;
            }
            return true;
        case LogicalButton::DPadRight:
            // Handle navigation - row 4 has only 2 buttons (Go, Cancel)
            if (m_numberPadSelectedRow == 4)
            {
                m_numberPadSelectedCol = (m_numberPadSelectedCol + 1) % 2;
            }
            else
            {
                m_numberPadSelectedCol = (m_numberPadSelectedCol + 1) % 3;
            }
            return true;
        case LogicalButton::Accept:
        {
            // Handle selected button press - use EXACT same layout as renderNumberPad
            const char* buttons[5][3] = {
                {"7", "8", "9"},        // Row 0: numbers
                {"4", "5", "6"},        // Row 1: numbers
                {"1", "2", "3"},        // Row 2: numbers
                {"Clear", "0", "Back"}, // Row 3: utility buttons
                {"Go", "Cancel", ""}    // Row 4: action buttons
            };

            const char* buttonText = buttons[m_numberPadSelectedRow][m_numberPadSelectedCol];

            // Skip empty buttons
            if (strlen(buttonText) == 0)
            {
                return true;
            }

            if (strcmp(buttonText, "Clear") == 0)
            {
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
            else if (strcmp(buttonText, "Go") == 0)
            {
                // Go to page if valid
                int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
                if (targetPage >= 1 && targetPage <= m_pageCount && m_pageJumpCallback)
                {
                    m_pageJumpCallback(targetPage - 1);
                    hideNumberPad();
                }
            }
            else if (strcmp(buttonText, "Cancel") == 0)
            {
                // Cancel - close number pad without action
                hideNumberPad();
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
            return true;
        }
        case LogicalButton::Cancel:
            // Backspace function - remove last character, or cancel if empty
            {
                int len = strlen(m_pageJumpInput);
                if (len > 0)
                {
                    m_pageJumpInput[len - 1] = '\0';
                }
                else
                {
                    // If input is empty, close number pad
                    hideNumberPad();
                }
                return true;
            }
        case LogicalButton::Menu:
            // Go to page if valid (alternative method)
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

    // Handle keyboard input as backup
    if (event.type == SDL_KEYDOWN)
    {
        switch (event.key.keysym.sym)
        {
        case SDLK_ESCAPE:
            hideNumberPad();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
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

int GuiManager::findFontIndex(const std::string& fontName) const
{
    const auto& fonts = m_optionsManager.getAvailableFonts();
    for (int i = 0; i < (int) fonts.size(); i++)
    {
        if (fonts[i].displayName == fontName)
        {
            return i;
        }
    }
    return 0; // Return 0 as safe default
}

void GuiManager::showNumberPad()
{
    m_showNumberPad = true;
    // Reset selection to top-left (row=0, col=0 = "7")
    m_numberPadSelectedRow = 0;
    m_numberPadSelectedCol = 0;
    // Reset debounce timer
    m_lastButtonPressTime = 0;
}

void GuiManager::hideNumberPad()
{
    m_showNumberPad = false;
    // Reset debounce timer
    m_lastButtonPressTime = 0;
}

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
    // Initialize font size input with default value (with bounds checking)
    int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
    if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
    {
        strcpy(m_fontSizeInput, "12"); // Safe fallback
    }

    // Initialize zoom step input
    result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);
    if (result < 0 || result >= (int) sizeof(m_zoomStepInput))
    {
        strcpy(m_zoomStepInput, "10"); // Safe fallback
    }

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

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

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
    std::cout << "Dear ImGui initialized successfully" << std::endl;

    // Initialize font manager and load config
    m_currentConfig = m_optionsManager.loadConfig();
    m_tempConfig = m_currentConfig;

    // Populate font names for dropdown
    const auto& fonts = m_optionsManager.getAvailableFonts();
    m_fontNames.clear();
    for (const auto& font : fonts)
    {
        m_fontNames.push_back(font.displayName);
    }

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
        int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
        if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
        {
            strcpy(m_fontSizeInput, "12"); // Safe fallback
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
            strcpy(m_fontSizeInput, "12");
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
    ImGui::DestroyContext();

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
        if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP)
        {
            return handleNumberPadInput(event);
        }
    }

    bool handled = ImGui_ImplSDL2_ProcessEvent(&event);
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
#ifdef TG5040_PLATFORM
    ImGui_ImplSDLRenderer_RenderDrawData(draw_data);
#else
    ImGui_ImplSDLRenderer2_RenderDrawData(draw_data, m_renderer);
#endif
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

    // Update font size input
    int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", config.fontSize);
    if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
    {
        strcpy(m_fontSizeInput, "12"); // Safe fallback
    }

    // Update zoom step input
    result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", config.zoomStep);
    if (result < 0 || result >= (int) sizeof(m_zoomStepInput))
    {
        strcpy(m_zoomStepInput, "10"); // Safe fallback
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
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.97f, io.DisplaySize.y * 0.92f), ImGuiCond_Always);
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

        // Update font names if needed
        if (m_fontNames.size() != fonts.size())
        {
            m_fontNames.clear();
            for (const auto& font : fonts)
            {
                m_fontNames.push_back(font.displayName);
            }
        }

        // Create const char* array for ImGui
        std::vector<const char*> fontNamesPtrs;
        for (const auto& name : m_fontNames)
        {
            fontNamesPtrs.push_back(name.c_str());
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

    // Font size input
    ImGui::Text("Font Size (pt):");
    if (ImGui::InputText("##FontSize", m_fontSizeInput, sizeof(m_fontSizeInput), ImGuiInputTextFlags_CharsDecimal))
    {
        int newSize = std::atoi(m_fontSizeInput);
        if (newSize >= 8 && newSize <= 72)
        {
            m_tempConfig.fontSize = newSize;
            m_fontSizeChanged = true;
        }
    }

    // Size slider for easier adjustment
    int tempSize = m_tempConfig.fontSize;
    if (ImGui::SliderInt("##FontSizeSlider", &tempSize, 8, 72))
    {
        m_tempConfig.fontSize = tempSize;
        int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", tempSize);
        if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
        {
            strcpy(m_fontSizeInput, "12"); // Safe fallback
        }
        m_fontSizeChanged = true;
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // === ZOOM SETTINGS SECTION ===
    ImGui::Text("Zoom Settings");
    ImGui::Separator();

    ImGui::Text("Zoom Step (%%):");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Amount to zoom in/out with +/- keys");

    if (ImGui::InputText("##ZoomStep", m_zoomStepInput, sizeof(m_zoomStepInput), ImGuiInputTextFlags_CharsDecimal))
    {
        int newStep = std::atoi(m_zoomStepInput);
        if (newStep >= 1 && newStep <= 50)
        {
            m_tempConfig.zoomStep = newStep;
        }
    }

    // Zoom step slider for easier adjustment
    int tempZoomStep = m_tempConfig.zoomStep;
    if (ImGui::SliderInt("##ZoomStepSlider", &tempZoomStep, 1, 50))
    {
        m_tempConfig.zoomStep = tempZoomStep;
        int result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", tempZoomStep);
        if (result < 0 || result >= (int) sizeof(m_zoomStepInput))
        {
            strcpy(m_zoomStepInput, "10"); // Safe fallback
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // === PAGE NAVIGATION SECTION ===
    ImGui::Text("Page Navigation");
    ImGui::Separator();

    // Current page display
    ImGui::Text("Current Page: %d / %d", m_currentPage + 1, m_pageCount);

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

    if (!hasValidFont)
    {
        ImGui::BeginDisabled();
    }

    if (ImGui::Button("Apply", ImVec2(90, 30)))
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
                      << "', fontPath: '" << m_tempConfig.fontPath << "'" << std::endl;

            // Update current config
            m_currentConfig = m_tempConfig;

            // Save config to file
            m_optionsManager.saveConfig(m_currentConfig);

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

    if (ImGui::Button("Close", ImVec2(90, 30)))
    {
        std::cout << "Close button clicked!" << std::endl;
        m_showFontMenu = false;
        // Reset temp config to current config
        m_tempConfig = m_currentConfig;
        m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);

        // Reset input fields
        int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
        if (result < 0 || result >= (int) sizeof(m_fontSizeInput))
        {
            strcpy(m_fontSizeInput, "12");
        }
        result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);
        if (result < 0 || result >= (int) sizeof(m_zoomStepInput))
        {
            strcpy(m_zoomStepInput, "10");
        }

        // Trigger redraw to clear menu from screen
        if (m_closeCallback)
        {
            m_closeCallback();
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Reset to Default", ImVec2(110, 30)))
    {
        // Reset to default config
        m_tempConfig = FontConfig();
        m_selectedFontIndex = 0;

        // Set first available font as default
        if (!fonts.empty())
        {
            m_tempConfig.fontPath = fonts[0].filePath;
            m_tempConfig.fontName = fonts[0].displayName;
        }

        // Reset input fields to defaults
        strcpy(m_fontSizeInput, "12");
        strcpy(m_zoomStepInput, "10");
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
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_Always);

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

    const float buttonSize = 60.0f;

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

            // Create button but disable functionality to prevent double input
            // Controller input is handled separately in handleNumberPadInput
            bool buttonPressed = ImGui::Button(buttons[row][col], ImVec2(buttonSize, buttonSize));

            // Only process ImGui button clicks if no controller input happened recently
            // This prevents double-input when controller and mouse/ImGui conflict
            Uint32 currentTime = SDL_GetTicks();
            if (buttonPressed && (currentTime - m_lastButtonPressTime > BUTTON_DEBOUNCE_MS))
            {

                // Handle button press (mouse click only, not controller)
                const char* buttonText = buttons[row][col];

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
                    // Add digit if there's space
                    int len = strlen(m_pageJumpInput);
                    if (len < (int) sizeof(m_pageJumpInput) - 1)
                    {
                        m_pageJumpInput[len] = buttonText[0];
                        m_pageJumpInput[len + 1] = '\0';
                    }
                }

                // Update debounce timer for mouse clicks too
                m_lastButtonPressTime = currentTime;
            }

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

        switch (event.cbutton.button)
        {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            m_numberPadSelectedRow = (m_numberPadSelectedRow - 1 + 5) % 5;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_numberPadSelectedRow = (m_numberPadSelectedRow + 1) % 5;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
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
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
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
        case SDL_CONTROLLER_BUTTON_A:
        {
            // Handle selected button press - use same layout as display
            const char* buttons[5][3] = {
                {"7", "8", "9"},
                {"4", "5", "6"},
                {"1", "2", "3"},
                {"Clear", "0", "Back"},
                {"Go", "Cancel", ""}};

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
        case SDL_CONTROLLER_BUTTON_B:
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
        case SDL_CONTROLLER_BUTTON_START:
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

#include "gui_manager.h"
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>
#include <iostream>
#include <algorithm>
#include <cstring>  // for strcpy

GuiManager::GuiManager() {
    // Initialize font size input with default value (with bounds checking)
    int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
    if (result < 0 || result >= sizeof(m_fontSizeInput)) {
        strcpy(m_fontSizeInput, "12");  // Safe fallback
    }
    
    // Initialize zoom step input
    result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);
    if (result < 0 || result >= sizeof(m_zoomStepInput)) {
        strcpy(m_zoomStepInput, "10");  // Safe fallback
    }
    
    // Initialize page jump input
    strcpy(m_pageJumpInput, "1");  // Start with page 1
}

GuiManager::~GuiManager() {
    cleanup();
}

bool GuiManager::initialize(SDL_Window* window, SDL_Renderer* renderer) {
    if (m_initialized) {
        return true;
    }

    // Store the renderer for later use
    m_renderer = renderer;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;   // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    if (!ImGui_ImplSDL2_InitForSDLRenderer(window, renderer)) {
        std::cerr << "Failed to initialize ImGui SDL2 backend" << std::endl;
        return false;
    }

    if (!ImGui_ImplSDLRenderer2_Init(renderer)) {
        std::cerr << "Failed to initialize ImGui SDL Renderer backend" << std::endl;
        ImGui_ImplSDL2_Shutdown();
        return false;
    }

    m_initialized = true;
    std::cout << "Dear ImGui initialized successfully" << std::endl;

    // Initialize font manager and load config
    m_currentConfig = m_fontManager.loadConfig();
    m_tempConfig = m_currentConfig;
    
    // Populate font names for dropdown
    const auto& fonts = m_fontManager.getAvailableFonts();
    m_fontNames.clear();
    for (const auto& font : fonts) {
        m_fontNames.push_back(font.displayName);
    }
    
    // Update UI state and ensure we have a valid font selected
    if (!m_currentConfig.fontName.empty()) {
        m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);
        // Ensure the index is valid
        if (m_selectedFontIndex < 0 || m_selectedFontIndex >= (int)fonts.size()) {
            m_selectedFontIndex = 0;
        }
        int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
        if (result < 0 || result >= sizeof(m_fontSizeInput)) {
            strcpy(m_fontSizeInput, "12");  // Safe fallback
        }
    } else {
        // No saved config, initialize with first available font
        if (!fonts.empty()) {
            m_selectedFontIndex = 0;
            m_tempConfig.fontPath = fonts[0].filePath;
            m_tempConfig.fontName = fonts[0].displayName;
            m_tempConfig.fontSize = 12; // default size
            strcpy(m_fontSizeInput, "12");
        }
    }

    return true;
}

void GuiManager::cleanup() {
    if (!m_initialized) {
        return;
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    m_initialized = false;
}

bool GuiManager::handleEvent(const SDL_Event& event) {
    if (!m_initialized) {
        return false;
    }

    // Handle number pad input first if it's visible - don't let ImGui process controller events
    if (m_showNumberPad) {
        if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
            std::cout << "[handleEvent] Number pad visible, handling controller event type " << event.type << std::endl;
            bool result = handleNumberPadInput(event);
            std::cout << "[handleEvent] Number pad handled: " << (result ? "true" : "false") << std::endl;
            return result;
        }
    }

    bool handled = ImGui_ImplSDL2_ProcessEvent(&event);
    return handled;
}

bool GuiManager::isFontMenuVisible() const {
    return m_showFontMenu;
}

void GuiManager::newFrame() {
    if (!m_initialized) {
        return;
    }

    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void GuiManager::render() {
    if (!m_initialized || !m_renderer) {
        return;
    }

    // Render our font menu
    if (m_showFontMenu) {
        renderFontMenu();
    }

    // Render number pad if visible
    if (m_showNumberPad) {
        renderNumberPad();
    }

    // Render ImGui
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplSDLRenderer2_RenderDrawData(draw_data, m_renderer);
}

void GuiManager::setCurrentFontConfig(const FontConfig& config) {
    m_currentConfig = config;
    m_tempConfig = config;
    
    // Update UI state
    if (!config.fontName.empty()) {
        m_selectedFontIndex = findFontIndex(config.fontName);
    }
    
    // Update font size input
    int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", config.fontSize);
    if (result < 0 || result >= sizeof(m_fontSizeInput)) {
        strcpy(m_fontSizeInput, "12");  // Safe fallback
    }
    
    // Update zoom step input
    result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", config.zoomStep);
    if (result < 0 || result >= sizeof(m_zoomStepInput)) {
        strcpy(m_zoomStepInput, "10");  // Safe fallback
    }
}

bool GuiManager::wantsCaptureMouse() const {
    if (!m_initialized) {
        return false;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureMouse;
}

bool GuiManager::wantsCaptureKeyboard() const {
    if (!m_initialized) {
        return false;
    }
    
    ImGuiIO& io = ImGui::GetIO();
    return io.WantCaptureKeyboard;
}

void GuiManager::renderFontMenu() {
    // Center the window and make it prominent
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always); // Fixed width, auto height

    if (!ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::End();
        return;
    }

    // === FONT SETTINGS SECTION ===
    ImGui::Text("Font Settings");
    ImGui::Separator();
    
    // Font selection dropdown
    ImGui::Text("Font Family:");
    
    const auto& fonts = m_fontManager.getAvailableFonts();
    if (fonts.empty()) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "No fonts found in /fonts directory");
        ImGui::Text("Please add .ttf or .otf files to the fonts folder");
    } else {
        // Ensure selected index is valid
        if (m_selectedFontIndex < 0 || m_selectedFontIndex >= (int)fonts.size()) {
            m_selectedFontIndex = 0;
        }
        
        // Update font names if needed
        if (m_fontNames.size() != fonts.size()) {
            m_fontNames.clear();
            for (const auto& font : fonts) {
                m_fontNames.push_back(font.displayName);
            }
        }
        
        // Create const char* array for ImGui
        std::vector<const char*> fontNamesPtrs;
        for (const auto& name : m_fontNames) {
            fontNamesPtrs.push_back(name.c_str());
        }

        if (ImGui::Combo("##FontFamily", &m_selectedFontIndex, fontNamesPtrs.data(), fontNamesPtrs.size())) {
            // Font selection changed - ensure index is still valid
            if (m_selectedFontIndex >= 0 && m_selectedFontIndex < (int)fonts.size()) {
                const FontInfo& selectedFont = fonts[m_selectedFontIndex];
                m_tempConfig.fontPath = selectedFont.filePath;
                m_tempConfig.fontName = selectedFont.displayName;
            } else {
                // Reset to first font if index became invalid
                m_selectedFontIndex = 0;
                if (!fonts.empty()) {
                    m_tempConfig.fontPath = fonts[0].filePath;
                    m_tempConfig.fontName = fonts[0].displayName;
                }
            }
        }
    }

    ImGui::Spacing();

    // Font size input
    ImGui::Text("Font Size (pt):");
    if (ImGui::InputText("##FontSize", m_fontSizeInput, sizeof(m_fontSizeInput), ImGuiInputTextFlags_CharsDecimal)) {
        int newSize = std::atoi(m_fontSizeInput);
        if (newSize >= 8 && newSize <= 72) {
            m_tempConfig.fontSize = newSize;
            m_fontSizeChanged = true;
        }
    }
    
    // Size slider for easier adjustment
    int tempSize = m_tempConfig.fontSize;
    if (ImGui::SliderInt("##FontSizeSlider", &tempSize, 8, 72)) {
        m_tempConfig.fontSize = tempSize;
        int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", tempSize);
        if (result < 0 || result >= sizeof(m_fontSizeInput)) {
            strcpy(m_fontSizeInput, "12");  // Safe fallback
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
    
    if (ImGui::InputText("##ZoomStep", m_zoomStepInput, sizeof(m_zoomStepInput), ImGuiInputTextFlags_CharsDecimal)) {
        int newStep = std::atoi(m_zoomStepInput);
        if (newStep >= 1 && newStep <= 50) {
            m_tempConfig.zoomStep = newStep;
        }
    }
    
    // Zoom step slider for easier adjustment
    int tempZoomStep = m_tempConfig.zoomStep;
    if (ImGui::SliderInt("##ZoomStepSlider", &tempZoomStep, 1, 50)) {
        m_tempConfig.zoomStep = tempZoomStep;
        int result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", tempZoomStep);
        if (result < 0 || result >= sizeof(m_zoomStepInput)) {
            strcpy(m_zoomStepInput, "10");  // Safe fallback
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
    if (ImGui::InputText("##PageJump", m_pageJumpInput, sizeof(m_pageJumpInput), ImGuiInputTextFlags_CharsDecimal)) {
        // Input is being edited, but we don't apply until Go button is pressed
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Number Pad")) {
        // Show on-screen number pad for controller input
        showNumberPad();
    }
    
    bool validPageInput = false;
    int targetPage = std::atoi(m_pageJumpInput);
    if (targetPage >= 1 && targetPage <= m_pageCount) {
        validPageInput = true;
    }
    
    if (!validPageInput) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Go")) {
        if (validPageInput && m_pageJumpCallback) {
            m_pageJumpCallback(targetPage - 1); // Convert to 0-based
        }
    }
    
    if (!validPageInput) {
        ImGui::EndDisabled();
    }
    
    if (!validPageInput && targetPage != 0) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid page number");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // === BUTTONS SECTION ===
    bool hasValidFont = !fonts.empty() && m_selectedFontIndex >= 0 && m_selectedFontIndex < (int)fonts.size();
    
    if (!hasValidFont) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Apply", ImVec2(90, 30))) {
        std::cout << "Apply button clicked!" << std::endl;
        if (hasValidFont && m_fontApplyCallback) {
            // Update current config
            m_currentConfig = m_tempConfig;
            
            // Save config to file
            m_fontManager.saveConfig(m_currentConfig);
            
            // Call callback to apply changes
            m_fontApplyCallback(m_currentConfig);
        } else {
            std::cout << "Error: Cannot apply - invalid font or no callback" << std::endl;
        }
    }
    
    if (!hasValidFont) {
        ImGui::EndDisabled();
    }

    ImGui::SameLine();
    
    if (ImGui::Button("Close", ImVec2(90, 30))) {
        std::cout << "Close button clicked!" << std::endl;
        m_showFontMenu = false;
        // Reset temp config to current config
        m_tempConfig = m_currentConfig;
        m_selectedFontIndex = findFontIndex(m_currentConfig.fontName);
        
        // Reset input fields
        int result = snprintf(m_fontSizeInput, sizeof(m_fontSizeInput), "%d", m_currentConfig.fontSize);
        if (result < 0 || result >= sizeof(m_fontSizeInput)) {
            strcpy(m_fontSizeInput, "12");
        }
        result = snprintf(m_zoomStepInput, sizeof(m_zoomStepInput), "%d", m_currentConfig.zoomStep);
        if (result < 0 || result >= sizeof(m_zoomStepInput)) {
            strcpy(m_zoomStepInput, "10");
        }
        
        // Trigger redraw to clear menu from screen
        if (m_closeCallback) {
            m_closeCallback();
        }
    }

    ImGui::SameLine();
    
    if (ImGui::Button("Reset to Default", ImVec2(110, 30))) {
        // Reset to default config
        m_tempConfig = FontConfig();
        m_selectedFontIndex = 0;
        
        // Reset input fields to defaults
        strcpy(m_fontSizeInput, "12");
        strcpy(m_zoomStepInput, "10");
        strcpy(m_pageJumpInput, "1");
    }

    ImGui::End();
}

void GuiManager::renderNumberPad() {
    // Center the number pad window
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
    
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_Always);

    if (!ImGui::Begin("Number Pad", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::Text("Enter Page Number:");
    ImGui::Separator();
    
    // Display current input
    ImGui::Text("Page: %s", m_pageJumpInput);
    ImGui::Separator();
    
    // Number pad layout (3x4 grid)
    // Row 0: 7, 8, 9
    // Row 1: 4, 5, 6  
    // Row 2: 1, 2, 3
    // Row 3: Clear, 0, Backspace
    
    const char* buttons[4][3] = {
        {"7", "8", "9"},
        {"4", "5", "6"},
        {"1", "2", "3"},
        {"Clear", "0", "Back"}
    };
    
    const float buttonSize = 60.0f;
    const float spacing = 10.0f;
    
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 3; col++) {
            if (col > 0) ImGui::SameLine();
            
            // Highlight selected button
            bool isSelected = (m_numberPadSelectedRow == row && m_numberPadSelectedCol == col);
            if (isSelected) {
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
            if (buttonPressed && (currentTime - m_lastButtonPressTime > BUTTON_DEBOUNCE_MS)) {
                std::cout << "[renderNumberPad] ImGui button clicked: " << buttons[row][col] << std::endl;
                
                // Handle button press (mouse click only, not controller)
                const char* buttonText = buttons[row][col];
                
                if (strcmp(buttonText, "Clear") == 0) {
                    // Clear all input
                    strcpy(m_pageJumpInput, "");
                } else if (strcmp(buttonText, "Back") == 0) {
                    // Backspace - remove last character
                    int len = strlen(m_pageJumpInput);
                    if (len > 0) {
                        m_pageJumpInput[len - 1] = '\0';
                    }
                } else {
                    // Add digit if there's space
                    int len = strlen(m_pageJumpInput);
                    if (len < sizeof(m_pageJumpInput) - 1) {
                        m_pageJumpInput[len] = buttonText[0];
                        m_pageJumpInput[len + 1] = '\0';
                    }
                }
                
                // Update debounce timer for mouse clicks too
                m_lastButtonPressTime = currentTime;
            } else if (buttonPressed) {
                std::cout << "[renderNumberPad] ImGui button click BLOCKED due to recent controller input" << std::endl;
            }
            
            if (isSelected) {
                ImGui::PopStyleColor(3);
            }
        }
    }
    
    ImGui::Separator();
    
    // Action buttons
    bool validPageInput = false;
    int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
    if (targetPage >= 1 && targetPage <= m_pageCount) {
        validPageInput = true;
    }
    
    if (!validPageInput) {
        ImGui::BeginDisabled();
    }
    
    if (ImGui::Button("Go to Page", ImVec2(120, 30))) {
        if (validPageInput && m_pageJumpCallback) {
            m_pageJumpCallback(targetPage - 1); // Convert to 0-based
            hideNumberPad();
        }
    }
    
    if (!validPageInput) {
        ImGui::EndDisabled();
    }
    
    ImGui::SameLine();
    
    if (ImGui::Button("Cancel", ImVec2(80, 30))) {
        hideNumberPad();
    }
    
    if (!validPageInput && targetPage != 0) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Invalid page number");
    }

    ImGui::End();
}

bool GuiManager::handleNumberPadInput(const SDL_Event& event) {
    if (!m_showNumberPad) {
        return false;
    }
    
    if (event.type == SDL_CONTROLLERBUTTONDOWN) {
        // Debug output
        Uint32 currentTime = SDL_GetTicks();
        std::cout << "=== CONTROLLER BUTTON DOWN ===" << std::endl;
        std::cout << "Button: " << (int)event.cbutton.button << std::endl;
        std::cout << "Current time: " << currentTime << std::endl;
        std::cout << "Last press time: " << m_lastButtonPressTime << std::endl;
        std::cout << "Time diff: " << (currentTime - m_lastButtonPressTime) << std::endl;
        std::cout << "Debounce threshold: " << BUTTON_DEBOUNCE_MS << std::endl;
        
        // Simple time-based debouncing
        if (currentTime - m_lastButtonPressTime < BUTTON_DEBOUNCE_MS) {
            std::cout << "BLOCKED: Too soon, ignoring" << std::endl;
            return true; // Ignore rapid repeated presses
        }
        
        std::cout << "PROCESSING: Button press accepted" << std::endl;
        m_lastButtonPressTime = currentTime;
        
        switch (event.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            m_numberPadSelectedRow = (m_numberPadSelectedRow - 1 + 4) % 4;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            m_numberPadSelectedRow = (m_numberPadSelectedRow + 1) % 4;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            m_numberPadSelectedCol = (m_numberPadSelectedCol - 1 + 3) % 3;
            return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            m_numberPadSelectedCol = (m_numberPadSelectedCol + 1) % 3;
            return true;
        case SDL_CONTROLLER_BUTTON_A:
            {
                std::cout << "A BUTTON PRESSED - Processing number selection" << std::endl;
                
                // Simulate button press for selected button
                const char* buttons[4][3] = {
                    {"7", "8", "9"},
                    {"4", "5", "6"},
                    {"1", "2", "3"},
                    {"Clear", "0", "Back"}
                };
                
                const char* buttonText = buttons[m_numberPadSelectedRow][m_numberPadSelectedCol];
                std::cout << "Selected button: " << buttonText << " (row=" << m_numberPadSelectedRow << ", col=" << m_numberPadSelectedCol << ")" << std::endl;
                std::cout << "Current input before: '" << m_pageJumpInput << "'" << std::endl;
                
                if (strcmp(buttonText, "Clear") == 0) {
                    strcpy(m_pageJumpInput, "");
                    std::cout << "CLEAR - Input cleared" << std::endl;
                } else if (strcmp(buttonText, "Back") == 0) {
                    int len = strlen(m_pageJumpInput);
                    if (len > 0) {
                        m_pageJumpInput[len - 1] = '\0';
                        std::cout << "BACK - Removed last character" << std::endl;
                    }
                } else {
                    int len = strlen(m_pageJumpInput);
                    if (len < sizeof(m_pageJumpInput) - 1) {
                        m_pageJumpInput[len] = buttonText[0];
                        m_pageJumpInput[len + 1] = '\0';
                        std::cout << "DIGIT - Added '" << buttonText[0] << "'" << std::endl;
                    }
                }
                
                std::cout << "Current input after: '" << m_pageJumpInput << "'" << std::endl;
                std::cout << "===========================" << std::endl;
                return true;
            }
        case SDL_CONTROLLER_BUTTON_B:
            // Cancel/close number pad
            hideNumberPad();
            return true;
        case SDL_CONTROLLER_BUTTON_START:
            // Go to page if valid
            {
                int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
                if (targetPage >= 1 && targetPage <= m_pageCount && m_pageJumpCallback) {
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
    if (event.type == SDL_KEYDOWN) {
        switch (event.key.keysym.sym) {
        case SDLK_ESCAPE:
            hideNumberPad();
            return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            {
                int targetPage = strlen(m_pageJumpInput) > 0 ? std::atoi(m_pageJumpInput) : 0;
                if (targetPage >= 1 && targetPage <= m_pageCount && m_pageJumpCallback) {
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

int GuiManager::findFontIndex(const std::string& fontName) const {
    const auto& fonts = m_fontManager.getAvailableFonts();
    for (int i = 0; i < (int)fonts.size(); i++) {
        if (fonts[i].displayName == fontName) {
            return i;
        }
    }
    return 0; // Return 0 as safe default
}

void GuiManager::showNumberPad() {
    m_showNumberPad = true;
    // Reset debounce timer
    m_lastButtonPressTime = 0;
}

void GuiManager::hideNumberPad() {
    m_showNumberPad = false;
    // Reset debounce timer
    m_lastButtonPressTime = 0;
}
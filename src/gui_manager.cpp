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

int GuiManager::findFontIndex(const std::string& fontName) const {
    const auto& fonts = m_fontManager.getAvailableFonts();
    for (int i = 0; i < (int)fonts.size(); i++) {
        if (fonts[i].displayName == fontName) {
            return i;
        }
    }
    return 0; // Return 0 as safe default
}
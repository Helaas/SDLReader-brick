// Headless regression check. Example build/run from the repo root on macOS:
// c++ -std=c++17 -DTRIMUI_PLATFORM -Iinclude -Iports/mac/nuklear-mac \
//   -Iports/mac/mupdf/include $(pkg-config --cflags sdl2) \
//   tests/ui_font_layout.cpp src/options_manager.cpp src/path_utils.cpp src/button_mapper.cpp \
//   -Lports/mac/mupdf/build/release -lmupdf -lmupdf-third \
//   -L/opt/homebrew/opt/libarchive/lib -larchive \
//   -L/opt/homebrew/opt/webp/lib -lwebp -lwebpdemux \
//   $(pkg-config --libs sdl2) -o /tmp/sdlreader-ui-test && /tmp/sdlreader-ui-test
// Linux can use its port's Nuklear/MuPDF paths and native library flags instead.
#include "button_mapper.h"
#include "options_manager.h"
#include "platform_constants.h"
#include <array>
#include <functional>
#include <iostream>
#include <string>
#include <vector>
#define private public
#include "gui_manager.h"
#undef private
#include "../src/gui_manager.cpp"
#include <cassert>

static bool hasText(nk_context* ctx, const char* expected)
{
    const nk_command* command;
    nk_foreach(command, ctx)
    {
        if (command->type != NK_COMMAND_TEXT) continue;
        const auto* text = reinterpret_cast<const nk_command_text*>(command);
        if (std::string(text->string, text->length) == expected) return true;
    }
    return false;
}

int main()
{
    const auto stateDir = std::filesystem::temp_directory_path() /
        ("sdlreader-ui-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(std::filesystem::create_directory(stateDir));
    setenv("SDL_READER_STATE_DIR", stateDir.c_str(), 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    auto* window = SDL_CreateWindow("UI check", 0, 0, 640, 480, SDL_WINDOW_HIDDEN);
    auto* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    assert(window && renderer);
    for (float size : {16.0f, 24.0f, 36.0f, 72.0f})
    {
        FontConfig config;
        config.uiFontSize = size;
        assert(OptionsManager().saveConfig(config));
        GuiManager gui;
        assert(gui.initialize(window, renderer));
        gui.newFrame();
        gui.endFrame();
        const char* description = "Font controls for EPUB/MOBI/TXT; others use embedded fonts.";
        if (nk_begin(gui.m_ctx, "Wrap check", nk_rect(0, 0, 640, 480), NK_WINDOW_NO_SCROLLBAR))
            wrappedLabel(gui.m_ctx, description, nk_rgb(255, 255, 255));
        nk_end(gui.m_ctx);
        std::string renderedDescription;
        const nk_command* command;
        nk_foreach(command, gui.m_ctx)
        {
            if (command->type != NK_COMMAND_TEXT) continue;
            const auto* text = reinterpret_cast<const nk_command_text*>(command);
            renderedDescription.append(text->string, text->length);
        }
        assert(renderedDescription == description);
        nk_clear(gui.m_ctx);
        gui.toggleFontMenu();
        for (auto widget : {GuiManager::WIDGET_FONT_SIZE_INPUT, GuiManager::WIDGET_ZOOM_STEP_INPUT})
        {
            gui.m_mainScreenFocusIndex = widget;
            for (int frame = 0; frame < 3; ++frame)
            {
                gui.newFrame();
                gui.endFrame();
                gui.requestFocusScroll();
                gui.renderFontMenu();
                if (frame == 2)
                {
                    const char* expected = widget == GuiManager::WIDGET_FONT_SIZE_INPUT ? "12" : "10";
                    assert(hasText(gui.m_ctx, expected));
                    const auto& bounds = gui.m_widgetBounds[widget];
                    assert(bounds.h >= size + 2 * gui.m_ctx->style.edit.padding.y);
                }
                nk_clear(gui.m_ctx);
            }
        }
        gui.showNumberPad();
        gui.m_numberPadSelectedRow = 4;
        gui.m_numberPadSelectedCol = 0;
        for (int frame = 0; frame < 3; ++frame)
        {
            gui.newFrame();
            gui.endFrame();
            gui.renderNumberPad();
            if (frame == 2) assert(hasText(gui.m_ctx, "Go"));
            nk_clear(gui.m_ctx);
        }
        std::cout << "UI font size " << size << ": passed\n";
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::filesystem::remove_all(stateDir);
}

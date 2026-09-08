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
    // Resize the same window/GUI: proportions must follow the Brick reference.
    FontConfig referenceConfig;
    referenceConfig.uiFontSize = 36.0f;
    assert(OptionsManager().saveConfig(referenceConfig));
    {
        GuiManager gui;
        assert(gui.initialize(window, renderer));
        SDL_ScaleMode fontFiltering = SDL_ScaleModeNearest;
        assert(SDL_GetTextureScaleMode(sdl.ogl.font_tex, &fontFiltering) == 0);
        assert(fontFiltering == SDL_ScaleModeLinear);
        gui.toggleFontMenu();
        for (const auto dimensions : {std::array<int, 2>{1024, 768}, {640, 480}, {720, 480}, {1280, 720}})
        {
            SDL_SetWindowSize(window, dimensions[0], dimensions[1]);
            const float scale = settingsScale(window);
            assert(std::fabs(scale - std::min(dimensions[0], dimensions[1]) / 768.0f) < 0.001f);
            for (int frame = 0; frame < 3; ++frame)
            {
                gui.newFrame();
                SDL_SetRenderDrawColor(renderer, 12, 12, 12, 255);
                SDL_RenderClear(renderer);
                gui.render();
            }
            float scaleX = 0.0f, scaleY = 0.0f;
            SDL_RenderGetScale(renderer, &scaleX, &scaleY);
            assert(SDL_GetTextureScaleMode(sdl.ogl.font_tex, &fontFiltering) == 0);
            assert(fontFiltering == SDL_ScaleModeLinear);
            assert(scaleX == 1.0f && scaleY == 1.0f);
            auto* pixels = SDL_CreateRGBSurfaceWithFormat(0, dimensions[0], dimensions[1], 32, SDL_PIXELFORMAT_RGBA32);
            assert(pixels);
            assert(SDL_RenderReadPixels(renderer, nullptr, pixels->format->format, pixels->pixels, pixels->pitch) == 0);
            int left = dimensions[0], right = -1, top = dimensions[1], bottom = -1;
            for (int y = 0; y < dimensions[1]; ++y)
            {
                const auto* row = static_cast<const unsigned char*>(pixels->pixels) + y * pixels->pitch;
                for (int x = 0; x < dimensions[0]; ++x)
                    if (row[x * 4] != 12 || row[x * 4 + 1] != 12 || row[x * 4 + 2] != 12)
                    {
                        left = std::min(left, x); right = std::max(right, x);
                        top = std::min(top, y); bottom = std::max(bottom, y);
                    }
            }
            const float expectedWidth = std::min(dimensions[0] - 24.0f * scale,
                std::max(900.0f * scale, dimensions[0] * (680.0f / 1024.0f)));
            // The window border extends outside its bounds; allow pixel rounding.
            const float tolerance = 2.0f * gui.m_ctx->style.window.border * scale + 1.0f;
            assert(std::fabs(left - (dimensions[0] - expectedWidth) / 2.0f) <= tolerance);
            assert(std::fabs((right - left + 1) - expectedWidth) <= tolerance);
            assert(std::fabs(top - 12.0f * scale) <= tolerance);
            assert(std::fabs((bottom - top + 1) - 744.0f * scale) <= tolerance);
            if (const char* output = std::getenv("SDL_READER_TEST_CAPTURES"))
            {
                const std::string path = std::string(output) + "/settings-" + std::to_string(dimensions[0]) + "x" + std::to_string(dimensions[1]) + ".bmp";
                assert(SDL_SaveBMP(pixels, path.c_str()) == 0);
            }
            SDL_FreeSurface(pixels);
            gui.newFrame();
            SDL_Event motion{};
            motion.type = SDL_MOUSEMOTION;
            motion.motion.x = static_cast<int>(200 * scale);
            motion.motion.y = static_cast<int>(160 * scale);
            gui.handleEvent(motion);
            assert(std::fabs(gui.m_ctx->input.mouse.pos.x - 200) <= 1.0f);
            assert(std::fabs(gui.m_ctx->input.mouse.pos.y - 160) <= 1.0f);
            gui.endFrame();
            nk_clear(gui.m_ctx);
            for (auto widget : {GuiManager::WIDGET_FILE_BROWSER_IMAGES_INFO_BUTTON,
                                GuiManager::WIDGET_EDGE_TURN_HOLD_DURATION_INFO_BUTTON,
                                GuiManager::WIDGET_EDGE_PAGE_TURNS_MODE_INFO_BUTTON,
                                GuiManager::WIDGET_KEEP_PANNING_INFO_BUTTON,
                                GuiManager::WIDGET_MINIMAP_INFO_BUTTON,
                                GuiManager::WIDGET_PAGE_INDICATOR_INFO_BUTTON,
                                GuiManager::WIDGET_ZOOM_OVERLAY_INFO_BUTTON})
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
                        struct nk_rect clip{};
                        int tooltipLines = 0;
                        const nk_command* command;
                        nk_foreach(command, gui.m_ctx)
                        {
                            if (command->type == NK_COMMAND_SCISSOR)
                            {
                                const auto* scissor = reinterpret_cast<const nk_command_scissor*>(command);
                                clip = nk_rect(scissor->x, scissor->y, scissor->w, scissor->h);
                            }
                            if (command->type != NK_COMMAND_TEXT) continue;
                            const auto* text = reinterpret_cast<const nk_command_text*>(command);
                            if (text->foreground.r != 230 || text->foreground.g != 230 || text->foreground.b != 230) continue;
                            ++tooltipLines;
                            const float width = text->font->width(text->font->userdata, text->height, text->string, text->length);
                            assert(text->x >= clip.x);
                            assert(text->x + width <= clip.x + clip.w);
                            if (!(text->y >= clip.y && text->y + text->height <= clip.y + clip.h))
                                std::cerr << "Clipped tooltip widget=" << widget << " text=" << std::string(text->string, text->length) << " y=" << text->y << " height=" << text->height << " clip=" << clip.y << "," << clip.h << "\n";
                            assert(text->y >= clip.y && text->y + text->height <= clip.y + clip.h);
                        }
                        assert(tooltipLines > 0);
                    }
                    nk_clear(gui.m_ctx);
                }
            }
            std::cout << "Settings scaling " << dimensions[0] << "x" << dimensions[1] << ": passed\n";
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::filesystem::remove_all(stateDir);
}

// Linux/SDL2 regression check; synthesizes events without opening input devices.
// c++ -std=c++17 -DTRIMUI_PLATFORM -Iinclude $(sdl2-config --cflags) \
//   tests/h700_face_buttons.cpp src/button_mapper.cpp $(sdl2-config --libs) \
//   -o /tmp/h700-face-buttons && /tmp/h700-face-buttons
#include "button_mapper.h"
#include "../src/h700_input.cpp"
#include <array>
#include <cassert>
#include <iostream>

int main()
{
    setenv("PLATFORM", "h700", 1);
    assert(SDL_Init(SDL_INIT_EVENTS) == 0);
    ButtonMapper mapper;
    struct Mapping { int code; SDL_GameControllerButton button; LogicalButton action; };
    for (auto mapping : std::array<Mapping, 4>{{
             {304, SDL_CONTROLLER_BUTTON_B, LogicalButton::Accept},
             {305, SDL_CONTROLLER_BUTTON_A, LogicalButton::Cancel},
             {307, SDL_CONTROLLER_BUTTON_Y, LogicalButton::Special}, // Physical X: zoom / browser toggle
             {306, SDL_CONTROLLER_BUTTON_X, LogicalButton::Alternate}}}) // Physical Y: rotate / settings apply
    {
        for (int value : {1, 0, 2})
        {
            input_event key{};
            key.type = EV_KEY;
            key.code = mapping.code;
            key.value = value;
            handleKey(key);
            SDL_Event event{};
            const int received = SDL_PeepEvents(&event, 1, SDL_GETEVENT,
                                                SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLERBUTTONUP);
            assert(received == (value == 2 ? 0 : 1)); // Ignore kernel repeats.
            if (!received) continue;
            assert(event.type == (value ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP));
            assert(event.cbutton.which == H700_INPUT_INSTANCE_ID);
            assert(event.cbutton.button == mapping.button);
            assert(event.cbutton.state == (value ? SDL_PRESSED : SDL_RELEASED));
            assert(mapper.mapButton(mapping.button) == mapping.action);
        }
    }
    SDL_Quit();
    std::cout << "H700 A/B/X/Y press, release, repeats and logical actions: passed\n";
}

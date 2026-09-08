#include "h700_input.h"
#include "platform_runtime.h"

#ifdef __linux__

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
int inputFd = -1;
Uint32 lastScan = 0;
int hatX = 0;
int hatY = 0;

void pushButton(SDL_GameControllerButton button, bool pressed)
{
    SDL_Event event{};
    event.type = pressed ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
    event.cbutton.which = H700_INPUT_INSTANCE_ID;
    event.cbutton.button = static_cast<Uint8>(button);
    event.cbutton.state = pressed ? SDL_PRESSED : SDL_RELEASED;
    SDL_PushEvent(&event);
}

void pushAxis(SDL_GameControllerAxis axis, Sint16 value)
{
    SDL_Event event{};
    event.type = SDL_CONTROLLERAXISMOTION;
    event.caxis.which = H700_INPUT_INSTANCE_ID;
    event.caxis.axis = static_cast<Uint8>(axis);
    event.caxis.value = value;
    SDL_PushEvent(&event);
}

void updateHat(int& previous, int value,
               SDL_GameControllerButton negative,
               SDL_GameControllerButton positive)
{
    if (previous < 0 && value >= 0) pushButton(negative, false);
    if (previous > 0 && value <= 0) pushButton(positive, false);
    if (previous >= 0 && value < 0) pushButton(negative, true);
    if (previous <= 0 && value > 0) pushButton(positive, true);
    previous = value;
}

void openBuiltInPad()
{
    if (inputFd >= 0 || !isH700Platform()) return;

    for (int i = 0; i < 16; ++i)
    {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;

        char name[256]{};
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 &&
            std::strcmp(name, "ANBERNIC-keys") == 0)
        {
            inputFd = fd;
            std::cout << "H700 input: opened " << path << " (" << name << ")" << std::endl;
            return;
        }
        close(fd);
    }
}

void handleKey(const input_event& event)
{
    if (event.value < 0 || event.value > 1) return;
    const bool pressed = event.value != 0;

    switch (event.code)
    {
    case KEY_UP:     pushButton(SDL_CONTROLLER_BUTTON_DPAD_UP, pressed); break;
    case KEY_DOWN:   pushButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN, pressed); break;
    case KEY_LEFT:   pushButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT, pressed); break;
    case KEY_RIGHT:  pushButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT, pressed); break;
    case 304: pushButton(SDL_CONTROLLER_BUTTON_B, pressed); break; // A
    case 305: pushButton(SDL_CONTROLLER_BUTTON_A, pressed); break; // B
    case 307: pushButton(SDL_CONTROLLER_BUTTON_Y, pressed); break; // Physical X
    case 306: pushButton(SDL_CONTROLLER_BUTTON_X, pressed); break; // Physical Y
    case 308: pushButton(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, pressed); break;
    case 309: pushButton(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, pressed); break;
    case 314: pushAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, pressed ? 32767 : 0); break;
    case 315: pushAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, pressed ? 32767 : 0); break;
    case 311: pushButton(SDL_CONTROLLER_BUTTON_START, pressed); break;
    case 310: pushButton(SDL_CONTROLLER_BUTTON_BACK, pressed); break;
    case 312: pushButton(SDL_CONTROLLER_BUTTON_GUIDE, pressed); break;
    default: break;
    }
}

void handleAxis(const input_event& event)
{
    switch (event.code)
    {
    case ABS_HAT0X:
        updateHat(hatX, event.value, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
                  SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
        break;
    case ABS_HAT0Y:
        updateHat(hatY, event.value, SDL_CONTROLLER_BUTTON_DPAD_UP,
                  SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        break;
    case ABS_X:
        pushAxis(SDL_CONTROLLER_AXIS_LEFTX,
                 static_cast<Sint16>((event.value * 32767) / 4096));
        break;
    case ABS_Y:
        pushAxis(SDL_CONTROLLER_AXIS_LEFTY,
                 static_cast<Sint16>((event.value * 32767) / 4096));
        break;
    case ABS_RX:
        pushAxis(SDL_CONTROLLER_AXIS_RIGHTX,
                 static_cast<Sint16>((event.value * 32767) / 4096));
        break;
    case ABS_RY:
        pushAxis(SDL_CONTROLLER_AXIS_RIGHTY,
                 static_cast<Sint16>((event.value * 32767) / 4096));
        break;
    default: break;
    }
}
} // namespace

void initializeH700Input()
{
    lastScan = 0;
    openBuiltInPad();
}

void pollH700Input()
{
    if (!isH700Platform()) return;

    const Uint32 now = SDL_GetTicks();
    if (inputFd < 0 && (!lastScan || now - lastScan >= 2000))
    {
        lastScan = now;
        openBuiltInPad();
    }
    if (inputFd < 0) return;

    input_event event{};
    errno = 0;
    while (read(inputFd, &event, sizeof(event)) == sizeof(event))
    {
        if (event.type == EV_KEY) handleKey(event);
        else if (event.type == EV_ABS) handleAxis(event);
    }

    if (errno && errno != EAGAIN && errno != EWOULDBLOCK)
    {
        close(inputFd);
        inputFd = -1;
    }
}

void closeH700Input()
{
    if (inputFd >= 0) close(inputFd);
    inputFd = -1;
}

#else

void initializeH700Input() {}
void pollH700Input() {}
void closeH700Input() {}

#endif

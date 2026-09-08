#pragma once

#include <cstdlib>
#include <cstring>

inline const char* getNextUIPlatform()
{
    const char* platform = std::getenv("PLATFORM");
    return platform && platform[0] ? platform : "tg5040";
}

inline bool isNextUIPlatform(const char* platform)
{
    return std::strcmp(getNextUIPlatform(), platform) == 0;
}

inline bool isMy355Platform()
{
    return isNextUIPlatform("my355");
}

inline bool isH700Platform()
{
    return isNextUIPlatform("h700");
}

inline bool hasSupportedNextUIPlatform()
{
    const char* platform = std::getenv("PLATFORM");
    return platform &&
           (std::strcmp(platform, "tg5040") == 0 ||
            std::strcmp(platform, "tg5050") == 0 ||
            std::strcmp(platform, "my355") == 0 ||
            std::strcmp(platform, "h700") == 0);
}

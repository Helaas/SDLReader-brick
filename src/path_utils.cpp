#include "path_utils.h"

#include <cstdlib>
#include <filesystem>

namespace
{
std::filesystem::path resolveStateDirectory()
{
    if (const char* stateDir = std::getenv("SDL_READER_STATE_DIR"))
    {
        if (stateDir[0] != '\0')
        {
            return std::filesystem::path(stateDir);
        }
    }

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::filesystem::path(home);
        }
    }

    return std::filesystem::current_path();
}
} // namespace

std::string getDefaultLibraryRoot()
{
    if (const char* browseRoot = std::getenv("SDL_READER_DEFAULT_DIR"))
    {
        if (browseRoot[0] != '\0')
        {
            return std::string(browseRoot);
        }
    }

    if (const char* home = std::getenv("HOME"))
    {
        if (home[0] != '\0')
        {
            return std::string(home);
        }
    }

    return std::filesystem::current_path().string();
}

std::filesystem::path getStateDirectory()
{
    std::filesystem::path stateDir = resolveStateDirectory();
    std::error_code ec;
    std::filesystem::create_directories(stateDir, ec);
    return stateDir;
}

std::filesystem::path getDefaultConfigPath()
{
    return getStateDirectory() / "config.json";
}

std::filesystem::path getDefaultHistoryPath()
{
    return getStateDirectory() / "reading_history.json";
}

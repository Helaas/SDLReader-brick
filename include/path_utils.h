#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include <filesystem>
#include <string>

/**
 * @brief Retrieve the default library root directory used by the file browser.
 *        Respects the SDL_READER_DEFAULT_DIR environment variable. Falls back to HOME
 *        when the variable is not provided.
 */
std::string getDefaultLibraryRoot();

/**
 * @brief Returns the directory used to persist reader state (config/history).
 *        Ensures the directory exists. Respects SDL_READER_STATE_DIR, falling back to HOME.
 */
std::filesystem::path getStateDirectory();

/**
 * @brief Convenience accessors for default config / history file paths.
 */
std::filesystem::path getDefaultConfigPath();
std::filesystem::path getDefaultHistoryPath();

#endif // PATH_UTILS_H

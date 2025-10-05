#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <SDL.h>
#include <functional>
#include <string>
#include <vector>

// Forward declarations
struct ImGuiContext;

/**
 * @brief Simple file browser using ImGui
 */
class FileBrowser
{
public:
    FileBrowser();
    ~FileBrowser();

    /**
     * @brief Initialize the file browser with SDL
     * @param window SDL window
     * @param renderer SDL renderer
     * @param startPath Initial directory to browse (default: /mnt/SDCARD)
     * @return true if successful
     */
    bool initialize(SDL_Window* window, SDL_Renderer* renderer, const std::string& startPath = "/mnt/SDCARD");

    /**
     * @brief Run the file browser main loop
     * @return Path of selected file, or empty string if cancelled
     */
    std::string run();

    /**
     * @brief Cleanup resources
     */
    void cleanup();

    /**
     * @brief Get the last browsed directory
     */
    std::string getLastDirectory() const
    {
        return m_currentPath;
    }

private:
    struct FileEntry
    {
        std::string name;
        std::string fullPath;
        bool isDirectory;

        FileEntry(const std::string& n, const std::string& p, bool dir)
            : name(n), fullPath(p), isDirectory(dir)
        {
        }
    };

    SDL_Window* m_window;
    SDL_Renderer* m_renderer;
    bool m_initialized;
    bool m_running;
    std::string m_currentPath;
    std::vector<FileEntry> m_entries;
    int m_selectedIndex;
    std::string m_selectedFile;
    SDL_GameController* m_gameController;
    SDL_JoystickID m_gameControllerInstanceID;

    /**
     * @brief Scan directory and populate entries
     * @param path Directory path to scan
     * @return true if successful
     */
    bool scanDirectory(const std::string& path);

    /**
     * @brief Check if file has supported extension
     * @param filename Filename to check
     * @return true if file is supported document format
     */
    bool isSupportedFile(const std::string& filename) const;

    /**
     * @brief Render the file browser UI
     */
    void render();

    /**
     * @brief Handle SDL events
     * @param event SDL event to process
     */
    void handleEvent(const SDL_Event& event);

    /**
     * @brief Navigate to parent directory
     */
    void navigateUp();

    /**
     * @brief Navigate into selected directory or open selected file
     */
    void navigateInto();
};

#endif // FILE_BROWSER_H

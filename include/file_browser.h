#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <SDL.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef TG5040_PLATFORM
#include <memory>
class PowerHandler;
#endif

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

    struct SDLTextureDeleter
    {
        void operator()(SDL_Texture* texture) const
        {
            if (texture)
            {
                SDL_DestroyTexture(texture);
            }
        }
    };

    struct ThumbnailData
    {
        std::unique_ptr<SDL_Texture, SDLTextureDeleter> texture;
        int width{0};
        int height{0};
        bool failed{false};
        bool pending{false};
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

    bool m_thumbnailView{false};
    int m_gridColumns{1};
    int m_lastWindowWidth{0};
    int m_lastWindowHeight{0};
    static constexpr int THUMBNAIL_MAX_DIM = 200;

    std::unordered_map<std::string, ThumbnailData> m_thumbnailCache;

    // D-pad hold state for continuous scrolling
    bool m_dpadUpHeld;
    bool m_dpadDownHeld;
    Uint32 m_lastScrollTime;
    static constexpr Uint32 SCROLL_INITIAL_DELAY_MS = 100; // Initial delay before repeat starts
    static constexpr Uint32 SCROLL_REPEAT_DELAY_MS = 50;   // Delay between repeats

#ifdef TG5040_PLATFORM
    std::unique_ptr<PowerHandler> m_powerHandler;
    bool m_inFakeSleep{false};
#endif

    struct ThumbnailJobResult
    {
        std::string fullPath;
        std::vector<uint32_t> pixels;
        int width{0};
        int height{0};
        bool success{false};
    };

    std::thread m_thumbnailThread;
    std::mutex m_thumbnailMutex;
    std::condition_variable m_thumbnailCv;
    std::deque<FileEntry> m_thumbnailJobs;
    std::deque<ThumbnailJobResult> m_thumbnailResults;
    bool m_thumbnailThreadStop{false};
    bool m_thumbnailThreadRunning{false};

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
    void renderListView(int windowWidth, int windowHeight);
    void renderThumbnailView(int windowWidth, int windowHeight);

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

    void toggleViewMode();
    void moveSelectionVertical(int direction);
    void moveSelectionHorizontal(int direction);
    void clampSelection();
    ThumbnailData& getOrCreateThumbnail(const FileEntry& entry);
    bool generateThumbnail(const FileEntry& entry, ThumbnailData& data);
    bool buildDocumentThumbnailPixels(const FileEntry& entry, std::vector<uint32_t>& pixels, int& width, int& height);
    bool buildDirectoryThumbnailPixels(std::vector<uint32_t>& pixels, int& width, int& height);
    bool generateDirectoryThumbnail(const FileEntry& entry, ThumbnailData& data);
    void clearThumbnailCache();
    SDL_Texture* createTextureFromPixels(const std::vector<uint32_t>& pixels, int width, int height);
    SDL_Texture* createSolidTexture(int width, int height, SDL_Color color, Uint8 alpha = 255);
    void startThumbnailWorker();
    void stopThumbnailWorker();
    void enqueueThumbnailJob(const FileEntry& entry);
    void pumpThumbnailResults();
    void thumbnailWorkerLoop();
};

#endif // FILE_BROWSER_H

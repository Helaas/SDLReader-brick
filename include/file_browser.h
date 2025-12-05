#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <SDL.h>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class PowerHandler;
struct nk_context;

/**
 * @brief Simple file browser using Nuklear
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
     * @param startPath Initial directory to browse (empty string uses default library root)
     * @return true if successful
     */
    bool initialize(SDL_Window* window, SDL_Renderer* renderer, const std::string& startPath = std::string());

    /**
     * @brief Run the file browser main loop
     * @return Path of selected file, or empty string if cancelled
     */
    std::string run();

    /**
     * @brief Cleanup resources
     */
    void cleanup(bool preserveThumbnails = false);

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
        bool isParentLink;

        FileEntry(const std::string& n, const std::string& p, bool dir, bool parentLink = false)
            : name(n), fullPath(p), isDirectory(dir), isParentLink(parentLink)
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
    nk_context* m_ctx = nullptr;
    bool m_initialized;
    bool m_running;
    std::string m_defaultRoot;
    bool m_lockToDefaultRoot{false};
    std::string m_currentPath;
    std::vector<FileEntry> m_entries;
    int m_selectedIndex;
    std::string m_selectedFile;
    std::string m_restoreSelectionPath;
    bool m_restoreSelectionPending{false};
    SDL_GameController* m_gameController;
    SDL_JoystickID m_gameControllerInstanceID;

    bool m_thumbnailView{false};
    int m_gridColumns{1};
    int m_lastWindowWidth{0};
    int m_lastWindowHeight{0};
#ifdef TG5040_PLATFORM
    static constexpr int THUMBNAIL_MAX_DIM = 150;
#else
    static constexpr int THUMBNAIL_MAX_DIM = 200;
#endif

    std::unordered_map<std::string, ThumbnailData> m_thumbnailCache;
    static constexpr size_t MAX_CACHED_THUMBNAILS = 100;
    std::list<std::string> m_thumbnailUsage;
    std::unordered_map<std::string, std::list<std::string>::iterator> m_thumbnailUsageLookup;

    // D-pad hold state for continuous scrolling
    bool m_dpadUpHeld;
    bool m_dpadDownHeld;
    Uint32 m_lastScrollTime;
    bool m_waitingForInitialRepeat;
    bool m_leftHeld{false};
    bool m_rightHeld{false};
    Sint16 m_leftStickX{0};
    Sint16 m_leftStickY{0};
    Uint32 m_lastHorizontalScrollTime{0};
    bool m_waitingForInitialHorizontalRepeat{false};
    static constexpr Uint32 SCROLL_INITIAL_DELAY_MS = 100;     // Initial delay before repeat starts
    static constexpr Uint32 SCROLL_REPEAT_DELAY_MS = 50;       // Delay between repeats
    static constexpr Uint32 THUMBNAIL_SCROLL_DELAY_FACTOR = 2; // Slow down thumbnail view repeat speed

#ifdef TG5040_PLATFORM
    std::unique_ptr<PowerHandler> m_powerHandler;
    bool m_inFakeSleep{false};
    static constexpr Uint32 POWER_MESSAGE_DURATION_MS = 4000;
    std::string m_powerMessage;
    Uint32 m_powerMessageStart{0};
    Uint32 m_powerMessageEventType{0};
#endif

    // Scroll tracking for Nuklear list/thumbnail views (used by all platforms)
    float m_listScrollY{0.0f};
    float m_thumbnailScrollY{0.0f};
    int m_lastListEnsureIndex{-1};
    int m_lastThumbEnsureIndex{-1};
    bool m_pendingListEnsure{false};
    bool m_pendingThumbEnsure{false};
    bool m_scrollJustSet{false}; // Track when we just set scroll to avoid reading stale values
    float m_lastContentHeight{0.0f};

    struct ThumbnailJobResult
    {
        std::string fullPath;
        std::vector<uint32_t> pixels;
        int width{0};
        int height{0};
        bool success{false};
    };

    std::vector<std::thread> m_thumbnailThreads;
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
    void setupNuklearStyle();
    void renderListViewNuklear(float viewHeight, int windowWidth);
    void renderThumbnailViewNuklear(float viewHeight, int windowWidth);
#ifdef TG5040_PLATFORM
    void showPowerMessage(const std::string& message);
    void renderPowerMessageOverlay(float windowWidth, float windowHeight);
#endif
    void ensureSelectionVisible(float itemHeight, float viewHeight, float itemSpacing,
                                float& scrollY, int& lastEnsureIndex, int targetIndex, int totalItems);
    void resetSelectionScrollTargets();

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
    void pageJump(int direction);
    void pageJumpList(int direction);
    void pageJumpThumbnail(int direction);
    void jumpSelectionByLetter(int direction);
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
    void requestThumbnailShutdown();
    void clearPendingThumbnails();
    void recordThumbnailUsage(const std::string& path);
    void evictOldThumbnails();
    void cancelThumbnailJobsForPath(const std::string& path);
    void removeThumbnailEntry(const std::string& path);
    void tryRestoreSelection(const std::string& directoryPath);

    static bool s_lastThumbnailView;
};

#endif // FILE_BROWSER_H

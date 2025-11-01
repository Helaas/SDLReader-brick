#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct ReadingHistoryEntry
{
    std::string documentPath;
    int lastPage;
    long long lastAccessTime; // Unix timestamp
};

class ReadingHistoryManager
{
public:
    ReadingHistoryManager();
    ~ReadingHistoryManager();

    // Load history from file
    bool loadHistory(std::string historyFilePath = {});

    // Save history to file
    bool saveHistory(std::string historyFilePath = {});

    // Get the last page for a document (returns -1 if not found)
    int getLastPage(const std::string& documentPath) const;

    // Update the last page for a document
    void updateLastPage(const std::string& documentPath, int page);

    // Get all history entries sorted by last access time (most recent first)
    std::vector<ReadingHistoryEntry> getRecentDocuments() const;

private:
    static constexpr int MAX_HISTORY_ENTRIES = 50;

    std::unordered_map<std::string, ReadingHistoryEntry> m_history;
    mutable std::mutex m_mutex;
    std::string m_historyFilePath;

    // Remove oldest entries if we exceed the maximum
    void pruneOldestEntries();

    // Get current Unix timestamp
    static long long getCurrentTimestamp();
};

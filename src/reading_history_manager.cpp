#include "reading_history_manager.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>

ReadingHistoryManager::ReadingHistoryManager()
    : m_historyFilePath("./reading_history.json")
{
}

ReadingHistoryManager::~ReadingHistoryManager()
{
    // Auto-save on destruction
    saveHistory(m_historyFilePath);
}

long long ReadingHistoryManager::getCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
}

bool ReadingHistoryManager::loadHistory(const std::string& historyFilePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_historyFilePath = historyFilePath;

    std::ifstream file(historyFilePath);
    if (!file.is_open())
    {
        std::cout << "No reading history file found at " << historyFilePath << ", starting fresh" << std::endl;
        return false;
    }

    // Simple JSON parsing (similar to options_manager.cpp)
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    if (content.empty())
    {
        return false;
    }

    // Parse JSON array of history entries
    // Expected format: { "history": [ { "path": "...", "page": 5, "timestamp": 123456 }, ... ] }

    size_t historyArrayStart = content.find("\"history\"");
    if (historyArrayStart == std::string::npos)
    {
        return false;
    }

    size_t arrayStart = content.find('[', historyArrayStart);
    size_t arrayEnd = content.rfind(']');

    if (arrayStart == std::string::npos || arrayEnd == std::string::npos)
    {
        return false;
    }

    std::string arrayContent = content.substr(arrayStart + 1, arrayEnd - arrayStart - 1);

    // Parse each entry (simplified JSON parsing)
    size_t pos = 0;
    while (pos < arrayContent.length())
    {
        size_t entryStart = arrayContent.find('{', pos);
        if (entryStart == std::string::npos)
            break;

        size_t entryEnd = arrayContent.find('}', entryStart);
        if (entryEnd == std::string::npos)
            break;

        std::string entry = arrayContent.substr(entryStart, entryEnd - entryStart + 1);

        // Extract path
        std::string path;
        size_t pathStart = entry.find("\"path\"");
        if (pathStart != std::string::npos)
        {
            size_t colonPos = entry.find(':', pathStart);
            size_t quoteStart = entry.find('"', colonPos);
            size_t quoteEnd = entry.find('"', quoteStart + 1);
            if (quoteStart != std::string::npos && quoteEnd != std::string::npos)
            {
                path = entry.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
            }
        }

        // Extract page
        int page = 0;
        size_t pageStart = entry.find("\"page\"");
        if (pageStart != std::string::npos)
        {
            size_t colonPos = entry.find(':', pageStart);
            if (colonPos != std::string::npos)
            {
                std::string pageStr;
                for (size_t i = colonPos + 1; i < entry.length(); i++)
                {
                    if (std::isdigit(entry[i]) || entry[i] == '-')
                        pageStr += entry[i];
                    else if (!pageStr.empty())
                        break;
                }
                if (!pageStr.empty())
                    page = std::stoi(pageStr);
            }
        }

        // Extract timestamp
        long long timestamp = getCurrentTimestamp();
        size_t timestampStart = entry.find("\"timestamp\"");
        if (timestampStart != std::string::npos)
        {
            size_t colonPos = entry.find(':', timestampStart);
            if (colonPos != std::string::npos)
            {
                std::string timestampStr;
                for (size_t i = colonPos + 1; i < entry.length(); i++)
                {
                    if (std::isdigit(entry[i]))
                        timestampStr += entry[i];
                    else if (!timestampStr.empty())
                        break;
                }
                if (!timestampStr.empty())
                    timestamp = std::stoll(timestampStr);
            }
        }

        if (!path.empty())
        {
            ReadingHistoryEntry historyEntry;
            historyEntry.documentPath = path;
            historyEntry.lastPage = page;
            historyEntry.lastAccessTime = timestamp;
            m_history[path] = historyEntry;
        }

        pos = entryEnd + 1;
    }

    std::cout << "Loaded reading history: " << m_history.size() << " documents" << std::endl;
    return true;
}

bool ReadingHistoryManager::saveHistory(const std::string& historyFilePath)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::ofstream file(historyFilePath);
    if (!file.is_open())
    {
        std::cerr << "Failed to save reading history to " << historyFilePath << std::endl;
        return false;
    }

    // Write JSON
    file << "{\n";
    file << "  \"history\": [\n";

    // Get entries sorted by timestamp (most recent first) for better readability
    std::vector<ReadingHistoryEntry> entries;
    for (const auto& pair : m_history)
    {
        entries.push_back(pair.second);
    }
    std::sort(entries.begin(), entries.end(),
              [](const ReadingHistoryEntry& a, const ReadingHistoryEntry& b)
              {
                  return a.lastAccessTime > b.lastAccessTime;
              });

    for (size_t i = 0; i < entries.size(); i++)
    {
        const auto& entry = entries[i];
        file << "    {\n";
        file << "      \"path\": \"" << entry.documentPath << "\",\n";
        file << "      \"page\": " << entry.lastPage << ",\n";
        file << "      \"timestamp\": " << entry.lastAccessTime << "\n";
        file << "    }";
        if (i < entries.size() - 1)
            file << ",";
        file << "\n";
    }

    file << "  ]\n";
    file << "}\n";

    std::cout << "Saved reading history: " << entries.size() << " documents" << std::endl;
    return true;
}

int ReadingHistoryManager::getLastPage(const std::string& documentPath) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_history.find(documentPath);
    if (it != m_history.end())
    {
        return it->second.lastPage;
    }

    return -1; // Not found
}

void ReadingHistoryManager::updateLastPage(const std::string& documentPath, int page)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    ReadingHistoryEntry entry;
    entry.documentPath = documentPath;
    entry.lastPage = page;
    entry.lastAccessTime = getCurrentTimestamp();

    m_history[documentPath] = entry;

    // Prune if we exceed the maximum
    if (m_history.size() > MAX_HISTORY_ENTRIES)
    {
        pruneOldestEntries();
    }
}

std::vector<ReadingHistoryEntry> ReadingHistoryManager::getRecentDocuments() const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ReadingHistoryEntry> entries;
    for (const auto& pair : m_history)
    {
        entries.push_back(pair.second);
    }

    // Sort by last access time (most recent first)
    std::sort(entries.begin(), entries.end(),
              [](const ReadingHistoryEntry& a, const ReadingHistoryEntry& b)
              {
                  return a.lastAccessTime > b.lastAccessTime;
              });

    return entries;
}

void ReadingHistoryManager::pruneOldestEntries()
{
    // Already locked by caller

    if (m_history.size() <= MAX_HISTORY_ENTRIES)
    {
        return;
    }

    // Get all entries sorted by timestamp
    std::vector<std::pair<std::string, long long>> entries;
    for (const auto& pair : m_history)
    {
        entries.push_back({pair.first, pair.second.lastAccessTime});
    }

    // Sort by timestamp (oldest first)
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b)
              {
                  return a.second < b.second;
              });

    // Remove oldest entries until we're at the limit
    size_t toRemove = m_history.size() - MAX_HISTORY_ENTRIES;
    for (size_t i = 0; i < toRemove && i < entries.size(); i++)
    {
        m_history.erase(entries[i].first);
        std::cout << "Pruned old history entry: " << entries[i].first << std::endl;
    }
}

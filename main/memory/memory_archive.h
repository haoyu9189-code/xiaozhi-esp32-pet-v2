#ifndef MEMORY_ARCHIVE_H
#define MEMORY_ARCHIVE_H

#include "memory_types.h"
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>

// Archived item structure for retrieval (Stage 2)
struct ArchivedItem {
    char timestamp[20];    // ISO 8601 format: YYYY-MM-DDTHH:MM:SS
    char type[16];         // fact, moment, event, etc.
    char content[256];     // JSON serialized content
};

/**
 * Memory Archive Manager
 *
 * Manages long-term memory storage in SPIFFS (Asset partition)
 * - Stage 1: Archive old memories to JSONL files
 * - Stage 2: Recall/search archived memories (future)
 */
class MemoryArchive {
public:
    static MemoryArchive& GetInstance();

    // Initialization - mount SPIFFS and create directory
    bool Init();

    // Archive operations (Stage 1)
    bool ArchiveFacts(const std::vector<Fact>& facts);
    bool ArchiveMoments(const std::vector<SpecialMoment>& moments);
    bool ArchiveEvents(const std::vector<Event>& events);

    // Statistics
    size_t GetArchiveCount(const char* type);
    bool IsInitialized() const { return initialized_; }

    // Recall operations (Stage 2)
    std::vector<ArchivedItem> RecallByTimeRange(const char* type,
        const char* start_date, const char* end_date, int limit = 10);
    std::vector<ArchivedItem> RecallByKeyword(const char* type,
        const char* keyword, int limit = 10);
    std::vector<ArchivedItem> RecallRecent(const char* type, int limit = 10);

private:
    MemoryArchive() = default;
    ~MemoryArchive();

    // File operations
    bool AppendToFile(const char* filename, const char* json_line);
    bool CreateDirectoryIfNotExists();

    // JSON serialization helpers
    std::string SerializeFact(const Fact& fact);
    std::string SerializeMoment(const SpecialMoment& moment);
    std::string SerializeEvent(const Event& event);

    // Recall helpers (Stage 2)
    const char* GetArchiveFilename(const char* type);
    bool ParseArchivedItem(const char* json_line, ArchivedItem& item);
    bool MatchesTimeRange(const char* timestamp, const char* start_date, const char* end_date);
    bool ContainsKeyword(const char* content, const char* keyword);

    std::mutex mutex_;
    bool initialized_ = false;
    bool spiffs_mounted_ = false;

    // Archive file paths
    static constexpr const char* ARCHIVE_BASE_PATH = "/spiffs/memory";
    static constexpr const char* FACTS_ARCHIVE = "/spiffs/memory/facts_archive.jsonl";
    static constexpr const char* MOMENTS_ARCHIVE = "/spiffs/memory/moments_archive.jsonl";
    static constexpr const char* EVENTS_ARCHIVE = "/spiffs/memory/events_archive.jsonl";
};

#endif // MEMORY_ARCHIVE_H

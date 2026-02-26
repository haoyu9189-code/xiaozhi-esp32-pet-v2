#ifndef PENDING_MEMORY_H
#define PENDING_MEMORY_H

#include "memory_types.h"
#include <vector>
#include <mutex>
#include <string>
#include <nvs_flash.h>

// Maximum pending items
#define MAX_PENDING_ITEMS 20

// Confirmation threshold (times to confirm)
#define CONFIRM_THRESHOLD 2

// High confidence - skip confirmation and save directly
#define HIGH_CONFIDENCE_THRESHOLD 5

// Expiry time in seconds (7 days)
#define PENDING_EXPIRY_SECONDS (7 * 24 * 60 * 60)

// Pending item structure (~108 bytes)
struct PendingItem {
    char magic[4];              // "XZPD"
    ExtractedType type;         // Memory type
    char key[32];               // Key: "identity:name", "like:xxx"
    char value[64];             // Value content
    uint32_t first_seen;        // First seen timestamp
    uint8_t count;              // Occurrence count
    uint8_t reserved[3];        // Alignment padding
};

class PendingMemory {
public:
    static PendingMemory& GetInstance();

    // Initialize and load from NVS
    bool Init();

    // Add or confirm a memory
    // Returns true if confirmed (reached threshold)
    bool AddOrConfirm(const ExtractedMemory& memory);

    // Clean expired items (older than 7 days)
    void CleanExpired();

    // Get pending count
    int GetCount() const { return pending_.size(); }

    // Get all pending items (for debugging)
    const std::vector<PendingItem>& GetPending() const { return pending_; }

    // Force save to NVS
    void Save();

private:
    PendingMemory() = default;
    ~PendingMemory();

    std::vector<PendingItem> pending_;
    nvs_handle_t nvs_handle_ = 0;
    std::mutex mutex_;
    bool dirty_ = false;
    bool initialized_ = false;

    // Generate key from extracted memory
    std::string MakeKey(const ExtractedMemory& mem);

    // Check if values match (for same key)
    bool IsSameValue(const PendingItem& item, const ExtractedMemory& mem);

    // Find existing item by key
    int FindByKey(const std::string& key);

    // NVS operations
    void LoadFromNvs();
    void SaveToNvs();
};

#endif // PENDING_MEMORY_H

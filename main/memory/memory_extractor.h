#ifndef MEMORY_EXTRACTOR_H
#define MEMORY_EXTRACTOR_H

#include "memory_types.h"
#include <string>
#include <vector>

class MemoryExtractor {
public:
    // Quick check if text contains extractable patterns
    static bool HasPatterns(const std::string& text);

    // Extract memories from user text
    static std::vector<ExtractedMemory> Extract(const std::string& user_text);

    // Apply extracted memories to storage
    static int Apply(const std::vector<ExtractedMemory>& memories);

private:
    // Check if pattern is negated
    static bool IsNegated(const std::string& text, size_t pattern_pos);

    // Check if text is a question
    static bool IsQuestion(const std::string& text);

    // Check if text is hypothetical
    static bool IsHypothetical(const std::string& text);

    // Extract content after pattern
    static std::string ExtractContent(const std::string& text, size_t start_pos,
                                       size_t max_len = 32);

    // Extract identity info (name, age, location, etc.)
    static void ExtractIdentity(const std::string& text,
                                 std::vector<ExtractedMemory>& memories);

    // Extract preferences (likes/dislikes)
    static void ExtractPreferences(const std::string& text,
                                    std::vector<ExtractedMemory>& memories);

    // Extract family members
    static void ExtractFamily(const std::string& text,
                               std::vector<ExtractedMemory>& memories);

    // Extract events
    static void ExtractEvents(const std::string& text,
                               std::vector<ExtractedMemory>& memories);

    // Extract facts
    static void ExtractFacts(const std::string& text,
                              std::vector<ExtractedMemory>& memories);
};

#endif // MEMORY_EXTRACTOR_H

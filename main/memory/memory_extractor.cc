#include "memory_extractor.h"
#include "memory_storage.h"
#include <esp_log.h>
#include <cstring>
#include <algorithm>

#define TAG "MemExtractor"

namespace {

// Negation words (Chinese)
const char* NEGATION_WORDS[] = {
    "不是", "不叫", "没有", "不", "别", "没",
    "非", "未", "不再", "不想", "不要", "并非",
    "绝非", "从不", "不会"
};
const int NEGATION_COUNT = sizeof(NEGATION_WORDS) / sizeof(NEGATION_WORDS[0]);

// Question markers
const char* QUESTION_MARKERS[] = {
    "吗", "？", "?", "什么", "谁", "哪", "怎么",
    "为什么", "多少", "几"
};
const int QUESTION_COUNT = sizeof(QUESTION_MARKERS) / sizeof(QUESTION_MARKERS[0]);

// Hypothetical markers
const char* HYPOTHETICAL_MARKERS[] = {
    "如果", "假如", "要是", "假设", "倘若", "万一"
};
const int HYPOTHETICAL_COUNT = sizeof(HYPOTHETICAL_MARKERS) / sizeof(HYPOTHETICAL_MARKERS[0]);

// Identity patterns
struct IdentityPattern {
    const char* pattern;
    const char* category;  // name, age, gender, location
    int confidence;
};

const IdentityPattern IDENTITY_PATTERNS[] = {
    {"我叫", "name", 5},
    {"我的名字是", "name", 5},
    {"我名叫", "name", 5},
    {"叫我", "name", 4},
    {"我是", "name", 3},  // Lower confidence - could be other things
    {"我今年", "age", 5},
    {"我的年龄是", "age", 5},
    {"岁了", "age", 4},
    {"我住在", "location", 4},
    {"我在", "location", 3},
    {"我来自", "location", 4},
    {"我是男", "gender", 4},
    {"我是女", "gender", 4},
};
const int IDENTITY_PATTERN_COUNT = sizeof(IDENTITY_PATTERNS) / sizeof(IDENTITY_PATTERNS[0]);

// Preference patterns
struct PreferencePattern {
    const char* pattern;
    bool is_like;
    int confidence;
};

const PreferencePattern PREFERENCE_PATTERNS[] = {
    {"我喜欢", true, 5},
    {"我爱", true, 5},
    {"我最喜欢", true, 5},
    {"我超喜欢", true, 5},
    {"我特别喜欢", true, 5},
    {"我比较喜欢", true, 4},
    {"我讨厌", false, 5},
    {"我不喜欢", false, 5},
    {"我恨", false, 5},
    {"我最讨厌", false, 5},
    {"我不爱", false, 4},
};
const int PREFERENCE_PATTERN_COUNT = sizeof(PREFERENCE_PATTERNS) / sizeof(PREFERENCE_PATTERNS[0]);

// Family relation mapping
struct RelationMapping {
    const char* keyword;
    const char* relation;
};

const RelationMapping RELATION_MAPPINGS[] = {
    {"爸爸", "父亲"}, {"父亲", "父亲"}, {"老爸", "父亲"}, {"爹", "父亲"},
    {"妈妈", "母亲"}, {"母亲", "母亲"}, {"老妈", "母亲"}, {"娘", "母亲"},
    {"爷爷", "爷爷"}, {"奶奶", "奶奶"},
    {"外公", "外公"}, {"外婆", "外婆"}, {"姥爷", "外公"}, {"姥姥", "外婆"},
    {"哥哥", "哥哥"}, {"弟弟", "弟弟"}, {"姐姐", "姐姐"}, {"妹妹", "妹妹"},
    {"老公", "丈夫"}, {"丈夫", "丈夫"}, {"老婆", "妻子"}, {"妻子", "妻子"},
    {"儿子", "儿子"}, {"女儿", "女儿"},
    {"朋友", "朋友"}, {"同事", "同事"}, {"同学", "同学"},
    {"宠物", "宠物"}, {"狗狗", "宠物"}, {"猫咪", "宠物"}, {"猫", "宠物"}, {"狗", "宠物"},
};
const int RELATION_COUNT = sizeof(RELATION_MAPPINGS) / sizeof(RELATION_MAPPINGS[0]);

// Fact patterns
const char* FACT_PATTERNS[] = {
    "我有", "我会", "我能", "我学", "我正在",
    "我在学", "我喜欢做", "我经常", "我每天"
};
const int FACT_PATTERN_COUNT = sizeof(FACT_PATTERNS) / sizeof(FACT_PATTERNS[0]);

}  // namespace

bool MemoryExtractor::HasPatterns(const std::string& text) {
    // Quick check for common patterns
    const char* quick_patterns[] = {
        "我叫", "我是", "我的", "我喜欢", "我讨厌", "我爱",
        "我有", "我住", "爸爸", "妈妈", "我今年"
    };

    for (const auto& pattern : quick_patterns) {
        if (text.find(pattern) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool MemoryExtractor::IsNegated(const std::string& text, size_t pattern_pos) {
    if (pattern_pos == 0) return false;

    // Check 6-12 bytes before pattern (2-4 Chinese chars)
    size_t check_start = (pattern_pos > 12) ? pattern_pos - 12 : 0;
    std::string prefix = text.substr(check_start, pattern_pos - check_start);

    for (int i = 0; i < NEGATION_COUNT; i++) {
        if (prefix.find(NEGATION_WORDS[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool MemoryExtractor::IsQuestion(const std::string& text) {
    for (int i = 0; i < QUESTION_COUNT; i++) {
        if (text.find(QUESTION_MARKERS[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool MemoryExtractor::IsHypothetical(const std::string& text) {
    for (int i = 0; i < HYPOTHETICAL_COUNT; i++) {
        if (text.find(HYPOTHETICAL_MARKERS[i]) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string MemoryExtractor::ExtractContent(const std::string& text, size_t start_pos,
                                             size_t max_len) {
    if (start_pos >= text.length()) return "";

    // Find end position (punctuation or max length)
    const char* terminators[] = {
        "，", "。", "！", "？", "、", ",", ".", "!", "?",
        "吗", "呢", "吧", "啊", "哦", "嘛"
    };

    size_t end_pos = std::min(start_pos + max_len * 3, text.length());  // *3 for UTF-8

    for (const auto& term : terminators) {
        size_t pos = text.find(term, start_pos);
        if (pos != std::string::npos && pos < end_pos) {
            end_pos = pos;
        }
    }

    std::string content = text.substr(start_pos, end_pos - start_pos);

    // Trim whitespace
    while (!content.empty() && (content.back() == ' ' || content.back() == '\t')) {
        content.pop_back();
    }
    while (!content.empty() && (content.front() == ' ' || content.front() == '\t')) {
        content.erase(0, 1);
    }

    return content;
}

void MemoryExtractor::ExtractIdentity(const std::string& text,
                                       std::vector<ExtractedMemory>& memories) {
    for (int i = 0; i < IDENTITY_PATTERN_COUNT; i++) {
        const auto& pattern = IDENTITY_PATTERNS[i];
        size_t pos = text.find(pattern.pattern);
        if (pos == std::string::npos) continue;

        // Check negation
        if (IsNegated(text, pos)) continue;

        // Extract content
        size_t content_start = pos + strlen(pattern.pattern);
        std::string content = ExtractContent(text, content_start, 24);

        if (content.empty()) continue;

        // For age, try to extract number
        if (strcmp(pattern.category, "age") == 0) {
            int age = 0;
            for (char c : content) {
                if (c >= '0' && c <= '9') {
                    age = age * 10 + (c - '0');
                }
            }
            if (age > 0 && age < 150) {
                char age_str[8];
                snprintf(age_str, sizeof(age_str), "%d", age);
                content = age_str;
            } else {
                continue;  // Invalid age
            }
        }

        // For gender
        if (strcmp(pattern.category, "gender") == 0) {
            if (text.find("男") != std::string::npos) {
                content = "male";
            } else if (text.find("女") != std::string::npos) {
                content = "female";
            } else {
                continue;
            }
        }

        ExtractedMemory mem;
        mem.type = ExtractedType::IDENTITY;
        strncpy(mem.category, pattern.category, sizeof(mem.category) - 1);
        strncpy(mem.content, content.c_str(), sizeof(mem.content) - 1);
        mem.confidence = pattern.confidence;

        memories.push_back(mem);
        ESP_LOGI(TAG, "Extracted identity: %s = %s (conf=%d)",
                 pattern.category, content.c_str(), pattern.confidence);
    }
}

void MemoryExtractor::ExtractPreferences(const std::string& text,
                                          std::vector<ExtractedMemory>& memories) {
    for (int i = 0; i < PREFERENCE_PATTERN_COUNT; i++) {
        const auto& pattern = PREFERENCE_PATTERNS[i];
        size_t pos = text.find(pattern.pattern);
        if (pos == std::string::npos) continue;

        // For negative patterns, don't check negation
        if (pattern.is_like && IsNegated(text, pos)) continue;

        size_t content_start = pos + strlen(pattern.pattern);
        std::string content = ExtractContent(text, content_start, 20);

        if (content.empty()) continue;

        ExtractedMemory mem;
        mem.type = ExtractedType::PREFERENCE;
        strncpy(mem.category, pattern.is_like ? "like" : "dislike", sizeof(mem.category) - 1);
        strncpy(mem.content, content.c_str(), sizeof(mem.content) - 1);
        mem.confidence = pattern.confidence;

        memories.push_back(mem);
        ESP_LOGI(TAG, "Extracted preference: %s %s (conf=%d)",
                 pattern.is_like ? "likes" : "dislikes", content.c_str(), pattern.confidence);
    }
}

void MemoryExtractor::ExtractFamily(const std::string& text,
                                     std::vector<ExtractedMemory>& memories) {
    // Patterns: "我的XX叫YY", "我XX叫YY", "XX叫YY"
    const char* family_patterns[] = {
        "我的", "我"
    };

    for (int i = 0; i < RELATION_COUNT; i++) {
        const auto& rel = RELATION_MAPPINGS[i];

        // Try different patterns
        for (const auto& prefix : family_patterns) {
            std::string pattern = std::string(prefix) + rel.keyword + "叫";
            size_t pos = text.find(pattern);
            if (pos == std::string::npos) {
                pattern = std::string(prefix) + rel.keyword + "是";
                pos = text.find(pattern);
            }

            if (pos != std::string::npos) {
                if (IsNegated(text, pos)) continue;

                size_t content_start = pos + pattern.length();
                std::string name = ExtractContent(text, content_start, 16);

                if (name.empty()) continue;

                ExtractedMemory mem;
                mem.type = ExtractedType::FAMILY;
                strncpy(mem.category, rel.relation, sizeof(mem.category) - 1);
                strncpy(mem.content, name.c_str(), sizeof(mem.content) - 1);
                mem.confidence = 4;

                memories.push_back(mem);
                ESP_LOGI(TAG, "Extracted family: %s = %s", rel.relation, name.c_str());
                break;
            }
        }
    }
}

void MemoryExtractor::ExtractEvents(const std::string& text,
                                     std::vector<ExtractedMemory>& memories) {
    // Event patterns
    const char* event_patterns[] = {
        "生日", "纪念日", "考试", "面试", "约会", "会议",
        "旅行", "出差", "婚礼", "聚会"
    };

    for (const auto& event_type : event_patterns) {
        if (text.find(event_type) != std::string::npos) {
            // Look for date patterns nearby
            // Simple: just note the event type for now
            ExtractedMemory mem;
            mem.type = ExtractedType::EVENT;
            strncpy(mem.category, "event", sizeof(mem.category) - 1);
            strncpy(mem.content, event_type, sizeof(mem.content) - 1);
            mem.confidence = 3;

            memories.push_back(mem);
            break;  // Only one event per message
        }
    }
}

void MemoryExtractor::ExtractFacts(const std::string& text,
                                    std::vector<ExtractedMemory>& memories) {
    for (int i = 0; i < FACT_PATTERN_COUNT; i++) {
        size_t pos = text.find(FACT_PATTERNS[i]);
        if (pos == std::string::npos) continue;

        if (IsNegated(text, pos)) continue;

        size_t content_start = pos + strlen(FACT_PATTERNS[i]);
        std::string content = ExtractContent(text, content_start, 40);

        if (content.empty() || content.length() < 2) continue;

        // Build full fact sentence
        std::string full_fact = std::string(FACT_PATTERNS[i]) + content;

        ExtractedMemory mem;
        mem.type = ExtractedType::FACT;
        strncpy(mem.category, "fact", sizeof(mem.category) - 1);
        strncpy(mem.content, full_fact.c_str(), sizeof(mem.content) - 1);
        mem.confidence = 3;

        memories.push_back(mem);
        ESP_LOGI(TAG, "Extracted fact: %s", full_fact.c_str());
    }
}

std::vector<ExtractedMemory> MemoryExtractor::Extract(const std::string& user_text) {
    std::vector<ExtractedMemory> memories;

    // Skip questions and hypothetical statements
    if (IsQuestion(user_text)) {
        ESP_LOGD(TAG, "Skipping question");
        return memories;
    }
    if (IsHypothetical(user_text)) {
        ESP_LOGD(TAG, "Skipping hypothetical");
        return memories;
    }

    // Extract different types
    ExtractIdentity(user_text, memories);
    ExtractPreferences(user_text, memories);
    ExtractFamily(user_text, memories);
    ExtractEvents(user_text, memories);
    ExtractFacts(user_text, memories);

    return memories;
}

int MemoryExtractor::Apply(const std::vector<ExtractedMemory>& memories) {
    int applied = 0;
    auto& storage = MemoryStorage::GetInstance();

    for (const auto& mem : memories) {
        // Only apply high confidence extractions
        if (mem.confidence < 3) continue;

        AUDNAction result = AUDNAction::NOOP;

        switch (mem.type) {
            case ExtractedType::IDENTITY:
                if (strcmp(mem.category, "name") == 0) {
                    result = storage.UpdateProfile(mem.content, nullptr, 0, nullptr, nullptr);
                } else if (strcmp(mem.category, "age") == 0) {
                    int age = atoi(mem.content);
                    if (age > 0) {
                        result = storage.UpdateProfile(nullptr, nullptr, age, nullptr, nullptr);
                    }
                } else if (strcmp(mem.category, "gender") == 0) {
                    result = storage.UpdateProfile(nullptr, nullptr, 0, mem.content, nullptr);
                } else if (strcmp(mem.category, "location") == 0) {
                    result = storage.UpdateProfile(nullptr, nullptr, 0, nullptr, mem.content);
                }
                break;

            case ExtractedType::PREFERENCE:
                if (strcmp(mem.category, "like") == 0) {
                    result = storage.AddPreference(mem.content, true);
                } else {
                    result = storage.AddPreference(mem.content, false);
                }
                break;

            case ExtractedType::FAMILY:
                result = storage.AddFamilyMember(mem.category, mem.content, nullptr, 3, nullptr);
                break;

            case ExtractedType::FACT:
                result = storage.AddFact(mem.content);
                break;

            case ExtractedType::EVENT:
                // Events need date info, skip for now
                break;

            default:
                break;
        }

        if (result == AUDNAction::ADDED || result == AUDNAction::UPDATED) {
            applied++;
        }
    }

    ESP_LOGI(TAG, "Applied %d memories out of %d extracted", applied, (int)memories.size());
    return applied;
}

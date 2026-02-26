#include "background_mcp_tools.h"
#include "background_manager.h"
#include "pet_achievements.h"
#include "pet_coin.h"
#include "mcp_server.h"
#include <esp_log.h>
#include <cJSON.h>
#include <cstring>

#define TAG "BgMcpTools"

// Forward declaration - implemented in board-specific code
extern void check_and_update_background(bool force_update);

// Background name to index mapping
static const struct {
    const char* name;
    uint16_t index;
    const char* category;
    const char* description;
} kBackgroundMap[] = {
    // Time backgrounds (always available)
    {"day",       BG_TIME_DAY,       "time", "white day"},
    {"sunset",    BG_TIME_SUNSET,    "time", "sunset"},
    {"sunrise",   BG_TIME_SUNRISE,   "time", "sunrise"},
    {"night",     BG_TIME_NIGHT,     "time", "night"},
    // Weather background
    {"rainy",     BG_WEATHER_RAINY,  "weather", "rainy"},
    // Festival backgrounds
    {"christmas", BG_FESTIVAL_CHRISTMAS,  "festival", "christmas"},
    {"birthday",  BG_FESTIVAL_BIRTHDAY,   "festival", "birthday"},
    {"spring",    BG_FESTIVAL_SPRING,     "festival", "spring festival"},
    {"newyear",   BG_FESTIVAL_NEWYEAR,    "festival", "new year"},
    {"midautumn", BG_FESTIVAL_MIDAUTUMN,  "festival", "mid autumn"},
    {"halloween", BG_FESTIVAL_HALLOWEEN,  "festival", "halloween"},
    {"valentines",BG_FESTIVAL_VALENTINES, "festival", "valentines"},
    // Style backgrounds (require unlock)
    {"cyberpunk", BG_STYLE_CYBERPUNK, "style", "cyberpunk"},
    {"steampunk", BG_STYLE_STEAMPUNK, "style", "steampunk"},
    {"fantasy",   BG_STYLE_FANTASY,   "style", "fantasy"},
    {"space",     BG_STYLE_SPACE,     "style", "space"},
};
static const int kBackgroundMapSize = sizeof(kBackgroundMap) / sizeof(kBackgroundMap[0]);

// Find background name by index
static const char* FindBackgroundName(uint16_t index) {
    for (int i = 0; i < kBackgroundMapSize; i++) {
        if (kBackgroundMap[i].index == index) {
            return kBackgroundMap[i].name;
        }
    }
    return "unknown";
}

// Add category backgrounds to JSON array
static void AddCategoryBackgrounds(cJSON* parent, const char* category, PetAchievements* achievements) {
    cJSON* cat_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(cat_obj, "category", category);
    cJSON* names = cJSON_CreateArray();

    for (int i = 0; i < kBackgroundMapSize; i++) {
        if (strcmp(kBackgroundMap[i].category, category) != 0) continue;

        // Style backgrounds require unlock check
        if (strcmp(category, "style") == 0 && achievements) {
            if (!achievements->IsBackgroundUnlocked(kBackgroundMap[i].index)) continue;
        }
        cJSON_AddItemToArray(names, cJSON_CreateString(kBackgroundMap[i].name));
    }

    cJSON_AddItemToObject(cat_obj, "names", names);
    cJSON_AddItemToArray(parent, cat_obj);
}

// Build background status JSON response
static cJSON* BuildBackgroundStatusResponse() {
    auto& bg_mgr = BackgroundManager::GetInstance();
    auto& achievements = PetAchievements::GetInstance();

    cJSON* root = cJSON_CreateObject();

    // Current background
    uint16_t current_bg = bg_mgr.GetLastBackground();
    cJSON_AddStringToObject(root, "current_background", FindBackgroundName(current_bg));
    cJSON_AddNumberToObject(root, "current_index", current_bg);

    // Available backgrounds by category
    cJSON* available = cJSON_CreateArray();
    AddCategoryBackgrounds(available, "time", nullptr);
    AddCategoryBackgrounds(available, "weather", nullptr);
    AddCategoryBackgrounds(available, "style", &achievements);
    cJSON_AddItemToObject(root, "available_backgrounds", available);

    return root;
}

// Handle background switch
static std::string HandleSetBackground(const std::string& name) {
    auto& bg_mgr = BackgroundManager::GetInstance();
    auto& achievements = PetAchievements::GetInstance();

    // Find background in map
    for (int i = 0; i < kBackgroundMapSize; i++) {
        if (name != kBackgroundMap[i].name) continue;

        uint16_t bg_idx = kBackgroundMap[i].index;

        // Check unlock status for style backgrounds
        if (strcmp(kBackgroundMap[i].category, "style") == 0 &&
            !achievements.IsBackgroundUnlocked(bg_idx)) {
            return std::string("Background '") + name + "' is not unlocked yet. Complete achievements to unlock.";
        }

        bg_mgr.ForceBackground(bg_idx);
        check_and_update_background(true);  // Force display refresh immediately
        ESP_LOGI(TAG, "Background switched to: %s (index=%d)", name.c_str(), bg_idx);
        return std::string("Switched to background: ") + kBackgroundMap[i].description;
    }

    return std::string("Unknown background name: ") + name +
           ". Available: day, sunset, sunrise, night, rainy, cyberpunk, steampunk, fantasy, space";
}

// Weather name to type mapping
static const struct {
    const char* name;
    WeatherCondition type;
    const char* display_name;
} kWeatherMap[] = {
    {"clear", WEATHER_CLEAR, "clear"},
    {"sunny", WEATHER_CLEAR, "clear"},
    {"rainy", WEATHER_RAINY, "rainy"},
    {"rain",  WEATHER_RAINY, "rainy"},
};

// Handle weather setting
static std::string HandleSetWeather(const std::string& weather) {
    for (const auto& w : kWeatherMap) {
        if (weather == w.name) {
            BackgroundManager::GetInstance().UpdateWeather(w.type);
            ESP_LOGI(TAG, "Weather set to: %s", w.display_name);
            return std::string("Weather set to: ") + w.display_name;
        }
    }
    return std::string("Unknown weather: ") + weather + ". Available: clear, rainy";
}

// Handle background purchase (10 coins)
static std::string HandlePurchaseBackground(const std::string& name) {
    auto& achievements = PetAchievements::GetInstance();
    auto& coin_system = CoinSystem::GetInstance();

    // Only style backgrounds can be purchased
    const struct {
        const char* name;
        bool (PetAchievements::*is_unlocked)() const;
        void (PetAchievements::*unlock)();
    } kPurchasableBackgrounds[] = {
        {"cyberpunk", &PetAchievements::IsCyberpunkUnlocked, &PetAchievements::UnlockCyberpunk},
        {"fantasy",   &PetAchievements::IsFantasyUnlocked,   &PetAchievements::UnlockFantasy},
        {"space",     &PetAchievements::IsSpaceUnlocked,     &PetAchievements::UnlockSpace},
        {"steampunk", &PetAchievements::IsSteampunkUnlocked, &PetAchievements::UnlockSteampunk},
    };

    // Find background in purchasable list
    for (const auto& bg : kPurchasableBackgrounds) {
        if (name != bg.name) continue;

        // Check if already unlocked
        if ((achievements.*bg.is_unlocked)()) {
            return std::string("Background '") + name + "' is already unlocked! No need to purchase again.";
        }

        // Check if enough coins
        if (coin_system.GetCoins() < COST_BACKGROUND) {
            return std::string("Not enough coins! Need ") + std::to_string(COST_BACKGROUND) +
                   " coins to purchase '" + name + "' background. Current coins: " +
                   std::to_string(coin_system.GetCoins());
        }

        // Spend coins and unlock
        if (!coin_system.SpendCoins(COST_BACKGROUND)) {
            return std::string("Failed to spend coins. Please try again.");
        }

        (achievements.*bg.unlock)();
        ESP_LOGI(TAG, "Background purchased: %s (cost=%d coins)", name.c_str(), COST_BACKGROUND);
        return std::string("Successfully purchased '") + name + "' background! Spent " +
               std::to_string(COST_BACKGROUND) + " coins. Remaining: " +
               std::to_string(coin_system.GetCoins()) + " coins.";
    }

    return std::string("Background '") + name + "' cannot be purchased. Only style backgrounds (cyberpunk, fantasy, space, steampunk) can be bought with coins.";
}

void RegisterBackgroundMcpTools(McpServer& mcp_server) {
    ESP_LOGI(TAG, "Registering background MCP tool");

    mcp_server.AddTool(
        "background",
        "Background management tool. Query current background status, switch backgrounds, purchase, or set weather.\n"
        "Backgrounds affect device display, including time backgrounds (day/sunset/sunrise/night),\n"
        "weather background (rainy), and style backgrounds (cyberpunk/steampunk/fantasy/space).\n\n"
        "Style backgrounds can be unlocked by:\n"
        "1. Completing achievements (free)\n"
        "2. Purchasing with 10 coins (via 'purchase' action)\n\n"
        "Actions:\n"
        "- status: Get current background status and available background list\n"
        "- set: Force switch to specified background. name: day, sunset, sunrise, night, rainy, cyberpunk, steampunk, fantasy, space\n"
        "- purchase: Purchase style background with 10 coins. name: cyberpunk, fantasy, space, steampunk\n"
        "- auto: Restore automatic background mode (auto-select based on time/weather/festival)\n"
        "- weather: Set weather condition. type: clear, rainy\n\n"
        "Examples:\n"
        "- background(action='status') -> Returns current background info\n"
        "- background(action='set', name='night') -> Switch to night background\n"
        "- background(action='purchase', name='cyberpunk') -> Purchase cyberpunk background (10 coins)\n"
        "- background(action='set', name='cyberpunk') -> Switch to cyberpunk (requires unlock)\n"
        "- background(action='auto') -> Restore auto background\n"
        "- background(action='weather', type='rainy') -> Set to rainy",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("name", kPropertyTypeString, std::string("")),
            Property("type", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string action = props["action"].value<std::string>();

            if (action == "status") {
                return BuildBackgroundStatusResponse();
            }
            else if (action == "set") {
                std::string name = props["name"].value<std::string>();
                if (name.empty()) {
                    return std::string("'set' requires 'name' parameter. Available: day, sunset, sunrise, night, rainy, cyberpunk, steampunk, fantasy, space");
                }
                return HandleSetBackground(name);
            }
            else if (action == "purchase") {
                std::string name = props["name"].value<std::string>();
                if (name.empty()) {
                    return std::string("'purchase' requires 'name' parameter. Available: cyberpunk, fantasy, space, steampunk");
                }
                return HandlePurchaseBackground(name);
            }
            else if (action == "auto") {
                BackgroundManager::GetInstance().ClearForce();
                ESP_LOGI(TAG, "Restored auto background mode");
                return std::string("Restored automatic background mode");
            }
            else if (action == "weather") {
                std::string type = props["type"].value<std::string>();
                if (type.empty()) {
                    return std::string("'weather' requires 'type' parameter. Available: clear, rainy");
                }
                return HandleSetWeather(type);
            }
            else {
                return std::string("Unknown action. Available: 'status'(query), 'set'(switch), 'purchase'(buy with coins), 'auto'(automatic), 'weather'(weather)");
            }
        }
    );

    ESP_LOGI(TAG, "Background MCP tool registered");
}

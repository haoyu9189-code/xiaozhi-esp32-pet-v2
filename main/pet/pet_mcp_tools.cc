#include "pet_mcp_tools.h"
#include "pet_state.h"
#include "pet_achievements.h"
#include "pet_coin.h"
#include "pet_event_log.h"
#include "scene_items.h"
#include "mcp_server.h"
#include "application.h"
#include <esp_log.h>
#include <cJSON.h>
#include <vector>
#include <cstdlib>

#define TAG "PetMcpTools"

// Style background metadata for status response
static const struct {
    const char* name;
    const char* unlock_condition;
    bool (PetAchievements::*is_unlocked)() const;
} kStyleBackgrounds[] = {
    {"cyberpunk", "洗澡5次",  &PetAchievements::IsCyberpunkUnlocked},
    {"fantasy",   "洗澡20次", &PetAchievements::IsFantasyUnlocked},
    {"space",     "对话10次", &PetAchievements::IsSpaceUnlocked},
    {"steampunk", "陪伴7天",  &PetAchievements::IsSteampunkUnlocked},
};

// 构建宠物状态 JSON 响应
static cJSON* BuildPetStatusResponse() {
    auto& pet = PetStateMachine::GetInstance();
    const auto& stats = pet.GetStats();
    auto& achievements = PetAchievements::GetInstance();
    const auto& counters = achievements.GetCounters();
    auto& coin_system = CoinSystem::GetInstance();

    cJSON* root = cJSON_CreateObject();

    // 基础属性 (0-100)
    cJSON_AddNumberToObject(root, "hunger", stats.hunger);
    cJSON_AddNumberToObject(root, "happiness", stats.happiness);
    cJSON_AddNumberToObject(root, "cleanliness", stats.cleanliness);

    // 金币系统
    cJSON_AddNumberToObject(root, "coins", coin_system.GetCoins());
    cJSON_AddNumberToObject(root, "daily_chat_count", coin_system.GetDailyChatCount());
    cJSON_AddNumberToObject(root, "total_coins_spent", coin_system.GetTotalCoinsSpent());  // Intimacy system parameter


    // 状态描述（便于大模型理解）
    cJSON_AddStringToObject(root, "mood", pet.GetMoodDescription());
    cJSON_AddNumberToObject(root, "age_days", stats.age_minutes / 1440);

    // 当前动作
    cJSON_AddStringToObject(root, "current_action",
        PetStateMachine::ActionToString(pet.GetAction()));

    // 活动统计
    cJSON* activities = cJSON_CreateObject();
    cJSON_AddNumberToObject(activities, "bathe_count", counters.bathe_count);
    cJSON_AddNumberToObject(activities, "feed_count", counters.feed_count);
    cJSON_AddNumberToObject(activities, "play_count", counters.play_count);
    cJSON_AddNumberToObject(activities, "conversation_count", counters.conversation_count);
    cJSON_AddNumberToObject(activities, "days_alive", counters.days_alive);
    cJSON_AddItemToObject(root, "activities", activities);

    // 提醒列表
    cJSON* reminders = cJSON_CreateArray();
    if (stats.NeedsBathing()) {
        cJSON* reminder = cJSON_CreateObject();
        cJSON_AddStringToObject(reminder, "type", "bathing");
        cJSON_AddStringToObject(reminder, "message", "宠物清洁度很低，需要洗澡了！");
        cJSON_AddItemToArray(reminders, reminder);
    }
    cJSON_AddItemToObject(root, "reminders", reminders);

    // 风格背景解锁状态
    cJSON* style_backgrounds = cJSON_CreateArray();
    for (const auto& bg : kStyleBackgrounds) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", bg.name);
        cJSON_AddBoolToObject(item, "unlocked", (achievements.*bg.is_unlocked)());
        cJSON_AddStringToObject(item, "unlock_condition", bg.unlock_condition);
        cJSON_AddItemToArray(style_backgrounds, item);
    }
    cJSON_AddItemToObject(root, "style_backgrounds", style_backgrounds);

    // 已解锁背景索引列表
    cJSON* unlocked_bg_indices = cJSON_CreateArray();
    auto unlocked_indices = achievements.GetUnlockedBackgroundIndices();
    for (uint16_t idx : unlocked_indices) {
        cJSON_AddItemToArray(unlocked_bg_indices, cJSON_CreateNumber(idx));
    }
    cJSON_AddItemToObject(root, "unlocked_background_indices", unlocked_bg_indices);

    // 最近事件日志
    auto& event_log = PetEventLog::GetInstance();
    if (event_log.GetCount() > 0) {
        std::string events_json = event_log.GetRecentEventsJson(5);
        cJSON* events = cJSON_Parse(events_json.c_str());
        if (events) {
            cJSON_AddItemToObject(root, "recent_events", events);
        }
    }

    return root;
}

// 检查双满奖励并追加提示
static void AppendBothFullBonus(std::string& result, const PetStats& stats) {
    if (stats.IsBothFull()) {
        result += "，饥饿和清洁都满了，心情变得超好！";
    }
}

// 处理交互动作 - 需要消耗金币
static std::string HandleInteraction(const std::string& type) {
    auto& pet = PetStateMachine::GetInstance();
    auto& coin_system = CoinSystem::GetInstance();

    if (type == "feed") {
        if (!coin_system.SpendCoins(1)) {
            return "金币不足！需要先捡金币才能喂食~";
        }
        pet.Feed();
        pet.OnSessionCareAction();
        std::string result = "喂食成功！消耗1金币，开始吃饭（持续5分钟，每分钟+20饱食度）";
        AppendBothFullBonus(result, pet.GetStats());
        return result;
    }

    if (type == "bathe") {
        if (!coin_system.SpendCoins(1)) {
            return "金币不足！需要先捡金币才能洗澡~";
        }
        auto& scene = SceneItemManager::GetInstance();
        uint8_t poop_count = scene.GetPoopCount();
        pet.Bathe();
        pet.OnSessionCareAction();

        std::string result = "洗澡成功！消耗1金币，开始洗澡（持续5分钟，每分钟+20清洁度）";
        if (poop_count > 0) {
            result += "，清理了" + std::to_string(poop_count) + "个便便（便便处有50%概率刷出金币）";
        }
        AppendBothFullBonus(result, pet.GetStats());
        return result;
    }

    return "未知的互动类型。可用类型: feed(喂食), bathe(洗澡)";
}

void RegisterPetMcpTools(McpServer& mcp_server) {
    ESP_LOGI(TAG, "Registering pet MCP tool");

    mcp_server.AddTool(
        "pet",
        "宠物状态管理工具。用于查询宠物当前状态或与宠物互动。\n"
        "宠物有3个属性(0-100): hunger(饱食度), happiness(心情值), cleanliness(清洁度)\n"
        "属性会随时间自然衰减。通过互动维护宠物状态。\n\n"
        "核心机制:\n"
        "1. 金币获取: 所有属性>50时，每30分钟随机生成金币（属性越高频率越快）\n"
        "2. 金币消费: 喂食/洗澡各消耗1金币，购买背景消耗10金币\n"
        "3. 持续恢复: 吃饭/洗澡期间每分钟+20，满了自动停止（最多5分钟）\n"
        "4. 双满奖励: 饥饿和清洁都达到100时，心情变为100\n\n"
        "Actions:\n"
        "- status: 获取宠物当前状态，包括所有属性值、金币数、心情描述等\n"
        "- interact: 与宠物互动（需要1金币）。type可选: feed(持续吃5分钟), bathe(持续洗5分钟+清理便便)\n\n"
        "Examples:\n"
        "- pet(action='status') -> 返回宠物完整状态\n"
        "- pet(action='interact', type='feed') -> 喂食宠物（消耗1金币，持续吃5分钟，每分钟+20）\n"
        "- pet(action='interact', type='bathe') -> 给宠物洗澡（消耗1金币，持续洗5分钟，每分钟+20，清理便便）",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("type", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string action = props["action"].value<std::string>();

            if (action == "status") {
                // 追踪本次对话是否查看了状态
                PetStateMachine::GetInstance().OnSessionStatusChecked();
                return BuildPetStatusResponse();
            }
            else if (action == "interact") {
                std::string type = props["type"].value<std::string>();
                if (type.empty()) {
                    return std::string("interact需要指定type参数。可选: feed, play, bathe, sleep, heal");
                }
                return HandleInteraction(type);
            }
            else {
                return std::string("未知的action。可用: 'status'(查询状态) 或 'interact'(互动)");
            }
        }
    );

    // 宠物移动控制工具
    mcp_server.AddTool(
        "pet_move",
        "控制宠物在屏幕上移动。可以让宠物向上、下、左、右移动。\n"
        "移动时会播放行走动画，移动结束后可以捡到金币或踩到便便。\n\n"
        "参数:\n"
        "- direction: 移动方向，可选 up(上), down(下), left(左), right(右)\n"
        "- distance: 移动距离(像素)，默认30，范围10-60\n\n"
        "示例:\n"
        "- pet_move(direction='left') -> 向左移动30像素\n"
        "- pet_move(direction='up', distance=20) -> 向上移动20像素\n"
        "- pet_move(direction='right', distance=50) -> 向右移动50像素",
        PropertyList({
            Property("direction", kPropertyTypeString),
            Property("distance", kPropertyTypeInteger, 30, 10, 60)
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string direction_str = props["direction"].value<std::string>();
            int distance = props["distance"].value<int>();

            MoveDirection direction;
            if (direction_str == "up") {
                direction = MoveDirection::kUp;
            } else if (direction_str == "down") {
                direction = MoveDirection::kDown;
            } else if (direction_str == "left") {
                direction = MoveDirection::kLeft;
            } else if (direction_str == "right") {
                direction = MoveDirection::kRight;
            } else {
                return std::string("未知方向: ") + direction_str + "。可用: up, down, left, right";
            }

            auto& pet = PetStateMachine::GetInstance();
            if (pet.Move(direction, (int16_t)distance)) {
                return std::string("宠物开始向") +
                    (direction_str == "up" ? "上" :
                     direction_str == "down" ? "下" :
                     direction_str == "left" ? "左" : "右") +
                    "移动" + std::to_string(distance) + "像素";
            } else {
                return std::string("移动失败：宠物正忙或已到达边界");
            }
        }
    );

    // 金币收集工具 - 获取金币位置并自动移动
    mcp_server.AddTool(
        "collect_coins",
        "查找屏幕上的金币并移动去捡取。\n"
        "返回宠物当前位置、所有金币坐标以及到每个金币的移动建议。\n\n"
        "工作模式:\n"
        "- action='scan': 扫描所有金币位置，返回详细信息供你决策\n"
        "- action='nearest': 自动移动到最近的金币（需要多次调用来捡多个）\n"
        "- action='all': 返回收集所有金币需要的移动序列\n\n"
        "示例:\n"
        "- collect_coins(action='scan') -> 返回宠物位置和所有金币坐标\n"
        "- collect_coins(action='nearest') -> 自动移动到最近的金币\n"
        "- collect_coins(action='all') -> 返回捡取所有金币的移动指令序列",
        PropertyList({
            Property("action", kPropertyTypeString, std::string("scan"))
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string action = props["action"].value<std::string>();
            auto& pet = PetStateMachine::GetInstance();
            auto& scene = SceneItemManager::GetInstance();

            int16_t pet_x = pet.GetPositionX();
            int16_t pet_y = pet.GetPositionY();

            uint8_t coin_count = 0;
            const SceneItem* coins = scene.GetCoins(&coin_count);

            if (coin_count == 0) {
                return std::string("屏幕上没有金币，需要等待金币刷新（保持属性>50会自动刷新金币）");
            }

            // Find nearest coin and build coin list
            int nearest_idx = -1;
            int32_t nearest_dist_sq = INT32_MAX;

            cJSON* coin_array = cJSON_CreateArray();
            for (uint8_t i = 0; i < coin_count; i++) {
                if (!coins[i].active) continue;

                int16_t dx = coins[i].x - pet_x;
                int16_t dy = coins[i].y - pet_y;
                int32_t dist_sq = dx * dx + dy * dy;

                cJSON* coin_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(coin_obj, "x", coins[i].x);
                cJSON_AddNumberToObject(coin_obj, "y", coins[i].y);
                cJSON_AddNumberToObject(coin_obj, "dx", dx);
                cJSON_AddNumberToObject(coin_obj, "dy", dy);

                // Suggest move direction
                const char* dir_x = (dx > 0) ? "right" : (dx < 0) ? "left" : "none";
                const char* dir_y = (dy > 0) ? "down" : (dy < 0) ? "up" : "none";
                cJSON_AddStringToObject(coin_obj, "horizontal", dir_x);
                cJSON_AddStringToObject(coin_obj, "vertical", dir_y);
                cJSON_AddItemToArray(coin_array, coin_obj);

                if (dist_sq < nearest_dist_sq) {
                    nearest_dist_sq = dist_sq;
                    nearest_idx = i;
                }
            }

            if (action == "scan") {
                // Return detailed info for AI to decide
                cJSON* result = cJSON_CreateObject();
                cJSON_AddNumberToObject(result, "pet_x", pet_x);
                cJSON_AddNumberToObject(result, "pet_y", pet_y);
                cJSON_AddNumberToObject(result, "coin_count", coin_count);
                cJSON_AddItemToObject(result, "coins", coin_array);
                return result;
            }
            else if (action == "nearest") {
                cJSON_Delete(coin_array);

                // Check if in conversation - defer coin collection
                auto& app = Application::GetInstance();
                DeviceState state = app.GetDeviceState();
                if (state == kDeviceStateListening || state == kDeviceStateSpeaking) {
                    return std::string("我们正在对话中呢~金币会一直在那里的，等聊完再去捡也不迟！");
                }

                if (nearest_idx < 0) {
                    return std::string("找不到可捡取的金币");
                }

                int16_t dx = coins[nearest_idx].x - pet_x;
                int16_t dy = coins[nearest_idx].y - pet_y;

                // Determine primary direction (larger distance)
                bool move_success = false;
                std::string move_desc;

                if (abs(dx) >= abs(dy) && dx != 0) {
                    // Move horizontally first
                    MoveDirection dir = (dx > 0) ? MoveDirection::kRight : MoveDirection::kLeft;
                    int16_t dist = abs(dx);
                    if (dist > 60) dist = 60;
                    move_success = pet.Move(dir, dist);
                    move_desc = (dx > 0) ? "右" : "左";
                    move_desc += std::to_string(dist) + "像素";
                } else if (dy != 0) {
                    // Move vertically
                    MoveDirection dir = (dy > 0) ? MoveDirection::kDown : MoveDirection::kUp;
                    int16_t dist = abs(dy);
                    if (dist > 60) dist = 60;
                    move_success = pet.Move(dir, dist);
                    move_desc = (dy > 0) ? "下" : "上";
                    move_desc += std::to_string(dist) + "像素";
                } else {
                    return std::string("已经在金币位置附近，等待拾取");
                }

                if (move_success) {
                    std::string result = "正在向" + move_desc + "移动去捡金币";
                    if (coin_count > 1) {
                        result += "（还有" + std::to_string(coin_count - 1) + "个金币）";
                    }
                    return result;
                } else {
                    return std::string("移动失败：宠物正忙或已到达边界");
                }
            }
            else if (action == "all") {
                // Return move sequence for all coins
                cJSON* result = cJSON_CreateObject();
                cJSON_AddNumberToObject(result, "pet_x", pet_x);
                cJSON_AddNumberToObject(result, "pet_y", pet_y);
                cJSON_AddNumberToObject(result, "total_coins", coin_count);

                cJSON* moves = cJSON_CreateArray();
                int16_t cur_x = pet_x, cur_y = pet_y;

                // Simple greedy: always go to nearest remaining coin
                std::vector<bool> collected(coin_count, false);
                for (uint8_t step = 0; step < coin_count; step++) {
                    int best_idx = -1;
                    int32_t best_dist = INT32_MAX;

                    for (uint8_t i = 0; i < coin_count; i++) {
                        if (collected[i] || !coins[i].active) continue;
                        int16_t dx = coins[i].x - cur_x;
                        int16_t dy = coins[i].y - cur_y;
                        int32_t dist = dx * dx + dy * dy;
                        if (dist < best_dist) {
                            best_dist = dist;
                            best_idx = i;
                        }
                    }

                    if (best_idx < 0) break;
                    collected[best_idx] = true;

                    int16_t dx = coins[best_idx].x - cur_x;
                    int16_t dy = coins[best_idx].y - cur_y;

                    cJSON* move = cJSON_CreateObject();
                    cJSON_AddNumberToObject(move, "step", step + 1);
                    cJSON_AddNumberToObject(move, "target_x", coins[best_idx].x);
                    cJSON_AddNumberToObject(move, "target_y", coins[best_idx].y);

                    // Build move commands
                    cJSON* commands = cJSON_CreateArray();
                    if (dx != 0) {
                        cJSON* cmd = cJSON_CreateObject();
                        cJSON_AddStringToObject(cmd, "direction", (dx > 0) ? "right" : "left");
                        cJSON_AddNumberToObject(cmd, "distance", abs(dx) > 60 ? 60 : abs(dx));
                        cJSON_AddItemToArray(commands, cmd);
                    }
                    if (dy != 0) {
                        cJSON* cmd = cJSON_CreateObject();
                        cJSON_AddStringToObject(cmd, "direction", (dy > 0) ? "down" : "up");
                        cJSON_AddNumberToObject(cmd, "distance", abs(dy) > 60 ? 60 : abs(dy));
                        cJSON_AddItemToArray(commands, cmd);
                    }
                    cJSON_AddItemToObject(move, "commands", commands);
                    cJSON_AddItemToArray(moves, move);

                    cur_x = coins[best_idx].x;
                    cur_y = coins[best_idx].y;
                }

                cJSON_AddItemToObject(result, "move_sequence", moves);
                cJSON_Delete(coin_array);
                return result;
            }

            cJSON_Delete(coin_array);
            return std::string("未知action，可用: scan, nearest, all");
        }
    );

    // 调试工具：强制生成场景物品
    mcp_server.AddTool(
        "debug_spawn_items",
        "调试工具：强制生成金币和便便用于测试显示。\n"
        "参数：\n"
        "- type: 物品类型，可选 coin(金币), poop(便便), both(两者都生成)\n\n"
        "示例：\n"
        "- debug_spawn_items(type='coin') -> 在随机位置生成一个金币\n"
        "- debug_spawn_items(type='poop') -> 在随机位置生成一个便便\n"
        "- debug_spawn_items(type='both') -> 同时生成金币和便便用于测试",
        PropertyList({
            Property("type", kPropertyTypeString)
        }),
        [](const PropertyList& props) -> ReturnValue {
            std::string type = props["type"].value<std::string>();
            auto& scene = SceneItemManager::GetInstance();

            if (type == "coin") {
                scene.SpawnCoin();
                return std::string("已生成金币，检查屏幕是否显示");
            } else if (type == "poop") {
                // 直接调用内部方法生成便便
                scene.DebugSpawnItems();  // 这会生成测试物品
                return std::string("已生成测试物品（金币+便便），检查屏幕是否显示");
            } else if (type == "both") {
                scene.DebugSpawnItems();
                return std::string("已生成测试物品（金币和便便），检查屏幕");
            } else {
                return std::string("未知类型: ") + type + "。可用: coin, poop, both";
            }
        }
    );

    // 退出对话工具
    mcp_server.AddTool(
        "end_conversation",
        "主动结束当前对话。\n"
        "当用户说再见、拜拜、88等告别语时，AI应该回复告别语后调用此工具主动退出对话。\n\n"
        "使用场景：\n"
        "- 用户说'拜拜' → AI回复'拜拜~' → 调用 end_conversation() 退出\n"
        "- 用户说'再见' → AI回复'再见~' → 调用 end_conversation() 退出\n"
        "- 用户说'88' → AI回复'88~' → 调用 end_conversation() 退出",
        PropertyList(std::vector<Property>{}),
        [](const PropertyList& props) -> ReturnValue {
            // 停止监听，结束对话
            Application::GetInstance().StopListening();
            ESP_LOGI(TAG, "AI ended conversation");
            return std::string("对话已结束");
        }
    );

    ESP_LOGI(TAG, "Pet MCP tools registered");
}

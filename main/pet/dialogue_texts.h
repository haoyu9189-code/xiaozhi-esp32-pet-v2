#ifndef DIALOGUE_TEXTS_H
#define DIALOGUE_TEXTS_H

// ============================================================================
// 环境对白文本库 - 体现可爱的性格
// ============================================================================

namespace DialogueTexts {

// ============================================================================
// 事件反应对白
// ============================================================================

// 金币出现时的对白
static const char* kCoinAppear[] = {
    "咦，那里有金币！",
    "有闪闪发光的东西~",
    "是金币耶！",
    "哇，金币出现了！",
    "快看快看，有金币！",
};
static constexpr int kCoinAppearCount = sizeof(kCoinAppear) / sizeof(kCoinAppear[0]);

// 捡到金币时的对白
static const char* kCoinPickup[] = {
    "耶，捡到金币啦~",
    "金币get！",
    "太好了，又有钱了！",
    "嘿嘿，发财啦~",
    "金币+1，开心！",
    "捡钱的感觉真好~",
    "我是小富婆！",
};
static constexpr int kCoinPickupCount = sizeof(kCoinPickup) / sizeof(kCoinPickup[0]);

// 便便生成时的对白
static const char* kPoopAppear[] = {
    "嗯...我拉便便了",
    "啊，不小心拉了...",
    "嘘嘘~",
    "有点尴尬...",
    "对不起，我没忍住...",
    "呜，拉便便了",
};
static constexpr int kPoopAppearCount = sizeof(kPoopAppear) / sizeof(kPoopAppear[0]);

// 踩到便便时的对白
static const char* kPoopStep[] = {
    "啊呀！踩到便便了！",
    "呜呜，好臭...",
    "不要啊，我的脚！",
    "踩到了...好恶心",
    "完蛋，踩中了...",
    "啊！！！我的小脚脚！",
    "呜呜呜，踩到自己的便便了",
};
static constexpr int kPoopStepCount = sizeof(kPoopStep) / sizeof(kPoopStep[0]);

// 开始吃饭时的对白
static const char* kStartEating[] = {
    "开饭啦~",
    "我要吃饭饭！",
    "好饿好饿，终于可以吃了！",
    "饭饭来咯~",
    "嗯嗯，要大吃一顿！",
    "干饭时间到！",
    "让我好好吃一顿~",
};
static constexpr int kStartEatingCount = sizeof(kStartEating) / sizeof(kStartEating[0]);

// 吃饱了的对白
static const char* kFullEating[] = {
    "吃饱啦~满足！",
    "好撑好撑~",
    "嗝~吃太饱了",
    "肚子圆滚滚的~",
    "吃得好开心！",
    "幸福满满~",
};
static constexpr int kFullEatingCount = sizeof(kFullEating) / sizeof(kFullEating[0]);

// 开始洗澡时的对白
static const char* kStartBathing[] = {
    "洗香香时间~",
    "要洗澡啦！",
    "泡泡澡最舒服了~",
    "洗得干干净净！",
    "我爱洗澡，皮肤好好~",
    "冲凉凉咯~",
};
static constexpr int kStartBathingCount = sizeof(kStartBathing) / sizeof(kStartBathing[0]);

// 洗完澡的对白
static const char* kFullBathing[] = {
    "洗完啦，好清爽~",
    "干净的我最可爱！",
    "香喷喷的~",
    "焕然一新的感觉！",
    "舒服极了~",
};
static constexpr int kFullBathingCount = sizeof(kFullBathing) / sizeof(kFullBathing[0]);

// ============================================================================
// 时间问候对白
// ============================================================================

// 早上 (6:00-11:59)
static const char* kMorningGreeting[] = {
    "早上好~新的一天开始啦！",
    "早安~今天也要元气满满！",
    "呼啊~睡得真舒服~",
    "早上好呀，阳光真好~",
    "新的一天，要加油哦！",
    "早早早~又是美好的一天！",
};
static constexpr int kMorningGreetingCount = sizeof(kMorningGreeting) / sizeof(kMorningGreeting[0]);

// 下午 (12:00-17:59)
static const char* kAfternoonGreeting[] = {
    "下午好~有点困困的...",
    "午安~要不要午睡一下？",
    "下午啦，时间过得好快~",
    "阳光暖暖的，好舒服~",
    "下午茶时间到了吗？",
};
static constexpr int kAfternoonGreetingCount = sizeof(kAfternoonGreeting) / sizeof(kAfternoonGreeting[0]);

// 傍晚 (18:00-20:59)
static const char* kEveningGreeting[] = {
    "傍晚啦~天要黑了呢",
    "太阳要下山咯~",
    "晚安时光~",
    "傍晚的风好舒服~",
    "快到睡觉时间了~",
};
static constexpr int kEveningGreetingCount = sizeof(kEveningGreeting) / sizeof(kEveningGreeting[0]);

// 夜晚 (21:00-5:59)
static const char* kNightGreeting[] = {
    "夜深了，该睡觉啦~",
    "晚安晚安，好梦~",
    "夜晚静悄悄~",
    "要早点睡哦！",
    "月亮出来啦~",
    "困了困了，想睡觉...",
};
static constexpr int kNightGreetingCount = sizeof(kNightGreeting) / sizeof(kNightGreeting[0]);

// ============================================================================
// 心情自言自语（根据属性值）
// ============================================================================

// 饥饿时 (hunger < 30)
static const char* kHungry[] = {
    "好饿啊...",
    "肚子咕咕叫了...",
    "想吃东西...",
    "饿扁了...",
    "快给我饭饭！",
    "饿得没力气了...",
};
static constexpr int kHungryCount = sizeof(kHungry) / sizeof(kHungry[0]);

// 很脏时 (cleanliness < 30)
static const char* kDirty[] = {
    "好脏啊，想洗澡...",
    "身上黏糊糊的...",
    "需要洗香香了...",
    "感觉臭臭的...",
    "快受不了了，要洗澡！",
};
static constexpr int kDirtyCount = sizeof(kDirty) / sizeof(kDirty[0]);

// 心情不好时 (happiness < 30)
static const char* kUnhappy[] = {
    "心情不太好...",
    "有点不开心...",
    "唉...",
    "闷闷不乐...",
    "需要人陪陪...",
    "感觉孤单...",
};
static constexpr int kUnhappyCount = sizeof(kUnhappy) / sizeof(kUnhappy[0]);

// 心情很好时 (happiness >= 80)
static const char* kHappy[] = {
    "今天心情真好~",
    "开心开心~",
    "感觉棒棒哒！",
    "好幸福啊~",
    "心情美美哒~",
    "开心到飞起~",
    "太快乐了！",
};
static constexpr int kHappyCount = sizeof(kHappy) / sizeof(kHappy[0]);

// 状态良好时 (所有属性 >= 60)
static const char* kFeelGood[] = {
    "感觉状态满满~",
    "今天状态真好！",
    "精神百倍！",
    "活力充沛~",
    "满血复活！",
};
static constexpr int kFeelGoodCount = sizeof(kFeelGood) / sizeof(kFeelGood[0]);

// ============================================================================
// 节日祝福
// ============================================================================

// 春节 (1月21日 - 2月20日，农历正月初一前后)
static const char* kSpringFestival[] = {
    "新年快乐！恭喜发财~",
    "过年啦！红包拿来~",
    "新年新气象，万事如意！",
    "春节快乐~",
    "恭喜恭喜，大吉大利！",
};
static constexpr int kSpringFestivalCount = sizeof(kSpringFestival) / sizeof(kSpringFestival[0]);

// 元宵节 (春节后15天)
static const char* kLanternFestival[] = {
    "元宵节快乐~吃汤圆咯！",
    "正月十五闹元宵~",
    "元宵节团团圆圆~",
};
static constexpr int kLanternFestivalCount = sizeof(kLanternFestival) / sizeof(kLanternFestival[0]);

// 情人节 (2月14日)
static const char* kValentinesDay[] = {
    "情人节快乐~要甜甜蜜蜜哦！",
    "爱你哟~",
    "情人节，送你一个大大的拥抱！",
};
static constexpr int kValentinesDayCount = sizeof(kValentinesDay) / sizeof(kValentinesDay[0]);

// 清明节 (4月4日-6日)
static const char* kQingmingFestival[] = {
    "清明时节，踏青去~",
    "春暖花开的日子~",
};
static constexpr int kQingmingFestivalCount = sizeof(kQingmingFestival) / sizeof(kQingmingFestival[0]);

// 劳动节 (5月1日)
static const char* kLaborDay[] = {
    "劳动节快乐~休息一下吧！",
    "五一假期，玩得开心~",
};
static constexpr int kLaborDayCount = sizeof(kLaborDay) / sizeof(kLaborDay[0]);

// 端午节 (农历五月初五，6月前后)
static const char* kDragonBoatFestival[] = {
    "端午节快乐~吃粽子咯！",
    "端午安康~",
    "粽子香香~",
};
static constexpr int kDragonBoatFestivalCount = sizeof(kDragonBoatFestival) / sizeof(kDragonBoatFestival[0]);

// 儿童节 (6月1日)
static const char* kChildrensDay[] = {
    "儿童节快乐~永远童心未泯！",
    "六一快乐~我们都是大宝宝！",
};
static constexpr int kChildrensDayCount = sizeof(kChildrensDay) / sizeof(kChildrensDay[0]);

// 中秋节 (农历八月十五，9月前后)
static const char* kMidAutumnFestival[] = {
    "中秋节快乐~月饼好吃！",
    "月圆人团圆~",
    "中秋佳节，赏月咯~",
};
static constexpr int kMidAutumnFestivalCount = sizeof(kMidAutumnFestival) / sizeof(kMidAutumnFestival[0]);

// 国庆节 (10月1日)
static const char* kNationalDay[] = {
    "国庆快乐~祖国万岁！",
    "十一假期，开心玩耍~",
};
static constexpr int kNationalDayCount = sizeof(kNationalDay) / sizeof(kNationalDay[0]);

// 万圣节 (10月31日)
static const char* kHalloween[] = {
    "不给糖就捣蛋~",
    "万圣节快乐！",
    "Halloween~",
};
static constexpr int kHalloweenCount = sizeof(kHalloween) / sizeof(kHalloween[0]);

// 圣诞节 (12月25日)
static const char* kChristmas[] = {
    "圣诞快乐~Merry Christmas！",
    "圣诞老人会来吗？",
    "平安夜，平平安安~",
    "叮叮当，叮叮当~",
};
static constexpr int kChristmasCount = sizeof(kChristmas) / sizeof(kChristmas[0]);

// 元旦 (1月1日)
static const char* kNewYear[] = {
    "新年快乐！Happy New Year！",
    "新的一年，新的开始~",
    "元旦快乐~",
};
static constexpr int kNewYearCount = sizeof(kNewYear) / sizeof(kNewYear[0]);

}  // namespace DialogueTexts

#endif  // DIALOGUE_TEXTS_H

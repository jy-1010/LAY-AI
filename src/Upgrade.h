#pragma once
#include "Config.h"

// アップグレード (ステージクリア後のショップで、ボルトを使って購入する)。
// プレイヤーの強化は参加している全員に、列車の強化は列車に効く。

enum class Upgrade {
    // --- プレイヤー ---
    MoveSpeed,          // 移動速度
    CarryCapacity,      // 持てる量
    HarvestYield,       // 採取で獲得個数が増える確率
    SmashChance,        // 採取した物の隣の 木・岩 を壊す確率
    // --- 列車 ---
    CraftSpeed,         // 線路を作る速さ
    TrackCapacity,      // 線路の積載量 (レール作成車両)
    CargoCapacity,      // 貨物の積載量 (貨物車両)
    Count
};
constexpr int kUpgradeCount = (int)Upgrade::Count;

struct UpgradeInfo {
    const char* name;       // 表示名
    const char* effect;     // 効果の説明
    bool        forTrain;   // true = 列車の強化 / false = プレイヤーの強化
    int         baseCost;   // 1レベル目の値段 (ボルト)
    int         costStep;   // レベルが上がるごとの値段の上乗せ
    int         maxLevel;   // 最大レベル
};

//                                          name               effect                                     train  base step max
inline constexpr UpgradeInfo kUpgradeInfo[kUpgradeCount] = {
    { "MOVE SPEED",     "PLAYERS MOVE FASTER",                      false, 3, 2, 5 },
    { "CARRY CAPACITY", "CARRY MORE ITEMS AT ONCE",                 false, 4, 2, 4 },
    { "HARVEST YIELD",  "CHANCE FOR EXTRA ITEMS WHEN PICKING UP",   false, 4, 2, 5 },
    { "SMASH CHANCE",   "CHANCE TO BREAK NEIGHBORING OBJECTS",      false, 5, 2, 5 },
    { "CRAFT SPEED",    "TRAIN CRAFTS TRACKS FASTER",               true,  4, 2, 5 },
    { "TRACK CAPACITY", "CRAFTER WAGON HOLDS MORE TRACKS",          true,  3, 2, 4 },
    { "CARGO CAPACITY", "CARGO WAGON HOLDS MORE WOOD AND IRON",     true,  3, 2, 4 },
};

// level レベルの時に、次のレベルを買うのに必要なボルト
inline int UpgradeCostAt(Upgrade u, int level)
{
    const UpgradeInfo& info = kUpgradeInfo[(int)u];
    return info.baseCost + info.costStep * level;
}

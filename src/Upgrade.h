#pragma once
#include "Config.h"

// アップグレード (ステージクリア後のショップで、ボルトを使って購入する)。
//   プレイヤーの強化は参加している全員に、列車の強化は列車に効く。
//   車両は購入すると列車に連結され、以降のステージでずっと使える (最大レベル 1 = 購入済み)。

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
    TrainBrake,         // 列車の速度を落とす
    // --- 車両 (高額) ---
    CollectorWagon,     // 自動回収車両: 近くに落ちている 木・鉄 とボルトを自動で回収する
    MinerWagon,         // 周囲採掘車両: 近くの 木・岩 を自動で採掘して貨物に入れる
    DynamiteWagon,      // ダイナマイト作成車両: 壊せない岩を壊すダイナマイトを作る
    Count
};
constexpr int kUpgradeCount = (int)Upgrade::Count;

enum class UpgradeKind { Player, Train, Wagon };          // ショップでの区分

struct UpgradeInfo {
    const char* name;       // 表示名
    const char* effect;     // 効果の説明
    UpgradeKind kind;       // 区分
    int         baseCost;   // 1レベル目の値段 (ボルト)
    int         costStep;   // レベルが上がるごとの値段の上乗せ
    int         maxLevel;   // 最大レベル
};

//                                          name               effect                                           kind                    base step max
inline constexpr UpgradeInfo kUpgradeInfo[kUpgradeCount] = {
    { "MOVE SPEED",     "PLAYERS MOVE FASTER",                            UpgradeKind::Player,  3, 2, 5 },
    { "CARRY CAPACITY", "CARRY MORE ITEMS AT ONCE",                       UpgradeKind::Player,  4, 2, 4 },
    { "HARVEST YIELD",  "CHANCE FOR EXTRA ITEMS WHEN PICKING UP",         UpgradeKind::Player,  4, 2, 5 },
    { "SMASH CHANCE",   "CHANCE TO BREAK NEIGHBORING OBJECTS",            UpgradeKind::Player,  5, 2, 5 },
    { "CRAFT SPEED",    "TRAIN CRAFTS TRACKS FASTER",                     UpgradeKind::Train,   4, 2, 5 },
    { "TRACK CAPACITY", "CRAFTER WAGON HOLDS MORE TRACKS",                UpgradeKind::Train,   3, 2, 4 },
    { "CARGO CAPACITY", "CARGO WAGON HOLDS MORE WOOD AND IRON",           UpgradeKind::Train,   3, 2, 4 },
    { "TRAIN BRAKE",    "THE TRAIN RUNS SLOWER (MORE TIME TO LAY RAILS)", UpgradeKind::Train,   5, 3, 5 },
    { "AUTO COLLECTOR", "WAGON AUTO-COLLECTS NEARBY ITEMS AND BOLTS",     UpgradeKind::Wagon,  14, 0, 1 },
    { "MINING WAGON",   "WAGON AUTO-MINES NEARBY TREES AND ROCKS",        UpgradeKind::Wagon,  16, 0, 1 },
    { "DYNAMITE WAGON", "CRAFTS DYNAMITE TO BLAST UNBREAKABLE ROCKS",     UpgradeKind::Wagon,  18, 0, 1 },
};

// level レベルの時に、次のレベルを買うのに必要なボルト
inline int UpgradeCostAt(Upgrade u, int level)
{
    const UpgradeInfo& info = kUpgradeInfo[(int)u];
    return info.baseCost + info.costStep * level;
}

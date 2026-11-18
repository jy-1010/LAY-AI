#pragma once
// =============================================================================
// Config.h : ゲーム全体の設定値。マジックナンバーはここに集約する。
//   ・数値を調整したい時は、まずこのファイルを見ること
//   ・単位: 長さ = マス (タイル1枚 = 1.0)、時間 = 秒、角度 = ラジアン
//   ・ワールド座標: x = 右、y = 上、z = 画面の奥 (左手系。Direct3D と同じ)
//   ・色は 0..1 の RGB
// =============================================================================
#include <cstdint>

namespace cfg {

struct Rgb  { float r, g, b; };            // 色 (不透明)
struct Rgba { float r, g, b, a; };         // 色 (a = 不透明度)
struct Vec2f { float x, z; };              // 地面上の位置

// 汎用の定数
constexpr float kHalf    = 0.5f;           // 「半分」(中心位置の計算用)
constexpr float kOpaque  = 1.0f;           // 不透明 (アルファ = 1)
constexpr float kPi      = 3.14159265f;
constexpr float kTwoPi   = kPi * 2.0f;
constexpr float kTileSize = 1.0f;          // タイル1枚の一辺

// ============================ ウィンドウ / 描画 ==============================
constexpr unsigned kWindowWidth  = 1280;   // ウィンドウの描画領域 (ピクセル)
constexpr unsigned kWindowHeight = 720;
constexpr float    kMaxFrameDt   = 0.05f;  // 1フレームの最大経過時間 (ウィンドウ移動などで dt が跳ねるのを防ぐ)
constexpr float    kFovY         = kPi / 4.0f;   // 垂直視野角 (45度)
constexpr float    kNearZ        = 0.1f;   // カメラのニアクリップ
constexpr float    kFarZ         = 200.0f; // カメラのファークリップ
constexpr Rgb      kSkyColor     = { 0.55f, 0.75f, 0.95f };   // 背景 (空) の色
constexpr unsigned kSwapChainBuffers = 1;  // スワップチェーンのバッファ数
constexpr unsigned kVSyncInterval    = 1;  // Present の垂直同期間隔 (1 = 60Hz同期)

// メッシュごとの最大インスタンス数 (バッファのサイズ。1フレームに描ける数の上限)
constexpr unsigned kMaxCubeInstances    = 16384;  // 地面・幹・線路・列車・建物・UI文字 (UI の文字は 1ドット = 1個で、HUD だけで数千個になる)
constexpr unsigned kMaxSphereInstances  = 1024;   // 岩・頭・影
constexpr unsigned kMaxPyramidInstances = 1024;   // 木の葉・屋根
constexpr unsigned kMaxHexInstances     = 512;    // ボルトの頭

// ============================ メッシュ生成 ===================================
namespace mesh {
constexpr float kHalfExtent   = 0.5f;      // 単位メッシュは 1x1x1 (中心が原点) なので、中心から面までは 0.5
constexpr int   kSphereSlices = 20;        // 球の経線の分割数
constexpr int   kSphereStacks = 12;        // 球の緯線の分割数
constexpr int   kHexSides     = 6;         // 六角柱の辺の数
constexpr int   kCubeFaces      = 6;         // 立方体の面の数
constexpr int   kQuadCorners    = 4;         // 四角形の頂点数
constexpr int   kPyramidSides   = 4;         // 四角錐の側面の数
constexpr int   kTriangleCorners = 3;        // 三角形の頂点数
constexpr float kVerticalNormalThreshold = 0.5f;   // 法線の y 成分がこれより大きい面は「ほぼ水平な面」とみなす
}

// ============================ ステージ =======================================
constexpr int kMapW       = 60;            // マップの幅 (マス)。x = 0 .. kMapW-1
constexpr int kMapH       = 15;            // マップの奥行き (マス)。z = 0 .. kMapH-1
constexpr int kMaxPlayers = 6;             // 同時プレイ人数の上限
constexpr int kMaxPads    = 4;             // XInput パッドの最大接続数
constexpr int kRailRow    = kMapH / 2;     // 線路が走る行 (z)
constexpr int kGateHalfRows = 1;           // ゴール駅の入口 = kRailRow ± この値 (3マス幅)
constexpr int kSafeMaxX   = 9;             // x がこれ以下のスタート地帯には資源・水・ボルトを置かない
constexpr int kGoalColumn = kMapW - 1;     // ゴール駅の入口がある列 (マップの右端)
constexpr int kResourceMinX = kSafeMaxX + 1;   // 木・岩・水・ボルトを置く x の範囲 (スタート地帯の次の列から…
constexpr int kResourceMaxX = kGoalColumn - 1; //                                     ゴール列の手前まで)
constexpr int kInitialRailLength = 9;      // 最初から敷いてある線路の長さ (x = 0 .. 8)
constexpr float kInitialTrainPos = 2.0f;   // 列車の初期位置 (線路上のマス番号)
constexpr int kInitialTracks     = 4;      // 各ステージの開始時に、レール作成車両に入っている線路の数

// 資源の配置 (確率は % )
constexpr int kPercent    = 100;           // 百分率の分母
constexpr int kTreeChance = 12;            // 草地1マスが木になる確率
constexpr int kRockChance = 11;            // 草地1マスが岩 (= 鉄の採取元) になる確率

// スタート駅 (左端)
constexpr int kStartStationCols = 2;                 // 建物の幅 (x = 0..1)
constexpr int kStartStationRows = 2;                 // 建物の奥行き
constexpr int kStartStationRow0 = kRailRow - 3;      // 建物の最初の行 (z)。線路の少し奥側
constexpr int kStartPlatformMaxX = 2;                // ホームは x = 0 .. この値
constexpr int kStartPlatformHalfRows = 1;            // ホームは線路の行 ± この値

// 水 (池)
constexpr int   kPondCount     = 5;        // 池の数
constexpr float kPondMinRadius = 1.0f;     // 池の半径 (最小)
constexpr float kPondMaxRadius = 2.2f;     // 池の半径 (最大)
constexpr int   kPondMinX = kSafeMaxX + 3; // 池の中心 x の範囲
constexpr int   kPondMaxX = kMapW - 4;
constexpr int   kPondMinZ = 1;             // 池の中心 z の範囲
constexpr int   kPondMaxZ = kMapH - 2;
constexpr int   kPondRetryMax = 12;        // ゴールに行けなくなる配置だった時の引き直し回数

// ボルト
constexpr int   kBoltCount       = 16;     // マップに置くボルトの数
constexpr int   kBoltMaxAttempts = 1000;   // 置き場所の抽選回数の上限
constexpr float kBoltPickupRadius = 0.6f;  // この距離まで近づくと拾える

// スコア
constexpr int kScorePerRail   = 10;        // 敷いた線路 1本あたり
constexpr int kScorePerBolt   = 50;        // ボルト 1個あたり
constexpr int kScoreClearBonus = 500;      // クリアボーナス

// ステージ進行 (ゴールすると次のステージへ。ステージが進むほど難しくなる)
constexpr float kStageSpeedStep = 0.05f;   // ステージが進むごとの列車の速度の上乗せ (初速・最高速とも)
constexpr float kTrainSpeedCap  = 1.60f;   // 最高速の上限
constexpr int   kPondPerStage   = 1;       // ステージが進むごとに増える池の数
constexpr int   kPondMaxCount   = 9;       // 池の数の上限

// 壊れない岩の塊 (採取できず通れない。ランダムウォークで数マスの塊にする)
constexpr int kBoulderClusters    = 3;     // 最初のステージの塊の数
constexpr int kBoulderPerStage    = 1;     // ステージが進むごとに増える塊の数
constexpr int kBoulderMaxClusters = 8;     // 塊の数の上限
constexpr int kBoulderClusterCells = 3;    // 1つの塊を作るマス数 (重なると小さくなる)

// 橋 (木を持っているプレイヤーが、水に向かって設置ボタンを押すと架けられる)
constexpr int kWoodPerBridge = 1;          // 橋1マスに必要な木 (プレイヤーが持っている木から使う)

// 物の持ち運びと貨車 (プレイヤーは 1種類の物しか持てない)
constexpr int   kCarryMax        = 3;      // プレイヤーが一度に持てる数 (同じ種類だけ)
constexpr int   kCargoCapacity   = 20;     // 貨物車両に預けられる数 (木・鉄それぞれ)
constexpr int   kCrafterCapacity = 8;      // レール作成車両に溜められる線路の数
constexpr float kCraftSeconds    = 2.0f;   // レール作成車両が線路1本を作る時間 (貨物車両に 木 + 鉄 がある間)
constexpr float kInteractReach   = 0.9f;   // 貨車に触れる距離 (プレイヤーの中心から向いている方向)
constexpr float kInteractPad     = 0.15f;  // 貨車に触れる判定を広げる余白

// 列車の当たり判定 (プレイヤーとの衝突)
constexpr int   kTrainCarCount = 3;        // 機関車 + 貨車2両 (後で定義する kCrafterWagonIndex + 1 と一致させる)
constexpr float kTrainCollisionPad = 0.05f;   // 判定を車体より少し大きくする
constexpr float kPushOutEpsilon    = 1e-4f;   // 中心が箱の内側かどうかの判定に使う微小値 (距離の2乗)
constexpr float kPushOutMargin     = 0.02f;   // 押し出す時に余分に離す距離 (すぐ再衝突しないように)
constexpr float kPushOutTolerance  = 0.005f;  // この程度のめり込みは「接している」とみなす (浮動小数の誤差対策)
constexpr int   kPushIterations    = 4;       // 1フレームに押し出しを繰り返す最大回数

// リスポーン (列車に挟まれて逃げ場が無かった時)
constexpr float kRespawnMinDist     = 1.0f;   // マルチ: 他のプレイヤーからこれ以上離れた所に出す
constexpr float kRespawnMaxDist     = 2.5f;   //         ただし この範囲の中で
constexpr float kRespawnTrainRadius = 3.0f;   // ソロ: 列車の先頭からこの範囲の中に出す
constexpr float kRespawnWidenStep   = 1.0f;   // 置ける場所が無い時に範囲を広げる幅
constexpr float kRespawnWidenMax    = 4.0f;   //                              広げる最大量

// ゴールラッシュ (線路がゴール駅まで繋がったら、列車が加速して一気にゴールする)
constexpr float kGoalRushAccel    = 4.0f;     // 加速 (マス/秒 毎秒)
constexpr float kGoalRushMaxSpeed = 6.0f;     // 最高速 (マス/秒)

// ============================ ゲームバランス =================================
constexpr float kPlayerSpeed  = 5.0f;      // プレイヤーの移動速度 (マス/秒)
constexpr float kPlayerRadius = 0.3f;      // 当たり判定の半径
constexpr float kTrainStartSpeed = 0.0f;  // 列車の初速 (マス/秒)
constexpr float kTrainAccel      = 0.004f; // 列車の加速 (マス/秒 毎秒)
constexpr float kTrainMaxSpeed   = 0.85f;  // 列車の最高速
constexpr float kWagonSpacing    = 1.1f;   // 車両どうしの間隔 (線路上のマス数)
constexpr int   kStorageWagonIndex = 1;    // 機関車から数えて何両目か (1両目 = 倉庫)
constexpr int   kCrafterWagonIndex = 2;    //                           (2両目 = 線路の加工車)
static_assert(kTrainCarCount == kCrafterWagonIndex + 1, "車両数と車両番号が合っていない");
constexpr int   kWoodPerTrack    = 1;      // 線路1本に必要な木
constexpr int   kIronPerTrack    = 1;      // 線路1本に必要な鉄
constexpr int   kInventoryShowMax = 8;     // 貨車の上に積んで表示する最大個数

// アップグレード (ステージクリア後にボルトで購入。購入すると 全員のプレイヤー / 列車 に効く)
namespace upgrade {
constexpr float kBaseMultiplier    = 1.0f;   // 強化前の倍率 (そのまま)
constexpr float kMoveSpeedStep     = 0.12f;  // プレイヤー: 移動速度 +12% / レベル
constexpr int   kCarryStep         = 1;      // プレイヤー: 持てる数 +1 / レベル
constexpr float kYieldChanceStep   = 0.15f;  // プレイヤー: 採取で 1個増える確率 +15% / レベル
constexpr float kSmashChanceStep   = 0.12f;  // プレイヤー: 採取した物の隣の 木・岩 も壊れる確率 +12% / レベル
constexpr float kCraftSpeedStep    = 0.25f;  // 列車: 線路を作る速さ +25% / レベル
constexpr int   kTrackCapacityStep = 2;      // 列車: 線路の積載量 +2 / レベル
constexpr int   kCargoCapacityStep = 10;     // 列車: 貨物の積載量 +10 / レベル (木・鉄それぞれ)
}

// プレイヤーの出現位置 (参加順)
inline constexpr Vec2f kSpawn[kMaxPlayers] = { { 8, 5 },{ 7, 5 },{ 8, 4 },{ 7, 4 } ,{ 7, 3 },{ 8, 3 } };
// プレイヤーの色 (青・赤・緑・黄・ 紫・黒)
inline constexpr Rgb kPlayerColors[kMaxPlayers] = {
    { 0.20f, 0.45f, 0.90f }, { 0.90f, 0.30f, 0.30f }, { 0.30f, 0.80f, 0.35f }, { 0.95f, 0.80f, 0.20f },{ 0.95f, 0.30f, 0.90f },{ 0.05f, 0.05f, 0.05 }
};

// ============================ カメラ =========================================
constexpr float kCamRange       = 11.0f;   // プレイヤーはカメラ中心から ±この距離までしか離れられない
constexpr float kCamMinX        = 11.0f;   // カメラ中心 x の最小値 (スタート駅が映る)
constexpr float kCamEndMargin   = 9.0f;    // カメラ中心 x の最大値 = kMapW - この値 (ゴール駅が映る)
constexpr float kCamFollowSpeed = 4.0f;    // カメラの追従の速さ (大きいほど速い)
constexpr float kCamHeight  = 15.0f;       // カメラの高さ (y)
constexpr float kCamEyeZ    = -2.0f;       // カメラの z (手前側)
constexpr float kCamTargetZ = (float)kRailRow;   // 注視点の z

// ============================ アニメーション =================================
namespace anim {
constexpr float kWaveSpeed = 2.0f;         // 水面の波の速さ
constexpr float kWaveFreqX = 0.7f;         // 水面の波の x 方向の細かさ
constexpr float kWaveFreqZ = 0.5f;         // 水面の波の z 方向の細かさ
constexpr float kWaveAmp   = 0.06f;        // 水面の明るさの揺れ幅
constexpr float kBoltSpinSpeed = 2.5f;     // ボルトの回転速度 (rad/秒)
constexpr float kBoltBobSpeed  = 3.0f;     // ボルトの上下運動の速さ
constexpr float kBoltBobAmp    = 0.12f;    // ボルトの上下運動の幅
constexpr float kBoltPhaseStep = 0.8f;     // ボルトごとの位相ずらし (動きをそろえない)
constexpr float kFlagSwaySpeed = 2.0f;     // 旗のなびく速さ
constexpr float kFlagSwayAmp   = 0.25f;    // 旗のなびく角度 (rad)
}

// ============================ 見た目 (サイズ / 色) ===========================
namespace vis {
// --- 地面 ---
constexpr float kTileThickness = 0.4f;     // 地面スラブの厚み (上面は y = 0)
constexpr int   kCheckerModulo = 2;        // 市松模様の周期
constexpr Rgb kGrassLight    = { 0.34f, 0.68f, 0.31f };
constexpr Rgb kGrassDark     = { 0.30f, 0.62f, 0.28f };
constexpr Rgb kPlatformColor = { 0.72f, 0.72f, 0.74f };   // 駅のホーム
constexpr Rgb kWaterColor    = { 0.20f, 0.45f, 0.85f };
constexpr float kWaterSink   = 0.08f;      // 水面は地面より少し低い
// --- 木 ---
constexpr float kTrunkWidth  = 0.25f;      // 幹の太さ
constexpr float kTrunkHeight = 0.9f;
constexpr Rgb   kTrunkColor  = { 0.45f, 0.28f, 0.12f };
constexpr float kLeavesLowSize   = 0.95f;  // 下段の葉 (四角錐): 幅
constexpr float kLeavesLowHeight = 0.9f;   //                    高さ
constexpr float kLeavesLowY      = 1.35f;  //                    中心の高さ
constexpr Rgb   kLeavesLowColor  = { 0.12f, 0.45f, 0.15f };
constexpr float kLeavesHighSize   = 0.70f; // 上段の葉
constexpr float kLeavesHighHeight = 0.70f;
constexpr float kLeavesHighY      = 1.85f;
constexpr Rgb   kLeavesHighColor  = { 0.18f, 0.55f, 0.20f };
// --- 岩 ---
constexpr float kRockWidth  = 0.85f;
constexpr float kRockHeight = 0.65f;
constexpr float kRockY      = 0.32f;
constexpr Rgb   kRockColor  = { 0.50f, 0.53f, 0.60f };
constexpr float kOreSize    = 0.30f;       // 岩に埋まった鉄鉱石のこぶ
constexpr float kOreOffsetX = 0.18f;
constexpr float kOreOffsetZ = -0.10f;
constexpr float kOreY       = 0.45f;
constexpr Rgb   kOreColor   = { 0.90f, 0.60f, 0.35f };
// --- 壊れない岩の塊 (壊せる岩より大きく暗い。上に青い結晶が生えている) ---
constexpr float kBoulderSize   = 1.05f;    // 本体の球 (幅)
constexpr float kBoulderHeight = 1.0f;
constexpr float kBoulderY      = 0.45f;
constexpr Rgb   kBoulderColor  = { 0.33f, 0.34f, 0.40f };
constexpr float kBoulderBumpSize    = 0.55f;   // 横のこぶ
constexpr float kBoulderBumpOffsetX = 0.28f;
constexpr float kBoulderBumpOffsetZ = -0.22f;
constexpr float kBoulderBumpY       = 0.80f;
constexpr Rgb   kBoulderBumpColor   = { 0.42f, 0.43f, 0.50f };
constexpr float kBoulderSpikeSize   = 0.35f;   // 結晶 (壊せない目印)
constexpr float kBoulderSpikeHeight = 0.6f;
constexpr float kBoulderSpikeY      = 1.0f;
constexpr Rgb   kBoulderSpikeColor  = { 0.55f, 0.70f, 0.95f };
// --- 橋 (水の上の木の床。四隅に杭) ---
constexpr float kBridgeDeckThickness = 0.12f;
constexpr float kBridgeDeckY         = -0.01f;   // 床の中心の高さ (上面が y = 0.05)
constexpr Rgb   kBridgeDeckColor     = { 0.62f, 0.45f, 0.25f };
constexpr int   kBridgeSlatCount     = 4;        // 床板の本数
constexpr float kBridgeSlatWidth     = 0.96f;
constexpr float kBridgeSlatHeight    = 0.03f;
constexpr float kBridgeSlatThickness = 0.10f;
constexpr float kBridgeSlatStep      = 0.25f;    // 床板の間隔
constexpr float kBridgeSlatY         = 0.065f;
constexpr Rgb   kBridgeSlatColor     = { 0.52f, 0.36f, 0.18f };
constexpr float kBridgePostSize   = 0.10f;       // 四隅の杭
constexpr float kBridgePostHeight = 0.22f;
constexpr float kBridgePostOffset = 0.42f;
constexpr float kBridgePostY      = 0.11f;
constexpr Rgb   kBridgePostColor  = { 0.40f, 0.27f, 0.12f };
// --- 柵 (ゴール駅の入口以外の列) ---
constexpr float kFenceThickness = 0.30f;
constexpr float kFenceHeight    = 0.70f;
constexpr Rgb   kFenceColor     = { 0.55f, 0.42f, 0.28f };
// --- 線路 ---
constexpr float kSleeperSize      = 0.9f;  // 枕木 (正方形)
constexpr float kSleeperThickness = 0.05f;
constexpr float kSleeperY         = 0.05f;
constexpr Rgb   kSleeperColor     = { 0.55f, 0.38f, 0.20f };
constexpr float kRailWidth  = 0.07f;       // レール
constexpr float kRailHeight = 0.07f;
constexpr float kRailLength = 1.0f;
constexpr float kRailGauge  = 0.22f;       // 中心からレールまでの距離
constexpr float kRailY      = 0.10f;
constexpr Rgb   kRailColor  = { 0.72f, 0.72f, 0.75f };
// --- 列車 ---
constexpr float kEngineWidth = 0.70f, kEngineHeight = 0.50f, kEngineLength = 1.00f, kEngineY = 0.40f;
constexpr Rgb   kEngineColor = { 0.80f, 0.20f, 0.20f };
constexpr float kCabWidth = 0.62f, kCabHeight = 0.40f, kCabLength = 0.40f, kCabY = 0.80f, kCabZ = -0.25f;
constexpr Rgb   kCabColor = { 0.55f, 0.12f, 0.12f };
constexpr float kChimneyWidth = 0.16f, kChimneyHeight = 0.35f, kChimneyY = 0.85f, kChimneyZ = 0.30f;
constexpr Rgb   kChimneyColor = { 0.10f, 0.10f, 0.10f };
constexpr float kWagonWidth = 0.70f, kWagonHeight = 0.45f, kWagonLength = 0.95f, kWagonY = 0.35f;
constexpr Rgb   kStorageWagonColor = { 0.60f, 0.45f, 0.20f };    // 1両目: 木と鉄の倉庫
constexpr Rgb   kCrafterWagonColor = { 0.20f, 0.55f, 0.60f };    // 2両目: 線路の加工車
constexpr float kCargoSize    = 0.16f;     // 積み荷 (木・鉄) の立方体
constexpr float kCargoLength  = 0.09f;
constexpr float kCargoY       = 0.66f;
constexpr float kCargoOffsetX = 0.15f;     // 木は左列(-)、鉄は右列(+)
constexpr float kCargoZStart  = -0.38f;    // 積み荷を並べ始める位置
constexpr float kCargoZStep   = 0.10f;
constexpr Rgb   kIronColor    = { 0.60f, 0.65f, 0.75f };
constexpr float kTrackItemWidth = 0.50f, kTrackItemHeight = 0.07f, kTrackItemY = 0.62f;   // 積まれた線路
constexpr Rgb   kTrackItemColor = { 0.80f, 0.62f, 0.35f };
// --- レール作成車両の進行バー (車体の側面) ---
constexpr float kCraftBarSideX    = 0.36f;     // 車体の側面の外側
constexpr float kCraftBarY        = 0.32f;
constexpr float kCraftBarThickness = 0.04f;
constexpr float kCraftBarFillThickness = 0.06f;   // 塗り部分は背景より少し厚くして重なりのちらつきを防ぐ
constexpr float kCraftBarHeight   = 0.08f;
constexpr float kCraftBarLength   = 0.80f;     // 満タンの長さ
constexpr float kCraftBarZ0       = -0.40f;    // バーの始まり (車両の後ろ側)
constexpr Rgb   kCraftBarColor    = { 0.30f, 1.00f, 0.50f };
constexpr Rgb   kCraftBarBgColor  = { 0.10f, 0.25f, 0.28f };
// --- プレイヤーが持っている物 (頭の上に積んで表示) ---
constexpr float kHeldItemSize  = 0.24f;        // 木・鉄の立方体
constexpr float kHeldItemStep  = 0.20f;        // 積む間隔
constexpr float kHeldItemBaseY = 1.62f;        // 最下段の高さ
constexpr float kHeldTrackWidth = 0.40f, kHeldTrackHeight = 0.08f, kHeldTrackLength = 0.24f;   // 線路 (平たい板)
// --- 地面に置かれた物 / 壊れた物が落とした物 (小さな山にして表示) ---
constexpr float kGroundItemSize    = 0.22f;
constexpr float kGroundItemSpacing = 0.26f;    // 山を作る間隔
constexpr float kGroundItemBaseY   = 0.14f;    // 一番下の高さ
constexpr int   kGroundItemColumns = 2;        // 1段に並べる数 (2 x 2)
constexpr int   kGroundItemShowMax = 12;       // 表示する最大個数
// --- プレイヤー ---
constexpr float kPlayerBodyWidth = 0.45f, kPlayerBodyHeight = 0.65f, kPlayerBodyDepth = 0.35f, kPlayerBodyY = 0.40f;
constexpr float kPlayerHeadSize = 0.38f, kPlayerHeadY = 0.90f;
constexpr Rgb   kSkinColor = { 0.95f, 0.78f, 0.60f };
constexpr float kPlayerNoseWidth = 0.12f, kPlayerNoseHeight = 0.12f, kPlayerNoseLength = 0.20f;   // 向きの目印
constexpr float kPlayerNoseY = 0.90f, kPlayerNoseZ = 0.22f;
constexpr Rgb   kNoseColor = { 1.00f, 0.40f, 0.20f };
constexpr float kMarkerWidth = 0.30f, kMarkerThickness = 0.06f, kMarkerY = 1.30f;   // 頭上のプレイヤー色の目印
// --- 向いているセルのハイライト ---
constexpr float kHighlightSize = 0.95f;
constexpr float kHighlightThickness = 0.02f;
constexpr float kHighlightY = 0.045f;
constexpr float kHighlightLayerStep = 0.004f;   // 複数人が同じセルを向いても重ならないよう、人ごとに少し持ち上げる
constexpr Rgb   kHighlightPlaceColor   = { 0.60f, 1.00f, 0.60f };   // 線路を置ける
constexpr Rgb   kHighlightHarvestColor = { 1.00f, 0.90f, 0.40f };   // 採取できる
constexpr Rgb   kHighlightBridgeColor  = { 0.40f, 0.75f, 1.00f };   // 橋を架けられる
// --- 駅 ---
constexpr float kStationWallHeight = 1.2f;       // スタート駅の壁の高さ
constexpr Rgb   kStationWallColor  = { 0.85f, 0.75f, 0.60f };
constexpr Rgb   kStationRoofColor  = { 0.75f, 0.25f, 0.20f };
constexpr float kRoofOverhang   = 0.3f;          // 屋根が壁からはみ出す量 (片側)
constexpr float kRoofGrow       = kRoofOverhang * 2.0f;   // 屋根の幅は壁より両側ぶん大きい
constexpr float kStartRoofHeight = 0.9f;
constexpr float kDoorThickness  = 0.06f;
constexpr Rgb   kDoorColor      = { 0.30f, 0.18f, 0.10f };
constexpr float kStartDoorWidth = 0.6f, kStartDoorHeight = 0.8f;
constexpr float kSignHeight     = 0.30f;         // 看板
constexpr float kStartSignWidth = 1.2f;
constexpr Rgb   kSignColor      = { 0.95f, 0.78f, 0.20f };
constexpr float kGoalDepth      = 3.0f;          // ゴール駅 (マップの右端の外側に建つ)
constexpr float kGoalWidth      = 5.0f;
constexpr float kGoalWallHeight = 1.8f;
constexpr float kGoalRoofHeight = 1.2f;
constexpr float kGoalDoorWidth  = 2.0f, kGoalDoorHeight = 1.2f;
constexpr float kPoleWidth = 0.08f, kPoleHeight = 1.2f;    // 旗ざお
constexpr Rgb   kPoleColor = { 0.60f, 0.60f, 0.62f };
constexpr float kFlagLength = 0.7f, kFlagHeight = 0.4f, kFlagThickness = 0.04f;
constexpr Rgb   kFlagColor = { 0.90f, 0.25f, 0.20f };
// --- ボルト ---
constexpr float kBoltBaseY = 0.55f;              // 浮いている高さの基準
constexpr float kBoltHeadSize = 0.36f, kBoltHeadHeight = 0.14f, kBoltHeadY = 0.12f;      // 六角の頭 (基準からの高さ)
constexpr float kBoltShaftWidth = 0.14f, kBoltShaftHeight = 0.34f, kBoltShaftY = -0.12f; // ねじ部
constexpr Rgb   kBoltHeadColor  = { 1.00f, 0.82f, 0.15f };
constexpr Rgb   kBoltShaftColor = { 0.85f, 0.65f, 0.10f };
constexpr float kBoltShadowSize = 0.45f, kBoltShadowHeight = 0.02f, kBoltShadowY = 0.03f;
constexpr Rgb   kBoltShadowColor = { 0.12f, 0.30f, 0.12f };
}

// ============================ 入力 (メニュー操作) ===========================
namespace input {
constexpr float kMenuRepeatDelay    = 0.40f;  // 押しっぱなしで連続入力が始まるまでの時間
constexpr float kMenuRepeatInterval = 0.08f;  // 連続入力の間隔
constexpr float kMenuStickThreshold = 0.5f;   // スティックを「倒した」とみなす大きさ (0..1)
}

// ============================ 設定 / サウンド ================================
namespace audio {
constexpr int   kVolumeSteps = 10;            // 音量の段階数 (0 = 無音, 10 = 最大)
constexpr float kVolumeCurve = 2.0f;          // 段階 → 音量の曲線 (2乗: 小さい側を細かく)
constexpr unsigned kSampleRate = 44100;       // サンプリング周波数 (Hz)
constexpr int   kSfxVoices   = 16;            // 効果音の同時発音数
// 合成する音の大きさ (0..1)
constexpr float kSfxAmplitude  = 0.35f;
constexpr float kLeadAmplitude = 0.16f;       // BGM: メロディ
constexpr float kBassAmplitude = 0.20f;       // BGM: ベース
constexpr float kHatAmplitude  = 0.05f;       // BGM: ハイハット
constexpr float kAttackSeconds  = 0.004f;     // 音の立ち上がり (プチノイズ防止)
constexpr float kReleaseSeconds = 0.012f;     // 音の終わりの減衰
constexpr float kPulseDuty = 0.25f;           // パルス波のデューティ比 (メロディ用)
constexpr float kNoteGateRatio = 0.92f;       // 音符の長さのうち実際に鳴らす割合 (隙間を作る)
constexpr float kBgmLeadDecay = 3.0f;         // メロディの減衰の速さ
constexpr float kBgmBassDecay = 1.5f;         // ベースの減衰の速さ
constexpr float kHatSeconds   = 0.03f;        // ハイハットの長さ
constexpr float kHatDecay     = 60.0f;        // ハイハットの減衰の速さ
constexpr float kBgmMenuBpm = 90.0f;          // タイトル/設定のBGMのテンポ
constexpr float kBgmPlayBpm = 132.0f;         // ゲーム中のBGMのテンポ
constexpr int   kMidiA4 = 69;                 // MIDIノート番号 (A4 = 440Hz)
constexpr float kFreqA4 = 440.0f;
constexpr int   kSemitonesPerOctave = 12;
constexpr float kTriangleGain = 4.0f;         // 三角波: 位相 0..0.5 を -1..1 に広げる倍率
constexpr uint32_t kNoiseSeed = 0x12345678u;  // ノイズ用乱数の種
constexpr float kTailSeconds = 0.05f;         // 効果音の最後に足す余韻の長さ
constexpr float kSampleToInt = 32767.0f;      // -1..1 → 16bit整数
}

namespace settings {
constexpr bool kDefaultFullscreen = false;
constexpr bool kDefaultVsync      = true;
constexpr bool kDefaultShowFps    = false;
constexpr int  kDefaultMasterVolume = 8;      // 0..audio::kVolumeSteps
constexpr int  kDefaultMusicVolume  = 6;
constexpr int  kDefaultSfxVolume    = 8;
}
constexpr unsigned kMinimizedSleepMs = 50;    // 最小化中の待ち時間 (CPUを使い続けないように)

// ============================ UI =============================================
namespace ui {
// UI は縦 kVirtualHeight の仮想座標で配置する (横幅は画面の縦横比で決まる)。
// 画面サイズ (フルスクリーンなど) が変わっても、配置と文字の大きさは同じ比率で拡縮される
constexpr float kVirtualHeight = 720.0f;

// 文字 (5x7 のドット文字を立方体のインスタンスで描く)
constexpr int   kGlyphCols    = 5;         // 1文字の横ドット数
constexpr int   kGlyphRows    = 7;         // 縦ドット数
constexpr int   kGlyphAdvance = 6;         // 次の文字までの送り (横ドット数 + 字間1)
constexpr float kPixelFill    = 0.92f;     // 1ドットの大きさ (隣との隙間を少し空ける)
constexpr float kShadowAlpha  = 0.55f;     // 文字の影の濃さ
constexpr float kShadowOffset = 0.5f;      // 影のずれ (ドット単位)
constexpr float kRectThickness = 0.04f;    // UI 矩形の奥行きの厚み
// 奥行き (正射影の z: 0 = 手前, 1 = 奥)。半透明は遠い物から Submit する
constexpr float kZDim    = 0.9f;           // 暗幕
constexpr float kZShadow = 0.6f;           // 文字の影
constexpr float kZText   = 0.5f;           // 文字
constexpr float kZFade   = 0.1f;           // フェード用の暗幕 (最前面)

// 文字色
constexpr Rgb kColorGold  = { 1.00f, 0.85f, 0.30f };
constexpr Rgb kColorWhite = { 1.00f, 1.00f, 1.00f };
constexpr Rgb kColorGray  = { 0.80f, 0.85f, 0.90f };
constexpr Rgb kColorRed   = { 1.00f, 0.35f, 0.30f };
constexpr Rgb kColorGreen = { 0.55f, 1.00f, 0.55f };
constexpr Rgb kColorBarOff = { 0.25f, 0.28f, 0.32f };  // 音量バーの空きマス
// 暗幕の色 (a = 濃さ)
constexpr Rgba kTitleDim = { 0.00f, 0.00f, 0.00f, 0.45f };
constexpr Rgba kClearDim = { 0.00f, 0.12f, 0.04f, 0.55f };
constexpr Rgba kOverDim  = { 0.18f, 0.00f, 0.00f, 0.60f };
constexpr Rgba kPauseDim    = { 0.00f, 0.00f, 0.00f, 0.55f };
constexpr Rgba kSettingsDim = { 0.00f, 0.00f, 0.00f, 0.70f };

// シーン遷移 / 演出
constexpr float kFadeTime    = 0.40f;      // 暗転/明転にかける時間
constexpr float kResultDelay = 1.20f;      // ゲーム終了から結果画面に切り替わるまで
constexpr float kOrthoNearZ = 0.0f;     // UI の正射影の手前/奥の z
constexpr float kOrthoFarZ  = 1.0f;
constexpr float kBlinkPeriod = 1.2f;       // 「PRESS ...」の点滅の周期
constexpr float kBlinkOnTime = 0.8f;       // 周期のうち表示している時間
constexpr float kTitleScrollSpeed = 3.0f;  // タイトル背景のスクロール速度 (マス/秒)

// タイトル画面のレイアウト (y = 画面上端からのピクセル, px = 1ドットの大きさ)
constexpr float kTitleLogoY = 130, kTitleLogoPx = 16;
constexpr float kTitleSubY = 280, kTitleSubPx = 5;
constexpr float kTitleHint1Y = 600, kTitleHint2Y = 635, kTitleHintPx = 3;
// ゲーム中のHUD
constexpr float kHudX = 20;
constexpr float kHudLine1Y = 16, kHudLine1Px = 4;
constexpr float kHudLine2Y = 52, kHudLine2Px = 3;
constexpr float kHudPlayerTopY = 100;      // プレイヤーごとの「持ち物」表示の1行目
constexpr float kHudPlayerStep = 30, kHudPlayerPx = 3;
constexpr float kHudSwatchSize = 18, kHudSwatchGap = 10;   // 色見本の四角の大きさ / 文字との間隔
constexpr float kHudBottomMargin = 36;
constexpr float kHudHintStep = 28;         // 画面下の操作ヒントの行間     // 画面下端から「JOIN」表示までの距離
constexpr float kBannerY = 260, kBannerPx = 12;   // 「DERAILED! / GOAL!」「STAGE n」
constexpr float kStageBannerTime = 1.5f;          // ステージ開始時に「STAGE n」を出しておく時間
constexpr float kBannerPulseSpeed = 8.0f;         // 「GOAL RUSH!!」の脈動の速さ
constexpr float kBannerPulseAmp   = 0.08f;        // 脈動の大きさ (文字の大きさの ±8%)
// 結果画面
constexpr float kResultTitleY = 130, kResultTitlePx = 16;
constexpr float kResultLine1Y = 290, kResultLine1Px = 6;   // 距離 / 線路の数
constexpr float kResultLine2Y = 350, kResultLine2Px = 5;   // ボルト
constexpr float kResultLine3Y = 410, kResultLine3Px = 6;   // スコア
constexpr float kResultLine4Y = 470, kResultLine4Px = 4;   // 人数
constexpr float kResultPromptY = 540, kResultPromptPx = 4;
// メニュー (タイトル / ポーズ)
constexpr float kMenuTopY = 380, kMenuStep = 64, kMenuPx = 5;       // タイトルのメニュー
constexpr float kPauseTitleY = 130, kPauseTitlePx = 12;             // 「PAUSED」
constexpr float kPauseMenuTopY = 290;                               // ポーズのメニュー (間隔・大きさは kMenuStep / kMenuPx)
constexpr float kPauseHintY = 600, kHintPx = 3;                     // 操作ヒント
// 強化ショップ
constexpr float kShopTitleY = 40, kShopTitlePx = 9;
constexpr float kShopBoltsY = 120, kShopBoltsPx = 5;
constexpr float kShopTopY = 170, kShopRowStep = 40;
constexpr float kShopTextPx = 4, kShopHeaderPx = 3;
constexpr float kShopPanelWidth = 900;                              // 項目を並べる幅 (画面中央に置く)
constexpr float kShopBarOffset = 360, kShopCostOffset = 640;        // 左端から レベルバー / 値段 までの距離
constexpr float kShopCellW = 22, kShopCellH = 22, kShopCellGap = 6; // レベルバーの1マス
constexpr float kShopDescY = 630, kShopHintY = 675;
// 設定画面
constexpr float kSettingsTitleY = 60, kSettingsTitlePx = 10;
constexpr float kSettingsTopY = 190, kSettingsRowStep = 58, kSettingsTextPx = 4;
constexpr float kSettingsPanelWidth = 820;                          // 項目を並べる幅 (画面中央に置く)
constexpr float kSettingsValueOffset = 520;                         // 左端から値の列までの距離
constexpr float kVolumeCellW = 24, kVolumeCellH = 26, kVolumeCellGap = 6;   // 音量バーの1マス
constexpr float kVolumeNumberGap = 14;                              // バーと数字の間
constexpr float kSettingsHintY = 650;
constexpr float kFpsMargin = 16, kFpsPx = 3;                        // 画面右上の FPS 表示
constexpr float kFpsInterval = 0.5f;                                 // FPS を更新する間隔 (秒)
constexpr float kJoinPromptPx = 6;                                  // 「PRESS ... TO JOIN」
}

} // namespace cfg

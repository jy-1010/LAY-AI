#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <vector>
#include "Config.h"
#include "Renderer.h"
#include "Upgrade.h"

// Unrailed! 風のプロトタイプ（最大4人のローカル協力プレイ）
//   - プレイヤーは 1種類の物 (木 / 鉄 / 線路) しか持てない。持っている物は頭の上とHUDに表示される
//   - 木・岩を採取して持つ → 貨物車両に預ける → レール作成車両が 木 + 鉄 から線路を作る
//     → 線路を取り出して持つ → 線路の先端に置く
//   - 持っている物は地面に置ける。地面の物は採取ボタンで拾える
//   - ステージクリア後のショップで、ボルトを使ってプレイヤー / 列車を強化できる
//   - 走り続ける列車の前に線路を敷き続ける。線路が途切れたらゲームオーバー
//   - 右端のゴール駅の入口まで線路を繋げばクリア → 次のステージへ (だんだん難しくなる)
//   - ステージ: 木 / 岩 / 壊れない岩の塊 / 水(木を持っていれば橋を架けられる) / 駅 / ボルト(拾うとスコア)
//   - 列車は固く、プレイヤーは車両をすり抜けられない (押し出される)

struct Input {                      // 1人ぶんの入力 (キーボード/パッド共通)
    float moveX = 0, moveZ = 0;     // 移動方向 -1..1 (アナログ対応)
    bool  harvest = false;          // 採取ボタン: 木・岩を拾う / 貨物車両に預ける / レール作成車両から線路を取り出す
    bool  place   = false;          // 設置ボタン: 線路を置く / 水の上なら橋を架ける
    bool  drop    = false;          // 置くボタン: 持っている物を向いているマスの地面に置く
    bool  restart = false;          // 今のステージをやり直す (キーボード R)
};

struct IVec2 { int x = 0, z = 0; }; // マス座標

// プレイヤーが持てる物
enum class Item { None, Wood, Iron, Track };
struct HeldItem {                   // 持っている物 (1種類だけ。count 個)
    Item item  = Item::None;
    int  count = 0;
};

// マスの種類
enum class Tile : uint8_t {
    Grass,      // 草地 (歩ける / 線路を置ける)
    Tree,       // 木 (採取で木を得る。通れない)
    Rock,       // 岩 (採取で鉄を得る。通れない)
    HardRock,   // 壊れない岩の塊 (採取できない。通れない)
    Water,      // 水 (通れない / 線路も置けない。木があれば橋を架けられる)
    Bridge,     // 橋 (歩ける / 線路を置ける)
    Platform,   // 駅のホーム (歩ける / 線路を置ける)
    Building,   // スタート駅の建物 (通れない)
    Fence,      // ゴール駅の柵 (通れない)
};
enum class State { Playing, GameOver, Clear };

// ゲーム内で起きた出来事 (効果音などの演出用。Game は音を知らず、App が TakeEvents() で受け取って鳴らす)
enum class GameEvent {
    Harvest,        // 木・岩を採取した
    PlaceTrack,     // 線路を置いた
    BuildBridge,    // 橋を架けた
    Bolt,           // ボルトを拾った
    Deposit,        // 貨物車両に預けた
    Pickup,         // レール作成車両から線路を取り出した
    Craft,          // レール作成車両が線路を1本作った
    Deny,           // できない操作 (手がふさがっている・材料が無い など)
    Drop,           // 持っている物を地面に置いた
    Smash,          // 採取の巻き添えで隣の木・岩が壊れた
    Pushed,         // 列車に押された (押され始めた時だけ)
    Respawn,        // 挟まれてリスポーンした
    RushStart,      // ゴールラッシュが始まった
    Clear,          // ステージクリア
    GameOver,       // ゲームオーバー
};

class GamePainter;                  // 描画ヘルパ (Game.cpp 内で定義)

class Game {
public:
    static constexpr int W = cfg::kMapW;                 // マップの幅
    static constexpr int H = cfg::kMapH;                 // マップの奥行き
    static constexpr int MaxPlayers = cfg::kMaxPlayers;
    // 描画に使うメッシュのID (Renderer::AddMesh の戻り値)
    struct Meshes { int cube = -1, sphere = -1, pyramid = -1, hex = -1; };
    struct GroundItem {                     // 地面に落ちている物 (1マスに1山)
        IVec2    cell;
        HeldItem stack;
    };
    struct CarBox {                         // 列車の車両 1両ぶんの当たり判定 (向き付きの箱)
        float x = 0, z = 0, yaw = 0;        // 中心の位置と向き
        float halfW = 0, halfL = 0;         // 幅・長さの半分 (判定の余白を含む)
    };

    // ---- ステージの開始 ----
    void Reset(uint32_t seed);              // 最初から (ステージ1・スコア0)。参加中のプレイヤーは維持
    void NextStage(uint32_t seed);          // クリア後に次のステージへ (スコア・ボルト・貨物・持ち物は引き継ぐ)
    void RestartStage(uint32_t seed);       // 今のステージをやり直す (ステージ開始時の状態に戻す)

    int  AddPlayer();                       // 空きスロットに参加 (戻り値 = プレイヤー番号, 満員なら -1)
    void RemovePlayer(int idx);
    int  PlayerCount() const;

    void Update(float dt, const Input in[MaxPlayers]);   // ゲームの進行 (Playing 中だけ呼ぶ)
    void Animate(float dt) { animTime_ += dt; }          // 水・ボルト・旗のアニメ用の時計 (毎フレーム呼ぶ)
    void Render(Renderer& r, const Meshes& m) const;     // 世界を Submit する
    void Camera(DirectX::XMFLOAT3& eye, DirectX::XMFLOAT3& target) const;

    // シーン/HUD から参照する情報
    State GetState() const { return state_; }
    int   Stage() const    { return stage_; }
    int   Wood() const     { return cargoWood_; }        // 貨物車両に積んである木
    int   Iron() const     { return cargoIron_; }        // 貨物車両に積んである鉄
    int   Tracks() const   { return crafterTracks_; }    // レール作成車両にある線路 (取り出せる数)
    bool  PlayerActive(int i) const { return players_[i].active; }
    HeldItem PlayerHeld(int i) const { return players_[i].held; }   // プレイヤー i が持っている物
    int   Bolts() const    { return bolts_; }            // 使えるボルト (ショップで消費する)
    int   PathLength() const { return (int)path_.size(); }
    int   Score() const;                                 // 通算スコア (前のステージぶん + 今のステージ)
    float CraftProgress() const;                         // 線路を作っている途中の進み具合 0..1

    // ---- 強化 (ステージクリア後のショップ) ----
    int  UpgradeLevel(Upgrade u) const { return upgradeLevel_[(int)u]; }
    int  UpgradeCost(Upgrade u) const;                   // 次のレベルの値段 (最大レベルなら -1)
    bool CanBuy(Upgrade u) const;                        // 最大レベルでなく、ボルトが足りるか
    bool BuyUpgrade(Upgrade u);                          // 購入する (買えたら true)
    // 強化を反映した、今の性能
    float MoveSpeedMultiplier() const;                   // 移動速度の倍率
    int   CarryMax() const;                              // 一度に持てる数
    float YieldChance() const;                           // 採取で 1個増える確率 (0..1)
    float SmashChance() const;                           // 採取した物の隣の木・岩が壊れる確率 (0..1)
    float CraftSeconds() const;                          // 線路を1本作る時間
    int   CrafterCapacity() const;                       // レール作成車両の線路の積載量
    int   CargoCapacity() const;                         // 貨物車両の積載量 (木・鉄それぞれ)
    float TrainSpeed() const;               // 今の列車の速さ (ゴールラッシュ中は加速した値)
    bool  IsRushing() const;                // 線路がゴールまで繋がり、列車が全速力で向かっているか
    void  SetCameraX(float x) { camX_ = x; }             // タイトルのデモ表示用

    // 前回から起きた出来事を受け取る (受け取ると空になる)
    std::vector<GameEvent> TakeEvents() { std::vector<GameEvent> e; e.swap(events_); return e; }

private:
    struct Player {
        bool  active = false;
        float px = 0, pz = 0;               // 位置 (セル中心 = 整数座標)
        IVec2 face{ 1, 0 };                 // 向き
        bool  pushed = false;               // 前のフレームで列車に押されていたか (押され始めの検出用)
        HeldItem held;                      // 持っている物
    };

    // --- ステージ生成 ---
    void StartStage(uint32_t seed);         // 現在の stage_ でステージを作って開始状態にする
    void Emit(GameEvent e) { events_.push_back(e); }
    uint32_t Rand();
    float    Rand01();                      // 0 以上 1 未満の乱数
    void BuildStage();                      // 地形・線路・水・岩の塊・資源・ボルトをまとめて作る
    void ClearMap();                        // 全部草地にして、駅を置く
    void PlacePonds();
    void PlaceBoulders();
    void RemoveHazards();                   // 水と岩の塊を取り除く (配置の引き直し用)
    bool GoalReachable() const;             // 線路の先端からゴール駅の入口まで行けるか (水は橋で渡れる扱い)
    void ScatterResources();                // 木と岩
    void ScatterBolts();
    static bool IsGateRow(int z);           // ゴール駅の入口の行か
    bool IsGoal(IVec2 c) const;             // ゴール駅の入口のマスか

    // --- 判定 ---
    bool InBounds(IVec2 c) const { return c.x >= 0 && c.z >= 0 && c.x < W && c.z < H; }
    bool Solid(float x, float z) const;
    bool Blocked(float x, float z) const;   // 体の四隅のどれかが歩けないマスに入るか
    void TryMove(Player& p, float dx, float dz);
    void CollectBolts(const Player& p);
    IVec2 FaceCell(const Player& p) const;
    bool CanPlace(IVec2 c) const;           // 線路を置けるマスか
    // --- 持ち運びと貨車 ---
    bool CanCarry(const Player& p, Item it) const;   // 手が空いている / 同じ種類で持てる数に余裕がある
    void Give(Player& p, Item it);                   // 1個持たせる
    void TakeOne(Player& p);                         // 1個減らす (0個になったら手ぶら)
    int  FacedWagon(const Player& p) const;          // 向いている先の貨車 (貨物 / レール作成)。無ければ 0
    void DepositToCargo(Player& p);                  // 持っている木・鉄を貨物車両に預ける
    void TakeTracks(Player& p);                      // レール作成車両から線路を取り出す
    void HarvestFacing(Player& p);                   // 向いているマスの 落ちている物 / 木・岩 を拾う
    void DropHeld(Player& p);                        // 持っている物を向いているマスの地面に置く

    // --- 地面の物 ---
    int  FindGroundItem(IVec2 c) const;              // マス c にある山の番号 (無ければ -1)
    void AddGroundItem(IVec2 c, Item it, int count); // 地面に置く (同じ種類の山があれば合わせる)
    bool PickUpGround(Player& p, int index);         // 山から持てるだけ拾う
    void SmashNeighbors(IVec2 c);                    // 強化の確率で、c の隣の木・岩も壊す (壊れた物は地面に落ちる)
    void PlaceFacing(Player& p);                     // 向いているマスに橋を架ける / 線路を置く
    int  StageRailScore() const;
    void TrainPoint(float t, float& x, float& z, float& yaw) const;
    void GetCars(CarBox cars[cfg::kTrainCarCount]) const;
    void ResolveTrainCollisions();          // プレイヤーを車両の外へ押し出す
    const CarBox* FirstCarOverlap(const CarBox cars[cfg::kTrainCarCount], float x, float z, float radius) const;
    void RespawnPlayer(int idx);            // 挟まれて逃げ場が無い時: マルチ=他のプレイヤーの近く / ソロ=列車の近く
    bool PickRespawnSpot(const CarBox cars[cfg::kTrainCarCount], float cx, float cz,
                         float minDist, float maxDist, float& outX, float& outZ);

    // --- 描画 ---
    void DrawTerrain(GamePainter& p) const;
    void DrawStations(GamePainter& p) const;
    void DrawBolts(GamePainter& p) const;
    void DrawTrack(GamePainter& p) const;
    void DrawTrain(GamePainter& p) const;
    void DrawPlayers(GamePainter& p) const;
    void DrawGroundItems(GamePainter& p) const;

    Tile  map_[H][W] = {};
    bool  onPath_[H][W] = {};
    std::vector<IVec2> path_;           // 線路のセル列 (列車はこの上を進む)
    std::vector<IVec2> boltItems_;      // まだ拾われていないボルトの位置

    Player players_[MaxPlayers];
    int   cargoWood_ = 0, cargoIron_ = 0;            // 貨物車両の中身 (チーム共有)
    int   crafterTracks_ = 0;                        // レール作成車両にある線路
    float craftTimer_ = 0;                           // 線路を1本作るまでの経過時間
    int   bolts_ = 0;                                // 使えるボルト (チーム共有。ショップで消費する)
    int   boltsCollected_ = 0;                       // これまでに拾ったボルトの総数 (スコア用。消費しても減らない)
    std::vector<GroundItem> groundItems_;            // 地面に落ちている物
    int   upgradeLevel_[kUpgradeCount] = {};         // 購入済みの強化のレベル (ゲームを最初からやり直すまで続く)

    // ステージ進行
    int   stage_ = 1;
    int   scoreBase_ = 0;               // 前のステージまでに確定した線路ぶんのスコア
    int   boltsAtStageStart_ = 0;       // やり直し用: ステージ開始時の状態 (ボルト・貨物・持ち物)
    int   boltsCollectedAtStageStart_ = 0;
    int   cargoWoodAtStageStart_ = 0, cargoIronAtStageStart_ = 0;
    HeldItem heldAtStageStart_[MaxPlayers];
    int   pondCount_ = 0, boulderClusters_ = 0;      // このステージの池・岩の塊の数
    float trainStartSpeed_ = 0, trainMaxSpeed_ = 0;  // このステージの列車の速度

    float trainPos_ = 0;                // path_ 上の位置 (セル番号, 小数)
    float rushSpeed_ = 0;               // ゴールラッシュ中の列車の速さ
    bool  rushAnnounced_ = false;       // ゴールラッシュ開始のイベントを出したか
    std::vector<GameEvent> events_;     // まだ App が受け取っていない出来事
    float time_ = 0;                    // ステージの経過時間 (Playing 中のみ進む。列車の加速に使う)
    float animTime_ = 0;                // アニメ用の時計 (常に進む)
    float camX_ = 0;
    State state_ = State::Playing;
    uint32_t rng_ = 1;
};

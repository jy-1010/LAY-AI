#include "Game.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <limits>
#include <queue>

using namespace DirectX;
namespace vis  = cfg::vis;
namespace anim = cfg::anim;

// ---------------------------------------------------------------------------
// 描画ヘルパ: メッシュの種類ごとに Submit する (色は cfg::Rgb)
// ---------------------------------------------------------------------------
class GamePainter {
public:
    GamePainter(Renderer& r, const Game::Meshes& m) : r_(r), m_(m) {}
    void Cube   (const XMMATRIX& w, const cfg::Rgb& c) { r_.Submit(m_.cube,    w, ToColor(c)); }
    void Sphere (const XMMATRIX& w, const cfg::Rgb& c) { r_.Submit(m_.sphere,  w, ToColor(c)); }
    void Pyramid(const XMMATRIX& w, const cfg::Rgb& c) { r_.Submit(m_.pyramid, w, ToColor(c)); }
    void Hex    (const XMMATRIX& w, const cfg::Rgb& c) { r_.Submit(m_.hex,     w, ToColor(c)); }
private:
    Renderer& r_;
    Game::Meshes m_;
};

namespace {

// 変形行列: スケール → ローカル平行移動 → Y軸回転 → ワールド平行移動
//   (s = 大きさ, l = 回転前の位置ずらし, yaw = Y軸まわりの回転, w = ワールド位置)
XMMATRIX Xf(float sx, float sy, float sz, float lx, float ly, float lz,
            float yaw, float wx, float wy, float wz)
{
    return XMMatrixScaling(sx, sy, sz) * XMMatrixTranslation(lx, ly, lz)
         * XMMatrixRotationY(yaw) * XMMatrixTranslation(wx, wy, wz);
}

// 歩けないマスか (橋・ホーム・草地は歩ける)
bool IsSolidTile(Tile t)
{
    return t == Tile::Tree || t == Tile::Rock || t == Tile::HardRock || t == Tile::Water
        || t == Tile::Building || t == Tile::Fence;
}

// 上下左右の隣接マス (幅優先探索・ランダムウォーク用)
const IVec2 kNeighbors[] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
constexpr unsigned kNeighborCount = sizeof(kNeighbors) / sizeof(kNeighbors[0]);

// xorshift32 乱数のシフト量 (Marsaglia の定数)
constexpr int kShiftA = 13, kShiftB = 17, kShiftC = 5;

// 円(プレイヤー)と、向き付きの箱(車両)の衝突判定。
// めり込んでいれば、円を箱の外へ出すための移動量 (out) を返す。
//   (cx, cz, yaw) = 箱の中心と向き, halfW/halfL = 箱の幅・長さの半分
bool CirclePushOutOfBox(float px, float pz, float radius,
                        float cx, float cz, float yaw, float halfW, float halfL,
                        float& outX, float& outZ)
{
    // 箱のローカル軸: 前 = (sin, cos), 右 = (cos, -sin)   (Y軸回転 yaw の左手系)
    const float fx = sinf(yaw), fz = cosf(yaw);
    const float rx = fz,        rz = -fx;

    // 円の中心を箱の座標系 (右, 前) に直す
    const float dx = px - cx, dz = pz - cz;
    const float lx = dx * rx + dz * rz;
    const float lz = dx * fx + dz * fz;

    // 箱の上で円の中心に一番近い点 → そこから中心へのベクトル
    const float nx = lx - std::min(std::max(lx, -halfW), halfW);
    const float nz = lz - std::min(std::max(lz, -halfL), halfL);
    const float dist2 = nx * nx + nz * nz;
    if (dist2 >= radius * radius) return false;                  // 離れている

    float pushX, pushZ;                                          // 箱の座標系での押し出し量
    if (dist2 > cfg::kPushOutEpsilon) {
        const float dist = sqrtf(dist2);
        const float depth = radius - dist;                       // めり込んだ深さ
        pushX = nx / dist * depth;
        pushZ = nz / dist * depth;
    } else {
        // 中心が箱の内側: 一番浅い面から外へ出す
        const float penX = halfW - fabsf(lx), penZ = halfL - fabsf(lz);
        if (penX < penZ) { pushX = (lx >= 0.0f ? 1.0f : -1.0f) * (penX + radius); pushZ = 0.0f; }
        else             { pushX = 0.0f; pushZ = (lz >= 0.0f ? 1.0f : -1.0f) * (penZ + radius); }
    }
    outX = pushX * rx + pushZ * fx;                              // ワールド座標へ戻す
    outZ = pushX * rz + pushZ * fz;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// 乱数
// ---------------------------------------------------------------------------
uint32_t Game::Rand()
{
    rng_ ^= rng_ << kShiftA; rng_ ^= rng_ >> kShiftB; rng_ ^= rng_ << kShiftC;
    return rng_;
}

float Game::Rand01()
{
    return (float)(Rand() % cfg::kPercent) / (float)cfg::kPercent;
}

// ---------------------------------------------------------------------------
// ステージ生成
// ---------------------------------------------------------------------------
bool Game::IsGateRow(int z)
{
    return std::abs(z - cfg::kRailRow) <= cfg::kGateHalfRows;
}

bool Game::IsGoal(IVec2 c) const
{
    return c.x == cfg::kGoalColumn && IsGateRow(c.z);
}

void Game::ClearMap()
{
    for (int z = 0; z < H; ++z)
    for (int x = 0; x < W; ++x) {
        map_[z][x] = Tile::Grass;
        onPath_[z][x] = false;
    }

    // スタート駅の建物 (線路の少し奥側)
    for (int dz = 0; dz < cfg::kStartStationRows; ++dz)
    for (int dx = 0; dx < cfg::kStartStationCols; ++dx)
        map_[cfg::kStartStationRow0 + dz][dx] = Tile::Building;

    // スタート駅のホーム (線路の両脇)
    for (int dz = -cfg::kStartPlatformHalfRows; dz <= cfg::kStartPlatformHalfRows; ++dz)
    for (int x = 0; x <= cfg::kStartPlatformMaxX; ++x)
        map_[cfg::kRailRow + dz][x] = Tile::Platform;

    // ゴール駅 (右端の列): 入口の3マスはホーム、それ以外は柵
    for (int z = 0; z < H; ++z)
        map_[z][cfg::kGoalColumn] = IsGateRow(z) ? Tile::Platform : Tile::Fence;
}

void Game::RemoveHazards()
{
    for (int z = 0; z < H; ++z)
    for (int x = 0; x < W; ++x)
        if (map_[z][x] == Tile::Water || map_[z][x] == Tile::HardRock) map_[z][x] = Tile::Grass;
}

void Game::PlacePonds()
{
    for (int k = 0; k < pondCount_; ++k) {
        // 池の中心と半径を乱数で決める
        const int cx = cfg::kPondMinX + (int)(Rand() % (cfg::kPondMaxX - cfg::kPondMinX + 1));
        const int cz = cfg::kPondMinZ + (int)(Rand() % (cfg::kPondMaxZ - cfg::kPondMinZ + 1));
        const float radius = cfg::kPondMinRadius + (cfg::kPondMaxRadius - cfg::kPondMinRadius) * Rand01();

        for (int z = 0; z < H; ++z)
        for (int x = cfg::kResourceMinX; x <= cfg::kResourceMaxX; ++x) {   // スタート地帯とゴール列は避ける
            const float dx = (float)(x - cx), dz = (float)(z - cz);
            if (dx * dx + dz * dz <= radius * radius && map_[z][x] == Tile::Grass)
                map_[z][x] = Tile::Water;
        }
    }
}

// 壊れない岩の塊: ランダムウォークで隣へ広げながら数マスぶん置く
void Game::PlaceBoulders()
{
    for (int k = 0; k < boulderClusters_; ++k) {
        IVec2 c = { cfg::kResourceMinX + (int)(Rand() % (cfg::kResourceMaxX - cfg::kResourceMinX + 1)),
                    (int)(Rand() % H) };
        for (int n = 0; n < cfg::kBoulderClusterCells; ++n) {
            if (c.x >= cfg::kResourceMinX && c.x <= cfg::kResourceMaxX && c.z >= 0 && c.z < H
                && map_[c.z][c.x] == Tile::Grass)                    // 池の上には置かない
                map_[c.z][c.x] = Tile::HardRock;
            const IVec2& d = kNeighbors[Rand() % kNeighborCount];    // 隣のマスへ
            c.x += d.x; c.z += d.z;
        }
    }
}

// 線路の先端からゴールの入口まで行けるか。
// 通れない物 = 壊れない岩・柵・建物。木と岩は採取で、水は橋で越えられるので通れる扱い
bool Game::GoalReachable() const
{
    bool seen[H][W] = {};
    std::queue<IVec2> open;
    open.push(path_.back());
    seen[path_.back().z][path_.back().x] = true;

    while (!open.empty()) {
        const IVec2 cur = open.front(); open.pop();
        if (IsGoal(cur)) return true;
        for (const IVec2& d : kNeighbors) {
            const IVec2 next = { cur.x + d.x, cur.z + d.z };
            if (!InBounds(next) || seen[next.z][next.x]) continue;
            const Tile t = map_[next.z][next.x];
            if (t == Tile::HardRock || t == Tile::Fence || t == Tile::Building) continue;
            seen[next.z][next.x] = true;
            open.push(next);
        }
    }
    return false;
}

void Game::ScatterResources()
{
    for (int z = 0; z < H; ++z)
    for (int x = cfg::kResourceMinX; x <= cfg::kResourceMaxX; ++x) {
        if (map_[z][x] != Tile::Grass) continue;                    // 水・岩の塊の上には置かない
        const int roll = (int)(Rand() % cfg::kPercent);             // 0..99
        if (roll < cfg::kTreeChance)                            map_[z][x] = Tile::Tree;
        else if (roll < cfg::kTreeChance + cfg::kRockChance)    map_[z][x] = Tile::Rock;
    }
}

void Game::ScatterBolts()
{
    boltItems_.clear();
    const int minX = cfg::kResourceMinX, maxX = cfg::kResourceMaxX;
    for (int attempt = 0; attempt < cfg::kBoltMaxAttempts && (int)boltItems_.size() < cfg::kBoltCount; ++attempt) {
        const IVec2 c = { minX + (int)(Rand() % (maxX - minX + 1)), (int)(Rand() % H) };
        if (map_[c.z][c.x] != Tile::Grass) continue;                // 草地だけ
        bool taken = false;                                          // 同じマスに2個置かない
        for (const IVec2& b : boltItems_) if (b.x == c.x && b.z == c.z) { taken = true; break; }
        if (!taken) boltItems_.push_back(c);
    }
}

// ブレーキ結晶: 拾うと列車が一時的に遅くなる。草地にランダムに置く (ボルトと同じマスには置かない)
void Game::ScatterCrystals()
{
    crystals_.clear();
    const int minX = cfg::kResourceMinX, maxX = cfg::kResourceMaxX;
    for (int attempt = 0; attempt < cfg::kBoltMaxAttempts && (int)crystals_.size() < cfg::kCrystalCount; ++attempt) {
        const IVec2 c = { minX + (int)(Rand() % (maxX - minX + 1)), (int)(Rand() % H) };
        if (map_[c.z][c.x] != Tile::Grass) continue;
        bool taken = false;
        for (const IVec2& b : boltItems_) if (b.x == c.x && b.z == c.z) { taken = true; break; }
        for (const IVec2& o : crystals_)  if (o.x == c.x && o.z == c.z) { taken = true; break; }
        if (!taken) crystals_.push_back(c);
    }
}

void Game::BuildStage()
{
    ClearMap();

    // 初期線路 (スタート駅から伸びる)
    path_.clear();
    for (int x = 0; x < cfg::kInitialRailLength; ++x) {
        path_.push_back({ x, cfg::kRailRow });
        onPath_[cfg::kRailRow][x] = true;
    }

    // 池と岩の塊: ゴールへ行けなくなる配置だったら引き直す。それでもダメなら置かない
    bool ok = false;
    for (int attempt = 0; attempt < cfg::kPondRetryMax && !ok; ++attempt) {
        RemoveHazards();
        PlacePonds();
        PlaceBoulders();
        ok = GoalReachable();
    }
    if (!ok) RemoveHazards();

    ScatterResources();
    ScatterBolts();
    ScatterCrystals();
}

// ---------------------------------------------------------------------------
// ステージの開始 (最初から / 次のステージ / やり直し)
// ---------------------------------------------------------------------------
void Game::Reset(uint32_t seed)
{
    stage_ = 1;
    scoreBase_ = 0;
    bolts_ = 0;
    boltsCollected_ = 0;
    for (int& level : upgradeLevel_) level = 0;                      // 強化も最初から
    cargoWood_ = cargoIron_ = 0;                                     // 最初からなので貨物も空
    dynamiteStock_ = 0;
    for (Player& p : players_) p.held = HeldItem();                  // 誰も何も持っていない
    StartStage(seed);
}

void Game::NextStage(uint32_t seed)
{
    scoreBase_ += StageRailScore();      // クリアしたステージの線路ぶん + クリアボーナスを確定 (ボルトは通算で数えている)
    ++stage_;                            // 貨物車両の木・鉄・ボルト・プレイヤーが持っている物はそのまま引き継ぐ
    StartStage(seed);
}

void Game::RestartStage(uint32_t seed)
{
    bolts_ = boltsAtStageStart_;         // このステージで増減した物は無かったことにする
    boltsCollected_ = boltsCollectedAtStageStart_;
    cargoWood_ = cargoWoodAtStageStart_;
    cargoIron_ = cargoIronAtStageStart_;
    dynamiteStock_ = dynamiteAtStageStart_;
    for (int i = 0; i < MaxPlayers; ++i) players_[i].held = heldAtStageStart_[i];
    StartStage(seed);
}

void Game::StartStage(uint32_t seed)
{
    rng_ = seed ? seed : 1;              // xorshift は 0 だと止まるので 1 に置き換える

    // ステージが進むほど難しくする
    const int extra = stage_ - 1;
    pondCount_       = std::min(cfg::kPondCount + extra * cfg::kPondPerStage, cfg::kPondMaxCount);
    boulderClusters_ = std::min(cfg::kBoulderClusters + extra * cfg::kBoulderPerStage, cfg::kBoulderMaxClusters);
    trainStartSpeed_ = std::min(cfg::kTrainStartSpeed + extra * cfg::kStageSpeedStep, cfg::kTrainSpeedCap);
    trainMaxSpeed_   = std::min(cfg::kTrainMaxSpeed   + extra * cfg::kStageSpeedStep, cfg::kTrainSpeedCap);

    BuildStage();

    for (int i = 0; i < MaxPlayers; ++i) {                           // 参加中のプレイヤーは維持して位置だけ戻す
        players_[i].px = cfg::kSpawn[i].x;
        players_[i].pz = cfg::kSpawn[i].z;
        players_[i].face = { 1, 0 };
        players_[i].pushed = false;
    }
    RebuildTrain();                                                  // 買った車両を連結する
    explosions_.clear();
    slowTimer_ = 0;
    collectTimer_ = mineTimer_ = dynamiteTimer_ = 0;
    for (Player& pl : players_) pl.noPickupTimer = 0;
    crafterTracks_ = cfg::kInitialTracks;                            // レール作成車両には毎ステージ決まった数の線路が入っている
    craftTimer_ = 0;
    groundItems_.clear();                                            // 落ちている物は前のステージのもの
    boltsAtStageStart_ = bolts_;                                     // やり直し用に開始時の状態を覚えておく
    boltsCollectedAtStageStart_ = boltsCollected_;
    cargoWoodAtStageStart_ = cargoWood_;
    cargoIronAtStageStart_ = cargoIron_;
    dynamiteAtStageStart_ = dynamiteStock_;
    for (int i = 0; i < MaxPlayers; ++i) heldAtStageStart_[i] = players_[i].held;
    trainPos_ = cfg::kInitialTrainPos;
    rushSpeed_ = 0;
    rushAnnounced_ = false;
    events_.clear();                                                 // 前のステージの出来事は捨てる
    time_ = 0;
    state_ = State::Playing;
    camX_ = cfg::kCamMinX;
}

// ---------------------------------------------------------------------------
// プレイヤーの参加 / 退出
// ---------------------------------------------------------------------------
int Game::AddPlayer()
{
    int ref = -1;                                                    // 既存プレイヤーの近くに出現させる
    for (int i = 0; i < MaxPlayers; ++i) if (players_[i].active) { ref = i; break; }
    for (int i = 0; i < MaxPlayers; ++i) {
        if (players_[i].active) continue;
        Player& p = players_[i];
        p.active = true;
        p.face = { 1, 0 };
        if (ref >= 0) { p.px = players_[ref].px; p.pz = players_[ref].pz; }
        else          { p.px = cfg::kSpawn[i].x; p.pz = cfg::kSpawn[i].z; }
        return i;
    }
    return -1;                                                       // 満員
}

void Game::RemovePlayer(int idx)
{
    if (idx >= 0 && idx < MaxPlayers) players_[idx].active = false;
}

int Game::PlayerCount() const
{
    int n = 0;
    for (const Player& p : players_) if (p.active) ++n;
    return n;
}

// ---------------------------------------------------------------------------
// 判定
// ---------------------------------------------------------------------------
// ワールド座標 (x, z) が歩けないマスの中か。マップの外も歩けない扱い
bool Game::Solid(float x, float z) const
{
    // マスの中心が整数座標なので、四捨五入でマス番号になる
    int cx = (int)floorf(x + cfg::kHalf), cz = (int)floorf(z + cfg::kHalf);
    if (cx < 0 || cz < 0 || cx >= W || cz >= H) return true;
    return IsSolidTile(map_[cz][cx]);
}

bool Game::Blocked(float x, float z) const
{
    const float r = cfg::kPlayerRadius;
    return Solid(x - r, z - r) || Solid(x + r, z - r) || Solid(x - r, z + r) || Solid(x + r, z + r);
}

void Game::TryMove(Player& p, float dx, float dz)
{
    float nx = p.px + dx, nz = p.pz + dz;
    // 画面外に出ないよう、カメラ範囲内に留める (全員が同じ画面に映る)
    nx = std::min(std::max(nx, camX_ - cfg::kCamRange), camX_ + cfg::kCamRange);
    if (Blocked(nx, nz)) return;                                     // 歩けないマスには入らない
    p.px = nx; p.pz = nz;
}

IVec2 Game::FaceCell(const Player& p) const
{
    return { (int)floorf(p.px + cfg::kHalf) + p.face.x, (int)floorf(p.pz + cfg::kHalf) + p.face.z };
}

// 線路を置けるか: 草地・ホーム・橋 / まだ線路が無い / 線路の先端の隣
bool Game::CanPlace(IVec2 c) const
{
    if (!InBounds(c) || onPath_[c.z][c.x]) return false;
    const Tile t = map_[c.z][c.x];
    if (t != Tile::Grass && t != Tile::Platform && t != Tile::Bridge) return false;
    const IVec2& end = path_.back();
    return std::abs(end.x - c.x) + std::abs(end.z - c.z) == 1;       // 上下左右のどれかで隣接
}

// プレイヤーが近づいたボルトと、ブレーキ結晶を拾う
void Game::CollectBolts(const Player& p)
{
    const float r2 = cfg::kBoltPickupRadius * cfg::kBoltPickupRadius;
    for (size_t b = 0; b < boltItems_.size(); ) {
        const float dx = (float)boltItems_[b].x - p.px, dz = (float)boltItems_[b].z - p.pz;
        if (dx * dx + dz * dz <= r2) {
            boltItems_[b] = boltItems_.back();                       // 最後の要素で埋めて削除
            boltItems_.pop_back();
            ++bolts_;
            ++boltsCollected_;
            Emit(GameEvent::Bolt);
        } else {
            ++b;
        }
    }
    // ブレーキ結晶: 拾うと列車が一定時間 遅くなる
    for (size_t c = 0; c < crystals_.size(); ) {
        const float dx = (float)crystals_[c].x - p.px, dz = (float)crystals_[c].z - p.pz;
        if (dx * dx + dz * dz <= r2) {
            crystals_[c] = crystals_.back();
            crystals_.pop_back();
            slowTimer_ = cfg::kCrystalSlowSeconds;
            Emit(GameEvent::BrakeCrystal);
        } else {
            ++c;
        }
    }
}

// 今のステージで敷いた線路ぶんのスコア (+ クリアボーナス)
int Game::StageRailScore() const
{
    int score = ((int)path_.size() - cfg::kInitialRailLength) * cfg::kScorePerRail;
    if (state_ == State::Clear) score += cfg::kScoreClearBonus;
    return score;
}

int Game::Score() const
{
    return scoreBase_ + StageRailScore() + boltsCollected_ * cfg::kScorePerBolt;    // 消費したボルトもスコアには数える
}

// 列車の通常の速さ: 時間とともに加速し、遅くする手段を反映する
//   ・TRAIN BRAKE の強化        : 常に遅い
//   ・ブレーキ結晶を拾った後    : 一定時間 遅い
//   ・機関車が橋を渡っている間  : 徐行
float Game::BaseSpeed() const
{
    float speed = std::min(trainMaxSpeed_, trainStartSpeed_ + cfg::kTrainAccel * time_);
    speed *= BrakeMultiplier();
    if (slowTimer_ > 0.0f) speed *= cfg::kCrystalSlowFactor;
    if (OnBridge())        speed *= cfg::kBridgeSlowFactor;
    return speed;
}

// ゴールラッシュ中 (線路がゴール駅まで繋がった後) は、一気に加速してゴールへ向かう
float Game::TrainSpeed() const
{
    const float base = BaseSpeed();
    return IsRushing() ? std::max(base, rushSpeed_) : base;
}

// 機関車の一番近い線路のマスが橋か
bool Game::OnBridge() const
{
    if (path_.empty()) return false;
    const int index = std::min(std::max((int)(trainPos_ + cfg::kHalf), 0), (int)path_.size() - 1);
    const IVec2& c = path_[index];
    return map_[c.z][c.x] == Tile::Bridge;
}

bool Game::HasWagon(Wagon w) const
{
    for (Wagon t : train_) if (t == w) return true;
    return false;
}

// 買った車両に合わせて train_ を作り直す。最初の3両 (機関車・貨物・レール作成) は固定で、その後ろに追加車両が並ぶ
void Game::RebuildTrain()
{
    train_.clear();
    train_.push_back(Wagon::Engine);
    train_.push_back(Wagon::Cargo);
    train_.push_back(Wagon::Crafter);
    if (upgradeLevel_[(int)Upgrade::CollectorWagon] > 0) train_.push_back(Wagon::Collector);
    if (upgradeLevel_[(int)Upgrade::MinerWagon]     > 0) train_.push_back(Wagon::Miner);
    if (upgradeLevel_[(int)Upgrade::DynamiteWagon]  > 0) train_.push_back(Wagon::Dynamite);
}

bool Game::IsRushing() const
{
    return state_ == State::Playing && IsGoal(path_.back());
}

// 線路上の位置 t (マス番号, 小数) → ワールド座標と向き
void Game::TrainPoint(float t, float& x, float& z, float& yaw) const
{
    const int n = (int)path_.size();
    int i; float f;                                                  // i = 区間の番号, f = 区間内の位置 0..1
    if (t <= 0.0f)            { i = 0;     f = t; }                  // 始点より後ろは第1区間を延長
    else if (t >= n - 1)      { i = n - 2; f = 1.0f; }               // 終点より先は終点に止める
    else                      { i = (int)t; f = t - i; }
    const IVec2& a = path_[i];
    const IVec2& b = path_[i + 1];
    x = a.x + (b.x - a.x) * f;
    z = a.z + (b.z - a.z) * f;
    yaw = atan2f((float)(b.x - a.x), (float)(b.z - a.z));            // 進行方向を向く (Y軸回転)
}

// 全車両 (機関車 + 貨車) の判定用の箱を作って、車両数を返す。大きさは見た目 (vis) と同じ + 余白
int Game::GetCars(CarBox cars[cfg::kMaxCars]) const
{
    const int count = std::min((int)train_.size(), cfg::kMaxCars);
    for (int i = 0; i < count; ++i) {
        CarBox& c = cars[i];
        TrainPoint(trainPos_ - cfg::kWagonSpacing * i, c.x, c.z, c.yaw);       // i = 0 が機関車
        c.type = train_[i];
        const float w = (i == 0) ? vis::kEngineWidth  : vis::kWagonWidth;
        const float l = (i == 0) ? vis::kEngineLength : vis::kWagonLength;
        c.halfW = w * cfg::kHalf + cfg::kTrainCollisionPad;
        c.halfL = l * cfg::kHalf + cfg::kTrainCollisionPad;
    }
    return count;
}

// どれかの車両に円 (x, z, radius) がめり込んでいれば、その車両を返す。無ければ nullptr
const Game::CarBox* Game::FirstCarOverlap(const CarBox* cars, int count, float x, float z, float radius) const
{
    for (int i = 0; i < count; ++i) {
        float ox, oz;
        if (CirclePushOutOfBox(x, z, radius, cars[i].x, cars[i].z, cars[i].yaw, cars[i].halfW, cars[i].halfL, ox, oz))
            return &cars[i];
    }
    return nullptr;
}

// プレイヤーを車両の外へ押し出す。
//   まず「一番短く外へ出る向き」へ、それが壁・水・別の車両で塞がっていれば
//   車両の4つの面のどれかから真っ直ぐ外へ、と候補を試して、最も短く動かせるものを選ぶ。
//   どこにも逃げられない (挟まれた) 時だけリスポーンさせる。
void Game::ResolveTrainCollisions()
{
    CarBox cars[cfg::kMaxCars];
    const int count = GetCars(cars);
    // 浮動小数の誤差で「押し出した直後なのにまだ重なっている」と誤判定しないよう、判定を少し甘くする
    const float testR = cfg::kPlayerRadius - cfg::kPushOutTolerance;

    for (int i = 0; i < MaxPlayers; ++i) {
        Player& p = players_[i];
        if (!p.active) continue;

        bool moved = false;                                          // このフレームに押し出されたか
        for (int iter = 0; iter < cfg::kPushIterations; ++iter) {
            const CarBox* hit = FirstCarOverlap(cars, count, p.px, p.pz, testR);
            if (!hit) break;                                         // もう重なっていない

            float bestDx = 0.0f, bestDz = 0.0f;
            float bestLen2 = std::numeric_limits<float>::max();
            bool found = false;
            // 候補 (dx, dz) 分だけ動いた先が空いていれば、一番短いものを覚えておく
            auto consider = [&](float dx, float dz) {
                const float nx = p.px + dx, nz = p.pz + dz;
                if (Blocked(nx, nz) || FirstCarOverlap(cars, count, nx, nz, testR)) return;
                const float len2 = dx * dx + dz * dz;
                if (len2 < bestLen2) { bestLen2 = len2; bestDx = dx; bestDz = dz; found = true; }
            };

            // 候補1: 一番短く外へ出る向き (+ 余白)
            float ox, oz;
            if (CirclePushOutOfBox(p.px, p.pz, cfg::kPlayerRadius, hit->x, hit->z, hit->yaw,
                                   hit->halfW, hit->halfL, ox, oz)) {
                const float len = sqrtf(ox * ox + oz * oz);
                const float k = (len > 0.0f) ? (len + cfg::kPushOutMargin) / len : 1.0f;
                consider(ox * k, oz * k);
            }

            // 候補2〜5: 車両の 右 / 左 / 前 / 後ろ の面から真っ直ぐ外へ
            const float fx = sinf(hit->yaw), fz = cosf(hit->yaw);    // 車両の前方向
            const float rx = fz,             rz = -fx;               // 車両の右方向
            const float dx0 = p.px - hit->x, dz0 = p.pz - hit->z;
            const float lx = dx0 * rx + dz0 * rz;                    // 車両の座標系での位置 (右, 前)
            const float lz = dx0 * fx + dz0 * fz;
            const float exitW = hit->halfW + cfg::kPlayerRadius + cfg::kPushOutMargin;   // 側面の外までの距離
            const float exitL = hit->halfL + cfg::kPlayerRadius + cfg::kPushOutMargin;   // 前後の外までの距離
            for (float side : { -1.0f, 1.0f }) {
                const float moveX = side * exitW - lx;               // 右/左へ出るのに必要な移動量
                consider(moveX * rx, moveX * rz);
                const float moveZ = side * exitL - lz;               // 前/後ろへ出るのに必要な移動量
                consider(moveZ * fx, moveZ * fz);
            }

            if (!found) break;                                       // 逃げ場が無い
            p.px += bestDx; p.pz += bestDz;                          // 押し出す
            moved = true;
        }

        // 繰り返しても外へ出られなかった (壁や水との間に挟まれた)
        if (FirstCarOverlap(cars, count, p.px, p.pz, testR) || Blocked(p.px, p.pz)) RespawnPlayer(i);

        if (moved && !p.pushed) Emit(GameEvent::Pushed);             // 押され始めた時だけ知らせる
        p.pushed = moved;
    }
}

// リスポーン先を探す: 中心 (cx, cz) から minDist 以上 maxDist 以内で、歩けて、列車とも重ならないマスをランダムに選ぶ
bool Game::PickRespawnSpot(const CarBox* cars, int count, float cx, float cz,
                           float minDist, float maxDist, float& outX, float& outZ)
{
    const float testR = cfg::kPlayerRadius - cfg::kPushOutTolerance;
    const int baseX = (int)floorf(cx + cfg::kHalf), baseZ = (int)floorf(cz + cfg::kHalf);   // 中心のマス

    // 候補が無ければ範囲を少しずつ広げる
    for (float widen = 0.0f; widen <= cfg::kRespawnWidenMax; widen += cfg::kRespawnWidenStep) {
        const float maxR = maxDist + widen;
        const int range = (int)ceilf(maxR);
        std::vector<cfg::Vec2f> spots;
        for (int dz = -range; dz <= range; ++dz)
        for (int dx = -range; dx <= range; ++dx) {
            const IVec2 cell = { baseX + dx, baseZ + dz };
            if (!InBounds(cell)) continue;
            const float x = (float)cell.x, z = (float)cell.z;        // マスの中心
            const float d = sqrtf((x - cx) * (x - cx) + (z - cz) * (z - cz));
            if (d < minDist || d > maxR) continue;
            if (Blocked(x, z) || FirstCarOverlap(cars, count, x, z, testR)) continue;
            spots.push_back({ x, z });
        }
        if (!spots.empty()) {
            const cfg::Vec2f& s = spots[Rand() % spots.size()];      // ランダムに1つ
            outX = s.x; outZ = s.z;
            return true;
        }
    }
    return false;
}

// 挟まれて逃げ場が無かったプレイヤーをリスポーンさせる
//   マルチ: 他のプレイヤーの誰か (ランダム) の近く
//   ソロ  : 列車 (先頭) の近く
void Game::RespawnPlayer(int idx)
{
    Player& p = players_[idx];
    CarBox cars[cfg::kMaxCars];
    const int count = GetCars(cars);

    int others[MaxPlayers], n = 0;                                   // 自分以外の参加者
    for (int j = 0; j < MaxPlayers; ++j) if (j != idx && players_[j].active) others[n++] = j;

    float x, z;
    bool ok;
    if (n > 0) {
        const Player& o = players_[others[Rand() % n]];              // ランダムに選んだ他のプレイヤー
        ok = PickRespawnSpot(cars, count, o.px, o.pz, cfg::kRespawnMinDist, cfg::kRespawnMaxDist, x, z);
    } else {
        ok = PickRespawnSpot(cars, count, cars[0].x, cars[0].z, 0.0f, cfg::kRespawnTrainRadius, x, z);
        // 列車の近くはカメラの外かもしれないので、カメラもすぐ追いつかせる
        if (ok) camX_ = std::min(std::max(x, cfg::kCamMinX), (float)W - cfg::kCamEndMargin);
    }
    if (!ok) { x = cfg::kSpawn[idx].x; z = cfg::kSpawn[idx].z; }     // どうしても見つからなければスタート地点
    p.px = x; p.pz = z;
    p.pushed = false;
    Emit(GameEvent::Respawn);
}

// ---------------------------------------------------------------------------
// 持ち運びと貨車
// ---------------------------------------------------------------------------
// 手が空いている、または同じ種類の物で持てる数に余裕がある
bool Game::CanCarry(const Player& p, Item it) const
{
    return p.held.item == Item::None || (p.held.item == it && p.held.count < CarryMax());
}

void Game::Give(Player& p, Item it)
{
    p.held.item = it;                    // 持てる種類は 1つだけ (CanCarry で確認してから呼ぶこと)
    ++p.held.count;
}

void Game::TakeOne(Player& p)
{
    if (--p.held.count <= 0) p.held = HeldItem();                    // 0個になったら手ぶらに戻る
}

// プレイヤーが向いている先 (中心から kInteractReach 先の点) にある、触れる貨車の車両番号。無ければ 0
//   触れるのは 貨物車両 (預ける) / レール作成車両 (線路を取り出す) / ダイナマイト作成車両 (取り出す)。
//   機関車と、自動で動く 回収・採掘 車両には触れない
int Game::FacedWagon(const Player& p) const
{
    CarBox cars[cfg::kMaxCars];
    const int count = GetCars(cars);
    const float probeX = p.px + (float)p.face.x * cfg::kInteractReach;
    const float probeZ = p.pz + (float)p.face.z * cfg::kInteractReach;

    for (int i = 1; i < count; ++i) {
        const CarBox& c = cars[i];
        if (c.type != Wagon::Cargo && c.type != Wagon::Crafter && c.type != Wagon::Dynamite) continue;
        const float fx = sinf(c.yaw), fz = cosf(c.yaw);              // 車両の前方向
        const float rx = fz,          rz = -fx;                      // 車両の右方向
        const float dx = probeX - c.x, dz = probeZ - c.z;
        const float lx = dx * rx + dz * rz;                          // 車両の座標系での位置 (右, 前)
        const float lz = dx * fx + dz * fz;
        if (fabsf(lx) <= c.halfW + cfg::kInteractPad && fabsf(lz) <= c.halfL + cfg::kInteractPad) return i;
    }
    return 0;
}

// 持っている木・鉄を貨物車両に預ける (積める数まで。余りは手に残る)
void Game::DepositToCargo(Player& p)
{
    if (p.held.item != Item::Wood && p.held.item != Item::Iron) { Emit(GameEvent::Deny); return; }
    int& stock = (p.held.item == Item::Wood) ? cargoWood_ : cargoIron_;
    const int moved = std::min(p.held.count, CargoCapacity() - stock);
    if (moved <= 0) { Emit(GameEvent::Deny); return; }               // 貨物車両が満杯
    stock += moved;
    p.held.count -= moved;
    if (p.held.count == 0) p.held = HeldItem();
    Emit(GameEvent::Deposit);
}

// レール作成車両から線路を取り出す (手ぶらか、線路を持っていて持てる数に余裕がある時)
void Game::TakeTracks(Player& p)
{
    if (!CanCarry(p, Item::Track)) { Emit(GameEvent::Deny); return; }
    const int have = (p.held.item == Item::Track) ? p.held.count : 0;
    const int moved = std::min(CarryMax() - have, crafterTracks_);
    if (moved <= 0) { Emit(GameEvent::Deny); return; }               // 線路がまだ出来ていない
    crafterTracks_ -= moved;
    p.held.item = Item::Track;
    p.held.count = have + moved;
    Emit(GameEvent::Pickup);
}

// 向いているマスの 落ちている物 / 木・岩 を拾う。手がふさがっている (別の種類を持っている / 持てる数いっぱい) と拾えない
//   木・岩は 1個拾える。強化の確率で 1個増え、持ちきれない分はその場に落ちる。
//   さらに強化の確率で、隣の木・岩も一緒に壊れる (壊れた物は地面に落ちる)
void Game::HarvestFacing(Player& p)
{
    const IVec2 c = FaceCell(p);
    if (!InBounds(c)) return;

    const int ground = FindGroundItem(c);                            // 落ちている物があれば先にそれを拾う
    if (ground >= 0) { PickUpGround(p, ground); return; }

    Tile& t = map_[c.z][c.x];
    Item it = Item::None;
    if (t == Tile::Tree) it = Item::Wood;
    else if (t == Tile::Rock) it = Item::Iron;
    else if (t == Tile::HardRock) { Emit(GameEvent::Deny); return; }   // 壊れない岩は拾えない
    if (it == Item::None) return;                                    // 何も無いマス

    if (!CanCarry(p, it)) { Emit(GameEvent::Deny); return; }

    const int gain = 1 + (Rand01() < YieldChance() ? 1 : 0);         // 獲得個数 (確率で +1)
    t = Tile::Grass;
    const int have = (p.held.item == it) ? p.held.count : 0;
    const int take = std::min(gain, CarryMax() - have);              // 手に持てる分
    p.held.item = it;
    p.held.count = have + take;
    if (gain > take) AddGroundItem(c, it, gain - take);              // 持ちきれない分は足元に落ちる
    Emit(GameEvent::Harvest);

    SmashNeighbors(c);
}

// 持っている物を、向いているマスの地面に置く (歩ける場所で、別の種類の山が無い所だけ)
void Game::DropHeld(Player& p)
{
    if (p.held.item == Item::None) return;                           // 何も持っていない
    const IVec2 c = FaceCell(p);
    if (!InBounds(c) || IsSolidTile(map_[c.z][c.x])) { Emit(GameEvent::Deny); return; }
    const int existing = FindGroundItem(c);
    if (existing >= 0 && groundItems_[existing].stack.item != p.held.item) { Emit(GameEvent::Deny); return; }

    AddGroundItem(c, p.held.item, p.held.count);
    p.held = HeldItem();
    p.noPickupTimer = cfg::kDropPickupCooldown;                      // 置いた直後に自動で拾い直さない
    Emit(GameEvent::Drop);
}

// 向いているマスに: 水なら橋を架ける (木を持っている時) / それ以外なら線路を置く (線路を持っている時)
void Game::PlaceFacing(Player& p)
{
    const IVec2 c = FaceCell(p);
    if (!InBounds(c)) return;
    Tile& t = map_[c.z][c.x];

    if (t == Tile::HardRock) {                                       // 壊れない岩: ダイナマイトだけが壊せる
        if (p.held.item == Item::Dynamite) { TakeOne(p); Explode(c); }
        else                               Emit(GameEvent::Deny);
        return;
    }
    if (t == Tile::Water) {
        if (p.held.item == Item::Wood && p.held.count >= cfg::kWoodPerBridge) {
            for (int k = 0; k < cfg::kWoodPerBridge; ++k) TakeOne(p);   // 持っている木を使う
            t = Tile::Bridge;
            Emit(GameEvent::BuildBridge);
        } else {
            Emit(GameEvent::Deny);                                   // 木を持っていないと橋は架けられない
        }
        return;
    }
    if (!CanPlace(c)) return;
    if (p.held.item != Item::Track) { Emit(GameEvent::Deny); return; }
    TakeOne(p);
    path_.push_back(c);
    onPath_[c.z][c.x] = true;
    Emit(GameEvent::PlaceTrack);
}

// ---------------------------------------------------------------------------
// 地面の物
// ---------------------------------------------------------------------------
int Game::FindGroundItem(IVec2 c) const
{
    for (size_t i = 0; i < groundItems_.size(); ++i)
        if (groundItems_[i].cell.x == c.x && groundItems_[i].cell.z == c.z) return (int)i;
    return -1;
}

void Game::AddGroundItem(IVec2 c, Item it, int count)
{
    const int index = FindGroundItem(c);
    if (index >= 0 && groundItems_[index].stack.item == it) { groundItems_[index].stack.count += count; return; }
    groundItems_.push_back({ c, { it, count } });
}

// 山から持てるだけ拾う。残りは山に残る
bool Game::PickUpGround(Player& p, int index)
{
    GroundItem& g = groundItems_[index];
    if (!CanCarry(p, g.stack.item)) { Emit(GameEvent::Deny); return false; }
    const int have = (p.held.item == g.stack.item) ? p.held.count : 0;
    const int take = std::min(g.stack.count, CarryMax() - have);
    p.held.item = g.stack.item;
    p.held.count = have + take;
    g.stack.count -= take;
    if (g.stack.count <= 0) groundItems_.erase(groundItems_.begin() + index);
    Emit(GameEvent::Pickup);
    return true;
}

// 採取した場所 c の上下左右の 木・岩 が、強化の確率で一緒に壊れる。壊れた物は 1個地面に落ちる
void Game::SmashNeighbors(IVec2 c)
{
    for (const IVec2& d : kNeighbors) {
        const IVec2 n = { c.x + d.x, c.z + d.z };
        if (!InBounds(n)) continue;
        Tile& t = map_[n.z][n.x];
        if (t != Tile::Tree && t != Tile::Rock) continue;            // 壊れない岩・水などは対象外
        if (Rand01() >= SmashChance()) continue;
        AddGroundItem(n, (t == Tile::Tree) ? Item::Wood : Item::Iron, 1);
        t = Tile::Grass;
        Emit(GameEvent::Smash);
    }
}

// ---------------------------------------------------------------------------
// 強化 (ショップ) と、強化を反映した性能
// ---------------------------------------------------------------------------
int Game::UpgradeCost(Upgrade u) const
{
    const int level = upgradeLevel_[(int)u];
    if (level >= kUpgradeInfo[(int)u].maxLevel) return -1;           // 最大レベル
    return UpgradeCostAt(u, level);
}

bool Game::CanBuy(Upgrade u) const
{
    const int cost = UpgradeCost(u);
    return cost >= 0 && bolts_ >= cost;
}

bool Game::BuyUpgrade(Upgrade u)
{
    if (!CanBuy(u)) return false;
    bolts_ -= UpgradeCost(u);
    ++upgradeLevel_[(int)u];
    RebuildTrain();                                                  // 車両を買った時は、すぐ列車に連結する
    return true;
}

float Game::MoveSpeedMultiplier() const
{
    return cfg::upgrade::kBaseMultiplier + (float)upgradeLevel_[(int)Upgrade::MoveSpeed] * cfg::upgrade::kMoveSpeedStep;
}
int Game::CarryMax() const
{
    return cfg::kCarryMax + upgradeLevel_[(int)Upgrade::CarryCapacity] * cfg::upgrade::kCarryStep;
}
float Game::YieldChance() const
{
    return (float)upgradeLevel_[(int)Upgrade::HarvestYield] * cfg::upgrade::kYieldChanceStep;
}
float Game::SmashChance() const
{
    return (float)upgradeLevel_[(int)Upgrade::SmashChance] * cfg::upgrade::kSmashChanceStep;
}
float Game::CraftSeconds() const
{
    // 速さが +25% / レベル → 時間は その逆数
    return cfg::kCraftSeconds / (cfg::upgrade::kBaseMultiplier
                                 + (float)upgradeLevel_[(int)Upgrade::CraftSpeed] * cfg::upgrade::kCraftSpeedStep);
}
int Game::CrafterCapacity() const
{
    return cfg::kCrafterCapacity + upgradeLevel_[(int)Upgrade::TrackCapacity] * cfg::upgrade::kTrackCapacityStep;
}
int Game::CargoCapacity() const
{
    return cfg::kCargoCapacity + upgradeLevel_[(int)Upgrade::CargoCapacity] * cfg::upgrade::kCargoCapacityStep;
}
float Game::CraftProgress() const
{
    return craftTimer_ / CraftSeconds();
}
float Game::DynamiteProgress() const
{
    return dynamiteTimer_ / cfg::kDynamiteSeconds;
}
// TRAIN BRAKE: 1レベルごとに速度が下がる。下がりすぎないよう下限がある
float Game::BrakeMultiplier() const
{
    return std::max(cfg::upgrade::kBrakeMinFactor,
                    cfg::upgrade::kBaseMultiplier - (float)upgradeLevel_[(int)Upgrade::TrainBrake] * cfg::upgrade::kBrakeStep);
}

// ---------------------------------------------------------------------------
// ダイナマイトと、自動で動く車両
// ---------------------------------------------------------------------------
// ダイナマイト作成車両からダイナマイトを取り出す (手ぶらか、ダイナマイトを持っていて持てる数に余裕がある時)
void Game::TakeDynamite(Player& p)
{
    if (!CanCarry(p, Item::Dynamite)) { Emit(GameEvent::Deny); return; }
    const int have = (p.held.item == Item::Dynamite) ? p.held.count : 0;
    const int moved = std::min(CarryMax() - have, dynamiteStock_);
    if (moved <= 0) { Emit(GameEvent::Deny); return; }               // まだ出来ていない
    dynamiteStock_ -= moved;
    p.held.item = Item::Dynamite;
    p.held.count = have + moved;
    Emit(GameEvent::Pickup);
}

// 爆発: 爆心と、その上下左右の 壊れない岩 を壊す
void Game::Explode(IVec2 c)
{
    auto blast = [&](IVec2 cell) {
        if (InBounds(cell) && map_[cell.z][cell.x] == Tile::HardRock) map_[cell.z][cell.x] = Tile::Grass;
    };
    blast(c);
    for (const IVec2& d : kNeighbors) blast({ c.x + d.x, c.z + d.z });
    explosions_.push_back({ c, 0.0f });
    Emit(GameEvent::Explosion);
}

// 持っている物と同じ種類が近くに落ちていれば、通りがかりに自動で拾う (手ぶらの時や、別の種類の時は拾わない)
void Game::AutoPickup(Player& p)
{
    if (p.held.item == Item::None || p.noPickupTimer > 0.0f) return;
    const float r2 = cfg::kAutoPickupRadius * cfg::kAutoPickupRadius;
    for (size_t i = 0; i < groundItems_.size() && p.held.count < CarryMax(); ) {
        const GroundItem& g = groundItems_[i];
        const float dx = (float)g.cell.x - p.px, dz = (float)g.cell.z - p.pz;
        if (g.stack.item == p.held.item && dx * dx + dz * dz <= r2) {
            const size_t before = groundItems_.size();
            PickUpGround(p, (int)i);                                 // 持てるだけ拾う (山が空になれば消える)
            if (groundItems_.size() == before) ++i;                  // 消えなかった時だけ次へ進む
        } else {
            ++i;
        }
    }
}

// 追加車両の自動動作 (購入済みの車両だけ)
void Game::UpdateWagons(float dt)
{
    CarBox cars[cfg::kMaxCars];
    const int count = GetCars(cars);
    for (int i = 1; i < count; ++i) {
        switch (cars[i].type) {
        case Wagon::Collector: RunCollector(cars[i], dt); break;
        case Wagon::Miner:     RunMiner(cars[i], dt);     break;
        case Wagon::Dynamite:  RunDynamiteCrafter(dt);    break;
        default: break;
        }
    }
}

// 自動回収車両: 範囲内の 落ちている木・鉄 を貨物車両へ、ボルトを所持ボルトへ 一定間隔で回収する
void Game::RunCollector(const CarBox& car, float dt)
{
    collectTimer_ += dt;
    if (collectTimer_ < cfg::kCollectInterval) return;
    collectTimer_ = 0.0f;

    const float r2 = cfg::kCollectorRadius * cfg::kCollectorRadius;
    bool any = false;

    for (size_t i = 0; i < groundItems_.size(); ) {                  // 落ちている 木・鉄 (積載量の範囲で)
        GroundItem& g = groundItems_[i];
        const float dx = (float)g.cell.x - car.x, dz = (float)g.cell.z - car.z;
        const bool resource = (g.stack.item == Item::Wood || g.stack.item == Item::Iron);
        if (resource && dx * dx + dz * dz <= r2) {
            int& stock = (g.stack.item == Item::Wood) ? cargoWood_ : cargoIron_;
            const int moved = std::min(g.stack.count, CargoCapacity() - stock);
            if (moved > 0) { stock += moved; g.stack.count -= moved; any = true; }
            if (g.stack.count <= 0) { groundItems_.erase(groundItems_.begin() + i); continue; }
        }
        ++i;
    }
    for (size_t b = 0; b < boltItems_.size(); ) {                    // ボルト
        const float dx = (float)boltItems_[b].x - car.x, dz = (float)boltItems_[b].z - car.z;
        if (dx * dx + dz * dz <= r2) {
            boltItems_[b] = boltItems_.back();
            boltItems_.pop_back();
            ++bolts_;
            ++boltsCollected_;
            any = true;
        } else {
            ++b;
        }
    }
    if (any) Emit(GameEvent::Collect);
}

// 周囲採掘車両: 範囲内で一番近い 木・岩 を一定間隔で 1つ採掘して、貨物車両に入れる (積載量が一杯の物は掘らない)
void Game::RunMiner(const CarBox& car, float dt)
{
    mineTimer_ += dt;
    if (mineTimer_ < cfg::kMineInterval) return;
    mineTimer_ = 0.0f;

    const float r2 = cfg::kMinerRadius * cfg::kMinerRadius;
    const int range = (int)ceilf(cfg::kMinerRadius);
    const int baseX = (int)floorf(car.x + cfg::kHalf), baseZ = (int)floorf(car.z + cfg::kHalf);
    IVec2 best = { -1, -1 };
    float bestD2 = std::numeric_limits<float>::max();

    for (int dz = -range; dz <= range; ++dz)
    for (int dx = -range; dx <= range; ++dx) {
        const IVec2 cell = { baseX + dx, baseZ + dz };
        if (!InBounds(cell)) continue;
        const Tile t = map_[cell.z][cell.x];
        if (t != Tile::Tree && t != Tile::Rock) continue;
        const int stock = (t == Tile::Tree) ? cargoWood_ : cargoIron_;
        if (stock >= CargoCapacity()) continue;                      // 貨物が一杯
        const float d2 = ((float)cell.x - car.x) * ((float)cell.x - car.x) + ((float)cell.z - car.z) * ((float)cell.z - car.z);
        if (d2 <= r2 && d2 < bestD2) { bestD2 = d2; best = cell; }
    }
    if (best.x < 0) return;                                          // 掘れる物が無い

    Tile& t = map_[best.z][best.x];
    if (t == Tile::Tree) ++cargoWood_; else ++cargoIron_;
    t = Tile::Grass;
    Emit(GameEvent::Harvest);
}

// ダイナマイト作成車両: 貨物車両の 木 と 鉄 から、一定時間ごとにダイナマイトを1本作る
void Game::RunDynamiteCrafter(float dt)
{
    if (cargoWood_ >= cfg::kDynamiteWood && cargoIron_ >= cfg::kDynamiteIron && dynamiteStock_ < cfg::kDynamiteCapacity) {
        dynamiteTimer_ += dt;
        if (dynamiteTimer_ >= cfg::kDynamiteSeconds) {
            dynamiteTimer_ = 0.0f;
            cargoWood_ -= cfg::kDynamiteWood;
            cargoIron_ -= cfg::kDynamiteIron;
            ++dynamiteStock_;
            Emit(GameEvent::Craft);
        }
    } else {
        dynamiteTimer_ = 0.0f;                                       // 材料が無い / 満杯の間は進めない
    }
}

// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------
void Game::Update(float dt, const Input in[MaxPlayers])
{
    // 誰かがリスタートを要求したら、今のステージをやり直す
    bool restart = false;
    for (int i = 0; i < MaxPlayers; ++i)
        if (in[i].restart) restart = true;
    if (restart) { RestartStage((uint32_t)GetTickCount()); return; }

    // カメラ: 参加者の最前列と最後列の中間を追従 (範囲はスタート駅〜ゴール駅が映るように制限)
    int count = 0;
    float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
    for (const Player& p : players_) {
        if (!p.active) continue;
        minX = std::min(minX, p.px); maxX = std::max(maxX, p.px); ++count;
    }
    float target = (count > 0) ? (minX + maxX) * cfg::kHalf : camX_;
    target = std::min(std::max(target, cfg::kCamMinX), (float)W - cfg::kCamEndMargin);
    camX_ += (target - camX_) * std::min(1.0f, dt * cfg::kCamFollowSpeed);

    if (state_ != State::Playing) return;
    time_ += dt;
    slowTimer_ = std::max(0.0f, slowTimer_ - dt);                    // ブレーキ結晶の効果が切れていく
    for (Explosion& e : explosions_) e.age += dt;                    // 爆発の演出
    explosions_.erase(std::remove_if(explosions_.begin(), explosions_.end(),
                      [](const Explosion& e) { return e.age >= cfg::kExplosionSeconds; }), explosions_.end());

    // --- プレイヤーごとの処理 ---
    for (int i = 0; i < MaxPlayers; ++i) {
        Player& p = players_[i];
        if (!p.active) continue;
        const Input& pin = in[i];

        // 移動 (アナログ入力は倒した大きさに応じて速度が変わる)
        float mvx = pin.moveX, mvz = pin.moveZ;
        const float len = sqrtf(mvx * mvx + mvz * mvz);
        if (len > 0.0f) {
            const float scale = std::min(1.0f, len);                 // 速度の倍率 0..1
            mvx /= len; mvz /= len;
            // 向きは上下左右の4方向。大きい方の軸に合わせる
            if (fabsf(mvx) >= fabsf(mvz)) p.face = { mvx > 0 ? 1 : -1, 0 };
            else                          p.face = { 0, mvz > 0 ? 1 : -1 };
            // X と Z を別々に動かす (壁にぶつかっても滑れるように)
            const float speed = cfg::kPlayerSpeed * MoveSpeedMultiplier();   // 強化で速くなる
            TryMove(p, mvx * speed * scale * dt, 0);
            TryMove(p, 0, mvz * speed * scale * dt);
        }

        CollectBolts(p);
        p.noPickupTimer = std::max(0.0f, p.noPickupTimer - dt);
        AutoPickup(p);                                               // 持っている物と同じ落とし物が近くにあれば自動で拾う

        // 採取ボタン: 向いている先に貨車があれば 預ける / 取り出す。無ければ 落ちている物 / 木・岩 を拾う
        if (pin.harvest) {
            const int wagon = FacedWagon(p);
            if (wagon > 0) {
                switch (train_[wagon]) {
                case Wagon::Cargo:    DepositToCargo(p); break;
                case Wagon::Crafter:  TakeTracks(p);     break;
                case Wagon::Dynamite: TakeDynamite(p);   break;
                default: break;
                }
            } else {
                HarvestFacing(p);
            }
        }

        // 設置ボタン: 水なら橋 (木を持っている時) / それ以外なら線路 (線路を持っている時)
        if (pin.place) PlaceFacing(p);

        // 置くボタン: 持っている物を、向いているマスの地面に置く
        if (pin.drop) DropHeld(p);
    }

    // --- レール作成車両: 貨物車両に 木 と 鉄 がある間、一定時間ごとに線路を1本作る ---
    if (cargoWood_ >= cfg::kWoodPerTrack && cargoIron_ >= cfg::kIronPerTrack && crafterTracks_ < CrafterCapacity()) {
        craftTimer_ += dt;
        if (craftTimer_ >= CraftSeconds()) {
            craftTimer_ = 0.0f;
            cargoWood_ -= cfg::kWoodPerTrack;
            cargoIron_ -= cfg::kIronPerTrack;
            ++crafterTracks_;
            Emit(GameEvent::Craft);
        }
    } else {
        craftTimer_ = 0.0f;                                          // 材料が無い / 出来上がりが満杯の間は進めない
    }

    UpdateWagons(dt);                                                // 追加車両 (自動回収 / 採掘 / ダイナマイト作成)

    // --- 列車: 線路の上を進み、少しずつ加速する ---
    if (IsRushing() && !rushAnnounced_) { rushAnnounced_ = true; Emit(GameEvent::RushStart); }
    if (IsRushing()) {                                               // 線路がゴールまで繋がった → どんどん加速
        const float base = BaseSpeed();
        rushSpeed_ = std::min(std::max(rushSpeed_, base) + cfg::kGoalRushAccel * dt, cfg::kGoalRushMaxSpeed);
    }
    const float end = (float)(path_.size() - 1);                     // 線路の終端 (マス番号)
    trainPos_ += TrainSpeed() * dt;
    if (trainPos_ >= end) {                                          // 終端に到達
        trainPos_ = end;
        const bool cleared = IsGoal(path_.back());                   // ゴール駅に着いていればクリア
        state_ = cleared ? State::Clear : State::GameOver;
        Emit(cleared ? GameEvent::Clear : GameEvent::GameOver);
    }

    ResolveTrainCollisions();                                        // 列車に押されて車両の外へ出る
}

void Game::Camera(XMFLOAT3& eye, XMFLOAT3& target) const
{
    target = XMFLOAT3(camX_, 0.0f, cfg::kCamTargetZ);
    eye    = XMFLOAT3(camX_, cfg::kCamHeight, cfg::kCamEyeZ);
}

// ---------------------------------------------------------------------------
// 描画
// ---------------------------------------------------------------------------
void Game::Render(Renderer& r, const Meshes& m) const
{
    GamePainter p(r, m);
    DrawTerrain(p);
    DrawStations(p);
    DrawGroundItems(p);
    DrawBolts(p);
    DrawCrystals(p);
    DrawTrack(p);
    DrawTrain(p);
    DrawPlayers(p);
    DrawExplosions(p);
}

// 地面・水・橋・木・岩・岩の塊・柵
void Game::DrawTerrain(GamePainter& p) const
{
    const float slabY = -vis::kTileThickness * cfg::kHalf;           // スラブの中心高さ (上面が y=0)
    for (int z = 0; z < H; ++z)
    for (int x = 0; x < W; ++x) {
        const float fx = (float)x, fz = (float)z;
        const Tile t = map_[z][x];

        // --- 地面スラブ ---
        if (t == Tile::Water || t == Tile::Bridge) {
            // 水面は少し沈め、波に合わせて明るさを揺らす (橋の下も水)
            const float wave = sinf(animTime_ * anim::kWaveSpeed + fx * anim::kWaveFreqX + fz * anim::kWaveFreqZ)
                             * anim::kWaveAmp;
            const cfg::Rgb c = { vis::kWaterColor.r + wave, vis::kWaterColor.g + wave, vis::kWaterColor.b + wave };
            p.Cube(Xf(cfg::kTileSize, vis::kTileThickness, cfg::kTileSize,
                      0, slabY - vis::kWaterSink, 0, 0, fx, 0, fz), c);
        } else {
            cfg::Rgb c = vis::kPlatformColor;                        // ホーム
            if (t != Tile::Platform)                                 // 草地は市松模様
                c = ((x + z) % vis::kCheckerModulo == 0) ? vis::kGrassLight : vis::kGrassDark;
            p.Cube(Xf(cfg::kTileSize, vis::kTileThickness, cfg::kTileSize, 0, slabY, 0, 0, fx, 0, fz), c);
        }

        // --- 地面の上のもの ---
        if (t == Tile::Tree) {
            p.Cube(Xf(vis::kTrunkWidth, vis::kTrunkHeight, vis::kTrunkWidth,
                      0, vis::kTrunkHeight * cfg::kHalf, 0, 0, fx, 0, fz), vis::kTrunkColor);
            p.Pyramid(Xf(vis::kLeavesLowSize, vis::kLeavesLowHeight, vis::kLeavesLowSize,
                         0, vis::kLeavesLowY, 0, 0, fx, 0, fz), vis::kLeavesLowColor);
            p.Pyramid(Xf(vis::kLeavesHighSize, vis::kLeavesHighHeight, vis::kLeavesHighSize,
                         0, vis::kLeavesHighY, 0, 0, fx, 0, fz), vis::kLeavesHighColor);
        } else if (t == Tile::Rock) {
            p.Sphere(Xf(vis::kRockWidth, vis::kRockHeight, vis::kRockWidth,
                        0, vis::kRockY, 0, 0, fx, 0, fz), vis::kRockColor);
            p.Sphere(Xf(vis::kOreSize, vis::kOreSize, vis::kOreSize,
                        vis::kOreOffsetX, vis::kOreY, vis::kOreOffsetZ, 0, fx, 0, fz), vis::kOreColor);
        } else if (t == Tile::HardRock) {
            // 壊れない岩: 壊せる岩より大きく暗い。上に結晶が生えている
            p.Sphere(Xf(vis::kBoulderSize, vis::kBoulderHeight, vis::kBoulderSize,
                        0, vis::kBoulderY, 0, 0, fx, 0, fz), vis::kBoulderColor);
            p.Sphere(Xf(vis::kBoulderBumpSize, vis::kBoulderBumpSize, vis::kBoulderBumpSize,
                        vis::kBoulderBumpOffsetX, vis::kBoulderBumpY, vis::kBoulderBumpOffsetZ, 0, fx, 0, fz),
                     vis::kBoulderBumpColor);
            p.Pyramid(Xf(vis::kBoulderSpikeSize, vis::kBoulderSpikeHeight, vis::kBoulderSpikeSize,
                         0, vis::kBoulderSpikeY, 0, 0, fx, 0, fz), vis::kBoulderSpikeColor);
        } else if (t == Tile::Bridge) {
            // 橋: 水の上の床 + 床板 + 四隅の杭
            p.Cube(Xf(cfg::kTileSize, vis::kBridgeDeckThickness, cfg::kTileSize,
                      0, vis::kBridgeDeckY, 0, 0, fx, 0, fz), vis::kBridgeDeckColor);
            for (int k = 0; k < vis::kBridgeSlatCount; ++k) {
                const float off = (k - (vis::kBridgeSlatCount - 1) * cfg::kHalf) * vis::kBridgeSlatStep;   // 中心からのずれ
                p.Cube(Xf(vis::kBridgeSlatWidth, vis::kBridgeSlatHeight, vis::kBridgeSlatThickness,
                          0, vis::kBridgeSlatY, off, 0, fx, 0, fz), vis::kBridgeSlatColor);
            }
            for (float sx : { -vis::kBridgePostOffset, vis::kBridgePostOffset })
            for (float sz : { -vis::kBridgePostOffset, vis::kBridgePostOffset })
                p.Cube(Xf(vis::kBridgePostSize, vis::kBridgePostHeight, vis::kBridgePostSize,
                          sx, vis::kBridgePostY, sz, 0, fx, 0, fz), vis::kBridgePostColor);
        } else if (t == Tile::Fence) {
            p.Cube(Xf(vis::kFenceThickness, vis::kFenceHeight, cfg::kTileSize,
                      0, vis::kFenceHeight * cfg::kHalf, 0, 0, fx, 0, fz), vis::kFenceColor);
        }
    }
}

// スタート駅 (左端) とゴール駅 (右端の外側)
void Game::DrawStations(GamePainter& p) const
{
    // ---- スタート駅: 線路の奥側に建つ小さな駅舎。扉と看板は線路 (+z) 側 ----
    {
        const float cols = (float)cfg::kStartStationCols, rows = (float)cfg::kStartStationRows;
        const float cx = (cols - 1.0f) * cfg::kHalf;                                   // 建物の中心 (マス座標)
        const float cz = (float)cfg::kStartStationRow0 + (rows - 1.0f) * cfg::kHalf;
        p.Cube(Xf(cols, vis::kStationWallHeight, rows, 0, vis::kStationWallHeight * cfg::kHalf, 0, 0, cx, 0, cz),
               vis::kStationWallColor);
        p.Pyramid(Xf(cols + vis::kRoofGrow, vis::kStartRoofHeight, rows + vis::kRoofGrow,
                     0, vis::kStationWallHeight + vis::kStartRoofHeight * cfg::kHalf, 0, 0, cx, 0, cz),
                  vis::kStationRoofColor);
        const float frontZ = cz + rows * cfg::kHalf + vis::kDoorThickness * cfg::kHalf;   // 壁の少し手前
        p.Cube(Xf(vis::kStartDoorWidth, vis::kStartDoorHeight, vis::kDoorThickness,
                  0, vis::kStartDoorHeight * cfg::kHalf, 0, 0, cx, 0, frontZ), vis::kDoorColor);
        p.Cube(Xf(vis::kStartSignWidth, vis::kSignHeight, vis::kDoorThickness,
                  0, vis::kStationWallHeight - vis::kSignHeight, 0, 0, cx, 0, frontZ), vis::kSignColor);
    }

    // ---- ゴール駅: マップの右端の外側に建つ。入口 (west 面) が線路の終点に向く ----
    {
        const float gx = (float)W - cfg::kHalf + vis::kGoalDepth * cfg::kHalf;      // 建物の中心 x
        const float gz = (float)cfg::kRailRow;
        const float roofTop = vis::kGoalWallHeight + vis::kGoalRoofHeight;          // 屋根のてっぺんの高さ
        p.Cube(Xf(vis::kGoalDepth, vis::kGoalWallHeight, vis::kGoalWidth,
                  0, vis::kGoalWallHeight * cfg::kHalf, 0, 0, gx, 0, gz), vis::kStationWallColor);
        p.Pyramid(Xf(vis::kGoalDepth + vis::kRoofGrow, vis::kGoalRoofHeight, vis::kGoalWidth + vis::kRoofGrow,
                     0, vis::kGoalWallHeight + vis::kGoalRoofHeight * cfg::kHalf, 0, 0, gx, 0, gz),
                  vis::kStationRoofColor);

        // 入口の扉と看板 (壁の西面のすぐ手前)
        const float doorX = (float)W - cfg::kHalf - vis::kDoorThickness * cfg::kHalf;
        p.Cube(Xf(vis::kDoorThickness, vis::kGoalDoorHeight, vis::kGoalDoorWidth,
                  0, vis::kGoalDoorHeight * cfg::kHalf, 0, 0, doorX, 0, gz), vis::kDoorColor);
        p.Cube(Xf(vis::kDoorThickness, vis::kSignHeight, vis::kGoalDoorWidth,
                  0, vis::kGoalDoorHeight + vis::kSignHeight * cfg::kHalf, 0, 0, doorX, 0, gz), vis::kSignColor);

        // 屋根の上の旗 (ゆらゆら揺れる)
        p.Cube(Xf(vis::kPoleWidth, vis::kPoleHeight, vis::kPoleWidth,
                  0, roofTop + vis::kPoleHeight * cfg::kHalf, 0, 0, gx, 0, gz), vis::kPoleColor);
        const float sway = sinf(animTime_ * anim::kFlagSwaySpeed) * anim::kFlagSwayAmp;
        p.Cube(Xf(vis::kFlagLength, vis::kFlagHeight, vis::kFlagThickness,
                  vis::kFlagLength * cfg::kHalf, roofTop + vis::kPoleHeight - vis::kFlagHeight * cfg::kHalf, 0,
                  sway, gx, 0, gz), vis::kFlagColor);                                // ポールを軸に回して揺らす
    }
}

// ボルト: くるくる回りながら上下に浮かぶ金色のボルト
void Game::DrawBolts(GamePainter& p) const
{
    for (size_t i = 0; i < boltItems_.size(); ++i) {
        const float bx = (float)boltItems_[i].x, bz = (float)boltItems_[i].z;
        const float phase = (float)i * anim::kBoltPhaseStep;                          // ボルトごとに動きをずらす
        const float yaw = animTime_ * anim::kBoltSpinSpeed + phase;
        const float baseY = vis::kBoltBaseY + sinf(animTime_ * anim::kBoltBobSpeed + phase) * anim::kBoltBobAmp;

        p.Sphere(Xf(vis::kBoltShadowSize, vis::kBoltShadowHeight, vis::kBoltShadowSize,
                    0, vis::kBoltShadowY, 0, 0, bx, 0, bz), vis::kBoltShadowColor);   // 地面の影
        p.Hex(Xf(vis::kBoltHeadSize, vis::kBoltHeadHeight, vis::kBoltHeadSize,
                 0, baseY + vis::kBoltHeadY, 0, yaw, bx, 0, bz), vis::kBoltHeadColor);   // 六角の頭
        p.Cube(Xf(vis::kBoltShaftWidth, vis::kBoltShaftHeight, vis::kBoltShaftWidth,
                  0, baseY + vis::kBoltShaftY, 0, yaw, bx, 0, bz), vis::kBoltShaftColor); // ねじ部
    }
}

// 線路: 枕木 + 左右のレール。区間の向きに合わせて回転させる
void Game::DrawTrack(GamePainter& p) const
{
    const int n = (int)path_.size();
    for (int i = 0; i < n; ++i) {
        IVec2 d{ 1, 0 };                                                              // この区間の進行方向
        if (i + 1 < n)   d = { path_[i + 1].x - path_[i].x, path_[i + 1].z - path_[i].z };
        else if (i > 0)  d = { path_[i].x - path_[i - 1].x, path_[i].z - path_[i - 1].z };
        const float yaw = atan2f((float)d.x, (float)d.z);
        const float fx = (float)path_[i].x, fz = (float)path_[i].z;

        p.Cube(Xf(vis::kSleeperSize, vis::kSleeperThickness, vis::kSleeperSize,
                  0, vis::kSleeperY, 0, yaw, fx, 0, fz), vis::kSleeperColor);
        for (float side : { -vis::kRailGauge, vis::kRailGauge })                      // 左右のレール
            p.Cube(Xf(vis::kRailWidth, vis::kRailHeight, vis::kRailLength,
                      side, vis::kRailY, 0, yaw, fx, 0, fz), vis::kRailColor);
    }
}

// 列車: 連結されている全車両を、後ろへ順に描く
//   機関車 / 貨物車両 (木と鉄を積む) / レール作成車両 (線路を積み、緑のバーが進み具合) /
//   自動回収車両 (回る磁石) / 周囲採掘車両 (回るドリル) / ダイナマイト作成車両 (ダイナマイトを積み、バーが進み具合)
void Game::DrawTrain(GamePainter& p) const
{
    float x, z, yaw;

    // 積み荷を貨車の上に1列に並べる (最大 kInventoryShowMax 個)
    auto cargoRow = [&](int count, float offsetX, const cfg::Rgb& color) {
        for (int k = 0; k < std::min(count, cfg::kInventoryShowMax); ++k)
            p.Cube(Xf(vis::kCargoSize, vis::kCargoSize, vis::kCargoLength,
                      offsetX, vis::kCargoY, vis::kCargoZStart + vis::kCargoZStep * k,
                      yaw, x, 0, z), color);
    };
    // 車体の側面に進行バー (進み具合 0..1)
    auto progressBar = [&](float progress) {
        p.Cube(Xf(vis::kCraftBarThickness, vis::kCraftBarHeight, vis::kCraftBarLength,         // 土台 (満タンの長さ)
                  vis::kCraftBarSideX, vis::kCraftBarY, vis::kCraftBarZ0 + vis::kCraftBarLength * cfg::kHalf,
                  yaw, x, 0, z), vis::kCraftBarBgColor);
        const float ratio = std::min(std::max(progress, 0.0f), 1.0f);
        if (ratio > 0.0f) {
            const float len = vis::kCraftBarLength * ratio;                                    // 進んだぶんだけ塗る
            p.Cube(Xf(vis::kCraftBarFillThickness, vis::kCraftBarHeight, len,
                      vis::kCraftBarSideX, vis::kCraftBarY, vis::kCraftBarZ0 + len * cfg::kHalf,
                      yaw, x, 0, z), vis::kCraftBarColor);
        }
    };
    auto wagonBody = [&](const cfg::Rgb& color) {
        p.Cube(Xf(vis::kWagonWidth, vis::kWagonHeight, vis::kWagonLength,
                  0, vis::kWagonY, 0, yaw, x, 0, z), color);
    };

    for (int i = 0; i < (int)train_.size(); ++i) {
        TrainPoint(trainPos_ - cfg::kWagonSpacing * i, x, z, yaw);           // i = 0 が機関車
        switch (train_[i]) {
        case Wagon::Engine:                                                  // 車体 + 運転台 + 煙突
            p.Cube(Xf(vis::kEngineWidth, vis::kEngineHeight, vis::kEngineLength,
                      0, vis::kEngineY, 0, yaw, x, 0, z), vis::kEngineColor);
            p.Cube(Xf(vis::kCabWidth, vis::kCabHeight, vis::kCabLength,
                      0, vis::kCabY, vis::kCabZ, yaw, x, 0, z), vis::kCabColor);
            p.Cube(Xf(vis::kChimneyWidth, vis::kChimneyHeight, vis::kChimneyWidth,
                      0, vis::kChimneyY, vis::kChimneyZ, yaw, x, 0, z), vis::kChimneyColor);
            break;

        case Wagon::Cargo:                                                   // 木 (左列) と 鉄 (右列)
            wagonBody(vis::kStorageWagonColor);
            cargoRow(cargoWood_, -vis::kCargoOffsetX, vis::kTrunkColor);
            cargoRow(cargoIron_,  vis::kCargoOffsetX, vis::kIronColor);
            break;

        case Wagon::Crafter:                                                 // 出来上がった線路 + 進行バー
            wagonBody(vis::kCrafterWagonColor);
            for (int k = 0; k < std::min(crafterTracks_, cfg::kInventoryShowMax); ++k)
                p.Cube(Xf(vis::kTrackItemWidth, vis::kTrackItemHeight, vis::kCargoLength,
                          0, vis::kTrackItemY, vis::kCargoZStart + vis::kCargoZStep * k,
                          yaw, x, 0, z), vis::kTrackItemColor);
            progressBar(CraftProgress());
            break;

        case Wagon::Collector:                                               // 上で磁石 (六角の円盤) が回る
            wagonBody(vis::kCollectorWagonColor);
            p.Hex(Xf(vis::kMagnetSize, vis::kMagnetHeight, vis::kMagnetSize,
                     0, vis::kMagnetY, 0, animTime_ * anim::kMagnetSpinSpeed, x, 0, z), vis::kMagnetColor);
            break;

        case Wagon::Miner:                                                   // 上でドリル (四角錐) が回る
            wagonBody(vis::kMinerWagonColor);
            p.Pyramid(Xf(vis::kDrillSize, vis::kDrillHeight, vis::kDrillSize,
                         0, vis::kDrillY, 0, animTime_ * anim::kDrillSpinSpeed, x, 0, z), vis::kDrillColor);
            break;

        case Wagon::Dynamite:                                                // ダイナマイトの束 + 進行バー
            wagonBody(vis::kDynamiteWagonColor);
            for (int k = 0; k < std::min(dynamiteStock_, cfg::kDynamiteCapacity); ++k)
                for (float side : { -vis::kDynamiteStickOffsetX, vis::kDynamiteStickOffsetX })
                    p.Cube(Xf(vis::kDynamiteStickWidth, vis::kDynamiteStickWidth, vis::kDynamiteStickLength,
                              side, vis::kDynamiteStickY, vis::kCargoZStart + vis::kDynamiteStickStep * k,
                              yaw, x, 0, z), vis::kDynamiteColor);
            progressBar(DynamiteProgress());
            break;
        }
    }
}

// 持ち物の色
static const cfg::Rgb& ItemColor(Item it)
{
    switch (it) {
    case Item::Wood:  return vis::kTrunkColor;
    case Item::Iron:  return vis::kIronColor;
    case Item::Dynamite: return vis::kDynamiteColor;
    default:          return vis::kTrackItemColor;     // Item::Track
    }
}

// プレイヤー: 体 + 頭 + 向きの目印 + 頭上の色マーカー + 持っている物。向いている先のハイライトも描く
void Game::DrawPlayers(GamePainter& p) const
{
    CarBox cars[cfg::kMaxCars];
    GetCars(cars);

    for (int i = 0; i < MaxPlayers; ++i) {
        const Player& pl = players_[i];
        if (!pl.active) continue;
        const cfg::Rgb& color = cfg::kPlayerColors[i];
        const float layerY = vis::kHighlightY + vis::kHighlightLayerStep * i;   // 複数人のハイライトが重ならないよう少しずらす

        // ---- 向いている先のハイライト ----
        const int wagon = FacedWagon(pl);
        if (wagon != 0) {
            // 貨車: 預けられる / 取り出せる時だけ光る (黄)
            const Wagon type = cars[wagon].type;
            const bool canDeposit = (type == Wagon::Cargo)
                                  && (pl.held.item == Item::Wood || pl.held.item == Item::Iron);
            const bool canTake = (type == Wagon::Crafter  && crafterTracks_ > 0 && CanCarry(pl, Item::Track))
                              || (type == Wagon::Dynamite && dynamiteStock_ > 0 && CanCarry(pl, Item::Dynamite));
            if (canDeposit || canTake) {
                const CarBox& c = cars[wagon];
                p.Cube(Xf(c.halfW * 2.0f, vis::kHighlightThickness, c.halfL * 2.0f,
                          0, layerY, 0, c.yaw, c.x, 0, c.z), vis::kHighlightHarvestColor);
            }
        } else {
            // マス: 緑 = 線路を置ける・ダイナマイトを使える / 水色 = 橋を架けられる / 黄 = 拾える
            const IVec2 c = FaceCell(pl);
            if (InBounds(c)) {
                const Tile t = map_[c.z][c.x];
                const cfg::Rgb* mark = nullptr;
                if (CanPlace(c) && pl.held.item == Item::Track)                  mark = &vis::kHighlightPlaceColor;
                else if (t == Tile::HardRock && pl.held.item == Item::Dynamite)  mark = &vis::kHighlightPlaceColor;
                else if (t == Tile::Water && pl.held.item == Item::Wood
                         && pl.held.count >= cfg::kWoodPerBridge)                mark = &vis::kHighlightBridgeColor;
                else if (FindGroundItem(c) >= 0 && CanCarry(pl, groundItems_[FindGroundItem(c)].stack.item))
                                                                                 mark = &vis::kHighlightHarvestColor;   // 落ちている物
                else if (t == Tile::Tree && CanCarry(pl, Item::Wood))            mark = &vis::kHighlightHarvestColor;
                else if (t == Tile::Rock && CanCarry(pl, Item::Iron))            mark = &vis::kHighlightHarvestColor;
                if (mark)
                    p.Cube(Xf(vis::kHighlightSize, vis::kHighlightThickness, vis::kHighlightSize,
                              0, layerY, 0, 0, (float)c.x, 0, (float)c.z), *mark);
            }
        }

        // ---- 体 ----
        const float yaw = atan2f((float)pl.face.x, (float)pl.face.z);                 // 向いている方向
        p.Cube(Xf(vis::kPlayerBodyWidth, vis::kPlayerBodyHeight, vis::kPlayerBodyDepth,
                  0, vis::kPlayerBodyY, 0, yaw, pl.px, 0, pl.pz), color);
        p.Sphere(Xf(vis::kPlayerHeadSize, vis::kPlayerHeadSize, vis::kPlayerHeadSize,
                    0, vis::kPlayerHeadY, 0, yaw, pl.px, 0, pl.pz), vis::kSkinColor);
        p.Cube(Xf(vis::kPlayerNoseWidth, vis::kPlayerNoseHeight, vis::kPlayerNoseLength,
                  0, vis::kPlayerNoseY, vis::kPlayerNoseZ, yaw, pl.px, 0, pl.pz), vis::kNoseColor);
        p.Cube(Xf(vis::kMarkerWidth, vis::kMarkerThickness, vis::kMarkerWidth,
                  0, vis::kMarkerY, 0, 0, pl.px, 0, pl.pz), color);

        // ---- 持っている物: 頭の上に積んで表示 (線路は平たい板) ----
        for (int k = 0; k < pl.held.count; ++k) {
            const float y = vis::kHeldItemBaseY + vis::kHeldItemStep * k;
            if (pl.held.item == Item::Track)
                p.Cube(Xf(vis::kHeldTrackWidth, vis::kHeldTrackHeight, vis::kHeldTrackLength,
                          0, y, 0, yaw, pl.px, 0, pl.pz), ItemColor(pl.held.item));
            else
                p.Cube(Xf(vis::kHeldItemSize, vis::kHeldItemSize, vis::kHeldItemSize,
                          0, y, 0, yaw, pl.px, 0, pl.pz), ItemColor(pl.held.item));
        }
    }
}

// 地面に置かれた物: 1マスに小さな山 (2 x 2 を段にして積む)。線路は平たい板
void Game::DrawGroundItems(GamePainter& p) const
{
    const int cols = vis::kGroundItemColumns;
    for (const GroundItem& g : groundItems_) {
        const float cx = (float)g.cell.x, cz = (float)g.cell.z;
        const int shown = std::min(g.stack.count, vis::kGroundItemShowMax);
        for (int k = 0; k < shown; ++k) {
            const int col = k % cols, row = (k / cols) % cols, layer = k / (cols * cols);
            const float ox = ((float)col - (float)(cols - 1) * cfg::kHalf) * vis::kGroundItemSpacing;   // 山の中心からのずれ
            const float oz = ((float)row - (float)(cols - 1) * cfg::kHalf) * vis::kGroundItemSpacing;
            const float y  = vis::kGroundItemBaseY + (float)layer * vis::kGroundItemSize;
            if (g.stack.item == Item::Track)
                p.Cube(Xf(vis::kHeldTrackWidth, vis::kHeldTrackHeight, vis::kHeldTrackLength,
                          ox, y, oz, 0, cx, 0, cz), ItemColor(g.stack.item));
            else
                p.Cube(Xf(vis::kGroundItemSize, vis::kGroundItemSize, vis::kGroundItemSize,
                          ox, y, oz, 0, cx, 0, cz), ItemColor(g.stack.item));
        }
    }
}

// ブレーキ結晶: くるくる回りながら上下に浮かぶ水色の結晶 (ボルトと同じ動き)
void Game::DrawCrystals(GamePainter& p) const
{
    for (size_t i = 0; i < crystals_.size(); ++i) {
        const float cx = (float)crystals_[i].x, cz = (float)crystals_[i].z;
        const float phase = (float)i * anim::kBoltPhaseStep;
        const float yaw = animTime_ * anim::kBoltSpinSpeed + phase;
        const float baseY = vis::kCrystalBaseY + sinf(animTime_ * anim::kBoltBobSpeed + phase) * anim::kBoltBobAmp;
        p.Sphere(Xf(vis::kBoltShadowSize, vis::kBoltShadowHeight, vis::kBoltShadowSize,
                    0, vis::kBoltShadowY, 0, 0, cx, 0, cz), vis::kBoltShadowColor);          // 地面の影
        p.Pyramid(Xf(vis::kCrystalSize, vis::kCrystalHeight, vis::kCrystalSize,
                     0, baseY, 0, yaw, cx, 0, cz), vis::kCrystalColor);
    }
}

// 爆発: 橙色の球が大きく膨らみながら、だんだん暗くなる
void Game::DrawExplosions(GamePainter& p) const
{
    for (const Explosion& e : explosions_) {
        const float t = std::min(e.age / cfg::kExplosionSeconds, 1.0f);              // 0..1
        const float size = vis::kExplosionStartSize + (vis::kExplosionEndSize - vis::kExplosionStartSize) * t;
        const float dim = cfg::upgrade::kBaseMultiplier - t * cfg::kHalf;           // 終わりに近づくほど暗く
        const cfg::Rgb c = { vis::kExplosionColor.r * dim, vis::kExplosionColor.g * dim, vis::kExplosionColor.b * dim };
        p.Sphere(Xf(size, size, size, 0, vis::kExplosionY, 0, 0, (float)e.cell.x, 0, (float)e.cell.z), c);
    }
}

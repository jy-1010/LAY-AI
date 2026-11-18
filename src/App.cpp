#include "App.h"
#include "MeshGen.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

using namespace DirectX;
namespace ui = cfg::ui;

namespace {

constexpr const wchar_t* kSettingsFileName = L"settings.ini";   // 設定の保存先 (exe と同じフォルダ)
constexpr size_t kTextBufferSize = 128;                         // 文字列の作業用バッファ

// ---- メニューの項目 (表示文字列と、カーソル位置の番号を同じ並びで定義する) ----
const char* const kTitleItems[] = { "START", "SETTINGS", "QUIT" };
enum TitleItem { kTitleStart, kTitleSettings, kTitleQuit, kTitleItemCount };
static_assert(sizeof(kTitleItems) / sizeof(kTitleItems[0]) == kTitleItemCount, "タイトルの項目数が合っていない");

const char* const kPauseItems[] = { "RESUME", "RETRY STAGE", "SETTINGS", "QUIT TO TITLE" };
enum PauseItem { kPauseResume, kPauseRetry, kPauseSettings, kPauseQuit, kPauseItemCount };
static_assert(sizeof(kPauseItems) / sizeof(kPauseItems[0]) == kPauseItemCount, "ポーズの項目数が合っていない");

// 強化ショップ: 強化の項目 (0 .. kUpgradeCount-1) の後ろに 操作の項目を並べる
enum ShopRow { kShopRowStart = kUpgradeCount, kShopRowQuit, kShopRowCount };
const char* const kShopActionLabels[] = { "START NEXT STAGE", "QUIT TO TITLE" };

const char* const kRowLabels[] = { "FULLSCREEN", "VSYNC", "SHOW FPS", "MASTER VOLUME", "MUSIC VOLUME",
                                   "SFX VOLUME", "RESET TO DEFAULTS", "BACK" };
enum SettingsRow { kRowFullscreen, kRowVsync, kRowShowFps, kRowMaster, kRowMusic, kRowSfx,
                   kRowReset, kRowBack, kRowCount };
static_assert(sizeof(kRowLabels) / sizeof(kRowLabels[0]) == kRowCount, "設定の項目数が合っていない");

// exe のあるフォルダ (末尾に \ を含む)
std::wstring ExeDirectory()
{
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir(buf);
    return dir.substr(0, dir.find_last_of(L"\\/") + 1);
}

// アセット(shaders\...)を探す: exeの隣 → 親フォルダ … の順に最大4階層上まで
// (VS の x64\Debug から実行しても、プロジェクト直下の shaders が見つかるように)
std::wstring FindAsset(const wchar_t* relative)
{
    const std::wstring dir = ExeDirectory();
    const wchar_t* up[] = { L"", L"..\\", L"..\\..\\", L"..\\..\\..\\", L"..\\..\\..\\..\\" };
    for (auto u : up) {
        std::wstring p = dir + u + relative;
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return dir + relative;                                    // 見つからなければエラー表示用に既定パスを返す
}

// 持ち物の表示名と色
void ItemInfo(Item it, const char*& name, cfg::Rgb& color)
{
    switch (it) {
    case Item::Wood:  name = "WOOD";  color = cfg::vis::kTrunkColor;     break;
    case Item::Iron:  name = "IRON";  color = cfg::vis::kIronColor;      break;
    case Item::Track: name = "TRACK"; color = cfg::vis::kTrackItemColor; break;
    default:          name = "EMPTY"; color = cfg::ui::kColorBarOff;     break;
    }
}

// ショップで見せる、強化後の今の性能
void CurrentValueText(const Game& g, Upgrade u, char* out, size_t size)
{
    const float percent = (float)cfg::kPercent;
    switch (u) {
    case Upgrade::MoveSpeed:     snprintf(out, size, "x%.2f", g.MoveSpeedMultiplier());                      break;
    case Upgrade::CarryCapacity: snprintf(out, size, "%d", g.CarryMax());                                    break;
    case Upgrade::HarvestYield:  snprintf(out, size, "%d%%", (int)(g.YieldChance() * percent + cfg::kHalf)); break;
    case Upgrade::SmashChance:   snprintf(out, size, "%d%%", (int)(g.SmashChance() * percent + cfg::kHalf)); break;
    case Upgrade::CraftSpeed:    snprintf(out, size, "%.1fS", g.CraftSeconds());                             break;
    case Upgrade::TrackCapacity: snprintf(out, size, "%d", g.CrafterCapacity());                             break;
    case Upgrade::CargoCapacity: snprintf(out, size, "%d", g.CargoCapacity());                               break;
    default:                     out[0] = '\0';                                                             break;
    }
}

// 点滅: 周期のうち前半だけ表示する
bool BlinkOn(float t)
{
    return fmodf(t, ui::kBlinkPeriod) < ui::kBlinkOnTime;
}

} // namespace

// ---------------------------------------------------------------------------
// 初期化 (① ロード)
// ---------------------------------------------------------------------------
bool App::Init(HWND hwnd, UINT w, UINT h)
{
    hwnd_ = hwnd; w_ = w; h_ = h;

    if (!r_.Init(hwnd, w, h) || !r_.LoadShader(FindAsset(L"shaders\\Instancing.hlsl"))) {
        err_ = r_.LastError().empty() ? L"初期化に失敗しました" : r_.LastError();
        return false;
    }
    // 形ごとにメッシュを1つ登録 (同じ形の物は全部インスタンスとして1回で描く)
    mesh_.cube    = r_.AddMesh(MakeCube(),      cfg::kMaxCubeInstances);      // 地面・幹・線路・列車・建物・UI
    mesh_.sphere  = r_.AddMesh(MakeSphere(),    cfg::kMaxSphereInstances);    // 岩・頭・影
    mesh_.pyramid = r_.AddMesh(MakePyramid(),   cfg::kMaxPyramidInstances);   // 木の葉・屋根
    mesh_.hex     = r_.AddMesh(MakeHexPrism(),  cfg::kMaxHexInstances);       // ボルトの頭
    if (mesh_.cube < 0 || mesh_.sphere < 0 || mesh_.pyramid < 0 || mesh_.hex < 0) {
        err_ = r_.LastError();
        return false;
    }
    ui_.Init(mesh_.cube, w, h);

    // 設定の読み込みとサウンドの準備 (サウンドが使えなくてもゲームは動く)
    settingsPath_ = ExeDirectory() + kSettingsFileName;
    settings_.Load(settingsPath_);
    audio_.Init();
    ApplySettings();

    Enter(Scene::Title);
    fade_ = Fade::In;                   // 起動時は暗転からフェードイン
    fadeA_ = 1.0f;
    return true;
}

// ---------------------------------------------------------------------------
// 設定の反映 / フルスクリーン
// ---------------------------------------------------------------------------
void App::ApplySettings()
{
    SetFullscreen(settings_.fullscreen);
    r_.SetVSync(settings_.vsync);
    audio_.SetVolumes(settings_.masterVolume, settings_.musicVolume, settings_.sfxVolume);
}

// 枠なしウィンドウでモニター全体に広げる (解像度は変えない)。戻す時は元の枠・位置・大きさに復元する
void App::SetFullscreen(bool on)
{
    if (on == fullscreen_) return;

    if (on) {
        windowStyle_ = GetWindowLongPtrW(hwnd_, GWL_STYLE);          // 元に戻す時のために覚えておく
        GetWindowRect(hwnd_, &windowRect_);
        MONITORINFO mon = { sizeof(mon) };
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &mon);
        SetWindowLongPtrW(hwnd_, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hwnd_, HWND_TOP, mon.rcMonitor.left, mon.rcMonitor.top,
                     mon.rcMonitor.right - mon.rcMonitor.left, mon.rcMonitor.bottom - mon.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, windowStyle_);
        SetWindowPos(hwnd_, HWND_NOTOPMOST, windowRect_.left, windowRect_.top,
                     windowRect_.right - windowRect_.left, windowRect_.bottom - windowRect_.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    fullscreen_ = on;
}

// ウィンドウの大きさが変わっていたら、描画バッファと UI の座標系を合わせる
void App::UpdateWindowSize()
{
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const UINT cw = (UINT)(rc.right - rc.left), ch = (UINT)(rc.bottom - rc.top);
    if (cw == 0 || ch == 0) return;                                   // 最小化中
    if (cw == w_ && ch == h_) return;
    if (r_.Resize(cw, ch)) { w_ = cw; h_ = ch; ui_.SetScreen(cw, ch); }
}

void App::UpdateFps(float realDt)
{
    ++fpsFrames_;
    fpsTimer_ += realDt;
    if (fpsTimer_ >= ui::kFpsInterval) {
        fps_ = (float)fpsFrames_ / fpsTimer_;
        fpsFrames_ = 0;
        fpsTimer_ = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// シーン遷移
// ---------------------------------------------------------------------------
// 暗転つきの遷移を要求する
void App::Request(Scene next, StartMode mode, const MenuInput* mi)
{
    if (fade_ == Fade::Out) return;                     // 遷移中は受け付けない
    pending_ = next;
    startMode_ = mode;
    joinOnEnter_ = (scene_ == Scene::Title && next == Scene::Playing);   // タイトルから始める時だけ参加者を登録
    if (mi) pendingMenu_ = *mi;
    fade_ = Fade::Out;
}

// 暗転の底で呼ばれる: シーンを切り替え、ゲームの開始処理をする
void App::Enter(Scene s)
{
    switch (s) {
    case Scene::Title:
        input_.ClearPlayers(game_);                     // プレイヤー全員を退出
        game_.Reset((uint32_t)GetTickCount());          // 背景に見せるマップ
        demoT_ = 0.0f;
        break;
    case Scene::Playing:
        switch (startMode_) {                           // 参加中のプレイヤーはどれでも維持される
        case StartMode::New:   game_.Reset((uint32_t)GetTickCount());        break;   // ステージ1から
        case StartMode::Next:  game_.NextStage((uint32_t)GetTickCount());    break;   // 次のステージ (スコア・在庫を引き継ぎ)
        case StartMode::Retry: game_.RestartStage((uint32_t)GetTickCount()); break;   // 今のステージをやり直す
        }
        if (joinOnEnter_) {                             // タイトルで「決定」した入力を 1P として参加させる
            input_.JoinFromMenu(game_, pendingMenu_);
            joinOnEnter_ = false;
        }
        resultTimer_ = 0.0f;
        break;
    case Scene::Settings:
    case Scene::Paused:
    case Scene::Clear:
    case Scene::Shop:
    case Scene::GameOver:
        break;                                          // ゲームの状態はそのまま使う
    }
    GoTo(s);
}

// 暗転なしで切り替える。resetTime = false ならシーン内の時計を進めたままにする (ポーズからの再開用)
void App::GoTo(Scene s, bool resetTime)
{
    scene_ = s;
    cursor_ = 0;
    if (resetTime) time_ = 0.0f;
    UpdateBgm();
}

void App::OpenSettings(Scene from)
{
    settingsReturn_ = from;
    GoTo(Scene::Settings);
}

void App::CloseSettings()
{
    settings_.Save(settingsPath_);
    GoTo(settingsReturn_);
    cursor_ = (settingsReturn_ == Scene::Title) ? kTitleSettings : kPauseSettings;   // 開いた項目にカーソルを戻す
}

// シーンに合わせた BGM: タイトル/設定(ゲーム外) = メニュー曲, ゲーム中/ポーズ = ゲーム曲, 結果画面 = 無し
void App::UpdateBgm()
{
    Scene effective = scene_;
    if (scene_ == Scene::Settings) effective = settingsReturn_;      // 設定は開いた元の画面の曲を続ける
    switch (effective) {
    case Scene::Title:
    case Scene::Shop:     audio_.PlayBgm(Bgm::Menu); break;
    case Scene::Playing:
    case Scene::Paused:   audio_.PlayBgm(Bgm::Play); break;
    default:              audio_.PlayBgm(Bgm::None); break;
    }
}

// ---------------------------------------------------------------------------
// 更新
// ---------------------------------------------------------------------------
void App::UpdateFade(float dt)
{
    if (fade_ == Fade::Out) {
        fadeA_ += dt / ui::kFadeTime;
        if (fadeA_ >= 1.0f) { fadeA_ = 1.0f; Enter(pending_); fade_ = Fade::In; }   // 真っ暗になったらシーンを切り替える
    } else if (fade_ == Fade::In) {
        fadeA_ -= dt / ui::kFadeTime;
        if (fadeA_ <= 0.0f) { fadeA_ = 0.0f; fade_ = Fade::None; }
    }
}

// タイトル背景のマップを、スタート駅からゴール駅の方向へ往復スクロール
void App::UpdateDemoCamera(float dt)
{
    demoT_ += dt;
    const float minX = cfg::kCamMinX;
    const float span      = ((float)Game::W - cfg::kCamEndMargin) - minX;   // 往路の長さ
    const float roundTrip = span + span;                                    // 往復の長さ
    const float t = fmodf(demoT_ * ui::kTitleScrollSpeed, roundTrip);       // 0..往復の長さ
    game_.SetCameraX(minX + (t < span ? t : roundTrip - t));                // 後半は戻る
}

void App::MoveCursor(const MenuInput& mi, int count)
{
    if (mi.up)   { cursor_ = (cursor_ + count - 1) % count; audio_.PlaySfx(Sfx::Select); }
    if (mi.down) { cursor_ = (cursor_ + 1) % count;         audio_.PlaySfx(Sfx::Select); }
}

void App::Update(float dt, float realDt)
{
    time_ += dt;
    game_.Animate(dt);                                  // 水・ボルト・旗のアニメ (全シーン共通)
    UpdateFps(realDt);
    UpdateWindowSize();
    UpdateFade(dt);
    const bool canInput = (fade_ == Fade::None);        // 暗転中は入力を無視 (連打防止)

    switch (scene_) {
    case Scene::Title: {
        UpdateDemoCamera(dt);
        const MenuInput mi = input_.PollMenu(hwnd_, dt);   // 入力は毎フレーム読む (押した/離したの状態を保つため)
        if (canInput) UpdateTitle(mi);
        break;
    }
    case Scene::Settings: {
        if (settingsReturn_ == Scene::Title) UpdateDemoCamera(dt);
        const MenuInput mi = input_.PollMenu(hwnd_, dt);
        if (canInput) UpdateSettings(mi);
        break;
    }
    case Scene::Playing:
        UpdatePlaying(dt, canInput);
        break;
    case Scene::Paused: {
        const MenuInput mi = input_.PollMenu(hwnd_, dt);
        if (canInput) UpdatePaused(mi);
        break;
    }
    case Scene::Clear:
    case Scene::GameOver: {
        const MenuInput mi = input_.PollMenu(hwnd_, dt);
        if (canInput) UpdateResult(mi);
        break;
    }
    case Scene::Shop: {
        const MenuInput mi = input_.PollMenu(hwnd_, dt);
        if (canInput) UpdateShop(mi);
        break;
    }
    }
}

// ---- タイトル: START / SETTINGS / QUIT ----
void App::UpdateTitle(const MenuInput& mi)
{
    MoveCursor(mi, kTitleItemCount);
    if (!mi.confirm) return;

    audio_.PlaySfx(Sfx::Confirm);
    switch (cursor_) {
    case kTitleStart:    Request(Scene::Playing, StartMode::New, &mi); break;   // 決定した入力が 1P になる
    case kTitleSettings: OpenSettings(Scene::Title);                   break;
    case kTitleQuit:     PostQuitMessage(0);                           break;
    }
}

// ---- ゲーム中 ----
void App::UpdatePlaying(float dt, bool canInput)
{
    Input in[Game::MaxPlayers];
    bool pauseRequested = false;
    input_.Update(hwnd_, game_, in, pauseRequested);    // 毎フレーム呼ぶ (参加/退出の判定を含む)

    // ポーズ: 操作があった時、またはウィンドウが前面でなくなった時
    if (canInput && game_.GetState() == State::Playing && (pauseRequested || GetForegroundWindow() != hwnd_)) {
        audio_.PlaySfx(Sfx::Select);
        GoTo(Scene::Paused);
        return;
    }

    // 誰も参加していない間 (全員が退出/切断) は進行を止めて、参加を待つ
    if (fade_ != Fade::Out && game_.PlayerCount() > 0) game_.Update(dt, in);
    PlayGameEvents();

    if (game_.GetState() == State::Playing) {
        resultTimer_ = 0.0f;
    } else if (fade_ == Fade::None) {                   // 脱線/ゴールを少し見せてから結果画面へ
        resultTimer_ += dt;
        if (resultTimer_ >= ui::kResultDelay)
            Request(game_.GetState() == State::Clear ? Scene::Clear : Scene::GameOver);
    }
}

// ゲーム内の出来事 → 効果音 / BGM
void App::PlayGameEvents()
{
    for (GameEvent e : game_.TakeEvents()) {
        switch (e) {
        case GameEvent::Harvest:     audio_.PlaySfx(Sfx::Harvest);  break;
        case GameEvent::PlaceTrack:  audio_.PlaySfx(Sfx::Place);    break;
        case GameEvent::BuildBridge: audio_.PlaySfx(Sfx::Bridge);   break;
        case GameEvent::Bolt:        audio_.PlaySfx(Sfx::Bolt);     break;
        case GameEvent::Deposit:     audio_.PlaySfx(Sfx::Deposit);  break;
        case GameEvent::Pickup:      audio_.PlaySfx(Sfx::Pickup);   break;
        case GameEvent::Craft:       audio_.PlaySfx(Sfx::Craft);    break;
        case GameEvent::Deny:        audio_.PlaySfx(Sfx::Deny);     break;
        case GameEvent::Drop:        audio_.PlaySfx(Sfx::Drop);     break;
        case GameEvent::Smash:       audio_.PlaySfx(Sfx::Smash);    break;
        case GameEvent::Pushed:      audio_.PlaySfx(Sfx::Pushed);   break;
        case GameEvent::Respawn:     audio_.PlaySfx(Sfx::Respawn);  break;
        case GameEvent::RushStart:   audio_.PlaySfx(Sfx::Rush);     break;
        case GameEvent::Clear:       audio_.PlayBgm(Bgm::None); audio_.PlaySfx(Sfx::Clear);    break;   // BGM を止めてジングル
        case GameEvent::GameOver:    audio_.PlayBgm(Bgm::None); audio_.PlaySfx(Sfx::GameOver); break;
        }
    }
}

// ---- ポーズ: RESUME / RETRY STAGE / SETTINGS / QUIT TO TITLE ----
void App::UpdatePaused(const MenuInput& mi)
{
    if (mi.pause || mi.cancel) {                        // Esc / P / Start / B で再開
        audio_.PlaySfx(Sfx::Back);
        GoTo(Scene::Playing, false);
        return;
    }
    MoveCursor(mi, kPauseItemCount);
    if (!mi.confirm) return;

    audio_.PlaySfx(Sfx::Confirm);
    switch (cursor_) {
    case kPauseResume:   GoTo(Scene::Playing, false);                 break;
    case kPauseRetry:    Request(Scene::Playing, StartMode::Retry);   break;
    case kPauseSettings: OpenSettings(Scene::Paused);                 break;
    case kPauseQuit:     Request(Scene::Title);                       break;
    }
}

// ---- 設定: 上下で項目を選び、左右で値を変える ----
void App::UpdateSettings(const MenuInput& mi)
{
    if (mi.cancel) {                                    // Esc / B で保存して戻る
        audio_.PlaySfx(Sfx::Back);
        CloseSettings();
        return;
    }
    MoveCursor(mi, kRowCount);

    const int dir = (mi.right ? 1 : 0) - (mi.left ? 1 : 0);     // 左右の入力 (-1, 0, +1)
    bool changed = false;
    auto toggle = [&](bool& value) {                            // ON/OFF の項目: 左右か決定で切り替え
        if (dir != 0 || mi.confirm) { value = !value; changed = true; }
    };
    auto stepVolume = [&](int& value) {                         // 音量の項目: 左右で 1段階ずつ
        const int next = std::min(std::max(value + dir, 0), cfg::audio::kVolumeSteps);
        if (dir != 0 && next != value) { value = next; changed = true; }
    };

    switch (cursor_) {
    case kRowFullscreen: toggle(settings_.fullscreen);      break;
    case kRowVsync:      toggle(settings_.vsync);           break;
    case kRowShowFps:    toggle(settings_.showFps);         break;
    case kRowMaster:     stepVolume(settings_.masterVolume); break;
    case kRowMusic:      stepVolume(settings_.musicVolume);  break;
    case kRowSfx:        stepVolume(settings_.sfxVolume);    break;
    case kRowReset:
        if (mi.confirm) { settings_.ResetToDefaults(); changed = true; }
        break;
    case kRowBack:
        if (mi.confirm) { audio_.PlaySfx(Sfx::Back); CloseSettings(); return; }
        break;
    }
    if (changed) {
        ApplySettings();
        audio_.PlaySfx(Sfx::Select);                    // 変更後の音量で確認音を鳴らす
    }
}

// ---- 結果画面 (ステージクリア / ゲームオーバー) ----
void App::UpdateResult(const MenuInput& mi)
{
    const bool cleared = (scene_ == Scene::Clear);
    if (mi.back) {                                                          // タイトルへ
        audio_.PlaySfx(Sfx::Back);
        Request(Scene::Title);
    } else if (mi.retry) {                                                  // 同じメンバーで今のステージをやり直す
        audio_.PlaySfx(Sfx::Confirm);
        Request(Scene::Playing, StartMode::Retry);
    } else if (mi.confirm) {
        audio_.PlaySfx(Sfx::Confirm);
        if (cleared) Request(Scene::Shop);                                  // クリア → 強化ショップへ (そこから次のステージへ)
        else         Request(Scene::Title);                                 // ゲームオーバー → タイトルへ
    }
}

// ---- 強化ショップ: ボルトで 強化を買い、準備ができたら次のステージへ ----
void App::UpdateShop(const MenuInput& mi)
{
    MoveCursor(mi, kShopRowCount);
    if (!mi.confirm) return;

    if (cursor_ < kUpgradeCount) {                                          // 強化を購入
        if (game_.BuyUpgrade((Upgrade)cursor_)) audio_.PlaySfx(Sfx::Buy);
        else                                    audio_.PlaySfx(Sfx::Deny);   // ボルト不足 / 最大レベル
        return;
    }
    audio_.PlaySfx(Sfx::Confirm);
    if (cursor_ == kShopRowStart) Request(Scene::Playing, StartMode::Next);  // 次のステージへ (強化はそのまま続く)
    else                          Request(Scene::Title);
}

// ---------------------------------------------------------------------------
// 描画 (② 描画): 3Dのワールド → UI の2パス
// ---------------------------------------------------------------------------
void App::Render()
{
    if (IsIconic(hwnd_)) { Sleep(cfg::kMinimizedSleepMs); return; }   // 最小化中は描かない

    const float sky[4] = { cfg::kSkyColor.r, cfg::kSkyColor.g, cfg::kSkyColor.b, cfg::kOpaque };
    r_.BeginFrame(sky);

    // --- パス1: 3Dワールド (全シーン共通の背景) ---
    XMFLOAT3 eye, target;
    game_.Camera(eye, target);
    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&eye), XMLoadFloat3(&target), XMVectorSet(0, 1, 0, 0));   // 上方向 = +Y
    XMMATRIX proj = XMMatrixPerspectiveFovLH(cfg::kFovY, (float)w_ / (float)h_, cfg::kNearZ, cfg::kFarZ);
    game_.Render(r_, mesh_);
    r_.Flush(view * proj, true, false, false);

    // --- パス2: UI (正射影 / ライティングなし / アルファブレンド) ---
    switch (scene_) {
    case Scene::Title:    DrawTitleUi();         break;
    case Scene::Settings: DrawSettingsUi();      break;
    case Scene::Playing:  DrawPlayingUi();       break;
    case Scene::Paused:   DrawPausedUi();        break;
    case Scene::Clear:    DrawResultUi(true);    break;
    case Scene::Shop:     DrawShopUi();          break;
    case Scene::GameOver: DrawResultUi(false);   break;
    }

    if (settings_.showFps) {                                 // 画面右上に FPS
        char buf[kTextBufferSize];
        snprintf(buf, sizeof(buf), "FPS:%d", (int)(fps_ + cfg::kHalf));   // 四捨五入
        ui_.Text(r_, buf, ui_.Width() - ui_.TextWidth(buf, ui::kFpsPx) - ui::kFpsMargin, ui::kFpsMargin,
                 ui::kFpsPx, ToColor(ui::kColorGray));
    }
    if (fadeA_ > 0.0f)                                       // フェード用の暗幕 (最前面なので最後に Submit)
        ui_.Rect(r_, 0, 0, ui_.Width(), ui_.Height(), XMFLOAT4(0, 0, 0, fadeA_), ui::kZFade);
    r_.Flush(ui_.Projection(), false, true, true);

    r_.Present();
}

// ---------------------------------------------------------------------------
// 各シーンのUI
// ---------------------------------------------------------------------------
// 縦に並べたメニュー。選んでいる項目は金色で「> 項目 <」と表示する
void App::DrawMenu(const char* const* items, int count, float topY)
{
    char buf[kTextBufferSize];
    for (int i = 0; i < count; ++i) {
        const bool selected = (i == cursor_);
        if (selected) snprintf(buf, sizeof(buf), "> %s <", items[i]);
        else          snprintf(buf, sizeof(buf), "%s", items[i]);
        ui_.TextCenter(r_, buf, topY + (float)i * ui::kMenuStep, ui::kMenuPx,
                       ToColor(selected ? ui::kColorGold : ui::kColorWhite));
    }
}

void App::DrawTitleUi()
{
    ui_.Rect(r_, 0, 0, ui_.Width(), ui_.Height(), ToColor(ui::kTitleDim), ui::kZDim);   // 背景を少し暗くする

    ui_.TextCenter(r_, "LAY", ui::kTitleLogoY, ui::kTitleLogoPx, ToColor(ui::kColorGold));
    ui_.TextCenter(r_, "BLUE CHICKEN (FINAL)", ui::kTitleSubY, ui::kTitleSubPx, ToColor(ui::kColorWhite));

    DrawMenu(kTitleItems, kTitleItemCount, ui::kMenuTopY);

    ui_.TextCenter(r_, "1P: WASD E Q    2P: ARROWS ENTER RSHIFT    PAD: STICK A X",
                   ui::kTitleHint1Y, ui::kTitleHintPx, ToColor(ui::kColorGray));
    ui_.TextCenter(r_, "UP TO 4 PLAYERS    THE DEVICE YOU START WITH BECOMES 1P",
                   ui::kTitleHint2Y, ui::kTitleHintPx, ToColor(ui::kColorGray));
}

void App::DrawPlayingUi()
{
    char buf[kTextBufferSize];

    // ---- 大きな表示 (最優先で先に出す) ----
    // 誰も参加していない時は参加を促す
    if (game_.PlayerCount() == 0 && BlinkOn(time_))
        ui_.TextCenter(r_, "PRESS E / ENTER / PAD A TO JOIN", ui::kBannerY, ui::kJoinPromptPx,
                       ToColor(ui::kColorWhite));

    // ステージ開始直後は「STAGE n」
    if (game_.GetState() == State::Playing && time_ < ui::kStageBannerTime) {
        snprintf(buf, sizeof(buf), "STAGE %d", game_.Stage());
        ui_.TextCenter(r_, buf, ui::kBannerY, ui::kBannerPx, ToColor(ui::kColorWhite));
    }

    // 線路がゴールまで繋がり、列車が全速力で向かっている間「GOAL RUSH!!」(脈動する)
    if (game_.IsRushing()) {
        const float pulse = cfg::upgrade::kBaseMultiplier + ui::kBannerPulseAmp * sinf(time_ * ui::kBannerPulseSpeed);
        ui_.TextCenter(r_, "GOAL RUSH!!", ui::kBannerY, ui::kBannerPx * pulse, ToColor(ui::kColorGold));
    }

    // 終了直後 (結果画面に切り替わるまでの間)
    if (game_.GetState() == State::GameOver)
        ui_.TextCenter(r_, "DERAILED!", ui::kBannerY, ui::kBannerPx, ToColor(ui::kColorRed));
    else if (game_.GetState() == State::Clear)
        ui_.TextCenter(r_, "GOAL!", ui::kBannerY, ui::kBannerPx, ToColor(ui::kColorGold));

    // ---- HUD ----
    // チームの状態: 貨物車両の中身 (木・鉄 / 積載量)
    snprintf(buf, sizeof(buf), "STAGE:%d  WOOD:%d/%d  IRON:%d/%d",
             game_.Stage(), game_.Wood(), game_.CargoCapacity(), game_.Iron(), game_.CargoCapacity());
    ui_.Text(r_, buf, ui::kHudX, ui::kHudLine1Y, ui::kHudLine1Px, ToColor(ui::kColorWhite));

    // レール作成車両の線路 / 線路の長さ / 列車 / ボルト (ショップで使える数)
    snprintf(buf, sizeof(buf), "TRACKS READY:%d/%d  TRAIN:%.2f  BOLT:%d",
             game_.Tracks(), game_.CrafterCapacity(), game_.TrainSpeed(), game_.Bolts());
    ui_.Text(r_, buf, ui::kHudX, ui::kHudLine2Y, ui::kHudLine2Px, ToColor(ui::kColorGray));

    // プレイヤーごとの持ち物: [プレイヤー色] P1  [持ち物の色] WOOD x2/3
    float rowY = ui::kHudPlayerTopY;
    for (int i = 0; i < Game::MaxPlayers; ++i) {
        if (!game_.PlayerActive(i)) continue;
        const HeldItem held = game_.PlayerHeld(i);
        const char* itemName; cfg::Rgb itemColor;
        ItemInfo(held.item, itemName, itemColor);

        float x = ui::kHudX;
        ui_.Rect(r_, x, rowY, ui::kHudSwatchSize, ui::kHudSwatchSize, ToColor(cfg::kPlayerColors[i]), ui::kZText);
        x += ui::kHudSwatchSize + ui::kHudSwatchGap;

        snprintf(buf, sizeof(buf), "P%d", i + 1);
        ui_.Text(r_, buf, x, rowY, ui::kHudPlayerPx, ToColor(ui::kColorWhite));
        x += ui_.TextWidth(buf, ui::kHudPlayerPx) + ui::kHudSwatchGap;

        ui_.Rect(r_, x, rowY, ui::kHudSwatchSize, ui::kHudSwatchSize, ToColor(itemColor), ui::kZText);
        x += ui::kHudSwatchSize + ui::kHudSwatchGap;

        if (held.item == Item::None) snprintf(buf, sizeof(buf), "%s", itemName);
        else                         snprintf(buf, sizeof(buf), "%s %d/%d", itemName, held.count, game_.CarryMax());
        ui_.Text(r_, buf, x, rowY, ui::kHudPlayerPx,
                 ToColor(held.item == Item::None ? ui::kColorGray : ui::kColorWhite));
        rowY += ui::kHudPlayerStep;
    }

    // 操作のヒント (画面下。下の行から順に積む)
    const char* const hints[] = {
        "E / ENTER / A: PICK UP (ITEM, GROUND), DEPOSIT TO CARGO, TAKE TRACKS",
        "Q / RSHIFT / X: PLACE TRACK, BUILD BRIDGE (HOLD WOOD)",
        "F / RCTRL / B: DROP HELD ITEM    PAUSE: ESC / START",
        "JOIN: E / ENTER / PAD A",
    };
    const int hintCount = (int)(sizeof(hints) / sizeof(hints[0]));
    for (int i = 0; i < hintCount; ++i) {
        const float y = ui_.Height() - ui::kHudBottomMargin - ui::kHudHintStep * (float)(hintCount - 1 - i);
        ui_.Text(r_, hints[i], ui::kHudX, y, ui::kHudLine2Px, ToColor(ui::kColorGray));
    }
}

void App::DrawPausedUi()
{
    // ゲーム画面 (止まったまま) の上に暗幕とメニュー
    ui_.Rect(r_, 0, 0, ui_.Width(), ui_.Height(), ToColor(ui::kPauseDim), ui::kZDim);
    ui_.TextCenter(r_, "PAUSED", ui::kPauseTitleY, ui::kPauseTitlePx, ToColor(ui::kColorWhite));
    DrawMenu(kPauseItems, kPauseItemCount, ui::kPauseMenuTopY);
    ui_.TextCenter(r_, "ESC / START: RESUME", ui::kPauseHintY, ui::kHintPx, ToColor(ui::kColorGray));
}

// 音量バー: 1段階 = 1マス。選んでいる項目は金色、数字も添える
void App::DrawVolumeBar(float x, float y, int value, bool selected)
{
    const cfg::Rgb& on = selected ? ui::kColorGold : ui::kColorWhite;
    for (int k = 0; k < cfg::audio::kVolumeSteps; ++k) {
        const float cx = x + (float)k * (ui::kVolumeCellW + ui::kVolumeCellGap);
        ui_.Rect(r_, cx, y, ui::kVolumeCellW, ui::kVolumeCellH, ToColor(k < value ? on : ui::kColorBarOff), ui::kZText);
    }
    char buf[kTextBufferSize];
    snprintf(buf, sizeof(buf), "%d", value);
    ui_.Text(r_, buf, x + (float)cfg::audio::kVolumeSteps * (ui::kVolumeCellW + ui::kVolumeCellGap) + ui::kVolumeNumberGap,
             y, ui::kSettingsTextPx, ToColor(on));
}

void App::DrawSettingsUi()
{
    ui_.Rect(r_, 0, 0, ui_.Width(), ui_.Height(), ToColor(ui::kSettingsDim), ui::kZDim);
    ui_.TextCenter(r_, "SETTINGS", ui::kSettingsTitleY, ui::kSettingsTitlePx, ToColor(ui::kColorGold));

    const float panelX = (ui_.Width() - ui::kSettingsPanelWidth) * cfg::kHalf;     // 項目の並びを画面中央に置く
    const float px = ui::kSettingsTextPx;
    for (int row = 0; row < kRowCount; ++row) {
        const float y = ui::kSettingsTopY + (float)row * ui::kSettingsRowStep;
        const bool selected = (row == cursor_);
        const XMFLOAT4 color = ToColor(selected ? ui::kColorGold : ui::kColorWhite);

        if (selected) ui_.Text(r_, ">", panelX - ui::kGlyphAdvance * px, y, px, color);   // 選択中の印
        ui_.Text(r_, kRowLabels[row], panelX, y, px, color);

        const float valueX = panelX + ui::kSettingsValueOffset;
        switch (row) {
        case kRowFullscreen: ui_.Text(r_, settings_.fullscreen ? "ON" : "OFF", valueX, y, px, color); break;
        case kRowVsync:      ui_.Text(r_, settings_.vsync      ? "ON" : "OFF", valueX, y, px, color); break;
        case kRowShowFps:    ui_.Text(r_, settings_.showFps    ? "ON" : "OFF", valueX, y, px, color); break;
        case kRowMaster:     DrawVolumeBar(valueX, y, settings_.masterVolume, selected); break;
        case kRowMusic:      DrawVolumeBar(valueX, y, settings_.musicVolume,  selected); break;
        case kRowSfx:        DrawVolumeBar(valueX, y, settings_.sfxVolume,    selected); break;
        default: break;                                      // RESET / BACK は値なし
        }
    }
    ui_.TextCenter(r_, "UP / DOWN: SELECT    LEFT / RIGHT: CHANGE    ESC / B: BACK",
                   ui::kSettingsHintY, ui::kHintPx, ToColor(ui::kColorGray));
}

// 強化ショップ: 上にプレイヤーの強化、下に列車の強化。ボルトで買い、レベルバーで現在の強さを見る
void App::DrawShopUi()
{
    ui_.Rect(r_, 0, 0, ui_.Width(), ui_.Height(), ToColor(ui::kSettingsDim), ui::kZDim);
    ui_.TextCenter(r_, "UPGRADE SHOP", ui::kShopTitleY, ui::kShopTitlePx, ToColor(ui::kColorGold));

    char buf[kTextBufferSize];
    snprintf(buf, sizeof(buf), "BOLTS: %d", game_.Bolts());
    ui_.TextCenter(r_, buf, ui::kShopBoltsY, ui::kShopBoltsPx, ToColor(ui::kColorGold));

    const float panelX = (ui_.Width() - ui::kShopPanelWidth) * cfg::kHalf;      // 項目の並びを画面中央に置く
    const float px = ui::kShopTextPx;
    float y = ui::kShopTopY;

    for (int i = 0; i < kUpgradeCount; ++i) {
        const UpgradeInfo& info = kUpgradeInfo[i];
        if (i == 0 || info.forTrain != kUpgradeInfo[i - 1].forTrain) {          // プレイヤー / 列車 の見出し
            ui_.Text(r_, info.forTrain ? "TRAIN" : "PLAYERS", panelX, y, ui::kShopHeaderPx, ToColor(ui::kColorGreen));
            y += ui::kShopRowStep;
        }
        const Upgrade u = (Upgrade)i;
        const bool selected = (cursor_ == i);
        const XMFLOAT4 color = ToColor(selected ? ui::kColorGold : ui::kColorWhite);

        if (selected) ui_.Text(r_, ">", panelX - ui::kGlyphAdvance * px, y, px, color);   // 選択中の印
        ui_.Text(r_, info.name, panelX, y, px, color);

        // レベルバー (1マス = 1レベル)
        const int level = game_.UpgradeLevel(u);
        const cfg::Rgb& on = info.forTrain ? ui::kColorGreen : ui::kColorGold;
        for (int k = 0; k < info.maxLevel; ++k)
            ui_.Rect(r_, panelX + ui::kShopBarOffset + (float)k * (ui::kShopCellW + ui::kShopCellGap), y,
                     ui::kShopCellW, ui::kShopCellH, ToColor(k < level ? on : ui::kColorBarOff), ui::kZText);

        // 値段: 買えれば白 / ボルト不足なら赤 / 最大レベルなら MAX
        const int cost = game_.UpgradeCost(u);
        if (cost < 0) {
            ui_.Text(r_, "MAX", panelX + ui::kShopCostOffset, y, px, ToColor(ui::kColorGreen));
        } else {
            snprintf(buf, sizeof(buf), "COST %d", cost);
            ui_.Text(r_, buf, panelX + ui::kShopCostOffset, y, px,
                     ToColor(game_.CanBuy(u) ? ui::kColorWhite : ui::kColorRed));
        }
        y += ui::kShopRowStep;
    }

    y += ui::kShopRowStep * cfg::kHalf;                                         // 強化と操作の間を少し空ける
    for (int action = 0; action < 2; ++action) {
        const bool selected = (cursor_ == kShopRowStart + action);
        const XMFLOAT4 color = ToColor(selected ? ui::kColorGold : ui::kColorWhite);
        if (selected) ui_.Text(r_, ">", panelX - ui::kGlyphAdvance * px, y, px, color);
        ui_.Text(r_, kShopActionLabels[action], panelX, y, px, color);
        y += ui::kShopRowStep;
    }

    // 選んでいる強化の説明と、今の性能
    if (cursor_ < kUpgradeCount) {
        char now[kTextBufferSize];
        CurrentValueText(game_, (Upgrade)cursor_, now, sizeof(now));
        snprintf(buf, sizeof(buf), "%s   NOW: %s", kUpgradeInfo[cursor_].effect, now);
        ui_.TextCenter(r_, buf, ui::kShopDescY, ui::kHintPx, ToColor(ui::kColorWhite));
    }
    ui_.TextCenter(r_, "UP / DOWN: SELECT    ENTER / A: BUY    BOLTS ARE SHARED BY THE TEAM",
                   ui::kShopHintY, ui::kHintPx, ToColor(ui::kColorGray));
}

void App::DrawResultUi(bool clear)
{
    char buf[kTextBufferSize];

    if (clear) {
        ui_.Rect(r_, 0, 0, ui_.Width(), ui_.Height(), ToColor(ui::kClearDim), ui::kZDim);
        ui_.TextCenter(r_, "STAGE CLEAR", ui::kResultTitleY, ui::kResultTitlePx, ToColor(ui::kColorGold));
        snprintf(buf, sizeof(buf), "TRACKS LAID: %d", game_.PathLength());
        ui_.TextCenter(r_, buf, ui::kResultLine1Y, ui::kResultLine1Px, ToColor(ui::kColorGreen));
    } else {
        ui_.Rect(r_, 0, 0, ui_.Width(), ui_.Height(), ToColor(ui::kOverDim), ui::kZDim);
        ui_.TextCenter(r_, "GAME OVER", ui::kResultTitleY, ui::kResultTitlePx, ToColor(ui::kColorRed));
        snprintf(buf, sizeof(buf), "DISTANCE: %d / %d", game_.PathLength(), Game::W);
        ui_.TextCenter(r_, buf, ui::kResultLine1Y, ui::kResultLine1Px, ToColor(ui::kColorWhite));
    }

    snprintf(buf, sizeof(buf), "BOLTS: %d", game_.Bolts());
    ui_.TextCenter(r_, buf, ui::kResultLine2Y, ui::kResultLine2Px, ToColor(ui::kColorGold));
    snprintf(buf, sizeof(buf), "SCORE: %d", game_.Score());
    ui_.TextCenter(r_, buf, ui::kResultLine3Y, ui::kResultLine3Px, ToColor(ui::kColorWhite));
    snprintf(buf, sizeof(buf), "STAGE: %d  PLAYERS: %d", game_.Stage(), game_.PlayerCount());
    ui_.TextCenter(r_, buf, ui::kResultLine4Y, ui::kResultLine4Px, ToColor(ui::kColorGray));

    if (BlinkOn(time_)) {
        const char* prompt = clear ? "ENTER / A: UPGRADE SHOP      T / BACK: TITLE"
                                   : "ENTER / A: TITLE      R / Y: RETRY";
        ui_.TextCenter(r_, prompt, ui::kResultPromptY, ui::kResultPromptPx, ToColor(ui::kColorWhite));
    }
}

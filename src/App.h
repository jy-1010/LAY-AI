#pragma once
#include <string>
#include "Renderer.h"
#include "Game.h"
#include "Input.h"
#include "Ui.h"
#include "Audio.h"
#include "Settings.h"

// シーン遷移:
//   Title ──START──▶ Playing(ステージ1) ──(列車が終端に到達)──▶ Clear / GameOver
//                                                    Clear ──▶ Shop (ボルトで強化) ──▶ 次のステージ
//     │  ▲               │  ▲  ▲                                    │
//     │  │          Esc/Start│  └─ Clear: Enter / A = 次のステージ ──┤
//     │  │               ▼  │     R / Y = 今のステージをやり直す ────┤
//     │  │            Paused ───RETRY / QUIT TO TITLE                │
//     │  └─────────── Clear: T / Back,  GameOver: Enter / A ─────────┘
//     └──SETTINGS──▶ Settings ◀──SETTINGS── Paused     (戻ると元の画面へ)
//   大きな遷移 (タイトル⇄ゲーム⇄結果) は「暗転 → シーン切替 → 明転」、
//   メニューの出し入れ (ポーズ / 設定) は即座に切り替える。
class App {
public:
    bool Init(HWND hwnd, UINT w, UINT h);       // ①ロード: 描画器・メッシュ・UI・設定・サウンド・ゲームの準備
    void Update(float dt, float realDt);        // シーンごとの更新 (dt = 上限付きの経過時間, realDt = 実際の経過時間)
    void Render();                              // ②描画: 3Dワールド → UI の2パス
    const std::wstring& LastError() const { return err_; }

private:
    enum class Scene { Title, Settings, Playing, Paused, Clear, Shop, GameOver };
    enum class Fade  { None, Out, In };         // Out = 暗転中, In = 明転中
    // Playing の始め方: 最初から / 次のステージ / 今のステージをやり直す
    enum class StartMode { New, Next, Retry };

    // ---- 遷移 ----
    void Request(Scene next, StartMode mode = StartMode::New, const MenuInput* mi = nullptr);   // 暗転つきの遷移を要求
    void Enter(Scene s);                        // 暗転の底でシーンを切り替える (ゲームの開始処理を含む)
    void GoTo(Scene s, bool resetTime = true);  // 暗転なしで即切り替え (メニューの出し入れ用)
    void OpenSettings(Scene from);              // 設定を開く (閉じたら from に戻る)
    void CloseSettings();                       // 設定を保存して元の画面へ戻る
    void UpdateBgm();                           // シーンに合わせて BGM を切り替える

    // ---- 更新 ----
    void UpdateFade(float dt);
    void UpdateWindowSize();                    // ウィンドウのサイズ変化 (フルスクリーン切替を含む) に追従する
    void UpdateFps(float realDt);
    void UpdateDemoCamera(float dt);            // タイトル背景のカメラ移動
    void UpdateTitle(const MenuInput& mi);
    void UpdatePlaying(float dt, bool canInput);
    void UpdatePaused(const MenuInput& mi);
    void UpdateSettings(const MenuInput& mi);
    void UpdateResult(const MenuInput& mi);
    void UpdateShop(const MenuInput& mi);       // ステージクリア後の強化ショップ
    void MoveCursor(const MenuInput& mi, int count);   // 上下でカーソルを動かす (端はループ)
    void PlayGameEvents();                      // ゲーム内の出来事を効果音にする

    // ---- 設定の反映 ----
    void ApplySettings();                       // フルスクリーン / 垂直同期 / 音量 を反映
    void SetFullscreen(bool on);                // 枠なしウィンドウで画面いっぱいにする / 元に戻す

    // ---- 描画 ----
    void DrawTitleUi();
    void DrawPlayingUi();
    void DrawPausedUi();
    void DrawSettingsUi();
    void DrawResultUi(bool clear);
    void DrawShopUi();
    void DrawMenu(const char* const* items, int count, float topY);
    void DrawVolumeBar(float x, float y, int value, bool selected);

    HWND hwnd_ = nullptr;
    UINT w_ = 0, h_ = 0;                        // 現在の画面サイズ (ピクセル)

    Renderer     r_;
    Game::Meshes mesh_;
    Ui           ui_;
    Game         game_;
    InputManager input_;
    Audio        audio_;
    Settings     settings_;
    std::wstring settingsPath_;

    Scene scene_   = Scene::Title;
    Scene pending_ = Scene::Title;              // フェードアウト後に切り替える先
    Scene settingsReturn_ = Scene::Title;       // 設定を閉じた後に戻る画面
    StartMode startMode_ = StartMode::New;      // pending_ が Playing の時の始め方
    Fade  fade_    = Fade::None;
    float fadeA_   = 0.0f;                      // 暗幕の濃さ 0..1
    int   cursor_  = 0;                         // メニューのカーソル位置

    MenuInput pendingMenu_;                     // タイトルで決定した入力 (1P になるキーボード / パッド)
    bool  joinOnEnter_ = false;                 // 次のシーン開始時にプレイヤーを参加させるか
    float time_ = 0.0f;                         // シーン内経過時間 (点滅・バナー用)
    float demoT_ = 0.0f;                        // タイトル背景のカメラ移動用の時計
    float resultTimer_ = 0.0f;                  // ゲーム終了 → 結果画面までの待ち時間

    // フルスクリーン切替の復元用 / FPS 計測
    bool   fullscreen_ = false;
    LONG_PTR windowStyle_ = 0;
    RECT   windowRect_ = {};
    float  fpsTimer_ = 0.0f, fps_ = 0.0f;
    int    fpsFrames_ = 0;

    std::wstring err_;
};

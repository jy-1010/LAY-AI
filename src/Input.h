#pragma once
#include "Game.h"
#include "KeyBindings.h"

// 入力の管理 (キーボード2人分 + XInputパッド最大4台)。
//   入力ソースごとに「参加」するとプレイヤー番号が割り当てられ、Game に AddPlayer する (参加した順が 1P, 2P …)。
//   キーボードも固定ではなく、タイトルで決定した入力 (キーボード or パッド) が 1P になる。
//
//   キー配置はキーコンフィグ (KeyBindings) で変更できる。初期値:
//     キーボード1 : WASD 移動 / E 採取 / Q 設置 / F 置く
//     キーボード2 : 矢印 移動 / Enter 採取 / 右Shift 設置 / 右Ctrl 置く
//     パッド      : 左スティック/十字キー 移動 / A 採取 / X 設置 / B 置く
//   参加: キーボードは 採取 か 設置 のキー, パッドは A か Start
//   退出: キーボード1 = Backspace, キーボード2 = Delete, パッド = Back (切断でも自動退出)。最後の1人は退出できない
//   ポーズ: Esc / P / パッドの Start。R = ステージのやり直し

// タイトル/ポーズ/設定/結果画面など、ゲーム外のメニュー入力
struct MenuInput {
    bool confirm = false;           // Enter / Space / パッド A・Start
    bool cancel  = false;           // Esc / パッド B
    bool retry   = false;           // R / パッド Y
    bool back    = false;           // T / パッド Back (タイトルへ戻る)
    bool pause   = false;           // Esc / P / パッド Start (ポーズの切替)
    bool up = false, down = false, left = false, right = false;   // 方向 (押しっぱなしで連続入力)
    bool     keyboard = false;      // confirm を押したのがキーボードか
    unsigned padMask  = 0;          // confirm (A / Start) を押したパッド (bit0..3)
};

class InputManager {
public:
    InputManager();

    // ゲーム中: out[プレイヤー番号] に各プレイヤーの入力を書き込む (参加/退出の判定も行う)
    //   kb: 今のキー配置 / pauseRequested: ポーズの操作があった
    void Update(HWND hwnd, Game& game, const KeyBindings& kb, Input out[Game::MaxPlayers], bool& pauseRequested);

    MenuInput PollMenu(HWND hwnd, float dt);                // メニュー用 (dt は連続入力の計時に使う)
    void ClearPlayers(Game& game);                          // 全員を退出させる
    void JoinFromMenu(Game& game, const MenuInput& mi);     // タイトルで決定した入力 (キーボード / パッド) を 1P として参加させる

    // キーコンフィグ用: 「次に押されたキー / ボタン」を受け取る (押された瞬間に true)
    bool CaptureKey(HWND hwnd, int& vk);                    // 割り当てられるキー (IsCapturableKey) か Esc
    bool CapturePadButton(unsigned& mask);                  // 割り当てられるパッドのボタン (IsPadButtonAssignable)

private:
    // 入力ソース: キーボード2種類 + パッド (cfg::kMaxPads 台)
    enum Source { KB1, KB2, PAD0, SourceCount = PAD0 + cfg::kMaxPads };
    enum Dir { DirUp, DirDown, DirLeft, DirRight, DirCount };
    static constexpr int kKeyCount = 256;       // 仮想キーコードの数 (エッジ検出用の配列の大きさ)

    void BeginFrame();                          // 1フレームの最初に呼ぶ (Edge の結果の保存を空にする)
    bool Held(int vk, bool active) const;       // キーが押されている間 true
    bool Edge(int vk, bool active);             // 押された瞬間だけ true (同じキーを何度聞いても同じ答え)
    bool Repeat(Dir d, bool held, float dt);    // 押した瞬間 + 押しっぱなしの連続入力

    int   slot_[SourceCount];                   // 各入力ソースのプレイヤー番号 (-1 = 未参加)
    bool  prev_[kKeyCount];                     // 前フレームのキー状態
    bool  edgeValid_[kKeyCount];                // 今フレームの Edge をもう計算したか (同じキーを2つの操作に割り当てても大丈夫なように)
    bool  edgeValue_[kKeyCount];
    WORD  prevButtons_[cfg::kMaxPads];          // 前フレームのパッドのボタン状態
    float holdTime_[DirCount];                  // 方向キーを押し続けている時間 (-1 = 押していない)
};

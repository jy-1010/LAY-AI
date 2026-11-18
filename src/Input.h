#pragma once
#include "Game.h"

// 入力の管理 (キーボード2人分 + XInputパッド最大4台)。
//   入力ソースごとに「参加」するとプレイヤー番号が割り当てられ、Game に AddPlayer する (参加した順が 1P, 2P …)。
//   キーボードも固定ではなく、タイトルで決定した入力 (キーボード or パッド) が 1P になる。
//
//   キーボード1 : WASD 移動 / E(Space) 採取 / Q 設置 / F 置く / R リスタート
//                 E か Space で参加, Backspace で退出 (キーボード2が未参加の間は 矢印キーでも移動できる)
//   キーボード2 : 矢印 移動 / Enter 採取 / 右Shift 設置 / 右Ctrl 置く  (Enter か 右Shift で参加, Delete で退出)
//   パッド      : 左スティック/十字キー 移動 / A 採取 / X 設置 / B 置く / Start ポーズ
//                 (A か Start で参加, Back で退出, 切断で自動退出)
//   ポーズ      : Esc / P / パッドの Start
//   ※最後の1人は退出できない (退出は2人以上の時だけ)

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
    //   pauseRequested: ポーズの操作があった
    void Update(HWND hwnd, Game& game, Input out[Game::MaxPlayers], bool& pauseRequested);

    MenuInput PollMenu(HWND hwnd, float dt);                // メニュー用 (dt は連続入力の計時に使う)
    void ClearPlayers(Game& game);                          // 全員を退出させる
    void JoinFromMenu(Game& game, const MenuInput& mi);     // タイトルで決定した入力 (キーボード / パッド) を 1P として参加させる

private:
    // 入力ソース: キーボード2種類 + パッド (cfg::kMaxPads 台)
    enum Source { KB1, KB2, PAD0, SourceCount = PAD0 + cfg::kMaxPads };
    enum Dir { DirUp, DirDown, DirLeft, DirRight, DirCount };
    static constexpr int kKeyCount = 256;       // 仮想キーコードの数 (エッジ検出用の配列の大きさ)

    bool Held(int vk, bool active) const;       // キーが押されている間 true
    bool Edge(int vk, bool active);             // 押された瞬間だけ true (1フレームにつき1回だけ呼ぶこと)
    bool Repeat(Dir d, bool held, float dt);    // 押した瞬間 + 押しっぱなしの連続入力

    int   slot_[SourceCount];                   // 各入力ソースのプレイヤー番号 (-1 = 未参加)
    bool  prev_[kKeyCount];                     // 前フレームのキー状態
    WORD  prevButtons_[cfg::kMaxPads];          // 前フレームのパッドのボタン状態
    float holdTime_[DirCount];                  // 方向キーを押し続けている時間 (-1 = 押していない)
};

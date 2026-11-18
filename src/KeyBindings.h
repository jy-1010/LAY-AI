#pragma once
#include <string>

// キー配置 (キーコンフィグ)。キーボード2人分と、パッドのボタンを変更できる。
//   固定のキー: Esc / P = ポーズ・戻る, R = ステージのやり直し, T = タイトルへ,
//               Backspace / Delete = キーボード1 / 2 の退出, メニューの上下左右決定
//               (これらは割り当てに使えない = IsReservedKey)

enum class KeyAction {
    Up, Down, Left, Right,      // 移動
    Action,                     // 採取 / 拾う / 預ける / 取り出す
    Place,                      // 線路・橋を置く / ダイナマイトを使う
    Drop,                       // 持っている物を地面に置く
    Count
};
constexpr int kKeyActionCount = (int)KeyAction::Count;
constexpr int kPadActionCount = 3;          // パッドで変更できるのは Action / Place / Drop (移動はスティック・十字キー固定)

// XInput の wButtons と同じ値 (xinput.h を読み込まなくて済むよう、ここで定義して Input.cpp で一致を確認する)
namespace padbtn {
constexpr unsigned kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;
constexpr unsigned kLeftShoulder = 0x0100, kRightShoulder = 0x0200;
constexpr unsigned kLeftStick = 0x0040, kRightStick = 0x0080;
}

struct KeyBindings {
    int      keyboard[2][kKeyActionCount];  // [0] = キーボード1, [1] = キーボード2 の仮想キーコード
    unsigned padButton[kPadActionCount];    // Action, Place, Drop に割り当てたパッドのボタン

    KeyBindings() { ResetToDefaults(); }
    void ResetToDefaults();
};

const char* KeyActionName(KeyAction a);     // 「MOVE UP」など
std::string KeyName(int vk);                // 「W」「SPACE」「RSHIFT」など (画面表示用。ASCII のみ)
std::string PadButtonName(unsigned mask);   // 「A」「LB」など
bool IsReservedKey(int vk);                 // 割り当てに使えないキー
bool IsCapturableKey(int vk);               // キー入力待ちで受け付けるキー
bool IsPadButtonAssignable(unsigned mask);  // パッドで割り当てに使えるボタン

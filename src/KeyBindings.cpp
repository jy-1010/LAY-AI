#include "KeyBindings.h"
#include <windows.h>
#include <cstdio>

namespace {

constexpr int kFirstCapturableKey = 8;       // VK_BACK 以降 (それより小さい値はマウスボタンなど)
constexpr int kLastKey            = 254;     // 仮想キーコードの最後
constexpr int kFunctionKeyCount   = 12;      // F1 .. F12
constexpr int kNumpadDigitCount   = 10;      // テンキーの 0 .. 9
constexpr size_t kNameBufferSize  = 16;      // 「KEY123」のような予備の名前用

struct NamedKey { int vk; const char* name; };

// 特別なキーの表示名 (ASCII のみ。文字・数字・ファンクションキー・テンキーは別に処理する)
const NamedKey kNamedKeys[] = {
    { VK_SPACE, "SPACE" },     { VK_RETURN, "ENTER" },     { VK_TAB, "TAB" },        { VK_BACK, "BACKSPACE" },
    { VK_ESCAPE, "ESC" },      { VK_CAPITAL, "CAPSLOCK" }, { VK_UP, "UP" },          { VK_DOWN, "DOWN" },
    { VK_LEFT, "LEFT" },       { VK_RIGHT, "RIGHT" },      { VK_INSERT, "INSERT" },  { VK_DELETE, "DELETE" },
    { VK_HOME, "HOME" },       { VK_END, "END" },          { VK_PRIOR, "PAGEUP" },   { VK_NEXT, "PAGEDOWN" },
    { VK_LSHIFT, "LSHIFT" },   { VK_RSHIFT, "RSHIFT" },    { VK_LCONTROL, "LCTRL" }, { VK_RCONTROL, "RCTRL" },
    { VK_LMENU, "LALT" },      { VK_RMENU, "RALT" },
    { VK_MULTIPLY, "NUMMUL" }, { VK_ADD, "NUMADD" },       { VK_SUBTRACT, "NUMSUB" },
    { VK_DECIMAL, "NUMDOT" },  { VK_DIVIDE, "NUMDIV" },
    { VK_OEM_1, "SEMICOLON" }, { VK_OEM_PLUS, "PLUS" },    { VK_OEM_COMMA, "COMMA" },
    { VK_OEM_MINUS, "MINUS" }, { VK_OEM_PERIOD, "PERIOD" },{ VK_OEM_2, "SLASH" },
    { VK_OEM_3, "BACKQUOTE" }, { VK_OEM_4, "LBRACKET" },   { VK_OEM_5, "BACKSLASH" },
    { VK_OEM_6, "RBRACKET" },  { VK_OEM_7, "QUOTE" },
};

struct NamedButton { unsigned mask; const char* name; };
const NamedButton kNamedButtons[] = {
    { padbtn::kA, "A" },  { padbtn::kB, "B" },  { padbtn::kX, "X" },  { padbtn::kY, "Y" },
    { padbtn::kLeftShoulder, "LB" },  { padbtn::kRightShoulder, "RB" },
    { padbtn::kLeftStick, "L3" },     { padbtn::kRightStick, "R3" },
};

} // namespace

void KeyBindings::ResetToDefaults()
{
    // キーボード1: WASD で移動 / E 採取 / Q 設置 / F 置く
    const int kb1[kKeyActionCount] = { 'W', 'S', 'A', 'D', 'E', 'Q', 'F' };
    // キーボード2: 矢印で移動 / Enter 採取 / 右Shift 設置 / 右Ctrl 置く
    const int kb2[kKeyActionCount] = { VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_RSHIFT, VK_RCONTROL };
    for (int i = 0; i < kKeyActionCount; ++i) { keyboard[0][i] = kb1[i]; keyboard[1][i] = kb2[i]; }
    // パッド: A 採取 / X 設置 / B 置く
    padButton[0] = padbtn::kA;
    padButton[1] = padbtn::kX;
    padButton[2] = padbtn::kB;
}

const char* KeyActionName(KeyAction a)
{
    switch (a) {
    case KeyAction::Up:     return "MOVE UP";
    case KeyAction::Down:   return "MOVE DOWN";
    case KeyAction::Left:   return "MOVE LEFT";
    case KeyAction::Right:  return "MOVE RIGHT";
    case KeyAction::Action: return "ACTION";
    case KeyAction::Place:  return "PLACE";
    case KeyAction::Drop:   return "DROP ITEM";
    default:                return "";
    }
}

std::string KeyName(int vk)
{
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, (char)vk);
    if (vk >= VK_F1 && vk < VK_F1 + kFunctionKeyCount) return "F" + std::to_string(vk - VK_F1 + 1);
    if (vk >= VK_NUMPAD0 && vk < VK_NUMPAD0 + kNumpadDigitCount) return "NUM" + std::to_string(vk - VK_NUMPAD0);
    for (const NamedKey& k : kNamedKeys) if (k.vk == vk) return k.name;
    char buf[kNameBufferSize];
    snprintf(buf, sizeof(buf), "KEY%d", vk);
    return buf;
}

std::string PadButtonName(unsigned mask)
{
    for (const NamedButton& b : kNamedButtons) if (b.mask == mask) return b.name;
    return "?";
}

bool IsReservedKey(int vk)
{
    return vk == VK_ESCAPE || vk == 'P' || vk == 'R' || vk == 'T' || vk == VK_BACK || vk == VK_DELETE;
}

bool IsCapturableKey(int vk)
{
    if (vk < kFirstCapturableKey || vk > kLastKey) return false;
    // 左右の区別が無い Shift / Ctrl / Alt (左右のキーが別に届く) と、Windows キー・アプリキーは除く
    return vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU && vk != VK_LWIN && vk != VK_RWIN && vk != VK_APPS;
}

bool IsPadButtonAssignable(unsigned mask)
{
    for (const NamedButton& b : kNamedButtons) if (b.mask == mask) return true;
    return false;
}

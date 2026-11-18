#include "Input.h"
#include <xinput.h>
#include <cmath>
#include <cstring>
#include <algorithm>

#pragma comment(lib, "xinput.lib")

// KeyBindings.h の padbtn が XInput の値と同じか確認する
static_assert(padbtn::kA == XINPUT_GAMEPAD_A && padbtn::kB == XINPUT_GAMEPAD_B &&
              padbtn::kX == XINPUT_GAMEPAD_X && padbtn::kY == XINPUT_GAMEPAD_Y &&
              padbtn::kLeftShoulder == XINPUT_GAMEPAD_LEFT_SHOULDER &&
              padbtn::kRightShoulder == XINPUT_GAMEPAD_RIGHT_SHOULDER &&
              padbtn::kLeftStick == XINPUT_GAMEPAD_LEFT_THUMB &&
              padbtn::kRightStick == XINPUT_GAMEPAD_RIGHT_THUMB, "padbtn の値が XInput と違う");

namespace {

constexpr int   kKeyDownMask   = 0x8000;     // GetAsyncKeyState の戻り値: 「今押されている」ビット
constexpr float kThumbStickMax = 32767.0f;   // XInput のスティックの最大値 (SHRT_MAX)
constexpr float kAxisMax       = 1.0f;       // 移動入力の最大値 (-1..1)
constexpr float kNotHeld       = -1.0f;      // holdTime_ の「押していない」を表す値
constexpr int   kPadAction = (int)KeyAction::Action - (int)KeyAction::Action;   // パッドの割り当て配列の添字: Action = 0
constexpr int   kPadPlace  = (int)KeyAction::Place  - (int)KeyAction::Action;   //                           Place  = 1
constexpr int   kPadDrop   = (int)KeyAction::Drop   - (int)KeyAction::Action;   //                           Drop   = 2

// 「負方向のキー」と「正方向のキー」から -1 / 0 / +1 の軸入力を作る
float Axis(bool neg, bool pos) { return (pos ? kAxisMax : 0.0f) - (neg ? kAxisMax : 0.0f); }
float ClampAxis(float v)       { return std::min(std::max(v, -kAxisMax), kAxisMax); }

} // namespace

InputManager::InputManager()
{
    for (int& s : slot_) s = -1;
    for (bool& b : prev_) b = false;
    for (bool& b : edgeValid_) b = false;
    for (bool& b : edgeValue_) b = false;
    for (WORD& w : prevButtons_) w = 0;
    for (float& t : holdTime_) t = kNotHeld;
}

void InputManager::BeginFrame()
{
    memset(edgeValid_, 0, sizeof(edgeValid_));
}

bool InputManager::Held(int vk, bool active) const
{
    return active && (GetAsyncKeyState(vk) & kKeyDownMask) != 0;
}

// 押された瞬間 (前フレームは押されていなかった) だけ true。
// 同じキーを複数の操作に割り当てていても、1フレームの中では同じ答えを返す
bool InputManager::Edge(int vk, bool active)
{
    if (vk < 0 || vk >= kKeyCount) return false;
    if (edgeValid_[vk]) return edgeValue_[vk];
    const bool down = Held(vk, active);
    edgeValue_[vk] = down && !prev_[vk];
    edgeValid_[vk] = true;
    prev_[vk] = down;
    return edgeValue_[vk];
}

// 方向入力: 押した瞬間に1回 → kMenuRepeatDelay 後から kMenuRepeatInterval ごとに連続入力
bool InputManager::Repeat(Dir d, bool held, float dt)
{
    if (!held) { holdTime_[d] = kNotHeld; return false; }
    if (holdTime_[d] < 0.0f) { holdTime_[d] = 0.0f; return true; }           // 押した瞬間
    holdTime_[d] += dt;
    if (holdTime_[d] >= cfg::input::kMenuRepeatDelay) {
        holdTime_[d] = cfg::input::kMenuRepeatDelay - cfg::input::kMenuRepeatInterval;   // 次の連続入力まで interval 待つ
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// ゲーム中の入力
// ---------------------------------------------------------------------------
void InputManager::Update(HWND hwnd, Game& game, const KeyBindings& kb, Input out[Game::MaxPlayers], bool& pauseRequested)
{
    BeginFrame();
    for (int i = 0; i < Game::MaxPlayers; ++i) out[i] = Input();
    pauseRequested = false;
    const bool active = (GetForegroundWindow() == hwnd);     // ウィンドウが前面の時だけ入力を受け付ける

    const int* k1 = kb.keyboard[0];                          // キーボード1 / 2 のキー配置
    const int* k2 = kb.keyboard[1];
    const int kAct = (int)KeyAction::Action, kPlc = (int)KeyAction::Place, kDrp = (int)KeyAction::Drop;
    const int kUp = (int)KeyAction::Up, kDown = (int)KeyAction::Down, kLeft = (int)KeyAction::Left, kRight = (int)KeyAction::Right;

    // Edge はここでまとめて取る
    const bool act1 = Edge(k1[kAct], active), place1 = Edge(k1[kPlc], active), drop1 = Edge(k1[kDrp], active);
    const bool act2 = Edge(k2[kAct], active), place2 = Edge(k2[kPlc], active), drop2 = Edge(k2[kDrp], active);
    const bool rr = Edge('R', active);
    const bool bs = Edge(VK_BACK, active), del = Edge(VK_DELETE, active);      // キーボード1 / 2 の退出
    const bool esc = Edge(VK_ESCAPE, active), pk = Edge('P', active);
    if (esc || pk) pauseRequested = true;

    // ---------- キーボード1 ----------
    if (slot_[KB1] < 0) {
        if (act1 || place1) slot_[KB1] = game.AddPlayer();    // 参加した押下は操作に使わない
    } else if (bs && game.PlayerCount() > 1) {                // 最後の1人は退出できない
        game.RemovePlayer(slot_[KB1]);
        slot_[KB1] = -1;
    } else {
        Input in;
        in.moveX = Axis(Held(k1[kLeft], active), Held(k1[kRight], active));
        in.moveZ = Axis(Held(k1[kDown], active), Held(k1[kUp], active));
        if (slot_[KB2] < 0) {            // キーボード2が居ない間は、キーボード2の移動キーでも動かせる
            in.moveX += Axis(Held(k2[kLeft], active), Held(k2[kRight], active));
            in.moveZ += Axis(Held(k2[kDown], active), Held(k2[kUp], active));
        }
        in.moveX = ClampAxis(in.moveX); in.moveZ = ClampAxis(in.moveZ);
        in.harvest = act1;
        in.place   = place1;
        in.drop    = drop1;
        in.restart = rr;
        out[slot_[KB1]] = in;
    }

    // ---------- キーボード2 ----------
    if (slot_[KB2] < 0) {
        if (act2 || place2) slot_[KB2] = game.AddPlayer();    // 参加した押下は操作に使わない
    } else if (del && game.PlayerCount() > 1) {
        game.RemovePlayer(slot_[KB2]);
        slot_[KB2] = -1;
    } else {
        Input in;
        in.moveX = Axis(Held(k2[kLeft], active), Held(k2[kRight], active));
        in.moveZ = Axis(Held(k2[kDown], active), Held(k2[kUp], active));
        in.harvest = act2;
        in.place   = place2;
        in.drop    = drop2;
        out[slot_[KB2]] = in;
    }

    // ---------- XInput パッド ----------
    for (int p = 0; p < cfg::kMaxPads; ++p) {
        const int s = PAD0 + p;
        XINPUT_STATE st = {};
        const bool connected = (XInputGetState((DWORD)p, &st) == ERROR_SUCCESS);

        if (!connected) {                                      // 切断 → 自動退出 (最後の1人でも)
            if (slot_[s] >= 0) { game.RemovePlayer(slot_[s]); slot_[s] = -1; }
            prevButtons_[p] = 0;
            continue;
        }

        const WORD b = st.Gamepad.wButtons;
        const WORD pressed = (WORD)(b & ~prevButtons_[p]);     // このフレームに新しく押されたボタン
        prevButtons_[p] = b;
        if (!active) continue;

        if (slot_[s] < 0) {                                    // 参加待ち
            if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) slot_[s] = game.AddPlayer();
            continue;
        }
        if ((pressed & XINPUT_GAMEPAD_BACK) && game.PlayerCount() > 1) {   // 退出 (最後の1人は不可)
            game.RemovePlayer(slot_[s]);
            slot_[s] = -1;
            continue;
        }
        if (pressed & XINPUT_GAMEPAD_START) pauseRequested = true;

        Input in;
        // 左スティック: 円形のデッドゾーンを除き、残りを 0..1 に再スケールする
        const float lx = (float)st.Gamepad.sThumbLX, ly = (float)st.Gamepad.sThumbLY;
        const float mag = sqrtf(lx * lx + ly * ly);
        const float deadZone = (float)XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
        if (mag > deadZone) {
            const float strength = (std::min(mag, kThumbStickMax) - deadZone) / (kThumbStickMax - deadZone);
            in.moveX = lx / mag * strength;
            in.moveZ = ly / mag * strength;
        }
        // 十字キー
        in.moveX += Axis((b & XINPUT_GAMEPAD_DPAD_LEFT) != 0, (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
        in.moveZ += Axis((b & XINPUT_GAMEPAD_DPAD_DOWN) != 0, (b & XINPUT_GAMEPAD_DPAD_UP) != 0);
        in.moveX = ClampAxis(in.moveX); in.moveZ = ClampAxis(in.moveZ);

        in.harvest = (pressed & kb.padButton[kPadAction]) != 0;
        in.place   = (pressed & kb.padButton[kPadPlace])  != 0;
        in.drop    = (pressed & kb.padButton[kPadDrop])   != 0;
        out[slot_[s]] = in;
    }
}

// ---------------------------------------------------------------------------
// メニューの入力 (タイトル / ポーズ / 設定 / 結果画面 / ショップ)
// ---------------------------------------------------------------------------
MenuInput InputManager::PollMenu(HWND hwnd, float dt)
{
    BeginFrame();
    MenuInput mi;
    const bool active = (GetForegroundWindow() == hwnd);

    const bool enter = Edge(VK_RETURN, active), space = Edge(VK_SPACE, active);
    const bool r = Edge('R', active), t = Edge('T', active);
    const bool esc = Edge(VK_ESCAPE, active), pk = Edge('P', active);
    if (enter || space) { mi.confirm = true; mi.keyboard = true; }
    if (r)              mi.retry = true;
    if (t)              mi.back  = true;
    if (esc)            { mi.cancel = true; mi.pause = true; }
    if (pk)             mi.pause = true;

    // 方向: キーボード (WASD / 矢印。メニューでは配置に関係なく固定) + パッド (十字キー / 左スティック)
    bool held[DirCount] = {
        Held('W', active) || Held(VK_UP, active),
        Held('S', active) || Held(VK_DOWN, active),
        Held('A', active) || Held(VK_LEFT, active),
        Held('D', active) || Held(VK_RIGHT, active),
    };

    for (int p = 0; p < cfg::kMaxPads; ++p) {
        XINPUT_STATE st = {};
        if (XInputGetState((DWORD)p, &st) != ERROR_SUCCESS) { prevButtons_[p] = 0; continue; }
        const WORD b = st.Gamepad.wButtons;
        const WORD pressed = (WORD)(b & ~prevButtons_[p]);
        prevButtons_[p] = b;
        if (!active) continue;

        if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) { mi.confirm = true; mi.padMask |= (1u << p); }
        if (pressed & XINPUT_GAMEPAD_START) mi.pause  = true;
        if (pressed & XINPUT_GAMEPAD_B)     mi.cancel = true;
        if (pressed & XINPUT_GAMEPAD_Y)     mi.retry  = true;
        if (pressed & XINPUT_GAMEPAD_BACK)  mi.back   = true;

        const float sx = (float)st.Gamepad.sThumbLX / kThumbStickMax;      // スティックを -1..1 に
        const float sy = (float)st.Gamepad.sThumbLY / kThumbStickMax;
        held[DirUp]    |= (b & XINPUT_GAMEPAD_DPAD_UP)    != 0 || sy >  cfg::input::kMenuStickThreshold;
        held[DirDown]  |= (b & XINPUT_GAMEPAD_DPAD_DOWN)  != 0 || sy < -cfg::input::kMenuStickThreshold;
        held[DirLeft]  |= (b & XINPUT_GAMEPAD_DPAD_LEFT)  != 0 || sx < -cfg::input::kMenuStickThreshold;
        held[DirRight] |= (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0 || sx >  cfg::input::kMenuStickThreshold;
    }

    mi.up    = Repeat(DirUp,    held[DirUp],    dt);
    mi.down  = Repeat(DirDown,  held[DirDown],  dt);
    mi.left  = Repeat(DirLeft,  held[DirLeft],  dt);
    mi.right = Repeat(DirRight, held[DirRight], dt);
    return mi;
}

// ---------------------------------------------------------------------------
// キーコンフィグ: 次に押されたキー / ボタンを受け取る
// ---------------------------------------------------------------------------
bool InputManager::CaptureKey(HWND hwnd, int& vk)
{
    BeginFrame();
    const bool active = (GetForegroundWindow() == hwnd);
    bool found = false;
    for (int k = 0; k < kKeyCount; ++k) {
        if (k != VK_ESCAPE && !IsCapturableKey(k)) continue;
        if (Edge(k, active) && !found) { vk = k; found = true; }      // 全キーの状態を更新しつつ、最初に押されたものを採る
    }
    return found;
}

bool InputManager::CapturePadButton(unsigned& mask)
{
    bool found = false;
    for (int p = 0; p < cfg::kMaxPads; ++p) {
        XINPUT_STATE st = {};
        if (XInputGetState((DWORD)p, &st) != ERROR_SUCCESS) { prevButtons_[p] = 0; continue; }
        const WORD b = st.Gamepad.wButtons;
        const WORD pressed = (WORD)(b & ~prevButtons_[p]);
        prevButtons_[p] = b;
        for (unsigned bit = 1; bit != 0 && bit <= 0x8000u; bit <<= 1)
            if ((pressed & bit) && IsPadButtonAssignable(bit) && !found) { mask = bit; found = true; }
    }
    return found;
}

void InputManager::ClearPlayers(Game& game)
{
    for (int& s : slot_) {
        if (s >= 0) game.RemovePlayer(s);
        s = -1;
    }
}

// タイトルで「決定」を押した入力が 1P になる (キーボードなら キーボード1、パッドならそのパッド)
void InputManager::JoinFromMenu(Game& game, const MenuInput& mi)
{
    bool joined = false;
    if (mi.keyboard && slot_[KB1] < 0) { slot_[KB1] = game.AddPlayer(); joined = true; }
    for (int p = 0; p < cfg::kMaxPads; ++p) {
        if ((mi.padMask & (1u << p)) && slot_[PAD0 + p] < 0) { slot_[PAD0 + p] = game.AddPlayer(); joined = true; }
    }
    if (!joined && slot_[KB1] < 0) slot_[KB1] = game.AddPlayer();    // 念のため (誰も特定できない時はキーボード1)
}

#include "Input.h"
#include <xinput.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "xinput.lib")

namespace {

constexpr int   kKeyDownMask   = 0x8000;     // GetAsyncKeyState の戻り値: 「今押されている」ビット
constexpr float kThumbStickMax = 32767.0f;   // XInput のスティックの最大値 (SHRT_MAX)
constexpr float kAxisMax       = 1.0f;       // 移動入力の最大値 (-1..1)
constexpr float kNotHeld       = -1.0f;      // holdTime_ の「押していない」を表す値

// 「負方向のキー」と「正方向のキー」から -1 / 0 / +1 の軸入力を作る
float Axis(bool neg, bool pos) { return (pos ? kAxisMax : 0.0f) - (neg ? kAxisMax : 0.0f); }
float ClampAxis(float v)       { return std::min(std::max(v, -kAxisMax), kAxisMax); }

} // namespace

InputManager::InputManager()
{
    for (int& s : slot_) s = -1;
    for (bool& b : prev_) b = false;
    for (WORD& w : prevButtons_) w = 0;
    for (float& t : holdTime_) t = kNotHeld;
}

bool InputManager::Held(int vk, bool active) const
{
    return active && (GetAsyncKeyState(vk) & kKeyDownMask) != 0;
}

bool InputManager::Edge(int vk, bool active)
{
    bool down = Held(vk, active);
    bool pressedNow = down && !prev_[vk];       // 前フレームは押されていなかった
    prev_[vk] = down;
    return pressedNow;
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
void InputManager::Update(HWND hwnd, Game& game, Input out[Game::MaxPlayers], bool& pauseRequested)
{
    for (int i = 0; i < Game::MaxPlayers; ++i) out[i] = Input();
    pauseRequested = false;
    const bool active = (GetForegroundWindow() == hwnd);     // ウィンドウが前面の時だけ入力を受け付ける

    // Edge は1フレームに1回だけ評価する (最初にまとめて取る)
    bool e1 = Edge('E', active), sp = Edge(VK_SPACE, active), q = Edge('Q', active), rr = Edge('R', active);
    bool bs = Edge(VK_BACK, active);                                                      // キーボード1の退出
    bool dropKb1 = Edge('F', active), dropKb2 = Edge(VK_RCONTROL, active);              // 持ち物を地面に置く
    bool enter = Edge(VK_RETURN, active), rsh = Edge(VK_RSHIFT, active), del = Edge(VK_DELETE, active);
    bool esc = Edge(VK_ESCAPE, active), pk = Edge('P', active);
    if (esc || pk) pauseRequested = true;

    // ---------- キーボード1 ----------
    if (slot_[KB1] < 0) {
        if (e1 || sp) slot_[KB1] = game.AddPlayer();          // 参加した押下は採取に使わない
    } else if (bs && game.PlayerCount() > 1) {                // 最後の1人は退出できない
        game.RemovePlayer(slot_[KB1]);
        slot_[KB1] = -1;
    } else {
        Input in;
        in.moveX = Axis(Held('A', active), Held('D', active));
        in.moveZ = Axis(Held('S', active), Held('W', active));
        if (slot_[KB2] < 0) {            // キーボード2が居ない間は矢印キーも使える
            in.moveX += Axis(Held(VK_LEFT, active), Held(VK_RIGHT, active));
            in.moveZ += Axis(Held(VK_DOWN, active), Held(VK_UP, active));
        }
        in.moveX = ClampAxis(in.moveX); in.moveZ = ClampAxis(in.moveZ);
        in.harvest = e1 || sp;
        in.place   = q;
        in.drop    = dropKb1;
        in.restart = rr;
        out[slot_[KB1]] = in;
    }

    // ---------- キーボード2 ----------
    if (slot_[KB2] < 0) {
        if (enter || rsh) slot_[KB2] = game.AddPlayer();      // 参加した押下は採取/設置に使わない
    } else if (del && game.PlayerCount() > 1) {
        game.RemovePlayer(slot_[KB2]);
        slot_[KB2] = -1;
    } else {
        Input in;
        in.moveX = Axis(Held(VK_LEFT, active), Held(VK_RIGHT, active));
        in.moveZ = Axis(Held(VK_DOWN, active), Held(VK_UP, active));
        in.harvest = enter;
        in.place   = rsh;
        in.drop    = dropKb2;
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

        in.harvest = (pressed & XINPUT_GAMEPAD_A) != 0;
        in.place   = (pressed & XINPUT_GAMEPAD_X) != 0;
        in.drop    = (pressed & XINPUT_GAMEPAD_B) != 0;
        out[slot_[s]] = in;
    }
}

// ---------------------------------------------------------------------------
// メニューの入力 (タイトル / ポーズ / 設定 / 結果画面)
// ---------------------------------------------------------------------------
MenuInput InputManager::PollMenu(HWND hwnd, float dt)
{
    MenuInput mi;
    const bool active = (GetForegroundWindow() == hwnd);

    bool enter = Edge(VK_RETURN, active), space = Edge(VK_SPACE, active);
    bool r = Edge('R', active), t = Edge('T', active);
    bool esc = Edge(VK_ESCAPE, active), pk = Edge('P', active);
    if (enter || space) { mi.confirm = true; mi.keyboard = true; }
    if (r)              mi.retry = true;
    if (t)              mi.back  = true;
    if (esc)            { mi.cancel = true; mi.pause = true; }
    if (pk)             mi.pause = true;

    // 方向: キーボード (WASD / 矢印) + パッド (十字キー / 左スティック)
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

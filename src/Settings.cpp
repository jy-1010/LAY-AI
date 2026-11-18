#include "Settings.h"
#include <windows.h>
#include <algorithm>
#include <cwchar>

namespace {

// ini のセクション名 / キー名
const wchar_t* const kSectionDisplay = L"Display";
const wchar_t* const kSectionAudio   = L"Audio";
const wchar_t* const kKeyFullscreen  = L"Fullscreen";
const wchar_t* const kKeyVsync       = L"VSync";
const wchar_t* const kKeyShowFps     = L"ShowFps";
const wchar_t* const kKeyMaster      = L"MasterVolume";
const wchar_t* const kKeyMusic       = L"MusicVolume";
const wchar_t* const kKeySfx         = L"SfxVolume";

constexpr size_t kIntBufferChars = 16;      // 整数を文字列にする作業用バッファ

int  ReadInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int fallback)
{
    return (int)GetPrivateProfileIntW(section, key, fallback, path.c_str());
}

void WriteInt(const std::wstring& path, const wchar_t* section, const wchar_t* key, int value)
{
    wchar_t buf[kIntBufferChars];
    swprintf_s(buf, L"%d", value);
    WritePrivateProfileStringW(section, key, buf, path.c_str());
}

int ClampVolume(int v) { return std::min(std::max(v, 0), cfg::audio::kVolumeSteps); }

// キー配置の ini 上の名前
const wchar_t* const kSectionKeyboard[2] = { L"Keyboard1", L"Keyboard2" };
const wchar_t* const kSectionGamepad     = L"Gamepad";
const wchar_t* const kActionKeys[kKeyActionCount] = { L"Up", L"Down", L"Left", L"Right", L"Action", L"Place", L"Drop" };
constexpr int kPadFirstAction = (int)KeyAction::Action;      // パッドで変更できる操作の先頭 (Action, Place, Drop)

} // namespace

void Settings::ResetToDefaults()
{
    const KeyBindings keepKeys = keys;      // キー配置はそのまま残す
    *this = Settings();
    keys = keepKeys;
}

void Settings::Load(const std::wstring& path)
{
    fullscreen   = ReadInt(path, kSectionDisplay, kKeyFullscreen, fullscreen ? 1 : 0) != 0;
    vsync        = ReadInt(path, kSectionDisplay, kKeyVsync,      vsync ? 1 : 0)      != 0;
    showFps      = ReadInt(path, kSectionDisplay, kKeyShowFps,    showFps ? 1 : 0)    != 0;
    masterVolume = ClampVolume(ReadInt(path, kSectionAudio, kKeyMaster, masterVolume));
    musicVolume  = ClampVolume(ReadInt(path, kSectionAudio, kKeyMusic,  musicVolume));
    sfxVolume    = ClampVolume(ReadInt(path, kSectionAudio, kKeySfx,    sfxVolume));

    // キー配置: 不正な値 (使えないキー・ボタン) は既定値のままにする
    for (int d = 0; d < 2; ++d)
        for (int a = 0; a < kKeyActionCount; ++a) {
            const int vk = ReadInt(path, kSectionKeyboard[d], kActionKeys[a], keys.keyboard[d][a]);
            if (IsCapturableKey(vk) && !IsReservedKey(vk)) keys.keyboard[d][a] = vk;
        }
    for (int a = 0; a < kPadActionCount; ++a) {
        const unsigned mask = (unsigned)ReadInt(path, kSectionGamepad, kActionKeys[kPadFirstAction + a], (int)keys.padButton[a]);
        if (IsPadButtonAssignable(mask)) keys.padButton[a] = mask;
    }
}

void Settings::Save(const std::wstring& path) const
{
    WriteInt(path, kSectionDisplay, kKeyFullscreen, fullscreen ? 1 : 0);
    WriteInt(path, kSectionDisplay, kKeyVsync,      vsync ? 1 : 0);
    WriteInt(path, kSectionDisplay, kKeyShowFps,    showFps ? 1 : 0);
    WriteInt(path, kSectionAudio, kKeyMaster, masterVolume);
    WriteInt(path, kSectionAudio, kKeyMusic,  musicVolume);
    WriteInt(path, kSectionAudio, kKeySfx,    sfxVolume);
    for (int d = 0; d < 2; ++d)
        for (int a = 0; a < kKeyActionCount; ++a) WriteInt(path, kSectionKeyboard[d], kActionKeys[a], keys.keyboard[d][a]);
    for (int a = 0; a < kPadActionCount; ++a)
        WriteInt(path, kSectionGamepad, kActionKeys[kPadFirstAction + a], (int)keys.padButton[a]);
}

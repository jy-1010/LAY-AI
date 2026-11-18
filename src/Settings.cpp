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

} // namespace

void Settings::ResetToDefaults()
{
    *this = Settings();
}

void Settings::Load(const std::wstring& path)
{
    fullscreen   = ReadInt(path, kSectionDisplay, kKeyFullscreen, fullscreen ? 1 : 0) != 0;
    vsync        = ReadInt(path, kSectionDisplay, kKeyVsync,      vsync ? 1 : 0)      != 0;
    showFps      = ReadInt(path, kSectionDisplay, kKeyShowFps,    showFps ? 1 : 0)    != 0;
    masterVolume = ClampVolume(ReadInt(path, kSectionAudio, kKeyMaster, masterVolume));
    musicVolume  = ClampVolume(ReadInt(path, kSectionAudio, kKeyMusic,  musicVolume));
    sfxVolume    = ClampVolume(ReadInt(path, kSectionAudio, kKeySfx,    sfxVolume));
}

void Settings::Save(const std::wstring& path) const
{
    WriteInt(path, kSectionDisplay, kKeyFullscreen, fullscreen ? 1 : 0);
    WriteInt(path, kSectionDisplay, kKeyVsync,      vsync ? 1 : 0);
    WriteInt(path, kSectionDisplay, kKeyShowFps,    showFps ? 1 : 0);
    WriteInt(path, kSectionAudio, kKeyMaster, masterVolume);
    WriteInt(path, kSectionAudio, kKeyMusic,  musicVolume);
    WriteInt(path, kSectionAudio, kKeySfx,    sfxVolume);
}

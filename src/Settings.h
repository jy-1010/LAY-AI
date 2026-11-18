#pragma once
#include <string>
#include "Config.h"
#include "KeyBindings.h"

// ユーザー設定。settings.ini (exe と同じフォルダ) に保存し、次回起動時に読み込む。
//   ・画面     : フルスクリーン / 垂直同期 / FPS表示
//   ・サウンド : 全体 / BGM / 効果音 の音量 (0 .. cfg::audio::kVolumeSteps)
struct Settings {
    bool fullscreen = cfg::settings::kDefaultFullscreen;   // フルスクリーン (枠なしウィンドウで画面いっぱい)
    bool vsync      = cfg::settings::kDefaultVsync;        // 垂直同期 (ONでティアリング防止)
    bool showFps    = cfg::settings::kDefaultShowFps;      // 画面右上にFPSを表示
    int  masterVolume = cfg::settings::kDefaultMasterVolume;
    int  musicVolume  = cfg::settings::kDefaultMusicVolume;
    int  sfxVolume    = cfg::settings::kDefaultSfxVolume;
    KeyBindings keys;                              // キー配置 (キーコンフィグ)

    void ResetToDefaults();                       // 画面・サウンドの設定を初期値に戻す (キー配置はキーコンフィグ画面で戻す)
    void Load(const std::wstring& path);          // ファイルが無い/項目が無ければ既定値のまま
    void Save(const std::wstring& path) const;
};

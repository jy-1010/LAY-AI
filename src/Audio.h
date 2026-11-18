#pragma once
#include <cstdint>
#include <vector>

// XAudio2 (DirectX のサウンドAPI) だけで鳴らす効果音とBGM。外部のサウンドファイルは使わず、
// 起動時に波形をプログラムで合成する。オーディオデバイスが無い環境では何もせず無音で動く。
struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

enum class Sfx {        // 効果音
    Select,             // メニューのカーソル移動
    Confirm,            // 決定
    Back,               // 戻る
    Harvest,            // 木・岩を採取
    Place,              // 線路を置く
    Bridge,             // 橋を架ける
    Bolt,               // ボルトを拾う
    Pushed,             // 列車に押された
    Respawn,            // リスポーン
    Rush,               // ゴールラッシュ開始
    Pickup,             // 貨車から線路を取り出した
    Deposit,            // 貨物車両に預けた
    Craft,              // レール作成車両が線路を1本作った
    Deny,               // できない操作 (手がふさがっている・材料が無い など)
    Drop,               // 持っている物を地面に置いた
    Smash,              // 隣の木・岩が壊れた
    Buy,                // ショップで強化を購入した
    Clear,              // ステージクリア
    GameOver,           // ゲームオーバー
    Count
};

enum class Bgm { None, Menu, Play };

class Audio {
public:
    ~Audio() { Shutdown(); }

    bool Init();                                   // 失敗しても動き続ける (無音)。成功したら true
    void Shutdown();

    void PlaySfx(Sfx sfx);
    void PlayBgm(Bgm bgm);                         // 同じ曲なら何もしない。Bgm::None で停止
    // 音量 (0 .. cfg::audio::kVolumeSteps): 全体 / BGM / 効果音
    void SetVolumes(int master, int music, int sfx);

private:
    float StepToGain(int step) const;

    IXAudio2*               xa_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SourceVoice*    bgmVoice_ = nullptr;
    std::vector<IXAudio2SourceVoice*> sfxVoices_;
    int  nextVoice_ = 0;                           // 次に使う効果音ボイス (空きが無ければ古い順に奪う)

    std::vector<int16_t> sfxBuffers_[(int)Sfx::Count];
    std::vector<int16_t> bgmMenu_, bgmPlay_;       // 再生中は XAudio2 が参照するので、保持しておく

    Bgm  currentBgm_ = Bgm::None;
    int  masterStep_ = 0, musicStep_ = 0, sfxStep_ = 0;
    bool ready_ = false;
    bool comInitialized_ = false;
};

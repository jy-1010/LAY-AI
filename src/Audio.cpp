#include "Audio.h"
#include "Config.h"
#include <xaudio2.h>
#include <objbase.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "ole32.lib")

namespace au = cfg::audio;

namespace {

// ---------------------------------------------------------------------------
// 波形の合成
// ---------------------------------------------------------------------------
enum class Wave { Sine, Square, Pulse, Triangle, Noise };

constexpr WORD kChannels      = 1;                   // モノラル
constexpr WORD kBitsPerSample = 16;                  // 16bit PCM
constexpr float kStepsPerBeat = 2.0f;                // BGM: 1拍あたりのメロディの区切り (8分音符 = 2)

// ノイズ用の乱数 (xorshift32) の定数
constexpr int      kShiftA = 13, kShiftB = 17, kShiftC = 5;
constexpr int      kNoiseDrop = 8;                   // 下位ビットは偏りやすいので捨てる
constexpr uint32_t kNoiseMask = 0xFFFF;              // 使う16bit
constexpr float    kNoiseHalfRange = 32767.5f;       // 0..65535 を -1..1 に直す

float MidiToHz(int note)
{
    return au::kFreqA4 * powf(2.0f, (float)(note - au::kMidiA4) / (float)au::kSemitonesPerOctave);
}

// 音を float のバッファに足し込んでいく合成器
class Synth {
public:
    explicit Synth(size_t samples) : buf_(samples, 0.0f), noise_(au::kNoiseSeed) {}

    // startSec から seconds 秒間、hz → hzEnd へ周波数を変えながら鳴らす
    //   amp = 大きさ, decay = 減衰の速さ (大きいほど早く消える)
    void AddNote(float startSec, float hz, float hzEnd, float seconds, Wave wave, float amp, float decay)
    {
        const float rate = (float)au::kSampleRate;
        const size_t first = (size_t)(startSec * rate);
        const size_t count = (size_t)(seconds * rate);
        float phase = 0.0f;                                         // 波形の位相 0..1
        for (size_t i = 0; i < count && first + i < buf_.size(); ++i) {
            const float t = (float)i / rate;                        // 音の開始からの経過時間
            const float freq = hz + (hzEnd - hz) * (t / seconds);   // 周波数を直線的に変化させる
            phase += freq / rate;
            phase -= floorf(phase);
            float env = expf(-decay * t);                           // 指数的な減衰
            env *= std::min(1.0f, t / au::kAttackSeconds);          // 立ち上がり (プチノイズ防止)
            env *= std::min(1.0f, (seconds - t) / au::kReleaseSeconds);   // 終わりの減衰
            buf_[first + i] += Sample(wave, phase) * amp * env;
        }
    }

    // -1..1 に収めて 16bit 整数に変換
    std::vector<int16_t> ToPcm() const
    {
        std::vector<int16_t> pcm(buf_.size());
        for (size_t i = 0; i < buf_.size(); ++i)
            pcm[i] = (int16_t)(std::min(std::max(buf_[i], -1.0f), 1.0f) * au::kSampleToInt);
        return pcm;
    }

private:
    float Sample(Wave wave, float phase)
    {
        switch (wave) {
        case Wave::Sine:     return sinf(phase * cfg::kTwoPi);
        case Wave::Square:   return phase < cfg::kHalf ? 1.0f : -1.0f;
        case Wave::Pulse:    return phase < au::kPulseDuty ? 1.0f : -1.0f;
        case Wave::Triangle: return (phase < cfg::kHalf ? phase : 1.0f - phase) * au::kTriangleGain - 1.0f;
        case Wave::Noise:
            noise_ ^= noise_ << kShiftA; noise_ ^= noise_ >> kShiftB; noise_ ^= noise_ << kShiftC;
            return (float)((noise_ >> kNoiseDrop) & kNoiseMask) / kNoiseHalfRange - 1.0f;
        }
        return 0.0f;
    }

    std::vector<float> buf_;
    uint32_t noise_;
};

// ---------------------------------------------------------------------------
// 効果音のデータ (1つの効果音 = 数個の音の並び)
// ---------------------------------------------------------------------------
struct SfxNote {
    float start;      // 開始時刻 (秒)
    float hz;         // 開始周波数 (Hz)。ノイズでは使わない
    float hzEnd;      // 終了周波数 (Hz)。hz との間を直線的に変化する (同じ値なら一定)
    float seconds;    // 長さ (秒)
    Wave  wave;
    float amp;        // 大きさの倍率
    float decay;      // 減衰の速さ
};

//                              start   hz    hzEnd  sec    wave           amp   decay
const SfxNote kSelect[]   = { { 0.00f,  880,   880, 0.05f, Wave::Square,   0.5f, 25 } };
const SfxNote kConfirm[]  = { { 0.00f,  660,   660, 0.07f, Wave::Square,   0.6f, 15 },
                              { 0.07f,  990,   990, 0.10f, Wave::Square,   0.6f, 12 } };
const SfxNote kBack[]     = { { 0.00f,  500,   500, 0.06f, Wave::Square,   0.5f, 15 },
                              { 0.06f,  330,   330, 0.09f, Wave::Square,   0.5f, 12 } };
const SfxNote kHarvest[]  = { { 0.00f,    0,     0, 0.10f, Wave::Noise,    0.6f, 30 },
                              { 0.00f,  180,   120, 0.12f, Wave::Triangle, 0.8f, 15 } };
const SfxNote kPlace[]    = { { 0.00f,  520,   520, 0.06f, Wave::Triangle, 0.8f, 18 },
                              { 0.05f,  780,   780, 0.09f, Wave::Triangle, 0.8f, 14 } };
const SfxNote kBridge[]   = { { 0.00f,  200,   140, 0.14f, Wave::Triangle, 1.0f, 10 },
                              { 0.00f,    0,     0, 0.08f, Wave::Noise,    0.4f, 25 },
                              { 0.12f,  300,   300, 0.10f, Wave::Square,   0.4f, 15 } };
const SfxNote kBolt[]     = { { 0.00f, 1320,  1320, 0.10f, Wave::Sine,     0.8f,  8 },
                              { 0.08f, 1760,  1760, 0.22f, Wave::Sine,     0.8f,  7 } };
const SfxNote kPushed[]   = { { 0.00f,  140,    90, 0.12f, Wave::Square,   0.6f, 12 } };
const SfxNote kRespawn[]  = { { 0.00f,  300,   900, 0.25f, Wave::Triangle, 0.8f,  3 } };
const SfxNote kRush[]     = { { 0.00f,  800,   800, 0.12f, Wave::Square,   0.5f,  6 },
                              { 0.15f,  600,   600, 0.12f, Wave::Square,   0.5f,  6 },
                              { 0.30f,  800,   800, 0.12f, Wave::Square,   0.5f,  6 },
                              { 0.45f,  600,   600, 0.12f, Wave::Square,   0.5f,  6 } };
const SfxNote kPickup[]   = { { 0.00f,  700,   900, 0.08f, Wave::Triangle, 0.8f, 12 } };
const SfxNote kDeposit[]  = { { 0.00f,  600,   600, 0.06f, Wave::Triangle, 0.8f, 14 },
                              { 0.06f,  450,   450, 0.09f, Wave::Triangle, 0.8f, 12 } };
const SfxNote kCraft[]    = { { 0.00f, 1000,  1000, 0.05f, Wave::Square,   0.4f, 25 },
                              { 0.06f, 1500,  1500, 0.09f, Wave::Square,   0.4f, 18 } };
const SfxNote kDeny[]     = { { 0.00f,  160,   120, 0.15f, Wave::Square,   0.6f, 10 } };
const SfxNote kDrop[]     = { { 0.00f,  300,   200, 0.08f, Wave::Triangle, 0.9f, 15 } };
const SfxNote kSmash[]    = { { 0.00f,    0,     0, 0.12f, Wave::Noise,    0.7f, 20 },
                              { 0.00f,  220,   110, 0.12f, Wave::Square,   0.5f, 14 } };
const SfxNote kBuy[]      = { { 0.00f,  880,   880, 0.07f, Wave::Square,   0.5f, 12 },
                              { 0.07f, 1175,  1175, 0.07f, Wave::Square,   0.5f, 12 },
                              { 0.14f, 1568,  1568, 0.18f, Wave::Square,   0.5f,  8 } };
const SfxNote kClear[]    = { { 0.00f,  523,   523, 0.14f, Wave::Square,   0.6f,  6 },     // ド
                              { 0.12f,  659,   659, 0.14f, Wave::Square,   0.6f,  6 },     // ミ
                              { 0.24f,  784,   784, 0.14f, Wave::Square,   0.6f,  6 },     // ソ
                              { 0.36f, 1046,  1046, 0.45f, Wave::Square,   0.6f,  3 } };   // 高いド
const SfxNote kGameOver[] = { { 0.00f,  392,   392, 0.35f, Wave::Triangle, 0.9f,  3 },     // ソ
                              { 0.20f,  330,   330, 0.35f, Wave::Triangle, 0.9f,  3 },     // ミ
                              { 0.40f,  262,   262, 0.35f, Wave::Triangle, 0.9f,  3 },     // ド
                              { 0.60f,  196,   196, 0.60f, Wave::Triangle, 0.9f,  2 } };   // 低いソ

template <size_t N>
std::vector<int16_t> BuildSfx(const SfxNote (&notes)[N])
{
    float end = 0.0f;
    for (const SfxNote& n : notes) end = std::max(end, n.start + n.seconds);
    Synth synth((size_t)((end + au::kTailSeconds) * (float)au::kSampleRate));
    for (const SfxNote& n : notes)
        synth.AddNote(n.start, n.hz, n.hzEnd, n.seconds, n.wave, n.amp * au::kSfxAmplitude, n.decay);
    return synth.ToPcm();
}

// ---------------------------------------------------------------------------
// BGM のデータ (MIDIノート番号。0 = 休符)
//   melody = 8分音符ごと / bass = 4分音符ごと (melody の半分の数)
// ---------------------------------------------------------------------------
// タイトル/設定: ゆったりした ハ長調
const uint8_t kMenuMelody[] = { 72,0,76,0,79,0,76,0,   77,0,81,0,84,0,81,0,
                                79,0,83,0,79,0,76,0,   74,0,77,0,72,0, 0,0 };
const uint8_t kMenuBass[]   = { 48,55,48,55,  53,60,53,60,  55,62,55,62,  50,57,48,0 };
// ゲーム中: ノリのいい イ短調
const uint8_t kPlayMelody[] = { 69,72,76,72,69,72,76,81,   77,81,77,74,77,81,84,81,
                                79,83,79,76,79,83,86,83,   76,79,81,79,76,74,72,71 };
const uint8_t kPlayBass[]   = { 45,45,45,45,  41,41,41,41,  43,43,43,43,  40,40,45,45 };

struct TrackData {
    float bpm;
    const uint8_t* melody; size_t melodyLen;
    const uint8_t* bass;   size_t bassLen;
    bool hat;                                        // 裏拍にハイハットを入れるか
};

std::vector<int16_t> BuildBgm(const TrackData& t)
{
    const float eighth  = 60.0f / t.bpm / kStepsPerBeat;     // 8分音符の長さ (秒)
    const float quarter = eighth * kStepsPerBeat;            // 4分音符の長さ
    Synth synth((size_t)((float)t.melodyLen * eighth * (float)au::kSampleRate));

    for (size_t i = 0; i < t.melodyLen; ++i) {
        if (t.melody[i] != 0) {
            const float hz = MidiToHz(t.melody[i]);
            synth.AddNote((float)i * eighth, hz, hz, eighth * au::kNoteGateRatio,
                          Wave::Pulse, au::kLeadAmplitude, au::kBgmLeadDecay);
        }
        if (t.hat && (i % 2) == 1)                           // 裏拍
            synth.AddNote((float)i * eighth, 0, 0, au::kHatSeconds, Wave::Noise, au::kHatAmplitude, au::kHatDecay);
    }
    for (size_t i = 0; i < t.bassLen; ++i) {
        if (t.bass[i] == 0) continue;
        const float hz = MidiToHz(t.bass[i]);
        synth.AddNote((float)i * quarter, hz, hz, quarter * au::kNoteGateRatio,
                      Wave::Triangle, au::kBassAmplitude, au::kBgmBassDecay);
    }
    return synth.ToPcm();
}

} // namespace

// ---------------------------------------------------------------------------
// 初期化 / 終了
// ---------------------------------------------------------------------------
bool Audio::Init()
{
    comInitialized_ = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));   // S_FALSE (初期化済み) も成功扱い

    if (FAILED(XAudio2Create(&xa_, 0, XAUDIO2_DEFAULT_PROCESSOR))) { xa_ = nullptr; return false; }
    if (FAILED(xa_->CreateMasteringVoice(&master_))) { Shutdown(); return false; }

    // 全ての音で同じ形式 (44.1kHz / 16bit / モノラル) を使う
    WAVEFORMATEX wf = {};
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = kChannels;
    wf.nSamplesPerSec  = au::kSampleRate;
    wf.wBitsPerSample  = kBitsPerSample;
    wf.nBlockAlign     = (WORD)(wf.nChannels * wf.wBitsPerSample / 8);   // 1サンプルのバイト数
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;

    // 波形を合成してバッファに持っておく
    sfxBuffers_[(int)Sfx::Select]   = BuildSfx(kSelect);
    sfxBuffers_[(int)Sfx::Confirm]  = BuildSfx(kConfirm);
    sfxBuffers_[(int)Sfx::Back]     = BuildSfx(kBack);
    sfxBuffers_[(int)Sfx::Harvest]  = BuildSfx(kHarvest);
    sfxBuffers_[(int)Sfx::Place]    = BuildSfx(kPlace);
    sfxBuffers_[(int)Sfx::Bridge]   = BuildSfx(kBridge);
    sfxBuffers_[(int)Sfx::Bolt]     = BuildSfx(kBolt);
    sfxBuffers_[(int)Sfx::Pushed]   = BuildSfx(kPushed);
    sfxBuffers_[(int)Sfx::Respawn]  = BuildSfx(kRespawn);
    sfxBuffers_[(int)Sfx::Rush]     = BuildSfx(kRush);
    sfxBuffers_[(int)Sfx::Pickup]   = BuildSfx(kPickup);
    sfxBuffers_[(int)Sfx::Deposit]  = BuildSfx(kDeposit);
    sfxBuffers_[(int)Sfx::Craft]    = BuildSfx(kCraft);
    sfxBuffers_[(int)Sfx::Deny]     = BuildSfx(kDeny);
    sfxBuffers_[(int)Sfx::Drop]     = BuildSfx(kDrop);
    sfxBuffers_[(int)Sfx::Smash]    = BuildSfx(kSmash);
    sfxBuffers_[(int)Sfx::Buy]      = BuildSfx(kBuy);
    sfxBuffers_[(int)Sfx::Clear]    = BuildSfx(kClear);
    sfxBuffers_[(int)Sfx::GameOver] = BuildSfx(kGameOver);
    bgmMenu_ = BuildBgm({ au::kBgmMenuBpm, kMenuMelody, sizeof(kMenuMelody), kMenuBass, sizeof(kMenuBass), false });
    bgmPlay_ = BuildBgm({ au::kBgmPlayBpm, kPlayMelody, sizeof(kPlayMelody), kPlayBass, sizeof(kPlayBass), true });

    // 効果音用のボイス (同時発音ぶん) と BGM 用のボイス
    for (int i = 0; i < au::kSfxVoices; ++i) {
        IXAudio2SourceVoice* v = nullptr;
        if (SUCCEEDED(xa_->CreateSourceVoice(&v, &wf))) sfxVoices_.push_back(v);
    }
    if (FAILED(xa_->CreateSourceVoice(&bgmVoice_, &wf))) bgmVoice_ = nullptr;

    ready_ = true;
    SetVolumes(masterStep_, musicStep_, sfxStep_);
    return true;
}

void Audio::Shutdown()
{
    ready_ = false;
    for (IXAudio2SourceVoice* v : sfxVoices_) if (v) v->DestroyVoice();
    sfxVoices_.clear();
    if (bgmVoice_) { bgmVoice_->DestroyVoice(); bgmVoice_ = nullptr; }
    if (master_)   { master_->DestroyVoice();   master_ = nullptr; }
    if (xa_)       { xa_->Release();            xa_ = nullptr; }
    if (comInitialized_) { CoUninitialize(); comInitialized_ = false; }
    currentBgm_ = Bgm::None;
}

// ---------------------------------------------------------------------------
// 再生
// ---------------------------------------------------------------------------
void Audio::PlaySfx(Sfx sfx)
{
    if (!ready_ || sfxVoices_.empty()) return;
    const std::vector<int16_t>& pcm = sfxBuffers_[(int)sfx];
    if (pcm.empty()) return;

    // 空いているボイスを探す (見つからなければ古いものを止めて使う)
    const size_t n = sfxVoices_.size();
    IXAudio2SourceVoice* voice = nullptr;
    for (size_t k = 0; k < n; ++k) {
        IXAudio2SourceVoice* v = sfxVoices_[(nextVoice_ + k) % n];
        XAUDIO2_VOICE_STATE state;
        v->GetState(&state);
        if (state.BuffersQueued == 0) { voice = v; nextVoice_ = (int)((nextVoice_ + k + 1) % n); break; }
    }
    if (!voice) {
        voice = sfxVoices_[nextVoice_];
        nextVoice_ = (int)((nextVoice_ + 1) % n);
        voice->Stop();
        voice->FlushSourceBuffers();
    }

    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = (UINT32)(pcm.size() * sizeof(int16_t));
    buf.pAudioData = reinterpret_cast<const BYTE*>(pcm.data());
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    voice->SubmitSourceBuffer(&buf);
    voice->Start(0);
}

void Audio::PlayBgm(Bgm bgm)
{
    if (!ready_ || !bgmVoice_ || bgm == currentBgm_) return;
    currentBgm_ = bgm;

    bgmVoice_->Stop();
    bgmVoice_->FlushSourceBuffers();
    if (bgm == Bgm::None) return;

    const std::vector<int16_t>& pcm = (bgm == Bgm::Menu) ? bgmMenu_ : bgmPlay_;
    XAUDIO2_BUFFER buf = {};
    buf.AudioBytes = (UINT32)(pcm.size() * sizeof(int16_t));
    buf.pAudioData = reinterpret_cast<const BYTE*>(pcm.data());
    buf.Flags      = XAUDIO2_END_OF_STREAM;
    buf.LoopCount  = XAUDIO2_LOOP_INFINITE;               // 曲全体を繰り返す
    bgmVoice_->SubmitSourceBuffer(&buf);
    bgmVoice_->Start(0);
}

// ---------------------------------------------------------------------------
// 音量
// ---------------------------------------------------------------------------
float Audio::StepToGain(int step) const
{
    const float t = (float)std::min(std::max(step, 0), au::kVolumeSteps) / (float)au::kVolumeSteps;   // 0..1
    return powf(t, au::kVolumeCurve);
}

void Audio::SetVolumes(int master, int music, int sfx)
{
    masterStep_ = master; musicStep_ = music; sfxStep_ = sfx;
    if (!ready_) return;
    master_->SetVolume(StepToGain(master));
    if (bgmVoice_) bgmVoice_->SetVolume(StepToGain(music));
    for (IXAudio2SourceVoice* v : sfxVoices_) v->SetVolume(StepToGain(sfx));
}

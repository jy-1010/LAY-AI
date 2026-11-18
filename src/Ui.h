#pragma once
#include "Renderer.h"

// 画面座標(左上原点, ピクセル)のUI描画。
// 外部ライブラリを使わず、5x7 のドット文字を「立方体のインスタンス」で描画する。
//   Flush(ui.Projection(), lit=false, blend=true, clearDepth=true) で描くこと。
//   奥行き (正射影の z) は cfg::ui::kZ* を参照: 暗幕 < 影 < 文字 < フェード の順に手前になる。
//   半透明は深度を書き込むので、遠い物から Submit すること。
class Ui {
public:
    void Init(int cubeMesh, UINT pixelW, UINT pixelH) { cube_ = cubeMesh; SetScreen(pixelW, pixelH); }

    // 画面サイズ (ピクセル) が変わったら呼ぶ。UI の座標系は縦が常に cfg::ui::kVirtualHeight で、
    // 横幅は画面の縦横比から決める (どんな解像度でも配置と文字の大きさの比率が変わらない)
    void SetScreen(UINT pixelW, UINT pixelH)
    {
        h_ = cfg::ui::kVirtualHeight;
        w_ = h_ * (float)pixelW / (float)pixelH;
    }
    float Width() const  { return w_; }          // UI座標での画面の幅
    float Height() const { return h_; }          // UI座標での画面の高さ

    // UI座標がそのまま座標になる正射影 (Y は上向き。Rect 内で上原点に変換する)
    DirectX::XMMATRIX Projection() const { return DirectX::XMMatrixOrthographicOffCenterLH(0, w_, 0, h_, cfg::ui::kOrthoNearZ, cfg::ui::kOrthoFarZ); }

    void  Rect(Renderer& r, float x, float y, float w, float h, const DirectX::XMFLOAT4& c, float z) const;
    float TextWidth(const char* s, float pixel) const;
    // x, y = 左上のピクセル座標, pixel = 1ドットの大きさ (ピクセル)
    void  Text(Renderer& r, const char* s, float x, float y, float pixel, const DirectX::XMFLOAT4& c) const;
    void  TextCenter(Renderer& r, const char* s, float y, float pixel, const DirectX::XMFLOAT4& c) const;

private:
    int   cube_ = -1;                // UI に使う立方体メッシュのID
    float w_ = 0, h_ = 0;            // 画面サイズ (UI座標)
};

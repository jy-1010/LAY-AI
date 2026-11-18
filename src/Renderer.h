#pragma once
#ifndef NOMINMAX
#define NOMINMAX      // windows.h の min/max マクロが std::min/max と衝突しないように
#endif
#include <windows.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "Mesh.h"

// インスタンシング描画器
//   AddMesh()  : メッシュを登録 (インスタンスバッファは DYNAMIC)
//   1フレーム  : BeginFrame → Submit(メッシュ, ワールド行列, 色) を何度でも → Flush(カメラ) → …
//                (Submit → Flush を繰り返せば 3D → UI のように複数パスを描ける) → Present
//   Flush で メッシュごとに 1 回の DrawIndexedInstanced で全インスタンスを描画する
class Renderer {
public:
    bool Init(HWND hwnd, UINT w, UINT h);                       // デバイス・スワップチェーン・深度バッファの作成
    bool Resize(UINT w, UINT h);                                // 画面サイズの変更 (ウィンドウのリサイズ / フルスクリーン切替)
    void SetVSync(bool on) { vsync_ = on; }                     // 垂直同期の ON/OFF
    bool LoadShader(const std::wstring& shaderPath);            // HLSL のコンパイルと入力レイアウト等の作成
    int  AddMesh(const MeshData& mesh, UINT maxInstances);      // 戻り値 = メッシュID (失敗時 -1)

    void BeginFrame(const float clear[4]);                      // 画面と深度をクリアし、共通の状態を設定
    void Submit(int meshId, const DirectX::XMMATRIX& world, const DirectX::XMFLOAT4& color);
    // ここまでに Submit した分を描画してリストを空にする
    //   lit       : ライティングを使う (UIは false)
    //   blend     : アルファブレンドを使う (a < 1 の半透明を描く時)
    //   clearDepth: 描画前に深度をクリア (UIパス用)
    void Flush(const DirectX::XMMATRIX& viewProj, bool lit = true, bool blend = false, bool clearDepth = false);
    void Present();                                             // 画面に表示

    const std::wstring& LastError() const { return lastError_; }

private:
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool CreateTargets(UINT w, UINT h);        // バックバッファのレンダーターゲットと深度バッファを作る

    // 1メッシュぶんの GPU リソースと、今フレームのインスタンスリスト
    struct GpuMesh {
        ComPtr<ID3D11Buffer> vb, ib, instb;
        UINT indexCount   = 0;
        UINT maxInstances = 0;
        std::vector<InstanceData> cpu;      // 今フレームのインスタンス (CPU側)
        unsigned dropped = 0;               // 上限を超えて捨てた数 (描かれなかった物がある印)
    };

    ComPtr<ID3D11Device>            dev_;
    ComPtr<ID3D11DeviceContext>     ctx_;
    ComPtr<IDXGISwapChain>          swap_;
    ComPtr<ID3D11RenderTargetView>  rtv_;
    ComPtr<ID3D11DepthStencilView>  dsv_;
    ComPtr<ID3D11VertexShader>      vs_;
    ComPtr<ID3D11PixelShader>       ps_;
    ComPtr<ID3D11InputLayout>       layout_;
    ComPtr<ID3D11Buffer>            cb_;
    ComPtr<ID3D11BlendState>        blendAlpha_;

    std::vector<GpuMesh> meshes_;
    std::wstring lastError_;
    UINT width_ = 0, height_ = 0;
    bool vsync_ = true;
};

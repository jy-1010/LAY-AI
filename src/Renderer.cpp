#include "Renderer.h"
#include <d3dcompiler.h>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <utility>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

namespace {

// HLSL の cbuffer CB と同じ並び (16バイト境界に揃えてある)
struct CBData {
    XMFLOAT4X4 viewProj;
    XMFLOAT4   params;          // x: ライティング (1 = 有効)
};

// --- Direct3D / 入力レイアウトの設定値 ---
constexpr DXGI_FORMAT kBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT kDepthFormat      = DXGI_FORMAT_D24_UNORM_S8_UINT;
constexpr float       kDepthClearValue  = 1.0f;          // 深度のクリア値 (= 一番奥)
constexpr UINT8       kStencilClearValue = 0;
constexpr UINT        kSampleCount      = 1;             // MSAA なし
constexpr UINT        kSingleMip        = 1;
constexpr UINT        kSingleArray      = 1;
constexpr UINT        kSlotVertex       = 0;             // 頂点データを入れる入力スロット
constexpr UINT        kSlotInstance     = 1;             // インスタンスデータを入れる入力スロット
constexpr UINT        kInstanceStepRate = 1;             // 1インスタンスごとにデータを進める
constexpr UINT        kMatrixRowBytes   = sizeof(XMFLOAT4);   // ワールド行列の1行のバイト数
constexpr UINT        kVertexBufferCount = 2;            // 同時にバインドする頂点バッファ数 (頂点 + インスタンス)
constexpr UINT        kBlendSampleMask  = 0xFFFFFFFF;    // ブレンドのサンプルマスク (全サンプル有効)
const float           kBlendFactor[]    = { 0, 0, 0, 0 };    // 使わない (SRC_ALPHA ブレンドなので)
constexpr float       kMinDepth = 0.0f, kMaxDepth = 1.0f;   // ビューポートの深度範囲
constexpr float       kLightOn = 1.0f, kLightOff = 0.0f;     // 定数バッファ params.x の値
constexpr size_t      kHexBufferChars   = 16;                // HRESULT を16進文字列にするバッファ

} // namespace

// ---------------------------------------------------------------------------
// デバイス / スワップチェーン / 深度バッファ
// ---------------------------------------------------------------------------
bool Renderer::Init(HWND hwnd, UINT w, UINT h)
{
    width_ = w; height_ = h;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount       = cfg::kSwapChainBuffers;
    sd.BufferDesc.Width  = w;
    sd.BufferDesc.Height = h;
    sd.BufferDesc.Format = kBackBufferFormat;
    sd.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow      = hwnd;
    sd.SampleDesc.Count  = kSampleCount;
    sd.Windowed          = TRUE;

    if (FAILED(D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &sd, &swap_, &dev_, nullptr, &ctx_)))
        return false;

    return CreateTargets(w, h);
}

// バックバッファのレンダーターゲットビューと、深度バッファを (再) 作成する
bool Renderer::CreateTargets(UINT w, UINT h)
{
    // バックバッファへ描くためのレンダーターゲットビュー
    ComPtr<ID3D11Texture2D> back;
    if (FAILED(swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return false;
    if (FAILED(dev_->CreateRenderTargetView(back.Get(), nullptr, &rtv_))) return false;

    // 深度バッファ (手前の物が奥の物を隠すため)
    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width = w; dd.Height = h;
    dd.MipLevels = kSingleMip; dd.ArraySize = kSingleArray;
    dd.Format = kDepthFormat;
    dd.SampleDesc.Count = kSampleCount;
    dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    ComPtr<ID3D11Texture2D> depth;
    if (FAILED(dev_->CreateTexture2D(&dd, nullptr, &depth))) return false;
    if (FAILED(dev_->CreateDepthStencilView(depth.Get(), nullptr, &dsv_))) return false;
    return true;
}

// 画面サイズが変わった時 (リサイズ / フルスクリーン切替): バッファを作り直す
bool Renderer::Resize(UINT w, UINT h)
{
    if (!swap_ || w == 0 || h == 0) return false;
    ctx_->OMSetRenderTargets(0, nullptr, nullptr);       // 古いバッファへの参照を外してから解放する
    rtv_.Reset();
    dsv_.Reset();
    if (FAILED(swap_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return false;   // 0 / UNKNOWN = 今のまま
    width_ = w; height_ = h;
    return CreateTargets(w, h);
}

// ---------------------------------------------------------------------------
// シェーダ / 入力レイアウト / 定数バッファ / ブレンド
// ---------------------------------------------------------------------------
bool Renderer::LoadShader(const std::wstring& shaderPath)
{
    ComPtr<ID3DBlob> vsBlob, psBlob;
    // HLSL ファイルをコンパイル。失敗したら HRESULT とエラー本文を lastError_ に残す
    auto compile = [&](const char* entry, const char* target, ComPtr<ID3DBlob>& out) -> bool {
        ComPtr<ID3DBlob> err;
        HRESULT hr = D3DCompileFromFile(shaderPath.c_str(), nullptr, nullptr,
                                        entry, target, 0, 0, &out, &err);
        if (SUCCEEDED(hr)) return true;
        wchar_t hex[kHexBufferChars];
        swprintf_s(hex, L"0x%08X", (unsigned)hr);
        lastError_ = std::wstring(L"シェーダのコンパイルに失敗 HRESULT=") + hex
                   + L"\nパス: " + shaderPath + L"\n";
        if (err) {
            std::string s((const char*)err->GetBufferPointer(), err->GetBufferSize());
            int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_ACP, 0, s.c_str(), -1, &w[0], n);
            lastError_ += w.c_str();
        } else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
                   hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
            lastError_ += L"→ HLSLファイルが見つかりません";
        }
        return false;
    };
    if (!compile("VSMain", "vs_5_0", vsBlob)) return false;
    if (!compile("PSMain", "ps_5_0", psBlob)) return false;
    dev_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &vs_);
    dev_->CreatePixelShader (psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &ps_);

    // 入力レイアウト: slot0 = 頂点 (位置/法線/頂点色)、slot1 = インスタンス (ワールド行列4行/色)
    // オフセットは offsetof で構造体から求める (手で数字を書かない)
    const UINT worldOffset = (UINT)offsetof(InstanceData, world);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    kSlotVertex,   (UINT)offsetof(Vertex, pos),    D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    kSlotVertex,   (UINT)offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA,   0 },
        { "VCOLOR",   0, DXGI_FORMAT_R32G32B32A32_FLOAT, kSlotVertex,   (UINT)offsetof(Vertex, color),  D3D11_INPUT_PER_VERTEX_DATA,   0 },
        // ワールド行列は 4つの float4 (INSTANCE0..3) に分けて渡す
        { "INSTANCE", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, kSlotInstance, worldOffset + kMatrixRowBytes * 0, D3D11_INPUT_PER_INSTANCE_DATA, kInstanceStepRate },
        { "INSTANCE", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, kSlotInstance, worldOffset + kMatrixRowBytes * 1, D3D11_INPUT_PER_INSTANCE_DATA, kInstanceStepRate },
        { "INSTANCE", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, kSlotInstance, worldOffset + kMatrixRowBytes * 2, D3D11_INPUT_PER_INSTANCE_DATA, kInstanceStepRate },
        { "INSTANCE", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, kSlotInstance, worldOffset + kMatrixRowBytes * 3, D3D11_INPUT_PER_INSTANCE_DATA, kInstanceStepRate },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, kSlotInstance, (UINT)offsetof(InstanceData, color), D3D11_INPUT_PER_INSTANCE_DATA, kInstanceStepRate },
    };
    if (FAILED(dev_->CreateInputLayout(il, _countof(il),
            vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &layout_))) {
        lastError_ = L"CreateInputLayout に失敗（HLSLの入力とレイアウトが不一致）";
        return false;
    }

    // 定数バッファ (カメラ行列 + ライティング有無)
    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth = sizeof(CBData);
    cbd.Usage     = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(dev_->CreateBuffer(&cbd, nullptr, &cb_))) return false;

    // アルファブレンド (フェード/暗幕用): 結果 = 色*a + 背景*(1-a)
    D3D11_BLEND_DESC bl = {};
    bl.RenderTarget[0].BlendEnable           = TRUE;
    bl.RenderTarget[0].SrcBlend              = D3D11_BLEND_SRC_ALPHA;
    bl.RenderTarget[0].DestBlend             = D3D11_BLEND_INV_SRC_ALPHA;
    bl.RenderTarget[0].BlendOp               = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].SrcBlendAlpha         = D3D11_BLEND_ONE;
    bl.RenderTarget[0].DestBlendAlpha        = D3D11_BLEND_ZERO;
    bl.RenderTarget[0].BlendOpAlpha          = D3D11_BLEND_OP_ADD;
    bl.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    return SUCCEEDED(dev_->CreateBlendState(&bl, &blendAlpha_));
}

// ---------------------------------------------------------------------------
// メッシュの登録 (①ロード)
// ---------------------------------------------------------------------------
int Renderer::AddMesh(const MeshData& mesh, UINT maxInstances)
{
    GpuMesh g;
    g.indexCount   = (UINT)mesh.i.size();
    g.maxInstances = maxInstances;
    g.cpu.reserve(maxInstances);

    // 頂点バッファ / インデックスバッファは作ったら変更しない (IMMUTABLE)
    D3D11_BUFFER_DESC bd = {};
    bd.Usage     = D3D11_USAGE_IMMUTABLE;
    bd.ByteWidth = (UINT)(mesh.v.size() * sizeof(Vertex));
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sr = { mesh.v.data() };
    if (FAILED(dev_->CreateBuffer(&bd, &sr, &g.vb))) { lastError_ = L"頂点バッファ作成に失敗"; return -1; }

    bd.ByteWidth = (UINT)(mesh.i.size() * sizeof(uint32_t));
    bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
    sr.pSysMem   = mesh.i.data();
    if (FAILED(dev_->CreateBuffer(&bd, &sr, &g.ib))) { lastError_ = L"インデックスバッファ作成に失敗"; return -1; }

    // インスタンスバッファは毎フレーム書き換えるので DYNAMIC (CPU から書き込める)
    D3D11_BUFFER_DESC ib = {};
    ib.Usage          = D3D11_USAGE_DYNAMIC;
    ib.ByteWidth      = maxInstances * sizeof(InstanceData);
    ib.BindFlags      = D3D11_BIND_VERTEX_BUFFER;
    ib.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev_->CreateBuffer(&ib, nullptr, &g.instb))) { lastError_ = L"インスタンスバッファ作成に失敗"; return -1; }

    meshes_.push_back(std::move(g));
    return (int)meshes_.size() - 1;
}

// ---------------------------------------------------------------------------
// 1フレームの描画 (②描画)
// ---------------------------------------------------------------------------
void Renderer::BeginFrame(const float clear[4])
{
    ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), dsv_.Get());
    ctx_->ClearRenderTargetView(rtv_.Get(), clear);
    ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, kDepthClearValue, kStencilClearValue);

    D3D11_VIEWPORT vpt = { 0, 0, (float)width_, (float)height_, kMinDepth, kMaxDepth };
    ctx_->RSSetViewports(1, &vpt);

    // 全メッシュ共通のパイプライン設定
    ctx_->IASetInputLayout(layout_.Get());
    ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx_->VSSetShader(vs_.Get(), nullptr, 0);
    ctx_->VSSetConstantBuffers(0, 1, cb_.GetAddressOf());
    ctx_->PSSetShader(ps_.Get(), nullptr, 0);

    for (auto& m : meshes_) m.cpu.clear();
}

void Renderer::Submit(int meshId, const XMMATRIX& world, const XMFLOAT4& color)
{
    if (meshId < 0 || meshId >= (int)meshes_.size()) return;
    GpuMesh& m = meshes_[meshId];
    if (m.cpu.size() >= m.maxInstances) { ++m.dropped; return; }   // バッファが一杯なら捨てる (後から Submit した物が描かれなくなる)
    InstanceData d;
    XMStoreFloat4x4(&d.world, world);
    d.color = color;
    m.cpu.push_back(d);
}

void Renderer::Flush(const XMMATRIX& viewProj, bool lit, bool blend, bool clearDepth)
{
    // カメラ行列とライティング有無を定数バッファへ
    CBData cb;
    XMStoreFloat4x4(&cb.viewProj, viewProj);
    cb.params = XMFLOAT4(lit ? kLightOn : kLightOff, 0.0f, 0.0f, 0.0f);
    ctx_->UpdateSubresource(cb_.Get(), 0, nullptr, &cb, 0, 0);

    if (clearDepth) ctx_->ClearDepthStencilView(dsv_.Get(), D3D11_CLEAR_DEPTH, kDepthClearValue, kStencilClearValue);
    ctx_->OMSetBlendState(blend ? blendAlpha_.Get() : nullptr, kBlendFactor, kBlendSampleMask);

    const UINT strides[kVertexBufferCount] = { sizeof(Vertex), sizeof(InstanceData) };
    const UINT offsets[kVertexBufferCount] = { 0, 0 };
    for (GpuMesh& m : meshes_) {
        if (m.cpu.empty()) continue;

        // 今フレームのインスタンスを GPU バッファへ転送 (DISCARD = 前のデータを捨てて新しい領域を使う)
        D3D11_MAPPED_SUBRESOURCE ms;
        if (SUCCEEDED(ctx_->Map(m.instb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &ms))) {
            memcpy(ms.pData, m.cpu.data(), m.cpu.size() * sizeof(InstanceData));
            ctx_->Unmap(m.instb.Get(), 0);

            ID3D11Buffer* bufs[kVertexBufferCount] = { m.vb.Get(), m.instb.Get() };
            ctx_->IASetVertexBuffers(0, kVertexBufferCount, bufs, strides, offsets);
            ctx_->IASetIndexBuffer(m.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
            // メッシュ1種類 = 1ドローコール (数千個のオブジェクトでも)
            ctx_->DrawIndexedInstanced(m.indexCount, (UINT)m.cpu.size(), 0, 0, 0);
        }
        if (m.dropped > 0) {                             // 上限が小さすぎる合図。デバッガの出力ウィンドウに出す
            char msg[96];
            snprintf(msg, sizeof(msg), "Renderer: instance buffer full, %u instances were not drawn (raise cfg::kMax*Instances)\n", m.dropped);
            OutputDebugStringA(msg);
            m.dropped = 0;
        }
        m.cpu.clear();
    }
}

void Renderer::Present()
{
    swap_->Present(vsync_ ? cfg::kVSyncInterval : 0, 0);
}

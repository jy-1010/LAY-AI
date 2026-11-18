#pragma once
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdint>
#include <utility>
#include "Config.h"

// ---- GPU に渡すデータ構造 ----------------------------------------------------

struct Vertex {                     // スロット0 : 頂点ごと
    DirectX::XMFLOAT3 pos;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT4 color = DirectX::XMFLOAT4(1, 1, 1, 1);   // 頂点色 (既定は白 = インスタンス色がそのまま出る)
};

struct InstanceData {               // スロット1 : インスタンスごと
    DirectX::XMFLOAT4X4 world;      // ワールド行列 (行優先 = 4 x float4)
    DirectX::XMFLOAT4   color;      // インスタンスの色 (頂点色に乗算される。a = 不透明度)
};

struct MeshData {                   // CPU 側のメッシュ
    std::vector<Vertex>   v;
    std::vector<uint32_t> i;        // 32bit インデックス (大きなOBJも読めるように)
};

// 設定値の色 → シェーダに渡す float4
inline DirectX::XMFLOAT4 ToColor(const cfg::Rgb& c, float a = cfg::kOpaque)
{
    return DirectX::XMFLOAT4(c.r, c.g, c.b, a);
}
inline DirectX::XMFLOAT4 ToColor(const cfg::Rgba& c)
{
    return DirectX::XMFLOAT4(c.r, c.g, c.b, c.a);
}

// 三角形を追加。頂点法線(外向き)に対して表面になるよう巻き順を自動補正する。
// (D3D の既定は「時計回りが表面」。左手系では cross(b-a, c-a) が外向きのとき表面になる)
inline void AddTri(MeshData& m, uint32_t a, uint32_t b, uint32_t c)
{
    using namespace DirectX;
    XMVECTOR pa = XMLoadFloat3(&m.v[a].pos);
    XMVECTOR pb = XMLoadFloat3(&m.v[b].pos);
    XMVECTOR pc = XMLoadFloat3(&m.v[c].pos);
    XMVECTOR n  = XMLoadFloat3(&m.v[a].normal);
    float facing = XMVectorGetX(XMVector3Dot(XMVector3Cross(pb - pa, pc - pa), n));
    if (facing < 0.0f) std::swap(b, c);          // 裏向きなら 2頂点を入れ替えて向きを直す
    m.i.push_back(a); m.i.push_back(b); m.i.push_back(c);
}

// OBJファイルを読み込む (外部ライブラリなし)
//  対応: v / vn / f (v, v/vt, v//vn, v/vt/vn、負のインデックス、4角形以上は扇状に三角形化)
//  色: .mtl の Kd (mtllib / usemtl)、または頂点色拡張 (v x y z r g b) を頂点色として読む
//  法線が無い場合は面から自動生成 (スムース)
//  読み込み後、最大辺が 1 になるよう原点中心に正規化し、右手系→左手系 (z反転) に変換する
bool LoadObj(const std::wstring& path, MeshData& out, std::wstring& err);

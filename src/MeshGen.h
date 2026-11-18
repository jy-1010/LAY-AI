#pragma once
#include "Mesh.h"
#include <cmath>

// 手続き生成メッシュ。どれも中心が原点で 1x1x1 に収まる。
// 頂点色は白なので、インスタンス色がそのまま表示色になる。

// ---- 立方体 (24頂点 / 36インデックス) ---------------------------------------
inline MeshData MakeCube()
{
    using namespace DirectX;
    MeshData m;
    const float h = cfg::mesh::kHalfExtent;
    const XMFLOAT3 normals[cfg::mesh::kCubeFaces] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };   // 6面の外向き法線
    for (const auto& nf : normals) {
        XMVECTOR n = XMLoadFloat3(&nf);
        // 面の張る2軸 u, v を求める (n と平行にならない軸を選ぶ)
        XMVECTOR u = (fabsf(nf.y) > cfg::mesh::kVerticalNormalThreshold) ? XMVectorSet(1, 0, 0, 0) : XMVectorSet(0, 1, 0, 0);
        XMVECTOR v = XMVector3Cross(n, u);
        // 4頂点 = 面の中心 (n*h) から ±u*h, ±v*h
        const float su[cfg::mesh::kQuadCorners] = { -1,  1, 1, -1 };
        const float sv[cfg::mesh::kQuadCorners] = { -1, -1, 1,  1 };
        uint32_t base = (uint32_t)m.v.size();
        for (int k = 0; k < cfg::mesh::kQuadCorners; ++k) {
            Vertex vt;
            XMStoreFloat3(&vt.pos, (n + u * su[k] + v * sv[k]) * h);
            vt.normal = nf;
            m.v.push_back(vt);
        }
        AddTri(m, base, base + 1, base + 2);          // 四角形 = 三角形2枚
        AddTri(m, base, base + 2, base + 3);
    }
    return m;
}

// ---- 球 (緯度経度分割) -------------------------------------------------------
inline MeshData MakeSphere(int slices = cfg::mesh::kSphereSlices, int stacks = cfg::mesh::kSphereStacks)
{
    using namespace DirectX;
    MeshData m;
    const float radius = cfg::mesh::kHalfExtent;
    for (int i = 0; i <= stacks; ++i) {
        float theta = cfg::kPi * i / stacks;                 // 緯度方向の角 (0 = 北極, π = 南極)
        for (int j = 0; j <= slices; ++j) {
            float phi = cfg::kTwoPi * j / slices;            // 経度方向の角
            XMFLOAT3 n(sinf(theta) * cosf(phi), cosf(theta), sinf(theta) * sinf(phi));   // 単位球上の点 = 法線
            Vertex vt;
            vt.normal = n;
            vt.pos    = XMFLOAT3(n.x * radius, n.y * radius, n.z * radius);
            m.v.push_back(vt);
        }
    }
    const int stride = slices + 1;                           // 1段あたりの頂点数
    for (int i = 0; i < stacks; ++i)
    for (int j = 0; j < slices; ++j) {
        uint32_t a = (uint32_t)(i * stride + j);             // 四角形の4頂点
        uint32_t b = a + 1;
        uint32_t c = a + stride;
        uint32_t d = c + 1;
        AddTri(m, a, b, c);
        AddTri(m, b, d, c);
    }
    return m;
}

// ---- 四角錐 (側面はフラットシェーディング) -----------------------------------
inline MeshData MakePyramid()
{
    using namespace DirectX;
    MeshData m;
    const float h = cfg::mesh::kHalfExtent;
    const XMFLOAT3 base4[cfg::mesh::kPyramidSides] = { {-h,-h,-h}, {h,-h,-h}, {h,-h,h}, {-h,-h,h} };   // 底面の4隅
    const XMFLOAT3 apex(0.0f, h, 0.0f);                                          // 頂点
    for (int k = 0; k < cfg::mesh::kPyramidSides; ++k) {                         // 側面 (底辺の数だけ)
        XMVECTOR A = XMLoadFloat3(&base4[k]);
        XMVECTOR B = XMLoadFloat3(&base4[(k + 1) % cfg::mesh::kPyramidSides]);
        XMVECTOR T = XMLoadFloat3(&apex);
        XMVECTOR n = XMVector3Normalize(XMVector3Cross(B - A, T - A));
        XMVECTOR centroid = (A + B + T) / (float)cfg::mesh::kTriangleCorners;                                  // 面の重心 (原点から見て外向きに法線を揃える)
        if (XMVectorGetX(XMVector3Dot(n, centroid)) < 0.0f) n = -n;
        XMFLOAT3 nf; XMStoreFloat3(&nf, n);
        uint32_t first = (uint32_t)m.v.size();
        Vertex v0, v1, v2;
        v0.pos = base4[k];           v0.normal = nf;
        v1.pos = base4[(k + 1) % cfg::mesh::kPyramidSides]; v1.normal = nf;
        v2.pos = apex;               v2.normal = nf;
        m.v.push_back(v0); m.v.push_back(v1); m.v.push_back(v2);
        AddTri(m, first, first + 1, first + 2);
    }
    uint32_t first = (uint32_t)m.v.size();                                       // 底面
    for (int k = 0; k < cfg::mesh::kPyramidSides; ++k) {
        Vertex v; v.pos = base4[k]; v.normal = XMFLOAT3(0, -1, 0);
        m.v.push_back(v);
    }
    AddTri(m, first, first + 1, first + 2);
    AddTri(m, first, first + 2, first + 3);
    return m;
}

// ---- 六角柱 (ボルトの頭) -----------------------------------------------------
inline MeshData MakeHexPrism()
{
    using namespace DirectX;
    MeshData m;
    const int   sides  = cfg::mesh::kHexSides;
    const float radius = cfg::mesh::kHalfExtent;
    const float halfH  = cfg::mesh::kHalfExtent;
    const float step   = cfg::kTwoPi / sides;                                    // 1辺ぶんの角度

    auto rim = [&](int k, float y) {                                             // 外周の k 番目の頂点
        return XMFLOAT3(radius * cosf(step * k), y, radius * sinf(step * k));
    };

    for (int k = 0; k < sides; ++k) {                                            // 側面 (6枚の長方形)
        float mid = step * (k + cfg::kHalf);                                     // 辺の中央の角度 = 法線の向き
        XMFLOAT3 n(cosf(mid), 0.0f, sinf(mid));
        uint32_t first = (uint32_t)m.v.size();
        const XMFLOAT3 corners[cfg::mesh::kQuadCorners] = { rim(k, -halfH), rim(k + 1, -halfH), rim(k + 1, halfH), rim(k, halfH) };
        for (const XMFLOAT3& c : corners) { Vertex v; v.pos = c; v.normal = n; m.v.push_back(v); }
        AddTri(m, first, first + 1, first + 2);
        AddTri(m, first, first + 2, first + 3);
    }
    for (int cap = 0; cap < 2; ++cap) {                                          // 上面・下面 (扇状)
        const float y = (cap == 0) ? halfH : -halfH;
        const XMFLOAT3 n(0.0f, (cap == 0) ? 1.0f : -1.0f, 0.0f);
        uint32_t center = (uint32_t)m.v.size();
        Vertex c; c.pos = XMFLOAT3(0.0f, y, 0.0f); c.normal = n; m.v.push_back(c);
        uint32_t ring = (uint32_t)m.v.size();
        for (int k = 0; k < sides; ++k) { Vertex v; v.pos = rim(k, y); v.normal = n; m.v.push_back(v); }
        for (int k = 0; k < sides; ++k) AddTri(m, center, ring + k, ring + (k + 1) % sides);
    }
    return m;
}

#include "Mesh.h"
#include <fstream>
#include <sstream>
#include <map>
#include <tuple>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <cmath>

using namespace DirectX;

namespace {

const float   kDefaultGray = 0.8f;                              // 色情報が無いOBJの既定色 (明るい灰色)
const XMFLOAT3 kDefaultColor(kDefaultGray, kDefaultGray, kDefaultGray);
const float   kLengthEpsilon = 1e-12f;                          // これ未満の長さの法線は「無い」とみなす (長さの2乗)

int ResolveIndex(int idx, size_t count)
{
    if (idx > 0) return idx - 1;                 // 1始まり
    if (idx < 0) return (int)count + idx;        // 負: 末尾から
    return -1;
}

// "v" / "v/vt" / "v//vn" / "v/vt/vn"
void ParseFaceToken(const std::string& tok, int& v, int& vt, int& vn)
{
    v = vt = vn = 0;
    size_t p1 = tok.find('/');
    if (p1 == std::string::npos) { v = atoi(tok.c_str()); return; }
    v = atoi(tok.substr(0, p1).c_str());
    size_t p2 = tok.find('/', p1 + 1);
    if (p2 == std::string::npos) { vt = atoi(tok.substr(p1 + 1).c_str()); return; }
    std::string mid = tok.substr(p1 + 1, p2 - p1 - 1);
    if (!mid.empty()) vt = atoi(mid.c_str());
    vn = atoi(tok.substr(p2 + 1).c_str());
}

std::string Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// .mtl から newmtl ごとの Kd (拡散反射色) を読む
void LoadMtl(const std::wstring& path, std::unordered_map<std::string, XMFLOAT3>& out)
{
    std::ifstream ifs(path);
    if (!ifs) return;
    std::string line, cur;
    while (std::getline(ifs, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "newmtl") {
            std::string rest; std::getline(ss, rest);
            cur = Trim(rest);
            out[cur] = kDefaultColor;
        } else if (tag == "Kd" && !cur.empty()) {
            XMFLOAT3 c;
            if (ss >> c.x >> c.y >> c.z) out[cur] = c;
        }
    }
}

} // namespace

bool LoadObj(const std::wstring& path, MeshData& out, std::wstring& err)
{
    std::ifstream ifs(path);                     // MSVC は wchar_t パスを受け付ける
    if (!ifs) { err = L"OBJファイルを開けません: " + path; return false; }
    const std::wstring dir = path.substr(0, path.find_last_of(L"\\/") + 1);

    std::vector<XMFLOAT3> P, N, Pcol;            // ファイル内の v / vn / 頂点色拡張
    std::vector<char>     hasPcol;
    std::vector<XMFLOAT3> pos, nor, col;         // 出力頂点 (右手系のまま)
    std::vector<char>     hasNormal;
    std::vector<uint32_t> tris;
    std::map<std::tuple<int, int, int>, uint32_t> cache;   // (v, vn, マテリアル) → 出力頂点番号

    std::unordered_map<std::string, XMFLOAT3> mtl;         // .mtl の内容
    std::map<std::string, int> matIds;                     // 使用したマテリアル
    std::vector<XMFLOAT3>      matColors;
    int curMat = -1;

    std::string line;
    while (std::getline(ifs, line)) {
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            XMFLOAT3 p; ss >> p.x >> p.y >> p.z;
            P.push_back(p);
            XMFLOAT3 c;                                     // 頂点色拡張: v x y z r g b
            if (ss >> c.x >> c.y >> c.z) { Pcol.push_back(c); hasPcol.push_back(1); }
            else                         { Pcol.push_back(kDefaultColor); hasPcol.push_back(0); }
        } else if (tag == "vn") {
            XMFLOAT3 n; ss >> n.x >> n.y >> n.z; N.push_back(n);
        } else if (tag == "mtllib") {
            std::string rest; std::getline(ss, rest);
            std::string name = Trim(rest);
            LoadMtl(dir + std::wstring(name.begin(), name.end()), mtl);
        } else if (tag == "usemtl") {
            std::string rest; std::getline(ss, rest);
            std::string name = Trim(rest);
            auto it = matIds.find(name);
            if (it == matIds.end()) {
                auto m = mtl.find(name);
                matColors.push_back(m != mtl.end() ? m->second : kDefaultColor);
                it = matIds.emplace(name, (int)matColors.size() - 1).first;
            }
            curMat = it->second;
        } else if (tag == "f") {
            std::vector<uint32_t> face;
            std::string tok;
            while (ss >> tok) {
                int v, vt, vn;
                ParseFaceToken(tok, v, vt, vn);
                int vi = ResolveIndex(v, P.size());
                int ni = vn ? ResolveIndex(vn, N.size()) : -1;
                if (vi < 0 || vi >= (int)P.size()) { err = L"OBJの面インデックスが不正です"; return false; }
                if (ni >= (int)N.size()) ni = -1;

                auto key = std::make_tuple(vi, ni, curMat);
                auto it = cache.find(key);
                uint32_t id;
                if (it == cache.end()) {
                    id = (uint32_t)pos.size();
                    pos.push_back(P[vi]);
                    nor.push_back(ni >= 0 ? N[ni] : XMFLOAT3(0, 0, 0));
                    hasNormal.push_back(ni >= 0 ? 1 : 0);
                    // 色の優先順位: マテリアル Kd > 頂点色拡張 > 既定色
                    col.push_back(curMat >= 0 ? matColors[curMat]
                                              : (hasPcol[vi] ? Pcol[vi] : kDefaultColor));
                    cache[key] = id;
                } else {
                    id = it->second;
                }
                face.push_back(id);
            }
            for (size_t k = 1; k + 1 < face.size(); ++k) {   // 扇状に三角形化
                tris.push_back(face[0]); tris.push_back(face[k]); tris.push_back(face[k + 1]);
            }
        }
    }
    if (pos.empty() || tris.empty()) { err = L"OBJに三角形がありません: " + path; return false; }

    // --- 法線が無い頂点は面法線の合計から生成 (CCW=表面 の右手系として計算) ---
    std::vector<XMVECTOR> acc(pos.size(), XMVectorZero());
    for (size_t t = 0; t + 2 < tris.size(); t += cfg::mesh::kTriangleCorners) {
        uint32_t a = tris[t], b = tris[t + 1], c = tris[t + 2];
        XMVECTOR pa = XMLoadFloat3(&pos[a]), pb = XMLoadFloat3(&pos[b]), pc = XMLoadFloat3(&pos[c]);
        XMVECTOR fn = XMVector3Cross(pb - pa, pc - pa);      // 面積で重み付け
        if (!hasNormal[a]) acc[a] += fn;
        if (!hasNormal[b]) acc[b] += fn;
        if (!hasNormal[c]) acc[c] += fn;
    }
    for (size_t i = 0; i < pos.size(); ++i) {
        XMVECTOR n = hasNormal[i] ? XMLoadFloat3(&nor[i]) : acc[i];
        if (XMVectorGetX(XMVector3LengthSq(n)) < kLengthEpsilon) n = XMVectorSet(0, 1, 0, 0);
        XMStoreFloat3(&nor[i], XMVector3Normalize(n));
    }

    // --- 原点中心・最大辺=1 に正規化 ---
    XMFLOAT3 mn = pos[0], mx = pos[0];
    for (auto& p : pos) {
        mn.x = std::min(mn.x, p.x); mn.y = std::min(mn.y, p.y); mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x); mx.y = std::max(mx.y, p.y); mx.z = std::max(mx.z, p.z);
    }
    XMFLOAT3 c((mn.x + mx.x) * cfg::kHalf, (mn.y + mx.y) * cfg::kHalf, (mn.z + mx.z) * cfg::kHalf);   // 境界ボックスの中心
    float ext = std::max({ mx.x - mn.x, mx.y - mn.y, mx.z - mn.z });
    float s = (ext > 0.0f) ? 1.0f / ext : 1.0f;

    out.v.clear(); out.i.clear();
    out.v.reserve(pos.size());
    for (size_t i = 0; i < pos.size(); ++i) {
        Vertex v;
        // 右手系(OBJ) → 左手系(D3D/本サンプル): z を反転
        v.pos    = XMFLOAT3((pos[i].x - c.x) * s, (pos[i].y - c.y) * s, -(pos[i].z - c.z) * s);
        v.normal = XMFLOAT3(nor[i].x, nor[i].y, -nor[i].z);
        v.color  = XMFLOAT4(col[i].x, col[i].y, col[i].z, 1.0f);
        out.v.push_back(v);
    }
    // 巻き順は法線を基準に自動補正
    for (size_t t = 0; t + 2 < tris.size(); t += cfg::mesh::kTriangleCorners)
        AddTri(out, tris[t], tris[t + 1], tris[t + 2]);
    return true;
}

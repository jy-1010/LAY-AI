// Instancing.hlsl : インスタンシング用シェーダ
//   slot0 = 頂点データ (位置 / 法線 / 頂点色)
//   slot1 = インスタンスデータ (ワールド行列 4行 / 色)
#pragma pack_matrix(row_major)

// ライティングの設定
static const float3 kLightDir      = float3(0.4, 0.8, -0.5);   // 光が来る方向 (正規化して使う)
static const float  kDiffuseWeight = 0.8;                      // 拡散光の強さ
static const float  kAmbient       = 0.2;                      // 環境光 (影の部分の最低の明るさ)

cbuffer CB : register(b0)
{
    float4x4 viewProj;
    float4   params;            // x: ライティング (1=有効, 0=無効 → UI用)
};

struct VSIn
{
    float3 pos : POSITION;      // slot0 : per-vertex
    float3 nrm : NORMAL;
    float4 vcol: VCOLOR;        // slot0 : 頂点の色
    float4 w0  : INSTANCE0;     // slot1 : per-instance (world 行0〜3)
    float4 w1  : INSTANCE1;
    float4 w2  : INSTANCE2;
    float4 w3  : INSTANCE3;
    float4 col : COLOR0;        // slot1 : インスタンスの色 (乗算, a = 不透明度)
};

struct VSOut
{
    float4 pos : SV_POSITION;
    float3 nrm : NORMAL;
    float4 col : COLOR0;
    float  lit : TEXCOORD0;     // ライティングを使うか (0..1)
};

VSOut VSMain(VSIn i)
{
    float4x4 world = float4x4(i.w0, i.w1, i.w2, i.w3);
    VSOut o;
    o.pos = mul(mul(float4(i.pos, 1.0), world), viewProj);
    o.nrm = mul(i.nrm, (float3x3)world);   // 等方スケール前提
    o.col = i.col * i.vcol;
    o.lit = params.x;
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float shade = saturate(dot(normalize(i.nrm), normalize(kLightDir))) * kDiffuseWeight + kAmbient;
    float d = lerp(1.0, shade, i.lit);      // lit=0 なら明るさをそのまま (UI)
    return float4(i.col.rgb * d, i.col.a);
}

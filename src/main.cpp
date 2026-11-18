#include "App.h"
#include <algorithm>

#pragma comment(lib, "user32.lib")

// ウィンドウプロシージャ: ウィンドウが閉じられたらループを終わらせる (Esc はゲーム側でポーズ/戻るに使う)
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hi;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"UnrailedDX11";
    RegisterClassW(&wc);

    // 描画領域が kWindowWidth x kWindowHeight になるよう、枠の分を足してウィンドウの大きさを決める
    RECT rc = { 0, 0, (LONG)cfg::kWindowWidth, (LONG)cfg::kWindowHeight };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Unrailed風 (DirectX11 Instancing)",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                              rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hi, nullptr);

    App app;
    if (!app.Init(hwnd, cfg::kWindowWidth, cfg::kWindowHeight)) {   // ① ロード
        MessageBoxW(hwnd, app.LastError().c_str(), L"Load Error", MB_OK | MB_ICONERROR);
        return -1;
    }

    // フレーム間の経過時間 (dt) を高精度タイマーで計る
    LARGE_INTEGER freq, prev;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prev);

    MSG msg = {};
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {          // メッセージがあれば先に処理
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float realDt = (float)(now.QuadPart - prev.QuadPart) / (float)freq.QuadPart;   // 実際の経過時間 (FPS 計測用)
        prev = now;
        const float dt = std::min(realDt, cfg::kMaxFrameDt);        // 長い停止の後でも一気に進まないよう上限をかける

        app.Update(dt, realDt);                                     // シーンごとの更新
        app.Render();                                               // ② 描画
    }
    return 0;
}

// VS のプロジェクトがコンソール(/SUBSYSTEM:CONSOLE)でも動くようにエントリを用意
int main()
{
    return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_SHOWDEFAULT);
}

// OrbitClick.cpp - Windows Autoclicker
// Compile (MinGW): g++ OrbitClick.cpp -o OrbitClick.exe -luser32 -lgdi32 -lshell32 -lwinmm -mwindows -std=c++17 -O2
// Compile (MSVC):  cl OrbitClick.cpp /std:c++17 /O2 /link user32.lib gdi32.lib shell32.lib winmm.lib /SUBSYSTEM:WINDOWS

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <mmsystem.h>
#include <string>
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include <vector>
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winmm.lib")

// ─── Orbit Color Palette ──────────────────────────────────────────────────────
static const COLORREF C_BG     = RGB(10,  10,  10 );
static const COLORREF C_CARD   = RGB(17,  17,  17 );
static const COLORREF C_BORDER = RGB(31,  31,  31 );
static const COLORREF C_T1     = RGB(224, 224, 224);
static const COLORREF C_T2     = RGB(90,  90,  90 );
static const COLORREF C_WHITE  = RGB(255, 255, 255);
static const COLORREF C_BLACK  = RGB(0,   0,   0  );
static const COLORREF C_GREEN  = RGB(74,  222, 128);
static const COLORREF C_RED    = RGB(239, 68,  68 );
static const COLORREF C_BLUE   = RGB(59,  130, 246);

// ─── Layout ───────────────────────────────────────────────────────────────────
static const int WIN_W       = 400;
static const int WIN_H       = 658;
static const int PAD         = 20;
static const int Y_STATUS    = 64;
static const int Y_SPEED     = 152;
static const int Y_CTYPE     = 264;
static const int Y_POS       = 356;
static const int Y_SETTINGS  = 448;
static const int Y_BTN       = 582;
static const int SLIDER_X    = PAD + 70;
static const int SLIDER_W    = WIN_W - PAD - PAD - 70 - 52; // VARY slider
// CPS edit box sits on the right of the CPS row
static const int CPS_EDIT_X  = WIN_W - PAD - 94;
static const int CPS_EDIT_Y  = Y_SPEED + 32;
static const int CPS_EDIT_W  = 90;
static const int CPS_EDIT_H  = 20;
static const int CPS_SLIDER_W = CPS_EDIT_X - SLIDER_X - 6;

// ─── IDs ──────────────────────────────────────────────────────────────────────
static const UINT WM_TRAY    = WM_APP + 1;
static const UINT WM_STOPPED = WM_APP + 2;
static const int  HOTKEY_ID  = 1;
static const UINT TIMER_UI   = 1;
static const UINT IDM_SHOW   = 1001;
static const UINT IDM_EXIT   = 1002;
static const int  IDC_CPS    = 100;

// ─── App State ────────────────────────────────────────────────────────────────
// FIX: All fields read by the click thread are std::atomic to prevent data races.
struct State {
    std::atomic<bool> running   { false };
    std::atomic<bool> active    { false };

    // Speed — atomic so click thread reads are safe
    std::atomic<int>  cps       { 10 };    // 1–1,000,000
    std::atomic<int>  randomize { 0  };    // 0–50 %

    // Click type: 0=Left 1=Right 2=Middle 3=Double
    std::atomic<int>  clickType { 0 };

    // Position: 0=Cursor 1=Fixed 2=Jitter
    std::atomic<int>  posMode   { 0 };
    std::atomic<int>  fixedX    { 960 };
    std::atomic<int>  fixedY    { 540 };
    std::atomic<int>  jitter    { 5  };

    // Settings
    std::atomic<int>  clickLimit { 0 };    // 0=unlimited
    UINT  hotkey      = VK_F6;            // UI thread only
    bool  alwaysOnTop = false;            // UI thread only

    // Stats
    std::atomic<long long> clickCount { 0 };

    // UI-only interaction flags (main thread only, no atomic needed)
    bool settingHotkey = false;
    bool draggingCPS   = false;
    bool draggingVary  = false;
} g;

// ─── Globals ──────────────────────────────────────────────────────────────────
static HWND           g_hwnd      = nullptr;
static HWND           g_cpsEdit   = nullptr;
static HBRUSH         g_editBrush = nullptr;
static HFONT          g_fLarge    = nullptr;
static HFONT          g_fMed      = nullptr;
static HFONT          g_fSmall    = nullptr;
static NOTIFYICONDATA g_nid       = {};
static bool           g_tray      = false;
static std::thread    g_thread;

// ─── Click Thread ─────────────────────────────────────────────────────────────
static void ClickLoop() {
    timeBeginPeriod(1); // raise Windows timer resolution to 1 ms

    std::mt19937 rng(std::random_device{}());
    using Clock = std::chrono::high_resolution_clock;

    while (g.running) {
        if (!g.active) { Sleep(10); continue; }

        auto t0 = Clock::now();

        // Snapshot settings atomically for this click
        int  cps       = g.cps.load(std::memory_order_relaxed);
        int  variance  = g.randomize.load(std::memory_order_relaxed);
        int  ctype     = g.clickType.load(std::memory_order_relaxed);
        int  posMode   = g.posMode.load(std::memory_order_relaxed);

        // Interval with optional variance
        double base = (double)cps;
        double var  = base * (variance / 100.0);
        std::uniform_real_distribution<double> dist(-var, var);
        double actualCps  = std::max(1.0, base + dist(rng));
        double intervalUs = 1'000'000.0 / actualCps;

        // Position
        if (posMode == 1) {
            SetCursorPos(g.fixedX.load(), g.fixedY.load());
        } else if (posMode == 2) {
            POINT cur; GetCursorPos(&cur);
            int r = g.jitter.load(std::memory_order_relaxed);
            std::uniform_int_distribution<int> jit(-r, r);
            SetCursorPos(cur.x + jit(rng), cur.y + jit(rng));
        }

        // Build and fire click
        INPUT inp[4] = {};
        int   cnt = 0;
        DWORD dn, up;
        switch (ctype) {
            case 1:  dn = MOUSEEVENTF_RIGHTDOWN;  up = MOUSEEVENTF_RIGHTUP;  break;
            case 2:  dn = MOUSEEVENTF_MIDDLEDOWN; up = MOUSEEVENTF_MIDDLEUP; break;
            default: dn = MOUSEEVENTF_LEFTDOWN;   up = MOUSEEVENTF_LEFTUP;   break;
        }
        inp[cnt].type = INPUT_MOUSE; inp[cnt++].mi.dwFlags = dn;
        inp[cnt].type = INPUT_MOUSE; inp[cnt++].mi.dwFlags = up;
        if (ctype == 3) { // double click = two down/up pairs
            inp[cnt].type = INPUT_MOUSE; inp[cnt++].mi.dwFlags = dn;
            inp[cnt].type = INPUT_MOUSE; inp[cnt++].mi.dwFlags = up;
        }
        SendInput(cnt, inp, sizeof(INPUT));
        g.clickCount++;

        // Check limit
        int lim = g.clickLimit.load(std::memory_order_relaxed);
        if (lim > 0 && g.clickCount >= lim) {
            g.active = false;
            PostMessage(g_hwnd, WM_STOPPED, 0, 0);
        }

        // Precision sleep: coarse Sleep() + spin-wait for the last 2ms.
        // This breaks the 60-CPS ceiling from Windows' default 15ms timer granularity.
        auto deadline   = t0 + std::chrono::microseconds((long long)intervalUs);
        auto sleepUntil = deadline - std::chrono::milliseconds(2);
        auto now        = Clock::now();
        if (now < sleepUntil) {
            DWORD ms = (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(sleepUntil - now).count();
            if (ms > 0) Sleep(ms);
        }
        while (Clock::now() < deadline) {} // spin-wait
    }

    timeEndPeriod(1);
}

// ─── GDI Helpers ──────────────────────────────────────────────────────────────
static void FillRR(HDC dc, RECT r, int rad, COLORREF c) {
    HBRUSH br  = CreateSolidBrush(c);
    HPEN   pen = CreatePen(PS_SOLID, 0, c);
    HBRUSH ob  = (HBRUSH)SelectObject(dc, br);
    HPEN   op  = (HPEN)  SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void BorderRR(HDC dc, RECT r, int rad, COLORREF fill, COLORREF border) {
    // Fill pass
    HBRUSH br  = CreateSolidBrush(fill);
    HPEN   pen = CreatePen(PS_SOLID, 0, fill);
    HBRUSH ob  = (HBRUSH)SelectObject(dc, br);
    HPEN   op  = (HPEN)  SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, ob); DeleteObject(br);
    SelectObject(dc, op); DeleteObject(pen);
    // Border pass
    HPEN bp  = CreatePen(PS_SOLID, 1, border);
    HPEN obp = (HPEN)SelectObject(dc, bp);
    HBRUSH onb = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad, rad);
    SelectObject(dc, obp); DeleteObject(bp);
    SelectObject(dc, onb);
}

static void Txt(HDC dc, const wchar_t* s, RECT r, COLORREF c, HFONT f,
                UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HFONT of = (HFONT)SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, fmt);
    SelectObject(dc, of);
}

static void DrawSlider(HDC dc, int x, int y, int w, int val, int lo, int hi, COLORREF accent) {
    const int h    = 22;
    const int trkH = 4;
    int trkY  = y + (h - trkH) / 2;
    float pct = (float)(val - lo) / (float)(hi - lo);
    if (pct < 0.f) pct = 0.f;
    if (pct > 1.f) pct = 1.f;
    int fillW = (int)(w * pct);

    RECT track = { x, trkY, x + w, trkY + trkH };
    FillRR(dc, track, 2, C_BORDER);
    if (fillW > 1) {
        RECT fill = { x, trkY, x + fillW, trkY + trkH };
        FillRR(dc, fill, 2, accent);
    }
    int tx = x + fillW, tr = 6;
    RECT thumb = { tx-tr, y+h/2-tr, tx+tr, y+h/2+tr };
    FillRR(dc, thumb, tr, C_WHITE);
}

static void DrawSegBtn(HDC dc, int x, int y, int w, int h,
                       const std::vector<const wchar_t*>& labels, int sel) {
    int n = (int)labels.size();
    int bw = w / n;
    for (int i = 0; i < n; i++) {
        RECT r = { x+i*bw, y, x+(i+1)*bw, y+h };
        bool s = (i == sel);
        BorderRR(dc, r, 6, s ? C_WHITE : C_CARD, s ? C_WHITE : C_BORDER);
        Txt(dc, labels[i], r, s ? C_BLACK : C_T2, g_fSmall, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
}

static void DrawToggle(HDC dc, int x, int y, bool on) {
    RECT r = { x, y, x+40, y+20 };
    BorderRR(dc, r, 10, on ? C_WHITE : C_CARD, on ? C_WHITE : C_BORDER);
    int kx = on ? x+22 : x+2;
    RECT k = { kx, y+2, kx+16, y+18 };
    FillRR(dc, k, 8, on ? C_BLACK : C_T2);
}

// ─── Paint ────────────────────────────────────────────────────────────────────
static void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    HDC     mem    = CreateCompatibleDC(hdc);
    HBITMAP bmp    = CreateCompatibleBitmap(hdc, WIN_W, WIN_H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    RECT full = { 0, 0, WIN_W, WIN_H };
    FillRR(mem, full, 12, C_BG);

    // ── Header ────────────────────────────────────────────────────────────────
    {
        RECT nr = { PAD, 16, 200, 48 };
        Txt(mem, L"ORBITCLICK", nr, C_WHITE, g_fLarge);
        RECT vr = { 157, 22, 230, 44 };
        Txt(mem, L"v1.0", vr, C_T2, g_fSmall);
        RECT cr = { WIN_W-44, 12, WIN_W-12, 44 };
        Txt(mem, L"\u2715", cr, C_T2, g_fMed, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        RECT mr = { WIN_W-84, 12, WIN_W-52, 44 };
        Txt(mem, L"\u2212", mr, C_T2, g_fMed, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        HPEN dp = CreatePen(PS_SOLID, 1, C_BORDER);
        HPEN op = (HPEN)SelectObject(mem, dp);
        MoveToEx(mem, PAD, 56, nullptr); LineTo(mem, WIN_W-PAD, 56);
        SelectObject(mem, op); DeleteObject(dp);
    }

    // ── Status ────────────────────────────────────────────────────────────────
    {
        int y = Y_STATUS;
        RECT card = { PAD, y, WIN_W-PAD, y+76 };
        BorderRR(mem, card, 10, C_CARD, C_BORDER);

        bool     active = g.active.load();
        COLORREF sc     = active ? C_GREEN : C_T2;
        RECT dot = { PAD+16, y+32, PAD+26, y+42 };
        FillRR(mem, dot, 5, sc);
        RECT sr = { PAD+34, y+12, PAD+200, y+50 };
        Txt(mem, active ? L"ACTIVE" : L"INACTIVE", sr, sc, g_fLarge);

        std::wstring cs = std::to_wstring(g.clickCount.load()) + L" clicks";
        RECT cr = { WIN_W-PAD-170, y+12, WIN_W-PAD-4, y+50 };
        Txt(mem, cs.c_str(), cr, C_T2, g_fSmall, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);

        wchar_t kn[32] = {};
        GetKeyNameTextW((LONG)(MapVirtualKey(g.hotkey, MAPVK_VK_TO_VSC) << 16), kn, 32);
        std::wstring hint = std::wstring(L"Toggle: ") + kn;
        RECT hr = { PAD+34, y+50, WIN_W-PAD, y+72 };
        Txt(mem, hint.c_str(), hr, C_T2, g_fSmall);
    }

    // ── Speed ─────────────────────────────────────────────────────────────────
    {
        int y = Y_SPEED;
        RECT card = { PAD, y, WIN_W-PAD, y+100 };
        BorderRR(mem, card, 10, C_CARD, C_BORDER);
        RECT lbl = { PAD+16, y+8, 200, y+26 };
        Txt(mem, L"SPEED", lbl, C_T2, g_fSmall);

        // CPS row — slider (visual 1–1000) + edit box for exact value
        RECT cl = { PAD+16, y+30, PAD+68, y+52 };
        Txt(mem, L"CPS", cl, C_T1, g_fSmall);
        int sliderCps = g.cps.load(); if (sliderCps > 1000) sliderCps = 1000;
        DrawSlider(mem, SLIDER_X, y+30, CPS_SLIDER_W, sliderCps, 1, 1000, C_WHITE);
        // Background behind the child edit control
        RECT eb = { CPS_EDIT_X-1, CPS_EDIT_Y-1, CPS_EDIT_X+CPS_EDIT_W+1, CPS_EDIT_Y+CPS_EDIT_H+1 };
        BorderRR(mem, eb, 4, C_BG, C_BORDER);

        // VARY row
        RECT vl = { PAD+16, y+62, PAD+68, y+84 };
        Txt(mem, L"VARY", vl, C_T1, g_fSmall);
        std::wstring vv = std::to_wstring(g.randomize.load()) + L"%";
        RECT vvr = { WIN_W-PAD-46, y+62, WIN_W-PAD-4, y+84 };
        Txt(mem, vv.c_str(), vvr, C_WHITE, g_fMed, DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        DrawSlider(mem, SLIDER_X, y+62, SLIDER_W, g.randomize.load(), 0, 50, C_BLUE);
    }

    // ── Click Type ────────────────────────────────────────────────────────────
    {
        int y = Y_CTYPE;
        RECT card = { PAD, y, WIN_W-PAD, y+80 };
        BorderRR(mem, card, 10, C_CARD, C_BORDER);
        RECT lbl = { PAD+16, y+8, 200, y+26 };
        Txt(mem, L"CLICK TYPE", lbl, C_T2, g_fSmall);
        DrawSegBtn(mem, PAD+16, y+36, WIN_W-PAD-PAD-32, 30,
            { L"LEFT", L"RIGHT", L"MIDDLE", L"DOUBLE" }, g.clickType.load());
    }

    // ── Position ──────────────────────────────────────────────────────────────
    {
        int y = Y_POS;
        RECT card = { PAD, y, WIN_W-PAD, y+80 };
        BorderRR(mem, card, 10, C_CARD, C_BORDER);
        RECT lbl = { PAD+16, y+8, 200, y+26 };
        Txt(mem, L"POSITION", lbl, C_T2, g_fSmall);
        DrawSegBtn(mem, PAD+16, y+36, WIN_W-PAD-PAD-32, 30,
            { L"CURSOR", L"FIXED", L"JITTER" }, g.posMode.load());
    }

    // ── Settings ──────────────────────────────────────────────────────────────
    {
        int y = Y_SETTINGS;
        RECT card = { PAD, y, WIN_W-PAD, y+122 };
        BorderRR(mem, card, 10, C_CARD, C_BORDER);
        RECT lbl = { PAD+16, y+8, 200, y+26 };
        Txt(mem, L"SETTINGS", lbl, C_T2, g_fSmall);

        // Hotkey
        RECT hl = { PAD+16, y+32, PAD+120, y+54 };
        Txt(mem, L"Hotkey", hl, C_T1, g_fSmall);
        wchar_t kn[32] = {};
        GetKeyNameTextW((LONG)(MapVirtualKey(g.hotkey, MAPVK_VK_TO_VSC) << 16), kn, 32);
        const wchar_t* hkDisplay = g.settingHotkey ? L"[ press key... ]" : kn;
        COLORREF hkCol = g.settingHotkey ? C_BLUE : C_WHITE;
        RECT hkBtn = { WIN_W-PAD-130, y+34, WIN_W-PAD-4, y+52 };
        BorderRR(mem, hkBtn, 6, C_BG, g.settingHotkey ? C_BLUE : C_BORDER);
        Txt(mem, hkDisplay, hkBtn, hkCol, g_fSmall, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        // Click limit
        RECT ll = { PAD+16, y+60, PAD+120, y+82 };
        Txt(mem, L"Click limit", ll, C_T1, g_fSmall);
        int lim = g.clickLimit.load();
        std::wstring limStr = (lim == 0) ? L"\u221e" : std::to_wstring(lim);
        RECT limBtn = { WIN_W-PAD-130, y+62, WIN_W-PAD-4, y+80 };
        BorderRR(mem, limBtn, 6, C_BG, C_BORDER);
        Txt(mem, limStr.c_str(), limBtn, C_WHITE, g_fMed, DT_CENTER|DT_VCENTER|DT_SINGLELINE);

        // Always on top
        RECT aotL = { PAD+16, y+90, PAD+180, y+112 };
        Txt(mem, L"Always on top", aotL, C_T1, g_fSmall);
        DrawToggle(mem, WIN_W-PAD-44, y+93, g.alwaysOnTop);
    }

    // ── Start / Stop ──────────────────────────────────────────────────────────
    {
        int y = Y_BTN;
        bool     active = g.active.load();
        COLORREF bg     = active ? C_RED   : C_WHITE;
        COLORREF tc     = active ? C_WHITE : C_BLACK;
        RECT btn = { PAD, y, WIN_W-PAD, y+50 };
        BorderRR(mem, btn, 10, bg, bg);
        Txt(mem, active ? L"STOP" : L"START", btn, tc, g_fMed, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }

    // Window border
    {
        HPEN bp  = CreatePen(PS_SOLID, 1, C_BORDER);
        HPEN obp = (HPEN)SelectObject(mem, bp);
        HBRUSH onb = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
        RoundRect(mem, 0, 0, WIN_W, WIN_H, 12, 12);
        SelectObject(mem, obp); DeleteObject(bp);
        SelectObject(mem, onb);
    }

    BitBlt(hdc, 0, 0, WIN_W, WIN_H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ─── Helpers ──────────────────────────────────────────────────────────────────
static int SliderValue(int mx, int trackX, int trackW, int lo, int hi) {
    float pct = (float)(mx - trackX) / (float)trackW;
    if (pct < 0.f) pct = 0.f;
    if (pct > 1.f) pct = 1.f;
    return lo + (int)(pct * (hi - lo) + 0.5f);
}

static void AddTray(HWND hwnd) {
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = 1;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);
    wcsncpy(g_nid.szTip, L"OrbitClick", 127);
    Shell_NotifyIcon(NIM_ADD, &g_nid);
    g_tray = true;
}
static void RemoveTray() {
    if (g_tray) { Shell_NotifyIcon(NIM_DELETE, &g_nid); g_tray = false; }
}

// FIX: Unregister the current hotkey before entering capture mode so the old
// hotkey doesn't fire (WM_HOTKEY) while the user is pressing the new one.
static void BeginHotkeyCapture(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_ID);
    g.settingHotkey = true;
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void CommitHotkey(HWND hwnd, UINT vk) {
    g.hotkey        = vk;
    g.settingHotkey = false;
    RegisterHotKey(hwnd, HOTKEY_ID, 0, vk);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void CancelHotkeyCapture(HWND hwnd) {
    g.settingHotkey = false;
    RegisterHotKey(hwnd, HOTKEY_ID, 0, g.hotkey); // re-register existing key
    InvalidateRect(hwnd, nullptr, FALSE);
}

// ─── Window Procedure ─────────────────────────────────────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_CREATE: {
        g.running   = true;
        g_editBrush = CreateSolidBrush(C_BG);
        g_thread    = std::thread(ClickLoop);
        RegisterHotKey(hwnd, HOTKEY_ID, 0, g.hotkey);
        SetTimer(hwnd, TIMER_UI, 200, nullptr);
        AddTray(hwnd);

        g_cpsEdit = CreateWindowExW(0, L"EDIT", L"10",
            WS_CHILD | WS_VISIBLE | ES_RIGHT | ES_NUMBER,
            CPS_EDIT_X, CPS_EDIT_Y, CPS_EDIT_W, CPS_EDIT_H,
            hwnd, (HMENU)(INT_PTR)IDC_CPS,
            ((LPCREATESTRUCT)lp)->hInstance, nullptr);
        SendMessage(g_cpsEdit, EM_SETLIMITTEXT, 7, 0);
        SendMessage(g_cpsEdit, WM_SETFONT, (WPARAM)g_fMed, TRUE);
        return 0;
    }

    case WM_DESTROY:
        g.running = false;
        g.active  = false;
        if (g_thread.joinable()) g_thread.join();
        UnregisterHotKey(hwnd, HOTKEY_ID);
        KillTimer(hwnd, TIMER_UI);
        RemoveTray();
        DeleteObject(g_editBrush);
        PostQuitMessage(0);
        return 0;

    case WM_TIMER:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_PAINT:
        OnPaint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_HOTKEY:
        if ((int)wp == HOTKEY_ID) {
            g.active = !g.active;
            if (g.active) g.clickCount = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_STOPPED:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        if (g.settingHotkey) {
            if (wp == VK_ESCAPE) CancelHotkeyCapture(hwnd);
            else                 CommitHotkey(hwnd, (UINT)wp);
        }
        return 0;

    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (pt.y < 56 && pt.x < WIN_W - 90) return HTCAPTION;
        return HTCLIENT;
    }

    case WM_LBUTTONDOWN: {
        int mx = GET_X_LPARAM(lp);
        int my = GET_Y_LPARAM(lp);

        // Cancel hotkey capture if user clicks elsewhere
        if (g.settingHotkey) { CancelHotkeyCapture(hwnd); return 0; }

        // Header: close / minimize
        if (my >= 12 && my <= 44) {
            if (mx >= WIN_W-44) { DestroyWindow(hwnd); return 0; }
            if (mx >= WIN_W-84) { ShowWindow(hwnd, SW_MINIMIZE); return 0; }
        }

        // Start/Stop
        if (mx >= PAD && mx <= WIN_W-PAD && my >= Y_BTN && my <= Y_BTN+50) {
            g.active = !g.active;
            if (g.active) g.clickCount = 0;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // CPS slider
        if (mx >= SLIDER_X && mx <= SLIDER_X+CPS_SLIDER_W &&
            my >= Y_SPEED+30 && my <= Y_SPEED+52) {
            g.cps = SliderValue(mx, SLIDER_X, CPS_SLIDER_W, 1, 1000);
            SetWindowTextW(g_cpsEdit, std::to_wstring(g.cps.load()).c_str());
            g.draggingCPS = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // VARY slider
        if (mx >= SLIDER_X && mx <= SLIDER_X+SLIDER_W &&
            my >= Y_SPEED+62 && my <= Y_SPEED+84) {
            g.randomize = SliderValue(mx, SLIDER_X, SLIDER_W, 0, 50);
            g.draggingVary = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Click type buttons
        if (my >= Y_CTYPE+36 && my <= Y_CTYPE+66) {
            int bw = (WIN_W-PAD-PAD-32) / 4, bx = PAD+16;
            for (int i = 0; i < 4; i++)
                if (mx >= bx+i*bw && mx < bx+(i+1)*bw)
                    { g.clickType = i; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        }

        // Position buttons
        if (my >= Y_POS+36 && my <= Y_POS+66) {
            int bw = (WIN_W-PAD-PAD-32) / 3, bx = PAD+16;
            for (int i = 0; i < 3; i++) {
                if (mx >= bx+i*bw && mx < bx+(i+1)*bw) {
                    if (i == 1) { // fixed — capture current cursor pos
                        POINT cur; GetCursorPos(&cur);
                        g.fixedX = cur.x; g.fixedY = cur.y;
                    }
                    g.posMode = i;
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
        }

        // Hotkey button — FIX: unregisters old key before listening for new one
        if (mx >= WIN_W-PAD-130 && mx <= WIN_W-PAD-4 &&
            my >= Y_SETTINGS+34 && my <= Y_SETTINGS+52) {
            BeginHotkeyCapture(hwnd);
            return 0;
        }

        // Limit button — cycle presets
        if (mx >= WIN_W-PAD-130 && mx <= WIN_W-PAD-4 &&
            my >= Y_SETTINGS+62 && my <= Y_SETTINGS+80) {
            static const int limits[] = { 0, 100, 500, 1000, 5000 };
            int cur = g.clickLimit.load();
            for (int i = 0; i < 5; i++)
                if (cur == limits[i]) { g.clickLimit = limits[(i+1)%5]; break; }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        // Always on top toggle
        if (mx >= WIN_W-PAD-44 && mx <= WIN_W-PAD-4 &&
            my >= Y_SETTINGS+93 && my <= Y_SETTINGS+113) {
            g.alwaysOnTop = !g.alwaysOnTop;
            SetWindowPos(hwnd, g.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0, SWP_NOMOVE|SWP_NOSIZE);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        int mx = GET_X_LPARAM(lp);
        if (g.draggingCPS) {
            g.cps = SliderValue(mx, SLIDER_X, CPS_SLIDER_W, 1, 1000);
            SetWindowTextW(g_cpsEdit, std::to_wstring(g.cps.load()).c_str());
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (g.draggingVary) {
            g.randomize = SliderValue(mx, SLIDER_X, SLIDER_W, 0, 50);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        g.draggingCPS = g.draggingVary = false;
        ReleaseCapture();
        return 0;

    case WM_CTLCOLOREDIT:
        if ((HWND)lp == g_cpsEdit) {
            SetTextColor((HDC)wp, C_WHITE);
            SetBkColor((HDC)wp, C_BG);
            return (LRESULT)g_editBrush;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);

    case WM_COMMAND:
        if (LOWORD(wp) == IDM_EXIT)  { DestroyWindow(hwnd); return 0; }
        if (LOWORD(wp) == IDM_SHOW)  { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); return 0; }

        // FIX: Only parse CPS on EN_KILLFOCUS (not EN_UPDATE) so that clearing
        // the field mid-type doesn't snap the value to 1.
        if (LOWORD(wp) == IDC_CPS && HIWORD(wp) == EN_KILLFOCUS) {
            wchar_t buf[16] = {};
            GetWindowTextW(g_cpsEdit, buf, 16);
            int val = _wtoi(buf);
            if (val < 1)       val = 1;
            if (val > 1000000) val = 1000000;
            g.cps = val;
            SetWindowTextW(g_cpsEdit, std::to_wstring(val).c_str());
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_TRAY:
        if (lp == WM_RBUTTONUP) {
            POINT pt; GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_SHOW, L"Show OrbitClick");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        } else if (lp == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
        }
        return 0;

    case WM_SIZE:
        if (wp == SIZE_MINIMIZED) ShowWindow(hwnd, SW_HIDE);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─── Entry Point ──────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"OrbitClick_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"OrbitClick is already running.", L"OrbitClick",
                    MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    // Fonts must be created before CreateWindowExW since WM_CREATE uses them
    g_fLarge = CreateFontW(20, 0,0,0, FW_BOLD,     FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fMed   = CreateFontW(15, 0,0,0, FW_SEMIBOLD, FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fSmall = CreateFontW(13, 0,0,0, FW_NORMAL,   FALSE,FALSE,FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    WNDCLASSEXW wc  = {};
    wc.cbSize       = sizeof(wc);
    wc.style        = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc  = WndProc;
    wc.hInstance    = hInst;
    wc.hCursor      = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground= (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName= L"OrbitClick";
    RegisterClassExW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    g_hwnd = CreateWindowExW(WS_EX_LAYERED, L"OrbitClick", L"OrbitClick",
        WS_POPUP | WS_VISIBLE,
        (sx-WIN_W)/2, (sy-WIN_H)/2, WIN_W, WIN_H,
        nullptr, nullptr, hInst, nullptr);

    HRGN rgn = CreateRoundRectRgn(0, 0, WIN_W+1, WIN_H+1, 12, 12);
    SetWindowRgn(g_hwnd, rgn, TRUE);
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteObject(g_fLarge);
    DeleteObject(g_fMed);
    DeleteObject(g_fSmall);
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return (int)msg.wParam;
}

// Workpaper PDF: a fast PDF app for audit workpapers.
// Build 1: viewer (tabs, continuous scroll, zoom, thumbnails, go to page).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdio>
#include <string>
#include <vector>
#include <ole2.h>
#include <set>
#include "convert.h"
#include "pdf.h"
#include "pdfops.h"
#include "annots.h"
#include "marks.h"
#include "fpdf_annot.h"
#include "fpdf_text.h"
#include "fpdf_edit.h"
#include "textedit.h"
#include "find.h"
#include <shlobj.h>
#include <functional>
#include "resource.h"
#include "theme.h"
#include <dwmapi.h>
#include <uxtheme.h>

#define APP_NAME L"Workpaper PDF"
#define APP_VERSION L"0.8.0 (build 8)"
static const wchar_t* kMainClass = L"WorkpaperPDFMain";
static const wchar_t* kViewClass = L"WorkpaperPDFView";
static const wchar_t* kThumbClass = L"WorkpaperPDFThumbs";
static const wchar_t* kSideClass = L"WorkpaperPDFSide";
static const wchar_t* kToolClass = L"WorkpaperPDFToolbar";
static const wchar_t* kTabsClass = L"WorkpaperPDFTabs";
static const wchar_t* kStatusClass = L"WorkpaperPDFStatus";
static const wchar_t* kRegKey = L"Software\\WorkpaperPDF";

enum {
    ID_OPEN = 100, ID_CLOSETAB, ID_EXIT, ID_ZOOMIN, ID_ZOOMOUT, ID_ZOOM100, ID_FITWIDTH, ID_FITPAGE,
    ID_THUMBS, ID_GOTO, ID_NEXTTAB, ID_PREVTAB, ID_PREVPAGE, ID_NEXTPAGE, ID_FIRSTPAGE, ID_LASTPAGE,
    ID_ABOUT, ID_SHORTCUTS, ID_SAVE, ID_SAVEAS, ID_UNDO, ID_REDO, ID_ROTR, ID_ROTL, ID_DUP, ID_DEL,
    ID_INSERTFILE, ID_INSERTBLANK, ID_EXTRACT, ID_SELECTALL, ID_MARKSPANE, ID_TOOL_HAND, ID_TOOL_SELECT,
    ID_TOOL_TICK, ID_TOOL_NUMBER, ID_TOOL_LETTER, ID_TOOL_HIGHLIGHT, ID_TOOL_NOTE, ID_TOOL_BOX, ID_TOOL_OVAL, ID_TOOL_PEN, ID_TOOL_ADDTEXT, ID_TOOL_EDITTEXT,
    ID_AUTOINC, ID_DELANNOT, ID_FIND, ID_FINDNEXT, ID_FINDPREV, ID_PRINT, ID_PRINTCLEAN, ID_SIDE_PAGES, ID_SIDE_OUTLINE, ID_SIDE_COMMENTS, ID_GUIDE, ID_DEFAULTAPP, ID_EDITLINE, ID_EDITPARA, ID_MENU, ID_THEME_SYS, ID_THEME_LIGHT, ID_THEME_DARK, ID_ZOOMMENU, ID_TOOL_PREPARED, ID_TOOL_REVIEWED, ID_SIGNNAME, ID_ZOOMPRESET,
    ID_ZOOMPRESET_LAST = ID_ZOOMPRESET + 20, ID_PAGEEDIT = 300, ID_TABS = 301, ID_STATUS = 302, ID_FINDEDIT = 303, ID_NAVLIST = 304,
};
#define WM_APP_RENDER (WM_APP + 1)
#define WM_APP_CLOSETAB (WM_APP + 2)

// ---------------------------------------------------------------- state

struct Cached {
    HBITMAP bmp = nullptr;
    double scale = 0;
    RECT r = {};  // region of the page (page pixels at `scale`) held in bmp
};

// One undo step: either a snapshot of the whole document, or an annotation that was added
// (undone by removing it, redone by running `make` again).
struct Tab;
struct Step {
    std::shared_ptr<std::vector<uint8_t>> snap;
    int page = -1;
    std::string nm;
    std::function<bool(Tab*)> make;
};

struct Tab {
    PdfDoc pdf;
    double zoom = 1.0;
    int fit = 1;  // 0 = manual zoom, 1 = fit width, 2 = fit page
    int sx = 0, sy = 0;
    double scale = 0;  // pixels per point at the current layout
    std::vector<int> top, pw, ph;
    int docW = 0, docH = 0;
    std::vector<Cached> cache;
    std::vector<HBITMAP> thumb;
    std::vector<int> thumbTop, thumbH;
    int thumbW = 0, thumbTotal = 0, thumbScroll = 0;
    double openMs = 0;
    bool dirty = false;
    std::vector<Step> undo, redo;
    std::set<int> sel;  // selected page thumbnails
    int anchor = -1;    // for shift-click ranges
    int gen = 1;        // bumps on every change, for auto-recovery
    int recGen = 0;     // generation last written to the recovery folder
    std::wstring recId; // name of this tab's recovery copy

    ~Tab() {
        for (auto& c : cache) if (c.bmp) DeleteObject(c.bmp);
        for (auto t : thumb) if (t) DeleteObject(t);
    }
};

static HINSTANCE g_inst;
static HWND g_main, g_tool, g_tabs, g_thumbs, g_view, g_status, g_pageEdit, g_findEdit, g_side, g_navList;
static std::vector<std::unique_ptr<Tab>> g_docs;
static int g_cur = -1;
static bool g_showThumbs = true;
static bool g_renderPosted = false;
static HFONT g_font, g_iconFont, g_bigFont, g_smallFont, g_headFont, g_titleFont, g_btnFont, g_logoFont;
static std::wstring g_iconFace = L"Segoe MDL2 Assets";
static Palette g_c = LightPalette();
static bool g_dark = false;
static int g_themePref = 0, g_themeOverride = -1;  // 0 follow Windows, 1 light, 2 dark
static HBRUSH g_inputBrush, g_surfaceBrush;
static HMENU g_menu;
static std::vector<std::wstring> g_recent;
static UINT g_dpi = 96;
static HACCEL g_accel;
static bool g_dragging = false;
static POINT g_dragStart;
static int g_dragSX, g_dragSY;

static int S(int v) { return MulDiv(v, (int)g_dpi, 96); }
static Tab* Cur() { return (g_cur >= 0 && g_cur < (int)g_docs.size()) ? g_docs[g_cur].get() : nullptr; }

static const int kGap = 12;
static const double kMinZoom = 0.1, kMaxZoom = 8.0;
static const long long kFullPageLimit = 12LL * 1000 * 1000;  // px; above this render only the visible part

// ---------------------------------------------------------------- registry helpers

static void RegSetDword(const wchar_t* sub, const std::wstring& name, DWORD v) {
    HKEY k;
    std::wstring key = std::wstring(kRegKey) + (sub ? std::wstring(L"\\") + sub : L"");
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == 0) {
        RegSetValueExW(k, name.c_str(), 0, REG_DWORD, (BYTE*)&v, sizeof(v));
        RegCloseKey(k);
    }
}
static bool RegGetDword(const wchar_t* sub, const std::wstring& name, DWORD& v) {
    std::wstring key = std::wstring(kRegKey) + (sub ? std::wstring(L"\\") + sub : L"");
    DWORD sz = sizeof(v);
    return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name.c_str(), RRF_RT_REG_DWORD, nullptr, &v, &sz) == 0;
}

// Recently opened files, newest first, for the start screen.
static void LoadRecent() {
    g_recent.clear();
    std::wstring key = std::wstring(kRegKey) + L"\\Recent";
    for (int i = 0; i < 12; i++) {
        wchar_t buf[MAX_PATH * 4];
        DWORD sz = sizeof(buf);
        if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), std::to_wstring(i).c_str(), RRF_RT_REG_SZ, nullptr, buf, &sz) == 0 && buf[0])
            g_recent.push_back(buf);
    }
}
static void SaveRecent() {
    std::wstring key = std::wstring(kRegKey) + L"\\Recent";
    RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str());
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != 0) return;
    for (size_t i = 0; i < g_recent.size(); i++)
        RegSetValueExW(k, std::to_wstring(i).c_str(), 0, REG_SZ, (const BYTE*)g_recent[i].c_str(), (DWORD)((g_recent[i].size() + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}
static void AddRecent(const std::wstring& path) {
    if (path.empty()) return;
    g_recent.erase(std::remove_if(g_recent.begin(), g_recent.end(), [&](const std::wstring& r) { return _wcsicmp(r.c_str(), path.c_str()) == 0; }),
                   g_recent.end());
    g_recent.insert(g_recent.begin(), path);
    if (g_recent.size() > 12) g_recent.resize(12);
    SaveRecent();
}

// ---------------------------------------------------------------- theme

static bool SystemUsesDark() {
    DWORD v = 1, sz = sizeof(v);
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &v, &sz);
    return v == 0;
}

// Dark menus and scroll bars use uxtheme entry points that Windows exports by number only.
static void UxDarkMode(HWND w, bool dark) {
    static HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    typedef int(WINAPI * SetMode)(int);
    typedef BOOL(WINAPI * AllowWnd)(HWND, BOOL);
    typedef void(WINAPI * Flush)();
    static auto setMode = (SetMode)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    static auto allow = (AllowWnd)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    static auto flush = (Flush)GetProcAddress(ux, MAKEINTRESOURCEA(136));
    OSVERSIONINFOW ov = {sizeof(ov)};
    // Only on Windows 10 1903 and later, where these numbers mean what we expect.
    typedef LONG(WINAPI * RtlVer)(OSVERSIONINFOW*);
    static auto rtl = (RtlVer)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    if (rtl) rtl(&ov);
    if (ov.dwBuildNumber < 18362) return;
    if (!w) {
        if (setMode) setMode(dark ? 2 : 3);
        if (flush) flush();
    } else if (allow) {
        allow(w, dark);
    }
}

static void ApplyTheme() {
    int pref = g_themeOverride >= 0 ? g_themeOverride : g_themePref;
    g_dark = pref == 2 || (pref == 0 && SystemUsesDark());
    g_c = g_dark ? DarkPalette() : LightPalette();
    if (g_inputBrush) DeleteObject(g_inputBrush);
    if (g_surfaceBrush) DeleteObject(g_surfaceBrush);
    g_inputBrush = CreateSolidBrush(g_c.input);
    g_surfaceBrush = CreateSolidBrush(g_c.surface);
    UxDarkMode(nullptr, g_dark);
    if (!g_main) return;
    BOOL d = g_dark;
    DwmSetWindowAttribute(g_main, 20, &d, sizeof(d));  // dark title bar
    // Windows 11: colour the title bar like the toolbar so the top of the window reads as one piece.
    COLORREF cap = g_c.surface, txt = g_c.text, brd = g_c.border;
    DwmSetWindowAttribute(g_main, 35, &cap, sizeof(cap));
    DwmSetWindowAttribute(g_main, 36, &txt, sizeof(txt));
    DwmSetWindowAttribute(g_main, 34, &brd, sizeof(brd));
    for (HWND w : {g_view, g_navList, g_thumbs}) {
        if (!w) continue;
        UxDarkMode(w, g_dark);
        SetWindowTheme(w, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        SendMessageW(w, WM_THEMECHANGED, 0, 0);
    }
    RedrawWindow(g_main, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

// ---------------------------------------------------------------- layout

static RECT ViewClient() {
    RECT rc;
    GetClientRect(g_view, &rc);
    return rc;
}

static void Layout(Tab* t) {
    RECT rc = ViewClient();
    int cw = std::max<int>(rc.right - 2 * kGap, 50), ch = std::max<int>(rc.bottom - 2 * kGap, 50);
    double k = g_dpi / 72.0;
    float maxw = 1, maxh = 1;
    for (int i = 0; i < t->pdf.PageCount(); i++) {
        maxw = std::max(maxw, t->pdf.Size(i).w);
        maxh = std::max(maxh, t->pdf.Size(i).h);
    }
    if (t->fit == 1) t->zoom = cw / (maxw * k);
    else if (t->fit == 2) t->zoom = std::min(cw / (maxw * k), ch / (t->pdf.Size(0).h * k));
    t->zoom = std::clamp(t->zoom, kMinZoom, kMaxZoom);
    t->scale = t->zoom * k;
    int n = t->pdf.PageCount();
    t->top.resize(n); t->pw.resize(n); t->ph.resize(n);
    t->cache.resize(n);
    int y = kGap, w = 0;
    for (int i = 0; i < n; i++) {
        t->pw[i] = std::max(1, (int)lround(t->pdf.Size(i).w * t->scale));
        t->ph[i] = std::max(1, (int)lround(t->pdf.Size(i).h * t->scale));
        t->top[i] = y;
        y += t->ph[i] + kGap;
        w = std::max(w, t->pw[i]);
    }
    t->docW = w + 2 * kGap;
    t->docH = y;
}

static void LayoutThumbs(Tab* t) {
    RECT rc;
    GetClientRect(g_thumbs, &rc);
    int n = t->pdf.PageCount();
    t->thumbW = std::max(40, (int)rc.right - S(48));
    t->thumb.resize(n, nullptr);
    t->thumbTop.resize(n);
    t->thumbH.resize(n);
    int y = S(10);
    for (int i = 0; i < n; i++) {
        int h = std::max(1, (int)lround(t->thumbW * t->pdf.Size(i).h / t->pdf.Size(i).w));
        if (h > t->thumbW * 2) h = t->thumbW * 2;
        if (t->thumb[i]) {
            BITMAP b;
            GetObject(t->thumb[i], sizeof(b), &b);
            if (b.bmWidth != t->thumbW) { DeleteObject(t->thumb[i]); t->thumb[i] = nullptr; }
        }
        t->thumbH[i] = h;
        t->thumbTop[i] = y;
        y += h + S(30);
    }
    t->thumbTotal = y;
}

static int PageX(Tab* t, int i) {
    RECT rc = ViewClient();
    int span = std::max<int>(rc.right, t->docW);
    return (span - t->pw[i]) / 2 - t->sx;
}

static void ClampScroll(Tab* t) {
    RECT rc = ViewClient();
    t->sx = std::clamp(t->sx, 0, std::max(0, t->docW - (int)rc.right));
    t->sy = std::clamp(t->sy, 0, std::max(0, t->docH - (int)rc.bottom));
}

static int FirstVisible(Tab* t, int y) {
    auto it = std::upper_bound(t->top.begin(), t->top.end(), y);
    int i = (int)(it - t->top.begin()) - 1;
    return std::max(0, i);
}

static int CurrentPage(Tab* t) {
    RECT rc = ViewClient();
    return FirstVisible(t, t->sy + rc.bottom / 3);
}

// ---------------------------------------------------------------- UI updates

static void UpdateScrollBars() {
    Tab* t = Cur();
    RECT rc = ViewClient();
    SCROLLINFO si = {sizeof(si), t ? (UINT)(SIF_ALL | SIF_DISABLENOSCROLL) : (UINT)SIF_ALL};
    si.nMin = 0;
    si.nMax = t ? t->docH - 1 : 0;
    si.nPage = rc.bottom;
    si.nPos = t ? t->sy : 0;
    SetScrollInfo(g_view, SB_VERT, &si, TRUE);
    SCROLLINFO sh = {sizeof(sh), SIF_ALL};
    sh.nMin = 0;
    sh.nMax = t ? t->docW - 1 : 0;
    sh.nPage = rc.right;
    sh.nPos = t ? t->sx : 0;
    SetScrollInfo(g_view, SB_HORZ, &sh, TRUE);
}

static int g_lastShownPage = -1;

static void EnsureThumbVisible(Tab* t, int page) {
    if (!g_showThumbs || t->thumbTop.empty()) return;
    RECT rc;
    GetClientRect(g_thumbs, &rc);
    int top = t->thumbTop[page] - S(6), bot = t->thumbTop[page] + t->thumbH[page] + S(24);
    if (top < t->thumbScroll) t->thumbScroll = top;
    else if (bot > t->thumbScroll + rc.bottom) t->thumbScroll = bot - rc.bottom;
    t->thumbScroll = std::clamp(t->thumbScroll, 0, std::max(0, t->thumbTotal - (int)rc.bottom));
}

static void UpdateStatus() {
    Tab* t = Cur();
    wchar_t buf[512];
    if (!t) {
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)L"  Drop PDF files here, or press Ctrl+O to open one.");
        for (int i = 1; i < 4; i++) SendMessageW(g_status, SB_SETTEXTW, i, (LPARAM)L"");
        SetWindowTextW(g_pageEdit, L"");
        SetWindowTextW(g_main, APP_NAME);
        InvalidateRect(g_tool, nullptr, FALSE);
        return;
    }
    int page = CurrentPage(t);
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(L"  " + (t->pdf.Path().empty() ? t->pdf.Title() : t->pdf.Path())).c_str());
    swprintf(buf, 512, L"Page %d of %d", page + 1, t->pdf.PageCount());
    SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)buf);
    swprintf(buf, 512, L"%d%%", (int)lround(t->zoom * 100));
    SendMessageW(g_status, SB_SETTEXTW, 2, (LPARAM)buf);
    swprintf(buf, 512, t->openMs > 0 ? L"Opened in %.0f ms" : L"", t->openMs);
    SendMessageW(g_status, SB_SETTEXTW, 3, (LPARAM)buf);
    if (GetFocus() != g_pageEdit) {
        swprintf(buf, 512, L"%d", page + 1);
        SetWindowTextW(g_pageEdit, buf);
    }
    SetWindowTextW(g_main, (t->pdf.Title() + (t->dirty ? L" *" : L"") + L" - " APP_NAME).c_str());
    InvalidateRect(g_tool, nullptr, FALSE);
    if (page != g_lastShownPage) {
        g_lastShownPage = page;
        EnsureThumbVisible(t, page);
        InvalidateRect(g_thumbs, nullptr, FALSE);
    }
    InvalidateRect(g_tool, nullptr, FALSE);
}

static void RequestRender() {
    if (!g_renderPosted) {
        g_renderPosted = true;
        PostMessageW(g_view, WM_APP_RENDER, 0, 0);
    }
}

static void Refresh(bool relayout) {
    Tab* t = Cur();
    if (t && relayout) Layout(t);
    if (t) ClampScroll(t);
    UpdateScrollBars();
    InvalidateRect(g_view, nullptr, FALSE);
    UpdateStatus();
}

// ---------------------------------------------------------------- rendering

// The part of page i (in page pixels) that should be held in the cache right now.
static RECT NeededRegion(Tab* t, int i) {
    RECT full = {0, 0, t->pw[i], t->ph[i]};
    if ((long long)t->pw[i] * t->ph[i] <= kFullPageLimit) return full;
    RECT rc = ViewClient();
    int px = PageX(t, i), py = t->top[i] - t->sy;
    RECT vis = {-px - rc.right / 2, -py - rc.bottom / 2, -px + rc.right * 3 / 2, -py + rc.bottom * 3 / 2};
    RECT out;
    IntersectRect(&out, &full, &vis);
    return out;
}

static bool Covers(const Cached& c, double scale, const RECT& need) {
    if (!c.bmp || c.scale != scale) return false;
    return need.left >= c.r.left && need.top >= c.r.top && need.right <= c.r.right && need.bottom <= c.r.bottom;
}

static void VisibleRange(Tab* t, int& first, int& last) {
    RECT rc = ViewClient();
    first = FirstVisible(t, t->sy);
    last = FirstVisible(t, t->sy + rc.bottom);
}

static bool RenderOnePage(Tab* t) {
    int first, last;
    VisibleRange(t, first, last);
    // Visible pages first, then one page either side for smooth scrolling.
    int order[64];
    int n = 0;
    for (int i = first; i <= last && n < 60; i++) order[n++] = i;
    if (last + 1 < t->pdf.PageCount()) order[n++] = last + 1;
    if (first - 1 >= 0) order[n++] = first - 1;
    RECT rc = ViewClient();
    for (int k = 0; k < n; k++) {
        int i = order[k];
        RECT need = NeededRegion(t, i);
        if (IsRectEmpty(&need)) continue;
        // For an off-screen neighbour only the full page case is prefetched.
        if (Covers(t->cache[i], t->scale, need)) continue;
        RECT r = need;
        if ((long long)t->pw[i] * t->ph[i] > kFullPageLimit) {
            // Render a little more than needed so small scrolls stay sharp.
            InflateRect(&r, rc.right / 2, rc.bottom / 2);
            RECT full = {0, 0, t->pw[i], t->ph[i]};
            IntersectRect(&r, &r, &full);
        }
        HBITMAP b = t->pdf.Render(i, t->pw[i], t->ph[i], r.left, r.top, r.right - r.left, r.bottom - r.top);
        if (t->cache[i].bmp) DeleteObject(t->cache[i].bmp);
        t->cache[i].bmp = b;
        t->cache[i].scale = t->scale;
        t->cache[i].r = r;
        // Drop pages far from view to keep memory low.
        for (int j = 0; j < (int)t->cache.size(); j++) {
            if ((j < first - 4 || j > last + 4) && t->cache[j].bmp) {
                DeleteObject(t->cache[j].bmp);
                t->cache[j].bmp = nullptr;
            }
        }
        if (i >= first && i <= last) {
            RECT pr = {PageX(t, i), t->top[i] - t->sy, PageX(t, i) + t->pw[i], t->top[i] - t->sy + t->ph[i]};
            InvalidateRect(g_view, &pr, FALSE);
        }
        return true;
    }
    return false;
}

static bool RenderOneThumb(Tab* t) {
    if (!g_showThumbs || t->thumbTop.empty()) return false;
    RECT rc;
    GetClientRect(g_thumbs, &rc);
    for (int i = 0; i < t->pdf.PageCount(); i++) {
        int y = t->thumbTop[i] - t->thumbScroll;
        if (y + t->thumbH[i] < 0 || y > rc.bottom) continue;
        if (t->thumb[i]) continue;
        t->thumb[i] = t->pdf.Render(i, t->thumbW, t->thumbH[i], 0, 0, t->thumbW, t->thumbH[i]);
        RECT r = {0, y, rc.right, y + t->thumbH[i]};
        InvalidateRect(g_thumbs, &r, FALSE);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- tabs

static void SaveLastPage(Tab* t) {
    if (t && t->pdf.PageCount() > 0 && !t->pdf.Untitled()) RegSetDword(L"LastPage", t->pdf.Path(), (DWORD)CurrentPage(t));
}

static void GoToPage(Tab* t, int page) {
    if (!t) return;
    page = std::clamp(page, 0, t->pdf.PageCount() - 1);
    t->sy = t->top[page] - kGap;
    Refresh(false);
}

// ---------------------------------------------------------------- tab strip (custom drawn; answers the TCM_ messages we use)

static struct {
    std::vector<std::wstring> titles;
    std::vector<bool> dirty;
    std::vector<RECT> rc, closeRc;
    RECT plus{};
    int cur = -1, hot = -1, hotClose = -1;
    bool hotPlus = false;
} g_tb;

static void LayoutTabs(HWND h) {
    RECT rc;
    GetClientRect(h, &rc);
    HDC dc = GetDC(h);
    HGDIOBJ of = SelectObject(dc, g_font);
    int n = (int)g_tb.titles.size(), x = S(8), top = S(5), bottom = rc.bottom - S(5);
    std::vector<int> w(n);
    int total = 0;
    for (int i = 0; i < n; i++) {
        SIZE sz;
        GetTextExtentPoint32W(dc, g_tb.titles[i].c_str(), (int)g_tb.titles[i].size(), &sz);
        w[i] = std::clamp((int)sz.cx + S(76), S(130), S(250));
        total += w[i] + S(4);
    }
    int avail = rc.right - S(56);
    if (total > avail && n) for (auto& v : w) v = std::max(S(96), avail / n - S(4));
    g_tb.rc.assign(n, RECT{});
    g_tb.closeRc.assign(n, RECT{});
    for (int i = 0; i < n; i++) {
        g_tb.rc[i] = {x, top, x + w[i], bottom};
        int c = (top + bottom) / 2;
        g_tb.closeRc[i] = {x + w[i] - S(30), c - S(11), x + w[i] - S(8), c + S(11)};
        x += w[i] + S(4);
    }
    int c = (top + bottom) / 2;
    g_tb.plus = {x + S(2), c - S(14), x + S(30), c + S(14)};
    SelectObject(dc, of);
    ReleaseDC(h, dc);
}

static LRESULT CALLBACK TabsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case TCM_DELETEALLITEMS:
        g_tb.titles.clear();
        g_tb.dirty.clear();
        LayoutTabs(h);
        InvalidateRect(h, nullptr, FALSE);
        return TRUE;
    case TCM_INSERTITEMW: {
        TCITEMW* it = (TCITEMW*)l;
        std::wstring t = it && it->pszText ? it->pszText : L"";
        bool dirty = t.size() > 2 && t.compare(t.size() - 2, 2, L" *") == 0;
        if (dirty) t.resize(t.size() - 2);
        int at = std::clamp((int)w, 0, (int)g_tb.titles.size());
        g_tb.titles.insert(g_tb.titles.begin() + at, t);
        g_tb.dirty.insert(g_tb.dirty.begin() + at, dirty);
        LayoutTabs(h);
        InvalidateRect(h, nullptr, FALSE);
        return at;
    }
    case TCM_SETCURSEL: { int old = g_tb.cur; g_tb.cur = (int)w; InvalidateRect(h, nullptr, FALSE); return old; }
    case TCM_GETCURSEL: return g_tb.cur;
    case TCM_GETITEMCOUNT: return (LRESULT)g_tb.titles.size();
    case TCM_GETITEMRECT:
        if ((int)w < 0 || (int)w >= (int)g_tb.rc.size()) return FALSE;
        *(RECT*)l = g_tb.rc[w];
        return TRUE;
    case TCM_HITTEST: {
        TCHITTESTINFO* hi = (TCHITTESTINFO*)l;
        for (int i = 0; i < (int)g_tb.rc.size(); i++) if (PtInRect(&g_tb.rc[i], hi->pt)) return i;
        return -1;
    }
    case WM_SIZE: LayoutTabs(h); InvalidateRect(h, nullptr, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC buf = CreateCompatibleDC(hdc);
        HBITMAP bb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(buf, bb);
        FillSolid(buf, rc, g_c.chrome);
        FillSolid(buf, RECT{0, rc.bottom - 1, rc.right, rc.bottom}, g_c.border);
        SetBkMode(buf, TRANSPARENT);
        for (int i = 0; i < (int)g_tb.rc.size(); i++) {
            RECT r = g_tb.rc[i];
            bool cur = i == g_tb.cur, hot = i == g_tb.hot;
            if (cur) RoundRectAA(buf, r, (float)S(8), g_c.surface, g_c.border);
            else if (hot) RoundRectAA(buf, r, (float)S(8), g_c.hover);
            RECT ic = {r.left + S(10), r.top, r.left + S(30), r.bottom};
            SelectObject(buf, g_iconFont);
            SetTextColor(buf, cur ? g_c.accent : g_c.muted);
            DrawTextW(buf, L"\xE8A5", -1, &ic, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            RECT tr = {r.left + S(36), r.top, g_tb.closeRc[i].left - S(4), r.bottom};
            SelectObject(buf, g_font);
            SetTextColor(buf, cur ? g_c.text : g_c.text2);
            DrawTextW(buf, g_tb.titles[i].c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            RECT cr = g_tb.closeRc[i];
            if (g_tb.dirty[i] && !(hot || cur)) {
                // Unsaved: a dot where the close button sits.
                int cx = (cr.left + cr.right) / 2, cy = (cr.top + cr.bottom) / 2;
                EllipseAA(buf, RECT{cx - S(4), cy - S(4), cx + S(4), cy + S(4)}, g_c.accent);
            } else if (hot || cur) {
                if (i == g_tb.hotClose) EllipseAA(buf, cr, g_c.pressed);
                SelectObject(buf, g_smallFont);
                SetTextColor(buf, g_tb.dirty[i] && i != g_tb.hotClose ? g_c.accent : g_c.text2);
                DrawTextW(buf, g_tb.dirty[i] && i != g_tb.hotClose ? L"\x25CF" : L"\x2715", -1, &cr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
        }
        if (!g_tb.titles.empty()) {
            if (g_tb.hotPlus) RoundRectAA(buf, g_tb.plus, (float)S(8), g_c.hover);
            SelectObject(buf, g_iconFont);
            SetTextColor(buf, g_c.text2);
            DrawTextW(buf, L"\xE710", -1, &g_tb.plus, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
        SelectObject(buf, ob);
        DeleteObject(bb);
        DeleteDC(buf);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int hot = -1, hc = -1;
        for (int i = 0; i < (int)g_tb.rc.size(); i++)
            if (PtInRect(&g_tb.rc[i], p)) { hot = i; if (PtInRect(&g_tb.closeRc[i], p)) hc = i; }
        bool hp = PtInRect(&g_tb.plus, p) != 0;
        if (hot != g_tb.hot || hc != g_tb.hotClose || hp != g_tb.hotPlus) {
            g_tb.hot = hot;
            g_tb.hotClose = hc;
            g_tb.hotPlus = hp;
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_tb.hot = g_tb.hotClose = -1;
        g_tb.hotPlus = false;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (PtInRect(&g_tb.plus, p)) { SendMessageW(g_main, WM_COMMAND, ID_OPEN, 0); return 0; }
        for (int i = 0; i < (int)g_tb.rc.size(); i++) {
            if (!PtInRect(&g_tb.rc[i], p)) continue;
            if (PtInRect(&g_tb.closeRc[i], p)) { PostMessageW(g_main, WM_APP_CLOSETAB, i, 0); return 0; }
            if (i != g_tb.cur) {
                g_tb.cur = i;
                NMHDR nm = {h, (UINT_PTR)ID_TABS, (UINT)TCN_SELCHANGE};
                SendMessageW(GetParent(h), WM_NOTIFY, ID_TABS, (LPARAM)&nm);
            }
            return 0;
        }
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- status bar (custom drawn; answers SB_SETTEXT)

static std::wstring g_statusText[4];

static LRESULT CALLBACK StatusProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case SB_SETTEXTW: {
        int i = LOWORD(w) & 0xFF;
        if (i < 0 || i > 3) return FALSE;
        std::wstring t = l ? (const wchar_t*)l : L"";
        size_t a = t.find_first_not_of(L' ');
        t = a == std::wstring::npos ? L"" : t.substr(a);
        if (t != g_statusText[i]) {
            g_statusText[i] = t;
            InvalidateRect(h, nullptr, FALSE);
        }
        return TRUE;
    }
    case SB_SETPARTS: return TRUE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC buf = CreateCompatibleDC(hdc);
        HBITMAP bb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(buf, bb);
        FillSolid(buf, rc, g_c.surface);
        FillSolid(buf, RECT{0, 0, rc.right, 1}, g_c.border);
        SetBkMode(buf, TRANSPARENT);
        SelectObject(buf, g_smallFont);
        // Right side: page, zoom and timing, separated by dots.
        int x = rc.right - S(14);
        for (int i = 3; i >= 1; i--) {
            if (g_statusText[i].empty()) continue;
            SIZE sz;
            GetTextExtentPoint32W(buf, g_statusText[i].c_str(), (int)g_statusText[i].size(), &sz);
            RECT r = {x - sz.cx, 0, x, rc.bottom};
            SetTextColor(buf, i == 1 ? g_c.text2 : g_c.muted);
            DrawTextW(buf, g_statusText[i].c_str(), -1, &r, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
            x -= sz.cx + S(10);
            int cy = rc.bottom / 2;
            if (i > 1) EllipseAA(buf, RECT{x - S(2), cy - S(2), x + S(2), cy + S(2)}, g_c.muted);
            x -= S(10);
        }
        RECT lr = {S(14), 0, x - S(12), rc.bottom};
        SetTextColor(buf, g_c.text2);
        DrawTextW(buf, g_statusText[0].c_str(), -1, &lr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
        SelectObject(buf, ob);
        DeleteObject(bb);
        DeleteDC(buf);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

static void RebuildTabStrip() {
    TabCtrl_DeleteAllItems(g_tabs);
    for (size_t i = 0; i < g_docs.size(); i++) {
        std::wstring title = g_docs[i]->pdf.Title() + (g_docs[i]->dirty ? L" *" : L"");
        TCITEMW it = {};
        it.mask = TCIF_TEXT;
        it.pszText = (LPWSTR)title.c_str();
        TabCtrl_InsertItem(g_tabs, (int)i, &it);
    }
    if (g_cur >= 0) TabCtrl_SetCurSel(g_tabs, g_cur);
}

static void RelayoutMain();
static void NavChanged();

static void SelectTab(int i) {
    if (g_docs.empty()) { g_cur = -1; Refresh(false); InvalidateRect(g_thumbs, nullptr, TRUE); return; }
    g_cur = std::clamp(i, 0, (int)g_docs.size() - 1);
    TabCtrl_SetCurSel(g_tabs, g_cur);
    g_lastShownPage = -1;
    Tab* t = Cur();
    NavChanged();
    Layout(t);
    LayoutThumbs(t);
    Refresh(false);
    InvalidateRect(g_thumbs, nullptr, FALSE);
    RequestRender();
    SetFocus(g_view);
}

static void OpenFile(const std::wstring& path) {
    // Already open? Switch to it.
    for (size_t i = 0; i < g_docs.size(); i++) {
        if (_wcsicmp(g_docs[i]->pdf.Path().c_str(), path.c_str()) == 0) { SelectTab((int)i); return; }
    }
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    auto t = std::make_unique<Tab>();
    std::wstring err;
    if (!t->pdf.Open(path, err)) {
        MessageBoxW(g_main, (path + L"\n\n" + err).c_str(), APP_NAME, MB_ICONWARNING);
        return;
    }
    QueryPerformanceCounter(&b);
    t->openMs = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart;
    g_docs.push_back(std::move(t));
    AddRecent(path);
    bool first = g_docs.size() == 1;
    RebuildTabStrip();
    if (first) RelayoutMain();
    SelectTab((int)g_docs.size() - 1);
    DWORD last;
    Tab* cur = Cur();
    if (RegGetDword(L"LastPage", path, last) && (int)last > 0 && (int)last < cur->pdf.PageCount()) GoToPage(cur, (int)last);
}

// ---------------------------------------------------------------- editing pages

static void SetStatusText(const wchar_t* msg) {
    if (msg) {
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(std::wstring(L"  ") + msg).c_str());
        UpdateWindow(g_status);
        SetCursor(LoadCursor(nullptr, IDC_WAIT));
    } else {
        UpdateStatus();
    }
}

// The pages a page command acts on: the selected thumbnails, or the current page.
static std::vector<int> TargetPages(Tab* t) {
    std::vector<int> v(t->sel.begin(), t->sel.end());
    if (v.empty()) v.push_back(CurrentPage(t));
    return v;
}

static void ClearRenders(Tab* t) {
    for (auto& c : t->cache) { if (c.bmp) DeleteObject(c.bmp); c = Cached(); }
    for (auto& b : t->thumb) { if (b) DeleteObject(b); b = nullptr; }
    t->cache.clear();
    t->thumb.clear();
}

static void SearchChanged();
static void NavChanged();

// Saves an undo point. Call before changing the document.
static bool Checkpoint(Tab* t) {
    auto snap = std::make_shared<std::vector<uint8_t>>();
    if (!ops::SaveToBuffer(t->pdf.Handle(), *snap)) return false;
    Step st;
    st.snap = snap;
    t->undo.push_back(st);
    if (t->undo.size() > 60) t->undo.erase(t->undo.begin());
    t->redo.clear();
    return true;
}

// Call after the document changed: re-reads pages and redraws everything.
static void AfterEdit(Tab* t, int showPage, bool markDirty = true) {
    t->pdf.RefreshSizes();
    ClearRenders(t);
    if (markDirty) { t->dirty = true; t->gen++; }
    SearchChanged();
    NavChanged();
    for (auto it = t->sel.begin(); it != t->sel.end();) it = *it >= t->pdf.PageCount() ? t->sel.erase(it) : std::next(it);
    Layout(t);
    LayoutThumbs(t);
    RebuildTabStrip();
    g_lastShownPage = -1;
    if (showPage >= 0) GoToPage(t, showPage);
    else Refresh(false);
    InvalidateRect(g_thumbs, nullptr, FALSE);
    InvalidateRect(g_view, nullptr, FALSE);
    RequestRender();
}

// Redraws one page (after an annotation on it changed).
static void RefreshPage(Tab* t, int page) {
    if (page < 0 || page >= (int)t->cache.size()) return;
    if (t->cache[page].bmp) { DeleteObject(t->cache[page].bmp); t->cache[page] = Cached(); }
    if (page < (int)t->thumb.size() && t->thumb[page]) { DeleteObject(t->thumb[page]); t->thumb[page] = nullptr; }
    if (!t->dirty) { t->dirty = true; RebuildTabStrip(); UpdateStatus(); }
    t->gen++;
    NavChanged();
    InvalidateRect(g_view, nullptr, FALSE);
    InvalidateRect(g_thumbs, nullptr, FALSE);
    RequestRender();
}

static bool RemoveAnnot(Tab* t, int page, const std::string& nm) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return false;
    bool ok = annots::Remove(p, nm);
    FPDF_ClosePage(p);
    return ok;
}

static void UndoRedo(Tab* t, bool redo) {
    auto& from = redo ? t->redo : t->undo;
    auto& to = redo ? t->undo : t->redo;
    if (from.empty()) return;
    Step st = from.back();
    from.pop_back();
    if (st.snap) {
        Step now;
        now.snap = std::make_shared<std::vector<uint8_t>>();
        if (!ops::SaveToBuffer(t->pdf.Handle(), *now.snap)) return;
        int page = CurrentPage(t);
        std::wstring path = t->pdf.Path(), title = t->pdf.Title();
        if (!t->pdf.OpenMemory(st.snap, path, title)) return;
        to.push_back(now);
        t->sel.clear();
        AfterEdit(t, std::min(page, t->pdf.PageCount() - 1));
        return;
    }
    // An added annotation: undo removes it, redo makes it again.
    if (redo) { if (st.make) st.make(t); }
    else RemoveAnnot(t, st.page, st.nm);
    to.push_back(st);
    RefreshPage(t, st.page);
}

static void RotateSel(Tab* t, int q) {
    if (!t || !Checkpoint(t)) return;
    auto pages = TargetPages(t);
    ops::RotatePages(t->pdf.Handle(), pages, q);
    AfterEdit(t, -1);
}

static void DeleteSel(Tab* t) {
    if (!t) return;
    auto pages = TargetPages(t);
    if ((int)pages.size() >= t->pdf.PageCount()) {
        MessageBoxW(g_main, L"A PDF needs at least one page, so every page can't be deleted.", APP_NAME, MB_ICONINFORMATION);
        return;
    }
    if (!Checkpoint(t)) return;
    ops::DeletePages(t->pdf.Handle(), pages);
    t->sel.clear();
    AfterEdit(t, std::min(pages.front(), t->pdf.PageCount() - 1));
}

static void DuplicateSel(Tab* t) {
    if (!t || !Checkpoint(t)) return;
    auto pages = TargetPages(t);
    int at = ops::DuplicatePages(t->pdf.Handle(), pages);
    if (at < 0) { t->undo.pop_back(); return; }
    t->sel.clear();
    for (size_t k = 0; k < pages.size(); k++) t->sel.insert(at + (int)k);
    AfterEdit(t, at);
}

// Moves the selection so it starts before the page now at `gap` (0..count).
static void MoveSelTo(Tab* t, int gap) {
    auto pages = TargetPages(t);
    int before = 0;
    for (int p : pages) if (p < gap) before++;
    int dest = gap - before;
    bool noop = true;
    for (size_t k = 0; k < pages.size(); k++) if (pages[k] != dest + (int)k) noop = false;
    if (noop || !Checkpoint(t)) return;
    ops::MovePages(t->pdf.Handle(), pages, dest);
    t->sel.clear();
    for (size_t k = 0; k < pages.size(); k++) t->sel.insert(dest + (int)k);
    AfterEdit(t, dest);
}

static void ProgressCb(const wchar_t* msg) { SetStatusText(msg); }

static void InsertFilesAt(Tab* t, int at, const std::vector<std::wstring>& files) {
    if (!t || files.empty() || !Checkpoint(t)) return;
    std::wstring errors;
    SetStatusText(L"Adding pages...");
    int added = InsertFiles(t->pdf.Handle(), at, files, errors, ProgressCb);
    SetStatusText(nullptr);
    if (added == 0) t->undo.pop_back();
    else {
        t->sel.clear();
        for (int k = 0; k < added; k++) t->sel.insert(at + k);
        AfterEdit(t, at);
    }
    if (!errors.empty()) MessageBoxW(g_main, (L"Some files could not be added:\n\n" + errors).c_str(), APP_NAME, MB_ICONWARNING);
}

static void RelayoutMain();
static void SelectTab(int i);

// Makes a new untitled document from non-PDF files dropped with nothing open.
static void NewDocFromFiles(const std::vector<std::wstring>& files) {
    static int counter = 1;
    auto t = std::make_unique<Tab>();
    t->pdf.NewEmpty(L"Untitled " + std::to_wstring(counter++));
    std::wstring errors;
    SetStatusText(L"Adding pages...");
    int added = InsertFiles(t->pdf.Handle(), 0, files, errors, ProgressCb);
    SetStatusText(nullptr);
    if (!errors.empty()) MessageBoxW(g_main, (L"Some files could not be added:\n\n" + errors).c_str(), APP_NAME, MB_ICONWARNING);
    if (added == 0) return;
    t->pdf.RefreshSizes();
    t->dirty = true;
    g_docs.push_back(std::move(t));
    RebuildTabStrip();
    if (g_docs.size() == 1) RelayoutMain();
    SelectTab((int)g_docs.size() - 1);
}

static bool PickSavePath(std::wstring& path, const std::wstring& suggest) {
    wchar_t buf[MAX_PATH * 4] = {};
    wcsncpy(buf, suggest.c_str(), MAX_PATH * 4 - 1);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"PDF files (*.pdf)\0*.pdf\0";
    ofn.lpstrDefExt = L"pdf";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH * 4;
    ofn.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return false;
    path = buf;
    return true;
}

static bool SaveTab(Tab* t, bool saveAs) {
    if (!t) return false;
    std::wstring target = t->pdf.Path();
    if (saveAs || t->pdf.Untitled()) {
        std::wstring suggest = t->pdf.Untitled() ? t->pdf.Title() + L".pdf" : t->pdf.Path();
        if (!PickSavePath(target, suggest)) return false;
    }
    SetStatusText(L"Saving...");
    std::wstring err;
    bool ok = t->pdf.SaveTo(target, err);
    SetStatusText(nullptr);
    if (!ok) { MessageBoxW(g_main, (L"The file was not saved.\n\n" + err).c_str(), APP_NAME, MB_ICONWARNING); return false; }
    t->dirty = false;
    AfterEdit(t, -1, false);
    return true;
}

static void ExtractSel(Tab* t) {
    if (!t) return;
    auto pages = TargetPages(t);
    PdfDoc out;
    out.NewEmpty(L"Extract");
    if (ops::InsertFrom(out.Handle(), t->pdf.Handle(), pages, 0) <= 0) return;
    std::wstring base = t->pdf.Title();
    size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) base = base.substr(0, dot);
    std::wstring path;
    if (!PickSavePath(path, base + L" (pages).pdf")) return;
    std::wstring err;
    if (!out.SaveTo(path, err)) MessageBoxW(g_main, (L"The file was not saved.\n\n" + err).c_str(), APP_NAME, MB_ICONWARNING);
}

// Asks about unsaved changes. Returns false if the user cancelled.
static bool ConfirmClose(Tab* t) {
    if (!t->dirty) return true;
    int r = MessageBoxW(g_main, (L"Save changes to " + t->pdf.Title() + L" before closing?").c_str(), APP_NAME,
                        MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return false;
    if (r == IDYES) return SaveTab(t, false);
    return true;
}

static void InsertFileDialog(Tab* t) {
    if (!t) return;
    std::vector<wchar_t> buf(65536, 0);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"Files that can be added\0*.pdf;*.doc;*.docx;*.rtf;*.odt;*.txt;*.csv;*.png;*.gif;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.lpstrTitle = L"Insert pages from files";
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    std::vector<std::wstring> files;
    std::wstring dir = buf.data();
    const wchar_t* p = buf.data() + dir.size() + 1;
    if (!*p) files.push_back(dir);
    while (*p) { files.push_back(dir + L"\\" + p); p += wcslen(p) + 1; }
    InsertFilesAt(t, CurrentPage(t) + 1, files);
}

static void CommitNote();
static void CommitText(bool cancel);
static void ClearSelection();
static void DropRecovery(Tab* t);
static void ForgetSearch(Tab* t);
static void CloseTab(int i) {
    if (i < 0 || i >= (int)g_docs.size()) return;
    CommitNote();
    ClearSelection();
    if (!ConfirmClose(g_docs[i].get())) return;
    DropRecovery(g_docs[i].get());
    ForgetSearch(g_docs[i].get());
    SaveLastPage(g_docs[i].get());
    g_docs.erase(g_docs.begin() + i);
    RebuildTabStrip();
    if (g_docs.empty()) RelayoutMain();
    SelectTab(std::min(i, (int)g_docs.size() - 1));
}

static bool IsPdf(const std::wstring& p) {
    size_t dot = p.find_last_of(L'.');
    return dot != std::wstring::npos && _wcsicmp(p.c_str() + dot, L".pdf") == 0;
}

static void OpenDialog() {
    std::vector<wchar_t> buf(65536, 0);
    OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.hwndOwner = g_main;
    ofn.lpstrFilter = L"PDF files (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buf.data();
    ofn.nMaxFile = (DWORD)buf.size();
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;
    std::wstring dir = buf.data();
    const wchar_t* p = buf.data() + dir.size() + 1;
    if (!*p) { OpenFile(dir); return; }
    while (*p) {
        OpenFile(dir + L"\\" + p);
        p += wcslen(p) + 1;
    }
}

// ---------------------------------------------------------------- zoom

static void SetZoom(Tab* t, double z, int fit, POINT* anchor) {
    if (!t) return;
    RECT rc = ViewClient();
    POINT a = anchor ? *anchor : POINT{rc.right / 2, rc.bottom / 3};
    // Remember which point of which page sits under the anchor.
    int page = FirstVisible(t, t->sy + a.y);
    double fy = (double)(t->sy + a.y - t->top[page]) / t->ph[page];
    double fx = (double)(a.x - PageX(t, page)) / t->pw[page];
    t->fit = fit;
    t->zoom = std::clamp(z, kMinZoom, kMaxZoom);
    Layout(t);
    t->sy = (int)lround(t->top[page] + fy * t->ph[page] - a.y);
    int span = std::max<int>(rc.right, t->docW);
    int newX = (span - t->pw[page]) / 2;
    t->sx = (int)lround(newX + fx * t->pw[page] - a.x);
    Refresh(false);
    RequestRender();
}

static void ZoomStep(Tab* t, int dir, POINT* anchor) {
    if (!t) return;
    static const double steps[] = {0.1, 0.25, 0.33, 0.5, 0.67, 0.75, 0.9, 1.0, 1.1, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0, 5.0, 6.0, 8.0};
    double z = t->zoom;
    double nz = z;
    if (dir > 0) { for (double s : steps) if (s > z + 0.001) { nz = s; break; } }
    else { for (int i = (int)(sizeof(steps) / sizeof(steps[0])) - 1; i >= 0; i--) if (steps[i] < z - 0.001) { nz = steps[i]; break; } }
    SetZoom(t, nz, 0, anchor);
}

// ---------------------------------------------------------------- view window


// ---------------------------------------------------------------- marking tools

enum Tool { kHand = 0, kSelect, kTick, kHighlight, kNote, kBox, kOval, kPen, kAddText, kEditText, kToolCount };
static const wchar_t* kToolNames[kToolCount] = {L"Hand", L"Select", L"Tickmark", L"Highlight", L"Note", L"Box", L"Oval", L"Pen", L"Add text", L"Edit text"};

struct MarkSettings {
    int tool = kHand;
    int kind = marks::kSymbol;  // symbol, number or letter
    int symbol = 0;
    int color = 0;
    int size = 0;  // Small by default, like the plug-in
    int autoInc = 1;
    int wrap = 0;
    int nextNumber = 1;  // 1..40
    int nextLetter = 0;  // 0..25
    int textFamily = 0;  // Add text: 0 sans, 1 serif, 2 mono
    int textBold = 0;
    int textSize = 10;   // points
    int editPara = 0;    // Edit text picks a line (0) or a whole paragraph (1)
};
static MarkSettings g_ms;
static HWND g_marks;  // the Marks pane on the right
static bool g_showMarks = true;
static const wchar_t* kMarksClass = L"WorkpaperPDFMarks";

static void LoadMarkSettings() {
    DWORD v;
    struct { const wchar_t* name; int* p; } f[] = {{L"Kind", &g_ms.kind}, {L"Symbol", &g_ms.symbol}, {L"Color", &g_ms.color},
        {L"Size", &g_ms.size}, {L"AutoIncrement", &g_ms.autoInc}, {L"Wrap", &g_ms.wrap}, {L"NextNumber", &g_ms.nextNumber},
        {L"NextLetter", &g_ms.nextLetter}, {L"TextFamily", &g_ms.textFamily}, {L"TextBold", &g_ms.textBold},
        {L"TextSize", &g_ms.textSize}, {L"EditParagraph", &g_ms.editPara}};
    for (auto& x : f) if (RegGetDword(L"Marks", x.name, v)) *x.p = (int)v;
    g_ms.textFamily = std::clamp(g_ms.textFamily, 0, 2);
    g_ms.textSize = std::clamp(g_ms.textSize, 6, 36);
    if (RegGetDword(nullptr, L"MarksPane", v)) g_showMarks = v != 0;
    g_ms.kind = std::clamp(g_ms.kind, 0, 4);
    g_ms.symbol = std::clamp(g_ms.symbol, 0, marks::SymbolCount() - 1);
    g_ms.color = std::clamp(g_ms.color, 0, marks::ColorCount() - 1);
    g_ms.size = std::clamp(g_ms.size, 0, 2);
    g_ms.nextNumber = std::clamp(g_ms.nextNumber, 1, 40);
    g_ms.nextLetter = std::clamp(g_ms.nextLetter, 0, 25);
}
static void SaveMarkSettings() {
    struct { const wchar_t* name; int v; } f[] = {{L"Kind", g_ms.kind}, {L"Symbol", g_ms.symbol}, {L"Color", g_ms.color},
        {L"Size", g_ms.size}, {L"AutoIncrement", g_ms.autoInc}, {L"Wrap", g_ms.wrap}, {L"NextNumber", g_ms.nextNumber},
        {L"NextLetter", g_ms.nextLetter}, {L"TextFamily", g_ms.textFamily}, {L"TextBold", g_ms.textBold},
        {L"TextSize", g_ms.textSize}, {L"EditParagraph", g_ms.editPara}};
    for (auto& x : f) RegSetDword(L"Marks", x.name, (DWORD)x.v);
    RegSetDword(nullptr, L"MarksPane", g_showMarks ? 1 : 0);
}

static std::u16string Author() {
    static std::u16string name;
    if (name.empty()) {
        wchar_t buf[256];
        DWORD n = 256;
        if (GetUserNameW(buf, &n)) name.assign(buf, buf + wcslen(buf));
        else name = u"Reviewer";
    }
    return name;
}

static std::u16string ToU16(const std::wstring& w) { return std::u16string(w.begin(), w.end()); }
static std::wstring ToW(const std::u16string& u) { return std::wstring(u.begin(), u.end()); }

// Screen rectangle of page i in view coordinates.
static RECT PageRect(Tab* t, int i) {
    int x = PageX(t, i), y = t->top[i] - t->sy;
    return RECT{x, y, x + t->pw[i], y + t->ph[i]};
}

// Page under a view point, or -1 (a few pixels of slack around each page).
static int PageAt(Tab* t, POINT p) {
    int i = FirstVisible(t, t->sy + p.y);
    for (int k = std::max(0, i - 1); k <= std::min(t->pdf.PageCount() - 1, i + 1); k++) {
        RECT r = PageRect(t, k);
        InflateRect(&r, S(6), S(6));
        if (PtInRect(&r, p)) return k;
    }
    return -1;
}

static void ToPage(Tab* t, FPDF_PAGE page, int i, POINT p, double& x, double& y) {
    RECT r = PageRect(t, i);
    FPDF_DeviceToPage(page, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, p.x, p.y, &x, &y);
}
static POINT ToView(Tab* t, FPDF_PAGE page, int i, double x, double y) {
    RECT r = PageRect(t, i);
    int dx, dy;
    FPDF_PageToDevice(page, r.left, r.top, r.right - r.left, r.bottom - r.top, 0, x, y, &dx, &dy);
    return POINT{dx, dy};
}
static RECT RectToView(Tab* t, FPDF_PAGE page, int i, const annots::Rect& a) {
    POINT p1 = ToView(t, page, i, a.left, a.top), p2 = ToView(t, page, i, a.right, a.bottom);
    return RECT{std::min(p1.x, p2.x), std::min(p1.y, p2.y), std::max(p1.x, p2.x), std::max(p1.y, p2.y)};
}

// In-progress gesture and the selected annotation, drawn over the pages.
static struct {
    bool active = false;
    int page = -1;
    POINT start{}, cur{};
    std::vector<POINT> pen;          // pen stroke in view coordinates
    std::vector<RECT> textRects;     // highlight preview
    // Select tool
    std::string selNm;
    int selPage = -1;
    int selSubtype = 0;
    annots::Rect selRect{};
    bool moving = false, resizing = false, snapTaken = false;
    // Edit text tool: the run of text under the mouse
    RECT hover{};
    bool hoverOn = false;
    POINT hoverAt{-1000, -1000};
} g_g;

static void ClearSelection() {
    g_g.selNm.clear();
    g_g.selPage = -1;
    InvalidateRect(g_view, nullptr, FALSE);
}

static void SetTool(int tool) {
    g_ms.tool = tool;
    if (tool != kSelect) ClearSelection();
    InvalidateRect(g_marks, nullptr, FALSE);
    static const wchar_t* hints[kToolCount] = {
        L"Hand: drag to move around the page.",
        L"Select: click a mark or comment to select it. Drag to move it, drag a corner to resize, Del to delete, double-click a note to edit.",
        L"Tickmark: click to place. Drag to draw it at a custom size. Esc returns to the hand.",
        L"Highlight: drag across text.",
        L"Note: click where the note should go, then type.",
        L"Box: drag to draw a box.",
        L"Oval: drag to draw an oval, for example around an amount.",
        L"Pen: draw freehand. Each stroke is saved as you go.",
        L"Add text: click where the text should start, then type. Enter starts a new line; click away or Ctrl+Enter to finish.",
        L"Edit text: click a line (or a table cell) to retype it. Switch to Paragraph in the Marks pane to retype a whole paragraph. Enter to finish, Esc to cancel.",
    };
    g_g.hoverOn = false;
    std::wstring hint = hints[tool];
    if (tool == kTick && (g_ms.kind == marks::kPrepared || g_ms.kind == marks::kReviewed))
        hint = L"Sign-off: click the page to place the stamp with your name and today's date. Esc returns to the hand.";
    if (Cur()) SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(L"  " + hint).c_str());
}

static void ScrollTo(Tab* t, int x, int y);
static void SelectAnnotByName(Tab* t, int page, const std::string& nm) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return;
    for (auto& info : annots::List(p)) {
        if (info.nm != nm) continue;
        SetTool(kSelect);
        g_g.selNm = nm;
        g_g.selPage = page;
        g_g.selSubtype = info.subtype;
        g_g.selRect = info.rect;
        RECT r = RectToView(t, p, page, info.rect);
        RECT rc = ViewClient();
        if (r.top < 0 || r.bottom > rc.bottom) ScrollTo(t, t->sx, t->sy + r.top - rc.bottom / 3);
        break;
    }
    FPDF_ClosePage(p);
    InvalidateRect(g_view, nullptr, FALSE);
}

static void PushAdd(Tab* t, int page, const std::string& nm, std::function<bool(Tab*)> make) {
    Step st;
    st.page = page;
    st.nm = nm;
    st.make = make;
    t->undo.push_back(st);
    if (t->undo.size() > 60) t->undo.erase(t->undo.begin());
    t->redo.clear();
    RefreshPage(t, page);
}

// Runs `make` (which adds one annotation named nm on `page`) and records it for undo.
static bool AddAnnotation(Tab* t, int page, const std::string& nm, std::function<bool(Tab*)> make) {
    if (!make(t)) return false;
    PushAdd(t, page, nm, make);
    return true;
}

// ---- sign-off stamps (Prepared by / Reviewed by)

static bool IsSignoff(int kind) { return kind == marks::kPrepared || kind == marks::kReviewed; }

// The name or initials printed on sign-off stamps, set once and kept in the registry.
static std::wstring SignName() {
    wchar_t buf[128] = {};
    DWORD sz = sizeof(buf);
    std::wstring key = std::wstring(kRegKey) + L"\\Marks";
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"SignoffName", RRF_RT_REG_SZ, nullptr, buf, &sz) != 0) return L"";
    return buf;
}
static void SetSignName(const std::wstring& name) {
    HKEY k;
    std::wstring key = std::wstring(kRegKey) + L"\\Marks";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == 0) {
        RegSetValueExW(k, L"SignoffName", 0, REG_SZ, (const BYTE*)name.c_str(), (DWORD)((name.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
    }
}

// Today's date in the short format set in Windows (e.g. 11/26/2026).
static std::wstring TodayText() {
    wchar_t buf[64] = {};
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, nullptr, nullptr, buf, 64, nullptr)) return L"";
    return buf;
}

// Stamp text uses the PDF standard font, which covers Latin-1; anything else becomes '?'.
static std::string Latin1(const std::wstring& w) {
    std::string out;
    for (wchar_t c : w) out += (c > 0 && c < 256) ? (char)c : '?';
    return out;
}

// A small "Your name or initials" box. Returns false if cancelled.
static INT_PTR CALLBACK SignNameDlgProc(HWND d, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_INITDIALOG: {
        std::wstring* name = (std::wstring*)l;
        SetWindowLongPtrW(d, DWLP_USER, (LONG_PTR)name);
        SetDlgItemTextW(d, 100, name->c_str());
        SendDlgItemMessageW(d, 100, EM_SETLIMITTEXT, 24, 0);
        SendDlgItemMessageW(d, 100, EM_SETSEL, 0, -1);
        RECT r, pr;
        GetWindowRect(d, &r);
        GetWindowRect(g_main, &pr);
        SetWindowPos(d, nullptr, (pr.left + pr.right - (r.right - r.left)) / 2, (pr.top + pr.bottom - (r.bottom - r.top)) / 3, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        SetFocus(GetDlgItem(d, 100));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK) {
            wchar_t buf[64] = {};
            GetDlgItemTextW(d, 100, buf, 64);
            std::wstring v = buf;
            while (!v.empty() && v.back() == L' ') v.pop_back();
            while (!v.empty() && v.front() == L' ') v.erase(v.begin());
            if (v.empty()) { MessageBeep(MB_ICONWARNING); return TRUE; }
            *(std::wstring*)GetWindowLongPtrW(d, DWLP_USER) = v;
            EndDialog(d, IDOK);
            return TRUE;
        }
        if (LOWORD(w) == IDCANCEL) { EndDialog(d, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

static bool AskSignName() {
    // An in-memory dialog template: prompt, edit box, OK and Cancel.
    std::vector<WORD> t;
    auto align = [&] { if (t.size() % 2) t.push_back(0); };
    auto str = [&](const wchar_t* s) { for (; *s; s++) t.push_back(*s); t.push_back(0); };
    auto dword = [&](DWORD v) { t.push_back(LOWORD(v)); t.push_back(HIWORD(v)); };
    dword(DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU);
    dword(0);
    t.push_back(4);  // items
    t.push_back(0); t.push_back(0); t.push_back(220); t.push_back(84);
    t.push_back(0); t.push_back(0);  // no menu, default class
    str(L"Sign-off name");
    t.push_back(9);
    str(L"Segoe UI");
    auto item = [&](DWORD style, short x, short y, short cx, short cy, WORD id, WORD cls, const wchar_t* text) {
        align();
        dword(style | WS_CHILD | WS_VISIBLE);
        dword(0);
        t.push_back(x); t.push_back(y); t.push_back(cx); t.push_back(cy); t.push_back(id);
        t.push_back(0xFFFF); t.push_back(cls);
        str(text);
        t.push_back(0);
    };
    item(SS_LEFT, 10, 8, 200, 20, 101, 0x0082,
         L"Your name or initials, as it should appear on Prepared by and Reviewed by stamps:");
    item(WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL, 10, 32, 200, 14, 100, 0x0081, L"");
    item(BS_DEFPUSHBUTTON | WS_TABSTOP, 104, 60, 50, 16, IDOK, 0x0080, L"OK");
    item(BS_PUSHBUTTON | WS_TABSTOP, 160, 60, 50, 16, IDCANCEL, 0x0080, L"Cancel");
    std::wstring name = SignName();
    if (name.empty()) {
        // Suggest initials from the Windows user name ("michael.jones" -> "MJ").
        std::wstring u = ToW(Author()), ini;
        bool start = true;
        for (wchar_t c : u) {
            if (iswalpha(c) && start) ini += (wchar_t)towupper(c);
            start = !iswalnum(c);
        }
        name = ini.size() >= 2 ? ini : u;
    }
    if (DialogBoxIndirectParamW(g_inst, (LPCDLGTEMPLATEW)t.data(), g_main, SignNameDlgProc, (LPARAM)&name) != IDOK) return false;
    SetSignName(name);
    InvalidateRect(g_marks, nullptr, FALSE);
    return true;
}

static std::string SignoffText() { return Latin1(SignName()) + "  " + Latin1(TodayText()); }

static std::string CurrentMarkText() {
    if (IsSignoff(g_ms.kind)) return SignoffText();
    if (g_ms.kind == marks::kNumber) return marks::NumberText(g_ms.nextNumber);
    if (g_ms.kind == marks::kLetter) return marks::LetterText(g_ms.nextLetter);
    return "";
}

static void AdvanceCounter() {
    if (!g_ms.autoInc) return;
    if (g_ms.kind == marks::kNumber) g_ms.nextNumber = g_ms.nextNumber >= 40 ? (g_ms.wrap ? 1 : 40) : g_ms.nextNumber + 1;
    if (g_ms.kind == marks::kLetter) g_ms.nextLetter = g_ms.nextLetter >= 25 ? (g_ms.wrap ? 0 : 25) : g_ms.nextLetter + 1;
    SaveMarkSettings();
    InvalidateRect(g_marks, nullptr, FALSE);
}

static void PlaceMark(Tab* t, int page, POINT a, POINT b, bool dragged) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return;
    int rot = FPDFPage_GetRotation(p);
    marks::Kind kind = (marks::Kind)g_ms.kind;
    if (IsSignoff(kind) && SignName().empty() && !AskSignName()) { FPDF_ClosePage(p); return; }
    std::string text = CurrentMarkText();
    double l, bo, r, tp;
    if (dragged) {
        double x1, y1, x2, y2;
        ToPage(t, p, page, a, x1, y1);
        ToPage(t, p, page, b, x2, y2);
        l = std::min(x1, x2); r = std::max(x1, x2); bo = std::min(y1, y2); tp = std::max(y1, y2);
    } else {
        double w, h, cx, cy;
        marks::BaseSize(kind, g_ms.symbol, text, w, h);
        // Sign-offs stay readable at every size: Small is 80%, Large 130%.
        static const double kSignScale[3] = {0.8, 1.0, 1.3};
        double sc = IsSignoff(kind) ? kSignScale[std::clamp(g_ms.size, 0, 2)] : marks::SizeScale(g_ms.size);
        w *= sc; h *= sc;
        if (rot % 2) std::swap(w, h);
        ToPage(t, p, page, a, cx, cy);
        l = cx - w / 2; r = cx + w / 2; bo = cy - h / 2; tp = cy + h / 2;
    }
    FPDF_ClosePage(p);
    std::string nm = annots::NewId("tm");
    int symbol = g_ms.symbol, color = g_ms.color;
    std::u16string author = Author();
    auto make = [=](Tab* tt) {
        FPDF_PAGE pg = FPDF_LoadPage(tt->pdf.Handle(), page);
        if (!pg) return false;
        bool ok = marks::AddMark(tt->pdf.Handle(), pg, kind, symbol, text, color, l, bo, r, tp, rot, author, nm);
        FPDF_ClosePage(pg);
        return ok;
    };
    if (AddAnnotation(t, page, nm, make) && (kind == marks::kNumber || kind == marks::kLetter)) AdvanceCounter();
}

static void ColorOf(int c, uint8_t& r, uint8_t& g, uint8_t& b) { marks::ColorRgb(c, r, g, b); }

// Line rectangles of the text between two view points on a page (page space).
static std::vector<annots::Rect> TextRange(Tab* t, int page, POINT a, POINT b) {
    std::vector<annots::Rect> out;
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return out;
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(p);
    double x1, y1, x2, y2;
    ToPage(t, p, page, a, x1, y1);
    ToPage(t, p, page, b, x2, y2);
    double tol = 6 / t->scale + 2;
    int i1 = FPDFText_GetCharIndexAtPos(tp, x1, y1, tol, tol), i2 = FPDFText_GetCharIndexAtPos(tp, x2, y2, tol, tol);
    if (i1 >= 0 && i2 >= 0) {
        int s0 = std::min(i1, i2), s1 = std::max(i1, i2);
        int n = FPDFText_CountRects(tp, s0, s1 - s0 + 1);
        for (int k = 0; k < n; k++) {
            double l, tt, r, bb;
            if (FPDFText_GetRect(tp, k, &l, &tt, &r, &bb)) out.push_back({(float)l, (float)bb, (float)r, (float)tt});
        }
    }
    FPDFText_ClosePage(tp);
    FPDF_ClosePage(p);
    return out;
}

static void UpdateHighlightPreview(Tab* t) {
    g_g.textRects.clear();
    auto rects = TextRange(t, g_g.page, g_g.start, g_g.cur);
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), g_g.page);
    if (!p) return;
    for (auto& r : rects) g_g.textRects.push_back(RectToView(t, p, g_g.page, r));
    FPDF_ClosePage(p);
}

static void FinishHighlight(Tab* t) {
    auto rects = TextRange(t, g_g.page, g_g.start, g_g.cur);
    if (rects.empty()) {
        // No text there (a scan?): highlight the dragged box instead.
        if (abs(g_g.cur.x - g_g.start.x) < S(4) || abs(g_g.cur.y - g_g.start.y) < S(4)) return;
        FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), g_g.page);
        double x1, y1, x2, y2;
        ToPage(t, p, g_g.page, g_g.start, x1, y1);
        ToPage(t, p, g_g.page, g_g.cur, x2, y2);
        FPDF_ClosePage(p);
        rects.push_back({(float)std::min(x1, x2), (float)std::min(y1, y2), (float)std::max(x1, x2), (float)std::max(y1, y2)});
    }
    std::string nm = annots::NewId("hl");
    int page = g_g.page;
    std::u16string author = Author();
    AddAnnotation(t, page, nm, [=](Tab* tt) {
        FPDF_PAGE pg = FPDF_LoadPage(tt->pdf.Handle(), page);
        if (!pg) return false;
        bool ok = annots::AddHighlight(pg, rects, 255, 226, 0, author, nm);
        FPDF_ClosePage(pg);
        return ok;
    });
}

static void FinishShape(Tab* t, bool oval) {
    if (abs(g_g.cur.x - g_g.start.x) < S(4) && abs(g_g.cur.y - g_g.start.y) < S(4)) return;
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), g_g.page);
    double x1, y1, x2, y2;
    ToPage(t, p, g_g.page, g_g.start, x1, y1);
    ToPage(t, p, g_g.page, g_g.cur, x2, y2);
    FPDF_ClosePage(p);
    annots::Rect rc = {(float)std::min(x1, x2), (float)std::min(y1, y2), (float)std::max(x1, x2), (float)std::max(y1, y2)};
    uint8_t r, g, b;
    ColorOf(g_ms.color, r, g, b);
    std::string nm = annots::NewId(oval ? "oval" : "box");
    int page = g_g.page;
    std::u16string author = Author();
    AddAnnotation(t, page, nm, [=](Tab* tt) {
        FPDF_PAGE pg = FPDF_LoadPage(tt->pdf.Handle(), page);
        if (!pg) return false;
        bool ok = annots::AddShape(pg, oval, rc, r, g, b, 1.5f, author, nm);
        FPDF_ClosePage(pg);
        return ok;
    });
}

static void FinishPen(Tab* t) {
    if (g_g.pen.size() < 2) return;
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), g_g.page);
    std::vector<FS_POINTF> pts;
    for (POINT v : g_g.pen) {
        double x, y;
        ToPage(t, p, g_g.page, v, x, y);
        pts.push_back({(float)x, (float)y});
    }
    FPDF_ClosePage(p);
    uint8_t r, g, b;
    ColorOf(g_ms.color, r, g, b);
    std::string nm = annots::NewId("ink");
    int page = g_g.page;
    std::u16string author = Author();
    AddAnnotation(t, page, nm, [=](Tab* tt) {
        FPDF_PAGE pg = FPDF_LoadPage(tt->pdf.Handle(), page);
        if (!pg) return false;
        bool ok = annots::AddInk(pg, {pts}, r, g, b, 1.8f, author, nm);
        FPDF_ClosePage(pg);
        return ok;
    });
}

// ---- text editor: a box over the page for adding new text or retyping a run of existing text

static HWND g_textEdit;
static HFONT g_textFont;
static HBRUSH g_whiteBrush;
static struct {
    Tab* tab = nullptr;
    int page = -1;
    bool replace = false;     // retyping existing text (else adding new text)
    bool para = false;        // retyping a whole paragraph
    textedit::Line line;      // the run being retyped
    textedit::Paragraph paragraph;
    std::wstring before;
    COLORREF color = 0;
    int ascent = 0;           // pixels from the box top to the first baseline
    int minW = 0, lineH = 0;
} g_te;

static void FitTextEditor() {
    if (!g_textEdit) return;
    if (g_te.para) {
        // Fixed width (the paragraph's), growing downwards as the text wraps.
        int lines = (int)SendMessageW(g_textEdit, EM_GETLINECOUNT, 0, 0);
        SetWindowPos(g_textEdit, nullptr, 0, 0, g_te.minW, std::max(1, lines) * g_te.lineH + S(6), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        return;
    }
    int len = GetWindowTextLengthW(g_textEdit);
    std::wstring text(len, L'\0');
    GetWindowTextW(g_textEdit, &text[0], len + 1);
    HDC dc = GetDC(g_textEdit);
    HGDIOBJ of = SelectObject(dc, g_textFont);
    RECT r = {0, 0, 0, 0};
    std::wstring measure = text.empty() ? L"M" : text;
    if (!measure.empty() && measure.back() == L'\n') measure += L"M";
    DrawTextW(dc, measure.c_str(), -1, &r, DT_CALCRECT | DT_NOPREFIX | DT_EXPANDTABS);
    SelectObject(dc, of);
    ReleaseDC(g_textEdit, dc);
    RECT wr;
    GetWindowRect(g_textEdit, &wr);
    MapWindowPoints(nullptr, g_view, (POINT*)&wr, 2);
    int w = std::max<int>(g_te.minW, r.right + g_te.lineH);
    int h = std::max<int>(g_te.lineH, r.bottom) + S(4);
    SetWindowPos(g_textEdit, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static bool TabAlive(Tab* t) {
    for (auto& d : g_docs) if (d.get() == t) return true;
    return false;
}

static void CommitText(bool cancel) {
    if (!g_textEdit || g_te.page < 0) return;
    int page = g_te.page;
    g_te.page = -1;
    Tab* t = g_te.tab;
    int len = GetWindowTextLengthW(g_textEdit);
    std::wstring text(len, L'\0');
    GetWindowTextW(g_textEdit, &text[0], len + 1);
    RECT wr;
    GetWindowRect(g_textEdit, &wr);
    MapWindowPoints(nullptr, g_view, (POINT*)&wr, 2);
    ShowWindow(g_textEdit, SW_HIDE);
    InvalidateRect(g_view, nullptr, FALSE);
    if (cancel || !TabAlive(t) || page >= t->pdf.PageCount()) return;
    std::wstring clean;
    for (wchar_t c : text) if (c != L'\r') clean.push_back(c);
    if (g_te.replace ? clean == g_te.before : clean.find_first_not_of(L" \n\t") == std::wstring::npos) return;
    if (!Checkpoint(t)) return;
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return;
    std::u16string u = ToU16(clean);
    if (g_te.para) {
        int used = 0;
        textedit::Result r = textedit::ReplaceParagraph(t->pdf.Handle(), p, g_te.paragraph, u, &used);
        FPDF_ClosePage(p);
        if (r == textedit::kFailed) {
            t->undo.pop_back();
            MessageBoxW(g_main, L"This paragraph could not be changed. Try editing it line by line instead.", APP_NAME, MB_ICONINFORMATION);
            return;
        }
        SearchChanged();
        std::wstring msg = r == textedit::kReplaced ? L"  Paragraph changed." : L"  Paragraph changed (old text covered and retyped).";
        if (used > (int)g_te.paragraph.lines.size())
            msg += L" It is now longer than before, so check that it doesn't run into what's below (Ctrl+Z undoes).";
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)msg.c_str());
    } else if (g_te.replace) {
        textedit::Result r = textedit::ReplaceLine(t->pdf.Handle(), p, g_te.line, u);
        FPDF_ClosePage(p);
        if (r == textedit::kFailed) {
            t->undo.pop_back();
            MessageBoxW(g_main, L"This text could not be changed. It may be part of an image (a scan) rather than real text.",
                        APP_NAME, MB_ICONINFORMATION);
            return;
        }
        SearchChanged();
        SendMessageW(g_status, SB_SETTEXTW, 0,
                     (LPARAM)(r == textedit::kReplaced ? L"  Text changed."
                                                      : L"  Text changed. The old text is covered with white and retyped on top."));
    } else {
        // The first baseline sits `ascent` pixels below the box top; the box's left edge is where the text starts.
        POINT base = {wr.left + S(2), wr.top + S(1) + g_te.ascent};
        double x, y;
        ToPage(t, p, page, base, x, y);
        int rot = FPDFPage_GetRotation(p);
        bool ok = textedit::AddText(t->pdf.Handle(), p, u, x, y, g_ms.textSize, g_ms.textFamily, g_ms.textBold != 0, false,
                                    GetRValue(g_te.color), GetGValue(g_te.color), GetBValue(g_te.color), rot);
        FPDF_ClosePage(p);
        if (!ok) {
            t->undo.pop_back();
            return;
        }
    }
    RefreshPage(t, page);
}

static HFONT MakeTextFont(int family, bool bold, bool italic, int px) {
    static const wchar_t* faces[3] = {L"Arial", L"Times New Roman", L"Courier New"};
    return CreateFontW(-std::max(6, px), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic, 0, 0, DEFAULT_CHARSET, 0, 0,
                       CLEARTYPE_QUALITY, 0, faces[std::clamp(family, 0, 2)]);
}

// Shows the editor with its top-left at `at` (view coordinates).
static void ShowTextEditor(Tab* t, int page, POINT at, int minW, const std::wstring& text, bool multiline, bool wrap = false) {
    if (g_textEdit) DestroyWindow(g_textEdit);
    DWORD style = WS_CHILD | (wrap ? ES_MULTILINE | ES_AUTOVSCROLL : ES_AUTOHSCROLL | (multiline ? ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL : 0));
    g_textEdit = CreateWindowExW(0, L"EDIT", L"", style, 0, 0, 0, 0, g_view, nullptr, g_inst, nullptr);
    if (!g_whiteBrush) g_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    SendMessageW(g_textEdit, WM_SETFONT, (WPARAM)g_textFont, FALSE);
    SendMessageW(g_textEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(S(2), S(2)));
    HDC dc = GetDC(g_textEdit);
    HGDIOBJ of = SelectObject(dc, g_textFont);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_textEdit, dc);
    g_te.ascent = tm.tmAscent;
    g_te.lineH = tm.tmHeight;
    g_te.minW = std::max(minW, S(40));
    g_te.tab = t;
    g_te.page = page;
    g_te.before = text;
    std::wstring shown;
    for (wchar_t c : text) { if (c == L'\n') shown += L"\r\n"; else shown.push_back(c); }
    SetWindowTextW(g_textEdit, shown.c_str());
    SetWindowPos(g_textEdit, HWND_TOP, at.x, at.y, g_te.minW, g_te.lineH + S(4), SWP_SHOWWINDOW);
    FitTextEditor();
    SetFocus(g_textEdit);
    SendMessageW(g_textEdit, EM_SETSEL, 0, -1);
}

static void StartAddText(Tab* t, int page, POINT v) {
    uint8_t r, g, b;
    marks::ColorRgb(g_ms.color, r, g, b);
    g_te.color = RGB(r, g, b);
    g_te.replace = false;
    g_te.para = false;
    if (g_textFont) DeleteObject(g_textFont);
    g_textFont = MakeTextFont(g_ms.textFamily, g_ms.textBold != 0, false, (int)lround(g_ms.textSize * t->scale));
    ShowTextEditor(t, page, POINT{v.x - S(2), v.y - S(1)}, S(60), L"", true);
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)L"  Type the text. Enter starts a new line. Click away or press Ctrl+Enter to finish, Esc to cancel.");
}

// Finds the run of text under a view point.
static bool RunAt(Tab* t, int page, POINT v, textedit::Line& ln, RECT& view) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return false;
    double x, y;
    ToPage(t, p, page, v, x, y);
    bool ok = textedit::LineAt(p, x, y, 3 / t->scale + 1, ln);
    if (ok) view = RectToView(t, p, page, annots::Rect{(float)ln.left, (float)ln.bottom, (float)ln.right, (float)ln.top});
    FPDF_ClosePage(p);
    return ok;
}

static bool ParaAt(Tab* t, int page, POINT v, textedit::Paragraph& para, RECT& view) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return false;
    double x, y;
    ToPage(t, p, page, v, x, y);
    bool ok = textedit::ParagraphAt(p, x, y, 3 / t->scale + 1, para);
    if (ok) {
        view = RectToView(t, p, page, annots::Rect{(float)para.left, (float)para.lines.back().bottom, (float)para.right,
                                                    (float)para.lines.front().top});
    }
    FPDF_ClosePage(p);
    return ok;
}

static void StartEditParagraph(Tab* t, int page, POINT v) {
    textedit::Paragraph para;
    RECT vr;
    if (!ParaAt(t, page, v, para, vr)) {
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)L"  There is no text there to edit. If the page is a scan, use Add text instead.");
        return;
    }
    const textedit::Line& ln = para.lines[0];
    g_te.line = ln;
    g_te.paragraph = para;
    g_te.replace = true;
    g_te.para = true;
    g_te.color = ln.invisible ? RGB(0, 0, 0) : RGB(ln.r, ln.g, ln.b);
    if (g_textFont) DeleteObject(g_textFont);
    g_textFont = MakeTextFont(ln.family, ln.bold, ln.italic, (int)lround(ln.size * t->scale));
    // Wrap the editor at the paragraph's width so the lines break about where they will on the page.
    ShowTextEditor(t, page, POINT{vr.left - S(3), vr.top - S(2)}, vr.right - vr.left + S(10), ToW(para.text), true, true);
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)L"  Retype the paragraph, then press Enter. The lines re-wrap to fit. Esc cancels.");
}

static void StartEditText(Tab* t, int page, POINT v) {
    if (g_ms.editPara) { StartEditParagraph(t, page, v); return; }
    g_te.para = false;
    textedit::Line ln;
    RECT vr;
    if (!RunAt(t, page, v, ln, vr)) {
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)L"  There is no text there to edit. If the page is a scan, use Add text instead.");
        return;
    }
    g_te.line = ln;
    g_te.replace = true;
    g_te.color = RGB(ln.r, ln.g, ln.b);
    if (ln.invisible) g_te.color = RGB(0, 0, 0);
    if (g_textFont) DeleteObject(g_textFont);
    // Sideways text is retyped in a level box; the page keeps its direction.
    g_textFont = MakeTextFont(ln.family, ln.bold, ln.italic, (int)lround(ln.size * t->scale));
    std::wstring text = ToW(ln.text);
    HDC dc = GetDC(g_view);
    HGDIOBJ of = SelectObject(dc, g_textFont);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, of);
    ReleaseDC(g_view, dc);
    // Line the editor's baseline up with the text's baseline.
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    POINT base = p ? ToView(t, p, page, ln.ox, ln.oy) : POINT{vr.left, vr.bottom};
    int rot = p ? FPDFPage_GetRotation(p) : 0;
    if (p) FPDF_ClosePage(p);
    double screenAngle = ln.angle - rot * 1.5707963267948966;  // the page turns clockwise on screen
    bool level = std::fabs(std::sin(screenAngle)) < 0.01 && std::cos(screenAngle) > 0;
    POINT at = level ? POINT{base.x - S(2), base.y - S(1) - tm.tmAscent} : POINT{vr.left, vr.top};
    ShowTextEditor(t, page, at, vr.right - vr.left + S(8), text, false);
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)L"  Retype the text, then press Enter. Esc cancels.");
}

static void UpdateEditHover(Tab* t, POINT v) {
    if (abs(v.x - g_g.hoverAt.x) < S(3) && abs(v.y - g_g.hoverAt.y) < S(3)) return;
    g_g.hoverAt = v;
    bool was = g_g.hoverOn;
    RECT old = g_g.hover;
    int page = PageAt(t, v);
    textedit::Line ln;
    textedit::Paragraph para;
    g_g.hoverOn = page >= 0 && (g_ms.editPara ? ParaAt(t, page, v, para, g_g.hover) : RunAt(t, page, v, ln, g_g.hover));
    if (was != g_g.hoverOn || (g_g.hoverOn && !EqualRect(&old, &g_g.hover))) InvalidateRect(g_view, nullptr, FALSE);
}

// ---- sticky note editor: a small yellow text box over the page

static HWND g_noteEdit;
static struct { int page = -1; std::string nm; std::wstring before; Tab* tab = nullptr; } g_noteEd;
static HBRUSH g_noteBrush;

static void CommitNote() {
    CommitText(false);
    if (!g_noteEdit || g_noteEd.page < 0) return;
    Tab* t = g_noteEd.tab;
    int len = GetWindowTextLengthW(g_noteEdit);
    std::wstring text(len, L'\0');
    GetWindowTextW(g_noteEdit, &text[0], len + 1);
    int page = g_noteEd.page;
    std::string nm = g_noteEd.nm;
    g_noteEd.page = -1;
    ShowWindow(g_noteEdit, SW_HIDE);
    bool found = false;
    for (auto& d : g_docs) if (d.get() == t) found = true;
    if (!found || text == g_noteEd.before) return;
    // A brand-new note's text is part of the "add note" step; later edits get their own undo step.
    bool fresh = !t->undo.empty() && !t->undo.back().snap && t->undo.back().nm == nm;
    if (!fresh) Checkpoint(t);
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (p) {
        annots::SetContents(p, nm, ToU16(text));
        FPDF_ClosePage(p);
    }
    if (fresh) {
        auto old = t->undo.back().make;
        std::u16string u = ToU16(text);
        t->undo.back().make = [old, page, nm, u](Tab* tt) {
            if (!old(tt)) return false;
            FPDF_PAGE pg = FPDF_LoadPage(tt->pdf.Handle(), page);
            if (pg) { annots::SetContents(pg, nm, u); FPDF_ClosePage(pg); }
            return true;
        };
    }
    RefreshPage(t, page);
}

static void OpenNoteEditor(Tab* t, int page, const std::string& nm, const annots::Rect& r, const std::wstring& text) {
    CommitNote();
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return;
    RECT vr = RectToView(t, p, page, r);
    FPDF_ClosePage(p);
    if (!g_noteEdit) {
        g_noteEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
                                     0, 0, 0, 0, g_view, nullptr, g_inst, nullptr);
        g_noteBrush = CreateSolidBrush(RGB(0xFF, 0xF6, 0xB8));
    }
    SendMessageW(g_noteEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
    g_noteEd.page = page;
    g_noteEd.nm = nm;
    g_noteEd.before = text;
    g_noteEd.tab = t;
    SetWindowTextW(g_noteEdit, text.c_str());
    RECT rc = ViewClient();
    int w = S(240), h = S(120);
    int x = std::min<int>(vr.right + S(6), rc.right - w - S(4)), y = std::clamp<int>(vr.top, 0, std::max(0, (int)rc.bottom - h));
    SetWindowPos(g_noteEdit, HWND_TOP, x, y, w, h, SWP_SHOWWINDOW);
    SetFocus(g_noteEdit);
    SendMessageW(g_noteEdit, EM_SETSEL, 0, -1);
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)L"  Type the note. Click elsewhere or press Ctrl+Enter when done.");
}

static void PlaceNote(Tab* t, int page, POINT a) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return;
    double x, y;
    ToPage(t, p, page, a, x, y);
    FPDF_ClosePage(p);
    std::string nm = annots::NewId("note");
    std::u16string author = Author();
    float fx = (float)x - 10, fy = (float)y + 10;
    bool ok = AddAnnotation(t, page, nm, [=](Tab* tt) {
        FPDF_PAGE pg = FPDF_LoadPage(tt->pdf.Handle(), page);
        if (!pg) return false;
        bool r = annots::AddNote(pg, fx, fy, u"", 255, 214, 0, author, nm);
        FPDF_ClosePage(pg);
        return r;
    });
    if (ok) OpenNoteEditor(t, page, nm, {fx, fy - 20, fx + 20, fy}, L"");
}

// ---- select tool

static bool HitAnnot(Tab* t, POINT v, int& page, annots::Info& out) {
    page = PageAt(t, v);
    if (page < 0) return false;
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), page);
    if (!p) return false;
    auto list = annots::List(p);
    bool hit = false;
    // Topmost first.
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        RECT r = RectToView(t, p, page, it->rect);
        InflateRect(&r, S(3), S(3));
        if (PtInRect(&r, v)) { out = *it; hit = true; break; }
    }
    FPDF_ClosePage(p);
    return hit;
}

static RECT SelViewRect(Tab* t) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), g_g.selPage);
    RECT r = {};
    if (p) { r = RectToView(t, p, g_g.selPage, g_g.selRect); FPDF_ClosePage(p); }
    return r;
}

static bool OnResizeHandle(Tab* t, POINT v) {
    if (g_g.selNm.empty()) return false;
    RECT r = SelViewRect(t);
    RECT hnd = {r.right - S(5), r.bottom - S(5), r.right + S(6), r.bottom + S(6)};
    return PtInRect(&hnd, v) != 0;
}

static void DeleteSelectedAnnot(Tab* t) {
    if (!t || g_g.selNm.empty()) return;
    if (!Checkpoint(t)) return;
    RemoveAnnot(t, g_g.selPage, g_g.selNm);
    int page = g_g.selPage;
    ClearSelection();
    RefreshPage(t, page);
}

// Applies a move or resize of the selected annotation to the document.
static void ApplySelRect(Tab* t, POINT from, POINT to, bool resize) {
    FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), g_g.selPage);
    if (!p) return;
    double x1, y1, x2, y2;
    ToPage(t, p, g_g.selPage, from, x1, y1);
    ToPage(t, p, g_g.selPage, to, x2, y2);
    annots::Rect r = g_g.selRect;
    double dx = x2 - x1, dy = y2 - y1;
    if (resize) {
        // Keep the shape: scale from the opposite corner by the larger change.
        RECT vr = SelViewRect(t);
        double vw = std::max<double>(1, vr.right - vr.left), vh = std::max<double>(1, vr.bottom - vr.top);
        double f = std::max((vw + (to.x - from.x)) / vw, (vh + (to.y - from.y)) / vh);
        f = std::max(f, 0.2);
        float w = (r.right - r.left) * (float)f, h = (r.top - r.bottom) * (float)f;
        // The bottom-right corner on screen is the page's (right, bottom) only on unrotated pages; keep top-left fixed on screen.
        POINT tl = {vr.left, vr.top};
        double ax, ay;
        ToPage(t, p, g_g.selPage, tl, ax, ay);
        int rot = FPDFPage_GetRotation(p);
        if (rot % 2) std::swap(w, h);
        // Rebuild around the fixed corner in page space.
        double nx1 = ax, ny1 = ay;
        double nx2 = ax + ((rot == 0 || rot == 3) ? w : -w), ny2 = ay + ((rot == 0 || rot == 1) ? -h : h);
        r = {(float)std::min(nx1, nx2), (float)std::min(ny1, ny2), (float)std::max(nx1, nx2), (float)std::max(ny1, ny2)};
    } else {
        r.left += (float)dx; r.right += (float)dx; r.bottom += (float)dy; r.top += (float)dy;
    }
    annots::SetRect(p, g_g.selNm, r);
    g_g.selRect = r;
    FPDF_ClosePage(p);
}

// ---- view mouse handling

static void ToolDown(Tab* t, POINT v, bool dbl) {
    CommitNote();
    int page = PageAt(t, v);
    g_g.start = g_g.cur = v;
    g_g.page = page;
    g_g.pen.clear();
    g_g.textRects.clear();
    switch (g_ms.tool) {
    case kSelect: {
        annots::Info info;
        int hp;
        if (OnResizeHandle(t, v)) {
            g_g.resizing = true;
            g_g.snapTaken = false;
            g_g.active = true;
            return;
        }
        if (HitAnnot(t, v, hp, info)) {
            g_g.selNm = info.nm;
            g_g.selPage = hp;
            g_g.selSubtype = info.subtype;
            g_g.selRect = info.rect;
            if (dbl && info.subtype == FPDF_ANNOT_TEXT) { OpenNoteEditor(t, hp, info.nm, info.rect, ToW(info.contents)); return; }
            if (!info.contents.empty()) SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(L"  " + ToW(info.contents)).c_str());
            g_g.moving = info.movable && !info.nm.empty();
            g_g.snapTaken = false;
            g_g.active = true;
        } else {
            ClearSelection();
        }
        InvalidateRect(g_view, nullptr, FALSE);
        return;
    }
    case kNote:
        if (page >= 0) PlaceNote(t, page, v);
        return;
    case kAddText:
        if (page >= 0) StartAddText(t, page, v);
        return;
    case kEditText:
        if (page >= 0) StartEditText(t, page, v);
        g_g.hoverOn = false;
        return;
    default:
        if (page >= 0) g_g.active = true;
        if (g_ms.tool == kPen) g_g.pen.push_back(v);
        return;
    }
}

static void ToolMove(Tab* t, POINT v) {
    if (!g_g.active) return;
    POINT prev = g_g.cur;
    g_g.cur = v;
    switch (g_ms.tool) {
    case kSelect:
        if ((g_g.moving || g_g.resizing) && (abs(v.x - g_g.start.x) > 2 || abs(v.y - g_g.start.y) > 2)) {
            if (!g_g.snapTaken) { Checkpoint(t); g_g.snapTaken = true; }
            ApplySelRect(t, prev, v, g_g.resizing);
            RefreshPage(t, g_g.selPage);
        }
        break;
    case kHighlight:
        UpdateHighlightPreview(t);
        break;
    case kPen:
        g_g.pen.push_back(v);
        break;
    default:
        break;
    }
    InvalidateRect(g_view, nullptr, FALSE);
}

static void ToolUp(Tab* t, POINT v) {
    if (!g_g.active) return;
    g_g.active = false;
    g_g.cur = v;
    bool dragged = abs(v.x - g_g.start.x) > S(4) || abs(v.y - g_g.start.y) > S(4);
    switch (g_ms.tool) {
    case kTick: PlaceMark(t, g_g.page, g_g.start, v, dragged); break;
    case kHighlight: FinishHighlight(t); break;
    case kBox: FinishShape(t, false); break;
    case kOval: FinishShape(t, true); break;
    case kPen: FinishPen(t); break;
    case kSelect: g_g.moving = g_g.resizing = false; break;
    }
    g_g.textRects.clear();
    g_g.pen.clear();
    InvalidateRect(g_view, nullptr, FALSE);
}

static void PaintMatches(HDC hdc, Tab* t);
static void PaintOverlay(HDC hdc, Tab* t) {
    PaintMatches(hdc, t);
    if (g_g.active && g_ms.tool == kHighlight && !g_g.textRects.empty()) {
        HBRUSH yb = CreateSolidBrush(RGB(0xFF, 0xE2, 0x00));
        for (auto r : g_g.textRects) {
            // Mark the selection with a yellow underline band so the text stays readable.
            RECT band = {r.left, r.bottom - S(3), r.right, r.bottom};
            FillRect(hdc, &band, yb);
            FrameRect(hdc, &r, yb);
        }
        DeleteObject(yb);
    }
    uint8_t cr, cg, cb;
    ColorOf(g_ms.color, cr, cg, cb);
    if (g_g.active && (g_ms.tool == kBox || g_ms.tool == kOval || g_ms.tool == kTick || (g_ms.tool == kHighlight && g_g.textRects.empty()))) {
        RECT r = {std::min(g_g.start.x, g_g.cur.x), std::min(g_g.start.y, g_g.cur.y), std::max(g_g.start.x, g_g.cur.x), std::max(g_g.start.y, g_g.cur.y)};
        if (r.right - r.left > S(4) || r.bottom - r.top > S(4)) {
            HPEN pen = CreatePen(PS_DOT, 1, RGB(cr, cg, cb));
            HGDIOBJ op = SelectObject(hdc, pen), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            if (g_ms.tool == kOval) Ellipse(hdc, r.left, r.top, r.right, r.bottom);
            else Rectangle(hdc, r.left, r.top, r.right, r.bottom);
            SelectObject(hdc, op);
            SelectObject(hdc, ob);
            DeleteObject(pen);
        }
    }
    if (g_g.active && g_ms.tool == kPen && g_g.pen.size() > 1) {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, (int)lround(1.8 * t->scale)), RGB(cr, cg, cb));
        HGDIOBJ op = SelectObject(hdc, pen);
        Polyline(hdc, g_g.pen.data(), (int)g_g.pen.size());
        SelectObject(hdc, op);
        DeleteObject(pen);
    }
    if (g_ms.tool == kEditText && g_g.hoverOn && !(g_textEdit && IsWindowVisible(g_textEdit))) {
        RECT r = g_g.hover;
        InflateRect(&r, S(3), S(2));
        HPEN pen = CreatePen(PS_DOT, 1, g_c.accent);
        HGDIOBJ op = SelectObject(hdc, pen), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SetBkMode(hdc, TRANSPARENT);
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(pen);
    }
    if (!g_g.selNm.empty() && g_g.selPage >= 0 && g_g.selPage < t->pdf.PageCount()) {
        RECT r = SelViewRect(t);
        InflateRect(&r, S(3), S(3));
        HPEN pen = CreatePen(PS_DASH, 1, g_c.accent);
        HGDIOBJ op = SelectObject(hdc, pen), ob = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SetBkMode(hdc, TRANSPARENT);
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, op);
        SelectObject(hdc, ob);
        DeleteObject(pen);
        if (g_g.selSubtype == FPDF_ANNOT_STAMP || g_g.selSubtype == FPDF_ANNOT_SQUARE || g_g.selSubtype == FPDF_ANNOT_CIRCLE) {
            RECT hnd = {r.right - S(5), r.bottom - S(5), r.right + S(3), r.bottom + S(3)};
            HBRUSH hb = CreateSolidBrush(g_c.accent);
            FillRect(hdc, &hnd, hb);
            DeleteObject(hb);
        }
    }
}

// ---------------------------------------------------------------- Marks pane (right side)

struct PaneHit {
    RECT r;
    int action;  // 1 tool, 2 symbol, 3 number, 4 letter, 5 color, 6 size, 7 autoinc, 8 num-, 9 num+, 10 let-, 11 let+, 12 wrap
    int arg;
    const wchar_t* tip;
};
static std::vector<PaneHit> g_paneHits;
static int g_paneHot = -1;
static std::vector<HBITMAP> g_symIcons;
static int g_symIconColor = -1, g_symIconSize = 0;

// Draws each tickmark with the real engine into small bitmaps for the palette.
static HBITMAP RenderMarkIcon(marks::Kind kind, int symbol, const std::string& text, int color, int px) {
    FPDF_DOCUMENT d = FPDF_CreateNewDocument();
    double w, h;
    marks::BaseSize(kind, symbol, text, w, h);
    double pad = 2;
    double pw = w + 2 * pad, ph = h + 2 * pad;
    FPDF_PAGE p = FPDFPage_New(d, 0, pw, ph);
    marks::AddMark(d, p, kind, symbol, text, color, pad, pad, pad + w, pad + h, 0, u"", "icon");
    FPDF_ClosePage(p);
    p = FPDF_LoadPage(d, 0);
    double sc = px / ph;
    int bw = std::max(1, (int)lround(pw * sc)), bh = px;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = bw;
    bi.bmiHeader.biHeight = -bh;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (hb && p) {
        FPDF_BITMAP fb = FPDFBitmap_CreateEx(bw, bh, FPDFBitmap_BGRx, bits, bw * 4);
        FPDFBitmap_FillRect(fb, 0, 0, bw, bh, 0xFF000000 | (GetRValue(g_c.surface) << 16) | (GetGValue(g_c.surface) << 8) | GetBValue(g_c.surface));
        FPDF_RenderPageBitmap(fb, p, 0, 0, bw, bh, 0, FPDF_ANNOT);
        FPDFBitmap_Destroy(fb);
    }
    if (p) FPDF_ClosePage(p);
    FPDF_CloseDocument(d);
    GdiFlush();
    return hb;
}

static void EnsureSymbolIcons() {
    int px = S(24);
    static COLORREF bgWas = CLR_INVALID;
    if (g_symIconColor == g_ms.color && g_symIconSize == px && bgWas == g_c.surface && (int)g_symIcons.size() == marks::SymbolCount()) return;
    bgWas = g_c.surface;
    for (auto b : g_symIcons) if (b) DeleteObject(b);
    g_symIcons.clear();
    for (int i = 0; i < marks::SymbolCount(); i++) g_symIcons.push_back(RenderMarkIcon(marks::kSymbol, i, "", g_ms.color, px));
    g_symIconColor = g_ms.color;
    g_symIconSize = px;
}

static void DrawBitmapCentered(HDC hdc, HBITMAP b, const RECT& r) {
    if (!b) return;
    BITMAP bm;
    GetObject(b, sizeof(bm), &bm);
    HDC mem = CreateCompatibleDC(hdc);
    SelectObject(mem, b);
    int w = std::min<int>(bm.bmWidth, r.right - r.left - S(4)), h = bm.bmHeight;
    int x = (r.left + r.right - w) / 2, y = (r.top + r.bottom - h) / 2;
    // The icon was rendered on the pane colour; that colour is left out so the tile shows through.
    TransparentBlt(hdc, x, y, w, h, mem, (bm.bmWidth - w) / 2, 0, w, h, g_c.surface);
    DeleteDC(mem);
}

static void PaneButton(HDC hdc, const RECT& r, bool on, bool hot) {
    if (on) RoundRectAA(hdc, r, (float)S(8), g_c.accentSoft, g_c.accent, 1.5f);
    else if (hot) RoundRectAA(hdc, r, (float)S(8), g_c.hover, g_c.border);
    else RoundRectAA(hdc, r, (float)S(8), g_c.surface, g_c.border);
}

static int g_paneScroll = 0, g_paneContentH = 0;
static void PaintMarksPane(HDC hdc, const RECT& rc) {
    FillSolid(hdc, rc, g_c.surface);
    FillSolid(hdc, RECT{0, 0, 1, rc.bottom}, g_c.border);
    g_paneHits.clear();
    SetBkMode(hdc, TRANSPARENT);
    int x0 = S(10), width = rc.right - S(20), y = S(8) - g_paneScroll;
    auto heading = [&](const wchar_t* text) {
        // Small capitals in the muted colour, like a section label.
        std::wstring up = text;
        for (auto& ch : up) ch = (wchar_t)towupper(ch);
        SelectObject(hdc, g_headFont);
        SetTextColor(hdc, g_c.muted);
        RECT r = {x0 + S(2), y + S(4), x0 + width, y + S(24)};
        DrawTextW(hdc, up.c_str(), -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        y += S(28);
    };
    auto label = [&](const RECT& r, const wchar_t* text, COLORREF col) {
        SelectObject(hdc, g_smallFont);
        SetTextColor(hdc, col);
        RECT rr = r;
        DrawTextW(hdc, text, -1, &rr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    };
    auto hit = [&](RECT r, int action, int arg, const wchar_t* tip) {
        g_paneHits.push_back({r, action, arg, tip});
        return (int)g_paneHits.size() - 1 == g_paneHot;
    };

    heading(L"Tools");
    {
        int cols = 3, gap = S(4), bw = (width - gap * (cols - 1)) / cols, bh = S(26);
        int order[] = {kHand, kSelect, kHighlight, kAddText, kEditText, kNote, kBox, kOval, kPen};
        for (int k = 0; k < 9; k++) {
            RECT r = {x0 + (k % cols) * (bw + gap), y + (k / cols) * (bh + gap), 0, 0};
            r.right = r.left + bw;
            r.bottom = r.top + bh;
            bool hot = hit(r, 1, order[k], nullptr);
            PaneButton(hdc, r, g_ms.tool == order[k], hot);
            label(r, kToolNames[order[k]], g_c.text);
        }
        y += 3 * (bh + gap) + S(8);
    }

    heading(L"Tickmarks");
    EnsureSymbolIcons();
    {
        int cols = 5, gap = S(4), bw = (width - gap * (cols - 1)) / cols, bh = S(32);
        int n = marks::SymbolCount();
        for (int i = 0; i < n; i++) {
            RECT r = {x0 + (i % cols) * (bw + gap), y + (i / cols) * (bh + gap), 0, 0};
            r.right = r.left + bw;
            r.bottom = r.top + bh;
            bool on = g_ms.tool == kTick && g_ms.kind == marks::kSymbol && g_ms.symbol == i;
            bool hot = hit(r, 2, i, marks::SymbolLabel(i));
            PaneButton(hdc, r, on, hot);
            DrawBitmapCentered(hdc, g_symIcons[i], r);
        }
        y += ((n + cols - 1) / cols) * (bh + gap) + S(6);
    }
    // Number and letter rows: tile, "Next" value, and - / + buttons.
    for (int row = 0; row < 2; row++) {
        bool isNum = row == 0;
        int bh = S(32);
        RECT tile = {x0, y, x0 + S(44), y + bh};
        bool on = g_ms.tool == kTick && g_ms.kind == (isNum ? marks::kNumber : marks::kLetter);
        bool hot = hit(tile, isNum ? 3 : 4, 0, isNum ? L"Numbers 1 to 40 (N)" : L"Letters A to Z (L)");
        PaneButton(hdc, tile, on, hot);
        std::string txt = isNum ? marks::NumberText(g_ms.nextNumber) : marks::LetterText(g_ms.nextLetter);
        HBITMAP icon = RenderMarkIcon(isNum ? marks::kNumber : marks::kLetter, 0, txt, g_ms.color, S(24));
        DrawBitmapCentered(hdc, icon, tile);
        if (icon) DeleteObject(icon);
        RECT lr = {tile.right + S(6), y, tile.right + S(80), y + bh};
        SelectObject(hdc, g_smallFont);
        SetTextColor(hdc, g_c.text2);
        DrawTextW(hdc, isNum ? L"Next number" : L"Next letter", -1, &lr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT minus = {x0 + width - S(58), y + S(4), x0 + width - S(32), y + bh - S(4)};
        RECT plus = {x0 + width - S(26), y + S(4), x0 + width, y + bh - S(4)};
        bool hm = hit(minus, isNum ? 8 : 10, 0, L"Previous");
        PaneButton(hdc, minus, false, hm);
        label(minus, L"\x2212", g_c.text);
        bool hp = hit(plus, isNum ? 9 : 11, 0, L"Next");
        PaneButton(hdc, plus, false, hp);
        label(plus, L"+", g_c.text);
        y += bh + S(4);
    }
    // Auto-increment and wrap check boxes.
    for (int k = 0; k < 2; k++) {
        RECT r = {x0, y, x0 + width, y + S(24)};
        hit(r, k == 0 ? 7 : 12, 0, nullptr);
        RECT box = {x0 + S(2), y + S(5), x0 + S(16), y + S(19)};
        bool on = k == 0 ? g_ms.autoInc : g_ms.wrap;
        if (on) {
            RoundRectAA(hdc, box, (float)S(4), g_c.accent);
            SelectObject(hdc, g_smallFont);
            SetTextColor(hdc, g_c.onAccent);
            DrawTextW(hdc, L"\x2713", -1, &box, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        } else {
            RoundRectAA(hdc, box, (float)S(4), g_c.surface, g_c.muted, 1.2f);
        }
        RECT tr = {box.right + S(8), y, x0 + width, y + S(24)};
        SelectObject(hdc, g_smallFont);
        SetTextColor(hdc, g_c.text);
        DrawTextW(hdc, k == 0 ? L"Auto-increment" : L"Start over after 40 / Z", -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        y += S(24);
    }
    y += S(8);

    heading(L"Sign-off");
    {
        // Two stamp tiles showing exactly what will be placed, then who is signing.
        int gap = S(4), bw = (width - gap) / 2, bh = S(44);
        std::string txt = SignName().empty() ? std::string("Your initials") : SignoffText();
        for (int i = 0; i < 2; i++) {
            marks::Kind k = i ? marks::kReviewed : marks::kPrepared;
            RECT r = {x0 + i * (bw + gap), y, x0 + i * (bw + gap) + bw, y + bh};
            bool on = g_ms.tool == kTick && g_ms.kind == k;
            bool hot = hit(r, 18, k, i ? L"Reviewed by stamp (R): click the page to sign off as reviewer"
                                       : L"Prepared by stamp (P): click the page to sign off as preparer");
            PaneButton(hdc, r, on, hot);
            HBITMAP icon = RenderMarkIcon(k, 0, txt, g_ms.color, S(30));
            DrawBitmapCentered(hdc, icon, r);
            if (icon) DeleteObject(icon);
        }
        y += bh + S(6);
        std::wstring who = SignName();
        RECT lr = {x0 + S(2), y, x0 + width - S(70), y + S(26)};
        SelectObject(hdc, g_smallFont);
        SetTextColor(hdc, g_c.text2);
        std::wstring line = who.empty() ? std::wstring(L"Name not set yet") : L"Signing as " + who;
        DrawTextW(hdc, line.c_str(), -1, &lr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        RECT cr = {x0 + width - S(64), y, x0 + width, y + S(26)};
        bool hc = hit(cr, 19, 0, L"Change the name or initials on sign-off stamps");
        PaneButton(hdc, cr, false, hc);
        label(cr, who.empty() ? L"Set" : L"Change", g_c.text);
        y += S(26) + S(12);
    }

    heading(L"Colour");
    {
        int n = marks::ColorCount(), gap = S(6), bw = (width - gap * (n - 1)) / n;
        for (int i = 0; i < n; i++) {
            RECT r = {x0 + i * (bw + gap), y, x0 + i * (bw + gap) + bw, y + S(24)};
            hit(r, 5, i, marks::ColorName(i));
            uint8_t cr, cg, cb;
            marks::ColorRgb(i, cr, cg, cb);
            int d = std::min<int>(r.right - r.left, r.bottom - r.top + S(4)), cxm = (r.left + r.right) / 2, cym = (r.top + r.bottom) / 2;
            RECT c = {cxm - d / 2, cym - d / 2, cxm + d / 2, cym + d / 2};
            if (g_ms.color == i) {
                RECT o = c;
                InflateRect(&o, S(3), S(3));
                EllipseAA(hdc, o, CLR_INVALID, g_c.text, 2.0f);
            }
            EllipseAA(hdc, c, RGB(cr, cg, cb));
        }
        y += S(34);
    }
    heading(L"Size");
    {
        int gap = S(4), bw = (width - 2 * gap) / 3;
        for (int i = 0; i < 3; i++) {
            RECT r = {x0 + i * (bw + gap), y, x0 + i * (bw + gap) + bw, y + S(26)};
            bool hot = hit(r, 6, i, nullptr);
            PaneButton(hdc, r, g_ms.size == i, hot);
            label(r, marks::SizeName(i), g_c.text);
        }
        y += S(36);
    }
    heading(L"New text");
    {
        int gap = S(4), bh = S(26);
        static const wchar_t* fam[3] = {L"Sans", L"Serif", L"Mono"};
        int bw = (width - 3 * gap - S(30)) / 3;
        for (int i = 0; i < 3; i++) {
            RECT r = {x0 + i * (bw + gap), y, x0 + i * (bw + gap) + bw, y + bh};
            bool hot = hit(r, 13, i, nullptr);
            PaneButton(hdc, r, g_ms.textFamily == i, hot);
            label(r, fam[i], g_c.text);
        }
        RECT br = {x0 + width - S(30), y, x0 + width, y + bh};
        bool hb = hit(br, 14, 0, L"Bold");
        PaneButton(hdc, br, g_ms.textBold != 0, hb);
        SelectObject(hdc, g_bigFont ? g_bigFont : g_font);
        SetTextColor(hdc, g_c.text);
        DrawTextW(hdc, L"B", -1, &br, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        y += bh + S(6);
        RECT lr = {x0, y, x0 + width - S(60), y + bh};
        wchar_t sz[32];
        swprintf(sz, 32, L"Size %d pt", g_ms.textSize);
        SelectObject(hdc, g_smallFont);
        SetTextColor(hdc, g_c.text2);
        DrawTextW(hdc, sz, -1, &lr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        RECT minus = {x0 + width - S(58), y + S(2), x0 + width - S(32), y + bh - S(2)};
        RECT plus = {x0 + width - S(26), y + S(2), x0 + width, y + bh - S(2)};
        bool hm = hit(minus, 15, 0, L"Smaller");
        PaneButton(hdc, minus, false, hm);
        label(minus, L"\x2212", g_c.text);
        bool hp = hit(plus, 16, 0, L"Larger");
        PaneButton(hdc, plus, false, hp);
        label(plus, L"+", g_c.text);
        y += bh + S(10);
    }
    heading(L"Edit text picks");
    {
        int gap = S(4), bh = S(26), bw = (width - gap) / 2;
        static const wchar_t* modes[2] = {L"A line", L"A paragraph"};
        for (int i = 0; i < 2; i++) {
            RECT r = {x0 + i * (bw + gap), y, x0 + i * (bw + gap) + bw, y + bh};
            bool hot = hit(r, 17, i, i ? L"Retype a whole paragraph; the lines re-wrap" : L"Retype one line or table cell");
            PaneButton(hdc, r, g_ms.editPara == i, hot);
            label(r, modes[i], g_c.text);
        }
        y += bh + S(10);
    }
    // Shortcut reminder.
    RECT hr = {x0, y, x0 + width, y + S(110)};
    g_paneContentH = y + S(116) + g_paneScroll;
    SelectObject(hdc, g_smallFont);
    SetTextColor(hdc, g_c.muted);
    DrawTextW(hdc,
              L"Click a mark, then click the page. Drag to size it.\n\n"
              L"Keys on the page: K tickmark, N number, L letter, P prepared by, R reviewed by, H highlight, T add text, E edit text, S select, Esc hand. "
              L"Ctrl+Z undoes.",
              -1, &hr, DT_LEFT | DT_WORDBREAK);
}

static void PaneAction(const PaneHit& h) {
    switch (h.action) {
    case 1: SetTool(h.arg); break;
    case 2: g_ms.kind = marks::kSymbol; g_ms.symbol = h.arg; SetTool(kTick); break;
    case 3: g_ms.kind = marks::kNumber; SetTool(kTick); break;
    case 4: g_ms.kind = marks::kLetter; SetTool(kTick); break;
    case 5: g_ms.color = h.arg; break;
    case 6: g_ms.size = h.arg; break;
    case 7: g_ms.autoInc = !g_ms.autoInc; break;
    case 12: g_ms.wrap = !g_ms.wrap; break;
    case 8: g_ms.nextNumber = std::max(1, g_ms.nextNumber - 1); break;
    case 9: g_ms.nextNumber = std::min(40, g_ms.nextNumber + 1); break;
    case 10: g_ms.nextLetter = std::max(0, g_ms.nextLetter - 1); break;
    case 11: g_ms.nextLetter = std::min(25, g_ms.nextLetter + 1); break;
    case 13: g_ms.textFamily = h.arg; if (g_ms.tool != kAddText) SetTool(kAddText); break;
    case 14: g_ms.textBold = !g_ms.textBold; break;
    case 15: g_ms.textSize = std::max(6, g_ms.textSize - (g_ms.textSize > 12 ? 2 : 1)); break;
    case 16: g_ms.textSize = std::min(36, g_ms.textSize + (g_ms.textSize >= 12 ? 2 : 1)); break;
    case 17: g_ms.editPara = h.arg; g_g.hoverAt = POINT{-1000, -1000}; SetTool(kEditText); break;
    case 18: g_ms.kind = h.arg; SetTool(kTick); if (SignName().empty()) AskSignName(); break;
    case 19: AskSignName(); break;
    }
    SaveMarkSettings();
    InvalidateRect(g_marks, nullptr, FALSE);
}

static LRESULT CALLBACK MarksProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEWHEEL: {
        // The pane scrolls when the window is too short to show all of it.
        RECT rc;
        GetClientRect(h, &rc);
        int maxScroll = std::max(0, g_paneContentH - (int)rc.bottom);
        g_paneScroll = std::clamp(g_paneScroll - GET_WHEEL_DELTA_WPARAM(w) * S(60) / WHEEL_DELTA, 0, maxScroll);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_SIZE: {
        RECT rc;
        GetClientRect(h, &rc);
        g_paneScroll = std::clamp(g_paneScroll, 0, std::max(0, g_paneContentH - (int)rc.bottom));
        InvalidateRect(h, nullptr, FALSE);
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC buf = CreateCompatibleDC(hdc);
        HBITMAP bb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ ob = SelectObject(buf, bb);
        PaintMarksPane(buf, rc);
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
        SelectObject(buf, ob);
        DeleteObject(bb);
        DeleteDC(buf);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int hot = -1;
        for (int i = 0; i < (int)g_paneHits.size(); i++) if (PtInRect(&g_paneHits[i].r, p)) hot = i;
        if (hot != g_paneHot) {
            g_paneHot = hot;
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
            if (hot >= 0 && g_paneHits[hot].tip) SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(std::wstring(L"  ") + g_paneHits[hot].tip).c_str());
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_paneHot = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        for (auto& hh : g_paneHits) if (PtInRect(&hh.r, p)) { PaneAction(hh); break; }
        SetFocus(g_view);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---- start screen (no file open): open button and recent files

struct StartHit { RECT r; int action; int arg; };  // action 1 = open, 2 = recent file
static std::vector<StartHit> g_startHits;
static int g_startHot = -1;

static void PaintStart(HDC hdc, const RECT& rc) {
    g_startHits.clear();
    SetBkMode(hdc, TRANSPARENT);
    std::vector<int> rec;
    for (int i = 0; i < (int)g_recent.size() && rec.size() < 6; i++)
        if (GetFileAttributesW(g_recent[i].c_str()) != INVALID_FILE_ATTRIBUTES) rec.push_back(i);
    int cw = std::min<int>(rc.right - S(48), S(560)), cx = (rc.right - cw) / 2;
    int rowH = S(54);
    int listH = rec.empty() ? 0 : S(40) + (int)rec.size() * rowH + S(8);
    int totalH = S(64) + S(22) + S(44) + S(30) + S(26) + S(48) + S(30) + listH;
    int y = std::max<int>(S(28), (rc.bottom - totalH) / 2);
    int mid = rc.right / 2;
    // Logo: a rounded tile with a tick.
    RECT logo = {mid - S(32), y, mid + S(32), y + S(64)};
    RoundRectAA(hdc, logo, (float)S(16), g_c.accent);
    SelectObject(hdc, g_logoFont);
    SetTextColor(hdc, g_c.onAccent);
    DrawTextW(hdc, L"\xE73E", -1, &logo, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    y += S(64) + S(22);
    RECT tr = {0, y, rc.right, y + S(44)};
    SelectObject(hdc, g_titleFont);
    SetTextColor(hdc, g_c.text);
    DrawTextW(hdc, APP_NAME, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    y += S(44);
    RECT sr = {0, y, rc.right, y + S(30)};
    SelectObject(hdc, g_font);
    SetTextColor(hdc, g_c.text2);
    DrawTextW(hdc, L"Open a workpaper, or drop PDFs, Word files and images anywhere in this window.", -1, &sr,
              DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    y += S(30) + S(26);
    RECT btn = {mid - S(96), y, mid + S(96), y + S(48)};
    bool hotBtn = g_startHot == 0;
    RoundRectAA(hdc, btn, (float)S(12), hotBtn ? g_c.accentHover : g_c.accent);
    g_startHits.push_back({btn, 1, 0});
    RECT bi = {btn.left + S(22), btn.top, btn.left + S(46), btn.bottom};
    SelectObject(hdc, g_iconFont);
    SetTextColor(hdc, g_c.onAccent);
    DrawTextW(hdc, L"\xE8E5", -1, &bi, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    RECT bt = {btn.left + S(50), btn.top, btn.right - S(16), btn.bottom};
    SelectObject(hdc, g_btnFont);
    DrawTextW(hdc, L"Open a PDF", -1, &bt, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += S(48);
    RECT hint = {0, y + S(6), rc.right, y + S(28)};
    SelectObject(hdc, g_smallFont);
    SetTextColor(hdc, g_c.muted);
    DrawTextW(hdc, L"Ctrl+O", -1, &hint, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    y += S(30);
    if (rec.empty()) return;
    y += S(14);
    RECT hd = {cx + S(4), y, cx + cw, y + S(26)};
    SelectObject(hdc, g_headFont);
    SetTextColor(hdc, g_c.muted);
    DrawTextW(hdc, L"RECENT", -1, &hd, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    y += S(30);
    RECT card = {cx, y, cx + cw, y + (int)rec.size() * rowH + S(8)};
    SoftShadow(hdc, card, S(4), g_c.shadow, g_dark ? 60 : 18);
    RoundRectAA(hdc, card, (float)S(12), g_c.surface, g_c.border);
    int ry = y + S(4);
    for (size_t k = 0; k < rec.size(); k++) {
        const std::wstring& path = g_recent[rec[k]];
        RECT row = {cx + S(4), ry, cx + cw - S(4), ry + rowH};
        int hitIndex = (int)g_startHits.size();
        g_startHits.push_back({row, 2, rec[k]});
        if (g_startHot == hitIndex) RoundRectAA(hdc, row, (float)S(9), g_c.hover);
        RECT ic = {row.left + S(10), row.top + S(9), row.left + S(46), row.bottom - S(9)};
        RoundRectAA(hdc, ic, (float)S(8), g_c.accentSoft);
        SelectObject(hdc, g_iconFont);
        SetTextColor(hdc, g_c.accent);
        DrawTextW(hdc, L"\xE8A5", -1, &ic, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        size_t sl = path.find_last_of(L"\\/");
        std::wstring name = sl == std::wstring::npos ? path : path.substr(sl + 1);
        std::wstring folder = sl == std::wstring::npos ? L"" : path.substr(0, sl);
        RECT nr = {ic.right + S(14), row.top + S(8), row.right - S(12), row.top + S(29)};
        SelectObject(hdc, g_font);
        SetTextColor(hdc, g_c.text);
        DrawTextW(hdc, name.c_str(), -1, &nr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT fr = {nr.left, row.top + S(28), nr.right, row.bottom - S(6)};
        SelectObject(hdc, g_smallFont);
        SetTextColor(hdc, g_c.muted);
        DrawTextW(hdc, folder.c_str(), -1, &fr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | DT_NOPREFIX);
        ry += rowH;
    }
}

static int StartHitAt(POINT p) {
    for (int i = 0; i < (int)g_startHits.size(); i++) if (PtInRect(&g_startHits[i].r, p)) return i;
    return -1;
}

static void StartClick(POINT p) {
    int i = StartHitAt(p);
    if (i < 0) return;
    if (g_startHits[i].action == 1) SendMessageW(g_main, WM_COMMAND, ID_OPEN, 0);
    else if (g_startHits[i].action == 2 && g_startHits[i].arg < (int)g_recent.size()) OpenFile(g_recent[g_startHits[i].arg]);
}

static void PaintView(HDC hdc, const RECT& rc) {
    Tab* t = Cur();
    FillSolid(hdc, rc, g_c.canvas);
    if (!t) {
        PaintStart(hdc, rc);
        return;
    }
    int first, last;
    VisibleRange(t, first, last);
    HDC mem = CreateCompatibleDC(hdc);
    HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
    bool missing = false;
    for (int i = first; i <= last; i++) {
        int x = PageX(t, i), y = t->top[i] - t->sy;
        RECT pr = {x, y, x + t->pw[i], y + t->ph[i]};
        SoftShadow(hdc, pr, S(5), g_c.shadow, g_dark ? 110 : 34);
        Cached& c = t->cache[i];
        RECT need = NeededRegion(t, i);
        if (!Covers(c, t->scale, need)) missing = true;
        if (c.bmp && c.scale == t->scale) {
            FillRect(hdc, &pr, white);
            SelectObject(mem, c.bmp);
            BitBlt(hdc, x + c.r.left, y + c.r.top, c.r.right - c.r.left, c.r.bottom - c.r.top, mem, 0, 0, SRCCOPY);
        } else if (c.bmp) {
            // Old zoom level: stretch it until the sharp version is ready.
            FillRect(hdc, &pr, white);
            double f = t->scale / c.scale;
            SelectObject(mem, c.bmp);
            SetStretchBltMode(hdc, COLORONCOLOR);
            StretchBlt(hdc, x + (int)(c.r.left * f), y + (int)(c.r.top * f), (int)((c.r.right - c.r.left) * f),
                       (int)((c.r.bottom - c.r.top) * f), mem, 0, 0, c.r.right - c.r.left, c.r.bottom - c.r.top, SRCCOPY);
        } else {
            FillRect(hdc, &pr, white);
        }
    }
    DeleteDC(mem);
    PaintOverlay(hdc, t);
    if (missing) RequestRender();
}

static void ScrollBy(Tab* t, int dx, int dy) {
    if (!t) return;
    int ox = t->sx, oy = t->sy;
    t->sx += dx;
    t->sy += dy;
    ClampScroll(t);
    if (t->sx == ox && t->sy == oy) return;
    ScrollWindowEx(g_view, ox - t->sx, oy - t->sy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE);
    UpdateScrollBars();
    UpdateStatus();
    RequestRender();
}

static void ScrollTo(Tab* t, int x, int y) { if (t) ScrollBy(t, x - t->sx, y - t->sy); }

static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Tab* t = Cur();
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        // Double buffer the dirty area to avoid flicker.
        RECT d = ps.rcPaint;
        int dw = d.right - d.left, dh = d.bottom - d.top;
        if (dw > 0 && dh > 0) {
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, dw, dh);
            HGDIOBJ old = SelectObject(mem, bmp);
            SetViewportOrgEx(mem, -d.left, -d.top, nullptr);
            PaintView(mem, rc);
            SetViewportOrgEx(mem, 0, 0, nullptr);
            BitBlt(hdc, d.left, d.top, dw, dh, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_APP_RENDER: {
        g_renderPosted = false;
        if (!t) return 0;
        if (RenderOnePage(t) || RenderOneThumb(t)) RequestRender();
        return 0;
    }
    case WM_SIZE:
        if (t) {
            if (t->fit) Layout(t);
            Refresh(t->fit == 0);
            RequestRender();
        } else InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_VSCROLL: {
        if (!t) return 0;
        CommitNote();
        RECT rc = ViewClient();
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(h, SB_VERT, &si);
        int y = t->sy;
        switch (LOWORD(w)) {
        case SB_LINEUP: y -= S(40); break;
        case SB_LINEDOWN: y += S(40); break;
        case SB_PAGEUP: y -= rc.bottom - S(40); break;
        case SB_PAGEDOWN: y += rc.bottom - S(40); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: y = si.nTrackPos; break;
        case SB_TOP: y = 0; break;
        case SB_BOTTOM: y = t->docH; break;
        }
        ScrollTo(t, t->sx, y);
        return 0;
    }
    case WM_HSCROLL: {
        if (!t) return 0;
        CommitNote();
        RECT rc = ViewClient();
        SCROLLINFO si = {sizeof(si), SIF_ALL};
        GetScrollInfo(h, SB_HORZ, &si);
        int x = t->sx;
        switch (LOWORD(w)) {
        case SB_LINELEFT: x -= S(40); break;
        case SB_LINERIGHT: x += S(40); break;
        case SB_PAGELEFT: x -= rc.right; break;
        case SB_PAGERIGHT: x += rc.right; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: x = si.nTrackPos; break;
        }
        ScrollTo(t, x, t->sy);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!t) return 0;
        CommitNote();
        int delta = GET_WHEEL_DELTA_WPARAM(w);
        if (GET_KEYSTATE_WPARAM(w) & MK_CONTROL) {
            POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            ZoomStep(t, delta > 0 ? 1 : -1, &p);
        } else if (GET_KEYSTATE_WPARAM(w) & MK_SHIFT) {
            ScrollBy(t, -delta * S(120) / WHEEL_DELTA, 0);
        } else {
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            ScrollBy(t, 0, -delta * (int)lines * S(36) / WHEEL_DELTA);
        }
        return 0;
    }
    case WM_MOUSEHWHEEL:
        if (t) ScrollBy(t, GET_WHEEL_DELTA_WPARAM(w) * S(120) / WHEEL_DELTA, 0);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SetFocus(h);
        if (!t) { StartClick(POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)}); return 0; }
        if (t) {
            POINT v = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            // Hand tool, or the space bar held down: pan.
            if (g_ms.tool == kHand || GetKeyState(VK_SPACE) < 0 || (g_ms.tool != kSelect && PageAt(t, v) < 0)) {
                CommitNote();
                g_dragging = true;
                g_dragStart = v;
                g_dragSX = t->sx;
                g_dragSY = t->sy;
            } else {
                ToolDown(t, v, m == WM_LBUTTONDBLCLK);
            }
            SetCapture(h);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging && t) ScrollTo(t, g_dragSX - (GET_X_LPARAM(l) - g_dragStart.x), g_dragSY - (GET_Y_LPARAM(l) - g_dragStart.y));
        else if (t && g_g.active) ToolMove(t, POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        else if (t && g_ms.tool == kEditText) UpdateEditHover(t, POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        else if (!t) {
            int hot = StartHitAt(POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)});
            if (hot != g_startHot) {
                g_startHot = hot;
                InvalidateRect(h, nullptr, FALSE);
                TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
                TrackMouseEvent(&tme);
            }
        }
        return 0;
    case WM_MOUSELEAVE:
        if (g_startHot >= 0) { g_startHot = -1; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging) g_dragging = false;
        else if (t && g_g.active) ToolUp(t, POINT{GET_X_LPARAM(l), GET_Y_LPARAM(l)});
        if (GetCapture() == h) ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED:
        g_dragging = false;
        if (g_g.active) { g_g.active = false; g_g.moving = g_g.resizing = false; g_g.textRects.clear(); g_g.pen.clear(); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_CTLCOLOREDIT:
        if ((HWND)l == g_noteEdit) { SetBkColor((HDC)w, RGB(0xFF, 0xF6, 0xB8)); return (LRESULT)g_noteBrush; }
        if ((HWND)l == g_textEdit) { SetBkColor((HDC)w, RGB(255, 255, 255)); SetTextColor((HDC)w, g_te.color); return (LRESULT)g_whiteBrush; }
        break;
    case WM_COMMAND:
        if ((HWND)l == g_noteEdit && HIWORD(w) == EN_KILLFOCUS) CommitNote();
        if (g_textEdit && (HWND)l == g_textEdit && HIWORD(w) == EN_KILLFOCUS) CommitText(false);
        if (g_textEdit && (HWND)l == g_textEdit && HIWORD(w) == EN_CHANGE) FitTextEditor();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && !t && g_startHot >= 0) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
        if (LOWORD(l) == HTCLIENT && t) {
            POINT v;
            GetCursorPos(&v);
            ScreenToClient(h, &v);
            LPCWSTR c = IDC_HAND;
            if (g_dragging) c = IDC_SIZEALL;
            else if (g_ms.tool == kSelect) c = OnResizeHandle(t, v) ? IDC_SIZENWSE : IDC_ARROW;
            else if (g_ms.tool == kHighlight || g_ms.tool == kAddText || g_ms.tool == kEditText) c = PageAt(t, v) >= 0 ? IDC_IBEAM : IDC_HAND;
            else if (g_ms.tool != kHand) c = PageAt(t, v) >= 0 ? IDC_CROSS : IDC_HAND;
            SetCursor(LoadCursor(nullptr, c));
            return TRUE;
        }
        break;
    case WM_KEYDOWN: {
        if (!t) break;
        RECT rc = ViewClient();
        bool ctrl = GetKeyState(VK_CONTROL) < 0;
        switch (w) {
        case VK_DOWN: ScrollBy(t, 0, S(40)); return 0;
        case VK_UP: ScrollBy(t, 0, -S(40)); return 0;
        case VK_LEFT: if (t->docW > rc.right) ScrollBy(t, -S(40), 0); else GoToPage(t, CurrentPage(t) - 1); return 0;
        case VK_RIGHT: if (t->docW > rc.right) ScrollBy(t, S(40), 0); else GoToPage(t, CurrentPage(t) + 1); return 0;
        case VK_NEXT: case VK_SPACE:
            if (w == VK_SPACE && GetKeyState(VK_SHIFT) < 0) ScrollBy(t, 0, -(rc.bottom - S(40)));
            else ScrollBy(t, 0, rc.bottom - S(40));
            return 0;
        case VK_PRIOR: ScrollBy(t, 0, -(rc.bottom - S(40))); return 0;
        case VK_HOME: if (ctrl || true) ScrollTo(t, t->sx, 0); return 0;
        case VK_END: ScrollTo(t, t->sx, t->docH); return 0;
        case VK_ESCAPE: if (g_g.selNm.empty()) SetTool(kHand); else ClearSelection(); return 0;
        case VK_DELETE: DeleteSelectedAnnot(t); return 0;
        case 'K': if (!ctrl) { g_ms.kind = marks::kSymbol; SetTool(kTick); return 0; } break;
        case 'N': if (!ctrl) { g_ms.kind = marks::kNumber; SetTool(kTick); return 0; } break;
        case 'L': if (!ctrl) { g_ms.kind = marks::kLetter; SetTool(kTick); return 0; } break;
        case 'P': if (!ctrl) { g_ms.kind = marks::kPrepared; SetTool(kTick); if (SignName().empty()) AskSignName(); return 0; } break;
        case 'R': if (!ctrl) { g_ms.kind = marks::kReviewed; SetTool(kTick); if (SignName().empty()) AskSignName(); return 0; } break;
        case 'H': if (!ctrl) { SetTool(kHighlight); return 0; } break;
        case 'S': if (!ctrl) { SetTool(kSelect); return 0; } break;
        case 'T': if (!ctrl) { SetTool(kAddText); return 0; } break;
        case 'E': if (!ctrl) { SetTool(kEditText); return 0; } break;
        }
        break;
    }
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- thumbnails pane

// Drag state for reordering thumbnails, and the drop marker for files dragged in from Explorer.
static int g_thumbPress = -1;     // thumbnail under the mouse when the button went down
static bool g_thumbDragging = false;
static POINT g_thumbPressPt;
static int g_dropGap = -1;        // insertion point shown while dragging (0..count), -1 = none

static int ThumbAt(Tab* t, int y) {
    y += t->thumbScroll;
    for (int i = 0; i < t->pdf.PageCount(); i++)
        if (y >= t->thumbTop[i] - S(6) && y < t->thumbTop[i] + t->thumbH[i] + S(24)) return i;
    return -1;
}

static int GapAt(Tab* t, int y) {
    y += t->thumbScroll;
    int n = t->pdf.PageCount();
    for (int i = 0; i < n; i++)
        if (y < t->thumbTop[i] + t->thumbH[i] / 2) return i;
    return n;
}

static void ThumbAutoScroll(Tab* t, int y) {
    RECT rc;
    GetClientRect(g_thumbs, &rc);
    int d = 0;
    if (y < S(30)) d = -S(24);
    else if (y > rc.bottom - S(30)) d = S(24);
    if (!d) return;
    t->thumbScroll = std::clamp(t->thumbScroll + d, 0, std::max(0, t->thumbTotal - (int)rc.bottom));
    RequestRender();
}

static void ThumbMenu(Tab* t, POINT screen) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_ROTR, L"Rotate right\tCtrl+R");
    AppendMenuW(m, MF_STRING, ID_ROTL, L"Rotate left\tCtrl+Shift+R");
    AppendMenuW(m, MF_STRING, ID_DUP, L"Duplicate\tCtrl+D");
    AppendMenuW(m, MF_STRING, ID_DEL, L"Delete\tDel");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_INSERTFILE, L"Insert pages from files...");
    AppendMenuW(m, MF_STRING, ID_INSERTBLANK, L"Insert blank page");
    AppendMenuW(m, MF_STRING, ID_EXTRACT, L"Save these pages as a new PDF...");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, ID_SELECTALL, L"Select all pages\tCtrl+A");
    TrackPopupMenu(m, TPM_RIGHTBUTTON, screen.x, screen.y, 0, g_main, nullptr);
    DestroyMenu(m);
}

static LRESULT CALLBACK ThumbProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Tab* t = Cur();
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC buf = CreateCompatibleDC(hdc);
        HBITMAP bb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ oldb = SelectObject(buf, bb);
        FillSolid(buf, rc, g_c.surface);
        FillSolid(buf, RECT{rc.right - 1, 0, rc.right, rc.bottom}, g_c.border);
        if (t && !t->thumbTop.empty()) {
            int curPage = CurrentPage(t);
            HDC mem = CreateCompatibleDC(hdc);
            SelectObject(buf, g_font);
            SetBkMode(buf, TRANSPARENT);
            HBRUSH blue = CreateSolidBrush(g_c.accent);
            HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
            bool missing = false;
            for (int i = 0; i < t->pdf.PageCount(); i++) {
                int y = t->thumbTop[i] - t->thumbScroll;
                if (y + t->thumbH[i] + S(30) < 0) continue;
                if (y > rc.bottom) break;
                int x = (rc.right - t->thumbW) / 2;
                if (t->sel.count(i)) {
                    RECT sb = {S(8), y - S(8), rc.right - S(9), y + t->thumbH[i] + S(28)};
                    RoundRectAA(buf, sb, (float)S(10), g_c.accentSoft);
                }
                RECT pr = {x, y, x + t->thumbW, y + t->thumbH[i]};
                if (i == curPage) {
                    RECT fr = pr;
                    InflateRect(&fr, S(3), S(3));
                    RoundRectAA(buf, fr, (float)S(5), CLR_INVALID, g_c.accent, (float)S(2));
                } else {
                    SoftShadow(buf, pr, S(3), g_c.shadow, g_dark ? 90 : 26);
                }
                FillRect(buf, &pr, white);
                if (t->thumb[i]) {
                    SelectObject(mem, t->thumb[i]);
                    BitBlt(buf, x, y, t->thumbW, t->thumbH[i], mem, 0, 0, SRCCOPY);
                } else missing = true;
                wchar_t num[16];
                swprintf(num, 16, L"%d", i + 1);
                RECT lr = {0, pr.bottom + S(6), rc.right, pr.bottom + S(24)};
                SelectObject(buf, g_smallFont);
                if (i == curPage) {
                    RECT pill = {rc.right / 2 - S(16), lr.top, rc.right / 2 + S(16), lr.bottom};
                    RoundRectAA(buf, pill, (float)S(9), g_c.accent);
                    SetTextColor(buf, g_c.onAccent);
                } else {
                    SetTextColor(buf, g_c.muted);
                }
                DrawTextW(buf, num, -1, &lr, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            }
            // Insertion marker while dragging pages or files.
            if (g_dropGap >= 0) {
                int n = t->pdf.PageCount();
                int y = g_dropGap < n ? t->thumbTop[g_dropGap] - S(9) : t->thumbTop[n - 1] + t->thumbH[n - 1] + S(24);
                y -= t->thumbScroll;
                RECT bar = {S(10), y - S(2), rc.right - S(10), y + S(2)};
                FillRect(buf, &bar, blue);
            }
            DeleteObject(blue);
            DeleteDC(mem);
            if (missing) RequestRender();
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
        SelectObject(buf, oldb);
        DeleteObject(bb);
        DeleteDC(buf);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (t) { LayoutThumbs(t); InvalidateRect(h, nullptr, FALSE); RequestRender(); }
        return 0;
    case WM_MOUSEWHEEL:
        if (t) {
            RECT rc;
            GetClientRect(h, &rc);
            t->thumbScroll -= GET_WHEEL_DELTA_WPARAM(w) * S(100) / WHEEL_DELTA;
            t->thumbScroll = std::clamp(t->thumbScroll, 0, std::max(0, t->thumbTotal - (int)rc.bottom));
            InvalidateRect(h, nullptr, FALSE);
            RequestRender();
        }
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        SetFocus(h);
        if (t) {
            int i = ThumbAt(t, GET_Y_LPARAM(l));
            bool ctrl = (w & MK_CONTROL) != 0, shift = (w & MK_SHIFT) != 0;
            if (i < 0) {
                if (m == WM_LBUTTONDOWN && !ctrl) t->sel.clear();
            } else if (m == WM_RBUTTONDOWN) {
                if (!t->sel.count(i)) { t->sel.clear(); t->sel.insert(i); t->anchor = i; }
            } else if (shift && t->anchor >= 0) {
                if (!ctrl) t->sel.clear();
                for (int k = std::min(t->anchor, i); k <= std::max(t->anchor, i); k++) t->sel.insert(k);
            } else if (ctrl) {
                if (t->sel.count(i)) t->sel.erase(i); else t->sel.insert(i);
                t->anchor = i;
            } else {
                // Keep a multi-selection if the press starts a drag on it.
                if (!t->sel.count(i)) { t->sel.clear(); t->sel.insert(i); }
                t->anchor = i;
                g_thumbPress = i;
                g_thumbPressPt = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                SetCapture(h);
            }
            if (i >= 0 && !ctrl && !shift && m == WM_LBUTTONDOWN) GoToPage(t, i);
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (t && g_thumbPress >= 0) {
            POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (!g_thumbDragging && (abs(p.y - g_thumbPressPt.y) > S(6) || abs(p.x - g_thumbPressPt.x) > S(6))) g_thumbDragging = true;
            if (g_thumbDragging) {
                ThumbAutoScroll(t, p.y);
                RECT rc;
                GetClientRect(h, &rc);
                g_dropGap = (p.x >= -S(40) && p.x <= rc.right + S(40)) ? GapAt(t, p.y) : -1;
                SetCursor(LoadCursor(nullptr, g_dropGap >= 0 ? IDC_HAND : IDC_NO));
                InvalidateRect(h, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (t && g_thumbPress >= 0) {
            bool dragged = g_thumbDragging;
            int gap = g_dropGap;
            g_thumbPress = -1;
            g_thumbDragging = false;
            g_dropGap = -1;
            ReleaseCapture();
            if (dragged) {
                POINT sp = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
                ClientToScreen(h, &sp);
                // Dropped on another tab: copy the pages to the end of that document.
                POINT tp = sp;
                ScreenToClient(g_tabs, &tp);
                TCHITTESTINFO hti = {tp, 0};
                int tab = IsWindowVisible(g_tabs) ? TabCtrl_HitTest(g_tabs, &hti) : -1;
                if (tab >= 0 && tab != g_cur) {
                    Tab* dst = g_docs[tab].get();
                    auto pages = TargetPages(t);
                    if (Checkpoint(dst)) {
                        int at = dst->pdf.PageCount();
                        int n = ops::InsertFrom(dst->pdf.Handle(), t->pdf.Handle(), pages, at);
                        if (n > 0) {
                            dst->pdf.RefreshSizes();
                            dst->dirty = true;
                            ClearRenders(dst);
                            RebuildTabStrip();
                            wchar_t msg[200];
                            swprintf(msg, 200, L"Copied %d page%s to %s.", n, n == 1 ? L"" : L"s", dst->pdf.Title().c_str());
                            SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(std::wstring(L"  ") + msg).c_str());
                        } else dst->undo.pop_back();
                    }
                } else if (gap >= 0) {
                    MoveSelTo(t, gap);
                }
            }
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_CAPTURECHANGED:
        if (g_thumbPress >= 0) { g_thumbPress = -1; g_thumbDragging = false; g_dropGap = -1; InvalidateRect(h, nullptr, FALSE); }
        return 0;
    case WM_CONTEXTMENU:
        if (t) {
            POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (p.x == -1) { p = {S(40), S(40)}; ClientToScreen(h, &p); }
            ThumbMenu(t, p);
        }
        return 0;
    case WM_KEYDOWN:
        if (!t) break;
        if (w == VK_DELETE) { DeleteSel(t); return 0; }
        if (w == VK_UP || w == VK_DOWN || w == VK_PRIOR || w == VK_NEXT) {
            int cur = t->sel.empty() ? CurrentPage(t) : (w == VK_UP || w == VK_PRIOR ? *t->sel.begin() : *t->sel.rbegin());
            int step = (w == VK_PRIOR || w == VK_NEXT) ? 5 : 1;
            int nx = std::clamp(cur + ((w == VK_UP || w == VK_PRIOR) ? -step : step), 0, t->pdf.PageCount() - 1);
            if (GetKeyState(VK_SHIFT) >= 0 || t->anchor < 0) { t->sel.clear(); t->anchor = nx; t->sel.insert(nx); }
            else { t->sel.clear(); for (int k = std::min(t->anchor, nx); k <= std::max(t->anchor, nx); k++) t->sel.insert(k); }
            GoToPage(t, nx);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (w == VK_ESCAPE) { t->sel.clear(); InvalidateRect(h, nullptr, FALSE); return 0; }
        break;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- dropping files from Explorer

class DropTarget final : public IDropTarget {
public:
    virtual ~DropTarget() = default;
private:
    LONG refs = 1;
    bool hasFiles = false;

    static std::vector<std::wstring> Files(IDataObject* d) {
        std::vector<std::wstring> out;
        FORMATETC fe = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        STGMEDIUM sm;
        if (FAILED(d->GetData(&fe, &sm))) return out;
        HDROP drop = (HDROP)GlobalLock(sm.hGlobal);
        if (drop) {
            UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT i = 0; i < n; i++) {
                UINT len = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring p(len, L'\0');
                DragQueryFileW(drop, i, &p[0], len + 1);
                out.push_back(p);
            }
            GlobalUnlock(sm.hGlobal);
        }
        ReleaseStgMedium(&sm);
        return out;
    }
    // Where a drop at this screen point would go: a gap in the thumbnails, or -1 for the main view.
    static int GapFor(POINTL pt) {
        Tab* t = Cur();
        if (!t || !g_showThumbs) return -1;
        POINT p = {pt.x, pt.y};
        ScreenToClient(g_thumbs, &p);
        RECT rc;
        GetClientRect(g_thumbs, &rc);
        if (!PtInRect(&rc, p)) return -1;
        ThumbAutoScroll(t, p.y);
        return GapAt(t, p.y);
    }

public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        LONG r = InterlockedDecrement(&refs);
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* d, DWORD, POINTL pt, DWORD* effect) override {
        FORMATETC fe = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
        hasFiles = SUCCEEDED(d->QueryGetData(&fe));
        return DragOver(0, pt, effect);
    }
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL pt, DWORD* effect) override {
        *effect = hasFiles ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        int gap = hasFiles ? GapFor(pt) : -1;
        if (gap != g_dropGap) { g_dropGap = gap; InvalidateRect(g_thumbs, nullptr, FALSE); }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {
        g_dropGap = -1;
        InvalidateRect(g_thumbs, nullptr, FALSE);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* d, DWORD, POINTL pt, DWORD* effect) override {
        *effect = DROPEFFECT_COPY;
        int gap = GapFor(pt);
        g_dropGap = -1;
        InvalidateRect(g_thumbs, nullptr, FALSE);
        std::vector<std::wstring> files = Files(d), pdfs, others, unknown;
        for (auto& f : files) {
            if (!CanConvert(f)) unknown.push_back(f);
            else if (IsPdfPath(f) && gap < 0) pdfs.push_back(f);
            else others.push_back(f);
        }
        SetForegroundWindow(g_main);
        // PDFs dropped on the page area open in their own tabs.
        for (auto& f : pdfs) OpenFile(f);
        // Everything else becomes pages: at the marker, after the current page, or in a new document.
        Tab* t = Cur();
        if (!others.empty()) {
            if (!t) NewDocFromFiles(others);
            else InsertFilesAt(t, gap >= 0 ? gap : CurrentPage(t) + 1, others);
        }
        if (!unknown.empty()) {
            std::wstring names;
            for (auto& f : unknown) names += f.substr(f.find_last_of(L"\\/") + 1) + L"\n";
            MessageBoxW(g_main, (L"These files can't be added. Workpaper PDF takes PDF, Word, text and image files.\n\n" + names).c_str(),
                        APP_NAME, MB_ICONINFORMATION);
        }
        return S_OK;
    }
};

// ---------------------------------------------------------------- search

static struct {
    Tab* tab = nullptr;
    std::u16string query;
    std::vector<find::Match> matches;
    int cur = -1;
    bool stale = true;
} g_find;

static void SearchChanged() {
    g_find.stale = true;
    g_find.matches.clear();
    g_find.cur = -1;
}

static std::u16string FindQuery() {
    int len = GetWindowTextLengthW(g_findEdit);
    std::wstring q(len, L'\0');
    GetWindowTextW(g_findEdit, &q[0], len + 1);
    return ToU16(q);
}

// View rectangle of a search box (a fraction of the page) on page i.
static RECT MatchRect(Tab* t, int i, const find::Box& b) {
    RECT pr = PageRect(t, i);
    double w = pr.right - pr.left, h = pr.bottom - pr.top;
    return RECT{pr.left + (int)floor(b.left * w), pr.top + (int)floor(b.bottom * h), pr.left + (int)ceil(b.right * w),
                pr.top + (int)ceil(b.top * h)};
}

static void ShowMatch(Tab* t) {
    if (g_find.cur < 0 || g_find.cur >= (int)g_find.matches.size()) return;
    auto& m = g_find.matches[g_find.cur];
    if (m.page >= t->pdf.PageCount() || m.view.empty()) return;
    RECT rc = ViewClient();
    RECT r = MatchRect(t, m.page, m.view[0]);
    int sx = t->sx, sy = t->sy;
    if (r.top < S(40) || r.bottom > rc.bottom - S(40)) sy = t->sy + r.top - rc.bottom / 3;
    if (r.left < 0 || r.right > rc.right) sx = t->sx + r.left - rc.right / 3;
    ScrollTo(t, sx, sy);
    wchar_t buf[128];
    swprintf(buf, 128, L"  Match %d of %d (page %d). Enter or F3 for the next, Shift+Enter for the previous.", g_find.cur + 1,
             (int)g_find.matches.size(), m.page + 1);
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)buf);
    InvalidateRect(g_view, nullptr, FALSE);
}

static void DoFind(bool back) {
    Tab* t = Cur();
    if (!t) return;
    CommitNote();
    std::u16string q = FindQuery();
    if (q.find_first_not_of(u" ") == std::u16string::npos) {
        SetFocus(g_findEdit);
        return;
    }
    bool fresh = g_find.tab != t || q != g_find.query || g_find.stale;
    if (fresh) {
        SetStatusText(L"Searching...");
        g_find.matches = find::FindAll(t->pdf.Handle(), q);
        SetStatusText(nullptr);
        g_find.tab = t;
        g_find.query = q;
        g_find.stale = false;
        g_find.cur = -1;
    }
    int n = (int)g_find.matches.size();
    if (!n) {
        MessageBeep(MB_ICONINFORMATION);
        SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(L"  No matches for \u201C" + ToW(q) + L"\u201D.").c_str());
        InvalidateRect(g_view, nullptr, FALSE);
        return;
    }
    if (g_find.cur < 0) {
        // Start from the page on screen.
        int page = CurrentPage(t);
        g_find.cur = back ? n - 1 : 0;
        if (back) { for (int i = n - 1; i >= 0; i--) if (g_find.matches[i].page <= page) { g_find.cur = i; break; } }
        else { for (int i = 0; i < n; i++) if (g_find.matches[i].page >= page) { g_find.cur = i; break; } }
    } else {
        g_find.cur = (g_find.cur + (back ? n - 1 : 1)) % n;
    }
    ShowMatch(t);
}

static void ForgetSearch(Tab* t) {
    if (g_find.tab == t) { SearchChanged(); g_find.tab = nullptr; }
}

static void ClearFind() {
    SearchChanged();
    g_find.tab = nullptr;
    InvalidateRect(g_view, nullptr, FALSE);
    UpdateStatus();
}

// Draws the search matches on the visible pages: all in yellow, the current one in orange.
static void PaintMatches(HDC hdc, Tab* t) {
    if (g_find.tab != t || g_find.matches.empty()) return;
    RECT rc = ViewClient();
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 110, 0};
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP one = CreateCompatibleBitmap(hdc, 2, 1);
    HGDIOBJ old = SelectObject(mem, one);
    SetPixel(mem, 0, 0, RGB(0xFF, 0xD4, 0x00));
    SetPixel(mem, 1, 0, RGB(0xFF, 0x7A, 0x00));
    for (int k = 0; k < (int)g_find.matches.size(); k++) {
        auto& m = g_find.matches[k];
        if (m.page >= t->pdf.PageCount()) continue;
        for (auto& b : m.view) {
            RECT r = MatchRect(t, m.page, b);
            if (r.bottom < 0 || r.top > rc.bottom || r.right < 0 || r.left > rc.right) continue;
            InflateRect(&r, 1, 1);
            AlphaBlend(hdc, r.left, r.top, r.right - r.left, r.bottom - r.top, mem, k == g_find.cur ? 1 : 0, 0, 1, 1, bf);
        }
    }
    SelectObject(mem, old);
    DeleteObject(one);
    DeleteDC(mem);
}

static WNDPROC g_findProc;
static LRESULT CALLBACK FindEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SETFOCUS || m == WM_KILLFOCUS) InvalidateRect(g_tool, nullptr, FALSE);
    if (m == WM_KEYDOWN && w == VK_RETURN) { DoFind(GetKeyState(VK_SHIFT) < 0); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { ClearFind(); SetFocus(g_view); return 0; }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    return CallWindowProcW(g_findProc, h, m, w, l);
}

// ---------------------------------------------------------------- side pane: pages, outline, comments

static int g_sideMode = 0;  // 0 thumbnails, 1 outline, 2 comments
static bool g_navDirty = true;
static const UINT_PTR kNavTimer = 7, kRecoveryTimer = 8;
struct NavItem { int page; std::string nm; };
static std::vector<NavItem> g_navItems;

static void NavChanged() {
    g_navDirty = true;
    if (g_sideMode && g_main) SetTimer(g_main, kNavTimer, 400, nullptr);
}

static const wchar_t* CommentKind(FPDF_ANNOTATION a, int subtype, const std::string& nm) {
    if (nm.rfind("wpp-tm", 0) == 0) return L"Tickmark";
    switch (subtype) {
    case FPDF_ANNOT_HIGHLIGHT: return L"Highlight";
    case FPDF_ANNOT_TEXT: return L"Note";
    case FPDF_ANNOT_FREETEXT: return L"Text box";
    case FPDF_ANNOT_SQUARE: return L"Box";
    case FPDF_ANNOT_CIRCLE: return L"Oval";
    case FPDF_ANNOT_INK: return L"Pen";
    case FPDF_ANNOT_STAMP: return L"Stamp";
    case FPDF_ANNOT_UNDERLINE: return L"Underline";
    case FPDF_ANNOT_STRIKEOUT: return L"Strikeout";
    case FPDF_ANNOT_LINE: return L"Line";
    case FPDF_ANNOT_POLYGON: case FPDF_ANNOT_POLYLINE: return L"Shape";
    default: (void)a; return nullptr;  // links, form fields, popups: not comments
    }
}

static void FillNavList() {
    g_navDirty = false;
    KillTimer(g_main, kNavTimer);
    SendMessageW(g_navList, WM_SETREDRAW, FALSE, 0);
    int top = (int)SendMessageW(g_navList, LB_GETTOPINDEX, 0, 0);
    SendMessageW(g_navList, LB_RESETCONTENT, 0, 0);
    g_navItems.clear();
    Tab* t = Cur();
    auto add = [](const std::wstring& s, int page, const std::string& nm) {
        SendMessageW(g_navList, LB_ADDSTRING, 0, (LPARAM)s.c_str());
        g_navItems.push_back({page, nm});
    };
    if (t && g_sideMode == 1) {
        auto marks = find::Outline(t->pdf.Handle());
        for (auto& b : marks) add(std::wstring(b.level * 3, L' ') + ToW(b.title), b.page, "");
        if (marks.empty()) add(L"This PDF has no bookmarks.", -1, "");
    } else if (t && g_sideMode == 2) {
        int n = t->pdf.PageCount();
        for (int i = 0; i < n; i++) {
            FPDF_PAGE p = FPDF_LoadPage(t->pdf.Handle(), i);
            if (!p) continue;
            int count = FPDFPage_GetAnnotCount(p);
            for (int k = 0; k < count; k++) {
                FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, k);
                if (!a) continue;
                int st = FPDFAnnot_GetSubtype(a);
                char16_t buf[512] = {0};
                std::string nm;
                if (FPDFAnnot_GetStringValue(a, "NM", (FPDF_WCHAR*)buf, sizeof(buf)) > 2)
                    for (int c = 0; buf[c]; c++) nm.push_back((char)buf[c]);
                const wchar_t* kind = CommentKind(a, st, nm);
                if (kind) {
                    std::wstring line = L"p. " + std::to_wstring(i + 1) + L"   " + kind;
                    char16_t txt[512] = {0};
                    if (FPDFAnnot_GetStringValue(a, "Contents", (FPDF_WCHAR*)txt, sizeof(txt)) > 2) {
                        std::wstring c;
                        for (int j = 0; txt[j] && j < 80; j++) c.push_back(txt[j] == u'\r' || txt[j] == u'\n' ? L' ' : (wchar_t)txt[j]);
                        if (!c.empty()) line += L": " + c;
                    }
                    add(line, i, nm);
                }
                FPDFPage_CloseAnnot(a);
            }
            FPDF_ClosePage(p);
        }
        if (g_navItems.empty()) add(L"No marks or comments yet.", -1, "");
    }
    SendMessageW(g_navList, LB_SETTOPINDEX, top, 0);
    SendMessageW(g_navList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_navList, nullptr, TRUE);
}

static void SetSideMode(int mode) {
    g_sideMode = mode;
    if (!g_showThumbs) { g_showThumbs = true; RegSetDword(nullptr, L"Thumbnails", 1); }
    RegSetDword(nullptr, L"SideMode", (DWORD)mode);
    RelayoutMain();
    if (mode) FillNavList();
    InvalidateRect(g_side, nullptr, FALSE);
}

static void SelectAnnotByName(Tab* t, int page, const std::string& nm);

static void NavPicked() {
    int i = (int)SendMessageW(g_navList, LB_GETCURSEL, 0, 0);
    Tab* t = Cur();
    if (!t || i < 0 || i >= (int)g_navItems.size() || g_navItems[i].page < 0) return;
    GoToPage(t, g_navItems[i].page);
    if (!g_navItems[i].nm.empty()) SelectAnnotByName(t, g_navItems[i].page, g_navItems[i].nm);
}

static const wchar_t* kSideNames[3] = {L"Pages", L"Outline", L"Notes"};

static LRESULT CALLBACK SideProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        FillSolid(hdc, rc, g_c.surface);
        FillSolid(hdc, RECT{rc.right - 1, 0, rc.right, rc.bottom}, g_c.border);
        SetBkMode(hdc, TRANSPARENT);
        SelectObject(hdc, g_smallFont);
        // A segmented control: a soft track with the chosen segment raised.
        RECT track = {S(10), S(8), rc.right - S(11), rc.bottom - S(4)};
        RoundRectAA(hdc, track, (float)S(9), g_c.input);
        int wEach = (track.right - track.left) / 3;
        for (int i = 0; i < 3; i++) {
            RECT r = {track.left + i * wEach, track.top, i == 2 ? track.right : track.left + (i + 1) * wEach, track.bottom};
            if (g_sideMode == i) {
                RECT seg = r;
                InflateRect(&seg, -S(3), -S(3));
                RoundRectAA(hdc, seg, (float)S(7), g_c.surface, g_c.border);
            }
            SetTextColor(hdc, g_sideMode == i ? g_c.text : g_c.text2);
            DrawTextW(hdc, kSideNames[i], -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        RECT rc;
        GetClientRect(h, &rc);
        int i = std::clamp((GET_X_LPARAM(l) - S(10)) / std::max(1, ((int)rc.right - S(21)) / 3), 0, 2);
        SetSideMode(i);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

// ---------------------------------------------------------------- printing

static void PrintDoc(Tab* t, bool withMarks) {
    if (!t) return;
    CommitNote();
    PRINTDLGW pd = {sizeof(pd)};
    pd.hwndOwner = g_main;
    pd.Flags = PD_RETURNDC | PD_ALLPAGES | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    pd.nFromPage = 1;
    pd.nToPage = (WORD)std::min(t->pdf.PageCount(), 65535);
    pd.nMinPage = 1;
    pd.nMaxPage = pd.nToPage;
    pd.nCopies = 1;
    if (!PrintDlgW(&pd)) return;
    int from = 0, to = t->pdf.PageCount() - 1;
    if (pd.Flags & PD_PAGENUMS) { from = std::max(0, pd.nFromPage - 1); to = std::min(to, pd.nToPage - 1); }
    HDC dc = pd.hDC;
    std::wstring name = t->pdf.Title();
    DOCINFOW di = {sizeof(di)};
    di.lpszDocName = name.c_str();
    if (StartDocW(dc, &di) <= 0) {
        DeleteDC(dc);
        MessageBoxW(g_main, L"The printer could not start printing.", APP_NAME, MB_ICONWARNING);
        return;
    }
    int dpiX = GetDeviceCaps(dc, LOGPIXELSX), dpiY = GetDeviceCaps(dc, LOGPIXELSY);
    int paperW = GetDeviceCaps(dc, HORZRES), paperH = GetDeviceCaps(dc, VERTRES);
    // Render at up to 300 dpi; the printer driver scales the picture up to its own resolution.
    double renderDpi = std::min(300, std::min(dpiX, dpiY));
    bool ok = true;
    for (int i = from; i <= to && ok; i++) {
        wchar_t msg[96];
        swprintf(msg, 96, L"Printing page %d of %d...", i - from + 1, to - from + 1);
        SetStatusText(msg);
        FPDF_PAGE page = FPDF_LoadPage(t->pdf.Handle(), i);
        if (!page) continue;
        double pw = FPDF_GetPageWidthF(page), ph = FPDF_GetPageHeightF(page);
        // Turn landscape pages to fit portrait paper (and the other way round).
        bool turn = (pw > ph) != (paperW > paperH);
        double w = turn ? ph : pw, hgt = turn ? pw : ph;
        // Shrink oversized pages to fit; smaller pages print at actual size.
        double fit = std::min(1.0, std::min(paperW / (w * dpiX / 72.0), paperH / (hgt * dpiY / 72.0)));
        int outW = (int)(w * dpiX / 72.0 * fit), outH = (int)(hgt * dpiY / 72.0 * fit);
        int bw = std::max(1, (int)(w * renderDpi / 72.0 * fit)), bh = std::max(1, (int)(hgt * renderDpi / 72.0 * fit));
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = bw;
        bi.bmiHeader.biHeight = -bh;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        std::vector<uint8_t> bits;
        try { bits.resize((size_t)bw * bh * 4); } catch (...) { FPDF_ClosePage(page); ok = false; break; }
        FPDF_BITMAP fb = FPDFBitmap_CreateEx(bw, bh, FPDFBitmap_BGRx, bits.data(), bw * 4);
        FPDFBitmap_FillRect(fb, 0, 0, bw, bh, 0xFFFFFFFF);
        FPDF_RenderPageBitmap(fb, page, 0, 0, bw, bh, turn ? 1 : 0, FPDF_PRINTING | (withMarks ? FPDF_ANNOT : 0));
        FPDFBitmap_Destroy(fb);
        FPDF_ClosePage(page);
        if (StartPage(dc) <= 0) { ok = false; break; }
        int x = (paperW - outW) / 2, y = (paperH - outH) / 2;
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, nullptr);
        StretchDIBits(dc, x, y, outW, outH, 0, 0, bw, bh, bits.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
        if (EndPage(dc) <= 0) ok = false;
    }
    if (ok) EndDoc(dc);
    else AbortDoc(dc);
    DeleteDC(dc);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    SetStatusText(nullptr);
    if (!ok) MessageBoxW(g_main, L"Printing stopped part way. Check the printer and try again.", APP_NAME, MB_ICONWARNING);
}

// ---------------------------------------------------------------- auto-recovery

// Unsaved changes are copied to %LOCALAPPDATA%\WorkpaperPDF\Recovery every minute, and offered back after a crash.
static std::wstring RecoveryDir() {
    PWSTR base = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
        dir = std::wstring(base) + L"\\WorkpaperPDF";
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\Recovery";
        CreateDirectoryW(dir.c_str(), nullptr);
    }
    if (base) CoTaskMemFree(base);
    return dir;
}

static bool WriteWhole(const std::wstring& path, const void* data, size_t size) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)size, &wrote, nullptr) && wrote == size;
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

static void DropRecovery(Tab* t) {
    if (!t || t->recId.empty()) return;
    std::wstring dir = RecoveryDir();
    DeleteFileW((dir + L"\\" + t->recId + L".pdf").c_str());
    DeleteFileW((dir + L"\\" + t->recId + L".txt").c_str());
    t->recGen = 0;
}

static void SaveRecovery() {
    std::wstring dir;
    for (auto& d : g_docs) {
        Tab* t = d.get();
        if (!t->dirty) { if (t->recGen) DropRecovery(t); continue; }
        if (t->gen == t->recGen) continue;
        if (dir.empty()) dir = RecoveryDir();
        if (dir.empty()) return;
        if (t->recId.empty()) {
            static int serial = 0;
            wchar_t id[64];
            swprintf(id, 64, L"%08lx-%lu-%d", (unsigned long)GetTickCount(), GetCurrentProcessId(), ++serial);
            t->recId = id;
        }
        std::vector<uint8_t> bytes;
        if (!ops::SaveToBuffer(t->pdf.Handle(), bytes)) continue;
        std::wstring info = t->pdf.Path() + L"\n" + t->pdf.Title();
        if (WriteWhole(dir + L"\\" + t->recId + L".pdf", bytes.data(), bytes.size()) &&
            WriteWhole(dir + L"\\" + t->recId + L".txt", info.c_str(), info.size() * sizeof(wchar_t)))
            t->recGen = t->gen;
    }
}

static void OfferRecovery() {
    std::wstring dir = RecoveryDir();
    if (dir.empty()) return;
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"\\*.pdf").c_str(), &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> ids;
    do {
        std::wstring n = fd.cFileName;
        ids.push_back(n.substr(0, n.size() - 4));
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    if (ids.empty()) return;
    wchar_t q[256];
    swprintf(q, 256, L"Workpaper PDF closed before %d file%s %s saved.\n\nOpen the recovered cop%s now? "
                     L"(No discards them.)", (int)ids.size(), ids.size() == 1 ? L"" : L"s", ids.size() == 1 ? L"was" : L"were",
             ids.size() == 1 ? L"y" : L"ies");
    bool open = MessageBoxW(g_main, q, APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES;
    for (auto& id : ids) {
        std::wstring pdf = dir + L"\\" + id + L".pdf", txt = dir + L"\\" + id + L".txt";
        if (open) {
            std::wstring path, title = L"Recovered";
            HANDLE h = CreateFileW(txt.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                std::wstring info(4096, L'\0');
                DWORD got = 0;
                ReadFile(h, &info[0], (DWORD)(info.size() * sizeof(wchar_t)), &got, nullptr);
                CloseHandle(h);
                info.resize(got / sizeof(wchar_t));
                size_t nl = info.find(L'\n');
                path = info.substr(0, nl);
                if (nl != std::wstring::npos) title = info.substr(nl + 1);
            }
            HANDLE h2 = CreateFileW(pdf.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h2 != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER len = {};
                GetFileSizeEx(h2, &len);
                auto bytes = std::make_shared<std::vector<uint8_t>>((size_t)len.QuadPart);
                DWORD got = 0;
                ReadFile(h2, bytes->data(), (DWORD)bytes->size(), &got, nullptr);
                CloseHandle(h2);
                auto t = std::make_unique<Tab>();
                if (got == bytes->size() && t->pdf.OpenMemory(bytes, path, title + L" (recovered)")) {
                    t->dirty = true;
                    t->gen = 1;
                    g_docs.push_back(std::move(t));
                    RebuildTabStrip();
                    if (g_docs.size() == 1) RelayoutMain();
                    SelectTab((int)g_docs.size() - 1);
                }
            }
        }
        DeleteFileW(pdf.c_str());
        DeleteFileW(txt.c_str());
    }
    // The reopened copies are written back to the folder on the next timer tick.
}

// ---------------------------------------------------------------- toolbar

struct ToolButton {
    int id;              // command; 0 = page box, -1 = divider, -2 = zoom readout, -3 = search box
    const wchar_t* glyph;  // Segoe Fluent Icons / Segoe MDL2 Assets
    const wchar_t* tip;
    int align;           // 0 left, 1 centre, 2 right
    RECT rc;
    float hover;         // 0..1, animated
};
static ToolButton g_buttons[] = {
    {ID_MENU, L"\xE700", L"Menu (Alt)", 0, {}, 0},
    {-1, nullptr, nullptr, 0, {}, 0},
    {ID_OPEN, L"\xE8E5", L"Open (Ctrl+O)", 0, {}, 0},
    {ID_SAVE, L"\xE74E", L"Save (Ctrl+S)", 0, {}, 0},
    {ID_PRINT, L"\xE749", L"Print (Ctrl+P)", 0, {}, 0},
    {-1, nullptr, nullptr, 0, {}, 0},
    {ID_UNDO, L"\xE7A7", L"Undo (Ctrl+Z)", 0, {}, 0},
    {ID_REDO, L"\xE7A6", L"Redo (Ctrl+Y)", 0, {}, 0},
    {ID_PREVPAGE, L"\xE76B", L"Previous page", 1, {}, 0},
    {0, nullptr, L"Page number: type one and press Enter (Ctrl+G)", 1, {}, 0},
    {ID_NEXTPAGE, L"\xE76C", L"Next page", 1, {}, 0},
    {-1, nullptr, nullptr, 1, {}, 0},
    {ID_ZOOMOUT, L"\xE71F", L"Zoom out (Ctrl+minus)", 1, {}, 0},
    {-2, nullptr, L"Zoom: fit width, fit page or a set size", 1, {}, 0},
    {ID_ZOOMIN, L"\xE8A3", L"Zoom in (Ctrl+plus)", 1, {}, 0},
    {ID_ROTR, L"\xE7AD", L"Rotate selected pages (Ctrl+R)", 2, {}, 0},
    {ID_INSERTFILE, L"\xE8F4", L"Insert pages from files", 2, {}, 0},
    {ID_DEL, L"\xE74D", L"Delete selected pages (Del)", 2, {}, 0},
    {-1, nullptr, nullptr, 2, {}, 0},
    {-3, nullptr, nullptr, 2, {}, 0},
    {-1, nullptr, nullptr, 2, {}, 0},
    {ID_THUMBS, L"\xE8A9", L"Side pane: pages, bookmarks, comments (F4)", 2, {}, 0},
    {ID_MARKSPANE, L"\xE70F", L"Marks and tools pane (F6)", 2, {}, 0},
};
static const int kButtonCount = (int)(sizeof(g_buttons) / sizeof(g_buttons[0]));
static int g_hot = -1, g_pressedBtn = -1;
static RECT g_pageBoxRect, g_findRect;
static HWND g_tip;
static const UINT_PTR kHoverTimer = 1;

static int ButtonWidth(const ToolButton& b, int searchW) {
    if (b.id == -1) return S(17);
    if (b.id == 0) return S(100);
    if (b.id == -2) return S(62);
    if (b.id == -3) return searchW;
    return S(38);
}

static void LayoutToolbar() {
    RECT rc;
    GetClientRect(g_tool, &rc);
    int h = rc.bottom, gap = S(2);
    auto groupW = [&](int align, int searchW) {
        int w = 0;
        for (auto& b : g_buttons) if (b.align == align) w += ButtonWidth(b, searchW) + gap;
        return w;
    };
    int searchW = S(260);
    int lw = groupW(0, 0), cw = groupW(1, 0);
    // The search box gives way first when the window is narrow.
    while (searchW > S(130) && lw + cw + groupW(2, searchW) + S(60) > rc.right) searchW -= S(10);
    int rw = groupW(2, searchW);
    int leftEnd = S(10) + lw, rightStart = rc.right - S(10) - rw;
    int cx = std::max(leftEnd + S(16), std::min<int>((rc.right - cw) / 2, rightStart - S(16) - cw));
    int xl = S(10), xc = cx, xr = rightStart;
    int top = (h - S(38)) / 2;
    for (auto& b : g_buttons) {
        int& x = b.align == 0 ? xl : b.align == 1 ? xc : xr;
        int w = ButtonWidth(b, searchW);
        b.rc = {x, top, x + w, top + S(38)};
        if (b.id == 0) {
            g_pageBoxRect = b.rc;
            InflateRect(&g_pageBoxRect, 0, -S(3));
            MoveWindow(g_pageEdit, b.rc.left + S(10), h / 2 - S(10), S(38), S(20), TRUE);
        }
        if (b.id == -3) {
            g_findRect = b.rc;
            InflateRect(&g_findRect, 0, -S(2));
            if (g_findEdit) MoveWindow(g_findEdit, b.rc.left + S(36), h / 2 - S(10), w - S(46), S(20), TRUE);
        }
        x += w + gap;
    }
    // Tooltips follow the buttons.
    if (g_tip) {
        for (int i = 0; i < kButtonCount; i++) {
            TOOLINFOW ti = {sizeof(ti)};
            ti.hwnd = g_tool;
            ti.uId = i + 1;
            SendMessageW(g_tip, TTM_DELTOOLW, 0, (LPARAM)&ti);
            if (!g_buttons[i].tip) continue;
            ti.uFlags = TTF_SUBCLASS;
            ti.rect = g_buttons[i].rc;
            ti.lpszText = (LPWSTR)g_buttons[i].tip;
            SendMessageW(g_tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        }
    }
}

static void ShowAppMenu() {
    RECT r = g_buttons[0].rc;
    POINT p = {r.left, r.bottom + S(4)};
    ClientToScreen(g_tool, &p);
    TrackPopupMenuEx(g_menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, p.x, p.y, g_main, nullptr);
}

static const int kZoomPresets[] = {50, 75, 100, 125, 150, 200, 300, 400};

static void ShowZoomMenu(const RECT& r) {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_FITWIDTH, L"Fit width\tCtrl+1");
    AppendMenuW(m, MF_STRING, ID_FITPAGE, L"Fit page\tCtrl+2");
    AppendMenuW(m, MF_STRING, ID_ZOOM100, L"Actual size\tCtrl+0");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    for (int k = 0; k < (int)(sizeof(kZoomPresets) / sizeof(kZoomPresets[0])); k++)
        AppendMenuW(m, MF_STRING, ID_ZOOMPRESET + k, (std::to_wstring(kZoomPresets[k]) + L"%").c_str());
    POINT p = {r.left, r.bottom + S(4)};
    ClientToScreen(g_tool, &p);
    TrackPopupMenuEx(m, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, p.x, p.y, g_main, nullptr);
    DestroyMenu(m);
}

static bool ButtonEnabled(const ToolButton& b) {
    if (Cur()) return true;
    return b.id == ID_MENU || b.id == ID_OPEN || b.id == ID_THUMBS || b.id == ID_MARKSPANE || b.id == -3;
}

static LRESULT CALLBACK ToolProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0, h,
                                nullptr, g_inst, nullptr);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC buf = CreateCompatibleDC(hdc);
        HBITMAP bb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ oldb = SelectObject(buf, bb);
        FillSolid(buf, rc, g_c.surface);
        FillSolid(buf, RECT{0, rc.bottom - 1, rc.right, rc.bottom}, g_c.border);
        SetBkMode(buf, TRANSPARENT);
        Tab* t = Cur();
        for (int i = 0; i < kButtonCount; i++) {
            auto& b = g_buttons[i];
            if (b.id == -1) {
                int x = (b.rc.left + b.rc.right) / 2;
                FillSolid(buf, RECT{x, b.rc.top + S(9), x + 1, b.rc.bottom - S(9)}, g_c.border);
                continue;
            }
            if (b.id == 0) {
                RoundRectAA(buf, g_pageBoxRect, (float)S(8), g_c.input);
                if (t) {
                    wchar_t of[32];
                    swprintf(of, 32, L"/ %d", t->pdf.PageCount());
                    RECT r = {g_pageBoxRect.left + S(50), g_pageBoxRect.top, g_pageBoxRect.right - S(6), g_pageBoxRect.bottom};
                    SelectObject(buf, g_font);
                    SetTextColor(buf, g_c.muted);
                    DrawTextW(buf, of, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                }
                continue;
            }
            if (b.id == -3) {
                bool focus = GetFocus() == g_findEdit;
                RoundRectAA(buf, g_findRect, (float)S(10), g_c.input, focus ? g_c.accent : CLR_INVALID, 1.5f);
                RECT g = {g_findRect.left + S(10), g_findRect.top, g_findRect.left + S(32), g_findRect.bottom};
                SelectObject(buf, g_iconFont);
                SetTextColor(buf, focus ? g_c.accent : g_c.muted);
                DrawTextW(buf, L"\xE721", -1, &g, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                continue;
            }
            bool enabled = ButtonEnabled(b);
            bool active = (b.id == ID_THUMBS && g_showThumbs) || (b.id == ID_MARKSPANE && g_showMarks);
            COLORREF fill = CLR_INVALID;
            if (active) fill = g_c.accentSoft;
            if (enabled && b.hover > 0.01f) fill = Mix(active ? g_c.accentSoft : g_c.surface, active ? g_c.accentSoft : g_c.hover, b.hover);
            if (enabled && i == g_pressedBtn && i == g_hot) fill = g_c.pressed;
            RECT br = b.rc;
            if (fill != CLR_INVALID) RoundRectAA(buf, br, (float)S(8), fill);
            SetTextColor(buf, !enabled ? Mix(g_c.muted, g_c.surface, 0.45) : active ? g_c.accent : g_c.text);
            if (b.id == -2) {
                wchar_t z[16];
                swprintf(z, 16, L"%d%%", t ? (int)lround(t->zoom * 100) : 100);
                SelectObject(buf, g_font);
                DrawTextW(buf, z, -1, &br, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
                continue;
            }
            SelectObject(buf, g_iconFont);
            DrawTextW(buf, b.glyph, -1, &br, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        BitBlt(hdc, 0, 0, rc.right, rc.bottom, buf, 0, 0, SRCCOPY);
        SelectObject(buf, oldb);
        DeleteObject(bb);
        DeleteDC(buf);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        LayoutToolbar();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_TIMER:
        if (w == kHoverTimer) {
            // Hover fades in and out over about 100 ms.
            bool moving = false;
            for (int i = 0; i < kButtonCount; i++) {
                float target = i == g_hot ? 1.0f : 0.0f;
                float& v = g_buttons[i].hover;
                if (std::fabs(v - target) > 0.01f) {
                    v += (target - v) * 0.35f + (target > v ? 0.05f : -0.05f);
                    v = std::clamp(v, 0.0f, 1.0f);
                    moving = true;
                } else v = target;
            }
            InvalidateRect(h, nullptr, FALSE);
            if (!moving) KillTimer(h, kHoverTimer);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int hot = -1;
        for (int i = 0; i < kButtonCount; i++)
            if (g_buttons[i].id != -1 && g_buttons[i].id != 0 && g_buttons[i].id != -3 && PtInRect(&g_buttons[i].rc, p)) hot = i;
        if (hot != g_hot) {
            g_hot = hot;
            SetTimer(h, kHoverTimer, 15, nullptr);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hot = -1;
        SetTimer(h, kHoverTimer, 15, nullptr);
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (PtInRect(&g_findRect, p)) { SetFocus(g_findEdit); return 0; }
        if (PtInRect(&g_pageBoxRect, p) && Cur()) { SetFocus(g_pageEdit); SendMessageW(g_pageEdit, EM_SETSEL, 0, -1); return 0; }
        g_pressedBtn = g_hot;
        if (g_hot >= 0) { SetCapture(h); InvalidateRect(h, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONUP: {
        int i = g_pressedBtn;
        g_pressedBtn = -1;
        if (GetCapture() == h) ReleaseCapture();
        InvalidateRect(h, nullptr, FALSE);
        if (i < 0 || i != g_hot || !ButtonEnabled(g_buttons[i])) return 0;
        if (g_buttons[i].id == ID_MENU) ShowAppMenu();
        else if (g_buttons[i].id == -2) ShowZoomMenu(g_buttons[i].rc);
        else SendMessageW(g_main, WM_COMMAND, g_buttons[i].id, 0);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)w, g_c.text);
        SetBkColor((HDC)w, g_c.input);
        return (LRESULT)g_inputBrush;
    case WM_COMMAND:
        if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static WNDPROC g_editProc;
static LRESULT CALLBACK PageEditProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_KEYDOWN && w == VK_RETURN) {
        wchar_t buf[16];
        GetWindowTextW(h, buf, 16);
        int p = _wtoi(buf);
        SetFocus(g_view);
        if (Cur() && p > 0) GoToPage(Cur(), p - 1);
        UpdateStatus();
        return 0;
    }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { SetFocus(g_view); UpdateStatus(); return 0; }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    if (m == WM_KILLFOCUS) { LRESULT r = CallWindowProcW(g_editProc, h, m, w, l); UpdateStatus(); return r; }
    return CallWindowProcW(g_editProc, h, m, w, l);
}

// ---------------------------------------------------------------- main window

static int TabStripHeight() {
    if (g_docs.empty()) return 0;
    RECT r;
    (void)r;
    return S(44);
    return S(28);
}

static void RelayoutMain() {
    RECT rc;
    GetClientRect(g_main, &rc);
    int statusH = S(30);
    MoveWindow(g_status, 0, rc.bottom - statusH, rc.right, statusH, TRUE);
    int toolH = S(54);
    MoveWindow(g_tool, 0, 0, rc.right, toolH, TRUE);
    int tabH = TabStripHeight();
    ShowWindow(g_tabs, tabH ? SW_SHOW : SW_HIDE);
    MoveWindow(g_tabs, 0, toolH, rc.right, tabH, TRUE);
    int y = toolH + tabH;
    int h = std::max(0, (int)rc.bottom - statusH - y);
    // The start screen has the window to itself; the side panes come back with the first file.
    bool hasDoc = !g_docs.empty();
    bool showThumbs = g_showThumbs && hasDoc, showMarks = g_showMarks && hasDoc;
    int thumbW = showThumbs ? (g_sideMode ? S(250) : S(212)) : 0;
    int sideH = S(46);
    ShowWindow(g_side, showThumbs ? SW_SHOW : SW_HIDE);
    MoveWindow(g_side, 0, y, thumbW, sideH, TRUE);
    ShowWindow(g_thumbs, showThumbs && g_sideMode == 0 ? SW_SHOW : SW_HIDE);
    ShowWindow(g_navList, showThumbs && g_sideMode != 0 ? SW_SHOW : SW_HIDE);
    MoveWindow(g_thumbs, 0, y + sideH, thumbW, std::max(0, h - sideH), TRUE);
    MoveWindow(g_navList, 0, y + sideH, thumbW, std::max(0, h - sideH), TRUE);
    if (showThumbs && g_sideMode && g_navDirty) FillNavList();
    int marksW = showMarks ? S(232) : 0;
    ShowWindow(g_marks, showMarks ? SW_SHOW : SW_HIDE);
    MoveWindow(g_marks, rc.right - marksW, y, marksW, h, TRUE);
    MoveWindow(g_view, thumbW, y, rc.right - thumbW - marksW, h, TRUE);
}

static void CreateFonts() {
    if (g_font) DeleteObject(g_font);
    if (g_iconFont) DeleteObject(g_iconFont);
    if (g_bigFont) DeleteObject(g_bigFont);
    for (HFONT f : {g_smallFont, g_headFont, g_titleFont, g_btnFont, g_logoFont}) if (f) DeleteObject(f);
    auto exists = [](const wchar_t* face) {
        HDC dc = GetDC(nullptr);
        LOGFONTW lf = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, face);
        bool found = false;
        EnumFontFamiliesExW(dc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int { *(bool*)p = true; return 0; }, (LPARAM)&found, 0);
        ReleaseDC(nullptr, dc);
        return found;
    };
    // Windows 11's Segoe UI Variable where it exists; Segoe UI otherwise.
    const wchar_t* ui = exists(L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
    const wchar_t* disp = exists(L"Segoe UI Variable Display") ? L"Segoe UI Variable Display" : L"Segoe UI";
    auto mk = [](int px, int weight, const wchar_t* face) {
        return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    };
    g_font = mk(S(14), FW_NORMAL, ui);
    g_smallFont = mk(S(13), FW_NORMAL, ui);
    g_headFont = mk(S(11), FW_SEMIBOLD, ui);
    g_btnFont = mk(S(15), FW_SEMIBOLD, ui);
    g_bigFont = mk(S(22), FW_SEMIBOLD, disp);
    g_titleFont = mk(S(32), FW_SEMIBOLD, disp);
    // Segoe Fluent Icons on Windows 11, Segoe MDL2 Assets on Windows 10.
    const wchar_t* face = L"Segoe Fluent Icons";
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcscpy_s(lf.lfFaceName, face);
    bool found = false;
    EnumFontFamiliesExW(hdc, &lf, [](const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM p) -> int { *(bool*)p = true; return 0; }, (LPARAM)&found, 0);
    ReleaseDC(nullptr, hdc);
    if (!found) face = L"Segoe MDL2 Assets";
    g_iconFace = face;
    g_iconFont = CreateFontW(-S(16), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    g_logoFont = CreateFontW(-S(32), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    if (g_pageEdit) SendMessageW(g_pageEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_findEdit) SendMessageW(g_findEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_navList) SendMessageW(g_navList, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_navList) SendMessageW(g_navList, LB_SETITEMHEIGHT, 0, S(34));
}

static void SavePlacement() {
    WINDOWPLACEMENT wp = {sizeof(wp)};
    GetWindowPlacement(g_main, &wp);
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == 0) {
        RegSetValueExW(k, L"Window", 0, REG_BINARY, (BYTE*)&wp, sizeof(wp));
        DWORD th = g_showThumbs;
        RegSetValueExW(k, L"Thumbnails", 0, REG_DWORD, (BYTE*)&th, sizeof(th));
        RegCloseKey(k);
    }
}

static void ShowShortcuts() {
    MessageBoxW(g_main,
                L"Open file\tCtrl+O\n"
                L"Close tab\tCtrl+W\n"
                L"Next / previous tab\tCtrl+Tab / Ctrl+Shift+Tab\n"
                L"Go to page\tCtrl+G\n"
                L"Next / previous page\tRight / Left arrow\n"
                L"Scroll a screen\tPage Down / Page Up, Space\n"
                L"First / last page\tHome / End\n"
                L"Zoom in / out\tCtrl+plus / Ctrl+minus, Ctrl+wheel\n"
                L"Actual size\tCtrl+0\n"
                L"Fit width / fit page\tCtrl+1 / Ctrl+2\n"
                L"Side pane\tF4\n"
                L"Marks pane\tF6\n"
                L"Tickmark / number / letter\tK / N / L\n"
                L"Prepared by / reviewed by stamp\tP / R\n"
                L"Highlight / select / hand\tH / S / Esc\n"
                L"Add text / edit text\tT / E\n"
                L"Search / next / previous\tCtrl+F / F3 / Shift+F3\n"
                L"Print\tCtrl+P\n"
                L"Delete selected mark\tDel\n"
                L"Save / Save as\tCtrl+S / Ctrl+Shift+S\n"
                L"Undo / redo\tCtrl+Z / Ctrl+Y\n"
                L"Rotate pages\tCtrl+R / Ctrl+Shift+R\n"
                L"Duplicate pages\tCtrl+D\n"
                L"Delete pages\tDel (in thumbnails)\n"
                L"Select pages\tClick, Ctrl+click, Shift+click\n"
                L"Move pages\tDrag thumbnails\n"
                L"Add pages\tDrop files on the thumbnails\n"
                L"Pan\tDrag with the mouse",
                L"Keyboard shortcuts", MB_OK);
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_main = h;
        g_dpi = GetDpiForWindow(h);
        CreateFonts();
        g_tool = CreateWindowExW(0, kToolClass, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0, h, nullptr, g_inst, nullptr);
        g_pageEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, g_tool, (HMENU)(INT_PTR)ID_PAGEEDIT, g_inst, nullptr);
        g_editProc = (WNDPROC)SetWindowLongPtrW(g_pageEdit, GWLP_WNDPROC, (LONG_PTR)PageEditProc);
        g_findEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 0, 0, 0, 0, g_tool,
                                     (HMENU)(INT_PTR)ID_FINDEDIT, g_inst, nullptr);
        SendMessageW(g_findEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search");
        g_findProc = (WNDPROC)SetWindowLongPtrW(g_findEdit, GWLP_WNDPROC, (LONG_PTR)FindEditProc);
        g_side = CreateWindowExW(0, kSideClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, nullptr, g_inst, nullptr);
        g_navList = CreateWindowExW(0, L"LISTBOX", nullptr,
                                    WS_CHILD | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED,
                                    0, 0, 0, 0, h, (HMENU)(INT_PTR)ID_NAVLIST, g_inst, nullptr);
        SetTimer(h, kRecoveryTimer, 60 * 1000, nullptr);
        g_tabs = CreateWindowExW(0, kTabsClass, nullptr, WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0, 0, h,
                                 (HMENU)(INT_PTR)ID_TABS, g_inst, nullptr);
        g_thumbs = CreateWindowExW(0, kThumbClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, nullptr, g_inst, nullptr);
        g_marks = CreateWindowExW(0, kMarksClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, nullptr, g_inst, nullptr);
        g_view = CreateWindowExW(0, kViewClass, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 0, 0, 0, 0, h,
                                 nullptr, g_inst, nullptr);
        g_status = CreateWindowExW(0, kStatusClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h,
                                   (HMENU)(INT_PTR)ID_STATUS, g_inst, nullptr);
        CreateFonts();
        ApplyTheme();
        RegisterDragDrop(h, new DropTarget());
        RelayoutMain();
        UpdateStatus();
        return 0;
    }
    case WM_SIZE:
        RelayoutMain();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mi = (MINMAXINFO*)l;
        mi->ptMinTrackSize = POINT{S(860), S(560)};
        return 0;
    }
    case WM_APP_CLOSETAB:
        CloseTab((int)w);
        return 0;
    case WM_SYSCOMMAND:
        // Alt (or F10) opens the app menu, since there is no menu bar.
        if ((w & 0xFFF0) == SC_KEYMENU) { ShowAppMenu(); return 0; }
        break;
    case WM_SETTINGCHANGE:
        if (l && wcscmp((const wchar_t*)l, L"ImmersiveColorSet") == 0 && g_themePref == 0 && g_themeOverride < 0) ApplyTheme();
        break;
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)l;
        if (mi->CtlID == ID_NAVLIST) { mi->itemHeight = S(34); return TRUE; }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)l;
        if (di->CtlID != ID_NAVLIST) break;
        RECT r = di->rcItem;
        FillSolid(di->hDC, r, g_c.surface);
        if (di->itemID == (UINT)-1) return TRUE;
        RECT card = r;
        InflateRect(&card, -S(8), -S(2));
        if (di->itemState & ODS_SELECTED) RoundRectAA(di->hDC, card, (float)S(8), g_c.accentSoft);
        int len = (int)SendMessageW(di->hwndItem, LB_GETTEXTLEN, di->itemID, 0);
        std::wstring text(std::max(0, len), L'\0');
        if (len > 0) SendMessageW(di->hwndItem, LB_GETTEXT, di->itemID, (LPARAM)&text[0]);
        SetBkMode(di->hDC, TRANSPARENT);
        SelectObject(di->hDC, g_smallFont);
        RECT tr = {card.left + S(10), card.top, card.right - S(8), card.bottom};
        // "p. 3   Tickmark: note" draws the page as a small badge.
        if (text.rfind(L"p. ", 0) == 0) {
            size_t sp = text.find(L"   ");
            std::wstring pg = text.substr(3, sp == std::wstring::npos ? std::wstring::npos : sp - 3);
            RECT badge = {tr.left, card.top + S(6), tr.left + S(34), card.bottom - S(6)};
            RoundRectAA(di->hDC, badge, (float)S(6), g_c.input);
            SetTextColor(di->hDC, g_c.text2);
            DrawTextW(di->hDC, pg.c_str(), -1, &badge, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            text = sp == std::wstring::npos ? L"" : text.substr(sp + 3);
            tr.left = badge.right + S(10);
        }
        bool empty = di->itemID < g_navItems.size() && g_navItems[di->itemID].page < 0;
        SetTextColor(di->hDC, empty ? g_c.muted : (di->itemState & ODS_SELECTED) ? g_c.accent : g_c.text);
        DrawTextW(di->hDC, text.c_str(), -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
        return TRUE;
    }
    case WM_CTLCOLORLISTBOX:
        return (LRESULT)g_surfaceBrush;
    case WM_TIMER:
        if (w == kNavTimer) { KillTimer(h, kNavTimer); if (g_sideMode) FillNavList(); }
        if (w == kRecoveryTimer) SaveRecovery();
        return 0;
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        CreateFonts();
        RECT* r = (RECT*)l;
        SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        RelayoutMain();
        LayoutToolbar();
        if (Cur()) { Layout(Cur()); LayoutThumbs(Cur()); Refresh(false); RequestRender(); }
        return 0;
    }
    case WM_SETFOCUS:
        SetFocus(g_view);
        return 0;
    case WM_NOTIFY: {
        NMHDR* n = (NMHDR*)l;
        if (n->hwndFrom == g_tabs && n->code == TCN_SELCHANGE) SelectTab(TabCtrl_GetCurSel(g_tabs));
        return 0;
    }
    case WM_PARENTNOTIFY:
        if (LOWORD(w) == WM_MBUTTONDOWN) {
            POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            MapWindowPoints(h, g_tabs, &p, 1);
            TCHITTESTINFO hti = {p, 0};
            int i = TabCtrl_HitTest(g_tabs, &hti);
            if (i >= 0) CloseTab(i);
        }
        return 0;
    case WM_COPYDATA: {
        // A second copy of the app was started with a file: open it here.
        COPYDATASTRUCT* cd = (COPYDATASTRUCT*)l;
        if (cd->dwData == 1 && cd->cbData >= sizeof(wchar_t)) {
            std::wstring p((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t));
            while (!p.empty() && p.back() == 0) p.pop_back();
            OpenFile(p);
            if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
            SetForegroundWindow(h);
        }
        return TRUE;
    }
    case WM_COMMAND: {
        Tab* t = Cur();
        switch (LOWORD(w)) {
        case ID_OPEN: OpenDialog(); break;
        case ID_CLOSETAB: CloseTab(g_cur); break;
        case ID_EXIT: SendMessageW(h, WM_CLOSE, 0, 0); break;
        case ID_ZOOMIN: ZoomStep(t, 1, nullptr); break;
        case ID_ZOOMOUT: ZoomStep(t, -1, nullptr); break;
        case ID_ZOOM100: SetZoom(t, 1.0, 0, nullptr); break;
        case ID_ZOOMPRESET: case ID_ZOOMPRESET + 1: case ID_ZOOMPRESET + 2: case ID_ZOOMPRESET + 3:
        case ID_ZOOMPRESET + 4: case ID_ZOOMPRESET + 5: case ID_ZOOMPRESET + 6: case ID_ZOOMPRESET + 7:
            SetZoom(t, kZoomPresets[LOWORD(w) - ID_ZOOMPRESET] / 100.0, 0, nullptr);
            break;
        case ID_FITWIDTH: SetZoom(t, t ? t->zoom : 1, 1, nullptr); break;
        case ID_FITPAGE:
            if (t) { SetZoom(t, t->zoom, 2, nullptr); GoToPage(t, CurrentPage(t)); }
            break;
        case ID_THUMBS:
            g_showThumbs = !g_showThumbs;
            RelayoutMain();
            InvalidateRect(g_tool, nullptr, FALSE);
            break;
        case ID_GOTO:
            if (t) { SetFocus(g_pageEdit); SendMessageW(g_pageEdit, EM_SETSEL, 0, -1); }
            break;
        case ID_NEXTTAB: if (g_docs.size() > 1) SelectTab((g_cur + 1) % (int)g_docs.size()); break;
        case ID_PREVTAB: if (g_docs.size() > 1) SelectTab((g_cur + (int)g_docs.size() - 1) % (int)g_docs.size()); break;
        case ID_PREVPAGE: if (t) GoToPage(t, CurrentPage(t) - 1); break;
        case ID_NEXTPAGE: if (t) GoToPage(t, CurrentPage(t) + 1); break;
        case ID_FIRSTPAGE: if (t) GoToPage(t, 0); break;
        case ID_LASTPAGE: if (t) GoToPage(t, t->pdf.PageCount() - 1); break;
        case ID_SHORTCUTS: ShowShortcuts(); break;
        case ID_MARKSPANE:
            g_showMarks = !g_showMarks;
            SaveMarkSettings();
            RelayoutMain();
            InvalidateRect(g_tool, nullptr, FALSE);
            break;
        case ID_TOOL_HAND: SetTool(kHand); break;
        case ID_TOOL_SELECT: SetTool(kSelect); break;
        case ID_TOOL_TICK: g_ms.kind = marks::kSymbol; SetTool(kTick); break;
        case ID_TOOL_NUMBER: g_ms.kind = marks::kNumber; SetTool(kTick); break;
        case ID_TOOL_LETTER: g_ms.kind = marks::kLetter; SetTool(kTick); break;
        case ID_TOOL_PREPARED: case ID_TOOL_REVIEWED:
            g_ms.kind = LOWORD(w) == ID_TOOL_PREPARED ? marks::kPrepared : marks::kReviewed;
            SetTool(kTick);
            if (SignName().empty()) AskSignName();
            break;
        case ID_SIGNNAME: AskSignName(); break;
        case ID_TOOL_HIGHLIGHT: SetTool(kHighlight); break;
        case ID_TOOL_NOTE: SetTool(kNote); break;
        case ID_TOOL_BOX: SetTool(kBox); break;
        case ID_TOOL_OVAL: SetTool(kOval); break;
        case ID_TOOL_PEN: SetTool(kPen); break;
        case ID_TOOL_ADDTEXT: SetTool(kAddText); break;
        case ID_EDITLINE: g_ms.editPara = 0; SaveMarkSettings(); SetTool(kEditText); break;
        case ID_EDITPARA: g_ms.editPara = 1; SaveMarkSettings(); SetTool(kEditText); break;
        case ID_GUIDE: {
            wchar_t exe[MAX_PATH * 2];
            GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
            std::wstring dir = exe;
            dir = dir.substr(0, dir.find_last_of(L"\\"));
            std::wstring guide = dir + L"\\README.txt";
            if (GetFileAttributesW(guide.c_str()) == INVALID_FILE_ATTRIBUTES)
                MessageBoxW(h, L"The guide (README.txt) wasn't found next to the app. Ask whoever gave you the installer for a copy.", APP_NAME, MB_ICONINFORMATION);
            else
                ShellExecuteW(h, L"open", L"notepad.exe", (L"\"" + guide + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
            break;
        }
        case ID_DEFAULTAPP:
            MessageBoxW(h, L"Windows asks you to choose the default app yourself.\n\nOn the page that opens, set .pdf to Workpaper PDF. "
                           L"(If it isn't listed, choose \u201CChoose an app on your PC\u201D, or right-click any PDF > Open with > "
                           L"Choose another app > Workpaper PDF > Always.)",
                        APP_NAME, MB_ICONINFORMATION);
            if ((INT_PTR)ShellExecuteW(h, L"open", L"ms-settings:defaultapps?registeredAppUser=Workpaper%20PDF", nullptr, nullptr, SW_SHOWNORMAL) <= 32)
                ShellExecuteW(h, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case ID_FIND: SetFocus(g_findEdit); SendMessageW(g_findEdit, EM_SETSEL, 0, -1); break;
        case ID_MENU: ShowAppMenu(); break;
        case ID_THEME_SYS: case ID_THEME_LIGHT: case ID_THEME_DARK:
            g_themePref = LOWORD(w) - ID_THEME_SYS;
            g_themeOverride = -1;
            RegSetDword(nullptr, L"Theme", (DWORD)g_themePref);
            ApplyTheme();
            break;
        case ID_FINDNEXT: DoFind(false); break;
        case ID_FINDPREV: DoFind(true); break;
        case ID_PRINT: PrintDoc(t, true); break;
        case ID_PRINTCLEAN: PrintDoc(t, false); break;
        case ID_SIDE_PAGES: SetSideMode(0); break;
        case ID_SIDE_OUTLINE: SetSideMode(1); break;
        case ID_SIDE_COMMENTS: SetSideMode(2); break;
        case ID_NAVLIST: if (HIWORD(w) == LBN_SELCHANGE) NavPicked(); break;
        case ID_TOOL_EDITTEXT: SetTool(kEditText); break;
        case ID_AUTOINC: g_ms.autoInc = !g_ms.autoInc; SaveMarkSettings(); InvalidateRect(g_marks, nullptr, FALSE); break;
        case ID_DELANNOT: DeleteSelectedAnnot(t); break;
        case ID_SAVE: CommitNote(); SaveTab(t, false); break;
        case ID_SAVEAS: CommitNote(); SaveTab(t, true); break;
        case ID_UNDO: if (t) { CommitNote(); UndoRedo(t, false); } break;
        case ID_REDO: if (t) { CommitNote(); UndoRedo(t, true); } break;
        case ID_ROTR: RotateSel(t, 1); break;
        case ID_ROTL: RotateSel(t, -1); break;
        case ID_DUP: DuplicateSel(t); break;
        case ID_DEL: DeleteSel(t); break;
        case ID_INSERTFILE: InsertFileDialog(t); break;
        case ID_INSERTBLANK:
            if (t && Checkpoint(t)) {
                int at = (t->sel.empty() ? CurrentPage(t) : *t->sel.rbegin()) + 1;
                ops::InsertBlank(t->pdf.Handle(), at, at - 1);
                t->sel.clear();
                t->sel.insert(at);
                AfterEdit(t, at);
            }
            break;
        case ID_EXTRACT: ExtractSel(t); break;
        case ID_SELECTALL:
            if (t) {
                for (int i = 0; i < t->pdf.PageCount(); i++) t->sel.insert(i);
                InvalidateRect(g_thumbs, nullptr, FALSE);
            }
            break;
        case ID_ABOUT:
            MessageBoxW(h, APP_NAME L"\nVersion " APP_VERSION L"\n\nA fast PDF app for audit workpapers.\nPDF engine: PDFium (BSD licence).",
                        L"About " APP_NAME, MB_OK | MB_ICONINFORMATION);
            break;
        }
        return 0;
    }
    case WM_CLOSE:
        CommitNote();
        for (auto& d : g_docs) if (!ConfirmClose(d.get())) return 0;
        for (auto& d : g_docs) DropRecovery(d.get());
        KillTimer(h, kRecoveryTimer);
        for (auto& d : g_docs) SaveLastPage(d.get());
        RevokeDragDrop(h);
        SavePlacement();
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_docs.clear();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HMENU BuildMenu() {
    HMENU bar = CreatePopupMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), pages = CreatePopupMenu(), view = CreatePopupMenu(),
          help = CreatePopupMenu(), tools = CreatePopupMenu();
    AppendMenuW(tools, MF_STRING, ID_TOOL_HAND, L"&Hand\tEsc");
    AppendMenuW(tools, MF_STRING, ID_TOOL_SELECT, L"&Select marks and comments\tS");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, ID_TOOL_TICK, L"&Tickmark\tK");
    AppendMenuW(tools, MF_STRING, ID_TOOL_NUMBER, L"&Number\tN");
    AppendMenuW(tools, MF_STRING, ID_TOOL_LETTER, L"&Letter\tL");
    AppendMenuW(tools, MF_STRING, ID_AUTOINC, L"&Auto-increment");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, ID_TOOL_PREPARED, L"P&repared by stamp\tP");
    AppendMenuW(tools, MF_STRING, ID_TOOL_REVIEWED, L"Re&viewed by stamp\tR");
    AppendMenuW(tools, MF_STRING, ID_SIGNNAME, L"Sign-off &name...");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, ID_TOOL_HIGHLIGHT, L"High&light\tH");
    AppendMenuW(tools, MF_STRING, ID_TOOL_NOTE, L"N&ote");
    AppendMenuW(tools, MF_STRING, ID_TOOL_BOX, L"&Box");
    AppendMenuW(tools, MF_STRING, ID_TOOL_OVAL, L"&Oval");
    AppendMenuW(tools, MF_STRING, ID_TOOL_PEN, L"&Pen");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, ID_TOOL_ADDTEXT, L"Add te&xt\tT");
    AppendMenuW(tools, MF_STRING, ID_TOOL_EDITTEXT, L"&Edit text\tE");
    AppendMenuW(tools, MF_STRING, ID_EDITLINE, L"Edit text picks a &line");
    AppendMenuW(tools, MF_STRING, ID_EDITPARA, L"Edit text picks a &paragraph");
    AppendMenuW(tools, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(tools, MF_STRING, ID_MARKSPANE, L"Show marks &pane\tF6");
    AppendMenuW(file, MF_STRING, ID_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_SAVEAS, L"Save &as...\tCtrl+Shift+S");
    AppendMenuW(file, MF_STRING, ID_EXTRACT, L"Save selected pages as a new PDF...");
    AppendMenuW(file, MF_STRING, ID_PRINT, L"&Print...\tCtrl+P");
    AppendMenuW(file, MF_STRING, ID_PRINTCLEAN, L"Print &without marks and comments...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_CLOSETAB, L"&Close tab\tCtrl+W");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"E&xit");
    AppendMenuW(edit, MF_STRING, ID_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, ID_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_SELECTALL, L"Select all pages\tCtrl+A");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_FIND, L"&Find...\tCtrl+F");
    AppendMenuW(edit, MF_STRING, ID_FINDNEXT, L"Find &next\tF3");
    AppendMenuW(edit, MF_STRING, ID_FINDPREV, L"Find pre&vious\tShift+F3");
    AppendMenuW(pages, MF_STRING, ID_INSERTFILE, L"&Insert pages from files...");
    AppendMenuW(pages, MF_STRING, ID_INSERTBLANK, L"Insert &blank page");
    AppendMenuW(pages, MF_STRING, ID_DUP, L"&Duplicate\tCtrl+D");
    AppendMenuW(pages, MF_STRING, ID_ROTR, L"Rotate &right\tCtrl+R");
    AppendMenuW(pages, MF_STRING, ID_ROTL, L"Rotate &left\tCtrl+Shift+R");
    AppendMenuW(pages, MF_STRING, ID_DEL, L"De&lete\tDel (in thumbnails)");
    AppendMenuW(view, MF_STRING, ID_ZOOMIN, L"Zoom &in\tCtrl++");
    AppendMenuW(view, MF_STRING, ID_ZOOMOUT, L"Zoom &out\tCtrl+-");
    AppendMenuW(view, MF_STRING, ID_ZOOM100, L"&Actual size\tCtrl+0");
    AppendMenuW(view, MF_STRING, ID_FITWIDTH, L"Fit &width\tCtrl+1");
    AppendMenuW(view, MF_STRING, ID_FITPAGE, L"Fit &page\tCtrl+2");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_GOTO, L"&Go to page...\tCtrl+G");
    AppendMenuW(view, MF_STRING, ID_FIRSTPAGE, L"First page\tCtrl+Home");
    AppendMenuW(view, MF_STRING, ID_LASTPAGE, L"Last page\tCtrl+End");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_THUMBS, L"Show or hide the side &pane\tF4");
    AppendMenuW(view, MF_STRING, ID_SIDE_PAGES, L"Side pane: pa&ges");
    AppendMenuW(view, MF_STRING, ID_SIDE_OUTLINE, L"Side pane: &bookmarks");
    AppendMenuW(view, MF_STRING, ID_SIDE_COMMENTS, L"Side pane: &comments list");
    HMENU theme = CreatePopupMenu();
    AppendMenuW(theme, MF_STRING, ID_THEME_SYS, L"Match Windows");
    AppendMenuW(theme, MF_STRING, ID_THEME_LIGHT, L"Light");
    AppendMenuW(theme, MF_STRING, ID_THEME_DARK, L"Dark");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_POPUP, (UINT_PTR)theme, L"T&heme");
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_STRING, ID_NEXTTAB, L"Next tab\tCtrl+Tab");
    AppendMenuW(view, MF_STRING, ID_PREVTAB, L"Previous tab\tCtrl+Shift+Tab");
    AppendMenuW(help, MF_STRING, ID_GUIDE, L"&Quick guide");
    AppendMenuW(help, MF_STRING, ID_SHORTCUTS, L"&Keyboard shortcuts");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, ID_DEFAULTAPP, L"Make Workpaper PDF the &default PDF app...");
    AppendMenuW(help, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"&About " APP_NAME);
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)pages, L"&Pages");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)tools, L"&Tools");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)view, L"&View");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    return bar;
}

static HACCEL BuildAccel() {
    ACCEL a[] = {
        {FCONTROL | FVIRTKEY, 'O', ID_OPEN},
        {FCONTROL | FVIRTKEY, 'W', ID_CLOSETAB},
        {FCONTROL | FVIRTKEY, VK_F4, ID_CLOSETAB},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, ID_ZOOMIN},
        {FCONTROL | FVIRTKEY, VK_ADD, ID_ZOOMIN},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, ID_ZOOMOUT},
        {FCONTROL | FVIRTKEY, VK_SUBTRACT, ID_ZOOMOUT},
        {FCONTROL | FVIRTKEY, '0', ID_ZOOM100},
        {FCONTROL | FVIRTKEY, '1', ID_FITWIDTH},
        {FCONTROL | FVIRTKEY, '2', ID_FITPAGE},
        {FCONTROL | FVIRTKEY, 'G', ID_GOTO},
        {FCONTROL | FVIRTKEY, VK_TAB, ID_NEXTTAB},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_TAB, ID_PREVTAB},
        {FCONTROL | FVIRTKEY, VK_HOME, ID_FIRSTPAGE},
        {FCONTROL | FVIRTKEY, VK_END, ID_LASTPAGE},
        {FVIRTKEY, VK_F4, ID_THUMBS},
        {FVIRTKEY, VK_F6, ID_MARKSPANE},
        {FCONTROL | FVIRTKEY, 'S', ID_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_SAVEAS},
        {FCONTROL | FVIRTKEY, 'Z', ID_UNDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_REDO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'Z', ID_REDO},
        {FCONTROL | FVIRTKEY, 'R', ID_ROTR},
        {FCONTROL | FSHIFT | FVIRTKEY, 'R', ID_ROTL},
        {FCONTROL | FVIRTKEY, 'D', ID_DUP},
        {FCONTROL | FVIRTKEY, 'A', ID_SELECTALL},
        {FCONTROL | FVIRTKEY, 'F', ID_FIND},
        {FVIRTKEY, VK_F10, ID_MENU},
        {FVIRTKEY, VK_F3, ID_FINDNEXT},
        {FSHIFT | FVIRTKEY, VK_F3, ID_FINDPREV},
        {FCONTROL | FVIRTKEY, 'P', ID_PRINT},
    };
    return CreateAcceleratorTableW(a, (int)(sizeof(a) / sizeof(a[0])));
}

static void Register(const wchar_t* name, WNDPROC proc, HCURSOR cur, HBRUSH bg, HICON icon) {
    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = g_inst;
    wc.hCursor = cur;
    wc.hbrBackground = bg;
    wc.lpszClassName = name;
    wc.hIcon = icon;
    wc.hIconSm = icon;
    RegisterClassExW(&wc);
}

// Headless timing run used by the automated build: open a file, draw pages, write timings.
static int RunBenchmark(const wchar_t* pdfPath, const wchar_t* outPath) {
    PdfInit();
    LARGE_INTEGER f, a, b, c, d;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    PdfDoc doc;
    std::wstring err;
    bool ok = doc.Open(pdfPath, err);
    QueryPerformanceCounter(&b);
    double openMs = (b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart, firstMs = 0, allMs = 0;
    int n = ok ? doc.PageCount() : 0;
    if (ok) {
        // Page 1 at a typical laptop "fit width" size, then every page.
        int w = 1100, h = (int)(1100 * doc.Size(0).h / doc.Size(0).w);
        HBITMAP bmp = doc.Render(0, w, h, 0, 0, w, h);
        QueryPerformanceCounter(&c);
        firstMs = (c.QuadPart - b.QuadPart) * 1000.0 / f.QuadPart;
        if (bmp) DeleteObject(bmp);
        for (int i = 0; i < n; i++) {
            int pw = 1100, ph = (int)(1100 * doc.Size(i).h / doc.Size(i).w);
            HBITMAP x = doc.Render(i, pw, ph, 0, 0, pw, ph);
            if (x) DeleteObject(x);
        }
        QueryPerformanceCounter(&d);
        allMs = (d.QuadPart - c.QuadPart) * 1000.0 / f.QuadPart;
    }
    FILE* fp = _wfopen(outPath, L"w");
    if (fp) {
        if (ok)
            fprintf(fp, "pages=%d\nopen_ms=%.1f\nfirst_page_ms=%.1f\nall_pages_ms=%.1f\nper_page_ms=%.1f\n", n, openMs, firstMs, allMs, n ? allMs / n : 0);
        else
            fprintf(fp, "error=%ls\n", err.c_str());
        fclose(fp);
    }
    PdfShutdown();
    return ok ? 0 : 1;
}

// Command line: WorkpaperPDF.exe --combine out.pdf file1 file2 ...  builds one PDF from the files
// (used by the automated build to test conversion; handy for scripts too). Problems go to out.pdf.log.
static int RunCombine(int argc, LPWSTR* argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    PdfInit();
    PdfDoc doc;
    doc.NewEmpty(L"Combined");
    std::vector<std::wstring> files(argv + 3, argv + argc);
    std::wstring errors, err;
    int added = InsertFiles(doc.Handle(), 0, files, errors, nullptr);
    doc.RefreshSizes();
    bool ok = added > 0 && doc.SaveTo(argv[2], err);
    FILE* fp = _wfopen((std::wstring(argv[2]) + L".log").c_str(), L"w");
    if (fp) {
        fprintf(fp, "pages_added=%d\nsaved=%d\n%ls%ls\n", added, ok ? 1 : 0, errors.c_str(), err.c_str());
        fclose(fp);
    }
    doc.Close();
    PdfShutdown();
    CoUninitialize();
    return ok ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    g_inst = inst;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argc == 4 && wcscmp(argv[1], L"--benchmark") == 0) return RunBenchmark(argv[2], argv[3]);
    // --theme light|dark (for screenshots) applies to this run only.
    std::vector<std::wstring> files;
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--theme") == 0 && i + 1 < argc) {
            g_themeOverride = wcscmp(argv[i + 1], L"dark") == 0 ? 2 : 1;
            i++;
            continue;
        }
        files.push_back(argv[i]);
    }
    if (argc >= 4 && wcscmp(argv[1], L"--combine") == 0) return RunCombine(argc, argv);

    // One window for the whole app: hand files to a copy that is already running.
    HWND existing = FindWindowW(kMainClass, nullptr);
    if (existing) {
        for (auto& f : files) {
            wchar_t full[MAX_PATH * 4];
            GetFullPathNameW(f.c_str(), MAX_PATH * 4, full, nullptr);
            COPYDATASTRUCT cd = {1, (DWORD)((wcslen(full) + 1) * sizeof(wchar_t)), full};
            SendMessageW(existing, WM_COPYDATA, 0, (LPARAM)&cd);
        }
        if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return 0;
    }

    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);
    PdfInit();
    Gdiplus::GdiplusStartupInput gsi;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gsi, nullptr);
    DWORD themePref;
    if (RegGetDword(nullptr, L"Theme", themePref)) g_themePref = std::clamp((int)themePref, 0, 2);
    ApplyTheme();
    LoadRecent();

    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    Register(kMainClass, MainProc, LoadCursor(nullptr, IDC_ARROW), nullptr, icon);
    Register(kViewClass, ViewProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    Register(kThumbClass, ThumbProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    Register(kMarksClass, MarksProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    LoadMarkSettings();
    Register(kToolClass, ToolProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    Register(kSideClass, SideProc, LoadCursor(nullptr, IDC_HAND), nullptr, nullptr);
    Register(kTabsClass, TabsProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    Register(kStatusClass, StatusProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);

    DWORD th;
    if (RegGetDword(nullptr, L"Thumbnails", th)) g_showThumbs = th != 0;
    if (RegGetDword(nullptr, L"SideMode", th)) g_sideMode = std::clamp((int)th, 0, 2);

    g_menu = BuildMenu();
    HWND h = CreateWindowExW(0, kMainClass, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 860,
                             nullptr, nullptr, inst, nullptr);
    WINDOWPLACEMENT wp = {};
    DWORD sz = sizeof(wp);
    bool placed = false;
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Window", RRF_RT_REG_BINARY, nullptr, &wp, &sz) == 0 && sz == sizeof(wp)) {
        wp.length = sizeof(wp);
        if (wp.showCmd == SW_SHOWMINIMIZED) wp.showCmd = SW_SHOWNORMAL;
        if (show == SW_SHOWMAXIMIZED) wp.showCmd = SW_SHOWMAXIMIZED;
        placed = SetWindowPlacement(h, &wp) != 0;
    }
    if (!placed) ShowWindow(h, show);
    UpdateWindow(h);

    for (auto& f : files) {
        wchar_t full[MAX_PATH * 4];
        GetFullPathNameW(f.c_str(), MAX_PATH * 4, full, nullptr);
        OpenFile(full);
    }
    LocalFree(argv);
    OfferRecovery();

    g_accel = BuildAccel();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Let the page box receive typing, but keep app shortcuts working everywhere.
        if (g_noteEdit && msg.hwnd == g_noteEdit && msg.message == WM_KEYDOWN &&
            (msg.wParam == VK_ESCAPE || (msg.wParam == VK_RETURN && GetKeyState(VK_CONTROL) < 0))) {
            CommitNote();
            SetFocus(g_view);
            continue;
        }
        if (g_textEdit && msg.hwnd == g_textEdit && msg.message == WM_KEYDOWN &&
            (msg.wParam == VK_ESCAPE || (msg.wParam == VK_RETURN && (g_te.replace || GetKeyState(VK_CONTROL) < 0)))) {
            CommitText(msg.wParam == VK_ESCAPE);
            SetFocus(g_view);
            continue;
        }
        if (g_textEdit && msg.hwnd == g_textEdit && IsWindowVisible(g_textEdit)) { TranslateMessage(&msg); DispatchMessageW(&msg); continue; }
        // While typing a note, keys belong to the note (no shortcuts).
        if (g_noteEdit && msg.hwnd == g_noteEdit && IsWindowVisible(g_noteEdit)) { TranslateMessage(&msg); DispatchMessageW(&msg); continue; }
        if (TranslateAcceleratorW(g_main, g_accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    PdfShutdown();
    Gdiplus::GdiplusShutdown(gdipToken);
    OleUninitialize();
    return 0;
}

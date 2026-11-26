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
#include "resource.h"

#define APP_NAME L"Workpaper PDF"
#define APP_VERSION L"0.2.0 (build 2)"
static const wchar_t* kMainClass = L"WorkpaperPDFMain";
static const wchar_t* kViewClass = L"WorkpaperPDFView";
static const wchar_t* kThumbClass = L"WorkpaperPDFThumbs";
static const wchar_t* kToolClass = L"WorkpaperPDFToolbar";
static const wchar_t* kRegKey = L"Software\\WorkpaperPDF";

enum {
    ID_OPEN = 100, ID_CLOSETAB, ID_EXIT, ID_ZOOMIN, ID_ZOOMOUT, ID_ZOOM100, ID_FITWIDTH, ID_FITPAGE,
    ID_THUMBS, ID_GOTO, ID_NEXTTAB, ID_PREVTAB, ID_PREVPAGE, ID_NEXTPAGE, ID_FIRSTPAGE, ID_LASTPAGE,
    ID_ABOUT, ID_SHORTCUTS, ID_SAVE, ID_SAVEAS, ID_UNDO, ID_REDO, ID_ROTR, ID_ROTL, ID_DUP, ID_DEL,
    ID_INSERTFILE, ID_INSERTBLANK, ID_EXTRACT, ID_SELECTALL, ID_PAGEEDIT = 300, ID_TABS = 301, ID_STATUS = 302,
};
#define WM_APP_RENDER (WM_APP + 1)

// ---------------------------------------------------------------- state

struct Cached {
    HBITMAP bmp = nullptr;
    double scale = 0;
    RECT r = {};  // region of the page (page pixels at `scale`) held in bmp
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
    std::vector<std::shared_ptr<std::vector<uint8_t>>> undo, redo;
    std::set<int> sel;  // selected page thumbnails
    int anchor = -1;    // for shift-click ranges

    ~Tab() {
        for (auto& c : cache) if (c.bmp) DeleteObject(c.bmp);
        for (auto t : thumb) if (t) DeleteObject(t);
    }
};

static HINSTANCE g_inst;
static HWND g_main, g_tool, g_tabs, g_thumbs, g_view, g_status, g_pageEdit;
static std::vector<std::unique_ptr<Tab>> g_docs;
static int g_cur = -1;
static bool g_showThumbs = true;
static bool g_renderPosted = false;
static HFONT g_font, g_iconFont, g_bigFont;
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
    SCROLLINFO si = {sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL};
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
    SendMessageW(g_status, SB_SETTEXTW, 0, (LPARAM)(L"  " + t->pdf.Path()).c_str());
    swprintf(buf, 512, L"Page %d of %d", page + 1, t->pdf.PageCount());
    SendMessageW(g_status, SB_SETTEXTW, 1, (LPARAM)buf);
    swprintf(buf, 512, L"%d%%", (int)lround(t->zoom * 100));
    SendMessageW(g_status, SB_SETTEXTW, 2, (LPARAM)buf);
    swprintf(buf, 512, L"Opened in %.0f ms", t->openMs);
    SendMessageW(g_status, SB_SETTEXTW, 3, (LPARAM)buf);
    if (GetFocus() != g_pageEdit) {
        swprintf(buf, 512, L"%d", page + 1);
        SetWindowTextW(g_pageEdit, buf);
    }
    SetWindowTextW(g_main, (t->pdf.Title() + (t->dirty ? L" *" : L"") + L" - " APP_NAME).c_str());
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

static void SelectTab(int i) {
    if (g_docs.empty()) { g_cur = -1; Refresh(false); InvalidateRect(g_thumbs, nullptr, TRUE); return; }
    g_cur = std::clamp(i, 0, (int)g_docs.size() - 1);
    TabCtrl_SetCurSel(g_tabs, g_cur);
    g_lastShownPage = -1;
    Tab* t = Cur();
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

// Saves an undo point. Call before changing the document.
static bool Checkpoint(Tab* t) {
    auto snap = std::make_shared<std::vector<uint8_t>>();
    if (!ops::SaveToBuffer(t->pdf.Handle(), *snap)) return false;
    t->undo.push_back(snap);
    if (t->undo.size() > 40) t->undo.erase(t->undo.begin());
    t->redo.clear();
    return true;
}

// Call after the document changed: re-reads pages and redraws everything.
static void AfterEdit(Tab* t, int showPage, bool markDirty = true) {
    t->pdf.RefreshSizes();
    ClearRenders(t);
    if (markDirty) t->dirty = true;
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

static void RestoreSnapshot(Tab* t, std::vector<std::shared_ptr<std::vector<uint8_t>>>& from,
                            std::vector<std::shared_ptr<std::vector<uint8_t>>>& to) {
    if (from.empty()) return;
    auto now = std::make_shared<std::vector<uint8_t>>();
    if (!ops::SaveToBuffer(t->pdf.Handle(), *now)) return;
    int page = CurrentPage(t);
    auto snap = from.back();
    from.pop_back();
    std::wstring path = t->pdf.Path(), title = t->pdf.Title();
    if (!t->pdf.OpenMemory(snap, path, title)) return;
    to.push_back(now);
    t->sel.clear();
    AfterEdit(t, std::min(page, t->pdf.PageCount() - 1));
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

static void CloseTab(int i) {
    if (i < 0 || i >= (int)g_docs.size()) return;
    if (!ConfirmClose(g_docs[i].get())) return;
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

static void PaintView(HDC hdc, const RECT& rc) {
    Tab* t = Cur();
    HBRUSH bg = CreateSolidBrush(RGB(0xDD, 0xE0, 0xE4));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);
    if (!t) {
        SelectObject(hdc, g_bigFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(0x55, 0x5B, 0x66));
        RECT r = rc;
        r.bottom = rc.bottom / 2;
        r.top = r.bottom - S(40);
        DrawTextW(hdc, L"Drop PDF files here", -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        SelectObject(hdc, g_font);
        r.top = r.bottom;
        r.bottom = r.top + S(30);
        DrawTextW(hdc, L"or press Ctrl+O to open a file", -1, &r, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return;
    }
    int first, last;
    VisibleRange(t, first, last);
    HDC mem = CreateCompatibleDC(hdc);
    HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
    HBRUSH shadow = CreateSolidBrush(RGB(0xC4, 0xC8, 0xCE));
    bool missing = false;
    for (int i = first; i <= last; i++) {
        int x = PageX(t, i), y = t->top[i] - t->sy;
        RECT pr = {x, y, x + t->pw[i], y + t->ph[i]};
        RECT sh = pr;
        OffsetRect(&sh, 1, 2);
        InflateRect(&sh, 1, 1);
        FillRect(hdc, &sh, shadow);
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
    DeleteObject(shadow);
    DeleteDC(mem);
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
        SetFocus(h);
        if (t) {
            g_dragging = true;
            g_dragStart = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            g_dragSX = t->sx;
            g_dragSY = t->sy;
            SetCapture(h);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (g_dragging && t) ScrollTo(t, g_dragSX - (GET_X_LPARAM(l) - g_dragStart.x), g_dragSY - (GET_Y_LPARAM(l) - g_dragStart.y));
        return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if (g_dragging) { g_dragging = false; if (m == WM_LBUTTONUP) ReleaseCapture(); }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && t) {
            SetCursor(LoadCursor(nullptr, g_dragging ? IDC_SIZEALL : IDC_HAND));
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
        HBRUSH bg = CreateSolidBrush(RGB(0xF3, 0xF4, 0xF6));
        FillRect(buf, &rc, bg);
        DeleteObject(bg);
        RECT div = {rc.right - 1, 0, rc.right, rc.bottom};
        HBRUSH dv = CreateSolidBrush(RGB(0xD0, 0xD4, 0xDA));
        FillRect(buf, &div, dv);
        DeleteObject(dv);
        if (t && !t->thumbTop.empty()) {
            int curPage = CurrentPage(t);
            HDC mem = CreateCompatibleDC(hdc);
            SelectObject(buf, g_font);
            SetBkMode(buf, TRANSPARENT);
            HBRUSH blue = CreateSolidBrush(RGB(0x1F, 0x6F, 0xEB));
            HBRUSH selBg = CreateSolidBrush(RGB(0xD6, 0xE4, 0xFB));
            HBRUSH white = (HBRUSH)GetStockObject(WHITE_BRUSH);
            HBRUSH edge = CreateSolidBrush(RGB(0xC4, 0xC8, 0xCE));
            bool missing = false;
            for (int i = 0; i < t->pdf.PageCount(); i++) {
                int y = t->thumbTop[i] - t->thumbScroll;
                if (y + t->thumbH[i] + S(30) < 0) continue;
                if (y > rc.bottom) break;
                int x = (rc.right - t->thumbW) / 2;
                if (t->sel.count(i)) {
                    RECT sb = {S(6), y - S(6), rc.right - S(7), y + t->thumbH[i] + S(24)};
                    FillRect(buf, &sb, selBg);
                }
                RECT pr = {x, y, x + t->thumbW, y + t->thumbH[i]};
                RECT fr = pr;
                InflateRect(&fr, i == curPage ? S(2) : 1, i == curPage ? S(2) : 1);
                FillRect(buf, &fr, i == curPage ? blue : edge);
                FillRect(buf, &pr, white);
                if (t->thumb[i]) {
                    SelectObject(mem, t->thumb[i]);
                    BitBlt(buf, x, y, t->thumbW, t->thumbH[i], mem, 0, 0, SRCCOPY);
                } else missing = true;
                wchar_t num[16];
                swprintf(num, 16, L"%d", i + 1);
                RECT lr = {0, pr.bottom + S(4), rc.right, pr.bottom + S(22)};
                SetTextColor(buf, i == curPage ? RGB(0x1F, 0x6F, 0xEB) : RGB(0x44, 0x48, 0x50));
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
            DeleteObject(selBg);
            DeleteObject(edge);
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

// ---------------------------------------------------------------- toolbar

struct ToolButton {
    int id;
    const wchar_t* glyph;  // Segoe MDL2 Assets / Segoe Fluent Icons
    const wchar_t* label;
    const wchar_t* tip;
    RECT rc;
};
static ToolButton g_buttons[] = {
    {ID_OPEN, L"\xE8E5", L"Open", L"Open (Ctrl+O)", {}},
    {ID_SAVE, L"\xE74E", L"Save", L"Save (Ctrl+S)", {}},
    {ID_UNDO, L"\xE7A7", nullptr, L"Undo (Ctrl+Z)", {}},
    {ID_THUMBS, L"\xE8A9", L"Pages", L"Show or hide page thumbnails (F4)", {}},
    {ID_PREVPAGE, L"\xE70E", nullptr, L"Previous page (Left arrow)", {}},
    {ID_NEXTPAGE, L"\xE70D", nullptr, L"Next page (Right arrow)", {}},
    {0, nullptr, nullptr, nullptr, {}},  // page box sits here
    {ID_ZOOMOUT, L"\xE71F", nullptr, L"Zoom out (Ctrl+minus)", {}},
    {ID_ZOOMIN, L"\xE8A3", nullptr, L"Zoom in (Ctrl+plus)", {}},
    {ID_FITWIDTH, L"\xE9A6", L"Fit width", L"Fit width (Ctrl+1)", {}},
    {ID_FITPAGE, L"\xE740", L"Fit page", L"Fit page (Ctrl+2)", {}},
    {ID_ROTR, L"\xE7AD", L"Rotate", L"Rotate selected pages right (Ctrl+R)", {}},
    {ID_INSERTFILE, L"\xE710", L"Insert", L"Insert pages from files", {}},
    {ID_DEL, L"\xE74D", L"Delete", L"Delete selected pages (Del)", {}},
};
static int g_hot = -1;
static RECT g_pageBoxRect;

static void LayoutToolbar() {
    HDC hdc = GetDC(g_tool);
    RECT rc;
    GetClientRect(g_tool, &rc);
    int x = S(8), h = rc.bottom;
    for (auto& b : g_buttons) {
        if (b.id == 0) {
            int w = S(110);
            g_pageBoxRect = {x, 0, x + w, h};
            MoveWindow(g_pageEdit, x + S(4), (h - S(22)) / 2, S(44), S(22), TRUE);
            x += w;
            continue;
        }
        int w = S(32);
        if (b.label) {
            SelectObject(hdc, g_font);
            SIZE sz;
            GetTextExtentPoint32W(hdc, b.label, (int)wcslen(b.label), &sz);
            w = S(36) + sz.cx;
        }
        b.rc = {x, S(4), x + w, h - S(4)};
        x += w + S(2);
    }
    ReleaseDC(g_tool, hdc);
}

static LRESULT CALLBACK ToolProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HDC buf = CreateCompatibleDC(hdc);
        HBITMAP bb = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ oldb = SelectObject(buf, bb);
        FillRect(buf, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        RECT line = {0, rc.bottom - 1, rc.right, rc.bottom};
        HBRUSH lb = CreateSolidBrush(RGB(0xD0, 0xD4, 0xDA));
        FillRect(buf, &line, lb);
        DeleteObject(lb);
        SetBkMode(buf, TRANSPARENT);
        bool have = Cur() != nullptr;
        for (int i = 0; i < (int)(sizeof(g_buttons) / sizeof(g_buttons[0])); i++) {
            auto& b = g_buttons[i];
            if (b.id == 0) continue;
            bool enabled = have || b.id == ID_OPEN || b.id == ID_THUMBS;
            bool active = (b.id == ID_THUMBS && g_showThumbs) || (have && b.id == ID_FITWIDTH && Cur()->fit == 1) ||
                          (have && b.id == ID_FITPAGE && Cur()->fit == 2);
            if ((i == g_hot && enabled) || active) {
                HBRUSH hb = CreateSolidBrush(active ? RGB(0xE3, 0xEC, 0xFB) : RGB(0xEE, 0xF0, 0xF3));
                HPEN np = (HPEN)GetStockObject(NULL_PEN);
                HGDIOBJ op = SelectObject(buf, np), ob = SelectObject(buf, hb);
                RoundRect(buf, b.rc.left, b.rc.top, b.rc.right, b.rc.bottom, S(6), S(6));
                SelectObject(buf, op);
                SelectObject(buf, ob);
                DeleteObject(hb);
            }
            COLORREF col = enabled ? RGB(0x24, 0x29, 0x33) : RGB(0xB0, 0xB4, 0xBA);
            SetTextColor(buf, col);
            RECT g = b.rc;
            g.left += S(8);
            g.right = g.left + S(18);
            if (!b.label) { g = b.rc; }
            SelectObject(buf, g_iconFont);
            DrawTextW(buf, b.glyph, -1, &g, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
            if (b.label) {
                RECT tr = b.rc;
                tr.left += S(30);
                SelectObject(buf, g_font);
                DrawTextW(buf, b.label, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
        }
        // "of N" next to the page box.
        if (Cur()) {
            wchar_t of[32];
            swprintf(of, 32, L"of %d", Cur()->pdf.PageCount());
            RECT r = g_pageBoxRect;
            r.left += S(54);
            SelectObject(buf, g_font);
            SetTextColor(buf, RGB(0x44, 0x48, 0x50));
            DrawTextW(buf, of, -1, &r, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
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
        return 0;
    case WM_MOUSEMOVE: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        int hot = -1;
        for (int i = 0; i < (int)(sizeof(g_buttons) / sizeof(g_buttons[0])); i++)
            if (g_buttons[i].id && PtInRect(&g_buttons[i].rc, p)) hot = i;
        if (hot != g_hot) {
            g_hot = hot;
            InvalidateRect(h, nullptr, FALSE);
            TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, h, 0};
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_hot = -1;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        POINT p = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        for (auto& b : g_buttons)
            if (b.id && PtInRect(&b.rc, p)) SendMessageW(g_main, WM_COMMAND, b.id, 0);
        return 0;
    }
    case WM_CTLCOLOREDIT:
        return (LRESULT)GetStockObject(WHITE_BRUSH);
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
    if (TabCtrl_GetItemRect(g_tabs, 0, &r)) return r.bottom + S(2);
    return S(28);
}

static void RelayoutMain() {
    RECT rc;
    GetClientRect(g_main, &rc);
    SendMessageW(g_status, WM_SIZE, 0, 0);
    RECT sr;
    GetWindowRect(g_status, &sr);
    int statusH = sr.bottom - sr.top;
    int parts[4] = {rc.right - S(380), rc.right - S(250), rc.right - S(170), -1};
    SendMessageW(g_status, SB_SETPARTS, 4, (LPARAM)parts);
    int toolH = S(40);
    MoveWindow(g_tool, 0, 0, rc.right, toolH, TRUE);
    int tabH = TabStripHeight();
    ShowWindow(g_tabs, tabH ? SW_SHOW : SW_HIDE);
    MoveWindow(g_tabs, 0, toolH, rc.right, tabH, TRUE);
    int y = toolH + tabH;
    int h = std::max(0, (int)rc.bottom - statusH - y);
    int thumbW = g_showThumbs ? S(170) : 0;
    ShowWindow(g_thumbs, g_showThumbs ? SW_SHOW : SW_HIDE);
    MoveWindow(g_thumbs, 0, y, thumbW, h, TRUE);
    MoveWindow(g_view, thumbW, y, rc.right - thumbW, h, TRUE);
}

static void CreateFonts() {
    if (g_font) DeleteObject(g_font);
    if (g_iconFont) DeleteObject(g_iconFont);
    if (g_bigFont) DeleteObject(g_bigFont);
    g_font = CreateFontW(-S(13), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_bigFont = CreateFontW(-S(22), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
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
    g_iconFont = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    if (g_pageEdit) SendMessageW(g_pageEdit, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_tabs) SendMessageW(g_tabs, WM_SETFONT, (WPARAM)g_font, TRUE);
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
                L"Page thumbnails\tF4\n"
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
        g_pageEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, g_tool, (HMENU)(INT_PTR)ID_PAGEEDIT, g_inst, nullptr);
        g_editProc = (WNDPROC)SetWindowLongPtrW(g_pageEdit, GWLP_WNDPROC, (LONG_PTR)PageEditProc);
        g_tabs = CreateWindowExW(0, WC_TABCONTROLW, nullptr, WS_CHILD | WS_CLIPSIBLINGS | TCS_FOCUSNEVER | TCS_TOOLTIPS, 0, 0, 0, 0, h,
                                 (HMENU)(INT_PTR)ID_TABS, g_inst, nullptr);
        g_thumbs = CreateWindowExW(0, kThumbClass, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, nullptr, g_inst, nullptr);
        g_view = CreateWindowExW(0, kViewClass, nullptr, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | WS_TABSTOP, 0, 0, 0, 0, h,
                                 nullptr, g_inst, nullptr);
        g_status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr, WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, h,
                                   (HMENU)(INT_PTR)ID_STATUS, g_inst, nullptr);
        CreateFonts();
        RegisterDragDrop(h, new DropTarget());
        RelayoutMain();
        UpdateStatus();
        return 0;
    }
    case WM_SIZE:
        RelayoutMain();
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
        case ID_SAVE: SaveTab(t, false); break;
        case ID_SAVEAS: SaveTab(t, true); break;
        case ID_UNDO: if (t) RestoreSnapshot(t, t->undo, t->redo); break;
        case ID_REDO: if (t) RestoreSnapshot(t, t->redo, t->undo); break;
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
        for (auto& d : g_docs) if (!ConfirmClose(d.get())) return 0;
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
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), edit = CreatePopupMenu(), pages = CreatePopupMenu(), view = CreatePopupMenu(),
          help = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_STRING, ID_SAVE, L"&Save\tCtrl+S");
    AppendMenuW(file, MF_STRING, ID_SAVEAS, L"Save &as...\tCtrl+Shift+S");
    AppendMenuW(file, MF_STRING, ID_EXTRACT, L"Save selected pages as a new PDF...");
    AppendMenuW(file, MF_STRING, ID_CLOSETAB, L"&Close tab\tCtrl+W");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_EXIT, L"E&xit");
    AppendMenuW(edit, MF_STRING, ID_UNDO, L"&Undo\tCtrl+Z");
    AppendMenuW(edit, MF_STRING, ID_REDO, L"&Redo\tCtrl+Y");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, ID_SELECTALL, L"Select all pages\tCtrl+A");
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
    AppendMenuW(view, MF_STRING, ID_THUMBS, L"Page &thumbnails\tF4");
    AppendMenuW(view, MF_STRING, ID_NEXTTAB, L"Next tab\tCtrl+Tab");
    AppendMenuW(view, MF_STRING, ID_PREVTAB, L"Previous tab\tCtrl+Shift+Tab");
    AppendMenuW(help, MF_STRING, ID_SHORTCUTS, L"&Keyboard shortcuts");
    AppendMenuW(help, MF_STRING, ID_ABOUT, L"&About " APP_NAME);
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)file, L"&File");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)edit, L"&Edit");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)pages, L"&Pages");
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
        {FCONTROL | FVIRTKEY, 'S', ID_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', ID_SAVEAS},
        {FCONTROL | FVIRTKEY, 'Z', ID_UNDO},
        {FCONTROL | FVIRTKEY, 'Y', ID_REDO},
        {FCONTROL | FSHIFT | FVIRTKEY, 'Z', ID_REDO},
        {FCONTROL | FVIRTKEY, 'R', ID_ROTR},
        {FCONTROL | FSHIFT | FVIRTKEY, 'R', ID_ROTL},
        {FCONTROL | FVIRTKEY, 'D', ID_DUP},
        {FCONTROL | FVIRTKEY, 'A', ID_SELECTALL},
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
    if (argc >= 4 && wcscmp(argv[1], L"--combine") == 0) return RunCombine(argc, argv);

    // One window for the whole app: hand files to a copy that is already running.
    HWND existing = FindWindowW(kMainClass, nullptr);
    if (existing) {
        for (int i = 1; i < argc; i++) {
            wchar_t full[MAX_PATH * 4];
            GetFullPathNameW(argv[i], MAX_PATH * 4, full, nullptr);
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

    HICON icon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    Register(kMainClass, MainProc, LoadCursor(nullptr, IDC_ARROW), nullptr, icon);
    Register(kViewClass, ViewProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    Register(kThumbClass, ThumbProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);
    Register(kToolClass, ToolProc, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr);

    DWORD th;
    if (RegGetDword(nullptr, L"Thumbnails", th)) g_showThumbs = th != 0;

    HWND h = CreateWindowExW(0, kMainClass, APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 860,
                             nullptr, BuildMenu(), inst, nullptr);
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

    for (int i = 1; i < argc; i++) {
        wchar_t full[MAX_PATH * 4];
        GetFullPathNameW(argv[i], MAX_PATH * 4, full, nullptr);
        OpenFile(full);
    }
    LocalFree(argv);

    g_accel = BuildAccel();
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        // Let the page box receive typing, but keep app shortcuts working everywhere.
        if (TranslateAcceleratorW(g_main, g_accel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    PdfShutdown();
    OleUninitialize();
    return 0;
}

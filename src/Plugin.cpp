// Tickmarks plug-in for Adobe Acrobat: settings, the placing tool, the Tickmarks
// tool panel (All tools > Tickmarks) and the plug-in handshake.

#include "Tickmarks.h"
#include "Resource.h"

#include <windows.h>
#include <string>
#include <vector>

extern "C" HINSTANCE gHINSTANCE;

TickmarkSettings gSettings = { 1, 0, true, true, 0, 0, 0 };

/*-------------------------------------------------------
	Settings (HKCU\Software\Tickmarks), kept across restarts
-------------------------------------------------------*/

static const char *kRegKey = "Software\\Tickmarks";

static int ReadInt(HKEY key, const char *name, int def, int lo, int hi)
{
    DWORD v = 0, size = sizeof v, type = 0;
    if (RegQueryValueExA(key, name, NULL, &type, (BYTE *)&v, &size) != ERROR_SUCCESS || type != REG_DWORD)
        return def;
    int i = (int)v;
    return (i < lo || i > hi) ? def : i;
}

static void WriteInt(HKEY key, const char *name, int v)
{
    DWORD d = (DWORD)v;
    RegSetValueExA(key, name, 0, REG_DWORD, (const BYTE *)&d, sizeof d);
}

void LoadSettings()
{
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, kRegKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return;
    gSettings.nextNumber    = ReadInt(key, "NextNumber", 1, 1, kMaxNumber);
    gSettings.nextLetter    = ReadInt(key, "NextLetter", 0, 0, 25);
    gSettings.autoIncrement = ReadInt(key, "AutoIncrement", 1, 0, 1) != 0;
    gSettings.wrap          = ReadInt(key, "Wrap", 1, 0, 1) != 0;
    gSettings.color         = ReadInt(key, "Color", 0, 0, ColorCount() - 1);
    gSettings.size          = ReadInt(key, "MarkSize", 0, 0, 2);
    gSettings.symbol        = ReadInt(key, "Symbol", 0, 0, SymbolCount() - 1);
    RegCloseKey(key);
}

static void UpdateLabels();

void SaveSettings()
{
    HKEY key;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, kRegKey, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        WriteInt(key, "NextNumber", gSettings.nextNumber);
        WriteInt(key, "NextLetter", gSettings.nextLetter);
        WriteInt(key, "AutoIncrement", gSettings.autoIncrement ? 1 : 0);
        WriteInt(key, "Wrap", gSettings.wrap ? 1 : 0);
        WriteInt(key, "Color", gSettings.color);
        WriteInt(key, "MarkSize", gSettings.size);
        WriteInt(key, "Symbol", gSettings.symbol);
        RegCloseKey(key);
    }
    UpdateLabels();
}

/*-------------------------------------------------------
	The placing tool: click on the page to drop the current mark
-------------------------------------------------------*/

static AVToolRec gTool;
static MarkMode  gMode = kModeSymbol;
static ASAtom    gToolType;

static bool ToolIsActive() { return AVAppGetActiveTool() == &gTool; }

static ACCB1 ASAtom ACCB2 ToolGetType(AVTool tool) { return gToolType; }
static ACCB1 void ACCB2 ToolActivate(AVTool tool, ASBool persistent) {}
static ACCB1 void ACCB2 ToolDeactivate(AVTool tool) {}
static ACCB1 ASBool ACCB2 ToolIsPersistent(AVTool tool) { return true; }

static ACCB1 ASBool ACCB2 ToolAdjustCursor(AVTool tool, AVPageView pageView, AVDevCoord x, AVDevCoord y)
{
    AVSysSetCursor(AVSysGetStandardCursor(ARROW_CURSOR));
    return true;
}

static ACCB1 ASBool ACCB2 ToolDoClick(AVTool tool, AVPageView pageView, AVDevCoord x, AVDevCoord y,
                                      AVFlagBits16 flags, AVTCount clickNo)
{
    // Let Acrobat handle clicks on an existing comment (select, move, resize) unless Shift is held.
    PDAnnot hit;
    if (!(AVSysGetModifiers() & AV_SHIFT) && AVPageViewIsAnnotAtPoint(pageView, x, y, &hit))
        return false;
    if (gMode == kModeNote) {
        PlaceNote(pageView, x, y);
        return true;
    }
    // Click to place at the chosen size, or click-and-drag to size the mark as you place it.
    AVDevRect drag;
    AVPageViewDragOutNewRect(pageView, x, y, &drag);
    ASFixedRect pageRect;
    AVPageViewDeviceRectToPage(pageView, &drag, &pageRect);
    PlaceMark(pageView, x, y, gMode, &pageRect);
    return true;
}

static ACCB1 ASBool ACCB2 ToolDoKeyDown(AVTool tool, AVKeyCode key, AVFlagBits16 flags)
{
    if (key == ASKEY_ESCAPE) {
        AVAppSetActiveTool(AVAppGetDefaultTool(), true);
        return true;
    }
    return false;
}

static ACCB1 ASBool ACCB2 HasEditableDoc(void *)
{
    return AVAppGetActiveDoc() != NULL;
}

static void StartTool(MarkMode mode)
{
    gMode = mode;
    if (!AVAppGetActiveDoc())
        return;
    AVAppSetActiveTool(&gTool, true);
}

static void SetUpTool()
{
    memset(&gTool, 0, sizeof gTool);
    gTool.size = sizeof(AVToolRec);
    gToolType = ASAtomFromString("Tickmarks:Place");
    gTool.Activate     = ASCallbackCreateProto(ActivateProcType, &ToolActivate);
    gTool.Deactivate   = ASCallbackCreateProto(DeactivateProcType, &ToolDeactivate);
    gTool.GetType      = ASCallbackCreateProto(GetTypeProcType, &ToolGetType);
    gTool.IsPersistent = ASCallbackCreateProto(IsPersistentProcType, &ToolIsPersistent);
    gTool.AdjustCursor = ASCallbackCreateProto(AdjustCursorProcType, &ToolAdjustCursor);
    gTool.DoClick      = ASCallbackCreateProto(DoClickProcType, &ToolDoClick);
    gTool.DoKeyDown    = ASCallbackCreateProto(DoKeyDownProcType, &ToolDoKeyDown);
    gTool.ComputeEnabled = ASCallbackCreateProto(AVComputeEnabledProc, &HasEditableDoc);
    AVAppRegisterTool(&gTool);
}

/*-------------------------------------------------------
	Icons (icons.pdf, compiled in as a resource)
-------------------------------------------------------*/

static AVIcon gIcons[kIconCount];

static void LoadIcons()
{
    memset(gIcons, 0, sizeof gIcons);
    HRSRC res = FindResourceA(gHINSTANCE, MAKEINTRESOURCEA(IDR_ICONS_PDF), "PDFICONS");
    if (!res) return;
    DWORD size = SizeofResource(gHINSTANCE, res);
    HGLOBAL mem = LoadResource(gHINSTANCE, res);
    const char *data = mem ? (const char *)LockResource(mem) : NULL;
    if (!data) return;

    DURING
        ASFileSys fs = ASGetRamFileSys();
        ASPathName path = ASFileSysGetTempPathName(fs, NULL);
        ASFile f = NULL;
        ASFileSysOpenFile(fs, path, ASFILE_WRITE | ASFILE_CREATE, &f);
        ASFileWrite(f, data, size);
        ASFileClose(f);
        ASFileSysOpenFile(fs, path, ASFILE_READ, &f);
        PDDoc doc = PDDocOpenFromASFile(f, NULL, false);
        for (int i = 0; i < kIconCount; ++i)
            gIcons[i] = AVIconCreateFromPDF(doc, i, 24, 24);
        PDDocClose(doc);
        ASFileSysReleasePath(fs, path);
    HANDLER
    END_HANDLER
}

/*-------------------------------------------------------
	Menus (used as drop-downs on the panel buttons)
-------------------------------------------------------*/

// Menu item callbacks get a small integer packed into clientData.
static void *Tag(int kind, int value) { return (void *)(size_t)((kind << 8) | value); }
static int TagKind(void *d)  { return (int)((size_t)d >> 8); }
static int TagValue(void *d) { return (int)((size_t)d & 0xFF); }

enum { kTagSymbol = 1, kTagColor, kTagSize, kTagNextNumber, kTagNextLetter, kTagReset, kTagAuto, kTagWrap,
       kTagStartNumber, kTagStartLetter };

static ACCB1 void ACCB2 MenuExecute(void *d)
{
    int v = TagValue(d);
    switch (TagKind(d)) {
    case kTagSymbol:      gSettings.symbol = v; SaveSettings(); StartTool(kModeSymbol); break;
    case kTagColor:       gSettings.color = v; SaveSettings(); break;
    case kTagSize:        gSettings.size = v; SaveSettings(); break;
    case kTagNextNumber:  gSettings.nextNumber = v; SaveSettings(); StartTool(kModeNumber); break;
    case kTagNextLetter:  gSettings.nextLetter = v; SaveSettings(); StartTool(kModeLetter); break;
    case kTagStartNumber: StartTool(kModeNumber); break;
    case kTagStartLetter: StartTool(kModeLetter); break;
    case kTagReset:       gSettings.nextNumber = 1; gSettings.nextLetter = 0; SaveSettings(); break;
    case kTagAuto:        gSettings.autoIncrement = !gSettings.autoIncrement; SaveSettings(); break;
    case kTagWrap:        gSettings.wrap = !gSettings.wrap; SaveSettings(); break;
    }
}

static ACCB1 ASBool ACCB2 MenuMarked(void *d)
{
    int v = TagValue(d);
    switch (TagKind(d)) {
    case kTagSymbol:     return ToolIsActive() && gMode == kModeSymbol && gSettings.symbol == v;
    case kTagColor:      return gSettings.color == v;
    case kTagSize:       return gSettings.size == v;
    case kTagNextNumber: return gSettings.nextNumber == v;
    case kTagNextLetter: return gSettings.nextLetter == v;
    case kTagAuto:       return gSettings.autoIncrement;
    case kTagWrap:       return gSettings.wrap;
    }
    return false;
}

static AVExecuteProc       gMenuExecute;
static AVComputeMarkedProc gMenuMarked;
static AVComputeEnabledProc gHasDoc;
static int gMenuSeq = 0;

static AVMenu NewMenu(const char *title)
{
    std::string name = "Tickmarks:Menu" + std::to_string(++gMenuSeq);
    return AVMenuNew(title, name.c_str(), gExtensionID);
}

static void AddItem(AVMenu menu, const std::string &title, void *tag, AVMenu submenu = NULL, bool needsDoc = false)
{
    std::string name = "Tickmarks:Item" + std::to_string(++gMenuSeq);
    AVMenuItem item = AVMenuItemNew(title.c_str(), name.c_str(), submenu, false, NO_SHORTCUT, 0, NULL, gExtensionID);
    if (!submenu) {
        AVMenuItemSetExecuteProc(item, gMenuExecute, tag);
        AVMenuItemSetComputeMarkedProc(item, gMenuMarked, tag);
    }
    if (needsDoc)
        AVMenuItemSetComputeEnabledProc(item, gHasDoc, NULL);
    AVMenuAddMenuItem(menu, item, APPEND_MENUITEM);
    AVMenuItemRelease(item);
}

static void AddSeparator(AVMenu menu)
{
    std::string name = "Tickmarks:Sep" + std::to_string(++gMenuSeq);
    AVMenuItem item = AVMenuItemNew("-", name.c_str(), NULL, false, NO_SHORTCUT, 0, NULL, gExtensionID);
    AVMenuAddMenuItem(menu, item, APPEND_MENUITEM);
    AVMenuItemRelease(item);
}

static AVMenu SymbolsMenu()
{
    AVMenu m = NewMenu("Symbols");
    for (int i = 0; i < SymbolCount(); ++i)
        AddItem(m, SymbolLabel(i), Tag(kTagSymbol, i), NULL, true);
    return m;
}

static AVMenu NumbersMenu()
{
    AVMenu m = NewMenu("Numbers");
    AddItem(m, "Place Next Number", Tag(kTagStartNumber, 0), NULL, true);
    AVMenu next = NewMenu("Set Next Number");
    for (int i = 1; i <= kMaxNumber; ++i)
        AddItem(next, std::to_string(i), Tag(kTagNextNumber, i));
    AddItem(m, "Set Next Number", NULL, next);
    AddSeparator(m);
    AddItem(m, "Auto-Increment", Tag(kTagAuto, 0));
    AddItem(m, "Wrap Around (40 to 1, Z to A)", Tag(kTagWrap, 0));
    AddItem(m, "Reset Counters (1 and A)", Tag(kTagReset, 0));
    return m;
}

static AVMenu LettersMenu()
{
    AVMenu m = NewMenu("Letters");
    AddItem(m, "Place Next Letter", Tag(kTagStartLetter, 0), NULL, true);
    AVMenu next = NewMenu("Set Next Letter");
    for (int i = 0; i < 26; ++i)
        AddItem(next, std::string(1, (char)('A' + i)), Tag(kTagNextLetter, i));
    AddItem(m, "Set Next Letter", NULL, next);
    AddSeparator(m);
    AddItem(m, "Auto-Increment", Tag(kTagAuto, 0));
    AddItem(m, "Reset Counters (1 and A)", Tag(kTagReset, 0));
    return m;
}

static AVMenu ColorMenu()
{
    AVMenu m = NewMenu("Color");
    for (int i = 0; i < ColorCount(); ++i)
        AddItem(m, ColorName(i), Tag(kTagColor, i));
    return m;
}

static AVMenu SizeMenu()
{
    AVMenu m = NewMenu("Size");
    for (int i = 0; i < 3; ++i)
        AddItem(m, SizeName(i), Tag(kTagSize, i));
    return m;
}

/*-------------------------------------------------------
	Panel buttons
-------------------------------------------------------*/

enum ButtonId { kBtnSymbols, kBtnNumbers, kBtnLetters, kBtnAuto, kBtnColor, kBtnSize, kBtnReset, kBtnNote,
                kBtnQuickCheck, kBtnQuickNumber, kBtnCount };

static AVToolButton gButtons[kBtnCount];

static ACCB1 void ACCB2 ButtonExecute(void *d)
{
    switch ((int)(size_t)d) {
    case kBtnSymbols: case kBtnQuickCheck: StartTool(kModeSymbol); break;
    case kBtnNumbers: case kBtnQuickNumber: StartTool(kModeNumber); break;
    case kBtnLetters: StartTool(kModeLetter); break;
    case kBtnNote:    StartTool(kModeNote); break;
    case kBtnAuto:    gSettings.autoIncrement = !gSettings.autoIncrement; SaveSettings(); break;
    case kBtnReset:   gSettings.nextNumber = 1; gSettings.nextLetter = 0; SaveSettings(); break;
    case kBtnColor:   gSettings.color = (gSettings.color + 1) % ColorCount(); SaveSettings(); break;
    case kBtnSize:    gSettings.size = (gSettings.size + 1) % 3; SaveSettings(); break;
    }
}

static ACCB1 ASBool ACCB2 ButtonMarked(void *d)
{
    switch ((int)(size_t)d) {
    case kBtnSymbols: case kBtnQuickCheck:  return ToolIsActive() && gMode == kModeSymbol;
    case kBtnNumbers: case kBtnQuickNumber: return ToolIsActive() && gMode == kModeNumber;
    case kBtnLetters: return ToolIsActive() && gMode == kModeLetter;
    case kBtnNote:    return ToolIsActive() && gMode == kModeNote;
    case kBtnAuto:    return gSettings.autoIncrement;
    }
    return false;
}

static ACCB1 ASBool ACCB2 ButtonEnabled(void *d)
{
    switch ((int)(size_t)d) {
    case kBtnSymbols: case kBtnNumbers: case kBtnLetters: case kBtnNote:
    case kBtnQuickCheck: case kBtnQuickNumber:
        return AVAppGetActiveDoc() != NULL;
    }
    return true;
}

static void SetLabel(AVToolButton b, const std::string &text)
{
    if (!b) return;
    ASText t = ASTextFromScriptText(text.c_str(), kASRomanScript);
    AVToolButtonSetLabelText(b, t, kAVButtonPriorityOnExtraHigh);
    ASTextDestroy(t);
}

static void UpdateLabels()
{
    SetLabel(gButtons[kBtnSymbols], std::string("Symbols (") + SymbolLabel(gSettings.symbol) + ")");
    SetLabel(gButtons[kBtnNumbers], "Numbers (next " + std::to_string(gSettings.nextNumber) + ")");
    SetLabel(gButtons[kBtnLetters], std::string("Letters (next ") + (char)('A' + gSettings.nextLetter) + ")");
    SetLabel(gButtons[kBtnAuto], std::string("Auto-Increment: ") + (gSettings.autoIncrement ? "On" : "Off"));
    SetLabel(gButtons[kBtnColor], std::string("Color: ") + ColorName(gSettings.color));
    SetLabel(gButtons[kBtnSize], std::string("Size: ") + SizeName(gSettings.size));
}

static AVToolButton NewButton(ButtonId id, const char *name, Icon icon, const char *label, const char *help, AVMenu menu)
{
    AVToolButton b = AVToolButtonNew(ASAtomFromString(name), gIcons[icon], true, false);
    AVToolButtonSetExecuteProc(b, ASCallbackCreateProto(AVExecuteProc, &ButtonExecute), (void *)(size_t)id);
    AVToolButtonSetComputeMarkedProc(b, ASCallbackCreateProto(AVComputeMarkedProc, &ButtonMarked), (void *)(size_t)id);
    AVToolButtonSetComputeEnabledProc(b, ASCallbackCreateProto(AVComputeEnabledProc, &ButtonEnabled), (void *)(size_t)id);
    AVToolButtonSetHelpText(b, help);
    if (menu)
        AVToolButtonSetMenu(b, menu);
    gButtons[id] = b;
    SetLabel(b, label);
    return b;
}

static void SetUpPanel()
{
    gMenuExecute = ASCallbackCreateProto(AVExecuteProc, &MenuExecute);
    gMenuMarked  = ASCallbackCreateProto(AVComputeMarkedProc, &MenuMarked);
    gHasDoc      = ASCallbackCreateProto(AVComputeEnabledProc, &HasEditableDoc);

    // The named toolbar is what Acrobat lists as a tool under "All tools".
    AVToolBar bar = AVToolBarNew("Tickmarks", "Tickmarks");
    AVToolBarSetIcon(bar, gIcons[kIconToolbar], gIcons[kIconToolbarActive]);

    AVToolBarAddButton(bar, NewButton(kBtnSymbols, "Tickmarks:Symbols", kIconSymbols, "Symbols",
        "Place symbol tickmarks; use the arrow to pick the symbol", SymbolsMenu()), false, NULL);
    AVToolBarAddButton(bar, NewButton(kBtnNumbers, "Tickmarks:Numbers", kIconNumbers, "Numbers",
        "Place numbers 1 to 40", NumbersMenu()), false, NULL);
    AVToolBarAddButton(bar, NewButton(kBtnLetters, "Tickmarks:Letters", kIconLetters, "Letters",
        "Place letters A to Z", LettersMenu()), false, NULL);
    AVToolBarAddButton(bar, NewButton(kBtnAuto, "Tickmarks:Auto", kIconAuto, "Auto-Increment",
        "Turn Auto-Increment on or off", NULL), false, NULL);
    AVToolBarAddButton(bar, NewButton(kBtnColor, "Tickmarks:Color", kIconColor, "Color",
        "Tickmark color for new marks", ColorMenu()), false, NULL);
    AVToolBarAddButton(bar, NewButton(kBtnSize, "Tickmarks:Size", kIconSize, "Size",
        "Tickmark size for new marks", SizeMenu()), false, NULL);
    AVToolBarAddButton(bar, NewButton(kBtnReset, "Tickmarks:Reset", kIconCounters, "Reset Counters",
        "Next number back to 1 and next letter back to A", NULL), false, NULL);
    AVToolBarAddButton(bar, NewButton(kBtnNote, "Tickmarks:Note", kIconNote, "Add Note",
        "Click on the page to add a sticky note", NULL), false, NULL);

    // Shortcuts on Acrobat's own toolbar (the quick-tools strip beside the page).
    AVToolBar main = AVAppGetToolBar();
    if (main) {
        AVToolBarAddButton(main, NewButton(kBtnQuickCheck, "Tickmarks:QuickSymbol", kIconSymbols, "Tickmark",
            "Tickmarks: place the current symbol", SymbolsMenu()), false, NULL);
        AVToolBarAddButton(main, NewButton(kBtnQuickNumber, "Tickmarks:QuickNumber", kIconNumbers, "Number",
            "Tickmarks: place the next number", NumbersMenu()), false, NULL);
    }
    UpdateLabels();
}

/*-------------------------------------------------------
	Handshake
-------------------------------------------------------*/

static ACCB1 ASBool ACCB2 PluginExportHFTs() { return true; }
static ACCB1 ASBool ACCB2 PluginImportReplaceAndRegister() { return true; }

static ACCB1 ASBool ACCB2 PluginInit()
{
    DURING
        LoadSettings();
        LoadIcons();
        SetUpTool();
        SetUpPanel();
    HANDLER
        char msg[256];
        ASGetErrorString(ERRORCODE, msg, sizeof msg);
        AVAlertNote((std::string("Tickmarks could not start: ") + msg).c_str());
    END_HANDLER
    return true;
}

static ACCB1 ASBool ACCB2 PluginUnload()
{
    for (AVToolButton &b : gButtons)
        if (b) { AVToolButtonRemove(b); b = NULL; }
    return true;
}

ASAtom GetExtensionName()
{
    return ASAtomFromString("Tickmarks:Plugin");
}

ACCB1 ASBool ACCB2 PIHandshake(Uns32 handshakeVersion, void *handshakeData)
{
    if (handshakeVersion != HANDSHAKE_V0200)
        return false;
    PIHandshakeData_V0200 *hs = (PIHandshakeData_V0200 *)handshakeData;
    hs->extensionName = GetExtensionName();
    hs->exportHFTsCallback = (void *)ASCallbackCreateProto(PIExportHFTsProcType, &PluginExportHFTs);
    hs->importReplaceAndRegisterCallback = (void *)ASCallbackCreateProto(PIImportReplaceAndRegisterProcType,
                                                                         &PluginImportReplaceAndRegister);
    hs->initCallback = (void *)ASCallbackCreateProto(PIInitProcType, &PluginInit);
    hs->unloadCallback = (void *)ASCallbackCreateProto(PIUnloadProcType, &PluginUnload);
    return true;
}

# Workpaper PDF

A fast PDF app for audit workpapers. Opens in a blink, no account, no auto-updates, no background services.

**This is build 7**, with a fresh look: a clean toolbar, rounded tabs, a start screen with your recent
files, and light and dark themes. It views, searches and prints PDFs, organizes pages, adds tickmarks and
comments, and edits text.

**Quick start:** open a PDF (drag it on, or Ctrl+O). Press F6 for the Marks pane, pick a tickmark, click the page.
Press E and click a line to fix text. Ctrl+S saves. All commands are in the **menu button** at the top left (or press Alt or F10);
Menu > Help > Quick guide opens this guide.

## Install

1. Run **WorkpaperPDF-Setup.exe** from the link Michael sends you.
2. Windows will warn you the first time, because the app isn't from a big-name publisher.
   On the blue **"Windows protected your PC"** screen, click **More info**, then **Run anyway**.
   You only do this once per version.
3. Follow the installer. It installs just for you, so it doesn't need admin rights.

If your antivirus or IT blocks it, ask Michael. Only run installers from the firm's own link.

To make Workpaper PDF your default PDF app: in the app choose **Help > Make Workpaper PDF the default PDF app**
and set .pdf on the Windows page that opens. Or right-click any PDF, choose **Open with > Choose another app**,
pick **Workpaper PDF** and tick **Always**.

No installer? The zip version runs without installing: unzip it anywhere and run `WorkpaperPDF.exe`.

## Using it

- **Open files:** drag PDFs onto the window, double-click a PDF, or press Ctrl+O. Each file opens in its own tab.
- **Move around:** scroll, drag the page with the mouse, or use the page box at the top (Ctrl+G).
  The thumbnail strip on the left (F4) jumps to any page.
- **Zoom:** Ctrl + mouse wheel, Ctrl+plus / Ctrl+minus, Ctrl+0 actual size, Ctrl+1 fit width, Ctrl+2 fit page.
- **Tabs:** Ctrl+Tab to switch, Ctrl+W or middle-click a tab to close.
- The app remembers the last page you were on in each file, and its window size.
- **Start screen:** with no file open, the app shows your recent files. Click one to reopen it.
- **Menu:** the button at the far left of the toolbar (or Alt / F10) holds every command: File, Edit, View,
  Pages, Marks and Help. Hover any toolbar button to see what it does and its shortcut.
- **Zoom menu:** click the zoom percentage in the toolbar to pick a size, fit width or fit page.
- **Light or dark:** Menu > View > Theme. **Match Windows** (the default) follows your Windows setting.

## Search, print and side pane

- **Search:** press Ctrl+F (or click the search box at the top right), type, press Enter. Every match is
  shaded yellow and the current one orange. Enter or F3 goes to the next, Shift+Enter or Shift+F3 to the previous.
  Case doesn't matter, and amounts match with or without commas and $ signs: `12345.67` finds `$12,345.67`
  (but not `112,345.67`). Esc clears the search.
- **Print:** Ctrl+P prints the file with its tickmarks and comments. File > **Print without marks and comments**
  prints the clean document. Landscape pages turn to fit the paper; oversized pages shrink to fit.
- **Side pane (F4):** the tabs at its top switch between **Pages** (thumbnails), **Bookmarks** (the PDF's outline)
  and **Comments** (every tickmark, note, highlight and shape, by page). Click an entry to jump to it.
- **Auto-recovery:** unsaved changes are copied aside every minute. If the app or the PC crashes, the next start
  offers to reopen them.

## Organizing pages

Work in the thumbnail strip on the left (F4 shows or hides it; pick **Pages** at its top).

- **Select pages:** click; Ctrl+click to add pages; Shift+click for a range; Ctrl+A for all.
- **Reorder:** drag selected thumbnails up or down. A blue line shows where they'll land.
- **Copy pages to another file:** drag thumbnails onto that file's tab.
- **Add pages:** drag files from Explorer onto the thumbnails, at the spot you want. PDF, Word (.doc, .docx, .rtf),
  text (.txt, .csv) and image files (.png, .gif, .jpg, .bmp, .tif) all become PDF pages.
  Dropping them on the page area adds them after the current page. With nothing open, they start a new document.
  Word files are converted by the copy of Microsoft Word on your computer, so they look exactly as in Word.
- **Delete:** press Del (or the Delete button). **Duplicate:** Ctrl+D. **Rotate:** Ctrl+R right, Ctrl+Shift+R left.
- **Right-click** a thumbnail for more: insert blank page, insert from files, save pages as a new PDF.
- **Undo** anything with Ctrl+Z (redo with Ctrl+Y). Nothing changes on disk until you **Save** (Ctrl+S).
  A `*` next to the file name means there are unsaved changes.
- PDFs dropped on the page area open in their own tabs.
- **Help > Keyboard shortcuts** lists every shortcut.

## Tickmarks and comments

Open the **Marks** pane on the right with the **Marks** button or F6.

- **Tickmarks:** pick a symbol (check, double check, F, CF, PY, TB, N/A and more), a circled number or a circled letter,
  then click the page. Drag instead of clicking to make it any size. Shortcuts: K symbols, N numbers, L letters.
- **Numbers and letters count up** by themselves (1, 2, 3 or A, B, C) when Auto-increment is ticked.
  Use − and + to set the next one.
- **Colour and size:** six colours and Small, Medium or Large. Your choices are remembered.
- **Highlight (H):** drag across text. **Note:** click to drop a sticky note and type; Ctrl+Enter or click away to finish.
- **Box, Oval and Pen:** drag to draw.
- **Select (S):** click a mark or comment to move it, drag its corner to resize it, press Del to delete it.
  Double-click a note to edit it. Esc goes back to the hand tool.
- Marks are standard PDF annotations, so they show and print the same in Acrobat and any other PDF reader.
- Ctrl+Z undoes marks too. Save (Ctrl+S) to keep them.

## Editing text

- **Add text (T):** click where the text should start and type. Enter starts a new line. Click away or press
  Ctrl+Enter to finish, Esc to cancel. Pick Sans, Serif or Mono, bold and the size under **New text** in the Marks pane;
  the colour comes from the colour swatches.
- **Edit text (E):** point at any line and a dotted box shows what will change. Click it, retype, press Enter.
  In tables, each cell is its own piece, so you can change one amount without touching the row.
  Clear the box and press Enter to delete the text.
- Where possible the change is made in the PDF itself, in the original font. When that can't be done
  (some fonts only carry the letters already used), the old text is covered with white and retyped in the
  closest standard font, sized to match. Either way, Ctrl+Z undoes it.
- Text in a scanned page is a picture, not real text, so it can't be retyped. Use Add text to write the correction next to it.
- **Whole paragraphs:** under **Edit text picks** in the Marks pane, choose **A paragraph**. Pointing now outlines
  the whole paragraph; click it, retype, press Enter, and the lines re-wrap to the paragraph's width at the same
  spacing. If your new text is longer, check it doesn't run into what's below (Ctrl+Z undoes).

## Uninstall

Settings > Apps > Installed apps > Workpaper PDF > Uninstall.

## For developers

Built on GitHub Actions (`.github/workflows/workpaper-pdf.yml`) with MSVC and CMake.
PDF engine: [PDFium](https://pdfium.googlesource.com/pdfium/) via
[pdfium-binaries](https://github.com/bblanchon/pdfium-binaries). Installer: Inno Setup.
Each build also runs a speed check on `tests/sample-60-pages.pdf` and publishes the timings with the release.

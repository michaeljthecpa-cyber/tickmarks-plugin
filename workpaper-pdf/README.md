# Workpaper PDF

A fast PDF app for audit workpapers. Opens in a blink, no account, no auto-updates, no background services.

**This is build 1 (viewer).** Pages, tickmarks, annotations and text editing arrive in the next builds.

## Install

1. Run **WorkpaperPDF-Setup.exe** from the link Michael sends you.
2. Windows will warn you the first time, because the app isn't from a big-name publisher.
   On the blue **"Windows protected your PC"** screen, click **More info**, then **Run anyway**.
   You only do this once per version.
3. Follow the installer. It installs just for you, so it doesn't need admin rights.

If your antivirus or IT blocks it, ask Michael. Only run installers from the firm's own link.

To make Workpaper PDF your default PDF app: right-click any PDF, choose **Open with > Choose another app**,
pick **Workpaper PDF** and tick **Always**.

No installer? The zip version runs without installing: unzip it anywhere and run `WorkpaperPDF.exe`.

## Using it

- **Open files:** drag PDFs onto the window, double-click a PDF, or press Ctrl+O. Each file opens in its own tab.
- **Move around:** scroll, drag the page with the mouse, or use the page box at the top (Ctrl+G).
  The thumbnail strip on the left (F4) jumps to any page.
- **Zoom:** Ctrl + mouse wheel, Ctrl+plus / Ctrl+minus, Ctrl+0 actual size, Ctrl+1 fit width, Ctrl+2 fit page.
- **Tabs:** Ctrl+Tab to switch, Ctrl+W or middle-click a tab to close.
- The app remembers the last page you were on in each file, and its window size.
- **Help > Keyboard shortcuts** lists every shortcut.

## Uninstall

Settings > Apps > Installed apps > Workpaper PDF > Uninstall.

## For developers

Built on GitHub Actions (`.github/workflows/workpaper-pdf.yml`) with MSVC and CMake.
PDF engine: [PDFium](https://pdfium.googlesource.com/pdfium/) via
[pdfium-binaries](https://github.com/bblanchon/pdfium-binaries). Installer: Inno Setup.
Each build also runs a speed check on `tests/sample-60-pages.pdf` and publishes the timings with the release.

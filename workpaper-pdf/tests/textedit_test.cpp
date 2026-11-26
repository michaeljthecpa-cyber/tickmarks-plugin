// Tests for text editing stage 1. Usage: textedit_test sample.pdf [font.ttf]
// Writes textedit_out.pdf and textedit_out*.ppm for a visual check.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "fpdf_edit.h"
#include "fpdf_text.h"
#include "fpdfview.h"
#include "../src/pdfops.h"
#include "../src/textedit.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static std::string Utf8(const std::u16string& s) {
    std::string o;
    for (char16_t c : s) o.push_back(c < 128 ? (char)c : '?');
    return o;
}

static std::u16string PageText(FPDF_PAGE p) {
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(p);
    int n = FPDFText_CountChars(tp);
    std::vector<unsigned short> buf(n + 1);
    FPDFText_GetText(tp, 0, n, buf.data());
    FPDFText_ClosePage(tp);
    return std::u16string(buf.begin(), buf.begin() + n);
}

static bool Find(FPDF_PAGE p, const std::u16string& what, double& x, double& y) {
    FPDF_TEXTPAGE tp = FPDFText_LoadPage(p);
    std::u16string z = what;
    z.push_back(0);
    FPDF_SCHHANDLE h = FPDFText_FindStart(tp, (FPDF_WIDESTRING)z.c_str(), 0, 0);
    bool ok = FPDFText_FindNext(h);
    if (ok) {
        int i = FPDFText_GetSchResultIndex(h);
        double l, r, b, t;
        FPDFText_GetCharBox(tp, i + 1, &l, &r, &b, &t);
        x = (l + r) / 2;
        y = (b + t) / 2;
    }
    FPDFText_FindClose(h);
    FPDFText_ClosePage(tp);
    return ok;
}

static void Ppm(FPDF_PAGE p, const char* path) {
    int w = (int)(FPDF_GetPageWidthF(p) * 1.5), h = (int)(FPDF_GetPageHeightF(p) * 1.5);
    FPDF_BITMAP bm = FPDFBitmap_Create(w, h, 0);
    FPDFBitmap_FillRect(bm, 0, 0, w, h, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bm, p, 0, 0, w, h, 0, FPDF_ANNOT);
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", w, h);
    uint8_t* px = (uint8_t*)FPDFBitmap_GetBuffer(bm);
    int st = FPDFBitmap_GetStride(bm);
    for (int yy = 0; yy < h; yy++)
        for (int xx = 0; xx < w; xx++) { uint8_t* q = px + yy * st + xx * 4; fputc(q[2], f); fputc(q[1], f); fputc(q[0], f); }
    fclose(f);
    FPDFBitmap_Destroy(bm);
}

static bool Has(const std::u16string& hay, const std::u16string& s) { return hay.find(s) != std::u16string::npos; }

int main(int argc, char** argv) {
    FPDF_InitLibrary();
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    FPDF_DOCUMENT d = FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr);
    CHECK(d);
    FPDF_PAGE p = FPDF_LoadPage(d, 0);

    // 1. Pick one table cell and retype it.
    double x, y;
    CHECK(Find(p, u"12,345.67", x, y));
    textedit::Line ln;
    CHECK(textedit::LineAt(p, x, y, 2, ln));
    printf("cell: '%s' size %.1f family %d bold %d box %.1f,%.1f-%.1f,%.1f\n", Utf8(ln.text).c_str(), ln.size, ln.family,
           ln.bold, ln.left, ln.bottom, ln.right, ln.top);
    CHECK(ln.text == u"12,345.67");
    int r1 = textedit::ReplaceLine(d, p, ln, u"12,354.76");
    printf("replace cell -> %d\n", r1);
    CHECK(r1 == textedit::kReplaced);
    std::u16string t = PageText(p);
    CHECK(Has(t, u"12,354.76"));
    CHECK(!Has(t, u"12,345.67"));
    CHECK(Has(t, u"24,691.34"));  // the next row is untouched

    // 2. A label cell: the run should stop at the gap before the amount.
    CHECK(Find(p, u"Operating cash", x, y));
    CHECK(textedit::LineAt(p, x, y, 2, ln));
    printf("label: '%s'\n", Utf8(ln.text).c_str());
    CHECK(ln.text == u"Account 1000 Operating cash" || ln.text == u"Operating cash");

    // 3. The title (bold): change it, using letters the font may not have yet.
    CHECK(Find(p, u"Cash lead", x, y));
    CHECK(textedit::LineAt(p, x, y, 2, ln));
    printf("title: '%s' bold %d size %.1f\n", Utf8(ln.text).c_str(), ln.bold, ln.size);
    CHECK(ln.bold);
    CHECK(textedit::ReplaceLine(d, p, ln, u"Cash lead schedule - reviewed Q4") != textedit::kFailed);
    CHECK(Has(PageText(p), u"reviewed Q4"));

    // 4. Delete a cell.
    CHECK(Find(p, u"Account 1003", x, y));
    CHECK(textedit::LineAt(p, x, y, 2, ln));
    CHECK(textedit::ReplaceLine(d, p, ln, u"") != textedit::kFailed);
    CHECK(!Has(PageText(p), u"Account 1003"));

    // 5. Add new text, two lines.
    CHECK(textedit::AddText(d, p, u"Agreed to bank confirmation\nSee W/P C-2", 330, 300, 10, textedit::kSans, false, false, 0, 0, 200, 0));
    t = PageText(p);
    CHECK(Has(t, u"Agreed to bank confirmation"));
    CHECK(Has(t, u"See W/P C-2"));
    Ppm(p, "textedit_out.ppm");

    // 6. Rotated page: added text reads upright on screen.
    FPDF_PAGE p2 = FPDF_LoadPage(d, 1);
    FPDFPage_SetRotation(p2, 1);
    CHECK(textedit::AddText(d, p2, u"Rotated note", 100, 100, 12, textedit::kSans, true, false, 200, 0, 0, 1));
    Ppm(p2, "textedit_out_rot.ppm");
    FPDF_ClosePage(p2);

    // 7. Embedded font (if given): the same font is reused when the characters exist.
    if (argc > 2) {
        std::ifstream ff(argv[2], std::ios::binary);
        std::vector<uint8_t> fb((std::istreambuf_iterator<char>(ff)), {});
        FPDF_PAGE p3 = FPDFPage_New(d, 2, 612, 792);
        FPDF_FONT font = FPDFText_LoadFont(d, fb.data(), (uint32_t)fb.size(), FPDF_FONT_TRUETYPE, 1);
        CHECK(font);
        const char16_t* rows[] = {u"Total revenue 1,234,567.89", u"Cost of sales 765,432.10"};
        for (int k = 0; k < 2; k++) {
            FPDF_PAGEOBJECT o = FPDFPageObj_CreateTextObj(d, font, 14);
            std::u16string z = rows[k];
            z.push_back(0);
            FPDFText_SetText(o, (FPDF_WIDESTRING)z.c_str());
            FPDFPageObj_Transform(o, 1, 0, 0, 1, 72, 700 - k * 24);
            FPDFPage_InsertObject(p3, o);
        }
        FPDFPage_GenerateContent(p3);
        CHECK(Find(p3, u"revenue", x, y));
        CHECK(textedit::LineAt(p3, x, y, 2, ln));
        printf("embedded: '%s' size %.1f\n", Utf8(ln.text).c_str(), ln.size);
        int r = textedit::ReplaceLine(d, p3, ln, u"Total revenue 1,243,567.98");
        printf("embedded replace -> %d\n", r);
        CHECK(r == textedit::kReplaced);
        t = PageText(p3);
        CHECK(Has(t, u"1,243,567.98"));
        CHECK(Has(t, u"sales"));  // the other row is untouched
        Ppm(p3, "textedit_out_font.ppm");
        FPDFFont_Close(font);
        FPDF_ClosePage(p3);
    }

    // 8. A paragraph: found as a block, retyped longer, and re-wrapped to the same width.
    {
        FPDF_PAGE pp = FPDFPage_New(d, FPDF_GetPageCount(d), 612, 792);
        const char16_t* para[] = {u"The client reconciles the operating account each month and the",
                                  u"controller reviews the reconciliation. We agreed the balance to the",
                                  u"bank confirmation without exception."};
        for (int k = 0; k < 3; k++) {
            FPDF_PAGEOBJECT o = FPDFPageObj_NewTextObj(d, "Times-Roman", 11);
            std::u16string z = para[k];
            z.push_back(0);
            FPDFText_SetText(o, (FPDF_WIDESTRING)z.c_str());
            FPDFPageObj_Transform(o, 1, 0, 0, 1, 72, 600 - k * 13.5);
            FPDFPage_InsertObject(pp, o);
        }
        FPDF_PAGEOBJECT h = FPDFPageObj_NewTextObj(d, "Helvetica-Bold", 14);
        FPDFText_SetText(h, (FPDF_WIDESTRING)u"Cash procedures");
        FPDFPageObj_Transform(h, 1, 0, 0, 1, 72, 630);
        FPDFPage_InsertObject(pp, h);
        FPDF_PAGEOBJECT after = FPDFPageObj_NewTextObj(d, "Times-Roman", 11);
        FPDFText_SetText(after, (FPDF_WIDESTRING)u"Conclusion: cash is fairly stated.");
        FPDFPageObj_Transform(after, 1, 0, 0, 1, 72, 520);
        FPDFPage_InsertObject(pp, after);
        FPDFPage_GenerateContent(pp);
        CHECK(Find(pp, u"controller", x, y));
        textedit::Paragraph para2;
        CHECK(textedit::ParagraphAt(pp, x, y, 2, para2));
        printf("paragraph: %zu lines, leading %.1f: '%s'\n", para2.lines.size(), para2.leading, Utf8(para2.text).c_str());
        CHECK(para2.lines.size() == 3);
        CHECK(para2.text.find(u"month and the controller") != std::u16string::npos);
        std::u16string longer = para2.text + u" We also tested the bank reconciliation for the last month of the year and traced reconciling items to subsequent statements.";
        int used = 0;
        int r = textedit::ReplaceParagraph(d, pp, para2, longer, &used);
        printf("paragraph replace -> %d, %d lines\n", r, used);
        CHECK(r != textedit::kFailed);
        CHECK(used >= 4);
        t = PageText(pp);
        CHECK(Has(t, u"subsequent statements"));
        CHECK(Has(t, u"Cash procedures"));
        CHECK(Has(t, u"Conclusion"));
        Ppm(pp, "textedit_out_para.ppm");
        FPDF_ClosePage(pp);
    }

    // Save and reload: the edits survive.
    std::vector<uint8_t> out;
    CHECK(ops::SaveToBuffer(d, out));
    FILE* fo = fopen("textedit_out.pdf", "wb");
    fwrite(out.data(), 1, out.size(), fo);
    fclose(fo);
    FPDF_ClosePage(p);
    FPDF_CloseDocument(d);
    FPDF_DOCUMENT d2 = FPDF_LoadMemDocument64(out.data(), out.size(), nullptr);
    CHECK(d2);
    FPDF_PAGE q = FPDF_LoadPage(d2, 0);
    t = PageText(q);
    CHECK(Has(t, u"12,354.76"));
    CHECK(Has(t, u"reviewed Q4"));
    CHECK(Has(t, u"See W/P C-2"));
    FPDF_ClosePage(q);
    FPDF_CloseDocument(d2);
    FPDF_DestroyLibrary();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}

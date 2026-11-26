// Tests for tickmarks and comment annotations. Writes annots_out.pdf and annots_out.ppm for a visual check.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "fpdf_annot.h"
#include "fpdf_edit.h"
#include "fpdfview.h"
#include "../src/annots.h"
#include "../src/marks.h"
#include "../src/pdfops.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(int argc, char** argv) {
    FPDF_InitLibrary();
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    FPDF_DOCUMENT d = FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr);
    FPDF_PAGE p = FPDF_LoadPage(d, 0);
    std::string id1 = annots::NewId("tm"), id2 = annots::NewId("hl"), id3 = annots::NewId("note"), id4 = annots::NewId("box"),
                id5 = annots::NewId("oval"), id6 = annots::NewId("ink"), id7 = annots::NewId("tm");
    CHECK(id1 != id7);
    CHECK(marks::AddMark(d, p, marks::kSymbol, 0, "", 0, 300, 680, 320, 700, 0, u"Tester", id1));
    CHECK(marks::AddMark(d, p, marks::kNumber, 0, "7", 1, 330, 680, 350, 700, 0, u"Tester", id7));
    CHECK(annots::AddHighlight(p, {{70, 686, 260, 700}}, 255, 230, 0, u"Tester", id2));
    CHECK(annots::AddNote(p, 500, 700, u"Agreed to bank statement", 255, 200, 0, u"Tester", id3));
    CHECK(annots::AddShape(p, false, {60, 600, 280, 640}, 0, 82, 230, 1.5f, u"Tester", id4));
    CHECK(annots::AddShape(p, true, {300, 600, 420, 640}, 0, 140, 46, 1.5f, u"Tester", id5));
    std::vector<FS_POINTF> s1 = {{100, 500}, {120, 520}, {140, 505}, {160, 530}};
    CHECK(annots::AddInk(p, {s1}, 128, 31, 173, 2.0f, u"Tester", id6));
    // Sign-off stamps: Prepared by and Reviewed by, sized from BaseSize like a click on the page.
    std::string id8 = annots::NewId("tm"), id9 = annots::NewId("tm");
    double sw, sh;
    marks::BaseSize(marks::kPrepared, 0, "MJ  11/26/2026", sw, sh);
    CHECK(sw > 64 && sh == 30);
    CHECK(marks::AddMark(d, p, marks::kPrepared, 0, "MJ  11/26/2026", 1, 420, 740, 420 + sw, 740 + sh, 0, u"Tester", id8));
    marks::BaseSize(marks::kReviewed, 0, "KL  11/27/2026", sw, sh);
    CHECK(marks::AddMark(d, p, marks::kReviewed, 0, "KL  11/27/2026", 2, 510, 740, 510 + sw, 740 + sh, 0, u"Tester", id9));

    auto list = annots::List(p);
    CHECK(list.size() == 9);
    int marksFound = 0;
    for (int i = 0; i < FPDFPage_GetAnnotCount(p); i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p, i);
        if (marks::IsMark(a)) marksFound++;
        FPDFPage_CloseAnnot(a);
    }
    CHECK(marksFound == 4);
    CHECK(annots::Find(p, id3) >= 0);
    CHECK(annots::SetContents(p, id3, u"Agreed to bank statement, no exceptions"));
    for (auto& i : annots::List(p)) if (i.nm == id3) CHECK(i.contents == u"Agreed to bank statement, no exceptions");
    // Move the check mark 100pt right.
    CHECK(annots::SetRect(p, id1, {400, 680, 420, 700}));
    for (auto& i : annots::List(p)) if (i.nm == id1) CHECK(i.rect.left == 400);
    // Undo-style removal of the number mark.
    CHECK(annots::Remove(p, id7));
    CHECK(annots::Find(p, id7) < 0);
    CHECK(annots::List(p).size() == 8);
    FPDF_ClosePage(p);

    std::vector<uint8_t> out;
    CHECK(ops::SaveToBuffer(d, out));
    FILE* fo = fopen("annots_out.pdf", "wb");
    fwrite(out.data(), 1, out.size(), fo);
    fclose(fo);
    FPDF_DOCUMENT d2 = FPDF_LoadMemDocument64(out.data(), out.size(), nullptr);
    FPDF_PAGE p2 = FPDF_LoadPage(d2, 0);
    CHECK(annots::List(p2).size() == 8);
    // The sign-offs survive a save, keep their subject (shown in Acrobat's comment list) and stay our marks.
    int signoffs = 0;
    for (int i = 0; i < FPDFPage_GetAnnotCount(p2); i++) {
        FPDF_ANNOTATION a = FPDFPage_GetAnnot(p2, i);
        unsigned short sub[64] = {};
        FPDFAnnot_GetStringValue(a, "Subj", sub, sizeof(sub));
        std::u16string subj((char16_t*)sub);
        if (subj == u"Prepared by" || subj == u"Reviewed by") { signoffs++; CHECK(marks::IsMark(a)); }
        FPDFPage_CloseAnnot(a);
    }
    CHECK(signoffs == 2);
    int W = 612, H = 792;
    FPDF_BITMAP bm = FPDFBitmap_Create(W, H, 0);
    FPDFBitmap_FillRect(bm, 0, 0, W, H, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bm, p2, 0, 0, W, H, 0, FPDF_ANNOT);
    unsigned char* px = (unsigned char*)FPDFBitmap_GetBuffer(bm);
    int st = FPDFBitmap_GetStride(bm);
    // The moved check mark must be drawn at its new place: look for red pixels there.
    int red = 0;
    for (int y = 792 - 700; y < 792 - 680; y++)
        for (int x = 400; x < 420; x++) { unsigned char* q = px + y * st + x * 4; if (q[2] > 180 && q[1] < 80) red++; }
    printf("red pixels at moved mark: %d\n", red);
    CHECK(red > 10);
    // The Prepared by stamp is drawn in blue.
    int blue = 0;
    for (int y = 792 - 770; y < 792 - 740; y++)
        for (int x = 420; x < 500; x++) { unsigned char* q = px + y * st + x * 4; if (q[0] > 180 && q[2] < 80) blue++; }
    printf("blue pixels in Prepared by stamp: %d\n", blue);
    CHECK(blue > 40);
    FILE* pp = fopen("annots_out.ppm", "wb");
    fprintf(pp, "P6 %d %d 255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { unsigned char* q = px + y * st + x * 4; fputc(q[2], pp); fputc(q[1], pp); fputc(q[0], pp); }
    fclose(pp);
    FPDFBitmap_Destroy(bm);
    FPDF_ClosePage(p2);
    FPDF_CloseDocument(d2);
    FPDF_CloseDocument(d);
    FPDF_DestroyLibrary();
    printf(failures ? "%d FAILURES\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}

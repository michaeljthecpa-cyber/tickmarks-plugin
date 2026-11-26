// Unit tests for pdfops. Build with any C++17 compiler against PDFium, run with the sample PDF path.
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

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static std::vector<uint8_t> ReadAll(const char* p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Returns the text of page i (ASCII only), used to identify which original page sits where.
static std::string PageText(FPDF_DOCUMENT d, int i) {
    FPDF_PAGE p = FPDF_LoadPage(d, i);
    FPDF_TEXTPAGE t = FPDFText_LoadPage(p);
    int n = FPDFText_CountChars(t);
    std::vector<unsigned short> buf(n + 1);
    FPDFText_GetText(t, 0, n, buf.data());
    std::string s;
    for (int k = 0; k < n; k++) s.push_back(buf[k] < 128 ? (char)buf[k] : '?');
    FPDFText_ClosePage(t);
    FPDF_ClosePage(p);
    return s;
}
static int PageNo(FPDF_DOCUMENT d, int i) {
    std::string s = PageText(d, i);
    size_t k = s.find("page ");
    return k == std::string::npos ? -1 : atoi(s.c_str() + k + 5);
}

int main(int argc, char** argv) {
    FPDF_InitLibrary();
    std::vector<uint8_t> bytes = ReadAll(argv[1]);
    FPDF_DOCUMENT d = FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr);
    CHECK(d);
    CHECK(FPDF_GetPageCount(d) == 60);

    // Move pages 3 and 5 (0-based 2, 4) to the front.
    CHECK(ops::MovePages(d, {4, 2}, 0));
    CHECK(PageNo(d, 0) == 3 && PageNo(d, 1) == 5 && PageNo(d, 2) == 1 && PageNo(d, 3) == 2 && PageNo(d, 4) == 4);
    // Move the first page to the end.
    CHECK(ops::MovePages(d, {0}, 59));
    CHECK(PageNo(d, 59) == 3 && PageNo(d, 0) == 5);

    // Delete two pages.
    CHECK(ops::DeletePages(d, {0, 1}));
    CHECK(FPDF_GetPageCount(d) == 58 && PageNo(d, 0) == 2);
    // Refuse to delete everything.
    std::vector<int> all;
    for (int i = 0; i < 58; i++) all.push_back(i);
    CHECK(!ops::DeletePages(d, all));

    // Duplicate pages 0 and 1: copies land at 2 and 3.
    int at = ops::DuplicatePages(d, {0, 1});
    CHECK(at == 2);
    CHECK(FPDF_GetPageCount(d) == 60 && PageNo(d, 2) == PageNo(d, 0) && PageNo(d, 3) == PageNo(d, 1));

    // Rotate page 0 clockwise; its size should swap.
    FS_SIZEF before, after;
    FPDF_GetPageSizeByIndexF(d, 0, &before);
    CHECK(ops::RotatePages(d, {0}, 1));
    FPDF_GetPageSizeByIndexF(d, 0, &after);
    printf("rotate: %.0fx%.0f -> %.0fx%.0f\n", before.width, before.height, after.width, after.height);
    CHECK(after.width == before.height && after.height == before.width);

    // Blank page, image page, text pages.
    CHECK(ops::InsertBlank(d, 1, 1));
    CHECK(FPDF_GetPageCount(d) == 61);
    std::vector<uint8_t> img(200 * 3 * 100, 0);
    for (size_t i = 0; i < img.size(); i += 3) { img[i] = 40; img[i + 1] = 40; img[i + 2] = 220; }
    CHECK(ops::InsertImage(d, 0, img.data(), 200, 100, 600));
    FS_SIZEF s;
    FPDF_GetPageSizeByIndexF(d, 0, &s);
    CHECK(s.width == 792 && s.height == 612);  // wide image -> landscape page
    std::u16string text;
    for (int i = 0; i < 130; i++) text += u"Line " + std::u16string(1, u'0' + i % 10) + u" of a long text file\tend\r\n";
    int tp = ops::InsertText(d, 62, text);
    printf("text pages: %d\n", tp);
    CHECK(tp == 3);
    CHECK(PageText(d, 62).find("Line 0 of a long text file") != std::string::npos);

    // Text decoding.
    CHECK(ops::DecodeText({0xEF, 0xBB, 0xBF, 'h', 0xC3, 0xA9}) == u"hé");
    CHECK(ops::DecodeText({'c', 'a', 'f', 0xE9}) == u"café");
    CHECK(ops::DecodeText({0xFF, 0xFE, 'h', 0, 'i', 0}) == u"hi");
    CHECK(ops::DecodeText({0x93, 'q', 0x94}) == u"“q”");

    // Insert pages from another document, then save and reload.
    FPDF_DOCUMENT src = FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr);
    CHECK(ops::InsertFrom(d, src, {0, 1, 2}, 0) == 3);
    CHECK(PageNo(d, 0) == 1 && PageNo(d, 2) == 3);
    FPDF_CloseDocument(src);
    std::vector<uint8_t> out;
    CHECK(ops::SaveToBuffer(d, out));
    int count = FPDF_GetPageCount(d);
    FPDF_DOCUMENT re = FPDF_LoadMemDocument64(out.data(), out.size(), nullptr);
    CHECK(re && FPDF_GetPageCount(re) == count);
    FILE* f = fopen("ops_out.pdf", "wb");
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    printf("saved %zu bytes, %d pages\n", out.size(), count);
    FPDF_CloseDocument(re);
    FPDF_CloseDocument(d);
    FPDF_DestroyLibrary();
    printf(failures ? "%d FAILURES\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}

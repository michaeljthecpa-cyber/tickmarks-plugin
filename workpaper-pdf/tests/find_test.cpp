// Tests for search. Usage: find_test sample.pdf
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "fpdfview.h"
#include "../src/find.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

int main(int, char** argv) {
    FPDF_InitLibrary();
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), {});
    FPDF_DOCUMENT d = FPDF_LoadMemDocument64(bytes.data(), bytes.size(), nullptr);
    CHECK(d);
    auto m = find::FindAll(d, u"cash lead schedule");
    printf("title matches: %zu\n", m.size());
    CHECK(m.size() == 60);
    CHECK(!m.empty() && m[0].page == 0 && m.back().page == 59 && !m[0].boxes.empty());
    m = find::FindAll(d, u"12345.67");  // without the comma
    printf("amount matches: %zu\n", m.size());
    CHECK(!m.empty() && m[0].page == 0);
    m = find::FindAll(d, u"2,345.67");  // part of a longer number: no match
    CHECK(m.empty());
    m = find::FindAll(d, u"ACCOUNT  1000");
    CHECK(m.size() >= 60);
    m = find::FindAll(d, u"no such words");
    CHECK(m.empty());
    auto o = find::Outline(d);
    printf("outline entries: %zu\n", o.size());
    FPDF_CloseDocument(d);
    FPDF_DestroyLibrary();
    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}

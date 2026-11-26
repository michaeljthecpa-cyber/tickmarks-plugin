// Turns dropped files (PDF, Word, text, images) into pages of an open document.
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "fpdfview.h"

// File types that can be added as pages.
bool CanConvert(const std::wstring& path);
bool IsPdfPath(const std::wstring& path);

// Inserts every file's pages at `at`, in order. Returns the number of pages added.
// Problems are described in `errors` (one line per file that failed).
// `progress` is called with a short message before slow steps (Word conversion).
int InsertFiles(FPDF_DOCUMENT doc, int at, const std::vector<std::wstring>& files, std::wstring& errors,
                void (*progress)(const wchar_t* msg));

cmake_minimum_required(VERSION 3.20)
project(WorkpaperPDF CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# PDFium prebuilt binaries (https://github.com/bblanchon/pdfium-binaries), unpacked by the build.
set(PDFIUM_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium" CACHE PATH "PDFium binaries")

add_executable(WorkpaperPDF WIN32 src/main.cpp src/pdf.cpp res/app.rc)
target_compile_definitions(WorkpaperPDF PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_include_directories(WorkpaperPDF PRIVATE src "${PDFIUM_DIR}/include")
target_link_libraries(WorkpaperPDF PRIVATE "${PDFIUM_DIR}/lib/pdfium.dll.lib" comctl32 comdlg32 shell32 gdi32 user32 advapi32)
target_compile_options(WorkpaperPDF PRIVATE /W3 /utf-8 /O2)
# The manifest comes from app.rc.
target_link_options(WorkpaperPDF PRIVATE /MANIFEST:NO)
add_custom_command(TARGET WorkpaperPDF POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFIUM_DIR}/bin/pdfium.dll" "$<TARGET_FILE_DIR:WorkpaperPDF>")

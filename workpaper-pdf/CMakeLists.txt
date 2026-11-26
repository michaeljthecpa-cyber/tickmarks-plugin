cmake_minimum_required(VERSION 3.20)
project(WorkpaperPDF CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# PDFium prebuilt binaries (https://github.com/bblanchon/pdfium-binaries), unpacked by the build.
set(PDFIUM_DIR "${CMAKE_SOURCE_DIR}/third_party/pdfium" CACHE PATH "PDFium binaries")

add_executable(WorkpaperPDF WIN32 src/main.cpp src/pdf.cpp src/pdfops.cpp src/convert.cpp src/marks.cpp src/annots.cpp res/app.rc)
target_compile_definitions(WorkpaperPDF PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(WorkpaperPDF PRIVATE src "${PDFIUM_DIR}/include")
target_link_libraries(WorkpaperPDF PRIVATE "${PDFIUM_DIR}/lib/pdfium.dll.lib" comctl32 comdlg32 shell32 gdi32 user32 advapi32 ole32 oleaut32 windowscodecs uuid)
target_compile_options(WorkpaperPDF PRIVATE /W3 /utf-8 /O2)
# The manifest comes from app.rc.
target_link_options(WorkpaperPDF PRIVATE /MANIFEST:NO)
add_custom_command(TARGET WorkpaperPDF POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PDFIUM_DIR}/bin/pdfium.dll" "$<TARGET_FILE_DIR:WorkpaperPDF>")

# Unit tests for the page editing engine.
add_executable(ops_test tests/ops_test.cpp src/pdfops.cpp)
target_include_directories(ops_test PRIVATE src "${PDFIUM_DIR}/include")
target_link_libraries(ops_test PRIVATE "${PDFIUM_DIR}/lib/pdfium.dll.lib")
target_compile_options(ops_test PRIVATE /utf-8 /D_CRT_SECURE_NO_WARNINGS)

# Unit tests for tickmarks and comments.
add_executable(annots_test tests/annots_test.cpp src/annots.cpp src/marks.cpp src/pdfops.cpp)
target_include_directories(annots_test PRIVATE src "${PDFIUM_DIR}/include")
target_link_libraries(annots_test PRIVATE "${PDFIUM_DIR}/lib/pdfium.dll.lib")
target_compile_options(annots_test PRIVATE /utf-8 /D_CRT_SECURE_NO_WARNINGS)

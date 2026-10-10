#include "util/jpeg.h"

#include <jpeglib.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace tak::jpeg {

namespace {

// libjpeg's error_exit must not return. Throw instead of the classic
// setjmp/longjmp: on Win64 MinGW the longjmp is an SEH unwind that clobbered
// the caller's stack (heap/stack corruption after a corrupt JPEG), whereas a
// C++ throw unwinds through the C frames via the ordinary unwind tables.
void onError(j_common_ptr) {
    throw std::runtime_error("JPEG decode failed");
}

} // namespace

Image load(const std::vector<uint8_t>& d) {
    jpeg_decompress_struct cinfo{};
    jpeg_error_mgr err{};
    cinfo.err = jpeg_std_error(&err);
    err.error_exit = onError;

    try {
        jpeg_create_decompress(&cinfo);
        jpeg_mem_src(&cinfo, d.data(), static_cast<unsigned long>(d.size()));
        jpeg_read_header(&cinfo, TRUE);
        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);

        Image img;
        img.width = int(cinfo.output_width);
        img.height = int(cinfo.output_height);
        img.rgba.resize(size_t(img.width) * img.height * 4);

        std::vector<uint8_t> row(size_t(img.width) * 3);
        uint8_t* rowPtr = row.data();
        while (cinfo.output_scanline < cinfo.output_height) {
            int y = int(cinfo.output_scanline);
            jpeg_read_scanlines(&cinfo, &rowPtr, 1);
            uint8_t* dst = &img.rgba[size_t(y) * img.width * 4];
            for (int x = 0; x < img.width; ++x) {
                dst[x * 4] = row[x * 3];
                dst[x * 4 + 1] = row[x * 3 + 1];
                dst[x * 4 + 2] = row[x * 3 + 2];
                dst[x * 4 + 3] = 255;
            }
        }
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return img;
    } catch (...) {
        jpeg_destroy_decompress(&cinfo);
        throw;
    }
}

} // namespace tak::jpeg

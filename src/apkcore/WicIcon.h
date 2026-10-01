#pragma once
// WicIcon — 使用 Windows Imaging Component 解码图片（PNG/JPEG/WebP…）
#include <cstdint>
#include <vector>
#include <windows.h>

namespace apk {

// 把任意受支持格式的图片解码为 sizePx 内的 BGRA（预乘 alpha）像素
bool decodeImageToBgra(const uint8_t *data, size_t size, int maxSize, std::vector<uint8_t> *bgra, int *width,
                       int *height);

// 从图片字节生成 sizePx x sizePx 的 HICON（保持纵横比，居中）。失败返回 nullptr。
HICON createHIconFromImageData(const uint8_t *data, size_t size, int sizePx);

} // namespace apk

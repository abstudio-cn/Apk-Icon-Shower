#pragma once
// ApkInfo — 从 APK 中读取应用信息与启动图标
#include <cstdint>
#include <string>
#include <vector>

namespace apk {

struct ApkInfo {
    bool ok = false;
    std::string packageName;
    std::string label;
    std::string versionName;
    std::string iconEntry;              // 归档内路径，例如 res/AB.png
    std::vector<uint8_t> iconData;      // 原始图片字节（PNG/WebP）
    int iconWidth = 0;
    int iconHeight = 0;
    std::string source;                 // 图标来源说明：manifest / adaptive / heuristic
    std::string error;
};

// 读取 APK 的基本信息与图标。失败时 info->ok == false，error 有说明。
bool readApkInfo(const std::wstring &apkPath, ApkInfo *info);

// 仅读取图标（性能优先，供 shell 扩展使用）
bool readApkIcon(const std::wstring &apkPath, std::vector<uint8_t> *iconData,
                 std::string *entryName);

// 读取 PNG 尺寸（无解码依赖）
bool pngSize(const uint8_t *data, size_t size, int *w, int *h);

} // namespace apk

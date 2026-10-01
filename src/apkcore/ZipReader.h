#pragma once
// ZipReader — 基于 miniz 的只读 ZIP 随机访问（APK 读取）
#include <cstdint>
#include <string>
#include <vector>

namespace apk {

class ZipReader {
public:
    ZipReader();
    ~ZipReader();
    ZipReader(const ZipReader &) = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool open(const std::wstring &path);
    void close();
    bool isOpen() const { return open_; }

    uint32_t entryCount() const;
    bool entryNameAt(uint32_t index, std::string *out) const;
    std::vector<std::string> listEntries() const;

    bool readEntry(const std::string &name, std::vector<uint8_t> *out) const;
    bool readEntryAt(uint32_t index, std::vector<uint8_t> *out) const;

private:
    struct Impl;
    Impl *d_ = nullptr;
    bool open_ = false;
};

} // namespace apk

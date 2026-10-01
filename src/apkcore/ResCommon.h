#pragma once
// ResCommon — Android 资源二进制（AXML / ARSC）通用读取辅助
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>

namespace apk {

static const uint16_t CHUNK_STRING_POOL   = 0x0001;
static const uint16_t CHUNK_TABLE         = 0x0002;
static const uint16_t CHUNK_XML           = 0x0003;
static const uint16_t CHUNK_XML_FIRST     = 0x0100;
static const uint16_t CHUNK_XML_START_NS  = 0x0100;
static const uint16_t CHUNK_XML_END_NS    = 0x0101;
static const uint16_t CHUNK_XML_START_EL  = 0x0102;
static const uint16_t CHUNK_XML_END_EL    = 0x0103;
static const uint16_t CHUNK_XML_CDATA     = 0x0104;
static const uint16_t CHUNK_XML_LAST      = 0x017f;
static const uint16_t CHUNK_XML_RESMAP    = 0x0180;
static const uint16_t CHUNK_TABLE_PACKAGE = 0x0200;
static const uint16_t CHUNK_TABLE_TYPE    = 0x0201;
static const uint16_t CHUNK_TABLE_TYPE_SPEC = 0x0202;

static const uint8_t  TYPE_NULL      = 0x00;
static const uint8_t  TYPE_REFERENCE = 0x01;
static const uint8_t  TYPE_STRING    = 0x03;
static const uint8_t  TYPE_INT_DEC   = 0x10;

static const uint32_t NO_ENTRY = 0xFFFFFFFFu;

struct ChunkHeader {
    uint16_t type;
    uint16_t headerSize;
    uint32_t size;
};

inline bool readU16(const uint8_t *p, size_t size, size_t off, uint16_t *out)
{
    if (off + 2 > size) return false;
    memcpy(out, p + off, 2);
    return true;
}
inline bool readU32(const uint8_t *p, size_t size, size_t off, uint32_t *out)
{
    if (off + 4 > size) return false;
    memcpy(out, p + off, 4);
    return true;
}
inline bool readChunkHeader(const uint8_t *p, size_t size, size_t off, ChunkHeader *out)
{
    if (!readU16(p, size, off, &out->type)) return false;
    if (!readU16(p, size, off + 2, &out->headerSize)) return false;
    if (!readU32(p, size, off + 4, &out->size)) return false;
    if (out->size < 8) return false;
    if (off + out->size > size) return false;
    return true;
}

// UTF-16LE -> UTF-8
inline std::string utf16ToUtf8(const uint16_t *w, size_t len)
{
    if (!w || len == 0) return std::string();
    // 先计算长度
    int n = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t *>(w), (int)len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string s;
    s.resize((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t *>(w), (int)len, &s[0], n, nullptr, nullptr);
    return s;
}

// 解析 Android ResStringPool（UTF-8 或 UTF-16 编码）
class StringPool {
public:
    bool parse(const uint8_t *base, size_t baseSize, size_t chunkOff);

    size_t count() const { return strings_.size(); }

    const std::string &at(size_t i) const
    {
        static const std::string empty;
        if (i >= strings_.size()) return empty;
        return strings_[i];
    }

private:
    std::vector<std::string> strings_;
};

// 读取一个变长长度字段：<0x80 时 1 字节；否则首字节高位置 1 表示 2 字节
inline bool readVarLen(const uint8_t *base, size_t baseSize, size_t q, uint32_t *value, size_t *fieldSize)
{
    if (q >= baseSize) return false;
    const uint8_t b0 = base[q];
    if (b0 & 0x80) {
        if (q + 1 >= baseSize) return false;
        *value = ((uint32_t)(b0 & 0x7F) << 8) | (uint32_t)base[q + 1];
        *fieldSize = 2;
    } else {
        *value = b0;
        *fieldSize = 1;
    }
    return true;
}

inline bool StringPool::parse(const uint8_t *base, size_t baseSize, size_t chunkOff)
{
    ChunkHeader h;
    if (!readChunkHeader(base, baseSize, chunkOff, &h)) return false;
    if (h.type != CHUNK_STRING_POOL) return false;

    uint32_t stringCount = 0, styleCount = 0, flags = 0, stringsStart = 0;
    if (!readU32(base, baseSize, chunkOff + 8, &stringCount)) return false;
    if (!readU32(base, baseSize, chunkOff + 12, &styleCount)) return false;
    if (!readU32(base, baseSize, chunkOff + 16, &flags)) return false;
    if (!readU32(base, baseSize, chunkOff + 20, &stringsStart)) return false;
    (void)styleCount;

    const bool utf8 = (flags & 0x100) != 0;
    const size_t stringsBase = chunkOff + stringsStart;
    if (stringsBase > baseSize) return false;

    const size_t indexBase = chunkOff + h.headerSize;
    strings_.clear();
    strings_.reserve(stringCount);

    std::vector<uint32_t> offs(stringCount, 0);
    for (uint32_t i = 0; i < stringCount; ++i)
        if (!readU32(base, baseSize, indexBase + (size_t)i * 4, &offs[i])) offs[i] = 0;

    // 取长度：mode 0 = 单长度（字节数）；mode 1 = 双长度（字符数 + 字节数）
    auto decodeLen = [&](size_t p, int mode, uint32_t *len, size_t *hdr) -> bool {
        uint32_t v1 = 0;
        size_t s1 = 0;
        if (!readVarLen(base, baseSize, p, &v1, &s1)) return false;
        if (mode == 0) {
            *len = v1;
            *hdr = s1;
            return true;
        }
        uint32_t v2 = 0;
        size_t s2 = 0;
        if (!readVarLen(base, baseSize, p + s1, &v2, &s2)) return false;
        *len = v2;
        *hdr = s1 + s2;
        return true;
    };

    // 自动探测：用相邻 offset 的步长投票（部分 APK 的 ARSC 使用“字符数+字节数”双长度）
    int votesSingle = 0, votesDoubled = 0;
    if (utf8) {
        for (uint32_t i = 0; i + 1 < stringCount && (votesSingle + votesDoubled) < 400; ++i) {
            if (offs[i + 1] <= offs[i]) continue;
            const size_t stride = (size_t)(offs[i + 1] - offs[i]);
            const size_t p = stringsBase + offs[i];
            for (int mode = 0; mode < 2; ++mode) {
                uint32_t len = 0;
                size_t hdr = 0;
                if (!decodeLen(p, mode, &len, &hdr)) continue;
                if (hdr + (size_t)len + 1 != stride) continue;
                if (p + hdr + len >= baseSize) continue;
                if (base[p + hdr + len] != 0) continue;
                if (mode == 0) votesSingle++;
                else votesDoubled++;
            }
        }
    }
    const int lenMode = (votesDoubled > votesSingle) ? 1 : 0;

    for (uint32_t i = 0; i < stringCount; ++i) {
        const size_t p = stringsBase + offs[i];
        if (p >= baseSize) {
            strings_.push_back(std::string());
            continue;
        }
        if (utf8) {
            uint32_t len = 0;
            size_t hdr = 0;
            int mode = lenMode;
            if (!decodeLen(p, mode, &len, &hdr)) {
                strings_.push_back(std::string());
                continue;
            }
            // 越界时退回另一种解释
            if (p + hdr + len >= baseSize) {
                int alt = 1 - mode;
                uint32_t l2 = 0;
                size_t h2 = 0;
                if (decodeLen(p, alt, &l2, &h2) && p + h2 + l2 < baseSize) {
                    len = l2;
                    hdr = h2;
                } else {
                    strings_.push_back(std::string());
                    continue;
                }
            }
            size_t actual = 0;
            while (actual < len && base[p + hdr + actual] != 0) actual++;
            strings_.push_back(std::string(reinterpret_cast<const char *>(base + p + hdr), actual));
        } else {
            size_t q = p;
            uint32_t len = 0;
            if (q + 2 > baseSize) {
                strings_.push_back(std::string());
                continue;
            }
            memcpy(&len, base + q, 2);
            if (len & 0x8000) {
                uint16_t hi = 0, lo = 0;
                memcpy(&hi, base + q, 2);
                memcpy(&lo, base + q + 2, 2);
                len = ((uint32_t)(hi & 0x7FFF) << 16) | lo;
                q += 4;
            } else {
                q += 2;
            }
            if (q + (size_t)len * 2 > baseSize) {
                strings_.push_back(std::string());
                continue;
            }
            strings_.push_back(utf16ToUtf8(reinterpret_cast<const uint16_t *>(base + q), len));
        }
    }
    return true;
}

} // namespace apk

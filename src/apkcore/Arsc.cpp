#include "Arsc.h"

#include <algorithm>

namespace apk {

namespace {

// 优选度：非本地化优先；密度越大越优先
int scoreOf(const ArscValue &v)
{
    int s = 0;
    if (!v.localized) s += 1000000;
    if (v.density != 0xFFFF) s += (int)v.density;
    return s;
}

std::string langOf(const uint8_t *cfg, size_t cfgSize)
{
    // ResTable_config: size(4) imsi.mcc(2) imsi.mnc(2) language[2] country[2] ...
    if (cfgSize < 12) return std::string();
    char lang[3] = {0, 0, 0};
    lang[0] = (char)cfg[8];
    lang[1] = (char)cfg[9];
    if (lang[0] == 0) return std::string();
    return std::string(lang);
}

uint16_t densityOf(const uint8_t *cfg, size_t cfgSize)
{
    if (cfgSize < 16) return 0;
    uint16_t d = 0;
    memcpy(&d, cfg + 14, 2);
    return d;
}

} // namespace

bool ArscTable::parse(const uint8_t *data, size_t size, const std::set<uint32_t> &wanted)
{
    entries_.clear();
    pool_ = StringPool();

    if (!data || size < 12) return false;
    ChunkHeader root;
    if (!readChunkHeader(data, size, 0, &root)) return false;
    if (root.type != CHUNK_TABLE) return false;

    size_t off = root.headerSize;
    while (off + 8 <= size) {
        ChunkHeader h;
        if (!readChunkHeader(data, size, off, &h)) break;

        if (h.type == CHUNK_STRING_POOL) {
            pool_.parse(data, size, off);
        } else if (h.type == CHUNK_TABLE_PACKAGE) {
            uint32_t pkgId = 0;
            readU32(data, size, off + 8, &pkgId);
            uint32_t typeStrings = 0, keyStrings = 0;
            // ResTable_package: header(8) + id(4) + name[128](256) = 268
            readU32(data, size, off + 268, &typeStrings);
            readU32(data, size, off + 276, &keyStrings);
            (void)keyStrings;

            StringPool typePool;
            const bool hasTypePool = (typeStrings > 0) && typePool.parse(data, size, off + typeStrings);

            // 遍历包内子块
            size_t sub = off + h.headerSize;
            while (sub + 8 <= off + h.size) {
                ChunkHeader sh;
                if (!readChunkHeader(data, size, sub, &sh)) break;

                if (sh.type == CHUNK_TABLE_TYPE) {
                    const uint8_t typeId = data[sub + 8];
                    const size_t cfgOff = sub + 20;
                    const uint32_t entryCount = [&]() {
                        uint32_t v = 0;
                        readU32(data, size, sub + 12, &v);
                        return v;
                    }();
                    uint32_t entriesStart = 0;
                    readU32(data, size, sub + 16, &entriesStart);
                    uint32_t cfgSize = 0;
                    readU32(data, size, cfgOff, &cfgSize);
                    if (cfgSize > sh.headerSize) cfgSize = sh.headerSize;
                    const size_t cfgBytes = cfgSize >= 4 ? (size_t)cfgSize - 4 : 0;
                    const uint8_t *cfg = data + cfgOff + 4;

                    const std::string typeName = hasTypePool ? typePool.at(typeId) : std::string();
                    const uint16_t density = densityOf(cfg, cfgBytes);
                    const std::string lang = langOf(cfg, cfgBytes);

                    const size_t offsetsBase = sub + sh.headerSize;
                    const size_t dataBase = sub + entriesStart;

                    for (uint32_t i = 0; i < entryCount; ++i) {
                        uint32_t eoff = 0;
                        if (!readU32(data, size, offsetsBase + (size_t)i * 4, &eoff)) break;
                        if (eoff == NO_ENTRY) continue;

                        const uint32_t resId = (pkgId << 24) | ((uint32_t)typeId << 16) | i;
                        if (!wanted.empty() && wanted.find(resId) == wanted.end()) continue;

                        const size_t e = dataBase + eoff;
                        uint16_t eSize = 0, eFlags = 0;
                        if (!readU16(data, size, e, &eSize)) continue;
                        if (!readU16(data, size, e + 2, &eFlags)) continue;
                        if (eFlags & 0x0001) continue;    // 复杂条目（bag），忽略
                        if (eSize < 8) eSize = 8;
                        const size_t v = e + eSize;
                        uint16_t vsize = 0;
                        if (!readU16(data, size, v, &vsize)) continue;
                        if (vsize < 8) continue;

                        ArscValue av;
                        av.dataType = data[v + 3];
                        readU32(data, size, v + 4, &av.data);
                        av.density = density;
                        av.language = lang;
                        av.localized = !lang.empty();
                        av.typeName = typeName;
                        entries_[resId].push_back(av);
                    }
                }
                if (sh.size == 0) break;
                sub += sh.size;
            }
        }
        if (h.size == 0) break;
        off += h.size;
    }

    for (auto &kv : entries_) {
        std::sort(kv.second.begin(), kv.second.end(), [](const ArscValue &a, const ArscValue &b) {
            return scoreOf(a) > scoreOf(b);
        });
    }
    return true;
}

std::vector<ArscValue> ArscTable::all(uint32_t resId) const
{
    auto it = entries_.find(resId);
    if (it == entries_.end()) return {};
    return it->second;
}

bool ArscTable::best(uint32_t resId, ArscValue *out) const
{
    auto it = entries_.find(resId);
    if (it == entries_.end() || it->second.empty()) return false;
    if (out) *out = it->second.front();
    return true;
}

bool ArscTable::resolveString(uint32_t resId, std::string *out) const
{
    ArscValue v;
    if (!best(resId, &v)) return false;
    if (v.dataType == TYPE_STRING) {
        const std::string s = pool_.at(v.data);
        if (s.empty()) return false;
        if (out) *out = s;
        return true;
    }
    if (v.dataType == TYPE_REFERENCE) {
        ArscValue v2;
        if (!best(v.data, &v2)) return false;
        if (v2.dataType == TYPE_STRING) {
            const std::string s = pool_.at(v2.data);
            if (s.empty()) return false;
            if (out) *out = s;
            return true;
        }
    }
    return false;
}

bool ArscTable::resolveFilePath(uint32_t resId, std::string *out) const
{
    // 最多跟随 3 层引用
    uint32_t cur = resId;
    for (int depth = 0; depth < 4; ++depth) {
        ArscValue v;
        if (!best(cur, &v)) return false;
        if (v.dataType == TYPE_STRING) {
            const std::string s = pool_.at(v.data);
            if (s.empty()) return false;
            if (out) *out = s;
            return true;
        }
        if (v.dataType == TYPE_REFERENCE) {
            cur = v.data;
            continue;
        }
        return false;
    }
    return false;
}

} // namespace apk

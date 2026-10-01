#include "Axml.h"

namespace apk {

bool AxmlDocument::parse(const uint8_t *data, size_t size)
{
    pool_ = StringPool();
    resMap_.clear();
    elements_.clear();
    manifestIndex_ = (size_t)-1;
    applicationIndex_ = (size_t)-1;

    if (!data || size < 8) return false;

    ChunkHeader root;
    if (!readChunkHeader(data, size, 0, &root)) return false;
    if (root.type != CHUNK_XML) return false;

    size_t off = root.headerSize;
    while (off + 8 <= size) {
        ChunkHeader h;
        if (!readChunkHeader(data, size, off, &h)) break;

        if (h.type == CHUNK_STRING_POOL) {
            pool_.parse(data, size, off);
        } else if (h.type == CHUNK_XML_RESMAP) {
            const size_t n = (h.size - h.headerSize) / 4;
            resMap_.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                uint32_t v = 0;
                if (readU32(data, size, off + h.headerSize + i * 4, &v)) resMap_.push_back(v);
                else resMap_.push_back(0);
            }
        } else if (h.type == CHUNK_XML_START_EL) {
            // ResXMLTree_node(16B) + ResXMLTree_attrExt(20B)
            size_t node = off + 16;
            uint32_t nsIdx = 0, nameIdx = 0;
            uint16_t attrStart = 0, attrSize = 0, attrCount = 0;
            readU32(data, size, node + 0, &nsIdx);
            readU32(data, size, node + 4, &nameIdx);
            readU16(data, size, node + 8, &attrStart);
            readU16(data, size, node + 10, &attrSize);
            readU16(data, size, node + 12, &attrCount);
            (void)nsIdx;

            AxmlElement el;
            el.name = pool_.at(nameIdx);

            const size_t attrsBase = off + 16 + attrStart;
            if (attrSize < 20) attrSize = 20;
            for (uint16_t i = 0; i < attrCount; ++i) {
                const size_t a = attrsBase + (size_t)i * attrSize;
                if (a + 20 > off + h.size) break;
                AxmlAttr at;
                readU32(data, size, a + 0, &at.nsIndex);
                readU32(data, size, a + 4, &at.nameIndex);
                uint32_t rawIdx = 0;
                readU32(data, size, a + 8, &rawIdx);
                uint16_t vsize = 0;
                readU16(data, size, a + 12, &vsize);
                at.dataType = data[a + 15];
                readU32(data, size, a + 16, &at.data);
                at.name = pool_.at(at.nameIndex);
                at.rawValue = pool_.at(rawIdx);
                if (at.nameIndex < resMap_.size()) at.resId = resMap_[at.nameIndex];
                el.attrs.push_back(at);
            }
            if (manifestIndex_ == (size_t)-1 && el.name == "manifest") manifestIndex_ = elements_.size();
            if (applicationIndex_ == (size_t)-1 && el.name == "application")
                applicationIndex_ = elements_.size();
            elements_.push_back(std::move(el));
        }
        if (h.size == 0) break;
        off += h.size;
    }
    return !elements_.empty();
}

} // namespace apk

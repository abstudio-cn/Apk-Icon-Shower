#pragma once
// Arsc — resources.arsc 资源表解析（把资源 id 解析为字符串/文件路径）
#include "ResCommon.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace apk {

struct ArscValue {
    uint8_t dataType = 0;
    uint32_t data = 0;
    bool localized = false;   // 是否带语言限定
    uint16_t density = 0;
    std::string language;     // 两字节语言码，默认 locale 为空
    std::string typeName;     // mipmap / drawable / string ...
};

class ArscTable {
public:
    // wanted：只保留这些资源 id（0 表示全部保留，用于调试）
    bool parse(const uint8_t *data, size_t size, const std::set<uint32_t> &wanted);

    std::string stringAt(size_t index) const { return pool_.at(index); }
    size_t stringCount() const { return pool_.count(); }

    // 返回该资源 id 的最佳取值（默认语言优先，其次密度最高）
    bool best(uint32_t resId, ArscValue *out) const;

    bool resolveString(uint32_t resId, std::string *out) const;
    bool resolveFilePath(uint32_t resId, std::string *out) const;

    // 该资源 id 的所有候选（已按优选度降序）
    std::vector<ArscValue> all(uint32_t resId) const;

private:
    StringPool pool_;
    std::map<uint32_t, std::vector<ArscValue>> entries_;
};

} // namespace apk

#pragma once
// Axml — AndroidManifest.xml 二进制 XML 解析（提取 application 的 icon/label 等）
#include "ResCommon.h"
#include <string>
#include <vector>

namespace apk {

struct AxmlAttr {
    uint32_t nsIndex = 0;
    uint32_t nameIndex = 0;
    uint32_t resId = 0;   // 由 RES_XML_RESOURCE_MAP 映射得到的 android 资源 id（0 表示无）
    std::string name;
    std::string rawValue;
    uint8_t dataType = 0;
    uint32_t data = 0;
};

struct AxmlElement {
    std::string name;
    std::vector<AxmlAttr> attrs;
};

class AxmlDocument {
public:
    bool parse(const uint8_t *data, size_t size);

    const std::vector<AxmlElement> &elements() const { return elements_; }

    // 注意：elements_ 会增长导致指针失效，这里用下标访问
    const AxmlElement *manifest() const
    {
        return manifestIndex_ == (size_t)-1 ? nullptr : &elements_[manifestIndex_];
    }
    const AxmlElement *application() const
    {
        return applicationIndex_ == (size_t)-1 ? nullptr : &elements_[applicationIndex_];
    }

private:
    StringPool pool_;
    std::vector<uint32_t> resMap_;
    std::vector<AxmlElement> elements_;
    size_t manifestIndex_ = (size_t)-1;
    size_t applicationIndex_ = (size_t)-1;
};

} // namespace apk

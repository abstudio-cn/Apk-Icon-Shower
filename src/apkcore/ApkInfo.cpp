#include "ApkInfo.h"

#include "Arsc.h"
#include "Axml.h"
#include "ZipReader.h"

#include <algorithm>
#include <set>

namespace apk {

static const uint32_t kResLabel       = 0x01010001;
static const uint32_t kResIcon        = 0x01010002;
static const uint32_t kResVersionName = 0x0101021b;
static const uint32_t kResRoundIcon   = 0x0101052c;
static const uint32_t kResDrawable    = 0x01010199;

static bool endsWithNoCase(const std::string &s, const std::string &suffix)
{
    if (s.size() < suffix.size()) return false;
    return _stricmp(s.c_str() + (s.size() - suffix.size()), suffix.c_str()) == 0;
}

bool pngSize(const uint8_t *data, size_t size, int *w, int *h)
{
    if (!data || size < 24) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (memcmp(data, sig, 8) != 0) return false;
    if (memcmp(data + 12, "IHDR", 4) != 0) return false;
    auto be32 = [](const uint8_t *p) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    };
    if (w) *w = (int)be32(data + 16);
    if (h) *h = (int)be32(data + 20);
    return true;
}

namespace {

const AxmlAttr *findAttr(const AxmlElement *el, uint32_t resId, const char *plainName = nullptr)
{
    if (!el) return nullptr;
    for (const AxmlAttr &a : el->attrs) {
        if (resId != 0 && a.resId == resId) return &a;
        if (plainName && a.resId == 0 && !a.name.empty() && a.name == plainName) return &a;
    }
    return nullptr;
}

std::string attrStringValue(const AxmlAttr *a, const ArscTable *table)
{
    if (!a) return std::string();
    if (a->dataType == TYPE_STRING) {
        if (table && a->data < table->stringCount()) return table->stringAt(a->data);
        return a->rawValue;
    }
    if (a->dataType == TYPE_REFERENCE) {
        std::string s;
        if (table && table->resolveString(a->data, &s)) return s;
    }
    return a->rawValue;
}

// 兜底：在 res/ 下找最大正方形 PNG
bool heuristicIcon(const ZipReader &zip, std::vector<uint8_t> *out, std::string *entry, int *w, int *h)
{
    int bestW = -1;
    uint32_t bestIdx = 0;
    std::vector<uint8_t> bestData;
    const uint32_t n = zip.entryCount();
    for (uint32_t i = 0; i < n; ++i) {
        std::string name;
        if (!zip.entryNameAt(i, &name)) continue;
        if (name.size() > 260) continue;
        if (name.compare(0, 4, "res/") != 0) continue;
        if (!endsWithNoCase(name, ".png")) continue;
        if (endsWithNoCase(name, ".9.png")) continue;
        std::vector<uint8_t> data;
        if (!zip.readEntryAt(i, &data)) continue;
        if (data.size() > 4u * 1024 * 1024) continue;
        int pw = 0, ph = 0;
        if (!pngSize(data.data(), data.size(), &pw, &ph)) continue;
        if (pw != ph || pw < 32) continue;
        if (pw > bestW) {
            bestW = pw;
            bestIdx = i;
            bestData.swap(data);
        }
    }
    if (bestW < 0) return false;
    if (entry) zip.entryNameAt(bestIdx, entry);
    if (w) *w = bestW;
    if (h) *h = bestW;
    if (out) *out = std::move(bestData);
    return true;
}

// 自适应图标 XML：取 foreground / 最大的可解析引用图片
bool adaptiveIcon(const ZipReader &zip, const ArscTable &table, const std::string &xmlEntry,
                  std::vector<uint8_t> *out, std::string *entry, int *w, int *h)
{
    std::vector<uint8_t> xml;
    if (!zip.readEntry(xmlEntry, &xml)) return false;
    AxmlDocument doc;
    if (!doc.parse(xml.data(), xml.size())) return false;

    std::vector<std::string> candidates;
    for (const AxmlElement &el : doc.elements()) {
        const bool isForeground = (el.name == "foreground");
        for (const AxmlAttr &a : el.attrs) {
            if (a.resId != kResDrawable && !(a.resId == 0 && a.name == "drawable")) continue;
            if (a.dataType != TYPE_REFERENCE) continue;
            std::string path;
            if (!table.resolveFilePath(a.data, &path)) continue;
            if (!endsWithNoCase(path, ".png")) continue;
            if (isForeground) candidates.insert(candidates.begin(), path);
            else candidates.push_back(path);
        }
    }
    int bestW = -1;
    std::vector<uint8_t> bestData;
    std::string bestName;
    for (const std::string &c : candidates) {
        std::vector<uint8_t> data;
        if (!zip.readEntry(c, &data)) continue;
        int pw = 0, ph = 0;
        if (!pngSize(data.data(), data.size(), &pw, &ph)) continue;
        if (pw < bestW) continue;
        bestW = pw;
        bestName = c;
        bestData.swap(data);
    }
    if (bestW < 0) return false;
    if (entry) *entry = bestName;
    if (w) *w = bestW;
    if (h) *h = bestW;
    if (out) *out = std::move(bestData);
    return true;
}

} // namespace

bool readApkInfo(const std::wstring &apkPath, ApkInfo *info)
{
    if (!info) return false;
    *info = ApkInfo();

    ZipReader zip;
    if (!zip.open(apkPath)) {
        info->error = "无法打开 APK（不是有效的 ZIP 包）";
        return false;
    }

    std::vector<uint8_t> manifestData;
    if (!zip.readEntry("AndroidManifest.xml", &manifestData)) {
        info->error = "APK 内缺少 AndroidManifest.xml";
        return false;
    }

    AxmlDocument doc;
    if (!doc.parse(manifestData.data(), manifestData.size())) {
        info->error = "AndroidManifest.xml 解析失败";
        return false;
    }

    const AxmlElement *manifest = doc.manifest();
    const AxmlElement *application = doc.application();

    const AxmlAttr *pkgAttr = findAttr(manifest, 0, "package");
    if (pkgAttr) info->packageName = pkgAttr->rawValue.empty() ? attrStringValue(pkgAttr, nullptr) : pkgAttr->rawValue;

    const AxmlAttr *verAttr = findAttr(manifest, kResVersionName);
    const AxmlAttr *iconAttr = findAttr(application, kResIcon);
    const AxmlAttr *roundAttr = findAttr(application, kResRoundIcon);
    const AxmlAttr *labelAttr = findAttr(application, kResLabel);

    std::set<uint32_t> wanted;
    if (verAttr && verAttr->dataType == TYPE_REFERENCE) wanted.insert(verAttr->data);
    if (iconAttr && iconAttr->dataType == TYPE_REFERENCE) wanted.insert(iconAttr->data);
    if (roundAttr && roundAttr->dataType == TYPE_REFERENCE) wanted.insert(roundAttr->data);
    if (labelAttr && labelAttr->dataType == TYPE_REFERENCE) wanted.insert(labelAttr->data);

    ArscTable table;
    std::vector<uint8_t> arsc;
    bool haveArsc = false;
    if (zip.readEntry("resources.arsc", &arsc) && !arsc.empty()) {
        haveArsc = table.parse(arsc.data(), arsc.size(), wanted);
    }

    if (haveArsc) {
        info->versionName = attrStringValue(verAttr, &table);
        info->label = attrStringValue(labelAttr, &table);
    }
    if (info->label.empty() && labelAttr) info->label = labelAttr->rawValue;
    if (info->label.empty() && !info->packageName.empty()) info->label = info->packageName;

    std::string iconEntry;
    if (haveArsc && iconAttr && iconAttr->dataType == TYPE_REFERENCE) {
        table.resolveFilePath(iconAttr->data, &iconEntry);
    }
    if (iconEntry.empty() && haveArsc && roundAttr && roundAttr->dataType == TYPE_REFERENCE) {
        table.resolveFilePath(roundAttr->data, &iconEntry);
    }

    bool got = false;
    if (!iconEntry.empty()) {
        if (endsWithNoCase(iconEntry, ".xml")) {
            int w = 0, h = 0;
            if (haveArsc && adaptiveIcon(zip, table, iconEntry, &info->iconData, &info->iconEntry, &w, &h)) {
                info->iconWidth = w;
                info->iconHeight = h;
                info->source = "adaptive";
                got = true;
            }
        } else {
            std::vector<uint8_t> data;
            if (zip.readEntry(iconEntry, &data) && !data.empty()) {
                info->iconData.swap(data);
                info->iconEntry = iconEntry;
                pngSize(info->iconData.data(), info->iconData.size(), &info->iconWidth, &info->iconHeight);
                info->source = "manifest";
                got = true;
            }
        }
    }

    if (!got) {
        int w = 0, h = 0;
        if (heuristicIcon(zip, &info->iconData, &info->iconEntry, &w, &h)) {
            info->iconWidth = w;
            info->iconHeight = h;
            info->source = "heuristic";
            got = true;
        }
    }

    if (info->iconEntry.empty() && iconEntry.size()) info->iconEntry = iconEntry;
    info->ok = true;
    if (!got) info->error = "未找到图标资源";
    return true;
}

bool readApkIcon(const std::wstring &apkPath, std::vector<uint8_t> *iconData, std::string *entryName)
{
    ApkInfo info;
    if (!readApkInfo(apkPath, &info)) return false;
    if (info.iconData.empty()) return false;
    if (iconData) *iconData = info.iconData;
    if (entryName) *entryName = info.iconEntry;
    return true;
}

} // namespace apk

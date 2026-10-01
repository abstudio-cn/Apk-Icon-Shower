// ApkIconExtractor.cpp — .apk 文件图标处理器（IExtractIconW + IPersistFile）
#include "ShellExt.h"

#include "ApkInfo.h"
#include "AppIdentity.h"
#include "WicIcon.h"

#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <new>
#include <vector>

namespace {

// ---- 调试日志：仅当 %TEMP%\apkIconShower_debug.on 存在时启用（默认零开销） ----
bool debugEnabled()
{
    static const bool on = [] {
        wchar_t tmp[MAX_PATH] = {0};
        GetTempPathW(MAX_PATH, tmp);
        std::wstring marker = std::wstring(tmp) + L"apkIconShower_debug.on";
        return GetFileAttributesW(marker.c_str()) != INVALID_FILE_ATTRIBUTES;
    }();
    return on;
}

void logLine(const wchar_t *fmt, ...)
{
    if (!debugEnabled()) return;
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring file = std::wstring(tmp) + L"apkIconShower_shellex.log";

    wchar_t msg[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(msg, _TRUNCATE, fmt, ap);
    va_end(ap);

    HANDLE h = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[1200];
    const int n = _snwprintf_s(line, _TRUNCATE, L"[%02d:%02d:%02d.%03d] %s\r\n", st.wHour, st.wMinute, st.wSecond,
                               st.wMilliseconds, msg);
    DWORD written = 0;
    if (n > 0) WriteFile(h, line, (DWORD)(n * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(h);
}

FILETIME fileMTime(const std::wstring &path)
{
    WIN32_FILE_ATTRIBUTE_DATA fad;
    FILETIME ft;
    memset(&ft, 0, sizeof(ft));
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) ft = fad.ftLastWriteTime;
    return ft;
}

bool sameTime(const FILETIME &a, const FILETIME &b)
{
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

struct CacheEntry {
    FILETIME mtime;
    std::wstring path;
    bool parsed = false;
    bool hasIcon = false;
    std::vector<uint8_t> png;
    std::map<UINT, HICON> icons;

    void clearIcons()
    {
        for (auto &kv : icons)
            if (kv.second) DestroyIcon(kv.second);
        icons.clear();
    }
};

std::mutex g_cacheMutex;
std::map<std::wstring, CacheEntry> g_cache;

const size_t kMaxCacheEntries = 256;

// 调用者必须持有 g_cacheMutex
CacheEntry *entryFor(const std::wstring &path)
{
    if (g_cache.size() > kMaxCacheEntries) {
        for (auto &kv : g_cache) kv.second.clearIcons();
        g_cache.clear();
    }
    CacheEntry &e = g_cache[path];
    const FILETIME ft = fileMTime(path);
    if (e.parsed && !sameTime(e.mtime, ft)) {
        e.clearIcons();
        e.parsed = false;
        e.hasIcon = false;
        e.png.clear();
    }
    e.mtime = ft;
    e.path = path;
    return &e;
}

// 调用者必须持有 g_cacheMutex
HICON iconFor(CacheEntry *e, UINT size)
{
    if (!e) return nullptr;
    if (!e->parsed) {
        e->parsed = true;
        apk::ApkInfo info;
        if (apk::readApkInfo(e->path, &info) && !info.iconData.empty()) {
            e->png = info.iconData;
            e->hasIcon = true;
        } else {
            e->hasIcon = false;
        }
    }
    if (!e->hasIcon) return nullptr;
    auto it = e->icons.find(size);
    if (it != e->icons.end() && it->second) return it->second;
    HICON h = apk::createHIconFromImageData(e->png.data(), e->png.size(), (int)size);
    if (h) e->icons[size] = h;
    return h;
}

bool hasApkExtension(const std::wstring &path)
{
    const size_t pos = path.find_last_of(L'.');
    if (pos == std::wstring::npos) return false;
    return _wcsicmp(path.c_str() + pos, L".apk") == 0;
}

} // namespace

class ApkIconExtractor : public IPersistFile, public IExtractIconW {
public:
    ApkIconExtractor() { InterlockedIncrement(&g_dllRefCount); }

    // ---------------- IUnknown ----------------
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IPersist) || IsEqualIID(riid, IID_IPersistFile)) {
            *ppv = static_cast<IPersistFile *>(this);
        } else if (IsEqualIID(riid, IID_IExtractIconW)) {
            *ppv = static_cast<IExtractIconW *>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }

    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG c = InterlockedDecrement(&ref_);
        if (c == 0) delete this;
        return (ULONG)c;
    }

    // ---------------- IPersist / IPersistFile ----------------
    STDMETHODIMP GetClassID(CLSID *pClassID) override
    {
        if (!pClassID) return E_POINTER;
        return CLSIDFromString(const_cast<LPWSTR>(apk::clsidString()), pClassID);
    }
    STDMETHODIMP IsDirty() override { return S_FALSE; }

    STDMETHODIMP Load(LPCOLESTR pszFileName, DWORD) override
    {
        path_ = pszFileName ? pszFileName : L"";
        return S_OK;
    }
    STDMETHODIMP Save(LPCOLESTR, BOOL) override { return E_NOTIMPL; }
    STDMETHODIMP SaveCompleted(LPCOLESTR) override { return E_NOTIMPL; }
    STDMETHODIMP GetCurFile(LPOLESTR *ppszFileName) override
    {
        if (ppszFileName) *ppszFileName = nullptr;
        return E_NOTIMPL;
    }

    // ---------------- IExtractIconW ----------------
    STDMETHODIMP GetIconLocation(UINT, LPWSTR pszIconFile, UINT cchMax, int *piIndex, UINT *pwFlags) override
    {
        if (piIndex) *piIndex = 0;
        if (pwFlags) *pwFlags = GIL_NOTFILENAME;
        if (!pszIconFile || cchMax == 0) {
            logLine(L"GetIconLocation: no buffer, path=%s", path_.c_str());
            return S_FALSE;
        }
        pszIconFile[0] = 0;
        if (path_.empty() || !hasApkExtension(path_)) {
            logLine(L"GetIconLocation: S_FALSE (path empty or not apk) path=%s", path_.c_str());
            return S_FALSE;
        }
        // 关键：把“本文件自己的路径”写回 pszIconFile。
        //   1) GIL_NOTFILENAME 时 shell 会把这个字符串作为 pszFile 传回 Extract；
        //   2) shell 的图标缓存以该字符串为键——若留空，同一进程内所有 .apk 会共用
        //      第一个被解析的图标（表现为“三个不同 APK 显示同一个图标”）。
        wcsncpy_s(pszIconFile, cchMax, path_.c_str(), _TRUNCATE);
        logLine(L"GetIconLocation: OK path=%s", path_.c_str());
        return S_OK;
    }

    STDMETHODIMP Extract(LPCWSTR pszFile, UINT, HICON *phiconLarge, HICON *phiconSmall, UINT nIconSize) override
    {
        if (phiconLarge) *phiconLarge = nullptr;
        if (phiconSmall) *phiconSmall = nullptr;

        std::wstring path = (pszFile && *pszFile) ? pszFile : path_;
        logLine(L"Extract: pszFile=[%s] fallback_path_=[%s] size=%u", pszFile ? pszFile : L"(null)",
                path_.c_str(), nIconSize);
        if (path.empty() || !hasApkExtension(path)) return S_FALSE;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return S_FALSE;

        const UINT largeSize = (UINT)LOWORD(nIconSize);
        const UINT smallSize = (UINT)HIWORD(nIconSize);

        // 注意：Windows 头文件把 small/large 定义为宏，这里不能用它们做变量名
        HICON hLargeNew = nullptr;
        HICON hSmallNew = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_cacheMutex);
            CacheEntry *e = entryFor(path);
            const UINT ls = largeSize ? largeSize : 32;
            const UINT ss = (smallSize && smallSize != ls) ? smallSize : 16;
            HICON cachedLarge = iconFor(e, ls);
            HICON cachedSmall = iconFor(e, ss);
            if (cachedLarge) hLargeNew = CopyIcon(cachedLarge);
            if (cachedSmall) hSmallNew = CopyIcon(cachedSmall);
        }

        if (!hLargeNew && !hSmallNew) return S_FALSE;

        if (phiconLarge) *phiconLarge = hLargeNew;
        else if (hLargeNew) DestroyIcon(hLargeNew);
        if (phiconSmall) *phiconSmall = hSmallNew;
        else if (hSmallNew) DestroyIcon(hSmallNew);
        return S_OK;
    }

private:
    ~ApkIconExtractor() { InterlockedDecrement(&g_dllRefCount); }

    LONG ref_ = 1;
    std::wstring path_;
};

HRESULT CreateApkIconExtractor(REFIID riid, void **ppv)
{
    ApkIconExtractor *obj = new (std::nothrow) ApkIconExtractor();
    if (!obj) return E_OUTOFMEMORY;
    const HRESULT hr = obj->QueryInterface(riid, ppv);
    obj->Release();
    return hr;
}

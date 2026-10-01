// RegShellExt.cpp — regsvr32 注册/注销（写入 CLSID 键，并同步文件关联）
#include "ShellExt.h"

#include "AssocRegistry.h"
#include "AppIdentity.h"

#include <string>

namespace {

std::wstring selfPath()
{
    wchar_t buf[MAX_PATH * 2] = {0};
    const DWORD n = GetModuleFileNameW(g_hInstance, buf, MAX_PATH * 2);
    return n > 0 ? std::wstring(buf, n) : std::wstring();
}

} // namespace

STDAPI DllRegisterServer()
{
    const std::wstring dll = selfPath();
    if (dll.empty()) return E_FAIL;

    std::wstring err;
    if (!apk::registerClsid(dll, &err)) return E_FAIL;

    apk::AssocPaths paths;
    paths.dllPath = dll;
    const std::wstring dir = apk::moduleDir();
    paths.exePath = dir.empty() ? std::wstring() : (dir + L"\\" + apk::appExeName());
    if (!paths.exePath.empty() && GetFileAttributesW(paths.exePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        apk::registerAssociations(paths, &err);
    }
    apk::notifyShellChanged();
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    std::wstring err;
    apk::unregisterAssociations(&err);
    apk::notifyShellChanged();
    return S_OK;
}

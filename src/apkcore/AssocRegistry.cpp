#include "AssocRegistry.h"

#include "AppIdentity.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")

namespace apk {

namespace {

const wchar_t *kClasses = L"Software\\Classes";

bool setValue(HKEY root, const std::wstring &subKey, const wchar_t *name, const std::wstring &value,
              DWORD type = REG_SZ)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(root, subKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return false;
    const DWORD bytes = (DWORD)((value.size() + 1) * sizeof(wchar_t));
    const LONG rc = RegSetValueExW(key, name, 0, type, reinterpret_cast<const BYTE *>(value.c_str()), bytes);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

bool deleteTree(HKEY root, const std::wstring &subKey)
{
    const LONG rc = RegDeleteTreeW(root, subKey.c_str());
    return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}

// Windows 10/11：若该扩展名在用户 Explorer\FileExts 下存在条目但没有有效的 UserChoice，
// 则 HKCU\Software\Classes 的关联不会生效（表现为图标空白、类型名显示为 ProgID 字符串）。
// 处理方式：先备份整个子键到本程序自己的注册表位置，再移除，使关联正常生效。
const wchar_t *kFileExtsApk = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\.apk";
const wchar_t *kFileExtsBackup = L"Software\\ApkIconShower\\Backup\\FileExts_apk";

bool copyKeyTree(HKEY srcRoot, const std::wstring &srcPath, HKEY dstRoot, const std::wstring &dstPath)
{
    HKEY src = nullptr;
    if (RegOpenKeyExW(srcRoot, srcPath.c_str(), 0, KEY_READ, &src) != ERROR_SUCCESS) return false;

    HKEY dst = nullptr;
    if (RegCreateKeyExW(dstRoot, dstPath.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &dst, nullptr) !=
        ERROR_SUCCESS) {
        RegCloseKey(src);
        return false;
    }

    DWORD idx = 0;
    wchar_t name[512];
    BYTE data[8192];
    for (;;) {
        DWORD nameLen = 512;
        DWORD type = 0;
        DWORD dataLen = sizeof(data);
        const LONG rc = RegEnumValueW(src, idx, name, &nameLen, nullptr, &type, data, &dataLen);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc == ERROR_SUCCESS) RegSetValueExW(dst, name, 0, type, data, dataLen);
        idx++;
    }

    idx = 0;
    for (;;) {
        DWORD nameLen = 512;
        const LONG rc = RegEnumKeyExW(src, idx, name, &nameLen, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc == ERROR_SUCCESS) copyKeyTree(src, srcPath + L"\\" + name, dstRoot, dstPath + L"\\" + name);
        idx++;
    }

    RegCloseKey(dst);
    RegCloseKey(src);
    return true;
}

bool backupAndRemoveFileExts()
{
    HKEY probe = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kFileExtsApk, 0, KEY_READ, &probe) != ERROR_SUCCESS) return true;
    RegCloseKey(probe);

    // 已备份过就不覆盖备份
    HKEY bkp = nullptr;
    const bool haveBackup = RegOpenKeyExW(HKEY_CURRENT_USER, kFileExtsBackup, 0, KEY_READ, &bkp) == ERROR_SUCCESS;
    if (haveBackup) RegCloseKey(bkp);
    if (!haveBackup) copyKeyTree(HKEY_CURRENT_USER, kFileExtsApk, HKEY_CURRENT_USER, kFileExtsBackup);

    deleteTree(HKEY_CURRENT_USER, kFileExtsApk);
    return true;
}

bool restoreFileExts()
{
    HKEY bkp = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kFileExtsBackup, 0, KEY_READ, &bkp) != ERROR_SUCCESS) return true;
    RegCloseKey(bkp);
    HKEY cur = nullptr;
    const bool exists = RegOpenKeyExW(HKEY_CURRENT_USER, kFileExtsApk, 0, KEY_READ, &cur) == ERROR_SUCCESS;
    if (exists) RegCloseKey(cur);
    if (!exists) copyKeyTree(HKEY_CURRENT_USER, kFileExtsBackup, HKEY_CURRENT_USER, kFileExtsApk);
    return true;
}

std::wstring regClassesPath(const std::wstring &suffix)
{
    std::wstring p = kClasses;
    p += L"\\";
    p += suffix;
    return p;
}

} // namespace

std::wstring moduleDir()
{
    wchar_t buf[MAX_PATH * 2] = {0};
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&moduleDir), &mod))
        return std::wstring();
    const DWORD n = GetModuleFileNameW(mod, buf, MAX_PATH * 2);
    if (n == 0) return std::wstring();
    std::wstring path(buf, n);
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

bool isProcessElevated()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0,
                                 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

bool writeClsid(HKEY root, const std::wstring &dllPath)
{
    const std::wstring base = regClassesPath(std::wstring(L"CLSID\\") + clsidString());
    if (!setValue(root, base, nullptr, clsidDescription())) return false;
    const std::wstring inproc = base + L"\\InprocServer32";
    if (!setValue(root, inproc, nullptr, dllPath)) return false;
    if (!setValue(root, inproc, L"ThreadingModel", L"Apartment")) return false;
    return true;
}

bool registerClsid(const std::wstring &dllPath, std::wstring *error)
{
    // 必须写 HKLM，否则资源管理器不会加载进程内图标处理器
    if (writeClsid(HKEY_LOCAL_MACHINE, dllPath)) return true;

    const bool elevated = isProcessElevated();
    if (writeClsid(HKEY_CURRENT_USER, dllPath)) {
        if (error) {
            *error = elevated ? L"HKLM 写入失败，已退回 HKCU（图标可能不显示）"
                              : L"需要管理员权限才能写入 HKLM；当前仅写入 HKCU，图标可能不显示。"
                                L"请以管理员身份重新注册。";
        }
        return false;
    }
    if (error) *error = L"写入图标处理器注册表失败";
    return false;
}

bool unregisterClsid(std::wstring *error)
{
    const std::wstring base = regClassesPath(std::wstring(L"CLSID\\") + clsidString());
    deleteTree(HKEY_LOCAL_MACHINE, base);
    deleteTree(HKEY_CURRENT_USER, base);
    return true;
}

bool registerAssociations(const AssocPaths &paths, std::wstring *error)
{
    if (paths.exePath.empty() || paths.dllPath.empty()) {
        if (error) *error = L"程序路径为空";
        return false;
    }
    if (!registerClsid(paths.dllPath, error)) return false;   // error 已带说明

    const std::wstring exe = paths.exePath;
    const std::wstring openCmd = L"\"" + exe + L"\" \"%1\"";

    // .apk 扩展名
    const std::wstring apkKey = regClassesPath(L".apk");
    if (!setValue(HKEY_CURRENT_USER, apkKey, nullptr, progId())) {
        if (error) *error = L"写入 .apk 默认 ProgID 失败";
        return false;
    }
    setValue(HKEY_CURRENT_USER, apkKey, L"Content Type", L"application/vnd.android.package-archive");
    setValue(HKEY_CURRENT_USER, apkKey, L"PerceivedType", L"application");
    setValue(HKEY_CURRENT_USER, apkKey + L"\\OpenWithProgids", progId(), L"", REG_NONE);

    // ProgID：类型名“安卓应用安装包” + 图标处理器
    const std::wstring prog = regClassesPath(progId());
    if (!setValue(HKEY_CURRENT_USER, prog, nullptr, friendlyTypeName())) {
        if (error) *error = L"写入 ProgID 失败";
        return false;
    }
    setValue(HKEY_CURRENT_USER, prog, L"FriendlyTypeName", friendlyTypeName());
    setValue(HKEY_CURRENT_USER, prog, L"AppUserModelID", L"ApkIconShower.Apk");
    setValue(HKEY_CURRENT_USER, prog + L"\\DefaultIcon", nullptr, exe + L",0");
    setValue(HKEY_CURRENT_USER, prog + L"\\shell\\open\\command", nullptr, openCmd);
    setValue(HKEY_CURRENT_USER, prog + L"\\shellex\\IconHandler", nullptr, clsidString());

    // Applications\<exe>：出现在“打开方式”列表中
    const std::wstring appKey = regClassesPath(std::wstring(L"Applications\\") + appExeName());
    if (!setValue(HKEY_CURRENT_USER, appKey, nullptr, appDisplayName())) {
        if (error) *error = L"写入 Applications 键失败";
        return false;
    }
    setValue(HKEY_CURRENT_USER, appKey, L"FriendlyAppName", appDisplayName());
    setValue(HKEY_CURRENT_USER, appKey + L"\\DefaultIcon", nullptr, exe + L",0");
    setValue(HKEY_CURRENT_USER, appKey + L"\\shell\\open\\command", nullptr, openCmd);
    setValue(HKEY_CURRENT_USER, appKey + L"\\shellex\\IconHandler", nullptr, clsidString());
    setValue(HKEY_CURRENT_USER, appKey + L"\\SupportedTypes", L".apk", L"", REG_NONE);

    // 关键：移除会阻止关联生效的陈旧 FileExts 条目（先备份）
    backupAndRemoveFileExts();

    notifyShellChanged();
    refreshIconCache();
    return true;
}

bool unregisterAssociations(std::wstring *error)
{
    unregisterClsid(error);
    deleteTree(HKEY_CURRENT_USER, regClassesPath(L"ApkIconShower.apk"));
    deleteTree(HKEY_CURRENT_USER, regClassesPath(std::wstring(L"Applications\\") + appExeName()));
    // 清理扩展名键中属于本程序的条目
    const std::wstring apkKey = regClassesPath(L".apk");
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, apkKey.c_str(), 0, KEY_READ | KEY_WRITE, &key) == ERROR_SUCCESS) {
        wchar_t def[512] = {0};
        DWORD cb = sizeof(def);
        DWORD type = 0;
        if (RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE *>(def), &cb) == ERROR_SUCCESS &&
            _wcsicmp(def, progId()) == 0) {
            RegDeleteValueW(key, nullptr);
        }
        RegCloseKey(key);
        RegDeleteKeyW(HKEY_CURRENT_USER, (apkKey + L"\\OpenWithProgids").c_str());
    }
    restoreFileExts();
    notifyShellChanged();
    refreshIconCache();
    return true;
}

bool isAssociationRegistered()
{
    HKEY key = nullptr;
    const std::wstring clsidKey = regClassesPath(std::wstring(L"CLSID\\") + clsidString());
    HKEY clsidRoot = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, clsidKey.c_str(), 0, KEY_READ, &clsidRoot) == ERROR_SUCCESS) {
        RegCloseKey(clsidRoot);
    } else if (RegOpenKeyExW(HKEY_CURRENT_USER, clsidKey.c_str(), 0, KEY_READ, &clsidRoot) == ERROR_SUCCESS) {
        RegCloseKey(clsidRoot);
    } else {
        return false;
    }
    if (RegOpenKeyExW(HKEY_CURRENT_USER, (regClassesPath(progId())).c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    RegCloseKey(key);
    return true;
}

std::wstring registeredExePath()
{
    HKEY key = nullptr;
    const std::wstring cmdKey = regClassesPath(std::wstring(progId()) + L"\\shell\\open\\command");
    if (RegOpenKeyExW(HKEY_CURRENT_USER, cmdKey.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return std::wstring();
    wchar_t buf[1024] = {0};
    DWORD cb = sizeof(buf);
    DWORD type = 0;
    std::wstring result;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, reinterpret_cast<BYTE *>(buf), &cb) == ERROR_SUCCESS) {
        result = buf;
        if (result.size() > 1 && result[0] == L'"') {
            const size_t end = result.find(L'"', 1);
            if (end != std::wstring::npos) result = result.substr(1, end - 1);
        }
    }
    RegCloseKey(key);
    return result;
}

void notifyShellChanged()
{
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void refreshIconCache()
{
    wchar_t winDir[MAX_PATH] = {0};
    if (GetWindowsDirectoryW(winDir, MAX_PATH) == 0) return;
    std::wstring ie4uinit = std::wstring(winDir) + L"\\System32\\ie4uinit.exe";
    if (GetFileAttributesW(ie4uinit.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    const wchar_t *args[] = {L"-show", L"-ClearIconCache"};
    for (const wchar_t *a : args) {
        std::wstring cmd = L"\"" + ie4uinit + L"\" " + a;
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        std::wstring mutableCmd = cmd;
        if (CreateProcessW(nullptr, &mutableCmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                           &pi)) {
            WaitForSingleObject(pi.hProcess, 8000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    notifyShellChanged();
}

} // namespace apk

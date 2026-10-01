#pragma once
// AssocRegistry — 注册/注销 .apk 文件关联与图标处理器（仅当前用户，无需管理员）
#include <string>

namespace apk {

struct AssocPaths {
    std::wstring exePath;   // ApkIconShower.exe 完整路径
    std::wstring dllPath;   // ApkIconShowerShell.dll 完整路径
};

// 当前进程是否具有管理员权限
bool isProcessElevated();

// CLSID 键（图标处理器）。
// 注意：Windows 只会在 HKLM 注册时把进程内图标处理器载入资源管理器；
// 仅写 HKCU 会出现“类型名正确但图标仍是空白”的现象。因此优先写 HKLM（需要管理员），
// 失败时退回 HKCU 并在 error 中说明。
bool registerClsid(const std::wstring &dllPath, std::wstring *error);
bool unregisterClsid(std::wstring *error);

// 完整文件关联：类型名“安卓应用安装包” + 图标处理器 + 打开方式候选
bool registerAssociations(const AssocPaths &paths, std::wstring *error);
bool unregisterAssociations(std::wstring *error);

// 是否已注册（检查 ProgID 与 CLSID）
bool isAssociationRegistered();

// 取出已注册的可执行文件路径（未注册返回空）
std::wstring registeredExePath();

// 通知 shell 关联变化 + 刷新图标缓存
void notifyShellChanged();
void refreshIconCache();

// 当前进程可执行文件所在目录
std::wstring moduleDir();

} // namespace apk

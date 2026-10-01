#pragma once
// AppIdentity — 应用与文件关联的固定标识（shell 扩展与 GUI 共用）
namespace apk {

inline const wchar_t *progId() { return L"ApkIconShower.apk"; }
inline const wchar_t *friendlyTypeName() { return L"安卓应用安装包"; }
inline const wchar_t *appExeName() { return L"ApkIconShower.exe"; }
inline const wchar_t *shellDllName() { return L"ApkIconShowerShell.dll"; }
inline const wchar_t *appDisplayName() { return L"APK 图标读取器"; }
inline const wchar_t *clsidString() { return L"{9B7E5C42-3D1A-4E77-9C8F-2A5B6D4E1F30}"; }
inline const wchar_t *clsidDescription() { return L"ApkIconShower 图标处理器"; }

} // namespace apk

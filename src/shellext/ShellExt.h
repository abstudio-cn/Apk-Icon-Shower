#pragma once
// ShellExt.h — shell 扩展内部共用声明
#include <windows.h>
#include <shlobj.h>
#include <string>

extern LONG g_dllRefCount;
extern HINSTANCE g_hInstance;

// 由 ApkIconExtractor.cpp 提供
HRESULT CreateApkIconExtractor(REFIID riid, void **ppv);

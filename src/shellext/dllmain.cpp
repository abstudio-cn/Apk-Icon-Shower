// dllmain.cpp — DLL 入口、类工厂与导出
#include "ShellExt.h"

#include "AppIdentity.h"

#include <new>
#include <string>

LONG g_dllRefCount = 0;
HINSTANCE g_hInstance = nullptr;

namespace {

bool isOurClsid(REFCLSID rclsid)
{
    CLSID id;
    if (FAILED(CLSIDFromString(const_cast<LPWSTR>(apk::clsidString()), &id))) return false;
    return IsEqualCLSID(rclsid, id);
}

} // namespace

class ApkIconClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&ref_); }
    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG c = InterlockedDecrement(&ref_);
        if (c == 0) delete this;
        return (ULONG)c;
    }
    STDMETHODIMP CreateInstance(IUnknown *pUnkOuter, REFIID riid, void **ppv) override
    {
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;
        return CreateApkIconExtractor(riid, ppv);
    }
    STDMETHODIMP LockServer(BOOL lock) override
    {
        if (lock) InterlockedIncrement(&g_dllRefCount);
        else InterlockedDecrement(&g_dllRefCount);
        return S_OK;
    }

private:
    LONG ref_ = 1;
};

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!isOurClsid(rclsid)) return CLASS_E_CLASSNOTAVAILABLE;
    ApkIconClassFactory *factory = new (std::nothrow) ApkIconClassFactory();
    if (!factory) return E_OUTOFMEMORY;
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return g_dllRefCount == 0 ? S_OK : S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

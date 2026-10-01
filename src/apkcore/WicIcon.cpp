#include "WicIcon.h"

#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace apk {

namespace {

template <class T>
void safeRelease(T **p)
{
    if (*p) {
        (*p)->Release();
        *p = nullptr;
    }
}

bool ensureCom()
{
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE || hr == S_FALSE;
}

} // namespace

bool decodeImageToBgra(const uint8_t *data, size_t size, int maxSize, std::vector<uint8_t> *bgra, int *width,
                       int *height)
{
    if (!data || size == 0 || !bgra || !width || !height) return false;
    if (maxSize <= 0) maxSize = 256;

    bgra->clear();
    *width = 0;
    *height = 0;

    if (!ensureCom()) return false;

    bool ok = false;
    IWICImagingFactory *factory = nullptr;
    IWICStream *stream = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *conv = nullptr;
    IWICBitmapScaler *scaler = nullptr;

    do {
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
        if (FAILED(hr) || !factory) break;

        hr = factory->CreateStream(&stream);
        if (FAILED(hr) || !stream) break;
        hr = stream->InitializeFromMemory(const_cast<BYTE *>(data), (DWORD)size);
        if (FAILED(hr)) break;

        hr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder);
        if (FAILED(hr) || !decoder) break;

        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr) || !frame) break;

        UINT srcW = 0, srcH = 0;
        frame->GetSize(&srcW, &srcH);
        if (srcW == 0 || srcH == 0) break;

        IWICBitmapSource *source = frame;
        double scale = 1.0;
        if ((UINT)maxSize < srcW || (UINT)maxSize < srcH)
            scale = std::min((double)maxSize / (double)srcW, (double)maxSize / (double)srcH);
        const UINT dstW = (UINT)std::max(1.0, srcW * scale);
        const UINT dstH = (UINT)std::max(1.0, srcH * scale);
        if (dstW != srcW || dstH != srcH) {
            if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) && scaler) {
                if (SUCCEEDED(scaler->Initialize(frame, dstW, dstH, WICBitmapInterpolationModeHighQualityCubic)))
                    source = scaler;
            }
        }

        hr = factory->CreateFormatConverter(&conv);
        if (FAILED(hr) || !conv) break;
        hr = conv->Initialize(source, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeMedianCut);
        if (FAILED(hr)) break;

        UINT w = 0, h = 0;
        hr = conv->GetSize(&w, &h);
        if (FAILED(hr) || w == 0 || h == 0) break;

        const UINT stride = w * 4;
        const UINT bufSize = stride * h;
        bgra->assign(bufSize, 0);
        hr = conv->CopyPixels(nullptr, stride, bufSize, bgra->data());
        if (FAILED(hr)) {
            bgra->clear();
            break;
        }
        *width = (int)w;
        *height = (int)h;
        ok = true;
    } while (false);

    safeRelease(&scaler);
    safeRelease(&conv);
    safeRelease(&frame);
    safeRelease(&decoder);
    safeRelease(&stream);
    safeRelease(&factory);
    return ok;
}

HICON createHIconFromImageData(const uint8_t *data, size_t size, int sizePx)
{
    if (sizePx <= 0) sizePx = 48;
    if (sizePx > 512) sizePx = 512;

    std::vector<uint8_t> bgra;
    int w = 0, h = 0;
    if (!decodeImageToBgra(data, size, sizePx, &bgra, &w, &h)) return nullptr;

    BITMAPV5HEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = w;
    bi.bV5Height = -h;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void *bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color || !bits) {
        if (color) DeleteObject(color);
        return nullptr;
    }
    memcpy(bits, bgra.data(), bgra.size());

    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO ii;
    memset(&ii, 0, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON result = CreateIconIndirect(&ii);
    DeleteObject(color);
    if (mask) DeleteObject(mask);
    return result;
}

} // namespace apk

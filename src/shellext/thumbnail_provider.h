#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <thumbcache.h>
#include <shobjidl.h>
#include <atomic>

namespace PsdThumbnail {

// CLSID for our Native PSD/PSB Thumbnail Provider: {66FC65C5-2883-4D88-82AF-70626AF90EE8}
extern const CLSID CLSID_PsdThumbnailProvider;

class CPsdThumbnailProvider : public IInitializeWithStream, public IThumbnailProvider {
public:
    CPsdThumbnailProvider();
    virtual ~CPsdThumbnailProvider();

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppvObject) override;
    STDMETHODIMP_(ULONG) AddRef() override;
    STDMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    STDMETHODIMP Initialize(IStream* pStream, DWORD grfMode) override;

    // IThumbnailProvider
    STDMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) override;

private:
    std::atomic<LONG> m_refCount;
    IStream* m_pStream;
};

} // namespace PsdThumbnail

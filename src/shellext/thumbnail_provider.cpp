#include "thumbnail_provider.h"
#include "../core/psd_decoder.h"

namespace PsdThumbnail {

// Global DLL reference count for DllCanUnloadNow
extern std::atomic<LONG> g_dllRefCount;

// {66FC65C5-2883-4D88-82AF-70626AF90EE8}
const CLSID CLSID_PsdThumbnailProvider = 
    { 0x66fc65c5, 0x2883, 0x4d88, { 0x82, 0xaf, 0x70, 0x62, 0x6a, 0xf9, 0x0e, 0xe8 } };

CPsdThumbnailProvider::CPsdThumbnailProvider()
    : m_refCount(1), m_pStream(nullptr) {
    g_dllRefCount.fetch_add(1);
}

CPsdThumbnailProvider::~CPsdThumbnailProvider() {
    if (m_pStream) {
        m_pStream->Release();
        m_pStream = nullptr;
    }
    g_dllRefCount.fetch_sub(1);
}

STDMETHODIMP CPsdThumbnailProvider::QueryInterface(REFIID riid, void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = nullptr;

    if (IsEqualIID(riid, IID_IUnknown)) {
        *ppvObject = static_cast<IInitializeWithStream*>(this);
    } else if (IsEqualIID(riid, IID_IInitializeWithStream)) {
        *ppvObject = static_cast<IInitializeWithStream*>(this);
    } else if (IsEqualIID(riid, __uuidof(IThumbnailProvider))) {
        *ppvObject = static_cast<IThumbnailProvider*>(this);
    } else {
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) CPsdThumbnailProvider::AddRef() {
    return static_cast<ULONG>(m_refCount.fetch_add(1) + 1);
}

STDMETHODIMP_(ULONG) CPsdThumbnailProvider::Release() {
    ULONG newCount = static_cast<ULONG>(m_refCount.fetch_sub(1) - 1);
    if (newCount == 0) {
        delete this;
    }
    return newCount;
}

STDMETHODIMP CPsdThumbnailProvider::Initialize(IStream* pStream, DWORD grfMode) {
    if (!pStream) return E_INVALIDARG;

    if (m_pStream) {
        m_pStream->Release();
        m_pStream = nullptr;
    }

    m_pStream = pStream;
    m_pStream->AddRef();
    return S_OK;
}

STDMETHODIMP CPsdThumbnailProvider::GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    if (!phbmp || !pdwAlpha) return E_POINTER;
    *phbmp = NULL;
    *pdwAlpha = WTSAT_UNKNOWN;

    if (!m_pStream) return E_UNEXPECTED;

    try {
        return PsdDecoder::Decode(m_pStream, cx, phbmp, pdwAlpha);
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace PsdThumbnail

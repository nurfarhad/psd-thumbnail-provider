#pragma once

#include <windows.h>
#include <objidl.h>
#include <thumbcache.h>
#include <cstdint>
#include <vector>

namespace PsdThumbnail {

struct PsdHeader {
    uint16_t version = 0;       // 1 = PSD, 2 = PSB
    uint16_t channels = 0;      // 1 to 56
    uint32_t height = 0;
    uint32_t width = 0;
    uint16_t depth = 0;         // 1, 8, 16, 32
    uint16_t colorMode = 0;     // 0=Bitmap, 1=Grayscale, 2=Indexed, 3=RGB, 4=CMYK, 7=Multichannel, 8=Duotone, 9=Lab
};

enum class DecodeSource {
    None,
    EmbeddedThumbnail,
    XmpThumbnail,
    CompositeImageData
};

struct DecodeResult {
    HRESULT hr = E_FAIL;
    DecodeSource source = DecodeSource::None;
    uint32_t originalWidth = 0;
    uint32_t originalHeight = 0;
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    bool hasAlpha = false;
};

class PsdDecoder {
public:
    // Decodes thumbnail from stream into 32-bpp BGRA DIB HBITMAP scaled to maxEdge.
    static HRESULT Decode(
        IStream* pStream,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        DecodeResult* pResult = nullptr
    );

private:
    static bool ReadHeader(IStream* pStream, PsdHeader& header);
    
    // Fast path: Resource 1036 / 1033 embedded JPEG/raw thumbnail
    static HRESULT TryEmbeddedThumbnail(
        IStream* pStream,
        uint64_t resSectionOffset,
        uint64_t resSectionLength,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        DecodeResult* pResult
    );

    // Fast path 2: XMP packet search for embedded base64 JPEG
    static HRESULT TryXmpThumbnail(
        IStream* pStream,
        uint64_t resSectionOffset,
        uint64_t resSectionLength,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        DecodeResult* pResult
    );

    // AI / PDF format XMP thumbnail search
    static HRESULT TryAiThumbnail(
        IStream* pStream,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        DecodeResult* pResult
    );

    // Secondary path: Image Data composite decompression (Raw or PackBits RLE)
    static HRESULT DecodeComposite(
        IStream* pStream,
        const PsdHeader& header,
        uint64_t imgDataOffset,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        DecodeResult* pResult
    );

    // WIC decode and scaling helper
    static HRESULT CreateDibFromMemory(
        const uint8_t* pData,
        size_t dataSize,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType
    );

    // Utility: stream read helpers
    static bool StreamRead(IStream* pStream, void* buffer, ULONG bytesToRead);
    static bool StreamSeek(IStream* pStream, int64_t offset, DWORD origin, uint64_t* pNewPos = nullptr);
    static uint16_t ReadUInt16BE(const uint8_t* p);
    static uint32_t ReadUInt32BE(const uint8_t* p);
    static uint64_t ReadUInt64BE(const uint8_t* p);
};

} // namespace PsdThumbnail

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objidl.h>
#include <thumbcache.h>
#include <cstdint>
#include <vector>
#include <string>

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

    // Shared XMP thumbnail extractor for PSD and AI formats (D-07)
    static bool ExtractXmpThumbnailData(
        const std::string& content,
        std::vector<uint8_t>& outJpeg
    );

private:
    static bool ReadHeader(IStream* pStream, PsdHeader& header, uint64_t streamSize);
    static bool IsCompositeSupported(const PsdHeader& header);
    
    // Fast path: Resource 1036 / 1033 embedded JPEG/raw thumbnail
    static HRESULT TryEmbeddedThumbnail(
        IStream* pStream,
        uint64_t resSectionOffset,
        uint64_t resSectionLength,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        uint32_t* pThumbW = nullptr,
        uint32_t* pThumbH = nullptr,
        std::vector<uint8_t>* pOutRawJpeg = nullptr
    );

    // Fast path 2: XMP Metadata embedded thumbnail (Resource 1060)
    static HRESULT TryXmpThumbnail(
        IStream* pStream,
        uint64_t resSectionOffset,
        uint64_t resSectionLength,
        UINT maxEdge,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        uint32_t* pThumbW = nullptr,
        uint32_t* pThumbH = nullptr
    );

    // AI / PDF format XMP thumbnail search with sniffing & head+tail scanning
    static HRESULT TryAiThumbnail(
        IStream* pStream,
        uint64_t streamSize,
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
        uint64_t streamSize,
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
        WTS_ALPHATYPE* pAlphaType,
        uint32_t* pOutW = nullptr,
        uint32_t* pOutH = nullptr
    );

    // High-quality WIC scaler for composite bitmap down to maxEdge
    static HRESULT ScaleBitmapToDib(
        const uint32_t* pPixels,
        uint32_t width,
        uint32_t height,
        UINT maxEdge,
        bool hasAlpha,
        HBITMAP* phBitmap,
        WTS_ALPHATYPE* pAlphaType,
        uint32_t* pOutW,
        uint32_t* pOutH
    );

    // Utility: stream read helpers with bounds checking
    static bool StreamGetSize(IStream* pStream, uint64_t& outSize);
    static bool StreamRead(IStream* pStream, void* buffer, ULONG bytesToRead);
    static bool StreamSeek(IStream* pStream, int64_t offset, DWORD origin, uint64_t* pNewPos = nullptr);
    static uint16_t ReadUInt16BE(const uint8_t* p);
    static uint32_t ReadUInt32BE(const uint8_t* p);
    static uint64_t ReadUInt64BE(const uint8_t* p);
};

} // namespace PsdThumbnail

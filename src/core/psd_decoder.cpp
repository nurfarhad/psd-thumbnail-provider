#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include "psd_decoder.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <string>
#include <vector>
#include <cstring>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "crypt32.lib")

namespace PsdThumbnail {

uint16_t PsdDecoder::ReadUInt16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t PsdDecoder::ReadUInt32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
            static_cast<uint32_t>(p[3]);
}

uint64_t PsdDecoder::ReadUInt64BE(const uint8_t* p) {
    return (static_cast<uint64_t>(ReadUInt32BE(p)) << 32) | ReadUInt32BE(p + 4);
}

bool PsdDecoder::StreamGetSize(IStream* pStream, uint64_t& outSize) {
    STATSTG stat;
    if (SUCCEEDED(pStream->Stat(&stat, STATFLAG_NONAME))) {
        outSize = stat.cbSize.QuadPart;
        return true;
    }
    return false;
}

bool PsdDecoder::StreamRead(IStream* pStream, void* buffer, ULONG bytesToRead) {
    ULONG bytesRead = 0;
    HRESULT hr = pStream->Read(buffer, bytesToRead, &bytesRead);
    return SUCCEEDED(hr) && (bytesRead == bytesToRead);
}

bool PsdDecoder::StreamSeek(IStream* pStream, int64_t offset, DWORD origin, uint64_t* pNewPos) {
    LARGE_INTEGER li;
    li.QuadPart = offset;
    ULARGE_INTEGER uli;
    HRESULT hr = pStream->Seek(li, origin, &uli);
    if (FAILED(hr)) return false;
    if (pNewPos) *pNewPos = uli.QuadPart;
    return true;
}

bool PsdDecoder::ReadHeader(IStream* pStream, PsdHeader& header, uint64_t streamSize) {
    if (streamSize < 26) return false;
    uint8_t buf[26];
    if (!StreamSeek(pStream, 0, STREAM_SEEK_SET)) return false;
    if (!StreamRead(pStream, buf, 26)) return false;

    // Check signature '8BPS'
    if (buf[0] != '8' || buf[1] != 'B' || buf[2] != 'P' || buf[3] != 'S')
        return false;

    header.version = ReadUInt16BE(buf + 4);
    if (header.version != 1 && header.version != 2) return false;

    header.channels = ReadUInt16BE(buf + 12);
    header.height = ReadUInt32BE(buf + 14);
    header.width = ReadUInt32BE(buf + 18);
    header.depth = ReadUInt16BE(buf + 22);
    header.colorMode = ReadUInt16BE(buf + 24);

    // Sanity validation
    if (header.width == 0 || header.height == 0) return false;
    if (header.version == 1 && (header.width > 30000 || header.height > 30000)) return false;
    if (header.version == 2 && (header.width > 300000 || header.height > 300000)) return false;
    if (header.channels < 1 || header.channels > 56) return false;

    return true;
}

bool PsdDecoder::IsCompositeSupported(const PsdHeader& header) {
    // We only support 8-bit depth composite decompression for Grayscale(1), RGB(3), and CMYK(4)
    if (header.depth != 8) return false;
    if (header.colorMode != 1 && header.colorMode != 3 && header.colorMode != 4) return false;
    return true;
}

// D-07: Unified XMP thumbnail extractor for PSD (Resource 1060) and AI/PDF
bool PsdDecoder::ExtractXmpThumbnailData(const std::string& content, std::vector<uint8_t>& outJpeg) {
    size_t posOpen = std::string::npos;
    bool isAttribute = false;

    // 1. Try attribute format: xmpGImg:image="
    const std::string attrPattern = "xmpGImg:image=\"";
    posOpen = content.find(attrPattern);
    if (posOpen != std::string::npos) {
        posOpen += attrPattern.length();
        isAttribute = true;
    } else {
        // Try attribute format: image="
        const std::string attrPattern2 = "image=\"";
        size_t cand = content.find(attrPattern2);
        if (cand != std::string::npos && cand > 10 && content.substr(cand - 10, 10).find("GImg") != std::string::npos) {
            posOpen = cand + attrPattern2.length();
            isAttribute = true;
        } else {
            // 2. Try element format: <xmpGImg:image>
            const std::string tagOpen = "<xmpGImg:image>";
            posOpen = content.find(tagOpen);
            if (posOpen != std::string::npos) {
                posOpen += tagOpen.length();
                isAttribute = false;
            } else {
                const std::string altOpen = ":image>";
                posOpen = content.find(altOpen);
                if (posOpen != std::string::npos) {
                    posOpen += altOpen.length();
                    isAttribute = false;
                } else {
                    return false;
                }
            }
        }
    }

    size_t posClose = std::string::npos;
    if (isAttribute) {
        posClose = content.find("\"", posOpen);
    } else {
        posClose = content.find("</", posOpen);
    }

    if (posClose == std::string::npos || posClose <= posOpen) return false;

    std::string b64 = content.substr(posOpen, posClose - posOpen);

    // Unescape XML entities commonly found in XMP base64 strings
    auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = 0;
        while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
            str.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    };

    replaceAll(b64, "&#xA;", "");
    replaceAll(b64, "&#xD;", "");
    replaceAll(b64, "&#x9;", "");
    replaceAll(b64, "&#10;", "");
    replaceAll(b64, "&#13;", "");
    replaceAll(b64, "&quot;", "");
    replaceAll(b64, "&apos;", "");

    // Remove whitespace and newlines
    b64.erase(std::remove_if(b64.begin(), b64.end(), [](char c) {
        return c == '\r' || c == '\n' || c == ' ' || c == '\t';
    }), b64.end());

    if (b64.empty()) return false;

    DWORD dwBinaryLen = 0;
    if (CryptStringToBinaryA(
            b64.c_str(), static_cast<DWORD>(b64.length()),
            CRYPT_STRING_BASE64, NULL, &dwBinaryLen, NULL, NULL) && dwBinaryLen > 0) {
        outJpeg.resize(dwBinaryLen);
        if (CryptStringToBinaryA(
                b64.c_str(), static_cast<DWORD>(b64.length()),
                CRYPT_STRING_BASE64, outJpeg.data(), &dwBinaryLen, NULL, NULL)) {
            outJpeg.resize(dwBinaryLen);
            return true;
        }
    }
    return false;
}

HRESULT PsdDecoder::Decode(
    IStream* pStream,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    DecodeResult* pResult
) {
    if (!pStream || !phBitmap || !pAlphaType) return E_POINTER;
    *phBitmap = NULL;
    *pAlphaType = WTSAT_UNKNOWN;

    DecodeResult localResult;
    if (!pResult) pResult = &localResult;

    uint64_t streamSize = 0;
    StreamGetSize(pStream, streamSize);

    PsdHeader header;
    if (!ReadHeader(pStream, header, streamSize)) {
        // Check for AI / PDF format with embedded XMP thumbnail (D-09)
        HRESULT hrAi = TryAiThumbnail(pStream, streamSize, maxEdge, phBitmap, pAlphaType, pResult);
        if (SUCCEEDED(hrAi)) {
            pResult->hr = S_OK;
            pResult->source = DecodeSource::XmpThumbnail;
            return S_OK;
        }
        pResult->hr = E_FAIL;
        return E_FAIL;
    }

    pResult->originalWidth = header.width;
    pResult->originalHeight = header.height;

    // Skip Color Mode Data Section
    uint8_t lenBuf[4];
    if (!StreamRead(pStream, lenBuf, 4)) return E_FAIL;
    uint32_t colorModeLen = ReadUInt32BE(lenBuf);
    if (!StreamSeek(pStream, colorModeLen, STREAM_SEEK_CUR)) return E_FAIL;

    // Image Resources Section
    if (!StreamRead(pStream, lenBuf, 4)) return E_FAIL;
    uint32_t resSectionLen = ReadUInt32BE(lenBuf);
    uint64_t resSectionOffset = 0;
    if (!StreamSeek(pStream, 0, STREAM_SEEK_CUR, &resSectionOffset)) return E_FAIL;

    // Compute offset to Layer and Mask section and Image Data section in advance
    uint64_t afterResOffset = resSectionOffset + resSectionLen;
    uint64_t imgDataOffset = 0;
    bool hasCompositeOffset = false;

    if (StreamSeek(pStream, static_cast<int64_t>(afterResOffset), STREAM_SEEK_SET)) {
        uint64_t layerMaskLen = 0;
        if (header.version == 1) {
            if (StreamRead(pStream, lenBuf, 4)) {
                layerMaskLen = ReadUInt32BE(lenBuf);
                hasCompositeOffset = StreamSeek(pStream, static_cast<int64_t>(layerMaskLen), STREAM_SEEK_CUR, &imgDataOffset);
            }
        } else {
            uint8_t lenBuf8[8];
            if (StreamRead(pStream, lenBuf8, 8)) {
                layerMaskLen = ReadUInt64BE(lenBuf8);
                hasCompositeOffset = StreamSeek(pStream, static_cast<int64_t>(layerMaskLen), STREAM_SEEK_CUR, &imgDataOffset);
            }
        }
    }

    // D-01: Size-aware selection logic
    // Check if embedded thumbnail exists
    uint32_t embW = 0, embH = 0;
    std::vector<uint8_t> rawEmbeddedJpeg;
    HRESULT hrThumb = TryEmbeddedThumbnail(
        pStream, resSectionOffset, resSectionLen, maxEdge, phBitmap, pAlphaType, &embW, &embH, &rawEmbeddedJpeg);

    if (SUCCEEDED(hrThumb) && *phBitmap != NULL) {
        UINT embeddedMax = std::max(embW, embH);
        // If embedded preview is big enough (~75% of requested cx), or composite is not supported, use it!
        bool isLargeEnough = (embeddedMax * 4 >= maxEdge * 3);
        if (isLargeEnough || !hasCompositeOffset || !IsCompositeSupported(header)) {
            pResult->hr = S_OK;
            pResult->source = DecodeSource::EmbeddedThumbnail;
            return S_OK;
        }

        // Embedded preview is smaller than requested, and composite path is supported:
        // Try high-resolution composite decode first!
        HBITMAP hCompBmp = NULL;
        WTS_ALPHATYPE compAlpha = WTSAT_UNKNOWN;
        HRESULT hrComp = DecodeComposite(pStream, header, imgDataOffset, streamSize, maxEdge, &hCompBmp, &compAlpha, pResult);
        if (SUCCEEDED(hrComp) && hCompBmp != NULL) {
            // Composite decode succeeded at higher resolution!
            DeleteObject(*phBitmap);
            *phBitmap = hCompBmp;
            *pAlphaType = compAlpha;
            pResult->hr = S_OK;
            pResult->source = DecodeSource::CompositeImageData;
            return S_OK;
        }

        // Composite decode failed: keep the embedded thumbnail as fallback
        pResult->hr = S_OK;
        pResult->source = DecodeSource::EmbeddedThumbnail;
        return S_OK;
    }

    // Fast path 2: XMP Metadata embedded thumbnail (Resource 1060)
    uint32_t xmpW = 0, xmpH = 0;
    HRESULT hrXmp = TryXmpThumbnail(
        pStream, resSectionOffset, resSectionLen, maxEdge, phBitmap, pAlphaType, &xmpW, &xmpH);
    if (SUCCEEDED(hrXmp) && *phBitmap != NULL) {
        UINT xmpMax = std::max(xmpW, xmpH);
        bool isLargeEnough = (xmpMax * 4 >= maxEdge * 3);
        if (isLargeEnough || !hasCompositeOffset || !IsCompositeSupported(header)) {
            pResult->hr = S_OK;
            pResult->source = DecodeSource::XmpThumbnail;
            return S_OK;
        }

        HBITMAP hCompBmp = NULL;
        WTS_ALPHATYPE compAlpha = WTSAT_UNKNOWN;
        HRESULT hrComp = DecodeComposite(pStream, header, imgDataOffset, streamSize, maxEdge, &hCompBmp, &compAlpha, pResult);
        if (SUCCEEDED(hrComp) && hCompBmp != NULL) {
            DeleteObject(*phBitmap);
            *phBitmap = hCompBmp;
            *pAlphaType = compAlpha;
            pResult->hr = S_OK;
            pResult->source = DecodeSource::CompositeImageData;
            return S_OK;
        }

        pResult->hr = S_OK;
        pResult->source = DecodeSource::XmpThumbnail;
        return S_OK;
    }

    // Secondary path: Image Data composite decode
    if (hasCompositeOffset && IsCompositeSupported(header)) {
        HRESULT hrComp = DecodeComposite(
            pStream, header, imgDataOffset, streamSize, maxEdge, phBitmap, pAlphaType, pResult);
        if (SUCCEEDED(hrComp) && *phBitmap != NULL) {
            pResult->hr = S_OK;
            pResult->source = DecodeSource::CompositeImageData;
            return S_OK;
        }
    }

    return E_FAIL;
}

HRESULT PsdDecoder::TryEmbeddedThumbnail(
    IStream* pStream,
    uint64_t resSectionOffset,
    uint64_t resSectionLength,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    uint32_t* pThumbW,
    uint32_t* pThumbH,
    std::vector<uint8_t>* pOutRawJpeg
) {
    if (!StreamSeek(pStream, static_cast<int64_t>(resSectionOffset), STREAM_SEEK_SET)) return E_FAIL;
    uint64_t resSectionEnd = resSectionOffset + resSectionLength;

    uint8_t headerBuf[16];
    while (true) {
        uint64_t curPos = 0;
        if (!StreamSeek(pStream, 0, STREAM_SEEK_CUR, &curPos)) break;
        if (curPos + 12 >= resSectionEnd) break;

        // Read signature '8BIM' (4 bytes) and ID (2 bytes)
        if (!StreamRead(pStream, headerBuf, 6)) break;
        if (headerBuf[0] != '8' || headerBuf[1] != 'B' || headerBuf[2] != 'I' || headerBuf[3] != 'M')
            break;

        uint16_t resId = ReadUInt16BE(headerBuf + 4);

        // Pascal string name: 1-byte length + name bytes, total rounded up to even
        uint8_t nameLen = 0;
        if (!StreamRead(pStream, &nameLen, 1)) break;
        int namePadding = nameLen + ((nameLen % 2 == 0) ? 1 : 0);
        if (!StreamSeek(pStream, namePadding, STREAM_SEEK_CUR)) break;

        // 4 bytes data size
        uint8_t sizeBuf[4];
        if (!StreamRead(pStream, sizeBuf, 4)) break;
        uint32_t dataSize = ReadUInt32BE(sizeBuf);
        uint64_t dataOffset = 0;
        if (!StreamSeek(pStream, 0, STREAM_SEEK_CUR, &dataOffset)) break;

        uint64_t nextBlock = dataOffset + dataSize;
        if (dataSize % 2 != 0) nextBlock++; // even padded

        // Check for Resource 1036 (0x040C) or 1033 (0x0409)
        if ((resId == 1036 || resId == 1033) && dataSize > 28) {
            uint8_t thumbHdr[28];
            if (StreamRead(pStream, thumbHdr, 28)) {
                uint32_t format = ReadUInt32BE(thumbHdr); // 1=JPEG, 0=Raw BGR
                uint32_t tw     = ReadUInt32BE(thumbHdr + 4);
                uint32_t th     = ReadUInt32BE(thumbHdr + 8);

                if (format == 1) {
                    uint32_t jpegLen = dataSize - 28;
                    if (jpegLen > 0 && jpegLen <= 100 * 1024 * 1024) {
                        std::vector<uint8_t> jpegData(jpegLen);
                        if (StreamRead(pStream, jpegData.data(), jpegLen)) {
                            if (pThumbW) *pThumbW = tw;
                            if (pThumbH) *pThumbH = th;
                            if (pOutRawJpeg) *pOutRawJpeg = jpegData;

                            uint32_t outW = 0, outH = 0;
                            HRESULT hr = CreateDibFromMemory(
                                jpegData.data(), jpegLen, maxEdge, phBitmap, pAlphaType, &outW, &outH);
                            if (SUCCEEDED(hr)) {
                                return S_OK;
                            }
                        }
                    }
                }
            }
        }

        if (!StreamSeek(pStream, static_cast<int64_t>(nextBlock), STREAM_SEEK_SET)) break;
    }

    return E_FAIL;
}

HRESULT PsdDecoder::TryXmpThumbnail(
    IStream* pStream,
    uint64_t resSectionOffset,
    uint64_t resSectionLength,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    uint32_t* pThumbW,
    uint32_t* pThumbH
) {
    if (!StreamSeek(pStream, static_cast<int64_t>(resSectionOffset), STREAM_SEEK_SET)) return E_FAIL;
    uint64_t resSectionEnd = resSectionOffset + resSectionLength;

    uint8_t headerBuf[6];
    while (true) {
        uint64_t curPos = 0;
        if (!StreamSeek(pStream, 0, STREAM_SEEK_CUR, &curPos)) break;
        if (curPos + 12 >= resSectionEnd) break;

        if (!StreamRead(pStream, headerBuf, 6)) break;
        if (headerBuf[0] != '8' || headerBuf[1] != 'B' || headerBuf[2] != 'I' || headerBuf[3] != 'M')
            break;

        uint16_t resId = ReadUInt16BE(headerBuf + 4);

        uint8_t nameLen = 0;
        if (!StreamRead(pStream, &nameLen, 1)) break;
        int namePadding = nameLen + ((nameLen % 2 == 0) ? 1 : 0);
        if (!StreamSeek(pStream, namePadding, STREAM_SEEK_CUR)) break;

        uint8_t sizeBuf[4];
        if (!StreamRead(pStream, sizeBuf, 4)) break;
        uint32_t dataSize = ReadUInt32BE(sizeBuf);
        uint64_t dataOffset = 0;
        if (!StreamSeek(pStream, 0, STREAM_SEEK_CUR, &dataOffset)) break;

        uint64_t nextBlock = dataOffset + dataSize;
        if (dataSize % 2 != 0) nextBlock++;

        // Resource 1060 (0x0424) is XMP Metadata
        if (resId == 1060 && dataSize > 100 && dataSize <= 10 * 1024 * 1024) {
            std::vector<char> xmpBuf(dataSize + 1, 0);
            if (StreamRead(pStream, xmpBuf.data(), dataSize)) {
                std::string xmp(xmpBuf.data(), dataSize);
                std::vector<uint8_t> jpegData;
                if (ExtractXmpThumbnailData(xmp, jpegData)) {
                    uint32_t outW = 0, outH = 0;
                    HRESULT hr = CreateDibFromMemory(
                        jpegData.data(), jpegData.size(), maxEdge, phBitmap, pAlphaType, &outW, &outH);
                    if (SUCCEEDED(hr)) {
                        if (pThumbW) *pThumbW = outW;
                        if (pThumbH) *pThumbH = outH;
                        return S_OK;
                    }
                }
            }
        }

        if (!StreamSeek(pStream, static_cast<int64_t>(nextBlock), STREAM_SEEK_SET)) break;
    }

    return E_FAIL;
}

// D-09 & S-03: AI / PDF format XMP thumbnail search with sniffing and head+tail scan
HRESULT PsdDecoder::TryAiThumbnail(
    IStream* pStream,
    uint64_t streamSize,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    DecodeResult* pResult
) {
    if (streamSize < 32) return E_FAIL;

    // S-03: Sniff first 16 bytes before allocating large buffers
    uint8_t magic[16];
    if (!StreamSeek(pStream, 0, STREAM_SEEK_SET)) return E_FAIL;
    if (!StreamRead(pStream, magic, 16)) return E_FAIL;

    bool looksLikeAi = (magic[0] == '%' && magic[1] == 'P' && magic[2] == 'D' && magic[3] == 'F') ||
                       (magic[0] == '%' && magic[1] == '!' && magic[2] == 'P' && magic[3] == 'S') ||
                       (magic[0] == '<' && magic[1] == '?' && magic[2] == 'x' && magic[3] == 'p');
    if (!looksLikeAi) return E_FAIL;

    const ULONG kScanChunk = 4 * 1024 * 1024;
    std::vector<uint8_t> jpegData;

    // 1. Scan head (first 4MB)
    ULONG headSize = static_cast<ULONG>(std::min<uint64_t>(streamSize, kScanChunk));
    std::string headContent(headSize, '\0');
    StreamSeek(pStream, 0, STREAM_SEEK_SET);
    if (StreamRead(pStream, headContent.data(), headSize)) {
        if (ExtractXmpThumbnailData(headContent, jpegData)) {
            uint32_t outW = 0, outH = 0;
            HRESULT hr = CreateDibFromMemory(jpegData.data(), jpegData.size(), maxEdge, phBitmap, pAlphaType, &outW, &outH);
            if (SUCCEEDED(hr)) {
                if (pResult) {
                    pResult->outputWidth = outW;
                    pResult->outputHeight = outH;
                }
                return S_OK;
            }
        }
    }

    // 2. Scan tail (last 4MB) if file is large and head didn't contain thumbnail
    if (streamSize > kScanChunk) {
        uint64_t tailOffset = (streamSize > kScanChunk) ? (streamSize - kScanChunk) : 0;
        ULONG tailSize = static_cast<ULONG>(streamSize - tailOffset);
        std::string tailContent(tailSize, '\0');
        if (StreamSeek(pStream, static_cast<int64_t>(tailOffset), STREAM_SEEK_SET) &&
            StreamRead(pStream, tailContent.data(), tailSize)) {
            if (ExtractXmpThumbnailData(tailContent, jpegData)) {
                uint32_t outW = 0, outH = 0;
                HRESULT hr = CreateDibFromMemory(jpegData.data(), jpegData.size(), maxEdge, phBitmap, pAlphaType, &outW, &outH);
                if (SUCCEEDED(hr)) {
                    if (pResult) {
                        pResult->outputWidth = outW;
                        pResult->outputHeight = outH;
                    }
                    return S_OK;
                }
            }
        }
    }

    return E_FAIL;
}

// Secondary path: Image Data composite decompression (D-02, D-03, D-04, D-05, D-06, S-01, S-02)
HRESULT PsdDecoder::DecodeComposite(
    IStream* pStream,
    const PsdHeader& header,
    uint64_t imgDataOffset,
    uint64_t streamSize,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    DecodeResult* pResult
) {
    if (!StreamSeek(pStream, static_cast<int64_t>(imgDataOffset), STREAM_SEEK_SET)) return E_FAIL;

    uint8_t compBuf[2];
    if (!StreamRead(pStream, compBuf, 2)) return E_FAIL;
    uint16_t compression = ReadUInt16BE(compBuf);

    // We support compression 0 (Raw) and 1 (PackBits RLE)
    if (compression != 0 && compression != 1) return E_NOTIMPL;

    // Only 8-bit depth supported for composite decode
    if (header.depth != 8) return E_NOTIMPL;

    // D-02: Reject unsupported color modes cleanly with E_NOTIMPL (Never return solid black)
    if (header.colorMode != 1 && header.colorMode != 3 && header.colorMode != 4) {
        return E_NOTIMPL;
    }

    uint32_t width = header.width;
    uint32_t height = header.height;
    uint16_t channels = header.channels;

    // Calculate step factor for downsampling directly during decode to conserve memory
    uint32_t maxDim = std::max(width, height);
    uint32_t step = 1;
    while (maxDim / (step * 2) >= maxEdge && (step * 2) <= 16) {
        step *= 2;
    }

    uint32_t outW = (width + step - 1) / step;
    uint32_t outH = (height + step - 1) / step;

    // S-02: Guard against unreasonable output dimensions (Cap at 4096 x 4096 = 16 MP)
    if (outW == 0 || outH == 0 || outW > 4096 || outH > 4096) return E_NOTIMPL;

    // Allocate 32-bpp BGRA target buffer
    std::vector<uint32_t> pixels(outW * outH, 0xFF000000);

    if (compression == 1) {
        // PackBits RLE
        size_t numRows = static_cast<size_t>(height) * channels;
        size_t countEntrySize = (header.version == 2) ? 4 : 2;

        // S-01: Guard row table size BEFORE allocating
        if (numRows > (64u << 20) / countEntrySize) return E_NOTIMPL;
        size_t totalCountBytes = numRows * countEntrySize;
        if (imgDataOffset + 2 + totalCountBytes > streamSize) return E_FAIL;

        std::vector<uint8_t> countsBuf(totalCountBytes);
        if (!StreamRead(pStream, countsBuf.data(), static_cast<ULONG>(countsBuf.size())))
            return E_FAIL;

        std::vector<uint32_t> rowByteCounts(numRows);
        std::vector<uint64_t> rowOffsets(numRows);
        uint64_t currentOffset = 0;

        for (size_t i = 0; i < numRows; ++i) {
            uint32_t rLen = (header.version == 2)
                ? ReadUInt32BE(countsBuf.data() + i * 4)
                : ReadUInt16BE(countsBuf.data() + i * 2);
            rowByteCounts[i] = rLen;
            rowOffsets[i] = currentOffset;
            currentOffset += rLen;
        }

        uint64_t rleDataStart = imgDataOffset + 2 + totalCountBytes;
        uint32_t kMaxRowLen = width * 2 + 16; // S-01 safe bound for PackBits

        std::vector<uint8_t> compressedRow;
        std::vector<uint8_t> decompressedRow(width);

        // D-03: Seek only to sampled rows!
        for (uint16_t ch = 0; ch < channels; ++ch) {
            for (uint32_t y = 0; y < height; ++y) {
                // If this row is not sampled by step, skip reading it completely!
                if (y % step != 0) continue;

                uint32_t outY = y / step;
                if (outY >= outH) continue;

                size_t rowIndex = static_cast<size_t>(ch) * height + y;
                uint32_t rowLen = rowByteCounts[rowIndex];

                // S-01: Cap row length
                if (rowLen > kMaxRowLen) return E_FAIL;
                if (rowLen > compressedRow.size()) compressedRow.resize(rowLen);

                // Direct seek to the exact row location in stream
                uint64_t rowFileOffset = rleDataStart + rowOffsets[rowIndex];
                if (!StreamSeek(pStream, static_cast<int64_t>(rowFileOffset), STREAM_SEEK_SET))
                    return E_FAIL;
                if (!StreamRead(pStream, compressedRow.data(), rowLen))
                    return E_FAIL;

                // PackBits decompress
                size_t inIdx = 0;
                size_t outIdx = 0;
                while (inIdx < rowLen && outIdx < width) {
                    int8_t n = static_cast<int8_t>(compressedRow[inIdx++]);
                    if (n >= 0) {
                        size_t count = static_cast<size_t>(n) + 1;
                        for (size_t k = 0; k < count && inIdx < rowLen && outIdx < width; ++k)
                            decompressedRow[outIdx++] = compressedRow[inIdx++];
                    } else if (n > -128) {
                        size_t count = static_cast<size_t>(-n) + 1;
                        if (inIdx < rowLen) {
                            uint8_t val = compressedRow[inIdx++];
                            for (size_t k = 0; k < count && outIdx < width; ++k)
                                decompressedRow[outIdx++] = val;
                        }
                    }
                }

                // Merge into target pixels
                for (uint32_t x = 0; x < width; x += step) {
                    uint32_t outX = x / step;
                    if (outX >= outW) break;

                    uint8_t val = (x < outIdx) ? decompressedRow[x] : 0;
                    size_t pixIdx = outY * outW + outX;

                    if (header.colorMode == 3) { // RGB
                        if (ch == 0) pixels[pixIdx] = (pixels[pixIdx] & 0xFF00FFFF) | (val << 16); // R
                        else if (ch == 1) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFF00FF) | (val << 8);  // G
                        else if (ch == 2) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFFFF00) | val;         // B
                        else if (ch == 3) pixels[pixIdx] = (pixels[pixIdx] & 0x00FFFFFF) | (val << 24); // Alpha
                    } else if (header.colorMode == 1) { // Grayscale
                        if (ch == 0) pixels[pixIdx] = 0xFF000000 | (val << 16) | (val << 8) | val;
                        else if (ch == 1) pixels[pixIdx] = (pixels[pixIdx] & 0x00FFFFFF) | (val << 24); // Alpha
                    } else if (header.colorMode == 4 && ch < 4) { // CMYK (D-04 inverted ink)
                        uint32_t c = (ch == 0) ? (255 - val) : (pixels[pixIdx] >> 16) & 0xFF;
                        uint32_t m = (ch == 1) ? (255 - val) : (pixels[pixIdx] >> 8) & 0xFF;
                        uint32_t yCol = (ch == 2) ? (255 - val) : pixels[pixIdx] & 0xFF;
                        uint32_t k = (ch == 3) ? (255 - val) : 0;
                        if (ch == 3) {
                            uint32_t r = ((255 - c) * (255 - k)) / 255;
                            uint32_t g = ((255 - m) * (255 - k)) / 255;
                            uint32_t b = ((255 - yCol) * (255 - k)) / 255;
                            pixels[pixIdx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                        } else {
                            pixels[pixIdx] = (pixels[pixIdx] & 0xFF000000) | (c << 16) | (m << 8) | yCol;
                        }
                    }
                }
            }
        }
    } else {
        // Raw uncompressed decode with D-03 row-skipping and D-04 CMYK
        uint64_t rawDataStart = imgDataOffset + 2;
        std::vector<uint8_t> rowBuf(width);

        for (uint16_t ch = 0; ch < channels; ++ch) {
            for (uint32_t y = 0; y < height; ++y) {
                if (y % step != 0) continue; // Skip unsampled row

                uint32_t outY = y / step;
                if (outY >= outH) continue;

                uint64_t rowFileOffset = rawDataStart + (static_cast<uint64_t>(ch) * height + y) * width;
                if (!StreamSeek(pStream, static_cast<int64_t>(rowFileOffset), STREAM_SEEK_SET)) return E_FAIL;
                if (!StreamRead(pStream, rowBuf.data(), width)) return E_FAIL;

                for (uint32_t x = 0; x < width; x += step) {
                    uint32_t outX = x / step;
                    if (outX >= outW) break;
                    uint8_t val = rowBuf[x];
                    size_t pixIdx = outY * outW + outX;

                    if (header.colorMode == 3) { // RGB
                        if (ch == 0) pixels[pixIdx] = (pixels[pixIdx] & 0xFF00FFFF) | (val << 16);
                        else if (ch == 1) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFF00FF) | (val << 8);
                        else if (ch == 2) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFFFF00) | val;
                        else if (ch == 3) pixels[pixIdx] = (pixels[pixIdx] & 0x00FFFFFF) | (val << 24);
                    } else if (header.colorMode == 1) { // Grayscale
                        if (ch == 0) pixels[pixIdx] = 0xFF000000 | (val << 16) | (val << 8) | val;
                        else if (ch == 1) pixels[pixIdx] = (pixels[pixIdx] & 0x00FFFFFF) | (val << 24);
                    } else if (header.colorMode == 4 && ch < 4) { // CMYK
                        uint32_t c = (ch == 0) ? (255 - val) : (pixels[pixIdx] >> 16) & 0xFF;
                        uint32_t m = (ch == 1) ? (255 - val) : (pixels[pixIdx] >> 8) & 0xFF;
                        uint32_t yCol = (ch == 2) ? (255 - val) : pixels[pixIdx] & 0xFF;
                        uint32_t k = (ch == 3) ? (255 - val) : 0;
                        if (ch == 3) {
                            uint32_t r = ((255 - c) * (255 - k)) / 255;
                            uint32_t g = ((255 - m) * (255 - k)) / 255;
                            uint32_t b = ((255 - yCol) * (255 - k)) / 255;
                            pixels[pixIdx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                        } else {
                            pixels[pixIdx] = (pixels[pixIdx] & 0xFF000000) | (c << 16) | (m << 8) | yCol;
                        }
                    }
                }
            }
        }
    }

    // D-05: Premultiply alpha for WTSAT_ARGB
    bool hasAlpha = (channels >= 4 && header.colorMode == 3);
    if (hasAlpha) {
        for (size_t i = 0; i < pixels.size(); ++i) {
            uint32_t a = (pixels[i] >> 24) & 0xFF;
            uint32_t r = (pixels[i] >> 16) & 0xFF;
            uint32_t g = (pixels[i] >> 8) & 0xFF;
            uint32_t b = pixels[i] & 0xFF;
            r = (r * a + 127) / 255;
            g = (g * a + 127) / 255;
            b = (b * a + 127) / 255;
            pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    // D-06: High-quality scaling to exact maxEdge
    uint32_t finalW = outW, finalH = outH;
    HRESULT hrScale = ScaleBitmapToDib(
        pixels.data(), outW, outH, maxEdge, hasAlpha, phBitmap, pAlphaType, &finalW, &finalH);
    if (FAILED(hrScale)) {
        return hrScale;
    }

    if (pResult) {
        pResult->outputWidth = finalW;
        pResult->outputHeight = finalH;
        pResult->hasAlpha = hasAlpha;
    }
    return S_OK;
}

// D-06: High-quality WIC scaler for composite bitmap down to maxEdge
HRESULT PsdDecoder::ScaleBitmapToDib(
    const uint32_t* pPixels,
    uint32_t srcW,
    uint32_t srcH,
    UINT maxEdge,
    bool hasAlpha,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    uint32_t* pOutW,
    uint32_t* pOutH
) {
    if (!pPixels || srcW == 0 || srcH == 0) return E_INVALIDARG;

    UINT targetW = srcW, targetH = srcH;
    if (maxEdge > 0 && (srcW > maxEdge || srcH > maxEdge)) {
        if (srcW >= srcH) {
            targetW = maxEdge;
            targetH = std::max(1u, (srcH * maxEdge) / srcW);
        } else {
            targetH = maxEdge;
            targetW = std::max(1u, (srcW * maxEdge) / srcH);
        }
    }

    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, reinterpret_cast<void**>(&pFactory));
    if (FAILED(hr)) return hr;

    IWICBitmap* pWicBitmap = nullptr;
    UINT stride = srcW * 4;
    UINT cbBufferSize = stride * srcH;
    hr = pFactory->CreateBitmapFromMemory(
        srcW, srcH, GUID_WICPixelFormat32bppBGRA, stride, cbBufferSize,
        reinterpret_cast<BYTE*>(const_cast<uint32_t*>(pPixels)), &pWicBitmap);

    if (FAILED(hr)) {
        pFactory->Release();
        return hr;
    }

    IWICBitmapSource* pSource = pWicBitmap;
    IWICBitmapScaler* pScaler = nullptr;

    if (targetW != srcW || targetH != srcH) {
        hr = pFactory->CreateBitmapScaler(&pScaler);
        if (SUCCEEDED(hr)) {
            hr = pScaler->Initialize(pWicBitmap, targetW, targetH, WICBitmapInterpolationModeFant);
            if (SUCCEEDED(hr)) {
                pSource = pScaler;
            }
        }
    }

    // Create 32-bpp DIB Section
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = targetW;
    bi.bmiHeader.biHeight = -static_cast<LONG>(targetH); // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pDibBits = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP hbmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pDibBits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (!hbmp || !pDibBits) {
        if (pScaler) pScaler->Release();
        pWicBitmap->Release();
        pFactory->Release();
        return E_FAIL;
    }

    UINT targetStride = targetW * 4;
    UINT targetCbSize = targetStride * targetH;
    hr = pSource->CopyPixels(NULL, targetStride, targetCbSize, static_cast<BYTE*>(pDibBits));

    if (pScaler) pScaler->Release();
    pWicBitmap->Release();
    pFactory->Release();

    if (FAILED(hr)) {
        DeleteObject(hbmp);
        return hr;
    }

    *phBitmap = hbmp;
    *pAlphaType = hasAlpha ? WTSAT_ARGB : WTSAT_RGB;
    if (pOutW) *pOutW = targetW;
    if (pOutH) *pOutH = targetH;
    return S_OK;
}

HRESULT PsdDecoder::CreateDibFromMemory(
    const uint8_t* pData,
    size_t dataSize,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    uint32_t* pOutW,
    uint32_t* pOutH
) {
    if (!pData || dataSize == 0) return E_INVALIDARG;

    IWICImagingFactory* pFactory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, reinterpret_cast<void**>(&pFactory));
    if (FAILED(hr)) return hr;

    IWICStream* pWicStream = nullptr;
    hr = pFactory->CreateStream(&pWicStream);
    if (FAILED(hr)) { pFactory->Release(); return hr; }

    hr = pWicStream->InitializeFromMemory(const_cast<uint8_t*>(pData), static_cast<DWORD>(dataSize));
    if (FAILED(hr)) { pWicStream->Release(); pFactory->Release(); return hr; }

    IWICBitmapDecoder* pDecoder = nullptr;
    hr = pFactory->CreateDecoderFromStream(
        pWicStream, NULL, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr)) { pWicStream->Release(); pFactory->Release(); return hr; }

    IWICBitmapFrameDecode* pFrame = nullptr;
    hr = pDecoder->GetFrame(0, &pFrame);
    if (FAILED(hr)) { pDecoder->Release(); pWicStream->Release(); pFactory->Release(); return hr; }

    UINT origW = 0, origH = 0;
    pFrame->GetSize(&origW, &origH);
    if (origW == 0 || origH == 0) {
        pFrame->Release(); pDecoder->Release(); pWicStream->Release(); pFactory->Release();
        return E_FAIL;
    }

    // Calculate aspect-ratio preserved scaling
    UINT targetW = origW, targetH = origH;
    if (maxEdge > 0 && (origW > maxEdge || origH > maxEdge)) {
        if (origW >= origH) {
            targetW = maxEdge;
            targetH = std::max(1u, (origH * maxEdge) / origW);
        } else {
            targetH = maxEdge;
            targetW = std::max(1u, (origW * maxEdge) / origH);
        }
    }

    IWICBitmapSource* pSource = pFrame;
    IWICBitmapScaler* pScaler = nullptr;

    if (targetW != origW || targetH != origH) {
        hr = pFactory->CreateBitmapScaler(&pScaler);
        if (SUCCEEDED(hr)) {
            hr = pScaler->Initialize(pFrame, targetW, targetH, WICBitmapInterpolationModeFant);
            if (SUCCEEDED(hr)) {
                pSource = pScaler;
            }
        }
    }

    // Convert to 32bpp BGRA
    IWICFormatConverter* pConverter = nullptr;
    hr = pFactory->CreateFormatConverter(&pConverter);
    if (FAILED(hr)) {
        if (pScaler) pScaler->Release();
        pFrame->Release(); pDecoder->Release(); pWicStream->Release(); pFactory->Release();
        return hr;
    }

    hr = pConverter->Initialize(
        pSource, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
        NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        pConverter->Release();
        if (pScaler) pScaler->Release();
        pFrame->Release(); pDecoder->Release(); pWicStream->Release(); pFactory->Release();
        return hr;
    }

    // Create DIB Section
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = targetW;
    bi.bmiHeader.biHeight = -static_cast<LONG>(targetH); // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pDibBits = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP hbmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pDibBits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (!hbmp || !pDibBits) {
        pConverter->Release();
        if (pScaler) pScaler->Release();
        pFrame->Release(); pDecoder->Release(); pWicStream->Release(); pFactory->Release();
        return E_FAIL;
    }

    UINT stride = targetW * 4;
    UINT cbBufferSize = stride * targetH;
    hr = pConverter->CopyPixels(NULL, stride, cbBufferSize, static_cast<BYTE*>(pDibBits));

    pConverter->Release();
    if (pScaler) pScaler->Release();
    pFrame->Release();
    pDecoder->Release();
    pWicStream->Release();
    pFactory->Release();

    if (FAILED(hr)) {
        DeleteObject(hbmp);
        return hr;
    }

    *phBitmap = hbmp;
    *pAlphaType = WTSAT_RGB;
    if (pOutW) *pOutW = targetW;
    if (pOutH) *pOutH = targetH;
    return S_OK;
}

} // namespace PsdThumbnail

#include "psd_decoder.h"
#include <wincodec.h>
#include <shlwapi.h>
#include <string>
#include <cstring>
#include <algorithm>
#include <cmath>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "gdi32.lib")

namespace PsdThumbnail {

uint16_t PsdDecoder::ReadUInt16BE(const uint8_t* p) {
    // Cast to uint16_t before shifting to avoid signed int promotion (B-01)
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

bool PsdDecoder::ReadHeader(IStream* pStream, PsdHeader& header) {
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

    PsdHeader header;
    if (!ReadHeader(pStream, header)) {
        // Check for AI / PDF format with embedded XMP thumbnail
        HRESULT hrAi = TryAiThumbnail(pStream, maxEdge, phBitmap, pAlphaType, pResult);
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

    // Fast path 1: Embedded JPEG thumbnail (Resource 1036/1033)
    HRESULT hrThumb = TryEmbeddedThumbnail(
        pStream, resSectionOffset, resSectionLen, maxEdge, phBitmap, pAlphaType, pResult);
    if (SUCCEEDED(hrThumb) && *phBitmap != NULL) {
        pResult->hr = S_OK;
        pResult->source = DecodeSource::EmbeddedThumbnail;
        return S_OK;
    }

    // Fast path 2: XMP Metadata embedded thumbnail (Resource 1060)
    HRESULT hrXmp = TryXmpThumbnail(
        pStream, resSectionOffset, resSectionLen, maxEdge, phBitmap, pAlphaType, pResult);
    if (SUCCEEDED(hrXmp) && *phBitmap != NULL) {
        pResult->hr = S_OK;
        pResult->source = DecodeSource::XmpThumbnail;
        return S_OK;
    }

    // Secondary path: Image Data composite decode
    uint64_t afterResOffset = resSectionOffset + resSectionLen;
    if (!StreamSeek(pStream, afterResOffset, STREAM_SEEK_SET)) return E_FAIL;

    // Skip Layer and Mask Information section: 4 bytes for PSD, 8 bytes for PSB
    uint64_t layerMaskLen = 0;
    if (header.version == 1) {
        if (!StreamRead(pStream, lenBuf, 4)) return E_FAIL;
        layerMaskLen = ReadUInt32BE(lenBuf);
    } else {
        uint8_t lenBuf8[8];
        if (!StreamRead(pStream, lenBuf8, 8)) return E_FAIL;
        layerMaskLen = ReadUInt64BE(lenBuf8);
    }

    uint64_t imgDataOffset = 0;
    if (!StreamSeek(pStream, layerMaskLen, STREAM_SEEK_CUR, &imgDataOffset)) return E_FAIL;

    HRESULT hrComp = DecodeComposite(
        pStream, header, imgDataOffset, maxEdge, phBitmap, pAlphaType, pResult);
    if (SUCCEEDED(hrComp) && *phBitmap != NULL) {
        pResult->hr = S_OK;
        pResult->source = DecodeSource::CompositeImageData;
        return S_OK;
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
    DecodeResult* pResult
) {
    if (!StreamSeek(pStream, resSectionOffset, STREAM_SEEK_SET)) return E_FAIL;
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

        // Pascal string name: 1-byte length + name bytes, total rounded up to even.
        // Total bytes consumed = 1 (len byte) + nameLen (chars).
        // Skip (nameLen + padding) so that (1 + nameLen + padding) is even.
        // Fix B-02: pad is applied to the full (1+nameLen) field.
        uint8_t nameLen = 0;
        if (!StreamRead(pStream, &nameLen, 1)) break;
        // skip nameLen chars + 0 or 1 pad byte so total field is even
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
                uint32_t format   = ReadUInt32BE(thumbHdr);      // 1=JPEG, 0=Raw BGR
                uint32_t tw       = ReadUInt32BE(thumbHdr + 4);
                uint32_t th       = ReadUInt32BE(thumbHdr + 8);
                // (unused) widthBytes, size, bitsPerPixel, planes follow

                // B-03: Only process JPEG format (format == 1). Raw BGR (0) is
                // handled by the composite path and WIC can't decode it directly.
                if (format != 1) {
                    if (!StreamSeek(pStream, nextBlock, STREAM_SEEK_SET)) break;
                    continue;
                }

                uint32_t jpegLen = dataSize - 28;
                if (jpegLen > 0 && jpegLen <= 100 * 1024 * 1024) {
                    std::vector<uint8_t> jpegData(jpegLen);
                    if (StreamRead(pStream, jpegData.data(), jpegLen)) {
                        HRESULT hr = CreateDibFromMemory(
                            jpegData.data(), jpegLen, maxEdge, phBitmap, pAlphaType);
                        if (SUCCEEDED(hr)) {
                            pResult->outputWidth = tw;
                            pResult->outputHeight = th;
                            return S_OK;
                        }
                    }
                }
            }
        }

        if (!StreamSeek(pStream, nextBlock, STREAM_SEEK_SET)) break;
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
    DecodeResult* pResult
) {
    if (!StreamSeek(pStream, resSectionOffset, STREAM_SEEK_SET)) return E_FAIL;
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
        // B-02 (same fix): skip nameLen + even-alignment pad byte
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
                // Look for <xmpGImg:image>...</xmpGImg:image>
                const std::string tagOpen = "<xmpGImg:image>";
                const std::string tagClose = "</xmpGImg:image>";
                size_t posOpen = xmp.find(tagOpen);
                if (posOpen != std::string::npos) {
                    posOpen += tagOpen.length();
                    size_t posClose = xmp.find(tagClose, posOpen);
                    if (posClose != std::string::npos && posClose > posOpen) {
                        std::string b64 = xmp.substr(posOpen, posClose - posOpen);
                        // Clean whitespace/newlines from base64 string
                        b64.erase(std::remove_if(b64.begin(), b64.end(), [](char c) {
                            return c == '\r' || c == '\n' || c == ' ' || c == '\t';
                        }), b64.end());

                        // Base64 decode
                        DWORD dwBinaryLen = 0;
                        if (CryptStringToBinaryA(
                                b64.c_str(), static_cast<DWORD>(b64.length()),
                                CRYPT_STRING_BASE64, NULL, &dwBinaryLen, NULL, NULL) && dwBinaryLen > 0) {
                            std::vector<uint8_t> jpegData(dwBinaryLen);
                            if (CryptStringToBinaryA(
                                    b64.c_str(), static_cast<DWORD>(b64.length()),
                                    CRYPT_STRING_BASE64, jpegData.data(), &dwBinaryLen, NULL, NULL)) {
                                HRESULT hr = CreateDibFromMemory(
                                    jpegData.data(), dwBinaryLen, maxEdge, phBitmap, pAlphaType);
                                if (SUCCEEDED(hr)) return S_OK;
                            }
                        }
                    }
                }
            }
        }

        if (!StreamSeek(pStream, nextBlock, STREAM_SEEK_SET)) break;
    }

    return E_FAIL;
}

HRESULT PsdDecoder::TryAiThumbnail(
    IStream* pStream,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    DecodeResult* pResult
) {
    if (!StreamSeek(pStream, 0, STREAM_SEEK_SET)) return E_FAIL;

    // Read up to 4MB from the start of the stream to scan for XMP thumbnail
    const ULONG kScanSize = 4 * 1024 * 1024;
    std::vector<char> buffer(kScanSize, 0);
    ULONG bytesRead = 0;
    HRESULT hr = pStream->Read(buffer.data(), kScanSize, &bytesRead);
    if (FAILED(hr) || bytesRead < 64) return E_FAIL;

    std::string content(buffer.data(), bytesRead);

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
                    return E_FAIL;
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

    if (posClose == std::string::npos || posClose <= posOpen) return E_FAIL;

    std::string b64 = content.substr(posOpen, posClose - posOpen);

    // Unescape XML entities commonly found in XMP base64 strings: &#xA; &#xD; &#9;
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

    // Remove whitespace and newlines
    b64.erase(std::remove_if(b64.begin(), b64.end(), [](char c) {
        return c == '\r' || c == '\n' || c == ' ' || c == '\t';
    }), b64.end());

    if (b64.empty()) return E_FAIL;

    DWORD dwBinaryLen = 0;
    if (CryptStringToBinaryA(
            b64.c_str(), static_cast<DWORD>(b64.length()),
            CRYPT_STRING_BASE64, NULL, &dwBinaryLen, NULL, NULL) && dwBinaryLen > 0) {
        std::vector<uint8_t> jpegData(dwBinaryLen);
        if (CryptStringToBinaryA(
                b64.c_str(), static_cast<DWORD>(b64.length()),
                CRYPT_STRING_BASE64, jpegData.data(), &dwBinaryLen, NULL, NULL)) {
            HRESULT hrDib = CreateDibFromMemory(
                jpegData.data(), dwBinaryLen, maxEdge, phBitmap, pAlphaType);
            if (SUCCEEDED(hrDib) && *phBitmap != NULL) {
                pResult->hr = S_OK;
                pResult->source = DecodeSource::XmpThumbnail;
                return S_OK;
            }
        }
    }

    return E_FAIL;
}

HRESULT PsdDecoder::DecodeComposite(
    IStream* pStream,
    const PsdHeader& header,
    uint64_t imgDataOffset,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType,
    DecodeResult* pResult
) {
    if (!StreamSeek(pStream, imgDataOffset, STREAM_SEEK_SET)) return E_FAIL;

    uint8_t compBuf[2];
    if (!StreamRead(pStream, compBuf, 2)) return E_FAIL;
    uint16_t compression = ReadUInt16BE(compBuf);

    // We support compression 0 (Raw) and 1 (PackBits RLE)
    if (compression != 0 && compression != 1) return E_NOTIMPL;

    // Only 8-bit depth supported for composite fast decode
    if (header.depth != 8) return E_NOTIMPL;

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

    // B-04: Guard against unreasonable output dimensions (e.g. huge PSB with step=1)
    // Cap at 16 MP output (e.g. 4096x4096) – more than enough for a thumbnail.
    if (outW == 0 || outH == 0 || outW > 8192 || outH > 8192) return E_NOTIMPL;

    // Allocate 32-bpp BGRA target buffer
    std::vector<uint32_t> pixels(outW * outH, 0xFF000000);

    if (compression == 1) {
        // PackBits RLE
        // Read byte counts table: (height * channels) entries
        // 2 bytes per row for PSD, 4 bytes per row for PSB
        size_t numRows = static_cast<size_t>(height) * channels;
        size_t countEntrySize = (header.version == 2) ? 4 : 2;
        std::vector<uint32_t> rowByteCounts(numRows);

        // B-05: Guard against ULONG truncation for huge PSB row-count tables
        size_t totalCountBytes = numRows * countEntrySize;
        if (totalCountBytes > 64 * 1024 * 1024) return E_NOTIMPL; // >64 MB table is pathological
        std::vector<uint8_t> countsBuf(totalCountBytes);
        if (!StreamRead(pStream, countsBuf.data(), static_cast<ULONG>(countsBuf.size())))
            return E_FAIL;

        for (size_t i = 0; i < numRows; ++i) {
            if (header.version == 2)
                rowByteCounts[i] = ReadUInt32BE(countsBuf.data() + i * 4);
            else
                rowByteCounts[i] = ReadUInt16BE(countsBuf.data() + i * 2);
        }

        // Decompress rows into channel plane buffers
        std::vector<uint8_t> compressedRow;
        std::vector<uint8_t> decompressedRow(width);

        for (uint16_t ch = 0; ch < channels; ++ch) {
            for (uint32_t y = 0; y < height; ++y) {
                uint32_t rowLen = rowByteCounts[ch * height + y];
                if (rowLen > compressedRow.size()) compressedRow.resize(rowLen);

                if (!StreamRead(pStream, compressedRow.data(), rowLen))
                    return E_FAIL;

                // Decompress row only if it's sampled by step
                if (y % step == 0) {
                    uint32_t outY = y / step;
                    if (outY >= outH) continue;

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

                        uint8_t val = decompressedRow[x];
                        size_t pixIdx = outY * outW + outX;

                        if (header.colorMode == 3) { // RGB
                            if (ch == 0) pixels[pixIdx] = (pixels[pixIdx] & 0xFF00FFFF) | (val << 16); // R
                            else if (ch == 1) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFF00FF) | (val << 8);  // G
                            else if (ch == 2) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFFFF00) | val;         // B
                            else if (ch == 3) pixels[pixIdx] = (pixels[pixIdx] & 0x00FFFFFF) | (val << 24); // A
                        } else if (header.colorMode == 1) { // Grayscale
                            if (ch == 0) pixels[pixIdx] = 0xFF000000 | (val << 16) | (val << 8) | val;
                            else if (ch == 1) pixels[pixIdx] = (pixels[pixIdx] & 0x00FFFFFF) | (val << 24); // Alpha
                        } else if (header.colorMode == 4 && ch < 4) { // CMYK
                            // Simple CMYK to RGB conversion
                            uint32_t c = (ch == 0) ? val : (pixels[pixIdx] >> 16) & 0xFF;
                            uint32_t m = (ch == 1) ? val : (pixels[pixIdx] >> 8) & 0xFF;
                            uint32_t yCol = (ch == 2) ? val : pixels[pixIdx] & 0xFF;
                            uint32_t k = (ch == 3) ? val : 0;
                            if (ch == 3) {
                                uint32_t r = 255 - std::min(255u, ((c * (255 - k)) / 255) + k);
                                uint32_t g = 255 - std::min(255u, ((m * (255 - k)) / 255) + k);
                                uint32_t b = 255 - std::min(255u, ((yCol * (255 - k)) / 255) + k);
                                pixels[pixIdx] = 0xFF000000 | (r << 16) | (g << 8) | b;
                            } else {
                                pixels[pixIdx] = (pixels[pixIdx] & 0xFF000000) | (c << 16) | (m << 8) | yCol;
                            }
                        }
                    }
                }
            }
        }
    } else {
        // Raw uncompressed decode
        std::vector<uint8_t> rowBuf(width);
        for (uint16_t ch = 0; ch < channels; ++ch) {
            for (uint32_t y = 0; y < height; ++y) {
                if (!StreamRead(pStream, rowBuf.data(), width)) return E_FAIL;
                if (y % step == 0) {
                    uint32_t outY = y / step;
                    if (outY >= outH) continue;
                    for (uint32_t x = 0; x < width; x += step) {
                        uint32_t outX = x / step;
                        if (outX >= outW) break;
                        uint8_t val = rowBuf[x];
                        size_t pixIdx = outY * outW + outX;

                        if (header.colorMode == 3) {
                            if (ch == 0) pixels[pixIdx] = (pixels[pixIdx] & 0xFF00FFFF) | (val << 16);
                            else if (ch == 1) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFF00FF) | (val << 8);
                            else if (ch == 2) pixels[pixIdx] = (pixels[pixIdx] & 0xFFFFFF00) | val;
                            else if (ch == 3) pixels[pixIdx] = (pixels[pixIdx] & 0x00FFFFFF) | (val << 24);
                        } else if (header.colorMode == 1) {
                            if (ch == 0) pixels[pixIdx] = 0xFF000000 | (val << 16) | (val << 8) | val;
                        }
                    }
                }
            }
        }
    }

    // Create 32-bpp DIB Section
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = outW;
    bi.bmiHeader.biHeight = -static_cast<LONG>(outH); // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pDibBits = nullptr;
    HDC hdc = GetDC(NULL);
    HBITMAP hbmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &pDibBits, NULL, 0);
    ReleaseDC(NULL, hdc);

    if (!hbmp || !pDibBits) return E_FAIL;

    memcpy(pDibBits, pixels.data(), outW * outH * sizeof(uint32_t));

    *phBitmap = hbmp;
    bool hasAlpha = (channels >= 4 && header.colorMode == 3);
    *pAlphaType = hasAlpha ? WTSAT_ARGB : WTSAT_RGB;
    if (pResult) {
        pResult->outputWidth = outW;
        pResult->outputHeight = outH;
        pResult->hasAlpha = hasAlpha;
    }

    return S_OK;
}

HRESULT PsdDecoder::CreateDibFromMemory(
    const uint8_t* pData,
    size_t dataSize,
    UINT maxEdge,
    HBITMAP* phBitmap,
    WTS_ALPHATYPE* pAlphaType
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
    return S_OK;
}

} // namespace PsdThumbnail

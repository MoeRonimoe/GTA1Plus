#pragma once

#include <cstdint>

// Farben werden intern immer als 0x00RRGGBB verarbeitet
struct PixelFormat
{
    int bpp = 0;
    uint32_t rMask = 0, gMask = 0, bMask = 0;
    int rShift = 0, gShift = 0, bShift = 0;
    int rBits = 0, gBits = 0, bBits = 0;

    bool Supported() const { return (bpp == 16 || bpp == 24 || bpp == 32) && rMask && gMask && bMask; }
    int BytesPerPixel() const { return bpp / 8; }
    bool operator==(const PixelFormat& o) const { return bpp == o.bpp && rMask == o.rMask && gMask == o.gMask && bMask == o.bMask; }
    bool operator!=(const PixelFormat& o) const { return !(*this == o); }
};

struct SurfaceView
{
    uint8_t* bits = nullptr;
    int pitch = 0;
    int width = 0;
    int height = 0;
    PixelFormat fmt;
    bool buffered = false;   // eigener Puffer im Arbeitsspeicher statt Bildspeicher der Surface (Lesen ist schnell)

    uint8_t* Row(int y) const { return bits + y * pitch; }
};

PixelFormat MakePixelFormat(int bpp, uint32_t rMask, uint32_t gMask, uint32_t bMask);
void ConvertFromSurface(const PixelFormat& fmt, const uint8_t* src, uint32_t* dst, int count);
void ConvertToSurface(const PixelFormat& fmt, const uint32_t* src, uint8_t* dst, int count);

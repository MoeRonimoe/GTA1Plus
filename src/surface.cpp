#include "surface.h"

#include <cstring>
#include <vector>

static void MaskInfo(uint32_t mask, int& shift, int& bits)
{
    shift = 0;
    bits = 0;
    if (!mask)
        return;
    while (!(mask & 1)) { mask >>= 1; ++shift; }
    while (mask & 1) { mask >>= 1; ++bits; }
}

// n-Bit-Kanal auf 8 Bit strecken (31 -> 255 statt 248)
static inline uint32_t Expand(uint32_t v, int bits)
{
    if (bits >= 8)
        return v >> (bits - 8);
    const uint32_t r = v << (8 - bits);
    return r | (r >> bits);
}

PixelFormat MakePixelFormat(int bpp, uint32_t rMask, uint32_t gMask, uint32_t bMask)
{
    PixelFormat f;
    f.bpp = bpp == 15 ? 16 : bpp;
    f.rMask = rMask;
    f.gMask = gMask;
    f.bMask = bMask;
    MaskInfo(rMask, f.rShift, f.rBits);
    MaskInfo(gMask, f.gShift, f.gBits);
    MaskInfo(bMask, f.bShift, f.bBits);
    return f;
}

static inline uint32_t Unpack(const PixelFormat& f, uint32_t v)
{
    const uint32_t r = Expand((v & f.rMask) >> f.rShift, f.rBits);
    const uint32_t g = Expand((v & f.gMask) >> f.gShift, f.gBits);
    const uint32_t b = Expand((v & f.bMask) >> f.bShift, f.bBits);
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t Pack(const PixelFormat& f, uint32_t c)
{
    const uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    return ((r >> (8 - f.rBits)) << f.rShift) | ((g >> (8 - f.gBits)) << f.gShift) | ((b >> (8 - f.bBits)) << f.bShift);
}

// Nachschlagetabelle 16 Bit -> RGB, wird pro Format einmal aufgebaut
static const uint32_t* Lut16(const PixelFormat& f)
{
    static PixelFormat s_fmt;
    static std::vector<uint32_t> s_lut;
    if (s_lut.empty() || s_fmt != f)
    {
        s_lut.resize(65536);
        for (uint32_t v = 0; v < 65536; ++v)
            s_lut[v] = Unpack(f, v);
        s_fmt = f;
    }
    return s_lut.data();
}

void ConvertFromSurface(const PixelFormat& f, const uint8_t* src, uint32_t* dst, int count)
{
    switch (f.bpp)
    {
    case 16:
    {
        const uint32_t* lut = Lut16(f);
        const uint16_t* s = reinterpret_cast<const uint16_t*>(src);
        for (int i = 0; i < count; ++i)
            dst[i] = lut[s[i]];
        break;
    }
    case 24:
        for (int i = 0; i < count; ++i, src += 3)
            dst[i] = Unpack(f, src[0] | (src[1] << 8) | (src[2] << 16));
        break;
    case 32:
    {
        const uint32_t* s = reinterpret_cast<const uint32_t*>(src);
        if (f.rMask == 0xFF0000 && f.gMask == 0xFF00 && f.bMask == 0xFF)
        {
            for (int i = 0; i < count; ++i)
                dst[i] = s[i] & 0xFFFFFF;
        }
        else
        {
            for (int i = 0; i < count; ++i)
                dst[i] = Unpack(f, s[i]);
        }
        break;
    }
    }
}

void ConvertToSurface(const PixelFormat& f, const uint32_t* src, uint8_t* dst, int count)
{
    switch (f.bpp)
    {
    case 16:
    {
        uint16_t* d = reinterpret_cast<uint16_t*>(dst);
        if (f.rMask == 0xF800 && f.gMask == 0x07E0 && f.bMask == 0x001F)
        {
            for (int i = 0; i < count; ++i)
            {
                const uint32_t c = src[i];
                d[i] = (uint16_t)(((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F));
            }
        }
        else
        {
            for (int i = 0; i < count; ++i)
                d[i] = (uint16_t)Pack(f, src[i]);
        }
        break;
    }
    case 24:
        for (int i = 0; i < count; ++i, dst += 3)
        {
            const uint32_t v = Pack(f, src[i]);
            dst[0] = (uint8_t)v;
            dst[1] = (uint8_t)(v >> 8);
            dst[2] = (uint8_t)(v >> 16);
        }
        break;
    case 32:
    {
        uint32_t* d = reinterpret_cast<uint32_t*>(dst);
        if (f.rMask == 0xFF0000 && f.gMask == 0xFF00 && f.bMask == 0xFF)
            memcpy(d, src, count * 4);
        else
            for (int i = 0; i < count; ++i)
                d[i] = Pack(f, src[i]);
        break;
    }
    }
}

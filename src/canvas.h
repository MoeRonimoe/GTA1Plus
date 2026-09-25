#pragma once

#include <windows.h>
#include <cstdint>

#include "surface.h"

// 32-Bit-Zeichenfläche (DIB-Section) – GDI für Text, eigene Routinen für Alpha-Formen
class Canvas
{
public:
    ~Canvas();

    bool Resize(int w, int h);
    int Width() const { return m_w; }
    int Height() const { return m_h; }
    uint32_t* Pixels() { return m_px; }
    uint32_t* Row(int y) { return m_px + y * m_w; }

    void Fill(int x, int y, int w, int h, uint32_t rgb);
    void Blend(int x, int y, int w, int h, uint32_t rgb, int alpha);
    void Frame(int x, int y, int w, int h, uint32_t rgb, int thickness, int alpha = 255);
    void Circle(float cx, float cy, float r, uint32_t rgb, int alpha = 255);
    void Ring(float cx, float cy, float r, float thickness, uint32_t rgb, int alpha = 255);
    void Triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb, int alpha = 255);
    // Beliebiges (auch nicht konvexes) Polygon in einem Durchgang, xy = x0, y0, x1, y1, ...
    void Polygon(const float* xy, int n, uint32_t rgb, int alpha = 255);
    void Line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, int alpha = 255);
    void BlendPixel(int x, int y, uint32_t rgb, int alpha);

    // Einschränkung für BlendPixel und alles, was darauf aufbaut (Kreis, Dreieck, Linie)
    void SetClipCircle(float cx, float cy, float r) { m_clipR = r; m_clipX = cx; m_clipY = cy; }
    void SetClipRect(int x, int y, int w, int h) { m_clipRect = { x, y, x + w, y + h }; m_hasClipRect = true; }
    void ClearClip() { m_clipR = 0; m_hasClipRect = false; }

    void Text(int x, int y, const wchar_t* s, HFONT font, uint32_t rgb, UINT align = TA_LEFT | TA_TOP, bool shadow = true);
    int TextWidth(const wchar_t* s, HFONT font);

    // Rechteck zwischen Spiel-Surface und Canvas kopieren (gleiche Koordinaten)
    void ReadFrom(const SurfaceView& v, int x, int y, int w, int h);
    void WriteTo(const SurfaceView& v, int x, int y, int w, int h);

private:
    bool Clip(int& x, int& y, int& w, int& h) const;

    HDC m_dc = nullptr;
    HBITMAP m_bmp = nullptr;
    HGDIOBJ m_oldBmp = nullptr;
    uint32_t* m_px = nullptr;
    int m_w = 0, m_h = 0;
    float m_clipX = 0, m_clipY = 0, m_clipR = 0;
    RECT m_clipRect = {};
    bool m_hasClipRect = false;
};

inline uint32_t BlendRgb(uint32_t dst, uint32_t src, int a)
{
    const uint32_t ia = 255 - a;
    const uint32_t rb = ((dst & 0xFF00FF) * ia + (src & 0xFF00FF) * a) >> 8;
    const uint32_t g = ((dst & 0x00FF00) * ia + (src & 0x00FF00) * a) >> 8;
    return (rb & 0xFF00FF) | (g & 0x00FF00);
}

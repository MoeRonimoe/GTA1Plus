#include "canvas.h"

#include <algorithm>
#include <cmath>
#include <vector>

bool Canvas::s_slowReadback = false;

namespace
{
    constexpr int kMaxSlowReads = 800;   // so viele Pixel darf eine Ebene höchstens zurücklesen, wenn Lesen langsam ist
    constexpr uint32_t kSnapAlpha = 64;  // sonst: ab dieser Deckung deckend, darunter weggelassen

    // Auf eine Ebene mischen: Farbe wie gehabt (ergibt die vormultiplizierte Farbe), Deckung im obersten Byte
    inline uint32_t BlendLayer(uint32_t dst, uint32_t rgb, int a)
    {
        const uint32_t da = dst >> 24;
        const uint32_t na = da + ((255 - da) * a + 127) / 255;
        return (na << 24) | BlendRgb(dst & 0xFFFFFF, rgb, a);
    }
}

Canvas::~Canvas()
{
    if (m_dc)
    {
        SelectObject(m_dc, m_oldBmp);
        DeleteObject(m_bmp);
        DeleteDC(m_dc);
    }
    if (m_maskDc)
    {
        SelectObject(m_maskDc, m_maskOld);
        DeleteObject(m_maskBmp);
        DeleteDC(m_maskDc);
    }
}

bool Canvas::Resize(int w, int h)
{
    if (w == m_w && h == m_h && m_px)
        return true;

    if (!m_dc)
        m_dc = CreateCompatibleDC(nullptr);
    if (m_bmp)
    {
        SelectObject(m_dc, m_oldBmp);
        DeleteObject(m_bmp);
        m_bmp = nullptr;
        m_px = nullptr;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    m_bmp = CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!m_bmp)
    {
        m_w = m_h = 0;
        return false;
    }
    m_oldBmp = SelectObject(m_dc, m_bmp);
    SetBkMode(m_dc, TRANSPARENT);
    m_px = static_cast<uint32_t*>(bits);
    m_w = w;
    m_h = h;
    return true;
}

bool Canvas::Clip(int& x, int& y, int& w, int& h) const
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > m_w) w = m_w - x;
    if (y + h > m_h) h = m_h - y;
    return w > 0 && h > 0;
}

void Canvas::Fill(int x, int y, int w, int h, uint32_t rgb)
{
    if (!Clip(x, y, w, h))
        return;
    if (m_layer)
        rgb |= 0xFF000000;
    for (int j = 0; j < h; ++j)
        std::fill_n(Row(y + j) + x, w, rgb);
}

void Canvas::Blend(int x, int y, int w, int h, uint32_t rgb, int alpha)
{
    if (!Clip(x, y, w, h))
        return;
    for (int j = 0; j < h; ++j)
    {
        uint32_t* p = Row(y + j) + x;
        if (m_layer)
            for (int i = 0; i < w; ++i)
                p[i] = BlendLayer(p[i], rgb, alpha);
        else
            for (int i = 0; i < w; ++i)
                p[i] = BlendRgb(p[i], rgb, alpha);
    }
}

void Canvas::Frame(int x, int y, int w, int h, uint32_t rgb, int t, int alpha)
{
    Blend(x, y, w, t, rgb, alpha);
    Blend(x, y + h - t, w, t, rgb, alpha);
    Blend(x, y + t, t, h - 2 * t, rgb, alpha);
    Blend(x + w - t, y + t, t, h - 2 * t, rgb, alpha);
}

void Canvas::BlendPixel(int x, int y, uint32_t rgb, int alpha)
{
    if (x < 0 || y < 0 || x >= m_w || y >= m_h || alpha <= 0)
        return;
    if (m_hasClipRect && (x < m_clipRect.left || y < m_clipRect.top || x >= m_clipRect.right || y >= m_clipRect.bottom))
        return;
    if (m_clipR > 0)
    {
        const float dx = x + 0.5f - m_clipX, dy = y + 0.5f - m_clipY;
        const float edge = m_clipR - std::sqrt(dx * dx + dy * dy);
        if (edge <= 0)
            return;
        if (edge < 1)
            alpha = (int)(alpha * edge);
    }
    uint32_t& p = Row(y)[x];
    if (m_layer)
        p = alpha >= 255 ? (rgb | 0xFF000000) : BlendLayer(p, rgb, alpha);
    else
        p = alpha >= 255 ? rgb : BlendRgb(p, rgb, alpha);
}

// Dicke Linie mit runden Enden; jedes Pixel wird genau einmal gemischt
void Canvas::Line(float x0, float y0, float x1, float y1, float width, uint32_t rgb, int alpha)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx * dx + dy * dy;
    const float half = width * 0.5f;
    const float len = std::sqrt(len2);
    const int chunks = std::max(1, (int)(len / 24.0f));
    for (int c = 0; c < chunks; ++c)
    {
        const float t0 = (float)c / chunks, t1 = (float)(c + 1) / chunks;
        const float ax = x0 + dx * t0, ay = y0 + dy * t0, bx = x0 + dx * t1, by = y0 + dy * t1;
        const int minX = (int)std::floor(std::min(ax, bx) - half - 1), maxX = (int)std::ceil(std::max(ax, bx) + half + 1);
        const int minY = (int)std::floor(std::min(ay, by) - half - 1), maxY = (int)std::ceil(std::max(ay, by) + half + 1);
        for (int y = minY; y <= maxY; ++y)
            for (int x = minX; x <= maxX; ++x)
            {
                const float px = x + 0.5f - x0, py = y + 0.5f - y0;
                float t = len2 > 0 ? (px * dx + py * dy) / len2 : 0.0f;
                t = std::min(1.0f, std::max(0.0f, t));
                // nur im Abschnitt zeichnen, in den die Projektion fällt
                if (t < t0 || (t >= t1 && !(c == chunks - 1 && t <= 1.0f)))
                    continue;
                const float ex = px - dx * t, ey = py - dy * t;
                const float cov = std::min(1.0f, half + 0.5f - std::sqrt(ex * ex + ey * ey));
                if (cov > 0)
                    BlendPixel(x, y, rgb, (int)(alpha * cov));
            }
    }
}

void Canvas::Circle(float cx, float cy, float r, uint32_t rgb, int alpha)
{
    const int x0 = (int)std::floor(cx - r - 1), x1 = (int)std::ceil(cx + r + 1);
    const int y0 = (int)std::floor(cy - r - 1), y1 = (int)std::ceil(cy + r + 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float cov = std::min(1.0f, r + 0.5f - std::sqrt(dx * dx + dy * dy));
            if (cov > 0)
                BlendPixel(x, y, rgb, (int)(alpha * cov));
        }
}

void Canvas::Ring(float cx, float cy, float r, float thickness, uint32_t rgb, int alpha)
{
    const float half = thickness * 0.5f;
    const int x0 = (int)std::floor(cx - r - half - 1), x1 = (int)std::ceil(cx + r + half + 1);
    const int y0 = (int)std::floor(cy - r - half - 1), y1 = (int)std::ceil(cy + r + half + 1);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
        {
            const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            const float d = std::fabs(std::sqrt(dx * dx + dy * dy) - r);
            const float cov = std::min(1.0f, half + 0.5f - d);
            if (cov > 0)
                BlendPixel(x, y, rgb, (int)(alpha * cov));
        }
}

// Dreieck mit 4x4-Supersampling für glatte Kanten
void Canvas::Triangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgb, int alpha)
{
    const int minX = (int)std::floor(std::min({ x0, x1, x2 })), maxX = (int)std::ceil(std::max({ x0, x1, x2 }));
    const int minY = (int)std::floor(std::min({ y0, y1, y2 })), maxY = (int)std::ceil(std::max({ y0, y1, y2 }));
    const float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (std::fabs(area) < 0.01f)
        return;
    const float sign = area > 0 ? 1.0f : -1.0f;

    auto inside = [&](float px, float py) {
        const float e0 = ((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0)) * sign;
        const float e1 = ((x2 - x1) * (py - y1) - (y2 - y1) * (px - x1)) * sign;
        const float e2 = ((x0 - x2) * (py - y2) - (y0 - y2) * (px - x2)) * sign;
        return e0 >= 0 && e1 >= 0 && e2 >= 0;
    };

    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x)
        {
            int hits = 0;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx)
                    hits += inside(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4);
            if (hits)
                BlendPixel(x, y, rgb, alpha * hits / 16);
        }
}

void Canvas::Polygon(const float* xy, int n, uint32_t rgb, int alpha)
{
    if (n < 3)
        return;
    float minXf = xy[0], maxXf = xy[0], minYf = xy[1], maxYf = xy[1];
    for (int i = 1; i < n; ++i)
    {
        minXf = std::min(minXf, xy[i * 2]);
        maxXf = std::max(maxXf, xy[i * 2]);
        minYf = std::min(minYf, xy[i * 2 + 1]);
        maxYf = std::max(maxYf, xy[i * 2 + 1]);
    }
    const int minX = (int)std::floor(minXf), maxX = (int)std::ceil(maxXf);
    const int minY = (int)std::floor(minYf), maxY = (int)std::ceil(maxYf);
    std::vector<int> cover(maxX - minX + 1);
    std::vector<float> cross;

    // Je Pixelzeile 4 Abtastzeilen; Kanten schneiden, Spannen (gerade/ungerade) mit 4 Abtastpunkten je Pixel zählen.
    // So wird jedes Pixel genau einmal gemischt und es entstehen keine Nähte zwischen Teilflächen.
    for (int y = minY; y <= maxY; ++y)
    {
        std::fill(cover.begin(), cover.end(), 0);
        for (int sy = 0; sy < 4; ++sy)
        {
            const float py = y + (sy + 0.5f) / 4;
            cross.clear();
            for (int i = 0; i < n; ++i)
            {
                const float x0 = xy[i * 2], y0 = xy[i * 2 + 1];
                const float x1 = xy[(i + 1) % n * 2], y1 = xy[(i + 1) % n * 2 + 1];
                if ((y0 <= py) != (y1 <= py))
                    cross.push_back(x0 + (py - y0) * (x1 - x0) / (y1 - y0));
            }
            std::sort(cross.begin(), cross.end());
            for (size_t k = 0; k + 1 < cross.size(); k += 2)
            {
                const float a = cross[k], b = cross[k + 1];
                for (int x = std::max(minX, (int)std::floor(a)); x <= std::min(maxX, (int)std::ceil(b)); ++x)
                    for (int sx = 0; sx < 4; ++sx)
                    {
                        const float px = x + (sx + 0.5f) / 4;
                        if (px >= a && px < b)
                            ++cover[x - minX];
                    }
            }
        }
        for (int x = minX; x <= maxX; ++x)
            if (const int c = cover[x - minX])
                BlendPixel(x, y, rgb, alpha * std::min(c, 16) / 16);
    }
}

static COLORREF ToColorRef(uint32_t rgb)
{
    return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

bool Canvas::EnsureMask(int w, int h)
{
    if (!m_maskDc)
    {
        m_maskDc = CreateCompatibleDC(nullptr);
        if (!m_maskDc)
            return false;
        SetBkMode(m_maskDc, TRANSPARENT);
        SetTextColor(m_maskDc, RGB(255, 255, 255));
        SetTextAlign(m_maskDc, TA_LEFT | TA_TOP);
    }
    if (m_maskPx && w <= m_maskW && h <= m_maskH)
        return true;
    if (m_maskBmp)
    {
        SelectObject(m_maskDc, m_maskOld);
        DeleteObject(m_maskBmp);
        m_maskBmp = nullptr;
        m_maskPx = nullptr;
    }
    m_maskW = std::max(w, m_maskW);
    m_maskH = std::max(h, m_maskH);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = m_maskW;
    bi.bmiHeader.biHeight = -m_maskH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    m_maskBmp = CreateDIBSection(m_maskDc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!m_maskBmp)
    {
        m_maskW = m_maskH = 0;
        return false;
    }
    m_maskOld = SelectObject(m_maskDc, m_maskBmp);
    m_maskPx = static_cast<uint32_t*>(bits);
    return true;
}

// Maske (weiß auf schwarz gezeichneter Text) als Deckung in die Ebene mischen
void Canvas::BlendMask(int left, int top, int w, int h, uint32_t rgb)
{
    for (int j = 0; j < h; ++j)
    {
        const int y = top + j;
        if (y < 0 || y >= m_h)
            continue;
        const uint32_t* mask = m_maskPx + (size_t)j * m_maskW;
        uint32_t* row = Row(y);
        for (int i = 0; i < w; ++i)
        {
            const int cov = (mask[i] >> 8) & 0xFF;
            const int x = left + i;
            if (cov && x >= 0 && x < m_w)
                row[x] = cov >= 255 ? (rgb | 0xFF000000) : BlendLayer(row[x], rgb, cov);
        }
    }
}

// Text in einer Ebene: GDI zeichnet ihn in eine eigene Maske, daraus wird die Deckung
void Canvas::TextLayer(int x, int y, const wchar_t* s, HFONT font, uint32_t rgb, UINT align, bool shadow)
{
    const int len = lstrlenW(s);
    if (!len || !EnsureMask(1, 1))
        return;
    HGDIOBJ old = SelectObject(m_maskDc, font);
    SIZE sz = {};
    GetTextExtentPoint32W(m_maskDc, s, len, &sz);
    constexpr int pad = 2;
    const int w = sz.cx + 2 * pad + sz.cy / 2, h = sz.cy + 2 * pad;   // etwas Luft für überhängende Zeichen
    SelectObject(m_maskDc, old);
    if (!EnsureMask(w, h))
        return;
    old = SelectObject(m_maskDc, font);
    for (int j = 0; j < h; ++j)
        std::fill_n(m_maskPx + (size_t)j * m_maskW, w, 0u);
    TextOutW(m_maskDc, pad, pad, s, len);
    GdiFlush();
    SelectObject(m_maskDc, old);

    int left = x - pad;
    if ((align & TA_CENTER) == TA_CENTER)
        left = x - sz.cx / 2 - pad;
    else if (align & TA_RIGHT)
        left = x - sz.cx - pad;
    if (shadow)
        BlendMask(left + 1, y - pad + 1, w, h, 0x000000);
    BlendMask(left, y - pad, w, h, rgb);
}

void Canvas::Text(int x, int y, const wchar_t* s, HFONT font, uint32_t rgb, UINT align, bool shadow)
{
    if (m_layer)
    {
        TextLayer(x, y, s, font, rgb, align, shadow);
        return;
    }
    const HGDIOBJ old = SelectObject(m_dc, font);
    SetTextAlign(m_dc, align);
    const int len = lstrlenW(s);
    if (shadow)
    {
        SetTextColor(m_dc, RGB(0, 0, 0));
        TextOutW(m_dc, x + 1, y + 1, s, len);
    }
    SetTextColor(m_dc, ToColorRef(rgb));
    TextOutW(m_dc, x, y, s, len);
    SelectObject(m_dc, old);
    GdiFlush();
}

int Canvas::TextWidth(const wchar_t* s, HFONT font)
{
    const HGDIOBJ old = SelectObject(m_dc, font);
    SIZE sz = {};
    GetTextExtentPoint32W(m_dc, s, lstrlenW(s), &sz);
    SelectObject(m_dc, old);
    return sz.cx;
}

void Canvas::ReadFrom(const SurfaceView& v, int x, int y, int w, int h)
{
    if (x + w > v.width) w = v.width - x;
    if (y + h > v.height) h = v.height - y;
    if (!Clip(x, y, w, h))
        return;
    const int bpp = v.fmt.BytesPerPixel();
    for (int j = 0; j < h; ++j)
        ConvertFromSurface(v.fmt, v.Row(y + j) + x * bpp, Row(y + j) + x, w);
}

void Canvas::BeginLayer(int x, int y, int w, int h)
{
    m_layer = true;
    if (!Clip(x, y, w, h))
        return;
    for (int j = 0; j < h; ++j)
        std::fill_n(Row(y + j) + x, w, 0u);
}

void Canvas::Composite(const SurfaceView& v, int x, int y, int w, int h)
{
    m_layer = false;
    if (x + w > v.width) w = v.width - x;
    if (y + h > v.height) h = v.height - y;
    if (!Clip(x, y, w, h))
        return;
    const int bpp = v.fmt.BytesPerPixel();

    // Halbdurchsichtige Pixel brauchen den Hintergrund. Ist das Lesen langsam, sind nur wenige erlaubt (weiche
    // Kanten von Formen); Ebenen mit großen halbdurchsichtigen Flächen werden stattdessen deckend gezeichnet.
    bool snap = false;
    if (s_slowReadback)
    {
        int translucent = 0;
        for (int j = 0; j < h && translucent <= kMaxSlowReads; ++j)
        {
            const uint32_t* row = Row(y + j) + x;
            for (int i = 0; i < w; ++i)
                translucent += (row[i] >> 24) - 1u < 254u;
        }
        snap = translucent > kMaxSlowReads;
    }

    for (int j = 0; j < h; ++j)
    {
        uint32_t* row = Row(y + j) + x;
        uint8_t* dst = v.Row(y + j) + x * bpp;
        int i = 0;
        while (i < w)
        {
            uint32_t a = row[i] >> 24;
            if (a == 0 || (snap && a < kSnapAlpha))
            {
                ++i;
                continue;
            }
            if (a == 255 || snap)
            {
                // deckende Pixel am Stück schreiben
                int k = i;
                for (; k < w; ++k)
                {
                    a = row[k] >> 24;
                    if (a == 255)
                        row[k] &= 0xFFFFFF;
                    else if (snap && a >= kSnapAlpha)
                    {
                        // vormultiplizierte Farbe zurückrechnen
                        const uint32_t c = row[k];
                        const uint32_t r = std::min(255u, ((c >> 16) & 0xFF) * 255 / a), g = std::min(255u, ((c >> 8) & 0xFF) * 255 / a),
                                       b = std::min(255u, (c & 0xFF) * 255 / a);
                        row[k] = (r << 16) | (g << 8) | b;
                    }
                    else
                        break;
                }
                ConvertToSurface(v.fmt, row + i, dst + i * bpp, k - i);
                i = k;
                continue;
            }
            uint32_t bg = 0;
            ConvertFromSurface(v.fmt, dst + i * bpp, &bg, 1);
            const uint32_t c = row[i], ia = 255 - a;
            const uint32_t r = std::min(255u, (((bg >> 16) & 0xFF) * ia + 127) / 255 + ((c >> 16) & 0xFF));
            const uint32_t g = std::min(255u, (((bg >> 8) & 0xFF) * ia + 127) / 255 + ((c >> 8) & 0xFF));
            const uint32_t b = std::min(255u, ((bg & 0xFF) * ia + 127) / 255 + (c & 0xFF));
            const uint32_t out = (r << 16) | (g << 8) | b;
            ConvertToSurface(v.fmt, &out, dst + i * bpp, 1);
            ++i;
        }
    }
}

void Canvas::WriteTo(const SurfaceView& v, int x, int y, int w, int h)
{
    GdiFlush();
    if (x + w > v.width) w = v.width - x;
    if (y + h > v.height) h = v.height - y;
    if (!Clip(x, y, w, h))
        return;
    const int bpp = v.fmt.BytesPerPixel();
    for (int j = 0; j < h; ++j)
        ConvertToSurface(v.fmt, Row(y + j) + x, v.Row(y + j) + x * bpp, w);
}

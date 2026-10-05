#include "ddhook.h"
#include "config.h"
#include "gta1.h"
#include "hooks.h"
#include "log.h"
#include "overlay.h"

#include <ddraw.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ddhook
{
    namespace
    {
        using GetProcAddress_t   = FARPROC(WINAPI*)(HMODULE, LPCSTR);
        using DirectDrawCreate_t = HRESULT(WINAPI*)(GUID*, LPDIRECTDRAW*, IUnknown*);
        using CreateSurface_t    = HRESULT(STDMETHODCALLTYPE*)(IDirectDraw*, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*, IUnknown*);
        using Flip_t             = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPDIRECTDRAWSURFACE, DWORD);
        using Lock_t             = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPRECT, LPDDSURFACEDESC, DWORD, HANDLE);
        using Unlock_t           = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPVOID);
        using Blt_t              = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPRECT, LPDIRECTDRAWSURFACE, LPRECT, DWORD, LPDDBLTFX);

        constexpr int kVtBlt           = 5;  // IDirectDrawSurface::Blt
        constexpr int kVtCreateSurface = 6;  // IDirectDraw::CreateSurface
        constexpr int kVtFlip          = 11; // IDirectDrawSurface::Flip
        constexpr int kVtLock          = 25; // IDirectDrawSurface::Lock
        constexpr int kVtUnlock        = 32; // IDirectDrawSurface::Unlock

        GetProcAddress_t   s_origGetProcAddress = nullptr;
        GetProcAddress_t*  s_iatGetProcAddress = nullptr; // IAT-Eintrag, den wir zur Laufzeit weiterrufen
        void*              s_loadCell = nullptr;          // Zelle, aus der MGL jetzt GetProcAddress lädt
        DirectDrawCreate_t s_realDirectDrawCreate = nullptr;
        DirectDrawCreate_t* s_createCell = nullptr;       // Zelle des Spiels mit DirectDrawCreate (Aufruf "call [Zelle]")
        void*              s_createCallCell = nullptr;    // Zelle, über die dieser Aufruf jetzt läuft
        CreateSurface_t    s_origCreateSurface = nullptr;
        Flip_t             s_origFlip = nullptr;

        IDirectDrawSurface* s_primary = nullptr;
        int      s_chainLength = 2;
        DWORD    s_lastFlip = 0;
        DWORD    s_gameThread = 0;
        unsigned s_flips = 0;

        // ---- Zwischenpuffer -----------------------------------------------------------------------------------
        //
        // Im Fenstermodus des ASI-Loaders (Classics-Ausgabe mit Widescreen-Fix) liegen die Seiten des Spiels im
        // Grafikspeicher. Der Loader kopiert bei jedem Seitenwechsel die Zielseite in die Primary und bringt die
        // Primary außerdem von sich aus regelmäßig ins Fenster. MGL zeichnet reihum in alle Seiten, auch direkt in
        // die Primary: jedes achte Bild ist deshalb schon beim Entstehen zu sehen (Flackern). Dazu kostet Lesen aus
        // diesem Speicher einige hundert Mal so viel wie Schreiben.
        //
        // Darum bekommt das Spiel beim Sperren einer Seite einen Puffer im Arbeitsspeicher. Beim Seitenwechsel
        // zeichnet das Overlay dort hinein, das fertige Bild geht in einem Stück in eine Seite, die gerade nicht zu
        // sehen ist, und erst der Seitenwechsel bringt es auf den Schirm.
        //
        // Das Hauptmenü (und alles andere außerhalb der Level) zeichnet das Spiel dort 640x480 groß in die linke
        // obere Ecke einer Primary in voller Auflösung - ohne Seitenwechsel; vor jedem Bild sperrt es sie kurz, um
        // sich den Bildzeiger zu holen. Auch sie wird umgeleitet: bei jedem Sperren ist das vorige Bild fertig und
        // kommt aus dem Puffer vergrößert in den Bildspeicher.
        enum class Buffering { Off, Game, Menu };

        struct Page
        {
            IDirectDrawSurface* surf = nullptr;
            uint8_t* bits = nullptr;   // Puffer im Arbeitsspeicher mit den Maßen der Surface
            bool fresh = false;        // das Spiel hat beim letzten Sperren den Zeiger auf den Puffer bekommen
        };

        constexpr int    kMaxPages = 8;
        constexpr double kSlowReadUs = 100.0;   // 16 KB lesen: Arbeitsspeicher wenige µs, Grafikspeicher um 500

        Buffering   s_buffering = Buffering::Off;
        Page        s_pages[kMaxPages];
        int         s_pageCount = 0;
        bool        s_allocFailed = false;
        int         s_bufW = 0, s_bufH = 0, s_bufPitch = 0, s_bufBytes = 0;
        PixelFormat s_bufFmt;
        DWORD       s_drawThread = 0;          // Thread, der die Surfaces angelegt hat und hineinzeichnet
        int         s_own = 0;                 // > 0: eigener DirectDraw-Aufruf, der den echten Bildspeicher will
        Page*       s_shown = nullptr;         // Seite mit dem Bild, das zuletzt auf den Schirm ging
        IDirectDrawSurface* s_stage = nullptr; // Seite, über die Bilder der Primary auf den Schirm gehen
        Lock_t      s_origLock = nullptr;
        Unlock_t    s_origUnlock = nullptr;
        Blt_t       s_origBlt = nullptr;
        LONGLONG    s_uploadTicks = 0;

        struct Own
        {
            Own() { ++s_own; }
            ~Own() { --s_own; }
        };

        // Hauptmenü
        constexpr int kMenuW = 640, kMenuH = 480;
        std::vector<uint8_t>  s_menuLast;     // zuletzt gezeigter Menüausschnitt (kMenuW x kMenuH, Format der Surface)
        std::vector<uint8_t>  s_menuRow;      // eine fertige Zeile im Format der Surface
        std::vector<int>      s_menuX, s_menuY;    // Quellspalte je Zielspalte, Quellzeile je Zielzeile
        std::vector<uint16_t> s_menuXw, s_menuYw;  // Anteil der nächsten Spalte bzw. Zeile (0..256), nur mit Glätten
        std::vector<uint32_t> s_menuRgb, s_menuA, s_menuB, s_menuMix;   // Glätten: Zeilen als 0x00RRGGBB
        int      s_menuOutW = 0, s_menuOutH = 0, s_menuLeft = 0, s_menuTop = 0;
        bool     s_menuSmooth = false;        // krummer Faktor: Pixelkanten anteilig mischen
        uint32_t s_menuFill = 0;              // Pixelwert, mit dem das Spiel die Seite löscht
        bool     s_menuShown = false;         // der Bildspeicher enthält schon ein Bild von uns (Ränder gefüllt)
        bool     s_menuWhole = false;         // das Spiel zeichnet über den Ausschnitt hinaus: unverändert zeigen
        DWORD    s_menuTick = 0;              // letzter Vergleich aus der Nachrichtenschleife
        DWORD    s_menuLockTick = 0;          // zuletzt hat das Spiel die Seite gesperrt

        bool LockToView(IDirectDrawSurface* surf, SurfaceView& v)
        {
            DDSURFACEDESC sd = {};
            sd.dwSize = sizeof(sd);
            {
                Own own;
                if (FAILED(surf->Lock(nullptr, &sd, DDLOCK_WAIT, nullptr)))
                    return false;
            }

            v.bits = static_cast<uint8_t*>(sd.lpSurface);
            v.pitch = sd.lPitch;
            v.width = sd.dwWidth;
            v.height = sd.dwHeight;

            DDPIXELFORMAT pf = sd.ddpfPixelFormat;
            if (!(sd.dwFlags & DDSD_PIXELFORMAT) || !pf.dwRGBBitCount)
            {
                DDSURFACEDESC full = {};
                full.dwSize = sizeof(full);
                if (SUCCEEDED(surf->GetSurfaceDesc(&full)))
                    pf = full.ddpfPixelFormat;
            }
            if ((pf.dwFlags & DDPF_RGB) && pf.dwRGBBitCount >= 15)
            {
                uint32_t r = pf.dwRBitMask, g = pf.dwGBitMask, b = pf.dwBBitMask;
                if (!r || !g || !b)
                {
                    const bool is16 = pf.dwRGBBitCount <= 16;
                    r = is16 ? 0xF800 : 0xFF0000;
                    g = is16 ? 0x07E0 : 0x00FF00;
                    b = is16 ? 0x001F : 0x0000FF;
                }
                v.fmt = MakePixelFormat(pf.dwRGBBitCount, r, g, b);
            }
            else
            {
                v.fmt = PixelFormat();
                v.fmt.bpp = pf.dwRGBBitCount; // z.B. 8 Bit mit Palette: wird nicht bemalt
            }
            return true;
        }

        void DropBuffers()
        {
            for (Page& p : s_pages)
            {
                if (p.bits)
                    VirtualFree(p.bits, 0, MEM_RELEASE);
                p = Page();
            }
            s_pageCount = 0;
            s_allocFailed = false;
            s_buffering = Buffering::Off;
            s_shown = nullptr;
            s_stage = nullptr;
            s_menuShown = false;
            s_menuWhole = false;
            s_menuFill = 0;
        }

        Page* FindPage(IDirectDrawSurface* surf)
        {
            for (int i = 0; i < s_pageCount; ++i)
                if (s_pages[i].surf == surf)
                    return &s_pages[i];
            return nullptr;
        }

        Page* AddPage(IDirectDrawSurface* surf)
        {
            if (s_pageCount >= kMaxPages || s_allocFailed)
                return nullptr;
            if (surf != s_primary && (s_buffering != Buffering::Game || !gta1::IsPageSurface(surf)))
                return nullptr;
            // Unsere Hooks hängen in der VTable der Primary; eine Surface anderer Bauart bleibt, wie sie ist
            if (*reinterpret_cast<void***>(surf) != *reinterpret_cast<void***>(s_primary))
                return nullptr;
            void* bits = VirtualAlloc(nullptr, (size_t)s_bufPitch * s_bufH, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!bits)
            {
                LOG("Zwischenpuffer: kein Speicher für Seite %d - sie wird weiter direkt gezeichnet", s_pageCount + 1);
                s_allocFailed = true;
                return nullptr;
            }
            Page& p = s_pages[s_pageCount++];
            p.surf = surf;
            p.bits = static_cast<uint8_t*>(bits);
            p.fresh = false;
            return &p;
        }

        SurfaceView BufferView(const Page& p)
        {
            SurfaceView v;
            v.bits = p.bits;
            v.pitch = s_bufPitch;
            v.width = s_bufW;
            v.height = s_bufH;
            v.fmt = s_bufFmt;
            v.buffered = true;
            return v;
        }

        // Füllen einer Fläche (Blt mit Farbe), wie es MGL zum Löschen einer Seite benutzt
        void FillBuffer(Page& p, const RECT* r, uint32_t color)
        {
            int x0 = 0, y0 = 0, x1 = s_bufW, y1 = s_bufH;
            if (r)
            {
                x0 = std::max<int>(r->left, 0);
                y0 = std::max<int>(r->top, 0);
                x1 = std::min<int>(r->right, s_bufW);
                y1 = std::min<int>(r->bottom, s_bufH);
            }
            if (x0 >= x1 || y0 >= y1)
                return;

            const int n = x1 - x0;
            uint8_t* first = p.bits + (size_t)y0 * s_bufPitch + (size_t)x0 * s_bufBytes;
            switch (s_bufBytes)
            {
            case 1:
                memset(first, (int)(color & 0xFF), n);
                break;
            case 2:
                std::fill_n(reinterpret_cast<uint16_t*>(first), n, (uint16_t)color);
                break;
            case 4:
                std::fill_n(reinterpret_cast<uint32_t*>(first), n, color);
                break;
            default:
                for (int i = 0; i < n; ++i)
                    memcpy(first + i * s_bufBytes, &color, s_bufBytes);
                break;
            }
            for (int y = y0 + 1; y < y1; ++y)
                memcpy(p.bits + (size_t)y * s_bufPitch + (size_t)x0 * s_bufBytes, first, (size_t)n * s_bufBytes);

            if (s_buffering == Buffering::Menu && x0 == 0 && y0 == 0 && x1 == s_bufW && y1 == s_bufH && s_menuFill != color)
            {
                s_menuFill = color;
                s_menuShown = false; // Ränder in der neuen Farbe
            }
        }

        void FillPixels(uint8_t* dst, int count, uint32_t pixel)
        {
            switch (s_bufBytes)
            {
            case 1:
                memset(dst, (int)(pixel & 0xFF), count);
                break;
            case 2:
                std::fill_n(reinterpret_cast<uint16_t*>(dst), count, (uint16_t)pixel);
                break;
            case 4:
                std::fill_n(reinterpret_cast<uint32_t*>(dst), count, pixel);
                break;
            default:
                for (int i = 0; i < count; ++i)
                    memcpy(dst + i * s_bufBytes, &pixel, s_bufBytes);
                break;
            }
        }

        // Zeichnet das Spiel über den Menüausschnitt hinaus (Stichproben), ist es kein 640x480-Bild in der Ecke
        bool DrawsBeyondMenu(const Page& p)
        {
            for (int y = 8; y < s_bufH; y += 16)
            {
                const uint8_t* row = p.bits + (size_t)y * s_bufPitch;
                for (int x = y < kMenuH ? kMenuW + 8 : 8; x < s_bufW; x += 16)
                    if (memcmp(row + (size_t)x * s_bufBytes, &s_menuFill, s_bufBytes) != 0)
                        return true;
            }
            return false;
        }

        // Hat sich das Menü im Puffer geändert, seit es zuletzt in den Bildspeicher kam?
        bool MenuNeedsDrawing(const Page& p)
        {
            const bool whole = DrawsBeyondMenu(p);
            if (whole != s_menuWhole)
            {
                s_menuWhole = whole;
                s_menuShown = false;
                LOG("Hauptmenü: das Spiel zeichnet %s", whole ? "über 640x480 hinaus - Bild bleibt unverändert" : "wieder im Ausschnitt 640x480");
            }
            if (whole)
                return true;

            bool changed = !s_menuShown;
            const size_t rowBytes = (size_t)kMenuW * s_bufBytes;
            for (int y = 0; y < kMenuH; ++y)
            {
                const uint8_t* src = p.bits + (size_t)y * s_bufPitch;
                uint8_t* last = &s_menuLast[y * rowBytes];
                if (changed || memcmp(last, src, rowBytes) != 0)
                {
                    memcpy(last, src, rowBytes);
                    changed = true;
                }
            }
            return changed;
        }

        inline uint32_t Mix(uint32_t a, uint32_t b, uint32_t w)
        {
            const uint32_t rb = ((a & 0xFF00FF) * (256 - w) + (b & 0xFF00FF) * w) >> 8;
            const uint32_t g = ((a & 0x00FF00) * (256 - w) + (b & 0x00FF00) * w) >> 8;
            return (rb & 0xFF00FF) | (g & 0x00FF00);
        }

        // Eine Zeile des Menüs in der Breite vergrößern (0x00RRGGBB), Kanten zwischen zwei Quellpixeln anteilig
        void MenuRowSmooth(int sy, std::vector<uint32_t>& out)
        {
            ConvertFromSurface(s_bufFmt, &s_menuLast[(size_t)sy * kMenuW * s_bufBytes], s_menuRgb.data(), kMenuW);
            const uint32_t* src = s_menuRgb.data();
            for (int x = 0; x < s_menuOutW; ++x)
            {
                const int i = s_menuX[x];
                const uint32_t w = s_menuXw[x];
                out[x] = w ? Mix(src[i], src[i + 1], w) : src[i];
            }
        }

        // Menü (bzw. das ganze Bild, wenn das Spiel darüber hinaus zeichnet) in den gesperrten Bildspeicher schreiben
        void DrawMenu(const Page& p, uint8_t* dst, int pitch)
        {
            if (s_menuWhole)
            {
                for (int y = 0; y < s_bufH; ++y)
                    memcpy(dst + (size_t)y * pitch, p.bits + (size_t)y * s_bufPitch, (size_t)s_bufW * s_bufBytes);
                s_menuShown = true;
                return;
            }
            if (!s_menuShown)
                for (int y = 0; y < s_bufH; ++y)
                    FillPixels(dst + (size_t)y * pitch, s_bufW, s_menuFill);

            const size_t outBytes = (size_t)s_menuOutW * s_bufBytes;
            uint8_t* row = s_menuRow.data();
            int rowA = -1, rowB = -1;   // Glätten: Quellzeilen, die in s_menuA und s_menuB stehen
            int packed = -1;            // Quellzeile, die unverändert in "row" steht
            for (int y = 0; y < s_menuOutH; ++y)
            {
                const int sy = s_menuY[y];
                const uint32_t w = s_menuSmooth ? s_menuYw[y] : 0;
                if (!s_menuSmooth)
                {
                    if (sy != packed)
                    {
                        const uint8_t* src = &s_menuLast[(size_t)sy * kMenuW * s_bufBytes];
                        switch (s_bufBytes)
                        {
                        case 1:
                            for (int x = 0; x < s_menuOutW; ++x)
                                row[x] = src[s_menuX[x]];
                            break;
                        case 2:
                            for (int x = 0; x < s_menuOutW; ++x)
                                reinterpret_cast<uint16_t*>(row)[x] = reinterpret_cast<const uint16_t*>(src)[s_menuX[x]];
                            break;
                        case 4:
                            for (int x = 0; x < s_menuOutW; ++x)
                                reinterpret_cast<uint32_t*>(row)[x] = reinterpret_cast<const uint32_t*>(src)[s_menuX[x]];
                            break;
                        default:
                            for (int x = 0; x < s_menuOutW; ++x)
                                memcpy(row + (size_t)x * s_bufBytes, src + (size_t)s_menuX[x] * s_bufBytes, s_bufBytes);
                            break;
                        }
                        packed = sy;
                    }
                }
                else
                {
                    if (rowA != sy)
                    {
                        if (rowB == sy)
                        {
                            s_menuA.swap(s_menuB);
                            std::swap(rowA, rowB);
                        }
                        else
                        {
                            MenuRowSmooth(sy, s_menuA);
                            rowA = sy;
                        }
                    }
                    if (w)
                    {
                        if (rowB != sy + 1)
                        {
                            MenuRowSmooth(sy + 1, s_menuB);
                            rowB = sy + 1;
                        }
                        for (int x = 0; x < s_menuOutW; ++x)
                            s_menuMix[x] = Mix(s_menuA[x], s_menuB[x], w);
                        ConvertToSurface(s_bufFmt, s_menuMix.data(), row, s_menuOutW);
                        packed = -1;
                    }
                    else if (packed != sy)
                    {
                        ConvertToSurface(s_bufFmt, s_menuA.data(), row, s_menuOutW);
                        packed = sy;
                    }
                }
                memcpy(dst + (size_t)(s_menuTop + y) * pitch + (size_t)s_menuLeft * s_bufBytes, row, outBytes);
            }
            s_menuShown = true;
        }

        // Aufteilung der Zielpixel auf die Quellpixel: jedes Zielpixel deckt 1/Faktor Quellpixel ab. Reicht es über
        // eine Pixelgrenze, wird anteilig gemischt (nur mit Glätten, sonst zählt die Mitte).
        void MenuAxis(int outSize, int srcSize, bool smooth, std::vector<int>& index, std::vector<uint16_t>& weight)
        {
            index.resize(outSize);
            weight.assign(outSize, 0);
            const double scale = outSize / (double)srcSize;
            for (int i = 0; i < outSize; ++i)
            {
                if (!smooth)
                {
                    index[i] = std::min(srcSize - 1, (int)((i + 0.5) / scale));
                    continue;
                }
                const double from = i / scale, to = (i + 1) / scale;
                const int first = std::min(srcSize - 1, (int)from);
                index[i] = first;
                if (to > first + 1 && first + 1 < srcSize)
                    weight[i] = (uint16_t)std::clamp((int)((to - (first + 1)) * scale * 256.0 + 0.5), 0, 256);
            }
        }


        HRESULT STDMETHODCALLTYPE Hook_Lock(IDirectDrawSurface* self, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event)
        {
            const HRESULT hr = s_origLock(self, rect, desc, flags, event);
            if (s_own || s_buffering == Buffering::Off || FAILED(hr) || !desc || GetCurrentThreadId() != s_drawThread)
                return hr;
            Page* p = FindPage(self);
            if (!p)
                p = AddPage(self);
            if (!p)
                return hr;
            // Nur die ganze Seite in den bekannten Maßen umleiten; alles andere zeichnet direkt
            if (rect || (int)desc->dwWidth != s_bufW || (int)desc->dwHeight != s_bufH || desc->lPitch != s_bufPitch)
            {
                p->fresh = false;
                return hr;
            }
            if (s_buffering == Buffering::Menu)
            {
                // Das Spiel holt sich den Zeiger für das nächste Bild: das vorige ist fertig
                if (p->fresh && MenuNeedsDrawing(*p))
                    DrawMenu(*p, static_cast<uint8_t*>(desc->lpSurface), desc->lPitch);
                s_menuLockTick = GetTickCount();
            }
            desc->lpSurface = p->bits;
            p->fresh = true;
            return hr;
        }

        HRESULT STDMETHODCALLTYPE Hook_Unlock(IDirectDrawSurface* self, LPVOID data)
        {
            if (data && !s_own && s_buffering != Buffering::Off)
            {
                // Unser Zeiger sagt der Surface nichts; gesperrt war die ganze Seite
                const Page* p = FindPage(self);
                if (p && data == p->bits)
                    data = nullptr;
            }
            return s_origUnlock(self, data);
        }

        HRESULT STDMETHODCALLTYPE Hook_Blt(IDirectDrawSurface* self, LPRECT dst, LPDIRECTDRAWSURFACE src, LPRECT srcRect, DWORD flags, LPDDBLTFX fx)
        {
            if (!s_own && s_buffering != Buffering::Off)
            {
                Page* p = FindPage(self);
                if (p && p->fresh)
                {
                    if ((flags & DDBLT_COLORFILL) && !(flags & DDBLT_ROP) && fx && !src)
                    {
                        FillBuffer(*p, dst, fx->dwFillColor);
                        return DD_OK;
                    }
                    static int s_logged = 0;
                    if (src != self && s_logged++ < 3)
                        LOG("Zwischenpuffer: Blt mit Flags 0x%X auf eine gepufferte Seite wird nicht nachgebildet", (unsigned)flags);
                }
            }
            return s_origBlt(self, dst, src, srcRect, flags, fx);
        }

        // wndmode (der Fenstermodus des ASI-Loaders) schreibt beim Anlegen von Surfaces seine eigenen Einträge neu
        // in die gemeinsame VTable; unsere Hooks sind danach fort
        void HookDrawing(IDirectDrawSurface* surf)
        {
            void** vt = *reinterpret_cast<void***>(surf);
            void* const lock = reinterpret_cast<void*>(&Hook_Lock);
            void* const unlock = reinterpret_cast<void*>(&Hook_Unlock);
            void* const blt = reinterpret_cast<void*>(&Hook_Blt);
            if (vt[kVtLock] == lock && vt[kVtUnlock] == unlock && vt[kVtBlt] == blt)
                return;

            const bool lost = s_origLock != nullptr;
            void* prev = hooks::PatchVTable(surf, kVtLock, lock);
            if (prev != lock)
                s_origLock = reinterpret_cast<Lock_t>(prev);
            prev = hooks::PatchVTable(surf, kVtUnlock, unlock);
            if (prev != unlock)
                s_origUnlock = reinterpret_cast<Unlock_t>(prev);
            prev = hooks::PatchVTable(surf, kVtBlt, blt);
            if (prev != blt)
                s_origBlt = reinterpret_cast<Blt_t>(prev);

            // Solange sie fehlten, kann sich das Spiel Zeiger auf den echten Bildspeicher geholt haben. Im Level
            // sperrt MGL vor jedem Bild neu, bis dahin gilt die Seite als direkt gezeichnet.
            if (s_buffering == Buffering::Game)
                for (int i = 0; i < s_pageCount; ++i)
                    s_pages[i].fresh = false;
            static int s_logged = 0;
            if (lost && s_pageCount && s_logged++ < 5)
                LOG("Zwischenpuffer: Sperr-Hooks waren überschrieben und sind neu gesetzt");
        }

        // Dauer in µs, 16 KB aus der Bildmitte zu lesen
        double ReadMicroseconds(const SurfaceView& v)
        {
            const int rowBytes = std::min(v.pitch, 4096) & ~3;
            LARGE_INTEGER f, t0, t1;
            QueryPerformanceFrequency(&f);
            double best = 1e9;
            volatile uint32_t sink = 0;
            for (int pass = 0; pass < 2; ++pass)
            {
                QueryPerformanceCounter(&t0);
                for (int k = 0; k < 4; ++k)
                {
                    const uint8_t* row = v.Row(std::min(v.height - 1, v.height / 2 + k));
                    uint32_t sum = 0;
                    for (int i = 0; i < rowBytes; i += 4)
                    {
                        uint32_t word;
                        memcpy(&word, row + i, 4);
                        sum += word;
                    }
                    sink = sink + sum;
                }
                QueryPerformanceCounter(&t1);
                best = std::min(best, (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart);
            }
            return best;
        }

        // Nach dem Anlegen einer Primary: braucht sie den Zwischenpuffer?
        void SetUpBuffering(IDirectDrawSurface* primary)
        {
            DropBuffers();
            s_drawThread = GetCurrentThreadId();
            if (!gta1::Supported() || (!g_cfg.frameBuffer && !g_cfg.scaleMenu))
                return;

            SurfaceView v;
            if (!LockToView(primary, v))
                return;
            const double us = v.bits && v.pitch > 0 ? ReadMicroseconds(v) : 0.0;
            primary->Unlock(nullptr);
            if (!v.bits || v.pitch <= 0 || v.width <= 0 || v.height <= 0)
                return;

            Buffering mode = Buffering::Off;
            if (s_chainLength >= 2 && v.fmt.Supported() && g_cfg.frameBuffer && us > kSlowReadUs)
                mode = Buffering::Game;
            else if (s_chainLength == 1 && (v.fmt.bpp == 8 || v.fmt.Supported()) && g_cfg.scaleMenu && v.width >= kMenuW &&
                     v.height >= kMenuH && (v.width > kMenuW || v.height > kMenuH))
                mode = Buffering::Menu;
            if (mode == Buffering::Off)
                return;

            s_bufW = v.width;
            s_bufH = v.height;
            s_bufPitch = v.pitch;
            s_bufBytes = std::max(1, v.fmt.bpp / 8);
            s_bufFmt = v.fmt;
            s_buffering = mode;
            HookDrawing(primary);

            if (mode == Buffering::Game)
            {
                LOG("Zwischenpuffer: Bilder entstehen im Arbeitsspeicher (%dx%d, %d Bit, %d Seiten; Lesen aus dem Bildspeicher %.0f µs je 16 KB)",
                    s_bufW, s_bufH, v.fmt.bpp, s_chainLength, us);
                return;
            }

            // Hauptmenü: so groß wie möglich. Liegt der Faktor knapp über einer ganzen Zahl, lieber die ganze Zahl -
            // dann werden alle Pixel gleich groß und bleiben scharf.
            double scale = std::min(s_bufW / (double)kMenuW, s_bufH / (double)kMenuH);
            if (scale - std::floor(scale) < 0.1)
                scale = std::floor(scale);
            s_menuOutW = std::min(s_bufW, (int)(kMenuW * scale + 0.5));
            s_menuOutH = std::min(s_bufH, (int)(kMenuH * scale + 0.5));
            s_menuLeft = (s_bufW - s_menuOutW) / 2;
            s_menuTop = (s_bufH - s_menuOutH) / 2;
            s_menuSmooth = v.fmt.Supported() && scale != std::floor(scale);
            MenuAxis(s_menuOutW, kMenuW, s_menuSmooth, s_menuX, s_menuXw);
            MenuAxis(s_menuOutH, kMenuH, s_menuSmooth, s_menuY, s_menuYw);
            s_menuRow.assign((size_t)s_menuOutW * 4, 0);
            s_menuLast.assign((size_t)kMenuW * kMenuH * s_bufBytes, 0);
            s_menuRgb.assign(kMenuW + 1, 0);
            s_menuA.assign(s_menuOutW, 0);
            s_menuB.assign(s_menuOutW, 0);
            s_menuMix.assign(s_menuOutW, 0);
            s_menuLockTick = GetTickCount();
            LOG("Hauptmenü wird vergrößert: %dx%d -> %dx%d auf %dx%d (%d Bit%s)", kMenuW, kMenuH, s_menuOutW, s_menuOutH, s_bufW, s_bufH,
                v.fmt.bpp, s_menuSmooth ? ", Kanten geglättet" : "");
        }

        // Fertiges Bild aus dem Puffer in den Bildspeicher einer Seite
        bool Upload(const Page& from, IDirectDrawSurface* to)
        {
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            DDSURFACEDESC sd = {};
            sd.dwSize = sizeof(sd);
            Own own;
            if (FAILED(to->Lock(nullptr, &sd, DDLOCK_WAIT, nullptr)))
                return false;
            const bool fits = sd.lpSurface && (int)sd.dwWidth == s_bufW && (int)sd.dwHeight == s_bufH && sd.lPitch > 0;
            if (fits)
            {
                uint8_t* dst = static_cast<uint8_t*>(sd.lpSurface);
                if (sd.lPitch == s_bufPitch)
                    memcpy(dst, from.bits, (size_t)s_bufPitch * s_bufH);
                else
                    for (int y = 0; y < s_bufH; ++y)
                        memcpy(dst + (size_t)y * sd.lPitch, from.bits + (size_t)y * s_bufPitch, (size_t)s_bufW * s_bufBytes);
            }
            to->Unlock(nullptr);
            QueryPerformanceCounter(&t1);
            s_uploadTicks += t1.QuadPart - t0.QuadPart;
            return fits;
        }

        // Seite, über die ein Bild der Primary auf den Schirm geht: in deren eigenen Bildspeicher schreiben hieße,
        // dass es schon während des Kopierens zu sehen ist
        IDirectDrawSurface* StageFor(IDirectDrawSurface* primary)
        {
            if (s_stage && s_stage != primary)
                return s_stage;
            for (int i = 0; i < s_pageCount; ++i)
                if (s_pages[i].surf != primary)
                    return s_pages[i].surf;
            return nullptr;
        }

        // Steckt das Bild der Zielseite im Puffer, zeichnet das Overlay dort und lädt das Ergebnis hoch. "target"
        // ist danach die Seite, die statt der gewünschten zu zeigen ist.
        bool PresentBuffered(IDirectDrawSurface* primary, LPDIRECTDRAWSURFACE& target)
        {
            if (s_buffering != Buffering::Game)
                return false;
            HookDrawing(primary);

            IDirectDrawSurface* drawn = target;
            if (!drawn)
            {
                DDSCAPS caps = { DDSCAPS_BACKBUFFER };
                if (FAILED(primary->GetAttachedSurface(&caps, &drawn)) || !drawn)
                    return false;
                drawn->Release(); // gehört zur Kette der Primary und bleibt mit ihr gültig
            }
            Page* p = FindPage(drawn);
            if (!p || !p->fresh)
                return false;

            SurfaceView v = BufferView(*p);
            overlay::OnFrame(v);

            IDirectDrawSurface* via = drawn;
            if (drawn == primary)
            {
                if (IDirectDrawSurface* stage = StageFor(primary))
                    via = target = stage;
            }
            else
                s_stage = drawn;
            Upload(*p, via);
            s_shown = p;
            return true;
        }

        // Rückfallebene für Bilder, die das Spiel ohne erneutes Sperren zeichnet: aus der Nachrichtenschleife zeigen
        void PresentMenu()
        {
            Page* p = FindPage(s_primary);
            const DWORD now = GetTickCount();
            if (!p || !p->fresh || now - s_menuLockTick < 250 || now - s_menuTick < 15)
                return;
            s_menuTick = now;
            if (!MenuNeedsDrawing(*p))
                return;

            DDSURFACEDESC sd = {};
            sd.dwSize = sizeof(sd);
            Own own;
            if (FAILED(s_primary->Lock(nullptr, &sd, DDLOCK_WAIT, nullptr)))
                return;
            if (sd.lpSurface && (int)sd.dwWidth == s_bufW && (int)sd.dwHeight == s_bufH && sd.lPitch >= s_bufW * s_bufBytes)
                DrawMenu(*p, static_cast<uint8_t*>(sd.lpSurface), sd.lPitch);
            s_primary->Unlock(nullptr);
        }

        void RenderInto(IDirectDrawSurface* primary, IDirectDrawSurface* target)
        {
            IDirectDrawSurface* back = target;
            if (back)
            {
                back->AddRef();
            }
            else
            {
                DDSCAPS caps = { DDSCAPS_BACKBUFFER };
                if (FAILED(primary->GetAttachedSurface(&caps, &back)) || !back)
                    return;
            }

            SurfaceView v;
            if (LockToView(back, v))
            {
                overlay::OnFrame(v);
                back->Unlock(nullptr);
            }
            back->Release();
        }

        HRESULT STDMETHODCALLTYPE Hook_Flip(IDirectDrawSurface* self, LPDIRECTDRAWSURFACE target, DWORD flags)
        {
            if (!s_flips)
                LOG("Erster Flip (Surface %p, Ziel %p, Flags 0x%X)", self, target, flags);
            if (s_primary != self)
            {
                DropBuffers(); // Primary, deren Anlegen wir nicht gesehen haben
                s_primary = self;
            }
            s_gameThread = GetCurrentThreadId();
            s_lastFlip = GetTickCount();
            ++s_flips;

            // Festhalten, wie lange das Overlay je Bild braucht (zweimal nach dem Start, mit der Debug-Anzeige alle
            // 500 Bilder): bei hohen Auflösungen oder langsamem Bildspeicher entscheidet das über die Bildrate
            static LONGLONG s_spent = 0, s_longest = 0, s_since = 0;
            static unsigned s_frames = 0;
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
            const bool buffered = PresentBuffered(self, target);
            if (!buffered)
                RenderInto(self, target);
            QueryPerformanceCounter(&t1);
            s_spent += t1.QuadPart - t0.QuadPart;
            s_longest = (std::max)(s_longest, t1.QuadPart - t0.QuadPart);
            if (++s_frames == 500)
            {
                LARGE_INTEGER f;
                QueryPerformanceFrequency(&f);
                static int s_logged = 0;
                if (s_since && (g_cfg.showInfo || s_logged++ < 2))
                {
                    char upload[64] = "";
                    if (s_uploadTicks)
                        sprintf_s(upload, ", davon %.2f ms Übertragen aus dem Zwischenpuffer", s_uploadTicks * 1000.0 / f.QuadPart / 500);
                    LOG("Bildrate %.1f/s, Overlay %.2f ms je Bild (längstes %.1f ms)%s", 500.0 * f.QuadPart / (t1.QuadPart - s_since),
                        s_spent * 1000.0 / f.QuadPart / 500, s_longest * 1000.0 / f.QuadPart, upload);
                }
                s_spent = s_longest = s_uploadTicks = 0;
                s_frames = 0;
                s_since = t1.QuadPart;
            }
            return s_origFlip(self, target, flags);
        }

        HRESULT STDMETHODCALLTYPE Hook_CreateSurface(IDirectDraw* dd, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE* out, IUnknown* outer)
        {
            const HRESULT hr = s_origCreateSurface(dd, desc, out, outer);
            if (SUCCEEDED(hr) && desc && out && *out && (desc->dwFlags & DDSD_CAPS) && (desc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE))
            {
                s_primary = *out;
                s_chainLength = (desc->dwFlags & DDSD_BACKBUFFERCOUNT) ? (int)desc->dwBackBufferCount + 1 : 1;

                void* prev = hooks::PatchVTable(*out, kVtFlip, reinterpret_cast<void*>(&Hook_Flip));
                if (prev != reinterpret_cast<void*>(&Hook_Flip))
                    s_origFlip = reinterpret_cast<Flip_t>(prev);

                DDSURFACEDESC sd = {};
                sd.dwSize = sizeof(sd);
                (*out)->GetSurfaceDesc(&sd);
                LOG("Primary-Surface %lux%lu, %lu Bit, Caps 0x%X, %d Puffer", sd.dwWidth, sd.dwHeight,
                    sd.ddpfPixelFormat.dwRGBBitCount, desc->ddsCaps.dwCaps, s_chainLength);
                SetUpBuffering(*out);
                if (s_chainLength < 2 && s_buffering != Buffering::Menu && sd.ddpfPixelFormat.dwRGBBitCount > 8)
                    LOG("Warnung: keine Backbuffer - ohne Flip kann das Overlay nicht zeichnen");
            }
            return hr;
        }

        void HookCreateSurface(IDirectDraw* dd)
        {
            void* prev = hooks::PatchVTable(dd, kVtCreateSurface, reinterpret_cast<void*>(&Hook_CreateSurface));
            if (prev != reinterpret_cast<void*>(&Hook_CreateSurface))
                s_origCreateSurface = reinterpret_cast<CreateSurface_t>(prev);
        }

        HRESULT WINAPI Hook_DirectDrawCreate(GUID* guid, LPDIRECTDRAW* out, IUnknown* outer)
        {
            const HRESULT hr = s_realDirectDrawCreate(guid, out, outer);
            LOG("DirectDrawCreate -> 0x%08X", (unsigned)hr);
            if (SUCCEEDED(hr) && out && *out)
                HookCreateSurface(*out);
            return hr;
        }

        // Aufruf über die Zelle des Spiels: dort steht, was es sich mit GetProcAddress geholt hat
        HRESULT WINAPI Hook_DirectDrawCreateAtCall(GUID* guid, LPDIRECTDRAW* out, IUnknown* outer)
        {
            const DirectDrawCreate_t real = *s_createCell;
            if (real != &Hook_DirectDrawCreate)
                s_realDirectDrawCreate = real;
            return Hook_DirectDrawCreate(guid, out, outer);
        }

        // MGL (die Grafikbibliothek in GTA.exe) lädt ddraw.dll dynamisch und holt sich DirectDrawCreate hier
        FARPROC Intercept(HMODULE module, LPCSTR name, FARPROC p)
        {
            if (p && !IS_INTRESOURCE(name) && strcmp(name, "DirectDrawCreate") == 0)
            {
                char path[MAX_PATH] = {};
                GetModuleFileNameA(module, path, MAX_PATH);
                LOG("Spiel holt DirectDrawCreate aus %s", path);
                s_realDirectDrawCreate = reinterpret_cast<DirectDrawCreate_t>(p);
                return reinterpret_cast<FARPROC>(&Hook_DirectDrawCreate);
            }
            return p;
        }

        FARPROC WINAPI Hook_GetProcAddress(HMODULE module, LPCSTR name)
        {
            return Intercept(module, name, s_origGetProcAddress(module, name));
        }

        // Ruft den aktuellen IAT-Eintrag auf – der kann inzwischen ein Hook von cnc-ddraw sein
        FARPROC WINAPI Hook_GetProcAddressAtLoad(HMODULE module, LPCSTR name)
        {
            GetProcAddress_t next = *s_iatGetProcAddress;
            if (next == &Hook_GetProcAddress || next == &Hook_GetProcAddressAtLoad)
                next = s_origGetProcAddress;
            return Intercept(module, name, next(module, name));
        }

        // Gehört die Primary, die wir kennen, noch dem Spiel? MGL gibt sie beim Wechsel der Auflösung frei.
        bool PrimaryAlive() { return s_primary && s_primary == gta1::PrimarySurface(); }
    }

    bool InstallAtLoads(uintptr_t iatSlot, const uintptr_t* sites, size_t count)
    {
        s_iatGetProcAddress = reinterpret_cast<GetProcAddress_t*>(iatSlot);
        s_origGetProcAddress = *s_iatGetProcAddress;
        s_loadCell = reinterpret_cast<void*>(&Hook_GetProcAddressAtLoad);
        size_t ok = 0;
        for (size_t i = 0; i < count; ++i)
            ok += hooks::RedirectLoad(sites[i], iatSlot, &s_loadCell);
        LOG("GetProcAddress: %u von %u Ladestellen umgebogen", (unsigned)ok, (unsigned)count);
        return ok == count;
    }

    bool InstallAtCall(uintptr_t site, uintptr_t cell)
    {
        s_createCell = reinterpret_cast<DirectDrawCreate_t*>(cell);
        s_createCallCell = reinterpret_cast<void*>(&Hook_DirectDrawCreateAtCall);
        return hooks::RedirectCallCell(site, cell, &s_createCallCell);
    }

    void EnsureFlipHook(void* primarySurface, int chainLength)
    {
        if (!primarySurface)
            return;
        auto surf = static_cast<IDirectDrawSurface*>(primarySurface);
        void** vtable = *reinterpret_cast<void***>(surf);
        if (vtable[kVtFlip] != reinterpret_cast<void*>(&Hook_Flip))
        {
            s_origFlip = reinterpret_cast<Flip_t>(hooks::PatchVTable(surf, kVtFlip, reinterpret_cast<void*>(&Hook_Flip)));
            LOG("Flip nachträglich an Primary-Surface %p gehängt (%d Seiten)", surf, chainLength);
        }
        if (s_primary != surf)
        {
            DropBuffers();
            s_primary = surf;
            s_chainLength = chainLength;
        }
    }

    bool Install(HMODULE exe)
    {
        s_origGetProcAddress = reinterpret_cast<GetProcAddress_t>(
            hooks::PatchImport(exe, "KERNEL32.dll", "GetProcAddress", reinterpret_cast<void*>(&Hook_GetProcAddress)));
        if (!s_origGetProcAddress)
        {
            LOG("GetProcAddress-Import nicht gefunden - DirectDraw kann nicht abgefangen werden");
            return false;
        }
        return true;
    }

    void OnPump()
    {
        // Auch der Hook auf CreateSurface kann überschrieben worden sein
        if (s_origCreateSurface)
            if (auto dd = static_cast<IDirectDraw*>(gta1::DirectDrawObject()))
                HookCreateSurface(dd);

        if (s_buffering == Buffering::Menu && GetCurrentThreadId() == s_drawThread && PrimaryAlive())
        {
            HookDrawing(s_primary);
            PresentMenu();
        }
    }

    bool PresentOverlay()
    {
        if (!s_primary || !s_origFlip)
            return false;
        if (s_buffering == Buffering::Game && s_shown)
        {
            // Das zuletzt gezeigte Bild liegt im Puffer: dort zeichnen und über eine verdeckte Seite zeigen
            if (!PrimaryAlive())
                return false;
            SurfaceView v = BufferView(*s_shown);
            overlay::OnFrame(v);
            IDirectDrawSurface* via = s_shown->surf != s_primary ? s_shown->surf : StageFor(s_primary);
            if (!via || !Upload(*s_shown, via))
                return false;
            return SUCCEEDED(s_origFlip(s_primary, via, DDFLIP_WAIT));
        }
        if (s_chainLength < 2)
            return false;
        // Einmal komplett durch die Flip-Kette, damit MGLs Seitenverwaltung danach wieder stimmt
        for (int i = 0; i < s_chainLength; ++i)
        {
            RenderInto(s_primary, nullptr);
            if (FAILED(s_origFlip(s_primary, nullptr, DDFLIP_WAIT)))
                return false;
        }
        return true;
    }

    bool WithFrontBuffer(void (*fn)(SurfaceView& view))
    {
        if (!s_primary)
            return false;
        if (s_buffering == Buffering::Game && s_shown)
        {
            SurfaceView v = BufferView(*s_shown);
            fn(v);
            return true;
        }
        SurfaceView v;
        if (!LockToView(s_primary, v))
            return false;
        fn(v);
        s_primary->Unlock(nullptr);
        return true;
    }

    DWORD LastGameFlipTick() { return s_lastFlip; }
    unsigned GameFlipCount() { return s_flips; }
    DWORD GameThreadId() { return s_gameThread; }
    void NoteGameThread() { s_gameThread = GetCurrentThreadId(); }
}

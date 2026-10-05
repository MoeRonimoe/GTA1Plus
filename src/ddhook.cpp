#include "ddhook.h"
#include "config.h"
#include "hooks.h"
#include "log.h"
#include "overlay.h"

#include <ddraw.h>
#include <algorithm>
#include <cstring>

namespace ddhook
{
    namespace
    {
        using GetProcAddress_t   = FARPROC(WINAPI*)(HMODULE, LPCSTR);
        using DirectDrawCreate_t = HRESULT(WINAPI*)(GUID*, LPDIRECTDRAW*, IUnknown*);
        using CreateSurface_t    = HRESULT(STDMETHODCALLTYPE*)(IDirectDraw*, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE*, IUnknown*);
        using Flip_t             = HRESULT(STDMETHODCALLTYPE*)(IDirectDrawSurface*, LPDIRECTDRAWSURFACE, DWORD);

        constexpr int kVtCreateSurface = 6;  // IDirectDraw::CreateSurface
        constexpr int kVtFlip          = 11; // IDirectDrawSurface::Flip

        GetProcAddress_t   s_origGetProcAddress = nullptr;
        GetProcAddress_t*  s_iatGetProcAddress = nullptr; // IAT-Eintrag, den wir zur Laufzeit weiterrufen
        void*              s_loadCell = nullptr;          // Zelle, aus der MGL jetzt GetProcAddress lädt
        DirectDrawCreate_t s_realDirectDrawCreate = nullptr;
        CreateSurface_t    s_origCreateSurface = nullptr;
        Flip_t             s_origFlip = nullptr;

        IDirectDrawSurface* s_primary = nullptr;
        int      s_chainLength = 2;
        DWORD    s_lastFlip = 0;
        DWORD    s_gameThread = 0;
        unsigned s_flips = 0;

        bool LockToView(IDirectDrawSurface* surf, SurfaceView& v)
        {
            DDSURFACEDESC sd = {};
            sd.dwSize = sizeof(sd);
            if (FAILED(surf->Lock(nullptr, &sd, DDLOCK_WAIT, nullptr)))
                return false;

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
            s_primary = self;
            s_gameThread = GetCurrentThreadId();
            s_lastFlip = GetTickCount();
            ++s_flips;

            // Festhalten, wie lange das Overlay je Bild braucht (zweimal nach dem Start, mit der Debug-Anzeige alle
            // 500 Bilder): bei hohen Auflösungen oder langsamem Bildspeicher entscheidet das über die Bildrate
            static LONGLONG s_spent = 0, s_longest = 0, s_since = 0;
            static unsigned s_frames = 0;
            LARGE_INTEGER t0, t1;
            QueryPerformanceCounter(&t0);
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
                    LOG("Bildrate %.1f/s, Overlay %.2f ms je Bild (längstes %.1f ms)", 500.0 * f.QuadPart / (t1.QuadPart - s_since),
                        s_spent * 1000.0 / f.QuadPart / 500, s_longest * 1000.0 / f.QuadPart);
                s_spent = s_longest = 0;
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
                if (s_chainLength < 2)
                    LOG("Warnung: keine Backbuffer - ohne Flip kann das Overlay nicht zeichnen");
            }
            return hr;
        }

        HRESULT WINAPI Hook_DirectDrawCreate(GUID* guid, LPDIRECTDRAW* out, IUnknown* outer)
        {
            const HRESULT hr = s_realDirectDrawCreate(guid, out, outer);
            LOG("DirectDrawCreate -> 0x%08X", (unsigned)hr);
            if (SUCCEEDED(hr) && out && *out)
            {
                void* prev = hooks::PatchVTable(*out, kVtCreateSurface, reinterpret_cast<void*>(&Hook_CreateSurface));
                if (prev != reinterpret_cast<void*>(&Hook_CreateSurface))
                    s_origCreateSurface = reinterpret_cast<CreateSurface_t>(prev);
            }
            return hr;
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

    bool PresentOverlay()
    {
        if (!s_primary || !s_origFlip || s_chainLength < 2)
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

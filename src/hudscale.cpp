#include "hudscale.h"
#include "config.h"
#include "ddhook.h"
#include "gta1.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hudscale
{
    namespace
    {
        // Das Spiel zeichnet sein HUD in ein eigenes, um den Faktor kleineres Bild. Darin liegt vorher eine
        // verkleinerte Kopie des fertigen Weltbilds: halb durchsichtige Teile (z.B. der Missionspfeil) mischen
        // sich so mit dem, was wirklich hinter ihnen ist. Jedes Pixel, das das HUD verändert hat, kommt danach
        // vergrößert ins eigentliche Bild. Ganze Faktoren: aus jedem Pixel wird ein Quadrat, die Grafik bleibt scharf.
        uint8_t* s_layer = nullptr;      // Bild, in das das Spiel zeichnet (Zeilenlänge wie seine eigene Seite)
        int      s_layerPitch = 0, s_layerRows = 0, s_layerWidth = 0, s_layerBytes = 0;
        std::vector<uint8_t> s_before;   // dasselbe Bild vor dem HUD, Zeilen dicht an dicht
        std::vector<uint8_t> s_wide;     // ein verbreiterter Lauf geänderter Pixel
        std::vector<int>     s_column;   // Byte-Abstand der Quellspalte je Spalte des kleinen Bilds
        int      s_factor = 1;
        int      s_height = 0;           // Bildhöhe des Spiels, zuletzt gesehen

        int AutoFor(int height)
        {
            // So groß wie auf dem Bild, für das das HUD gemacht ist (640x480): nach dem Vergrößern soll es wieder
            // etwa ein Bild von 450 bis 540 Zeilen füllen. Doppelt ab 810 Zeilen, dreifach ab 1350, vierfach ab 1890.
            return std::clamp((height + 270) / 540, 1, 4);
        }

        bool EnsureLayer(const gta1::DrawBuffer& real, int width, int rows, int factor)
        {
            const int bytes = real.bytesPerPixel;
            if (s_layer && s_layerPitch == real.pitch && s_layerRows == rows && s_layerWidth == width && s_layerBytes == bytes)
                return true;
            if (s_layer)
                VirtualFree(s_layer, 0, MEM_RELEASE);
            // Die Zeichenroutinen kennen nur die Zeilen des kleinen Bilds; zwei Zeilen Reserve schaden nicht
            s_layer = static_cast<uint8_t*>(VirtualAlloc(nullptr, (size_t)real.pitch * (rows + 2), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            s_layerPitch = real.pitch;
            s_layerRows = rows;
            s_layerWidth = width;
            s_layerBytes = bytes;
            if (!s_layer)
            {
                LOG("HUD-Größe: kein Speicher für das HUD-Bild (%d x %d)", width, rows);
                s_layerRows = 0;
                return false;
            }
            s_before.resize((size_t)width * rows * bytes);
            s_wide.resize((size_t)width * factor * bytes);
            // Aus jedem Block von factor x factor Pixeln steht das mittlere für den ganzen Block
            s_column.resize(width);
            for (int x = 0; x < width; ++x)
                s_column[x] = std::min(real.width - 1, x * factor + factor / 2) * bytes;
            return true;
        }

        // Verkleinerte Kopie des Weltbilds ins HUD-Bild legen und für den Vergleich danach festhalten
        void CopyWorld(const gta1::DrawBuffer& real, int width, int rows, int factor)
        {
            const int bytes = real.bytesPerPixel;
            const size_t rowBytes = (size_t)width * bytes;
            for (int y = 0; y < rows; ++y)
            {
                const uint8_t* src = real.bits + (size_t)std::min(real.height - 1, y * factor + factor / 2) * real.pitch;
                uint8_t* before = &s_before[y * rowBytes];
                if (bytes == 4)
                {
                    uint32_t* out = reinterpret_cast<uint32_t*>(before);
                    for (int x = 0; x < width; ++x)
                        memcpy(&out[x], src + s_column[x], 4);
                }
                else
                {
                    for (int x = 0; x < width; ++x)
                        memcpy(before + (size_t)x * bytes, src + s_column[x], bytes);
                }
                memcpy(s_layer + (size_t)y * s_layerPitch, before, rowBytes);
            }
        }

        // Alles, was das HUD im kleinen Bild verändert hat, vergrößert ins Bild des Spiels schreiben
        void Composite(const gta1::DrawBuffer& real, int width, int rows, int factor)
        {
            const int bytes = real.bytesPerPixel;
            const size_t rowBytes = (size_t)width * bytes;
            for (int y = 0; y < rows; ++y)
            {
                const uint8_t* src = s_layer + (size_t)y * s_layerPitch;
                const uint8_t* before = &s_before[y * rowBytes];
                if (memcmp(src, before, rowBytes) == 0)
                    continue;

                auto same = [&](int x) {
                    if (bytes == 4)
                        return reinterpret_cast<const uint32_t*>(src)[x] == reinterpret_cast<const uint32_t*>(before)[x];
                    return memcmp(src + (size_t)x * bytes, before + (size_t)x * bytes, bytes) == 0;
                };
                int x = 0;
                while (x < width)
                {
                    while (x < width && same(x))
                        ++x;
                    const int from = x;
                    while (x < width && !same(x))
                        ++x;
                    const int outX = from * factor;
                    const int count = std::min((x - from) * factor, real.width - outX);
                    if (x == from || count <= 0)
                        continue;

                    uint8_t* wide = s_wide.data();
                    if (bytes == 4)
                    {
                        const uint32_t* in = reinterpret_cast<const uint32_t*>(src) + from;
                        uint32_t* out = reinterpret_cast<uint32_t*>(wide);
                        for (int i = 0; i < x - from; ++i)
                            for (int k = 0; k < factor; ++k)
                                *out++ = in[i];
                    }
                    else
                    {
                        for (int i = 0; i < x - from; ++i)
                            for (int k = 0; k < factor; ++k)
                                memcpy(wide + ((size_t)i * factor + k) * bytes, src + (size_t)(from + i) * bytes, bytes);
                    }
                    for (int k = 0; k < factor; ++k)
                    {
                        const int outY = y * factor + k;
                        if (outY >= real.height)
                            break;
                        memcpy(real.bits + (size_t)outY * real.pitch + (size_t)outX * bytes, wide, (size_t)count * bytes);
                    }
                }
            }
        }

        void Draw(void (*drawHud)())
        {
            s_factor = 1;
            gta1::DrawBuffer real;
            if (!gta1::GetDrawBuffer(real))
            {
                drawHud();
                return;
            }
            s_height = real.height;
            const int factor = g_cfg.hudScale > 0 ? g_cfg.hudScale : AutoFor(real.height);
            if (factor < 2 || real.bytesPerPixel < 2 || real.bytesPerPixel > 4)
            {
                drawHud();
                return;
            }
            // Die Kopie des Weltbilds liest aus dem Bild des Spiels; liegt es im Grafikspeicher, dauert das zu lange
            if (!ddhook::FastToRead(real.bits))
            {
                static bool s_told = false;
                if (!s_told)
                    LOG("HUD-Größe: der Bildspeicher ist zu langsam zu lesen - braucht den Zwischenpuffer ([Display] FrameBuffer=1)");
                s_told = true;
                drawHud();
                return;
            }
            const int width = (real.width + factor - 1) / factor, rows = (real.height + factor - 1) / factor;
            if (!EnsureLayer(real, width, rows, factor))
            {
                drawHud();
                return;
            }

            LARGE_INTEGER t0, t1, t2, t3;
            QueryPerformanceCounter(&t0);
            CopyWorld(real, width, rows, factor);
            const bool redirected = gta1::BeginHudLayer(s_layer, width, rows, factor);
            QueryPerformanceCounter(&t1);
            drawHud();
            if (!redirected)
                return;
            QueryPerformanceCounter(&t2);
            gta1::EndHudLayer();
            Composite(real, width, rows, factor);
            QueryPerformanceCounter(&t3);
            s_factor = factor;

            // Aufwand festhalten: einmal nach dem Start, mit der Debug-Anzeige laufend
            static LONGLONG s_spent = 0;
            static int s_frames = 0, s_logged = 0;
            s_spent += (t1.QuadPart - t0.QuadPart) + (t3.QuadPart - t2.QuadPart);
            if (++s_frames == 500)
            {
                LARGE_INTEGER f;
                QueryPerformanceFrequency(&f);
                if (g_cfg.showInfo || s_logged++ < 1)
                    LOG("HUD-Größe: Faktor %d, %.2f ms je Bild", factor, s_spent * 1000.0 / f.QuadPart / 500);
                s_spent = 0;
                s_frames = 0;
            }
        }
    }

    void Init()
    {
        if (!gta1::CanRedirectHud())
            return;
        gta1::SetHudDrawer(&Draw);
        LOG("HUD-Größe: einstellbar (%s)", g_cfg.hudScale > 0 ? "fester Faktor" : "automatisch nach Bildhöhe");
    }

    bool Available() { return gta1::CanRedirectHud(); }
    int Factor() { return s_factor; }
    int AutoFactor() { return s_height > 0 ? AutoFor(s_height) : 1; }
}

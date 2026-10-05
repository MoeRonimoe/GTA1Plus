#include "tiltcam.h"
#include "config.h"
#include "ddhook.h"
#include "gta1.h"
#include "log.h"
#include "parallel.h"

#include <windows.h>
#include <emmintrin.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace tiltcam
{
    namespace
    {
        constexpr float kPi = 3.14159265f;
        constexpr float kBlendTime = 0.6f;     // Sekunden für das Ein- und Ausblenden
        constexpr float kTurnRate = 2.5f;      // wie schnell das Bild der Fahrtrichtung folgt (1/s)
        constexpr float kFocal = 2.0f;         // Brennweite der gedachten Kamera in Bildhöhen (größer = flacher)
        constexpr float kPivotLow = 0.17f;     // so weit rückt die Figur gekippt unter die Bildmitte (Bildhöhen)
        constexpr float kMaxShrink = 3.5f;
        // Größter Bereich, den das Spiel vollständig zeichnet: bei schneller Fahrt nutzt es selbst eine halbe Breite
        // von etwa 1160 Weltpixeln (ViewRect: (Höhe + 384) * 160 / zoom, zoom = focal * 320 / 800)
        constexpr float kMaxHalfWidth = 1200.0f;

        // Abbildung eines Bildes: Ausgabepunkt -> Bodenpunkt relativ zur Figur (Pixel des ungekippten Spielbilds)
        struct View
        {
            bool active = false;
            float rot = 0;             // Drehung im Uhrzeigersinn
            float tilt = 0;            // Neigung
            float focal = 1;           // Pixel
            float pivotX = 0, pivotY = 0;
            float squash = 1;          // senkrechter Bildfaktor des Spiels
            int width = 0, height = 0;
            float shrink = 1;          // so viel kleiner hat das Spiel dieses Bild gezeichnet
            float scale = 1;           // Bodenpixel je Ausgabepixel an der Figur (< 1: gegenüber dem Spiel vergrößert)
        };

        View g_next;                   // für das Bild, das gerade gezeichnet wird
        View g_shown;                  // zuletzt umgerechnet, also auf dem Bildschirm
        float g_ramp = 0;              // 0..1 linear
        float g_rot = 0;               // geglättete Fahrtrichtung
        LARGE_INTEGER g_last = {};
        std::vector<uint32_t> g_src;
        bool g_warnedFormat = false, g_loggedStart = false;

        float WrapAngle(float a)
        {
            while (a > kPi)
                a -= 2 * kPi;
            while (a < -kPi)
                a += 2 * kPi;
            return a;
        }

        // Richtung aus dem Spiel -> Bildschirmwinkel (0 = oben, im Uhrzeigersinn), wie der Spielerpfeil der Karte
        float HeadingRadians(int angle)
        {
            const float a = ((angle + 512 + g_cfg.headingOffset) & 1023) * (2 * kPi / 1024.0f);
            return g_cfg.headingCcw ? -a : a;
        }

        bool ToGround(const View& v, float ox, float oy, float& gx, float& gy)
        {
            const float c = std::cos(v.tilt), s = std::sin(v.tilt);
            const float y = (oy - v.pivotY) / (v.focal * v.squash);
            const float d = c + y * s;
            if (d < 0.05f)
                return false; // über dem Horizont
            const float t = v.focal * c / d;
            const float lx = t / v.focal * (ox - v.pivotX);
            const float ly = t * (y * c - s) + v.focal * s;
            const float cr = std::cos(v.rot), sr = std::sin(v.rot);
            gx = (cr * lx - sr * ly) * v.scale;
            gy = (sr * lx + cr * ly) * v.squash * v.scale;
            return true;
        }

        bool FromGround(const View& v, float gx, float gy, float& ox, float& oy)
        {
            const float cr = std::cos(v.rot), sr = std::sin(v.rot);
            gx /= v.scale;
            const float yy = gy / (v.squash * v.scale);
            const float lx = cr * gx + sr * yy;
            const float ly = -sr * gx + cr * yy;
            const float c = std::cos(v.tilt), s = std::sin(v.tilt);
            const float h = v.focal * c;
            const float Y = ly - v.focal * s;
            const float den = h * c - Y * s;
            if (den <= 1e-3f)
                return false;
            const float y = (h * s + Y * c) / den;
            const float t = h / (c + y * s);
            ox = v.pivotX + lx / t * v.focal;
            oy = v.pivotY + y * v.focal * v.squash;
            return true;
        }
    }

    void BeginFrame()
    {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        const float dt = g_last.QuadPart ? std::clamp((now.QuadPart - g_last.QuadPart) / (float)freq.QuadPart, 0.0f, 0.1f) : 0.0f;
        g_last = now;
        g_next.active = false;

        gta1::CameraView cam;
        gta1::DrawBuffer buf;
        if (!g_cfg.tilt || !gta1::TiltCameraAvailable() || !gta1::GetCameraView(cam) || !gta1::GetDrawBuffer(buf) ||
            !ddhook::FastToRead(buf.bits))
        {
            g_ramp = 0;
            g_rot = 0;
            return;
        }
        const bool vehicle = cam.ctrlType == 0;
        const bool want = g_cfg.tilt == 2 || vehicle;
        g_ramp = std::clamp(g_ramp + (want ? dt : -dt) / kBlendTime, 0.0f, 1.0f);
        const float blend = g_ramp * g_ramp * (3 - 2 * g_ramp);

        // Im Fahrzeug die Fahrtrichtung nach oben drehen, zu Fuß zurück nach Norden
        const float target = want && vehicle && g_cfg.tiltRotate ? HeadingRadians(cam.angle) : 0.0f;
        g_rot = WrapAngle(g_rot + WrapAngle(target - g_rot) * std::min(1.0f, dt * kTurnRate));
        if (blend <= 0.001f)
        {
            g_rot = target == 0 ? 0 : g_rot;
            return;
        }

        View v;
        v.width = buf.width;
        v.height = buf.height;
        v.rot = g_rot * blend;
        v.tilt = g_cfg.tiltAngle * blend * (kPi / 180.0f);
        v.focal = buf.height * kFocal;
        v.pivotX = buf.width * 0.5f;
        v.pivotY = buf.height * (0.5f + kPivotLow * blend);
        v.squash = cam.squash;

        // Bildmitte: vom Spiel (mit seiner Vorausschau) zur Mitte des gebrauchten Bereichs überblenden
        const float depth = cam.height + cam.playerZ;
        const float ppw = cam.focal / depth; // Bildpixel je Weltpixel in Höhe der Figur
        const float gameX = (cam.x - cam.playerX) * ppw, gameY = (cam.y - cam.playerY) * ppw * cam.squash;
        float need = 1, dx = 0, dy = 0;
        // Welcher Bereich des ungekippten Bildes (relativ zur Figur) wird gebraucht? Rand der Ausgabe abtasten;
        // need = nötige Verkleinerung, damit er um die Mitte (dx, dy) ganz ins Bild passt
        auto measure = [&]() -> bool
        {
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
            constexpr int kSteps = 12;
            for (int i = 0; i <= kSteps; ++i)
            {
                const float fx = (v.width - 1) * i / (float)kSteps, fy = (v.height - 1) * i / (float)kSteps;
                const float pts[4][2] = { { fx, 0 }, { fx, (float)v.height - 1 }, { 0, fy }, { (float)v.width - 1, fy } };
                for (const auto& p : pts)
                {
                    float gx, gy;
                    if (!ToGround(v, p[0], p[1], gx, gy))
                        continue;
                    minX = std::min(minX, gx);
                    maxX = std::max(maxX, gx);
                    minY = std::min(minY, gy);
                    maxY = std::max(maxY, gy);
                }
            }
            if (minX > maxX)
                return false;
            dx = gameX + ((minX + maxX) * 0.5f - gameX) * blend;
            dy = gameY + ((minY + maxY) * 0.5f - gameY) * blend;
            const float needX = std::max(std::abs(minX - dx), std::abs(maxX - dx)) / (v.width * 0.5f - 2);
            const float needY = std::max(std::abs(minY - dy), std::abs(maxY - dy)) / (v.height * 0.5f - 2);
            need = std::max(needX, needY) * 1.01f;
            return true;
        };
        if (!measure())
            return;
        // Einen größeren Bereich zeichnet das Spiel nicht vollständig (z.B. mit weit herausgezoomter Fahrzeugkamera).
        // Dann zeigt die gekippte Ansicht einen kleineren Ausschnitt statt leerer Ränder.
        const float halfWidth = (cam.height + 384) * 160 / std::max(1.0f, cam.zoom);
        const float maxShrink = std::clamp(kMaxHalfWidth / halfWidth, 1.0f, kMaxShrink);
        for (int i = 0; i < 3 && need > maxShrink; ++i)
        {
            v.scale *= maxShrink / need;
            if (!measure())
                return;
        }
        const float shrink = std::clamp(need, 1.0f, maxShrink);

        float used = 1;
        if (!gta1::OverrideRenderCamera(cam.playerX + dx / ppw, cam.playerY + dy / (ppw * cam.squash), shrink, used))
            return;
        v.shrink = used;
        v.active = true;
        g_next = v;
        if (!g_loggedStart)
        {
            g_loggedStart = true;
            LOG("Kippkamera aktiv: %dx%d, %d Byte je Pixel, Verkleinerung %.2f", buf.width, buf.height, buf.bytesPerPixel, used);
        }
    }

    namespace
    {
        // Ein Bild umrechnen: erst das fertige Weltbild kopieren, dann jede Zeile der Ausgabe daraus holen. Beides
        // läuft zeilenweise auf mehreren Prozessorkernen.
        struct WarpJob
        {
            View v;
            gta1::DrawBuffer b;
            float psx, psy;            // Spielfigur im ungekippten Bild
        };

        void CopyRows(int first, int last, void* ctx)
        {
            const WarpJob& job = *static_cast<const WarpJob*>(ctx);
            const int W = job.b.width;
            for (int y = first; y < last; ++y)
                memcpy(&g_src[(size_t)y * W], job.b.bits + (size_t)y * job.b.pitch, (size_t)W * 4);
        }

        void WarpRows(int first, int last, void* ctx)
        {
            const WarpJob& job = *static_cast<const WarpJob*>(ctx);
            const View& v = job.v;
            const int W = job.b.width, H = job.b.height;
            const float c = std::cos(v.tilt), s = std::sin(v.tilt);
            const float cr = std::cos(v.rot), sr = std::sin(v.rot);
            const float inv = 1.0f / v.shrink;
            const int maxX = (W - 2) << 16, maxY = (H - 2) << 16;
            const __m128i zero = _mm_setzero_si128();
            for (int oy = first; oy < last; ++oy)
            {
                uint32_t* dst = reinterpret_cast<uint32_t*>(job.b.bits + (size_t)oy * job.b.pitch);
                const float y = (oy - v.pivotY) / (v.focal * v.squash);
                const float d = c + y * s;
                if (d < 0.05f)
                {
                    memset(dst, 0, (size_t)W * 4);
                    continue;
                }
                // In jeder Zeile hängt die Quelle linear von x ab
                const float t = v.focal * c / d;
                const float k = t / v.focal * v.scale;
                const float ly = (t * (y * c - s) + v.focal * s) * v.scale;
                const float lx0 = -k * v.pivotX;
                const float sx0 = job.psx + (cr * lx0 - sr * ly) * inv;
                const float sy0 = job.psy + (sr * lx0 + cr * ly) * v.squash * inv;
                int fx = (int)(sx0 * 65536.0f), fy = (int)(sy0 * 65536.0f);
                const int stepX = (int)(cr * k * inv * 65536.0f), stepY = (int)(sr * k * v.squash * inv * 65536.0f);
                for (int ox = 0; ox < W; ++ox, fx += stepX, fy += stepY)
                {
                    const int cx = std::clamp(fx, 0, maxX), cy = std::clamp(fy, 0, maxY);
                    const uint32_t* p = &g_src[(size_t)(cy >> 16) * W + (cx >> 16)];
                    // Vier Nachbarn mischen, alle Farbkanäle zugleich. Gewichte mit 7 Bit, damit die Produkte in
                    // 16 Bit mit Vorzeichen passen.
                    const __m128i wx = _mm_set1_epi16((short)((cx >> 9) & 0x7F)), wy = _mm_set1_epi16((short)((cy >> 9) & 0x7F));
                    const __m128i top = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p)), zero);
                    const __m128i bottom = _mm_unpacklo_epi8(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(p + W)), zero);
                    const __m128i row = _mm_add_epi16(top, _mm_srai_epi16(_mm_mullo_epi16(_mm_sub_epi16(bottom, top), wy), 7));
                    const __m128i right = _mm_srli_si128(row, 8);
                    const __m128i mix = _mm_add_epi16(row, _mm_srai_epi16(_mm_mullo_epi16(_mm_sub_epi16(right, row), wx), 7));
                    dst[ox] = (uint32_t)_mm_cvtsi128_si32(_mm_packus_epi16(mix, mix));
                }
            }
        }
    }

    void Warp()
    {
        WarpJob job;
        job.v = g_next;
        g_next.active = false;
        g_shown.active = false;
        if (!job.v.active)
            return;
        if (!gta1::GetDrawBuffer(job.b) || job.b.width != job.v.width || job.b.height != job.v.height ||
            !gta1::PlayerScreenPos(job.psx, job.psy))
            return;
        if (job.b.bytesPerPixel != 4)
        {
            if (!g_warnedFormat)
            {
                g_warnedFormat = true;
                LOG("Kippkamera: nur mit 32 Bit Farbtiefe (Spiel zeichnet mit %d Byte je Pixel)", job.b.bytesPerPixel);
            }
            return;
        }

        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        const int W = job.b.width, H = job.b.height;
        g_src.resize((size_t)W * H + 2);   // die Mischung liest je zwei Pixel am Stück
        parallel::For(H, 64, &CopyRows, &job);
        parallel::For(H, 16, &WarpRows, &job);
        g_shown = job.v;

        // Aufwand festhalten: einmal nach dem Start, mit der Debug-Anzeige laufend
        QueryPerformanceCounter(&t1);
        static LONGLONG s_spent = 0;
        static int s_frames = 0, s_logged = 0;
        s_spent += t1.QuadPart - t0.QuadPart;
        if (++s_frames == 300)
        {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            if (g_cfg.showInfo || s_logged++ < 1)
                LOG("Kippkamera: %.2f ms je Bild (%dx%d, %d Threads)", s_spent * 1000.0 / f.QuadPart / 300, W, H, parallel::Threads());
            s_spent = 0;
            s_frames = 0;
        }
    }

    bool Available()
    {
        if (!gta1::TiltCameraAvailable())
            return false;
        // Erst im Level zeigt sich, ob das Bild taugt; vorher gilt die Kamera als verfügbar
        gta1::DrawBuffer b;
        return !gta1::GetDrawBuffer(b) || (b.bytesPerPixel == 4 && ddhook::FastToRead(b.bits));
    }

    bool Active() { return g_shown.active; }
    float Rotation() { return g_shown.active ? g_shown.rot : 0.0f; }

    bool OutputToGame(float ox, float oy, float& gx, float& gy)
    {
        float px, py, dx, dy;
        if (!g_shown.active || !gta1::PlayerScreenPos(px, py) || !ToGround(g_shown, ox, oy, dx, dy))
            return false;
        gx = px + dx;
        gy = py + dy;
        return true;
    }

    bool GameToOutput(float gx, float gy, float& ox, float& oy)
    {
        float px, py;
        return g_shown.active && gta1::PlayerScreenPos(px, py) && FromGround(g_shown, gx - px, gy - py, ox, oy);
    }
}

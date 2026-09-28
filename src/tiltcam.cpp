#include "tiltcam.h"
#include "config.h"
#include "gta1.h"
#include "log.h"

#include <windows.h>
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
            gx = cr * lx - sr * ly;
            gy = (sr * lx + cr * ly) * v.squash;
            return true;
        }

        bool FromGround(const View& v, float gx, float gy, float& ox, float& oy)
        {
            const float cr = std::cos(v.rot), sr = std::sin(v.rot);
            const float yy = gy / v.squash;
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

        inline uint32_t Lerp(uint32_t a, uint32_t b, uint32_t w) // w 0..256
        {
            const uint32_t rb = (((a & 0xFF00FF) * (256 - w) + (b & 0xFF00FF) * w) >> 8) & 0xFF00FF;
            const uint32_t g = (((a & 0x00FF00) * (256 - w) + (b & 0x00FF00) * w) >> 8) & 0x00FF00;
            return rb | g;
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
        if (!g_cfg.tilt || !gta1::GetCameraView(cam) || !gta1::GetDrawBuffer(buf))
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

        // Welcher Bereich des ungekippten Bildes (relativ zur Figur) wird gebraucht? Rand der Ausgabe abtasten
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
            return;

        // Bildmitte: vom Spiel (mit seiner Vorausschau) zur Mitte des gebrauchten Bereichs überblenden,
        // Verkleinerung so, dass der Bereich um diese Mitte ganz ins Bild passt
        const float depth = cam.height + cam.playerZ;
        const float ppw = cam.focal / depth; // Bildpixel je Weltpixel in Höhe der Figur
        const float gameX = (cam.x - cam.playerX) * ppw, gameY = (cam.y - cam.playerY) * ppw * cam.squash;
        const float dx = gameX + ((minX + maxX) * 0.5f - gameX) * blend;
        const float dy = gameY + ((minY + maxY) * 0.5f - gameY) * blend;
        const float needX = std::max(std::abs(minX - dx), std::abs(maxX - dx)) / (v.width * 0.5f - 2);
        const float needY = std::max(std::abs(minY - dy), std::abs(maxY - dy)) / (v.height * 0.5f - 2);
        const float shrink = std::min(std::max(1.0f, std::max(needX, needY) * 1.01f), kMaxShrink);

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

    void Warp()
    {
        const View v = g_next;
        g_next.active = false;
        g_shown.active = false;
        if (!v.active)
            return;
        gta1::DrawBuffer b;
        float psx, psy;
        if (!gta1::GetDrawBuffer(b) || b.width != v.width || b.height != v.height || !gta1::PlayerScreenPos(psx, psy))
            return;
        if (b.bytesPerPixel != 4)
        {
            if (!g_warnedFormat)
            {
                g_warnedFormat = true;
                LOG("Kippkamera: nur mit 32 Bit Farbtiefe (Spiel zeichnet mit %d Byte je Pixel)", b.bytesPerPixel);
            }
            return;
        }

        const int W = b.width, H = b.height;
        g_src.resize((size_t)W * H);
        for (int y = 0; y < H; ++y)
            memcpy(&g_src[(size_t)y * W], b.bits + (size_t)y * b.pitch, (size_t)W * 4);


        const float c = std::cos(v.tilt), s = std::sin(v.tilt);
        const float cr = std::cos(v.rot), sr = std::sin(v.rot);
        const float inv = 1.0f / v.shrink;
        const int maxX = (W - 2) << 16, maxY = (H - 2) << 16;
        for (int oy = 0; oy < H; ++oy)
        {
            uint32_t* dst = reinterpret_cast<uint32_t*>(b.bits + (size_t)oy * b.pitch);
            const float y = (oy - v.pivotY) / (v.focal * v.squash);
            const float d = c + y * s;
            if (d < 0.05f)
            {
                memset(dst, 0, (size_t)W * 4);
                continue;
            }
            // In jeder Zeile hängt die Quelle linear von x ab
            const float t = v.focal * c / d;
            const float k = t / v.focal;
            const float ly = t * (y * c - s) + v.focal * s;
            const float lx0 = -k * v.pivotX;
            const float sx0 = psx + (cr * lx0 - sr * ly) * inv;
            const float sy0 = psy + (sr * lx0 + cr * ly) * v.squash * inv;
            int fx = (int)(sx0 * 65536.0f), fy = (int)(sy0 * 65536.0f);
            const int stepX = (int)(cr * k * inv * 65536.0f), stepY = (int)(sr * k * v.squash * inv * 65536.0f);
            for (int ox = 0; ox < W; ++ox, fx += stepX, fy += stepY)
            {
                const int cx = std::clamp(fx, 0, maxX), cy = std::clamp(fy, 0, maxY);
                const uint32_t* p = &g_src[(size_t)(cy >> 16) * W + (cx >> 16)];
                const uint32_t wx = (cx >> 8) & 0xFF, wy = (cy >> 8) & 0xFF;
                dst[ox] = Lerp(Lerp(p[0], p[1], wx), Lerp(p[W], p[W + 1], wx), wy);
            }
        }
        g_shown = v;
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

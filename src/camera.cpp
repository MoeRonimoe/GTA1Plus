#include "camera.h"
#include "citymap.h"
#include "overlay.h"

#include <windows.h>
#include <algorithm>
#include <cmath>

namespace camera
{
    namespace
    {
        float g_targetX = 0, g_targetY = 0; // Pixel der Spielauflösung
        float g_curX = 0, g_curY = 0;       // Weltpixel
        LARGE_INTEGER g_last = {};

        // Sieht man die Spielfigur, wenn die Kamera um (ox, oy) verschoben ist? GTA1 projiziert perspektivisch
        // vom Auge über dem Bildmittelpunkt (Höhe -height); Dächer nahe der Bildmitte werden stark vergrößert
        // und können eine Figur am Boden überdecken. Dazu den Sehstrahl vom Auge zur Figur verfolgen und
        // prüfen, ob er unter die Dachfläche eines höheren Blocks taucht.
        bool RayClear(const gta1::CameraView& v, const CityMap& map, float ox, float oy, float px, float py)
        {
            // Strahl in Blöcken: Punkt + (Auge - Punkt) * u, Höhe playerZ - drop * u (u = 0 am Punkt)
            const float x0 = px / 64.0f, y0 = py / 64.0f;
            const float ex = (v.x + ox - px) / 64.0f, ey = (v.y + oy - py) / 64.0f;
            const float drop = v.playerZ + v.height;
            const float uTop = v.playerZ / drop; // darüber liegt nichts mehr, was verdecken könnte

            // Alle Blöcke entlang des Strahls (Amanatides/Woo). In jedem Block liegt der Strahl beim Eintritt
            // am tiefsten; steckt er dort unter der Dachfläche, verdeckt der Block den Punkt.
            int bx = (int)std::floor(x0), by = (int)std::floor(y0);
            const int sx = ex > 0 ? 1 : -1, sy = ey > 0 ? 1 : -1;
            const float inf = 1e9f;
            const float dux = ex != 0 ? 1.0f / std::abs(ex) : inf, duy = ey != 0 ? 1.0f / std::abs(ey) : inf;
            float nextX = ex != 0 ? (ex > 0 ? bx + 1 - x0 : x0 - bx) * dux : inf;
            float nextY = ey != 0 ? (ey > 0 ? by + 1 - y0 : y0 - by) * duy : inf;
            float u = 0;
            for (int guard = 0; guard < 64 && u <= uTop; ++guard)
            {
                if (bx >= 0 && by >= 0 && bx < 256 && by < 256)
                {
                    const float roof = map.LidLevel(bx, by) * 64.0f; // 0 = oberste Ebene, 6 = leere Spalte
                    if (roof < v.playerZ - 16.0f && v.playerZ - drop * u > roof)
                        return false;
                }
                if (nextX < nextY)
                {
                    u = nextX;
                    nextX += dux;
                    bx += sx;
                }
                else
                {
                    u = nextY;
                    nextY += duy;
                    by += sy;
                }
            }
            return true;
        }

        // Mitte und Rand der Figur bzw. des Fahrzeugs: Bit i = Sehstrahl zu Punkt i ist frei
        unsigned ClearRays(const gta1::CameraView& v, const CityMap& map, float ox, float oy)
        {
            static const float kDir[5][2] = { { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
            unsigned mask = 0;
            for (int i = 0; i < 5; ++i)
                if (RayClear(v, map, ox, oy, v.playerX + kDir[i][0] * v.radius, v.playerY + kDir[i][1] * v.radius))
                    mask |= 1u << i;
            return mask;
        }

        // Größter Anteil 0..1 des Versatzes, bei dem alles sichtbar bleibt, was bei mittiger Kamera sichtbar
        // ist. Punkte in einer Wand oder unter einer Brücke verdeckt das Spiel ohnehin.
        float Clearance(const gta1::CameraView& v, const CityMap& map, unsigned base, float ox, float oy)
        {
            const auto ok = [&](float s) { return (ClearRays(v, map, ox * s, oy * s) & base) == base; };
            if ((ox == 0 && oy == 0) || ok(1.0f))
                return 1.0f;
            float lo = 0, hi = 1;
            for (int i = 0; i < 7; ++i)
            {
                const float mid = (lo + hi) * 0.5f;
                (ok(mid) ? lo : hi) = mid;
            }
            return lo;
        }
    }

    void SetTarget(float dx, float dy)
    {
        g_targetX = dx;
        g_targetY = dy;
    }

    void Reset()
    {
        g_targetX = g_targetY = g_curX = g_curY = 0;
    }

    void Filter(const gta1::CameraView* v, float& dx, float& dy)
    {
        LARGE_INTEGER now, freq;
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&freq);
        const float dt = g_last.QuadPart ? std::clamp((now.QuadPart - g_last.QuadPart) / (float)freq.QuadPart, 0.0f, 0.1f) : 0.0f;
        g_last = now;

        dx = dy = 0;
        if (!v)
        {
            g_curX = g_curY = 0; // Kamera folgt gerade nicht der Figur
            return;
        }

        // Bildschirmpixel -> Weltpixel in der Höhe der Spielfigur
        const float k = (v->height + v->playerZ) / v->focal;
        float tx = g_targetX * k, ty = g_targetY * k / v->squash;

        // Im Fahrzeug schaut das Spiel schon selbst in Fahrtrichtung voraus (v->x/y). Zusammen mit unserem
        // Versatz bleibt die Figur mindestens ein Zehntel der Bildbreite bzw. ein Achtel der Bildhöhe vom Rand
        // entfernt; die Vorausschau des Spiels selbst bleibt unangetastet.
        const float leadX = v->playerX - v->x, leadY = v->playerY - v->y;
        const float roomX = 0.8f * v->halfW * k, roomY = 0.75f * v->halfH * k / v->squash;
        tx = std::clamp(tx, std::min(0.0f, leadX - roomX), std::max(0.0f, leadX + roomX));
        ty = std::clamp(ty, std::min(0.0f, leadY - roomY), std::max(0.0f, leadY + roomY));
        bool hidden = false;
        if (const CityMap* map = overlay::City())
        {
            const unsigned base = ClearRays(*v, *map, 0, 0);
            const float s = Clearance(*v, *map, base, tx, ty);
            tx *= s;
            ty *= s;
            hidden = (ClearRays(*v, *map, g_curX, g_curY) & base) != base;
        }

        // Nach etwa 0,3 s zu zwei Dritteln am Ziel; verdeckt die aktuelle Stellung die Figur, schneller
        const float a = 1.0f - std::exp(-dt / (hidden ? 0.08f : 0.3f));
        g_curX += (tx - g_curX) * a;
        g_curY += (ty - g_curY) * a;
        if (tx == 0 && ty == 0 && std::abs(g_curX) < 0.5f && std::abs(g_curY) < 0.5f)
            g_curX = g_curY = 0;
        dx = g_curX;
        dy = g_curY;
    }

    void Current(float& dx, float& dy)
    {
        dx = g_curX;
        dy = g_curY;
    }
}

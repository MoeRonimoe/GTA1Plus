#include "firebomb.h"

#include "config.h"
#include "controls.h"
#include "gta1.h"
#include "log.h"
#include "style.h"
#include "text.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace firebomb
{
    namespace
    {
        constexpr float kBlock = 64.0f;                         // Weltpixel je Block
        constexpr float kMinRange = 2.5f * kBlock;              // näher reißt die Explosion den Werfer mit
        constexpr float kMaxRange = 7.0f * kBlock, kDefaultRange = 5.0f * kBlock;
        constexpr float kSpeed = 8.0f * kBlock;                 // Flug je Sekunde
        constexpr int kPerCrate = 5, kMaxAmmo = 99;
        constexpr DWORD kThrowGap = 600;                        // ms zwischen zwei Würfen

        int s_ammo = 0;
        bool s_selected = false;
        bool s_nativeHud = false;     // das Spiel zeichnet das Symbol samt Anzahl selbst
        bool s_lastFire = false, s_lastNext = false, s_lastPrev = false;
        bool s_holdFire = false, s_holdNext = false, s_holdPrev = false;   // Taste bis zum Loslassen zurückhalten
        bool s_pendingNext = false;                             // Wechsel ans Spiel gegeben, Ergebnis im nächsten Bild
        DWORD s_lastThrow = 0;
        unsigned s_level = ~0u;
        bool s_wasDead = false;
        std::wstring s_notice;

        struct Flight
        {
            float x0, y0, z, x1, y1;
            DWORD start, duration;
        };
        std::vector<Flight> s_flights;

        // ---- Kisten ----

        // Jede fünfte Pistolen- und MG-Kiste wird zur Brandbomben-Kiste, ausgewählt nach ihrer Lage: so sind es bei
        // jedem Spiel dieselben, und eine Kiste, die das Skript nach dem Aufheben neu aufstellt, bleibt, was sie war.
        // Mengen ab 100 sind Amokläufe und bleiben unberührt.
        void ConvertCrate(int& type, int& amount, int x, int y)
        {
            if (!g_cfg.firebomb || (type != 1 && type != 2) || amount >= 100)
                return;
            uint32_t h = static_cast<uint32_t>(x) * 2654435761u ^ static_cast<uint32_t>(y) * 40503u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            if (h % 5 != 0)
                return;
            type = 5;
            amount = kPerCrate;
        }

        void CratePicked(int player, int amount)
        {
            if (player != gta1::LocalPlayerIndex())
                return;
            s_ammo = std::min(kMaxAmmo, s_ammo + std::max(1, amount));
            s_notice = text::Get(text::FirebombPicked);
            LOG("Brandbombe: Kiste aufgehoben, jetzt %d", s_ammo);
        }

        // ---- Symbol aus der Style-Datei ----

        style::Sprite s_icon[2];      // 0 klein (Sprite 9), 1 groß (Sprite 34)
        unsigned s_iconMapGen = ~0u;

        // Die Pfeil-Sprites (HUD-Symbole) sind die ersten im Style: Nummer = Index der Sprite-Tabelle
        bool LoadIcons()
        {
            style::File file;
            return file.Load() && file.SpriteAt(9, s_icon[0]) && file.SpriteAt(34, s_icon[1]);
        }

        // ---- Wurf ----

        void Throw()
        {
            float x, y, z;
            int angle;
            if (!gta1::PlayerFoot(x, y, z, angle))
                return;
            // Ziel: das Fadenkreuz (Maus oder rechter Stick), sonst geradeaus
            float tx = 0, ty = 0, gx, gy;
            bool aimed = controls::AimPoint(gx, gy) && gta1::ScreenToWorld(gx, gy, z, tx, ty);
            float dx = tx - x, dy = ty - y, dist = std::sqrt(dx * dx + dy * dy);
            if (!aimed || dist < 1.0f)
            {
                // Blickrichtung des Spiels: 0 = nach unten, im Uhrzeigersinn (vgl. controls::AngleFromScreen)
                const float compass = (180.0f - angle * 360.0f / 1024.0f) * 3.14159265f / 180.0f;
                dx = std::sin(compass);
                dy = -std::cos(compass);
                dist = kDefaultRange;
            }
            else
            {
                dx /= dist;
                dy /= dist;
            }
            dist = std::clamp(dist, kMinRange, kMaxRange);
            // An einer Hauswand zerschellt sie
            float reach = dist;
            for (float s = 16.0f; s <= dist; s += 16.0f)
                if (gta1::IsBuilding(x + dx * s, y + dy * s, z))
                {
                    reach = std::max(0.0f, s - 24.0f);
                    break;
                }
            if (reach < kMinRange * 0.6f)
            {
                // direkt vor einer Wand: nicht werfen, die Explosion träfe den Werfer selbst
                static int s_logged = 0;
                if (s_logged < 10 && ++s_logged)
                    LOG("Brandbombe: Wand direkt voraus, kein Wurf");
                return;
            }
            Flight f;
            f.x0 = x;
            f.y0 = y;
            f.z = z;
            f.x1 = x + dx * reach;
            f.y1 = y + dy * reach;
            f.start = GetTickCount();
            f.duration = std::max<DWORD>(150, static_cast<DWORD>(reach / kSpeed * 1000.0f));
            s_flights.push_back(f);
            s_lastThrow = f.start;
            if (--s_ammo <= 0)
            {
                s_ammo = 0;
                s_selected = false;
            }
        }
    }

    void Init()
    {
        if (!gta1::FirebombAvailable())
        {
            LOG("Brandbombe: für diese Version nicht verfügbar");
            return;
        }
        LOG("Brandbombe: %s", gta1::HookCrates(&ConvertCrate, &CratePicked) ? "Kisten eingehängt" : "Spielcode passt nicht");
        s_nativeHud = gta1::HookFirebombHud([](int& ammo) { ammo = s_ammo; return s_selected && s_ammo > 0; });
        LOG("Brandbombe: Symbol %s", s_nativeHud ? "im HUD des Spiels" : "vom Mod gezeichnet");
    }

    void Tick()
    {
        const unsigned level = gta1::LevelLoads();
        if (level != s_level)
        {
            s_level = level;
            s_ammo = 0;
            s_selected = false;
            s_flights.clear();
        }
        // Tod oder Verhaftung: wenn das Spiel die Waffen genommen hat (sonst Gefängniskarte), auch die Brandbomben
        const bool dead = gta1::IsPlayerDead();
        if (s_wasDead && !dead && !gta1::PlayerHasWeapons())
        {
            s_ammo = 0;
            s_selected = false;
        }
        s_wasDead = dead;

        const DWORD now = GetTickCount();
        for (size_t i = 0; i < s_flights.size();)
        {
            const Flight& f = s_flights[i];
            if (now - f.start < f.duration)
            {
                ++i;
                continue;
            }
            static int s_logged = 0;
            if (s_logged < 30 && ++s_logged)
                LOG("Brandbombe: Aufschlag bei (%.1f, %.1f), %.1f Blöcke vom Wurf", f.x1 / kBlock, f.y1 / kBlock,
                    std::hypot(f.x1 - f.x0, f.y1 - f.y0) / kBlock);
            gta1::Explode(f.x1, f.y1, f.z);
            s_flights.erase(s_flights.begin() + i);
        }
    }

    Keys Filter(Keys in, bool onFoot)
    {
        Keys out = in;
        const bool nextPress = in.next && !s_lastNext, prevPress = in.prev && !s_lastPrev, firePress = in.fire && !s_lastFire;
        s_lastNext = in.next;
        s_lastPrev = in.prev;
        s_lastFire = in.fire;
        if (!in.next)
            s_holdNext = false;
        if (!in.prev)
            s_holdPrev = false;
        if (!in.fire)
            s_holdFire = false;

        const int weapon = gta1::PlayerWeapon();
        if (!g_cfg.firebomb || !gta1::FirebombAvailable() || !gta1::InGame() || weapon < 0)
        {
            s_selected = false;
            s_pendingNext = false;
            return out;
        }
        // Vorwärts über die höchste Waffe hinaus landet das Spiel auf der Faust: dazwischen kommt die Brandbombe
        if (s_pendingNext && weapon == 0 && s_ammo > 0)
            s_selected = true;
        s_pendingNext = false;
        if (s_selected && (weapon != 0 || s_ammo <= 0))
            s_selected = false;

        // Reihenfolge: Faust, Pistole .. Flammenwerfer, Brandbombe, Faust ...
        if (nextPress)
        {
            if (s_selected)
            {
                s_selected = false;           // Brandbombe -> Faust: das Spiel steht schon darauf
                s_holdNext = true;
            }
            else if (weapon > 0 || !gta1::PlayerHasWeapons())
                s_pendingNext = true;
        }
        if (prevPress)
        {
            if (s_selected)
                s_selected = false;           // Brandbombe -> höchste Waffe: das Spiel wechselt von der Faust aus
            else if (weapon == 0 && s_ammo > 0)
            {
                s_selected = true;            // Faust -> Brandbombe
                s_holdPrev = true;
            }
        }
        if (s_selected && onFoot)
        {
            s_holdFire = s_holdFire || in.fire;   // die Faust schlägt nicht zu
            if (firePress && GetTickCount() - s_lastThrow >= kThrowGap)
                Throw();
        }
        if (s_holdNext)
            out.next = false;
        if (s_holdPrev)
            out.prev = false;
        if (s_holdFire)
            out.fire = false;
        return out;
    }

    bool Selected() { return s_selected; }
    bool NativeHud() { return s_nativeHud; }
    int Ammo() { return s_ammo; }
    void SetAmmo(int n) { s_ammo = std::clamp(n, 0, kMaxAmmo); }

    void FlyingBombs(std::vector<Bomb>& out)
    {
        out.clear();
        const DWORD now = GetTickCount();
        for (const Flight& f : s_flights)
        {
            const float t = std::min(1.0f, (now - f.start) / float(f.duration));
            Bomb b;
            if (!gta1::WorldToScreen(f.x0 + (f.x1 - f.x0) * t, f.y0 + (f.y1 - f.y0) * t, f.z, b.x, b.y))
                continue;
            b.scale = 1.0f + 0.8f * std::sin(t * 3.14159265f);   // im Bogen der Kamera näher
            out.push_back(b);
        }
    }

    const uint32_t* Icon(bool large, int& w, int& h)
    {
        if (s_iconMapGen != gta1::MapGeneration())
        {
            s_iconMapGen = gta1::MapGeneration();
            if (!LoadIcons())
                LOG("Brandbombe: Symbol nicht im Style gefunden");
        }
        const style::Sprite& icon = s_icon[large];
        w = icon.w;
        h = icon.h;
        return icon.w ? icon.px.data() : nullptr;
    }

    bool TakeNotice(std::wstring& out)
    {
        if (s_notice.empty())
            return false;
        out.swap(s_notice);
        s_notice.clear();
        return true;
    }
}

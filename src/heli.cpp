#include "heli.h"
#include "config.h"
#include "gta1.h"
#include "log.h"
#include "text.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>

namespace heli
{
    namespace
    {
        constexpr int kModel = 88;
        constexpr float kBlock = 64.0f;
        constexpr float kPi = 3.14159265f;
        // Träge wie ein Hubschrauber: Schub zieht in Bugrichtung, das Tempo bleibt und läuft nur langsam aus,
        // Drehen und Steigen kommen verzögert
        constexpr float kThrust = 4.2f * kBlock;     // Weltpixel/s² bei vollem Gas, rückwärts 60 %
        constexpr float kDrag = 0.6f;                // je Sekunde: Endtempo kThrust / kDrag = 7 Blöcke/s
        constexpr float kTurn = 300.0f;              // Drehen, Winkeleinheiten (1024 = 360°) je Sekunde
        constexpr float kTurnLag = 3.0f;             // Drehtempo folgt der Lenkung (je Sekunde)
        constexpr float kClimb = 1.2f * kBlock;      // Steigen und Sinken, Weltpixel je Sekunde
        constexpr float kClimbLag = 2.0f;
        constexpr float kCeiling = -1.5f * kBlock;   // höchste Höhe: anderthalb Blöcke über der Oberkante der Karte
        constexpr float kSafeSpeed = 2.0f * kBlock;  // Aufsetzen und Anstoßen bis zu diesem Tempo geht gut
        constexpr float kHalfLength = 50.0f;         // Rumpf vor und hinter der Mitte (Sprite 48 x 128)
        constexpr float kCarClearance = 40.0f;       // tiefer über dem Boden stoßen Fahrzeuge an
        constexpr float kDeadlyDrop = 32.0f;         // höher über dem Boden ist Aussteigen tödlich
        constexpr float kGravity = 6.0f * kBlock;    // ohne Pilot fällt er (Weltpixel/s²)
        constexpr float kGroundThrust = 0.1f;        // am Boden nur noch zum Wenden: ein Zehntel des Schubs

        bool  s_active = false, s_up = false, s_down = false, s_flying = false, s_covered = false;
        bool  s_abandoned = false;                   // in der Luft verlassen: fällt
        bool  s_help = false, s_wasActive = false;   // Steuerung einblenden
        unsigned s_spawnLevel = ~0u;
        DWORD s_levelTick = 0;
        DWORD s_jumpTick = 0;                        // Spielfigur aus der Höhe abgesprungen, fällt noch
        float s_steer = 0, s_throttle = 0;
        int   s_car = -1, s_limitedCar = -1;
        float s_limitedThrust = 0;
        float s_x = 0, s_y = 0, s_z = 0;             // Weltpixel, z kleiner = höher (0 = Oberkante der Karte)
        float s_vx = 0, s_vy = 0, s_vz = 0;          // Weltpixel je Sekunde
        float s_heading = 0, s_turn = 0;             // Winkeleinheiten, 0 = Süden, gegen den Uhrzeigersinn
        float s_ground = 0;
        float s_camera = 0;                          // Kamera: 0 = wie gehabt, 1 = wie bei schneller Fahrt
        DWORD s_tick = 0, s_logged = 0;

        // Beim Einsteigen zeigt das Spiel den Namen des Modells ("car88"); den gibt es in keiner Sprachdatei mehr.
        // Die Schrift des Spiels kennt nur ASCII sicher, Akzente fallen weg.
        const char* ExtraText(const char* key)
        {
            if (strcmp(key, "car88") != 0)
                return nullptr;
            static char s_name[64];
            const wchar_t* name = text::Get(text::HeliName);
            size_t n = 0;
            for (; name[n] && n + 1 < sizeof(s_name); ++n)
            {
                const wchar_t c = name[n];
                const wchar_t* from = L"àáâäèéêëìíîïòóôöùúûüçÀÁÂÄÈÉÊËÌÍÎÏÒÓÔÖÙÚÛÜÇ";
                const char* to = "aaaaeeeeiiiioooouuuucAAAAEEEEIIIIOOOOUUUUC";
                const wchar_t* hit = c < 128 ? nullptr : wcschr(from, c);
                s_name[n] = c < 128 ? (char)c : hit ? to[hit - from] : '?';
            }
            s_name[n] = 0;
            return s_name;
        }

        // Abstellplätze: freie ebene Flächen (3 x 4 Blöcke Gehweg oder Wiese unter freiem Himmel, ohne Zaun, Mast oder
        // Objekt), je Stadt vier über die Karte verteilt. Block x, y der Mitte, Block des Bodens z, Winkel
        struct Place { const char* map; int x, y, z, angle; };
        constexpr Place kPlaces[] = {
            { "nyc.cmp", 144, 96, 4, 0 }, { "nyc.cmp", 71, 143, 4, 256 }, { "nyc.cmp", 199, 93, 4, 256 }, { "nyc.cmp", 233, 61, 4, 256 },
            { "sanb.cmp", 17, 46, 1, 256 }, { "sanb.cmp", 57, 193, 4, 256 }, { "sanb.cmp", 160, 173, 4, 256 }, { "sanb.cmp", 235, 68, 4, 256 },
            { "miami.cmp", 109, 81, 4, 256 }, { "miami.cmp", 49, 122, 4, 256 }, { "miami.cmp", 242, 130, 4, 256 }, { "miami.cmp", 85, 14, 4, 0 },
        };

        bool OnMap(const Place& p)
        {
            std::string map = gta1::MapFile();
            for (char& c : map)
                c = (char)tolower((unsigned char)c);
            const size_t n = strlen(p.map);
            return map.size() > n && map.compare(map.size() - n, n, p.map) == 0 && (map[map.size() - n - 1] == '\\' || map[map.size() - n - 1] == '/');
        }

        // Kurz nach dem Levelstart (die Missionsdatei hat ihre Fahrzeuge dann aufgestellt) die Hubschrauber hinstellen
        void SpawnParked(DWORD now)
        {
            const unsigned level = gta1::LevelLoads();
            if (level != s_spawnLevel)
            {
                s_spawnLevel = level;
                s_levelTick = now;
            }
            if (!s_levelTick || now - s_levelTick < 3000 || !gta1::InGame() || !gta1::ModCarAvailable())
                return;
            if (!g_cfg.helicopter)
            {
                s_levelTick = 0;   // abgeschaltet: in diesem Level keine
                return;
            }
            s_levelTick = 0;
            int placed = 0;
            for (const Place& p : kPlaces)
                if (OnMap(p) && gta1::CarNear(p.x * kBlock + 32, p.y * kBlock + 32, p.z * kBlock, 96, -1) < 0 &&
                    gta1::CreateCar(p.x, p.y, p.z, kModel, p.angle) >= 0)
                    ++placed;
            if (placed)
                LOG("Hubschrauber: %d abgestellt", placed);
        }

        void Forward(float heading, float& fx, float& fy)
        {
            const float a = heading * (2 * kPi / 1024);
            fx = std::sin(a);
            fy = std::cos(a);
        }

        // Ebene des Blocks, in dem die Unterseite steckt; über der Oberkante der Karte -1
        int LevelAt(float z) { return z < 2 ? -1 : (int)((z - 2) / kBlock); }

        // Steckt der Rumpf in der Höhe z in einem Gebäude oder unter einem Deckel (Brücke, Dach)?
        bool HitsSolid(float x, float y, float z, float heading)
        {
            const int level = LevelAt(z);
            if (level < 0)
                return false;
            float fx, fy;
            Forward(heading, fx, fy);
            for (float along : { -kHalfLength, 0.0f, kHalfLength })
                if (gta1::IsSolidBlock((int)((x + fx * along) / kBlock), (int)((y + fy * along) / kBlock), level))
                    return true;
            return false;
        }

        // Über ihm noch ein Deckel (unter einer Brücke): dann zeichnet ihn das Spiel, die Brücke liegt richtig darüber
        bool Covered(float x, float y, float z)
        {
            for (int level = LevelAt(z) - 1; level >= 0; --level)
                if (gta1::IsSolidBlock((int)(x / kBlock), (int)(y / kBlock), level))
                    return true;
            return false;
        }

        float Speed() { return std::sqrt(s_vx * s_vx + s_vy * s_vy); }

        void Release(int car, const char* why)
        {
            gta1::SetModCar(-1);
            gta1::SetModCarDrawn(false);
            s_flying = s_abandoned = false;
            LOG("Hubschrauber: %s (Fahrzeug %d, Höhe %.2f, Boden %.2f, Tempo %.1f)", why, car, s_z / kBlock, s_ground / kBlock,
                Speed() / kBlock);
        }

        // Aufschlag: Explosion an Ort und Stelle, das Wrack fällt auf den Boden darunter
        void Crash(int car)
        {
            gta1::Explode(s_x, s_y, std::max(0.0f, s_z));
            gta1::SetCarPlace(car, s_x, s_y, s_ground, (int)s_heading);
            gta1::SetCarDamage(car, 100);
            Release(car, "abgestürzt");
        }
    }

    namespace
    {
        // Ohne Pilot: kein Schub, Schwerkraft zieht ihn hinunter; Wände und Boden enden schnell in einer Explosion
        void Fall(float dt)
        {
            s_vz += kGravity * dt;
            const float drag = std::exp(-kDrag * dt);
            s_vx *= drag;
            s_vy *= drag;
            const float nx = std::clamp(s_x + s_vx * dt, kBlock, 255 * kBlock);
            const float ny = std::clamp(s_y + s_vy * dt, kBlock, 255 * kBlock);
            const float nz = s_z + s_vz * dt;
            s_ground = gta1::GroundBelow(s_x, s_y, std::max(0.0f, s_z));
            if (HitsSolid(nx, ny, nz, s_heading) || nz >= s_ground - 1)
            {
                if (nz >= s_ground - 1 && s_vz <= kSafeSpeed && Speed() <= kSafeSpeed)
                {
                    gta1::SetCarPlace(s_car, s_x, s_y, s_ground, (int)s_heading);
                    Release(s_car, "führerlos gelandet");
                }
                else
                    Crash(s_car);
                return;
            }
            s_x = nx;
            s_y = ny;
            s_z = nz;
            s_covered = Covered(s_x, s_y, s_z);
            gta1::SetModCarDrawn(s_covered);
            gta1::SetCarPlace(s_car, s_x, s_y, std::max(1.0f, s_z), (int)s_heading);
            gta1::SyncModCar();
        }

        // Abgesprungen: das Spiel lässt die Figur fallen; kommt sie unten lebend an, war es trotzdem zu hoch
        void WatchJump(DWORD now)
        {
            if (!s_jumpTick)
                return;
            float x, y, z;
            int angle;
            if (now - s_jumpTick > 5000 || !gta1::PlayerFoot(x, y, z, angle))
            {
                s_jumpTick = 0;
                return;
            }
            if (now - s_jumpTick < 300 || z < gta1::GroundBelow(x, y, z) - 2)
                return;   // fällt noch
            s_jumpTick = 0;
            if (gta1::PlayerHealth() > 0)
            {
                gta1::KillPlayer();
                LOG("Hubschrauber: Sturz aus der Höhe nicht überlebt");
            }
        }
    }

    void Init()
    {
        LOG("Hubschrauber: Name des Modells %s", gta1::SetExtraTexts(&ExtraText) ? "eingehängt" : "nicht eingehängt - Einsteigen bricht ab");
    }

    void SetInput(bool up, bool down)
    {
        s_up = up;
        s_down = down;
    }

    void SetDrive(float steer, float throttle)
    {
        s_steer = std::clamp(steer, -1.0f, 1.0f);
        s_throttle = std::clamp(throttle, -1.0f, 1.0f);
    }

    void Spots(std::vector<Spot>& out)
    {
        out.clear();
        if (g_cfg.helicopter && gta1::ModCarAvailable())
            for (const Place& p : kPlaces)
                if (OnMap(p))
                    out.push_back({ p.x + 0.5f, p.y + 0.5f });
    }

    bool TakeHelp()
    {
        const bool help = s_help;
        s_help = false;
        return help;
    }

    bool Active() { return s_active; }
    bool Flying() { return s_flying; }

    bool GetVisual(Visual& out)
    {
        if (!s_flying || s_covered)
            return false;
        out = { s_x, s_y, s_z, s_ground, s_heading, s_vx / (kThrust / kDrag), s_vy / (kThrust / kDrag) };
        return true;
    }

    void Tick()
    {
        const DWORD now = GetTickCount();
        const float dt = s_tick ? std::min(0.1f, (now - s_tick) / 1000.0f) : 0.0f;
        s_tick = now;
        SpawnParked(now);
        int car = -1, model = -1;
        const bool inHeli = gta1::PlayerCar(car, model) && model == kModel && gta1::ModCarAvailable();
        // Abgeschaltet: ein Hubschrauber, der noch herumsteht, ist wieder ein Bus mit Hubschrauber-Grafik wie im
        // Spiel; ein laufender Flug endet noch wie gewohnt
        s_active = inHeli && (g_cfg.helicopter || s_flying);
        const float groundThrust = g_cfg.helicopter ? kGroundThrust : 1.0f;
        if (inHeli && (car != s_limitedCar || groundThrust != s_limitedThrust))
        {
            s_limitedCar = car;
            s_limitedThrust = groundThrust;
            gta1::ScaleCarThrust(car, groundThrust);
        }
        if (s_active && !s_wasActive)
            s_help = true;
        s_wasActive = s_active;
        if (s_flying && !s_abandoned && (!s_active || car != s_car))
        {
            // Ausgestiegen (oder Tod, Verhaftung, Missionsende): in der Höhe fällt der Hubschrauber führerlos, die
            // Figur stürzt hinterher
            if (s_ground - s_z > kDeadlyDrop)
            {
                s_abandoned = true;
                s_jumpTick = !s_active ? now : 0;
                LOG("Hubschrauber: in %.1f Blöcken Höhe verlassen", (s_ground - s_z) / kBlock);
            }
            else
                Release(s_car, "ohne Pilot");
        }
        WatchJump(now);
        s_camera = std::clamp(s_camera + (s_flying && !s_abandoned ? dt : -dt) / 0.8f, 0.0f, 1.0f);
        gta1::SetCameraFlight(s_camera * s_camera * (3 - 2 * s_camera));
        if (s_abandoned)
        {
            Fall(dt);
            return;
        }
        if (!s_active)
        {
            s_car = -1;
            return;
        }
        float x, y, z;
        int angle;
        if (!gta1::CarPlace(car, x, y, z, angle))
            return;
        s_car = car;
        if (!s_flying)
        {
            // Am Boden fährt ihn das Spiel; Steigen hebt ab, gleich mit etwas Auftrieb
            if (!s_up)
                return;
            s_x = x;
            s_y = y;
            s_z = z - 2;
            s_vx = s_vy = s_turn = 0;
            s_vz = -kClimb * 0.5f;
            s_heading = (float)angle;
            if (!gta1::SetModCar(car))
                return;
            s_flying = true;
            LOG("Hubschrauber: hebt ab (Fahrzeug %d, Winkel %d, Höhe %.2f)", car, angle, z / kBlock);
        }

        // Drehen und Steigen folgen den Tasten verzögert, der Schub ändert das Tempo, das sonst nur langsam ausläuft
        s_turn += (-s_steer * kTurn - s_turn) * std::min(1.0f, kTurnLag * dt);   // rechts = im Uhrzeigersinn
        s_heading = std::fmod(s_heading + s_turn * dt + 1024.0f, 1024.0f);
        const float climb = (s_down ? kClimb : 0.0f) - (s_up ? kClimb : 0.0f);
        s_vz += (climb - s_vz) * std::min(1.0f, kClimbLag * dt);
        float fx, fy;
        Forward(s_heading, fx, fy);
        const float thrust = s_throttle >= 0 ? s_throttle * kThrust : s_throttle * kThrust * 0.6f;
        const float drag = std::exp(-kDrag * dt);
        s_vx = (s_vx + fx * thrust * dt) * drag;
        s_vy = (s_vy + fy * thrust * dt) * drag;
        const float nx = std::clamp(s_x + s_vx * dt, kBlock, 255 * kBlock);
        const float ny = std::clamp(s_y + s_vy * dt, kBlock, 255 * kBlock);
        const float nz = std::max(s_z + s_vz * dt, kCeiling);

        // Hindernisse in der Flughöhe: langsam prallt er ab, schnell explodiert er. Steigen oder Sinken in einen
        // Deckel hinein (unter der Brücke hoch) hält er an derselben Stelle auf.
        if (HitsSolid(s_x, s_y, nz, s_heading))
        {
            if (std::fabs(s_vz) > kSafeSpeed)
            {
                s_ground = gta1::GroundBelow(s_x, s_y, std::max(0.0f, s_z));
                Crash(car);
                return;
            }
            s_vz = 0;
        }
        else
            s_z = nz;
        // Dicht über der Straße sind auch Fahrzeuge im Weg
        const bool carInWay = s_ground - s_z < kCarClearance && gta1::CarNear(nx, ny, s_ground, 44, car) >= 0;
        if (carInWay || HitsSolid(nx, ny, s_z, s_heading))
        {
            if (Speed() > kSafeSpeed)
            {
                s_ground = gta1::GroundBelow(s_x, s_y, std::max(0.0f, s_z));
                Crash(car);
                return;
            }
            s_vx *= -0.3f;
            s_vy *= -0.3f;
        }
        else
        {
            s_x = nx;
            s_y = ny;
        }

        // Boden: sanft aufsetzen gibt ihn ans Spiel zurück, zu schnell endet es wie an der Wand. Wer steigt, setzt
        // nicht auf (gleich nach dem Abheben, über einer Kante nach oben)
        s_ground = gta1::GroundBelow(s_x, s_y, std::max(0.0f, s_z));
        if (s_z >= s_ground - 1 && (s_up || s_vz < 0))
            s_z = s_ground - 1;
        else if (s_z >= s_ground - 1)
        {
            s_z = s_ground;
            if (Speed() > kSafeSpeed)
            {
                Crash(car);
                return;
            }
            gta1::SetCarPlace(car, s_x, s_y, s_ground, (int)s_heading);
            Release(car, "gelandet");
            return;
        }
        s_covered = Covered(s_x, s_y, s_z);
        gta1::SetModCarDrawn(s_covered);
        // Für das Spiel (Kamera, Polizei, Missionen, Zusammenstöße) steht er da, wo er fliegt, in der Höhe aber nie über
        // der Karte
        gta1::SetCarPlace(car, s_x, s_y, std::max(1.0f, s_z), (int)s_heading);
        gta1::SyncModCar();
        if (now - s_logged > 2000)
        {
            s_logged = now;
            LOG("Hubschrauber: Höhe %.2f, Boden %.2f, Tempo %.1f, Winkel %d%s", s_z / kBlock, s_ground / kBlock, Speed() / kBlock,
                (int)s_heading, s_covered ? ", unter einem Deckel" : "");
        }
    }
}

#include "rumble.h"
#include "config.h"
#include "gta1.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>

namespace rumble
{
    namespace
    {
        // ---- Ausgabe: eigener Thread, weil das Schreiben an ein Bluetooth-Pad einige Millisekunden dauern kann ----
        enum class Kind { None, XInput, Ds4Usb, Ds4Bluetooth, DualSenseUsb, DualSenseBluetooth };

        CRITICAL_SECTION g_lock;
        Kind g_kind = Kind::None;
        XInputSetState_t g_xinputSet = nullptr;
        HANDLE g_hid = INVALID_HANDLE_VALUE;
        HANDLE g_wake = nullptr;
        volatile LONG g_target = 0;         // gewünschte Motorstärke: stark << 8 | schwach (je 0..255)

        using HidSetOutputReport_t = BOOLEAN(__stdcall*)(HANDLE, void*, ULONG);
        HidSetOutputReport_t g_hidSetOutput = nullptr;

        uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc)
        {
            for (size_t i = 0; i < size; ++i)
            {
                crc ^= data[i];
                for (int k = 0; k < 8; ++k)
                    crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
            }
            return crc;
        }

        // Bluetooth-Berichte der Sony-Pads tragen am Ende eine Prüfsumme über 0xA2 und den Bericht
        void SealBluetooth(uint8_t* report, size_t size)
        {
            const uint8_t seed = 0xA2;
            const uint32_t crc = ~Crc32(report, size - 4, Crc32(&seed, 1, 0xFFFFFFFFu));
            memcpy(report + size - 4, &crc, 4);
        }

        // Nur die Motoren stellen, Leuchtbalken und Töne bleiben, wie sie sind. strong = großer (linker) Motor
        bool Send(Kind kind, uint8_t strong, uint8_t weak)
        {
            DWORD written = 0;
            switch (kind)
            {
            case Kind::XInput:
            {
                struct { WORD left, right; } v = { (WORD)(strong * 257), (WORD)(weak * 257) };
                return g_xinputSet && g_xinputSet(0, &v) == ERROR_SUCCESS;
            }
            case Kind::Ds4Usb:
            {
                uint8_t r[32] = { 0x05, 0x01 };     // Bericht 5, gültig: Motoren
                r[4] = weak;
                r[5] = strong;
                return WriteFile(g_hid, r, sizeof(r), &written, nullptr) != 0;
            }
            case Kind::Ds4Bluetooth:
            {
                uint8_t r[78] = { 0x11, 0xC0, 0x00, 0x01 };     // Bericht 0x11, HID mit Prüfsumme, gültig: Motoren
                r[6] = weak;
                r[7] = strong;
                SealBluetooth(r, sizeof(r));
                // Über den Steuerkanal; manche Bluetooth-Treiber nehmen den Bericht nur als gewöhnliches Schreiben
                return (g_hidSetOutput && g_hidSetOutput(g_hid, r, sizeof(r))) || WriteFile(g_hid, r, sizeof(r), &written, nullptr) != 0;
            }
            case Kind::DualSenseUsb:
            {
                uint8_t r[48] = { 0x02, 0x03 };     // Bericht 2, gültig: Vibration wie beim DualShock
                r[3] = weak;
                r[4] = strong;
                return WriteFile(g_hid, r, sizeof(r), &written, nullptr) != 0;
            }
            case Kind::DualSenseBluetooth:
            {
                static uint8_t s_sequence = 0;
                uint8_t r[78] = { 0x31, 0x00, 0x10, 0x03 };
                r[1] = (uint8_t)((s_sequence++ & 0x0F) << 4);
                r[5] = weak;
                r[6] = strong;
                SealBluetooth(r, sizeof(r));
                return WriteFile(g_hid, r, sizeof(r), &written, nullptr) != 0 || (g_hidSetOutput && g_hidSetOutput(g_hid, r, sizeof(r)));
            }
            default:
                return false;
            }
        }

        DWORD WINAPI OutputThread(void*)
        {
            LONG sent = 0;
            DWORD sentTick = 0;
            int failures = 0;
            for (;;)
            {
                WaitForSingleObject(g_wake, 200);
                const LONG target = g_target;
                const DWORD now = GetTickCount();
                // Laufende Vibration ab und zu auffrischen; ein Stopp wird sicherheitshalber wiederholt
                if (target == sent && (target == 0 || now - sentTick < 400))
                    continue;
                EnterCriticalSection(&g_lock);
                const Kind kind = g_kind;
                const bool ok = kind != Kind::None && Send(kind, (uint8_t)(target >> 8), (uint8_t)target);
                LeaveCriticalSection(&g_lock);
                if (kind == Kind::None)
                {
                    sent = 0;
                    continue;
                }
                if (ok)
                {
                    sent = target;
                    sentTick = now;
                    failures = 0;
                }
                else if (++failures == 1)
                    LOG("Vibration: Schreiben ans Pad fehlgeschlagen (Fehler %u)", GetLastError());
            }
        }

        void CloseHid()
        {
            if (g_hid != INVALID_HANDLE_VALUE)
            {
                CloseHandle(g_hid);
                g_hid = INVALID_HANDLE_VALUE;
            }
        }

        void EnsureThread()
        {
            if (g_wake)
                return;
            InitializeCriticalSection(&g_lock);
            g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (HMODULE hid = LoadLibraryW(L"hid.dll"))
                g_hidSetOutput = reinterpret_cast<HidSetOutputReport_t>(GetProcAddress(hid, "HidD_SetOutputReport"));
            if (HANDLE t = CreateThread(nullptr, 0, &OutputThread, nullptr, 0, nullptr))
                CloseHandle(t);
        }

        void SetTarget(float strong, float weak)
        {
            const int s = (int)(std::min(1.0f, std::max(0.0f, strong)) * 255.0f + 0.5f);
            const int w = (int)(std::min(1.0f, std::max(0.0f, weak)) * 255.0f + 0.5f);
            const LONG target = (s << 8) | w;
            if (InterlockedExchange(&g_target, target) != target && g_wake)
                SetEvent(g_wake);
        }

        // ---- Ereignisse: je Motor ein Impuls, der stärkere gewinnt, bis er abgelaufen ist ----
        struct Pulse
        {
            float level = 0;
            DWORD until = 0;
        };
        Pulse g_strong, g_weak;

        void Add(Pulse& p, float level, int ms, DWORD now)
        {
            const bool running = (int)(p.until - now) > 0;
            if (level <= 0 || (running && p.level > level))
                return;
            p.level = level;
            p.until = now + ms;
        }

        void Shake(float strong, float weak, int ms)
        {
            const DWORD now = GetTickCount();
            Add(g_strong, strong, ms, now);
            Add(g_weak, weak, ms, now);
        }

        gta1::Feedback g_prev;
        bool g_havePrev = false;
        DWORD g_testUntil = 0;              // Debug: bis dahin volle Stärke, egal womit gespielt wird
        unsigned g_level = ~0u;

        // Schuss der eigenen Waffe: großer Motor, kleiner Motor, Dauer
        struct Kick { float strong, weak; int ms; };
        Kick WeaponKick(int weapon)
        {
            switch (weapon)
            {
            // Unter etwa 100 ms und ohne den großen Motor ist ein Impuls am PS4-Pad kaum zu spüren
            case 1:  return { 0.35f, 0.65f, 120 };  // Pistole
            case 2:  return { 0.40f, 0.60f, 100 };  // Maschinenpistole (läuft bei Dauerfeuer durch)
            case 3:  return { 0.90f, 0.60f, 240 };  // Raketenwerfer
            case 4:  return { 0.25f, 0.40f, 110 };  // Flammenwerfer
            default: return { 0.30f, 0.50f, 110 };
            }
        }

        // Explosion: nah stark, ab etwa acht Blöcken nichts mehr
        void OnExplosion(float x, float y)
        {
            gta1::Feedback fb;
            if (!gta1::GetFeedback(fb))
                return;
            const float dist = std::sqrt((x - fb.x) * (x - fb.x) + (y - fb.y) * (y - fb.y));
            const float closeness = 1.0f - dist / 8.0f;
            if (closeness > 0)
                Shake(0.35f + 0.65f * closeness, 0.25f + 0.55f * closeness, 250 + (int)(250 * closeness));
            if (g_cfg.showInfo)
                LOG("Vibration: Explosion in %.1f Blöcken Entfernung", dist);
        }
    }

    void Init()
    {
        const bool hooked = gta1::SetExplosionCallback(&OnExplosion);
        LOG("Vibration: Explosionen %s", hooked ? "werden gemeldet" : "nicht verfügbar (Spielcode passt nicht)");
    }

    void Test(int ms)
    {
        g_testUntil = GetTickCount() + ms;
        LOG("Debug: Vibration für %d ms (%s)", ms, !g_wake || g_kind == Kind::None ? "kein Pad für Vibration" : "Pad angemeldet");
    }

    void UseXInput(XInputSetState_t setState)
    {
        EnsureThread();
        EnterCriticalSection(&g_lock);
        g_xinputSet = setState;
        if (setState)
        {
            CloseHid();
            g_kind = Kind::XInput;
        }
        else if (g_kind == Kind::XInput)
            g_kind = Kind::None;
        LeaveCriticalSection(&g_lock);
    }

    void UseHid(const wchar_t* devicePath, WORD vid, WORD pid, const wchar_t* name)
    {
        EnsureThread();
        // Sony: DualShock 4 (erste und zweite Ausgabe, Funkadapter) und DualSense (auch Edge)
        const bool ds4 = vid == 0x054C && (pid == 0x05C4 || pid == 0x09CC || pid == 0x0BA0);
        const bool dualSense = vid == 0x054C && (pid == 0x0CE6 || pid == 0x0DF2);
        std::wstring path = devicePath ? devicePath : L"";
        for (wchar_t& ch : path)
            ch = (wchar_t)towlower(ch);
        // Bluetooth-Geräte tragen die Kennung des HID-Dienstes im Pfad
        const bool bluetooth = path.find(L"00001124-0000-1000-8000-00805f9b34fb") != std::wstring::npos;

        EnterCriticalSection(&g_lock);
        if (g_kind != Kind::XInput)
        {
            CloseHid();
            g_kind = Kind::None;
            if (devicePath && (ds4 || dualSense))
            {
                g_hid = CreateFileW(devicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (g_hid == INVALID_HANDLE_VALUE)
                    g_hid = CreateFileW(devicePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (g_hid != INVALID_HANDLE_VALUE)
                    g_kind = ds4 ? (bluetooth ? Kind::Ds4Bluetooth : Kind::Ds4Usb) : (bluetooth ? Kind::DualSenseBluetooth : Kind::DualSenseUsb);
            }
        }
        const Kind kind = g_kind;
        LeaveCriticalSection(&g_lock);
        if (!devicePath)
            return;
        if (kind == Kind::None)
            LOG("Vibration: %ls wird nicht unterstützt%s", name ? name : L"Controller",
                ds4 || dualSense ? " (Gerät ließ sich nicht zum Schreiben öffnen)" : " (nur Xbox- und PS4/PS5-Pads)");
        else if (kind != Kind::XInput)
            LOG("Vibration: %ls über %s", name ? name : L"Controller", bluetooth ? "Bluetooth" : "USB");
    }

    void Tick(bool active, int strength)
    {
        if (!g_wake)
            return;
        gta1::Feedback fb;
        const bool have = gta1::GetFeedback(fb);
        const unsigned level = gta1::LevelLoads();
        // Vergleichen nur innerhalb desselben Levels
        if (have && g_havePrev && level == g_level)
        {
            // Treffer: Brennen kostet je Schritt einen Punkt (leichtes Dauerrütteln), ein Faustschlag etwa 10,
            // eine Kugel deutlich mehr; der Tod zählt als voller Schlag
            const int hurt = g_prev.health - fb.health;
            if (hurt > 0 && g_prev.health > 0)
            {
                Shake(0.25f + std::min(1.0f, hurt / 50.0f) * 0.75f, 0.30f, 140 + std::min(260, hurt * 5));
                if (g_cfg.showInfo && hurt > 1)
                    LOG("Vibration: Treffer, Lebensenergie -%d", hurt);
            }
            if (fb.shots != g_prev.shots)
            {
                const Kick k = WeaponKick(fb.weapon);
                Shake(k.strong, k.weak, k.ms);
                if (g_cfg.showInfo)
                    LOG("Vibration: Schuss mit Waffe %d", fb.weapon);
            }
            // Schaden am eigenen Fahrzeug (0..100, bei 100 explodiert es)
            const int damage = fb.carDamage - g_prev.carDamage;
            if (fb.car >= 0 && fb.car == g_prev.car && damage > 0)
            {
                const float hit = std::min(1.0f, damage / 20.0f);
                Shake(0.30f + 0.70f * hit, 0.20f + 0.40f * hit, 120 + (int)(280 * hit));
                if (g_cfg.showInfo)
                    LOG("Vibration: Fahrzeugschaden +%d", damage);
            }
        }
        g_prev = fb;
        g_havePrev = have;
        g_level = level;

        const DWORD now = GetTickCount();
        if ((int)(g_testUntil - now) > 0)
        {
            SetTarget(1.0f, 1.0f);
            return;
        }
        const float scale = active && have ? std::min(100, std::max(0, strength)) / 100.0f : 0.0f;
        if (scale <= 0)
        {
            g_strong = Pulse();
            g_weak = Pulse();
        }
        SetTarget((int)(g_strong.until - now) > 0 ? g_strong.level * scale : 0.0f,
                  (int)(g_weak.until - now) > 0 ? g_weak.level * scale : 0.0f);
    }
}

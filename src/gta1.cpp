#include "gta1.h"
#include "hooks.h"
#include "log.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace gta1
{
    // Adressen für GTA.exe 1.0.0.1 (Windows-Version vom 10.10.1997), per Disassembler ermittelt
    namespace addr
    {
        constexpr uintptr_t HudDraw       = 0x46DBB0; // zeichnet HUD; läuft jeden Frame im Spiel, auch pausiert
        constexpr uintptr_t HudCallJmp    = 0x42F002; // jmp HudDraw
        constexpr uintptr_t HudCallCall   = 0x42F02B; // call HudDraw
        constexpr uintptr_t GetPlayerPos  = 0x463FF0; // GtaPos* __cdecl(int player), je nach Steuerungstyp Auto/Zug/Fußgänger
        constexpr uintptr_t PauseOn       = 0x46B9C0; // setzt "paused"-Anzeige
        constexpr uintptr_t PauseOff      = 0x46B9E0;
        constexpr uintptr_t SoundPause    = 0x4583B0;
        constexpr uintptr_t SoundResume   = 0x4583E0;
        constexpr uintptr_t PauseKeyCase  = 0x42F30A; // F6-Handler, liest NumPlayers und RunState
        constexpr uintptr_t DebugCoords   = 0x46DBF0; // Debug-Anzeige "%s ID %d at (%d,%d,%d)", liest LocalPlayer
        constexpr uintptr_t QuitLevel     = 0x42EB60; // wie Enter in der Beenden-Abfrage des Spiels (0x46D701)
        constexpr uintptr_t ArrowResolve  = 0x470A90; // GtaPos* __cdecl(ArrowTarget*), Typ 0 Auto, 1 Position, 3 Fußgänger, 4 Spieler

        constexpr uintptr_t ArrowMainOn   = 0x677F0C; // Missionspfeil aktiv (Befehle ARROW/ARROWCAR/ARROWPED)
        constexpr uintptr_t ArrowMain     = 0x678418; // ArrowTarget des Missionspfeils
        constexpr uintptr_t ArrowRedOn    = 0x678400; // roter Pfeil aktiv (RED_ARROW)
        constexpr uintptr_t ArrowRed      = 0x678DD8;

        constexpr uintptr_t RenderFrame   = 0x42EFE0; // zeichnet Welt (0x428B40) und HUD
        constexpr uintptr_t RenderCall    = 0x45D057; // call RenderFrame in der Hauptschleife, nach der Simulation
        constexpr uintptr_t PedFire       = 0x4470B0; // Fußgänger feuert, die Waffen lesen die Richtung aus Ped+0x99
        constexpr uintptr_t PedFireCall   = 0x451AA5; // call PedFire in der Fußgängersteuerung
        constexpr uintptr_t PedStruct     = 0x600C48; // Beginn der Fußgänger-Struktur (Blickrichtung bei +0x99)

        // Kameraführung 0x42B910 (je Spiel-Frame nach der Simulation): glättet in 0x65760D + Spieler * 417,
        // schreibt den Bildmittelpunkt in die Kamera (CameraData) und berechnet daraus in 0x42BB40 den
        // Sichtbereich für die Spiellogik (0x6575CC). Zeichnen und Ton lesen danach die Kamera (0x42B780/0x42B7E0).
        constexpr uintptr_t Camera        = 0x6575E0; // CameraData je Spieler (Abstand wie die Spieler-Struktur)
        constexpr uintptr_t ViewRect      = 0x42BB40; // void __cdecl(int player)
        constexpr uintptr_t ViewRectCall  = 0x42BB0B; // call ViewRect am Ende der Kameraführung

        constexpr uintptr_t ReadInput     = 0x4365E0; // liefert das Eingabepaket des lokalen Spielers
        constexpr uintptr_t ReadInputCall = 0x45CFD3; // call ReadInput in der Hauptschleife
        constexpr uintptr_t InSteer       = 0x600A80; // int8 -7..7 (Links/Rechts)
        constexpr uintptr_t InGas         = 0x6008FC; // 0..3
        constexpr uintptr_t InBrake       = 0x600A18; // 0..3
        constexpr uintptr_t InJump        = 0x600900; // Handbremse/Springen
        constexpr uintptr_t InFire        = 0x6008F8; // Feuern/Hupe (gehalten)
        constexpr uintptr_t InNext        = 0x600A1C; // Waffe vor
        constexpr uintptr_t InPrev        = 0x600A48; // Waffe zurück
        constexpr uintptr_t InSpecial     = 0x600A84;
        constexpr uintptr_t PedAngle      = 0x600CE1; // int16, Fußgänger-Struktur 235 Byte
        constexpr uintptr_t PedRemap      = 0x600CA0; // Farbvariante 0..63 (REMAP_PED -> 0x454B00)
        constexpr uintptr_t PedDrawRemap  = 0x600CDB; // dieselbe Farbvariante in den Zeichendaten
        constexpr int       PedStride     = 235;
        constexpr uintptr_t PlayerCharacter = 0x657764; // byte, im Hauptmenü gewähltes Portrait (0 Ulrika .. 7 Kivlov)
        constexpr uintptr_t CharacterColors = 0x4C1668; // byte [Charakter * 4 + Spieler]: Farbnummer wie in mission.ini (0x462150)
        constexpr uintptr_t ColorToRemap    = 0x4C0110; // byte [Farbnummer]: Farbvariante 0..63

        // Fußgänger (0x442BE0: PedStruct + ID * 235): +0 ID, +0x1B 1 = Spielfigur, +0x44 Lebensenergie 0..100,
        // +0x52 Handle der Flammen an der Figur (-1 = brennt nicht), +0x66 Zustand
        constexpr int       PedId         = 0x00;
        constexpr int       PedAnim       = 0x17;     // int16 Animationsbild (0x88 steht, 0xAF..0xB2 fällt, 0xE9..0xED liegt)
        constexpr int       PedHealth     = 0x44;
        constexpr int       PedFlames     = 0x52;
        constexpr int       PedPose       = 0x66;     // int32 Zustand: 2 zu Fuß, 0x15 getroffen/stirbt, 0x17 tot, 0x18 im Wasser
        constexpr int       PedAngleOfs   = 0x99;
        constexpr uintptr_t PedSprite     = 0x442790; // void __cdecl(Ped*): Sprite aus Ped+0x17 bestimmen (für das Zeichnen)
        constexpr uintptr_t BurnDamage    = 0x451AF7; // cmp word [esi+52h],-1 / je / dec byte [esi+44h]: Brennschaden je Frame
        constexpr uintptr_t KeyScancodes  = 0x600A50; // int je Steuerung (Scancode, +0x100 erweitert), 4 = Springen

        // Flammen an Figuren: 0x41CB30/0x404FD0 erzeugen mit ObjectCreate ein o_lfire (0x2E) an der Figur und
        // merken das Handle in Ped+0x52; solange es gesetzt ist, zieht 0x451AFE jeden Frame 1 Lebenspunkt ab.
        // Endet das Objekt, löst 0x41D200 es von der Figur (Ped+0x52 = -1).
        constexpr uintptr_t ObjectCreate  = 0x41BAA0; // int16 __cdecl(x, y, z, Typ, Besitzer-ID, Besitzerart 1 Auto/2 Fußgänger)
        constexpr uintptr_t ObjectDelete  = 0x41D380; // void __cdecl(int16 handle), wie beim Ablauf eines Objekts (0x41CE51)
        constexpr uintptr_t Objects       = 0x50F5F8; // Objekt-Plätze, 130 Byte: +0 Handle, +0x0A Typ, +0x0C belegt, +0x14 Besitzer
        constexpr int       ObjectSize    = 130;
        constexpr int       ObjectCount   = 0xDAC;
        constexpr int16_t   ObjFlames     = 0x2E;     // o_lfire

        constexpr uintptr_t LocalPlayer   = 0x657C50; // int
        constexpr uintptr_t NumPlayers    = 0x657C4C; // int, 1 = Einzelspieler
        constexpr uintptr_t PlayerPedId   = 0x6575CA; // int16 ID der Spielfigur, auch im Fahrzeug
        constexpr uintptr_t PlayerCtrl    = 0x657695; // int Steuerungstyp, Spieler-Struktur 417 Byte
        constexpr uintptr_t PlayerObject  = 0x657699; // Index des gesteuerten Objekts
        constexpr int       PlayerStride  = 417;
        constexpr uintptr_t RunState      = 0x5F3A90; // 1 = läuft, 2 = pausiert

        constexpr uintptr_t MultHalve     = 0x464649; // shr ax,1: Multiplikator halbieren (Strafe bei Verhaftung, 0x464570)

        // Hauptmenü: Außer in der englischen Sprachversion überspringt die Charakterauswahl die Frauen
        // (0 Ulrika, 2 Katie, 3 Mikki, 4 Divine); 0x4201B0 liefert "nicht Englisch"
        constexpr uintptr_t NotEnglish     = 0x4201B0;
        constexpr uintptr_t PickPrevCheck  = 0x40991E; // call NotEnglish nach "Charakter zurück"
        constexpr uintptr_t PickNextCheck  = 0x409976; // call NotEnglish nach "Charakter vor"

        // Missionsskript (mission.ini): 0x482250 führt eine Zeile des laufenden Prozesses aus
        constexpr uintptr_t ScriptDispatch = 0x4822EA; // jmp [ebx*4+ScriptJumps], ebx = Befehl, ecx = Skriptzeile
        constexpr uintptr_t ScriptJumps    = 0x487FDC;
        constexpr uintptr_t ScriptSurvive  = 0x476120; // Befehl SURVIVE
        constexpr uintptr_t ScriptProc     = 0x736A08; // int16 laufender Prozess
        constexpr uintptr_t ScriptState    = 0x6DB660; // int16 je Prozess, 1 = Zeile läuft zum ersten Mal
        constexpr uintptr_t ScriptActive   = 0x6FB768; // int16 je Prozess, 0 = frei
        constexpr int       ScriptProcs    = 32;
        constexpr uintptr_t ScriptObjSlot  = 0x6DB768; // int16 je Zeilennummer: Platz in der Objekttabelle
        constexpr uintptr_t ScriptObjects  = 0x6796F8; // Objekte aus mission.ini, 29 Byte: +0 Typ, +4 Wert/Handle
        constexpr int       ScriptObjSize  = 29;

        constexpr uintptr_t MglPrimary    = 0x4C2FEC; // IDirectDrawSurface* (Primary von MGL)
        constexpr uintptr_t MglPageCount  = 0x4C2FE8; // Anzahl Seiten der Flip-Kette

        constexpr uintptr_t IatGetProcAddress = 0x787474;
        constexpr uintptr_t IatPeekMessageA   = 0x78754C;
        constexpr uintptr_t LoadGetProcAddress[] = { 0x49AFC3 };                     // holt DirectDrawCreate
        constexpr uintptr_t LoadPeekMessageA[]   = { 0x43082C, 0x49A2DC, 0x49C54B }; // Nachrichtenschleifen
    }

#pragma pack(push, 1)
    struct GtaPos
    {
        int32_t x, y, z;        // Festkomma, 22 Nachkommabits = 1 Block
        int16_t w, h;
        int16_t speed;
        int16_t angle;
    };

    // Eine Zeile aus mission.ini: "<nr> BEFEHL <obj> <succ> <fail> <data> <score>"
    struct ScriptLine
    {
        uint16_t command;       // Index in der Befehlstabelle 0x4C26B8
        int32_t obj, succ, fail, data, score;
    };

    // Kamera eines Spielers. Welt -> Bildschirm (0x42B630):
    //   Bild = center + (Welt - Kamera) * focal / (height + z), alles in Pixeln (64 je Block)
    struct CameraData
    {
        int32_t centerX, centerY;   // Bildmitte in der Spielauflösung
        int32_t baseW, baseH;       // Bezugsgröße für den Zoom (800x600)
        int32_t baseW16, baseH16;
        int32_t height;             // Kamerahöhe über der Welt-Oberkante
        int32_t x, y;               // Bildmittelpunkt in der Welt
        int32_t focal;
        int32_t zoom;
        uint8_t squash;             // 1 = senkrecht auf 5/6 gestaucht (Bezugsgröße nicht 4:3)
    };
#pragma pack(pop)
    static_assert(sizeof(ScriptLine) == 22, "Skriptzeile wie im Spiel");

    static bool s_supported = false;
    static volatile DWORD s_lastHud = 0;
    static volatile unsigned s_hudCount = 0;
    static void (*s_hudCallback)() = nullptr;

    static CRITICAL_SECTION s_mapLock;
    static std::string s_mapPath;
    static volatile unsigned s_mapGen = 0;

    static HANDLE (WINAPI* s_origCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;

    static uint32_t (*s_inputFilter)(uint32_t) = nullptr;
    static void (*s_preRender)() = nullptr;
    static int s_fireAngle = -1;
    static void (*s_cameraFilter)(const CameraView*, float&, float&) = nullptr;

    static int PlayerPed();
    static const GtaPos* ControlledPos(int player, int& type);

    static CameraData& CameraOf(int player)
    {
        return *reinterpret_cast<CameraData*>(addr::Camera + player * addr::PlayerStride);
    }

    // Zwischen Kameraführung und Sichtbereich: Versatz der Kamerasteuerung aufschlagen. Die Glättung
    // des Spiels rechnet mit dem Mittelpunkt ohne unseren Versatz weiter, der Sichtbereich passt zum Bild.
    static void __cdecl ViewRectHook(int player)
    {
        if (s_cameraFilter && player == *reinterpret_cast<int*>(addr::LocalPlayer))
        {
            int type = -1;
            const GtaPos* p = ControlledPos(player, type);
            CameraData& cam = CameraOf(player);
            float dx = 0, dy = 0;
            if (p && type <= 2 && cam.focal > 0 && cam.height + (p->z >> 16) > 0)
            {
                const CameraView v = { (float)cam.x, (float)cam.y, p->x / 65536.0f, p->y / 65536.0f, p->z / 65536.0f,
                                       type == 2 ? 10.0f : 24.0f, (float)cam.height, (float)cam.focal,
                                       cam.squash ? 5.0f / 6.0f : 1.0f, (float)cam.centerX, (float)cam.centerY };
                s_cameraFilter(&v, dx, dy);
                cam.x = std::clamp(cam.x + (int)std::lround(dx), 0, 0x3FFF);
                cam.y = std::clamp(cam.y + (int)std::lround(dy), 0, 0x3FFF);
            }
            else
                s_cameraFilter(nullptr, dx, dy);
        }
        reinterpret_cast<void(__cdecl*)(int)>(addr::ViewRect)(player);
    }

    static void __cdecl RenderHook()
    {
        if (s_preRender)
            s_preRender();
        reinterpret_cast<void(__cdecl*)()>(addr::RenderFrame)();
    }

    // Beim Seitwärtslaufen zeigt der Spieler während der Simulation in Laufrichtung;
    // nur für den Schuss wird er kurz zum Fadenkreuz gedreht
    static void __cdecl PedFireHook(uint8_t* ped)
    {
        const auto fire = reinterpret_cast<void(__cdecl*)(uint8_t*)>(addr::PedFire);
        const int player = s_fireAngle >= 0 ? PlayerPed() : -1;
        if (player < 0 || ped != reinterpret_cast<uint8_t*>(addr::PedStruct + player * addr::PedStride))
        {
            fire(ped);
            return;
        }
        int16_t* angle = reinterpret_cast<int16_t*>(addr::PedAngle + player * addr::PedStride);
        const int16_t walk = *angle;
        *angle = (int16_t)(s_fireAngle & 1023);
        fire(ped);
        *angle = walk;
    }

    static void __cdecl HudHook()
    {
        s_lastHud = GetTickCount();
        ++s_hudCount;
        reinterpret_cast<void(__cdecl*)()>(addr::HudDraw)();
        if (s_hudCallback)
            s_hudCallback();
    }

    static uint32_t __cdecl InputHook()
    {
        const uint32_t packet = reinterpret_cast<uint32_t(__cdecl*)()>(addr::ReadInput)();
        return s_inputFilter ? s_inputFilter(packet) : packet;
    }

    // Merkt sich, welche Stadtkarte das Spiel lädt
    static HANDLE WINAPI Hook_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disposition, DWORD flags, HANDLE templ)
    {
        HANDLE h = s_origCreateFileA(name, access, share, sa, disposition, flags, templ);
        if (h != INVALID_HANDLE_VALUE && name)
        {
            const size_t len = strlen(name);
            if (len > 4 && _stricmp(name + len - 4, ".cmp") == 0)
            {
                char full[MAX_PATH];
                if (!GetFullPathNameA(name, MAX_PATH, full, nullptr))
                    strcpy_s(full, name);
                EnterCriticalSection(&s_mapLock);
                if (_stricmp(s_mapPath.c_str(), full) != 0)
                {
                    s_mapPath = full;
                    ++s_mapGen;
                    LOG("Spiel lädt Karte %s", full);
                }
                LeaveCriticalSection(&s_mapLock);
            }
        }
        return h;
    }

    static bool CheckSignatures()
    {
        struct Sig { uintptr_t addr; uint8_t bytes[10]; size_t len; };
        static const Sig sigs[] = {
            { addr::HudDraw,      { 0x53, 0xA1, 0x74, 0x50, 0x65, 0x00 }, 6 },
            { addr::GetPlayerPos, { 0x8B, 0x44, 0x24, 0x04, 0x56, 0x33, 0xF6, 0x8D, 0x0C, 0x40 }, 10 },
            { addr::PauseOn,      { 0xC6, 0x05, 0x8C, 0x83, 0x67, 0x00, 0x01 }, 7 },
            { addr::PauseOff,     { 0xC6, 0x05, 0x8C, 0x83, 0x67, 0x00, 0x00, 0xC3 }, 8 },
            { addr::SoundPause,   { 0xA0, 0x38, 0x02, 0x4C, 0x00, 0x84, 0xC0 }, 7 },
            { addr::SoundResume,  { 0xA0, 0x38, 0x02, 0x4C, 0x00, 0x84, 0xC0 }, 7 },
            { addr::PauseKeyCase, { 0xA1, 0x4C, 0x7C, 0x65, 0x00, 0x83, 0xF8, 0x01 }, 8 },
            { addr::PauseKeyCase + 0x0E, { 0xA1, 0x90, 0x3A, 0x5F, 0x00, 0x83, 0xF8, 0x01 }, 8 },
            { addr::DebugCoords,  { 0xA1, 0x50, 0x7C, 0x65, 0x00, 0x50, 0xE8 }, 7 },
            { addr::QuitLevel,    { 0xC7, 0x05, 0x7C, 0x3A, 0x5F, 0x00, 0x02, 0x00, 0x00, 0x00 }, 10 },
            { addr::ArrowResolve, { 0x8B, 0x4C, 0x24, 0x04, 0x56, 0x57, 0x8B, 0x01, 0x83, 0xF8 }, 10 },
            { 0x436AC0,           { 0xA0, 0x88, 0x0A, 0x60, 0x00 }, 5 },  // Handler Steuerung 6 (Ein-/Aussteigen)
            { 0x436A99,           { 0xA0, 0xF8, 0x08, 0x60, 0x00 }, 5 },  // Handler Steuerung 5 (Feuern) liest InFire
            { 0x452B9E,           { 0x0F, 0xBF, 0x85, 0xE1, 0x0C, 0x60, 0x00 }, 7 }, // Fußgänger-Bewegung liest PedAngle
        };
        for (const Sig& s : sigs)
        {
            if (!hooks::BytesMatch(s.addr, s.bytes, s.len))
            {
                LOG("GTA.exe: Signatur bei 0x%06X passt nicht", (unsigned)s.addr);
                return false;
            }
        }
        return true;
    }

    bool Init(HMODULE exe)
    {
        InitializeCriticalSection(&s_mapLock);

        s_origCreateFileA = reinterpret_cast<decltype(s_origCreateFileA)>(
            hooks::PatchImport(exe, "KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(&Hook_CreateFileA)));
        if (!s_origCreateFileA)
            LOG("CreateFileA-Import nicht gefunden - Karte wird nicht erkannt");

        if (reinterpret_cast<uintptr_t>(exe) != 0x400000 || !CheckSignatures())
        {
            LOG("Unbekannte GTA.exe - Spielerposition und Pause deaktiviert");
            return false;
        }

        const bool a = hooks::RedirectRel32(addr::HudCallJmp, 0xE9, addr::HudDraw, reinterpret_cast<void*>(&HudHook));
        const bool b = hooks::RedirectRel32(addr::HudCallCall, 0xE8, addr::HudDraw, reinterpret_cast<void*>(&HudHook));
        if (!a || !b)
        {
            LOG("HUD-Hook fehlgeschlagen (jmp %d, call %d)", a, b);
            return false;
        }
        if (!hooks::RedirectRel32(addr::ReadInputCall, 0xE8, addr::ReadInput, reinterpret_cast<void*>(&InputHook)))
            LOG("Eingabe-Hook fehlgeschlagen - keine Twin-Stick-/Controller-Steuerung");
        if (!hooks::RedirectRel32(addr::RenderCall, 0xE8, addr::RenderFrame, reinterpret_cast<void*>(&RenderHook)))
            LOG("Zeichen-Hook fehlgeschlagen - kein Seitwärtslaufen und keine Portraitfarben");
        if (!hooks::RedirectRel32(addr::PedFireCall, 0xE8, addr::PedFire, reinterpret_cast<void*>(&PedFireHook)))
            LOG("Schuss-Hook fehlgeschlagen - beim Seitwärtslaufen wird in Laufrichtung geschossen");

        s_supported = true;
        LOG("GTA.exe 1.0.0.1 erkannt, Spiel-Hooks aktiv");
        return true;
    }

    ImportLoads GetProcAddressLoads()
    {
        return { addr::IatGetProcAddress, addr::LoadGetProcAddress, sizeof(addr::LoadGetProcAddress) / sizeof(uintptr_t) };
    }

    ImportLoads PeekMessageLoads()
    {
        return { addr::IatPeekMessageA, addr::LoadPeekMessageA, sizeof(addr::LoadPeekMessageA) / sizeof(uintptr_t) };
    }

    bool Supported() { return s_supported; }
    void SetHudCallback(void (*cb)()) { s_hudCallback = cb; }
    unsigned HudCount() { return s_hudCount; }
    void* PrimarySurface() { return s_supported ? *reinterpret_cast<void**>(addr::MglPrimary) : nullptr; }
    int PageCount() { return s_supported ? *reinterpret_cast<int*>(addr::MglPageCount) : 0; }
    bool InGame() { return s_supported && s_hudCount && GetTickCount() - s_lastHud < 500; }

    // Position dessen, was der Spieler steuert (Auto, Zug, Fußgänger ...) bzw. nullptr
    static const GtaPos* ControlledPos(int player, int& type)
    {
        type = -1;
        if (player < 0 || player > 3)
            return nullptr;

        // Plausibilitätsprüfung, damit die Spielfunktion nie außerhalb ihrer Tabellen liest
        const int t = *reinterpret_cast<int*>(addr::PlayerCtrl + player * addr::PlayerStride);
        const int object = *reinterpret_cast<int*>(addr::PlayerObject + player * addr::PlayerStride);
        if (t < 0 || t > 5)
            return nullptr;
        if (t == 0 && (object & 0xFFFF) >= 400)
            return nullptr;
        if (t == 2 && (object < 0 || object >= 4000))
            return nullptr;

        const GtaPos* p = reinterpret_cast<GtaPos*(__cdecl*)(int)>(addr::GetPlayerPos)(player);
        if (p)
            type = t;
        return p;
    }

    bool GetPlayer(Player& out)
    {
        if (!InGame())
            return false;
        int type = -1;
        const GtaPos* p = ControlledPos(*reinterpret_cast<int*>(addr::LocalPlayer), type);
        if (!p)
            return false;

        out.x = p->x / 4194304.0f;
        out.y = p->y / 4194304.0f;
        out.z = p->z / 4194304.0f;
        out.angle = p->angle;
        out.speed = p->speed;
        out.ctrlType = type;
        return out.x >= 0 && out.x < 256 && out.y >= 0 && out.y < 256;
    }

    bool IsSinglePlayer() { return s_supported && *reinterpret_cast<int*>(addr::NumPlayers) == 1; }
    int RunState() { return s_supported ? *reinterpret_cast<int*>(addr::RunState) : 0; }

    // Entspricht dem F6-Handler des Spiels (0x42F318)
    void Pause()
    {
        reinterpret_cast<void(__cdecl*)()>(addr::PauseOn)();
        *reinterpret_cast<int*>(addr::RunState) = 2;
        reinterpret_cast<void(__cdecl*)()>(addr::SoundPause)();
    }

    void Resume()
    {
        reinterpret_cast<void(__cdecl*)()>(addr::PauseOff)();
        *reinterpret_cast<int*>(addr::RunState) = 1;
        reinterpret_cast<void(__cdecl*)()>(addr::SoundResume)();
    }

    void QuitLevel()
    {
        if (s_supported)
            reinterpret_cast<void(__cdecl*)()>(addr::QuitLevel)();
    }

    bool GetArrowTarget(int which, float& x, float& y)
    {
        if (!InGame())
            return false;
        const uintptr_t on = which ? addr::ArrowRedOn : addr::ArrowMainOn;
        const uintptr_t target = which ? addr::ArrowRed : addr::ArrowMain;
        if (!*reinterpret_cast<int*>(on))
            return false;

        // Nur Typen zulassen, für die 0x470A90 keinen Fatal Error auslöst, und Indizes grob prüfen
        const int type = *reinterpret_cast<int*>(target);
        const int16_t index = *reinterpret_cast<int16_t*>(target + 4);
        if ((type == 0 && (uint16_t)index >= 400) || (type == 3 && (index < 0 || index >= 4000)) ||
            (type == 4 && (index < 0 || index > 3)) || (type != 0 && type != 1 && type != 3 && type != 4))
            return false;

        const GtaPos* p = reinterpret_cast<GtaPos*(__cdecl*)(uintptr_t)>(addr::ArrowResolve)(target);
        if (!p)
            return false;
        x = p->x / 4194304.0f;
        y = p->y / 4194304.0f;
        return x >= 0 && x < 256 && y >= 0 && y < 256;
    }

    KeyboardInput GetKeyboardInput()
    {
        KeyboardInput k = {};
        if (!s_supported)
            return k;
        k.steer = *reinterpret_cast<int8_t*>(addr::InSteer);
        k.gas = *reinterpret_cast<uint8_t*>(addr::InGas);
        k.brake = *reinterpret_cast<uint8_t*>(addr::InBrake);
        k.jump = *reinterpret_cast<uint8_t*>(addr::InJump) != 0;
        k.fire = *reinterpret_cast<uint8_t*>(addr::InFire) != 0;
        k.next = *reinterpret_cast<uint8_t*>(addr::InNext) != 0;
        k.prev = *reinterpret_cast<uint8_t*>(addr::InPrev) != 0;
        k.special = *reinterpret_cast<uint8_t*>(addr::InSpecial) != 0;
        return k;
    }

    void SetInputFilter(uint32_t (*filter)(uint32_t)) { s_inputFilter = filter; }

    // Index des gesteuerten Fußgängers oder -1
    static int PlayerPed()
    {
        const int idx = *reinterpret_cast<int*>(addr::LocalPlayer);
        if (idx < 0 || idx > 3 || *reinterpret_cast<int*>(addr::PlayerCtrl + idx * addr::PlayerStride) != 2)
            return -1;
        const int ped = *reinterpret_cast<int*>(addr::PlayerObject + idx * addr::PlayerStride);
        return ped >= 0 && ped < 4000 ? ped : -1;
    }

    bool IsPlayerOnFoot() { return InGame() && PlayerPed() >= 0; }

    // Spielfigur des lokalen Spielers, auch während sie im Fahrzeug sitzt, bzw. nullptr
    static uint8_t* PlayerPedStruct()
    {
        if (!InGame())
            return nullptr;
        const int idx = *reinterpret_cast<int*>(addr::LocalPlayer);
        if (idx < 0 || idx > 3)
            return nullptr;
        const int16_t id = *reinterpret_cast<int16_t*>(addr::PlayerPedId + idx * addr::PlayerStride);
        if (id < 0 || id >= 4000)
            return nullptr;
        return reinterpret_cast<uint8_t*>(addr::PedStruct + id * addr::PedStride);
    }

    int PlayerHealth()
    {
        const uint8_t* ped = PlayerPedStruct();
        return ped ? std::clamp<int>(static_cast<int8_t>(ped[addr::PedHealth]), 0, 100) : -1;
    }

    static int16_t& PedFlames(uint8_t* ped) { return *reinterpret_cast<int16_t*>(ped + addr::PedFlames); }

    bool IsPlayerBurning()
    {
        uint8_t* ped = PlayerPedStruct();
        return ped && PedFlames(ped) > 0 && static_cast<int8_t>(ped[addr::PedHealth]) > 0;
    }

    // Steht das Objekt in der Liste, die 0x41D200 beim Entfernen durchsucht? (sonst bricht das Spiel ab)
    static bool InActiveObjectList(int16_t handle)
    {
        const uint8_t* o = *reinterpret_cast<uint8_t* const*>(0x57E750);
        for (int guard = 0; o && guard < addr::ObjectCount; ++guard)
        {
            if (*reinterpret_cast<const int16_t*>(o) == handle)
                return true;
            o = *reinterpret_cast<uint8_t* const*>(o + 0x22);
        }
        return false;
    }

    bool ExtinguishPlayer()
    {
        uint8_t* ped = PlayerPedStruct();
        if (!ped)
            return false;
        const int16_t flames = PedFlames(ped);
        if (flames <= 0 || flames >= addr::ObjectCount)
            return false;

        // Nur die Flammen genau dieser Figur entfernen, wie beim Ablauf des Objekts im Spiel (0x41CE51)
        const uint8_t* obj = reinterpret_cast<const uint8_t*>(addr::Objects + flames * addr::ObjectSize);
        const auto word = [obj](int offset) { return *reinterpret_cast<const int16_t*>(obj + offset); };
        if (word(0x00) == flames && word(0x0A) == addr::ObjFlames && word(0x0C) != 0 &&
            word(0x14) == *reinterpret_cast<int16_t*>(ped + addr::PedId) && word(0x18) == 2 && InActiveObjectList(flames))
            reinterpret_cast<void(__cdecl*)(int)>(addr::ObjectDelete)(flames);
        PedFlames(ped) = -1;
        return true;
    }

    // Brennschaden: statt "cmp word ptr [esi+52h],-1" ruft das Spiel BurnCheck auf. Für die Figur in
    // s_noBurnPed meldet der Vergleich "brennt nicht" (ZF = 1), das folgende je überspringt den Abzug.
    static const uint8_t* volatile s_noBurnPed = nullptr;

    __declspec(naked) static void BurnCheck()
    {
        __asm
        {
            cmp word ptr [esi + 0x52], 0xFFFF
            je done
            cmp esi, s_noBurnPed
        done:
            ret
        }
    }

    bool EnableBurnPause()
    {
        static const uint8_t expect[] = { 0x66, 0x83, 0x7E, 0x52, 0xFF, 0x74, 0x11, 0xFE, 0x4E, 0x44 };
        if (!s_supported || !hooks::BytesMatch(addr::BurnDamage, expect, sizeof(expect)))
            return false;
        uint8_t patch[5] = { 0xE8 };
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&BurnCheck) - (addr::BurnDamage + 5));
        memcpy(patch + 1, &rel, 4);
        return hooks::WriteMemory(reinterpret_cast<void*>(addr::BurnDamage), patch, sizeof(patch));
    }

    void SetPlayerBurnPaused(bool paused)
    {
        const int idx = paused ? PlayerPed() : -1;
        s_noBurnPed = idx >= 0 ? reinterpret_cast<const uint8_t*>(addr::PedStruct + idx * addr::PedStride) : nullptr;
    }

    bool IsPlayerDead()
    {
        const uint8_t* ped = PlayerPedStruct();
        const int32_t pose = ped ? *reinterpret_cast<const int32_t*>(ped + addr::PedPose) : 0;
        return !ped || static_cast<int8_t>(ped[addr::PedHealth]) <= 0 || pose == 0x15 || pose == 0x17;
    }

    bool GetPlayerPedAngle(int& angle)
    {
        const int ped = s_supported ? PlayerPed() : -1;
        if (ped < 0)
            return false;
        angle = *reinterpret_cast<int16_t*>(addr::PedAngle + ped * addr::PedStride) & 1023;
        return true;
    }

    void ShowPlayerPose(int anim, int angle)
    {
        const int idx = s_supported ? PlayerPed() : -1;
        if (idx < 0)
            return;
        uint8_t* ped = reinterpret_cast<uint8_t*>(addr::PedStruct + idx * addr::PedStride);
        int16_t& current = *reinterpret_cast<int16_t*>(ped + addr::PedAnim);
        const int16_t keep = current;
        if (anim >= 0)
            current = static_cast<int16_t>(anim);
        if (angle >= 0)
            *reinterpret_cast<int16_t*>(ped + addr::PedAngleOfs) = static_cast<int16_t>(angle & 1023);
        reinterpret_cast<void(__cdecl*)(uint8_t*)>(addr::PedSprite)(ped);
        current = keep;
    }

    int JumpKeyScancode()
    {
        return s_supported ? *reinterpret_cast<int*>(addr::KeyScancodes + 4 * 4) : 0;
    }

    bool IgnitePlayer()
    {
        const int idx = PlayerPed();
        if (!InGame() || idx < 0)
            return false;
        uint8_t* ped = reinterpret_cast<uint8_t*>(addr::PedStruct + idx * addr::PedStride);
        if (PedFlames(ped) > 0)
            return false;
        // wie 0x41CB30: Flammen etwas unterhalb der Figur erzeugen und an ihr festmachen
        const int32_t x = *reinterpret_cast<int32_t*>(ped + 0x83);
        const int32_t y = *reinterpret_cast<int32_t*>(ped + 0x87);
        const int32_t z = *reinterpret_cast<int32_t*>(ped + 0x8B);
        using Create = int(__cdecl*)(int32_t, int32_t, int32_t, int, int, int);
        const int16_t h = static_cast<int16_t>(reinterpret_cast<Create>(addr::ObjectCreate)(
            x, y, z - 0x10000, addr::ObjFlames, *reinterpret_cast<int16_t*>(ped + addr::PedId), 2));
        if (h <= 0)
            return false;
        PedFlames(ped) = h;
        return true;
    }

    void SetPlayerPedAngle(int angle)
    {
        const int ped = s_supported ? PlayerPed() : -1;
        if (ped >= 0)
            *reinterpret_cast<int16_t*>(addr::PedAngle + ped * addr::PedStride) = (int16_t)(angle & 1023);
    }

    void SetPreRenderCallback(void (*cb)()) { s_preRender = cb; }
    void SetPlayerFireAngle(int angle) { s_fireAngle = angle; }

    bool SetCameraFilter(void (*filter)(const CameraView* view, float& dx, float& dy))
    {
        if (!s_supported)
            return false;
        if (!s_cameraFilter &&
            !hooks::RedirectRel32(addr::ViewRectCall, 0xE8, addr::ViewRect, reinterpret_cast<void*>(&ViewRectHook)))
            return false;
        s_cameraFilter = filter;
        return true;
    }

    bool PlayerScreenPos(float& x, float& y)
    {
        if (!InGame())
            return false;
        const int idx = *reinterpret_cast<int*>(addr::LocalPlayer);
        int type = -1;
        const GtaPos* p = ControlledPos(idx, type);
        if (!p || type > 2)
            return false;
        const CameraData& cam = CameraOf(idx);
        const float depth = cam.height + p->z / 65536.0f;
        if (depth <= 0 || cam.focal <= 0)
            return false;
        const float k = cam.focal / depth;
        x = cam.centerX + (p->x / 65536.0f - cam.x) * k;
        y = cam.centerY + (p->y / 65536.0f - cam.y) * k * (cam.squash ? 5.0f / 6.0f : 1.0f);
        return true;
    }

    int PlayerCharacter()
    {
        const int idx = s_supported ? *reinterpret_cast<int*>(addr::LocalPlayer) : -1;
        if (idx < 0 || idx > 3)
            return -1;
        return *reinterpret_cast<uint8_t*>(addr::PlayerCharacter + idx * addr::PlayerStride);
    }

    int PlayerDefaultRemap()
    {
        const int idx = s_supported ? *reinterpret_cast<int*>(addr::LocalPlayer) : -1;
        const int character = PlayerCharacter();
        if (idx < 0 || idx > 3 || character < 0 || character > 7)
            return -1;
        const uint8_t color = *reinterpret_cast<uint8_t*>(addr::CharacterColors + character * 4 + idx);
        return *reinterpret_cast<uint8_t*>(addr::ColorToRemap + color);
    }

    bool GetPlayerPedRemap(int& remap)
    {
        const int ped = s_supported ? PlayerPed() : -1;
        if (ped < 0)
            return false;
        remap = *reinterpret_cast<uint8_t*>(addr::PedRemap + ped * addr::PedStride);
        return true;
    }

    void SetPlayerPedRemap(int remap)
    {
        const int ped = s_supported ? PlayerPed() : -1;
        if (ped < 0 || remap < 0 || remap > 63)
            return;
        *reinterpret_cast<uint8_t*>(addr::PedRemap + ped * addr::PedStride) = (uint8_t)remap;
        *reinterpret_cast<uint8_t*>(addr::PedDrawRemap + ped * addr::PedStride) = (uint8_t)remap;
    }

    std::string MapFile()
    {
        EnterCriticalSection(&s_mapLock);
        std::string p = s_mapPath;
        LeaveCriticalSection(&s_mapLock);
        return p;
    }

    unsigned MapGeneration() { return s_mapGen; }

    // ---- Spielregeln ([Gameplay]) ----

    namespace
    {
        // Befehlsnummern = Reihenfolge der Namenstabelle 0x4C26B8
        enum : uint16_t
        {
            CmdSurvive = 4, CmdDonowt = 17, CmdDisable = 18, CmdEnable = 19, CmdDecCount = 20,
            CmdLockDoor = 27, CmdUnlockDoor = 28, CmdArrowOff = 36, CmdKillObj = 43, CmdKillCar = 50,
            CmdDummyOff = 63, CmdDoorOn = 70, CmdDoorOff = 71, CmdCloseDoor = 74, CmdKillPed = 79,
            CmdBankAlarmOff = 81, CmdCancelBriefing = 91, CmdKillProcess = 96, CmdKillSideProc = 98,
            CmdPowerupOff = 100, CmdKillSpecProc = 101, CmdScoreCheck = 102, CmdIsGoalDead = 105,
            CmdMobileBrief = 111, CmdMessageBrief = 116, CmdDropWantedLevel = 119, CmdIncCount = 127,
            CmdCompare = 128, CmdReset = 129, CmdKeepThisProc = 130, CmdDeadArrested = 132,
            CmdStopFrenzy = 134, CmdKfCancelBriefing = 138, CmdKfCancelGeneral = 140,
            CmdResetWithBriefs = 145, CmdRedArrowOff = 146,
            CmdMPhone = 16, CmdMissionEnd = 45,
        };
        // Objekttypen in der Objekttabelle (= Index der Namenstabelle 0x4C2580 minus 4)
        constexpr int32_t kObjMPhones = 34, kObjPhoneToggle = 39;
        constexpr int32_t kBriefMissionFailed = 2501; // "MISSION FEHLGESCHLAGEN!"
        constexpr int32_t kRetryWait = 30;            // Frames (~1 s), damit die Meldungen nacheinander kommen
        constexpr DWORD   kRetryTimeout = 180000;

        // Befehle, mit denen die Skripte nach einem Fehlschlag aufräumen und warten, bevor sie die Mission
        // neu aufbauen. Alles andere (ARROW, PARKED_ON, POWERUP_ON ...) beendet die Wartephase, damit
        // SURVIVE als Missionsziel ("überlebe 60 Sekunden") nie verkürzt wird.
        bool IsCleanupCommand(uint16_t c)
        {
            switch (c)
            {
            case CmdSurvive: case CmdDonowt: case CmdDisable: case CmdEnable: case CmdDecCount:
            case CmdLockDoor: case CmdUnlockDoor: case CmdArrowOff: case CmdKillObj: case CmdKillCar:
            case CmdDummyOff: case CmdDoorOn: case CmdDoorOff: case CmdCloseDoor: case CmdKillPed:
            case CmdBankAlarmOff: case CmdCancelBriefing: case CmdKillProcess: case CmdKillSideProc:
            case CmdPowerupOff: case CmdKillSpecProc: case CmdScoreCheck: case CmdIsGoalDead:
            case CmdMobileBrief: case CmdMessageBrief: case CmdDropWantedLevel: case CmdIncCount:
            case CmdCompare: case CmdReset: case CmdKeepThisProc: case CmdDeadArrested:
            case CmdStopFrenzy: case CmdKfCancelBriefing: case CmdKfCancelGeneral:
            case CmdResetWithBriefs: case CmdRedArrowOff:
                return true;
            default:
                return false;
            }
        }

        DWORD s_failedAt[addr::ScriptProcs] = {}; // "MISSION FEHLGESCHLAGEN" je Skriptprozess, 0 = normal
        int   s_failedCount = 0;

        // Objekt aus der Zeile <line> von mission.ini: +0 Typ, +4 Wert (Zähler) bzw. Handle (Telefon)
        uint8_t* ScriptObject(int line)
        {
            if (line < 0 || line >= 32768)
                return nullptr;
            const int16_t slot = *reinterpret_cast<int16_t*>(addr::ScriptObjSlot + line * 2);
            if (slot < 0 || slot >= 2000)
                return nullptr;
            return reinterpret_cast<uint8_t*>(addr::ScriptObjects + slot * addr::ScriptObjSize);
        }
        int32_t* ObjectValue(int line)
        {
            uint8_t* o = ScriptObject(line);
            return o ? reinterpret_cast<int32_t*>(o + 4) : nullptr;
        }

        // Telefongruppe (MPHONE): Wer abhebt, dessen Telefon setzt das Spiel dauerhaft auf Handle -1 (0x4776F4)
        // und es klingelt nie wieder. Außerdem wandelt die Mission ihre "künftigen" Objekte um (PARKED_ON macht
        // aus FUTURECAR ein Auto und überschreibt dabei Modell und Typ). Für einen neuen Versuch sichern wir beim
        // Abheben die ganze Objekttabelle und stellen nach dem Fehlschlag Zähler, umgewandelte Objekte und das
        // Telefon wieder her.
        constexpr int32_t kObjCounter = 12;
        struct PhoneGroup { int line = -1, count = 0; int32_t handles[8] = {}; };
        struct MissionLog
        {
            bool active = false, failed = false, phonesBack = false;
            int failProc = -1;
            int phoneLine = -1;
            int32_t phoneHandle = -1;
            int slots = 0;                   // Anzahl gesicherter Objekte
            std::vector<uint8_t> start;      // Objekttabelle beim Abheben
            std::vector<int> converted;      // von der Mission umgewandelte Objekte (bis zum Fehlschlag)
        };
        PhoneGroup s_phones;
        MissionLog s_mission;

        uint8_t* ObjectSlot(int slot) { return reinterpret_cast<uint8_t*>(addr::ScriptObjects + slot * addr::ScriptObjSize); }
        int32_t SlotType(const uint8_t* entry) { int32_t t; memcpy(&t, entry, 4); return t; }

        // Höchster belegter Platz der Objekttabelle (Zeilennummer -> Platz, unbenutzte Zeilen sind 0)
        int ObjectSlotCount()
        {
            int top = -1;
            const int16_t* map = reinterpret_cast<const int16_t*>(addr::ScriptObjSlot);
            for (int line = 0; line < 32768; ++line)
                if (map[line] > top && map[line] < 2000)
                    top = map[line];
            return top + 1;
        }

        void WatchPhones(const ScriptLine* line)
        {
            s_phones.line = line->obj;
            s_phones.count = std::min<int>(std::max<int>(line->fail, 0), 8);
            for (int k = 0; k < s_phones.count; ++k)
            {
                const int32_t* v = ObjectValue(s_phones.line + k);
                s_phones.handles[k] = v ? *v : -1;
            }
        }

        void CheckAnswered()
        {
            for (int k = 0; k < s_phones.count; ++k)
            {
                const int32_t* v = ObjectValue(s_phones.line + k);
                if (!v || s_phones.handles[k] == -1 || *v != -1)
                    continue;
                s_mission = MissionLog();
                s_mission.active = true;
                s_mission.phoneLine = s_phones.line + k;
                s_mission.phoneHandle = s_phones.handles[k];
                s_mission.slots = ObjectSlotCount();
                s_mission.start.assign(ObjectSlot(0), ObjectSlot(0) + s_mission.slots * addr::ScriptObjSize);
                s_phones.line = -1;
                LOG("Telefon aus Zeile %d abgenommen - Missionsstand (%d Objekte) für einen neuen Versuch gemerkt",
                    s_mission.phoneLine, s_mission.slots);
                return;
            }
        }

        // Beim Fehlschlag: welche Objekte hat die Mission bis hierher umgewandelt?
        void NoteConversions()
        {
            s_mission.converted.clear();
            for (int i = 0; i < s_mission.slots; ++i)
                if (SlotType(ObjectSlot(i)) != SlotType(&s_mission.start[i * addr::ScriptObjSize]))
                    s_mission.converted.push_back(i);
        }

        // Ende der Wartephase nach einem Fehlschlag: Hat das Skript die Telefongruppe wieder freigegeben
        // (wie in Liberty City), steht die gescheiterte Mission wieder zur Wahl, als wäre sie nie angenommen worden
        void FinishFailure(int proc)
        {
            s_failedAt[proc] = 0;
            if (!s_mission.active || !s_mission.failed || s_mission.failProc != proc)
                return;
            if (s_mission.phonesBack)
            {
                // Umgewandelte Objekte wieder in den Ausgangszustand (z.B. FUTURECAR mit Modell statt Auto-Handle)
                for (int slot : s_mission.converted)
                    memcpy(ObjectSlot(slot), &s_mission.start[slot * addr::ScriptObjSize], addr::ScriptObjSize);
                // Zähler auf den Stand vor der Mission
                int counters = 0;
                for (int i = 0; i < s_mission.slots; ++i)
                {
                    uint8_t* entry = ObjectSlot(i);
                    const uint8_t* before = &s_mission.start[i * addr::ScriptObjSize];
                    if (SlotType(entry) == kObjCounter && SlotType(before) == kObjCounter && memcmp(entry + 4, before + 4, 4) != 0)
                    {
                        memcpy(entry + 4, before + 4, 4);
                        ++counters;
                    }
                }
                int32_t* phone = ObjectValue(s_mission.phoneLine);
                if (phone && *phone == -1)
                    *phone = s_mission.phoneHandle;
                LOG("Neuer Versuch möglich: Telefon aus Zeile %d klingelt wieder, %d Objekte und %d Zähler zurückgesetzt",
                    s_mission.phoneLine, (int)s_mission.converted.size(), counters);
            }
            s_mission.active = false;
            s_mission.start.clear();
        }

        void __cdecl OnScriptCommand(uint32_t command, ScriptLine* line)
        {
            const int proc = *reinterpret_cast<int16_t*>(addr::ScriptProc);
            if (proc < 0 || proc >= addr::ScriptProcs)
                return;

            // Beendete Prozesse vergessen, damit ein neuer Prozess im selben Platz normal läuft
            if (s_failedCount)
            {
                s_failedCount = 0;
                for (int i = 0; i < addr::ScriptProcs; ++i)
                {
                    if (s_failedAt[i] && !*reinterpret_cast<int16_t*>(addr::ScriptActive + i * 2))
                        FinishFailure(i);
                    s_failedCount += s_failedAt[i] != 0;
                }
            }

            if (s_phones.line >= 0)
                CheckAnswered();
            switch (command)
            {
            case CmdMPhone:
                WatchPhones(line);
                break;
            case CmdMissionEnd:
                s_mission.active = false; // geschafft, nichts zurücksetzen
                break;
            default:
                break;
            }

            DWORD& failedAt = s_failedAt[proc];
            if (command == CmdMessageBrief && line->score == kBriefMissionFailed)
            {
                if (!failedAt)
                {
                    LOG("Mission fehlgeschlagen (Skriptprozess %d) - Wartezeit bis zum neuen Versuch wird verkürzt", proc);
                    ++s_failedCount;
                }
                failedAt = GetTickCount() | 1;
                if (s_mission.active && !s_mission.failed)
                {
                    s_mission.failed = true;
                    s_mission.failProc = proc;
                    NoteConversions();
                }
                return;
            }
            if (!failedAt)
                return;
            if (!IsCleanupCommand(static_cast<uint16_t>(command)) || command == CmdDonowt || GetTickCount() - failedAt > kRetryTimeout)
            {
                FinishFailure(proc);
                return;
            }
            if (command == CmdEnable && s_mission.failed)
            {
                const uint8_t* o = ScriptObject(line->obj);
                const int32_t type = o ? *reinterpret_cast<const int32_t*>(o) : -1;
                if (type == kObjMPhones || type == kObjPhoneToggle)
                    s_mission.phonesBack = true;
            }
            // SURVIVE kopiert beim ersten Durchlauf die Dauer <data> nach <fail> und zählt dort jeden Frame herunter
            if (command == CmdSurvive && *reinterpret_cast<int16_t*>(addr::ScriptState + proc * 2) != 1 && line->fail > kRetryWait)
                line->fail = kRetryWait;
        }

        // Ersetzt "jmp [ebx*4+ScriptJumps]" im Interpreter, alle Register bleiben erhalten
        __declspec(naked) void ScriptDispatchHook()
        {
            __asm
            {
                pushfd
                pushad
                push ecx
                push ebx
                call OnScriptCommand
                add esp, 8
                popad
                popfd
                jmp dword ptr [ebx * 4 + 0x487FDC] // addr::ScriptJumps
            }
        }
    }

    bool EnableKeepMultiplier()
    {
        // mov ax,[edi+6576BC]; shr ax,1; mov [edi+6576BC],ax  ->  das Halbieren entfällt
        static const uint8_t expect[] = { 0x66, 0x8B, 0x87, 0xBC, 0x76, 0x65, 0x00, 0x66, 0xD1, 0xE8, 0x66, 0x89, 0x87, 0xBC, 0x76, 0x65, 0x00 };
        static const uint8_t nop3[] = { 0x0F, 0x1F, 0x00 };
        if (!s_supported || !hooks::BytesMatch(addr::MultHalve - 7, expect, sizeof(expect)))
            return false;
        return hooks::WriteMemory(reinterpret_cast<void*>(addr::MultHalve), nop3, sizeof(nop3));
    }

    bool EnableAllCharacters()
    {
        // "call NotEnglish" durch "xor al,al" (+ 3-Byte-NOP) ersetzen: nie überspringen
        static const uint8_t patch[5] = { 0x32, 0xC0, 0x0F, 0x1F, 0x00 };
        if (!s_supported)
            return false;
        for (uintptr_t site : { addr::PickPrevCheck, addr::PickNextCheck })
        {
            int32_t rel;
            memcpy(&rel, reinterpret_cast<const void*>(site + 1), 4);
            const bool original = *reinterpret_cast<const uint8_t*>(site) == 0xE8 && site + 5 + rel == addr::NotEnglish;
            const bool patched = hooks::BytesMatch(site, patch, sizeof(patch));
            if (!original && !patched)
                return false;
        }
        return hooks::WriteMemory(reinterpret_cast<void*>(addr::PickPrevCheck), patch, sizeof(patch)) &&
               hooks::WriteMemory(reinterpret_cast<void*>(addr::PickNextCheck), patch, sizeof(patch));
    }

    bool EnableQuickRetry()
    {
        static const uint8_t dispatch[] = { 0xFF, 0x24, 0x9D, 0xDC, 0x7F, 0x48, 0x00 };
        // SURVIVE liest den Zustand aus ScriptState und zählt in <fail> (Zeile + 0x0A) herunter
        static const uint8_t state[] = { 0x66, 0x8B, 0x34, 0x4D, 0x60, 0xB6, 0x6D, 0x00 };
        static const uint8_t countdown[] = { 0x8B, 0x82, 0x12, 0xBC, 0x6F, 0x00, 0x48, 0x89, 0x82, 0x12, 0xBC, 0x6F, 0x00 };
        if (!s_supported || !hooks::BytesMatch(addr::ScriptDispatch, dispatch, sizeof(dispatch)) ||
            !hooks::BytesMatch(addr::ScriptSurvive + 0x09, state, sizeof(state)) ||
            !hooks::BytesMatch(addr::ScriptSurvive + 0x50, countdown, sizeof(countdown)))
            return false;

        uint8_t patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&ScriptDispatchHook) - (addr::ScriptDispatch + 5));
        memcpy(patch + 1, &rel, 4);
        return hooks::WriteMemory(reinterpret_cast<void*>(addr::ScriptDispatch), patch, sizeof(patch));
    }
}

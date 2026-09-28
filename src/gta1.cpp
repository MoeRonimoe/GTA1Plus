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
    // Aufbau der Spieldaten; gleich in GTA.exe 1.0.0.1 und London 1969 (gepackte Strukturen)
    namespace addr
    {
        // Fußgänger (PedStruct + ID * 235): +0 ID, +0x1B 1 = Spielfigur, +0x44 Lebensenergie 0..100,
        // +0x52 Handle der Flammen an der Figur (-1 = brennt nicht), +0x66 Zustand
        constexpr int       PedStride     = 235;
        constexpr int       PedId         = 0x00;
        constexpr int       PedAnim       = 0x17;     // int16 Animationsbild (0x88 steht, 0xAF..0xB2 fällt, 0xE9..0xED liegt)
        constexpr int       PedHealth     = 0x44;
        constexpr int       PedFlames     = 0x52;
        constexpr int       PedPose       = 0x66;     // int32 Zustand: 2 zu Fuß, 0x15 getroffen/stirbt, 0x17 tot, 0x18 im Wasser
        constexpr int       PedAngleOfs   = 0x99;
        constexpr int       ObjectSize    = 130;      // Objekt-Plätze: +0 Handle, +0x0A Typ, +0x0C belegt, +0x14 Besitzer
        constexpr int       ObjectCount   = 0xDAC;
        constexpr int16_t   ObjFlames     = 0x2E;     // o_lfire
        constexpr int       PlayerStride  = 417;      // Spieler-Struktur
        constexpr int       ScriptProcs   = 32;
        constexpr int       ScriptObjSize = 29;       // Objekte aus mission.ini: +0 Typ, +4 Wert/Handle
    }

    // Adressen je Spielversion, per Disassembler ermittelt. 0 = in dieser Version (noch) nicht bekannt;
    // die Funktion des Mods, die sie braucht, bleibt dann aus.
    struct Addresses
    {
        const char* name = "";
        uint32_t  timestamp = 0;      // Zeitstempel im PE-Kopf der exe

        uintptr_t HudDraw = 0;        // zeichnet HUD; läuft jeden Frame im Spiel, auch pausiert
        uintptr_t HudCallJmp = 0;     // jmp HudDraw
        uintptr_t HudCallCall = 0;    // call HudDraw
        uintptr_t GetPlayerPos = 0;   // GtaPos* __cdecl(int player), je nach Steuerungstyp Auto/Zug/Fußgänger
        uintptr_t PauseOn = 0, PauseOff = 0;         // "paused"-Anzeige
        uintptr_t SoundPause = 0, SoundResume = 0;
        uintptr_t PauseAll = 0, ResumeAll = 0;       // London: Anzeige, RunState und Ton in einer Funktion
        uintptr_t QuitLevel = 0;      // wie Enter in der Beenden-Abfrage des Spiels
        uintptr_t ArrowResolve = 0;   // GtaPos* __cdecl(ArrowTarget*), Typ 0 Auto, 1 Position, 3 Fußgänger, 4 Spieler
        uintptr_t ArrowMainOn = 0, ArrowMain = 0;    // Missionspfeil (ARROW/ARROWCAR/ARROWPED)
        uintptr_t ArrowRedOn = 0, ArrowRed = 0;      // roter Pfeil (RED_ARROW)

        uintptr_t RenderFrame = 0;    // zeichnet Welt und HUD
        uintptr_t RenderCall = 0;     // call RenderFrame in der Hauptschleife, nach der Simulation
        uintptr_t PedFire = 0;        // Fußgänger feuert, die Waffen lesen die Richtung aus Ped+0x99
        uintptr_t PedFireCall = 0;
        uintptr_t PedStruct = 0;      // Beginn der Fußgänger-Struktur
        uintptr_t PedAngle = 0;       // PedStruct + 0x99
        uintptr_t PedRemap = 0;       // PedStruct + 0x58, Farbvariante 0..63
        uintptr_t PedDrawRemap = 0;   // PedStruct + 0x93, dieselbe Farbvariante in den Zeichendaten
        uintptr_t PedSprite = 0;      // void __cdecl(Ped*): Sprite aus Ped+0x17 bestimmen (für das Zeichnen)
        uintptr_t BurnDamage = 0;     // cmp word [esi+52h],-1 / je / dec byte [esi+44h]: Brennschaden je Frame

        uintptr_t Camera = 0;         // CameraData je Spieler (Abstand wie die Spieler-Struktur)
        uintptr_t ViewRect = 0;       // void __cdecl(int player), Sichtbereich aus der Kamera
        uintptr_t ViewRectCall = 0;   // call ViewRect am Ende der Kameraführung
        // Kameraführung: "lea eax,[eax+eax*4] / shl eax,5 / cdq" berechnet, wie weit sich die Figur vom
        // Bildmittelpunkt entfernen darf, bevor die Kamera nachzieht (Abstand * 160 / Tempo)
        uintptr_t SteadyCamera[2] = {};

        uintptr_t ReadInput = 0;      // liefert das Eingabepaket des lokalen Spielers
        uintptr_t ReadInputCall = 0;  // call ReadInput in der Hauptschleife
        uintptr_t InSteer = 0, InGas = 0, InBrake = 0, InJump = 0, InFire = 0, InNext = 0, InPrev = 0, InSpecial = 0;
        uintptr_t KeyScancodes = 0;   // int je Steuerung (Scancode, +0x100 erweitert), 4 = Springen

        uintptr_t PlayerCharacter = 0;  // byte, im Hauptmenü gewähltes Portrait (0 Ulrika .. 7 Kivlov)
        uintptr_t CharacterColors = 0;  // byte [Charakter * 4 + Spieler]: Farbnummer wie in mission.ini
        uintptr_t ColorToRemap = 0;     // byte [Farbnummer]: Farbvariante 0..63

        uintptr_t ObjectCreate = 0;   // int16 __cdecl(x, y, z, Typ, Besitzer-ID, Besitzerart 1 Auto/2 Fußgänger)
        uintptr_t ObjectDelete = 0;   // void __cdecl(int16 handle), wie beim Ablauf eines Objekts
        uintptr_t Objects = 0;        // Objekt-Plätze
        uintptr_t ActiveObjects = 0;  // Liste, die das Entfernen durchsucht

        uintptr_t LocalPlayer = 0;    // int
        uintptr_t NumPlayers = 0;     // int, 1 = Einzelspieler
        uintptr_t PlayerPedId = 0;    // int16 ID der Spielfigur, auch im Fahrzeug
        uintptr_t PlayerCtrl = 0;     // int Steuerungstyp
        uintptr_t PlayerObject = 0;   // Index des gesteuerten Objekts
        uintptr_t RunState = 0;       // 1 = läuft, 2 = pausiert

        uintptr_t MultHalve = 0;      // shr ax,1: Multiplikator halbieren (Strafe bei Verhaftung)
        uintptr_t NotEnglish = 0;     // Hauptmenü: "nicht Englisch" -> Frauen überspringen
        uintptr_t PickPrevCheck = 0, PickNextCheck = 0;

        uintptr_t ScriptDispatch = 0; // jmp [reg*4+ScriptJumps]: 1.0.0.1 ebx = Befehl, ecx = Zeile; London edi, ebp
        uintptr_t ScriptJumps = 0;
        bool      ScriptRegsEdi = false;
        uintptr_t ScriptSurvive = 0;  // Befehl SURVIVE
        uintptr_t ScriptProc = 0, ScriptState = 0, ScriptActive = 0, ScriptObjSlot = 0, ScriptObjects = 0;
        uintptr_t ScriptLines = 0;     // 5000 Zeilen zu 22 Byte (ScriptLine), SURVIVE zählt darin herunter
        uintptr_t ScriptProcBlock = 0; // Tabellen je Prozess (aktuelle Zeile, Verkettung ...), 0x4A0 Byte

        // Spielerwerte (Spieler 0, weitere im Abstand PlayerStride)
        uintptr_t PlayerScore = 0, PlayerMultiplier = 0, PlayerJailCard = 0, PlayerLives = 0, PlayerAmmo = 0, PlayerArmor = 0;

        uintptr_t TextReady = 0;      // int: FXT-Texte entschlüsselt
        uintptr_t TextBuffer = 0;     // char*: "[Schlüssel]Text\0..." bis "[]"
        uintptr_t MglPrimary = 0;     // IDirectDrawSurface* (Primary von MGL)
        uintptr_t MglPageCount = 0;   // Anzahl Seiten der Flip-Kette
        uintptr_t IatGetProcAddress = 0, IatPeekMessageA = 0;
        uintptr_t LoadGetProcAddress[1] = {};   // "mov reg,[IAT]", holt DirectDrawCreate
        uintptr_t LoadPeekMessageA[3] = {};     // "mov reg,[IAT]" in den Nachrichtenschleifen

        struct Sig { uintptr_t at; uint8_t bytes[10]; size_t len; };
        Sig sigs[16] = {};            // Stichproben zur Erkennung
    };

    // GTA.exe 1.0.0.1 (Windows-Version vom 10.10.1997, z.B. Ready2Play; GTA_Intro.exe ist bis auf das Intro gleich)
    static Addresses AddressesR2P()
    {
        Addresses a;
        a.name = "GTA.exe 1.0.0.1";
        a.timestamp = 0x343DFF8B;
        a.HudDraw = 0x46DBB0; a.HudCallJmp = 0x42F002; a.HudCallCall = 0x42F02B;
        a.GetPlayerPos = 0x463FF0;
        a.PauseOn = 0x46B9C0; a.PauseOff = 0x46B9E0; a.SoundPause = 0x4583B0; a.SoundResume = 0x4583E0;
        a.QuitLevel = 0x42EB60;   // wie Enter in der Beenden-Abfrage (0x46D701)
        a.ArrowResolve = 0x470A90;
        a.ArrowMainOn = 0x677F0C; a.ArrowMain = 0x678418; a.ArrowRedOn = 0x678400; a.ArrowRed = 0x678DD8;
        a.RenderFrame = 0x42EFE0; a.RenderCall = 0x45D057;
        a.PedFire = 0x4470B0; a.PedFireCall = 0x451AA5;
        a.PedStruct = 0x600C48; a.PedAngle = 0x600CE1; a.PedRemap = 0x600CA0; a.PedDrawRemap = 0x600CDB;
        a.PedSprite = 0x442790; a.BurnDamage = 0x451AF7;
        // Kameraführung 0x42B910 (je Spiel-Frame nach der Simulation): glättet in 0x65760D + Spieler * 417,
        // schreibt den Bildmittelpunkt in die Kamera und berechnet daraus in ViewRect den Sichtbereich für die
        // Spiellogik (0x6575CC). Zeichnen und Ton lesen danach die Kamera (0x42B780/0x42B7E0).
        a.Camera = 0x6575E0; a.ViewRect = 0x42BB40; a.ViewRectCall = 0x42BB0B;
        a.SteadyCamera[0] = 0x42B9F2; a.SteadyCamera[1] = 0x42C761;
        a.ReadInput = 0x4365E0; a.ReadInputCall = 0x45CFD3;
        a.InSteer = 0x600A80; a.InGas = 0x6008FC; a.InBrake = 0x600A18; a.InJump = 0x600900;
        a.InFire = 0x6008F8; a.InNext = 0x600A1C; a.InPrev = 0x600A48; a.InSpecial = 0x600A84;
        a.KeyScancodes = 0x600A50;
        a.PlayerCharacter = 0x657764; a.CharacterColors = 0x4C1668; a.ColorToRemap = 0x4C0110;
        // Flammen an Figuren: 0x41CB30/0x404FD0 erzeugen mit ObjectCreate ein o_lfire an der Figur und merken
        // das Handle in Ped+0x52; solange es gesetzt ist, zieht 0x451AFE jeden Frame 1 Lebenspunkt ab.
        // Endet das Objekt, löst 0x41D200 es von der Figur (Ped+0x52 = -1).
        a.ObjectCreate = 0x41BAA0; a.ObjectDelete = 0x41D380; a.Objects = 0x50F5F8; a.ActiveObjects = 0x57E750;
        a.LocalPlayer = 0x657C50; a.NumPlayers = 0x657C4C; a.PlayerPedId = 0x6575CA;
        a.PlayerCtrl = 0x657695; a.PlayerObject = 0x657699; a.RunState = 0x5F3A90;
        a.MultHalve = 0x464649;   // in der Strafe bei Verhaftung (0x464570)
        // Außer in der englischen Sprachversion überspringt die Charakterauswahl die Frauen (0 Ulrika, 2 Katie,
        // 3 Mikki, 4 Divine); 0x4201B0 liefert "nicht Englisch"
        a.NotEnglish = 0x4201B0; a.PickPrevCheck = 0x40991E; a.PickNextCheck = 0x409976;
        // Missionsskript (mission.ini): 0x482250 führt eine Zeile des laufenden Prozesses aus
        a.ScriptDispatch = 0x4822EA; a.ScriptJumps = 0x487FDC; a.ScriptSurvive = 0x476120;
        a.ScriptProc = 0x736A08; a.ScriptState = 0x6DB660; a.ScriptActive = 0x6FB768;
        a.ScriptObjSlot = 0x6DB768; a.ScriptObjects = 0x6796F8;
        a.ScriptLines = 0x6FBC08; a.ScriptProcBlock = 0x6FB768;
        a.PlayerScore = 0x6576B8; a.PlayerMultiplier = 0x6576BC; a.PlayerJailCard = 0x6576BE;
        a.PlayerLives = 0x65774C; a.PlayerAmmo = 0x657718; a.PlayerArmor = 0x65775C;
        a.TextReady = 0x57EC60; a.TextBuffer = 0x57EC8C;   // aus GetText 0x4200D0
        a.MglPrimary = 0x4C2FEC; a.MglPageCount = 0x4C2FE8;
        a.IatGetProcAddress = 0x787474; a.IatPeekMessageA = 0x78754C;
        a.LoadGetProcAddress[0] = 0x49AFC3;
        a.LoadPeekMessageA[0] = 0x43082C; a.LoadPeekMessageA[1] = 0x49A2DC; a.LoadPeekMessageA[2] = 0x49C54B;
        const Addresses::Sig sigs[] = {
            { 0x46DBB0, { 0x53, 0xA1, 0x74, 0x50, 0x65, 0x00 }, 6 },                         // HudDraw
            { 0x463FF0, { 0x8B, 0x44, 0x24, 0x04, 0x56, 0x33, 0xF6, 0x8D, 0x0C, 0x40 }, 10 }, // GetPlayerPos
            { 0x46B9C0, { 0xC6, 0x05, 0x8C, 0x83, 0x67, 0x00, 0x01 }, 7 },                   // PauseOn
            { 0x46B9E0, { 0xC6, 0x05, 0x8C, 0x83, 0x67, 0x00, 0x00, 0xC3 }, 8 },             // PauseOff
            { 0x4583B0, { 0xA0, 0x38, 0x02, 0x4C, 0x00, 0x84, 0xC0 }, 7 },                   // SoundPause
            { 0x4583E0, { 0xA0, 0x38, 0x02, 0x4C, 0x00, 0x84, 0xC0 }, 7 },                   // SoundResume
            { 0x42F30A, { 0xA1, 0x4C, 0x7C, 0x65, 0x00, 0x83, 0xF8, 0x01 }, 8 },             // F6-Handler: NumPlayers
            { 0x42F318, { 0xA1, 0x90, 0x3A, 0x5F, 0x00, 0x83, 0xF8, 0x01 }, 8 },             // F6-Handler: RunState
            { 0x46DBF0, { 0xA1, 0x50, 0x7C, 0x65, 0x00, 0x50, 0xE8 }, 7 },                   // Debug-Anzeige: LocalPlayer
            { 0x42EB60, { 0xC7, 0x05, 0x7C, 0x3A, 0x5F, 0x00, 0x02, 0x00, 0x00, 0x00 }, 10 }, // QuitLevel
            { 0x470A90, { 0x8B, 0x4C, 0x24, 0x04, 0x56, 0x57, 0x8B, 0x01, 0x83, 0xF8 }, 10 }, // ArrowResolve
            { 0x436AC0, { 0xA0, 0x88, 0x0A, 0x60, 0x00 }, 5 },                               // Steuerung 6 (Ein-/Aussteigen)
            { 0x436A99, { 0xA0, 0xF8, 0x08, 0x60, 0x00 }, 5 },                               // Steuerung 5 (Feuern) liest InFire
            { 0x452B9E, { 0x0F, 0xBF, 0x85, 0xE1, 0x0C, 0x60, 0x00 }, 7 },                   // Fußgänger-Bewegung liest PedAngle
        };
        std::copy(std::begin(sigs), std::end(sigs), a.sigs);
        return a;
    }

    // GTA_London.exe (London 1969, Build vom 12.03.1999). Inkrementell gelinkt: Aufrufe laufen über Sprung-Thunks
    // (jmp Funktion) am Anfang des Codes, dort lassen sich alle Aufrufer einer Funktion auf einmal umbiegen.
    // Spielerdaten liegen gegenüber 1.0.0.1 um 0x1B5960 verschoben, HUD-Daten um 0x1CFDE8.
    static Addresses AddressesLondon69()
    {
        Addresses a;
        a.name = "GTA_London.exe (London 1969)";
        a.timestamp = 0x36E95F23;
        a.HudDraw = 0x4AE290; a.HudCallJmp = 0x40262B; // einziger Weg zum HUD: Thunk, angesprungen aus 0x4424E3
        a.GetPlayerPos = 0x4828F0;
        a.PauseAll = 0x441EB0; a.ResumeAll = 0x441EE0;  // 0x441EB0: PauseOn, RunState = 2, Ton anhalten
        a.QuitLevel = 0x441F70;
        a.ArrowResolve = 0x4B0E30;
        a.ArrowMainOn = 0x847CF4; a.ArrowMain = 0x848200; a.ArrowRedOn = 0x8481E8; a.ArrowRed = 0x848BC0;
        // Hauptschleife 0x41F390: Eingabe (0x444AC0) und Zeichnen (0x442480) laufen über Thunks
        a.ReadInput = 0x401C12; a.ReadInputCall = 0x41F3A1;
        a.RenderFrame = 0x401B90; a.RenderCall = 0x41F425;
        // Tastenzustände aus dem Druck-Handler 0x444DC0 (gleiche Reihenfolge und Paketbits wie 1.0.0.1)
        a.InSteer = 0x572F50; a.InGas = 0x572DCC; a.InBrake = 0x572EE8; a.InJump = 0x572DD0;
        a.InFire = 0x572DC8; a.InNext = 0x572EEC; a.InPrev = 0x572F18; a.InSpecial = 0x572F54;
        a.KeyScancodes = 0x572F20;
        // Fußgänger wie in 1.0.0.1 (235 Byte), Position über 0x47E2B0
        a.PedStruct = 0x7E2370; a.PedAngle = 0x7E2409; a.PedRemap = 0x7E23C8; a.PedDrawRemap = 0x7E2403;
        a.PlayerPedId = 0x80CF2A; a.PlayerCharacter = 0x80D0C4;
        // Kameraführung endet wie in 1.0.0.1 mit ViewRect(player); Sichtbereich dort als (h + 0x180) * 160 / zoom
        a.Camera = 0x80CF40; a.ViewRect = 0x450BC0; a.ViewRectCall = 0x450B87;
        a.SteadyCamera[0] = 0x451581;
        // Objekte wie in 1.0.0.1 (130 Byte); Anzünden in 0x465FEF, Sprite der Figuren in 0x467FE0
        a.ObjectCreate = 0x40222F; a.ObjectDelete = 0x401924; a.Objects = 0x75CDC0; a.ActiveObjects = 0x7CBF18;
        a.PedSprite = 0x467FE0;
        a.BurnDamage = 0x47A760;  // Brennschaden: cmp [esi+52h],-1 / je / Lebensenergie - 1 (anders geschrieben)
        a.PedFire = 0x402AEF; a.PedFireCall = 0x47A71D;
        a.MultHalve = 0x48307E;   // eigene Funktion 0x483070 halbiert den Multiplikator (0x80D01C)
        a.NotEnglish = 0x40286F; a.PickPrevCheck = 0x439D19; a.PickNextCheck = 0x439D69;
        // Missionsskript: Daten gegenüber 1.0.0.1 um 8 Byte verschoben, gleiche Befehls- und Objektnummern
        a.ScriptDispatch = 0x45F639; a.ScriptJumps = 0x461490; a.ScriptRegsEdi = true; a.ScriptSurvive = 0x456B00;
        a.ScriptProc = 0x736A00; a.ScriptState = 0x6DB658; a.ScriptActive = 0x6FB760;
        a.ScriptObjSlot = 0x6DB760; a.ScriptObjects = 0x6796F0;
        a.ScriptLines = 0x6FBC00; a.ScriptProcBlock = 0x6FB760;
        a.PlayerScore = 0x80D018; a.PlayerMultiplier = 0x80D01C; a.PlayerJailCard = 0x80D01E;
        a.PlayerLives = 0x80D0AC; a.PlayerAmmo = 0x80D078; a.PlayerArmor = 0x80D0BC;
        a.LocalPlayer = 0x80D5B0; a.NumPlayers = 0x80D5AC;
        a.PlayerCtrl = 0x80CFF5; a.PlayerObject = 0x80CFF9; a.RunState = 0x572ACC;
        a.TextReady = 0x8378A0; a.TextBuffer = 0x8378CC;   // aus GetText 0x4A6CB0
        a.MglPrimary = 0x503CFC; a.MglPageCount = 0x503CF8;
        a.IatGetProcAddress = 0x858738; a.IatPeekMessageA = 0x8587F8;
        a.LoadGetProcAddress[0] = 0x4BEA13;
        a.LoadPeekMessageA[0] = 0x420780; a.LoadPeekMessageA[1] = 0x4BDD2C; a.LoadPeekMessageA[2] = 0x4B9D5B;
        const Addresses::Sig sigs[] = {
            { 0x4AE290, { 0xA1, 0xA4, 0x02, 0x56, 0x00, 0x85, 0xC0 }, 7 },                   // HudDraw
            { 0x4828F0, { 0x8B, 0x4C, 0x24, 0x04, 0x56, 0x33, 0xF6, 0x8D, 0x04, 0x49 }, 10 }, // GetPlayerPos
            { 0x441EB5, { 0xC7, 0x05, 0xCC, 0x2A, 0x57, 0x00, 0x02, 0x00, 0x00, 0x00 }, 10 }, // PauseAll: RunState = 2
            { 0x441EE5, { 0xC7, 0x05, 0xCC, 0x2A, 0x57, 0x00, 0x01, 0x00, 0x00, 0x00 }, 10 }, // ResumeAll: RunState = 1
            { 0x441F70, { 0xC7, 0x05, 0xB8, 0x2A, 0x57, 0x00, 0x02, 0x00, 0x00, 0x00 }, 10 }, // QuitLevel
            { 0x4B0E30, { 0x8B, 0x44, 0x24, 0x04, 0x56, 0x57, 0x8B, 0x08, 0x83, 0xF9 }, 10 }, // ArrowResolve
            { 0x4B0AE0, { 0xA1, 0xB0, 0xD5, 0x80, 0x00 }, 5 },                               // Debug-Anzeige: LocalPlayer
        };
        std::copy(std::begin(sigs), std::end(sigs), a.sigs);
        return a;
    }

    // GTA_London_1961.exe (London 1961, Build vom 26.06.1999): zu 79 % gleich wie London 1969, Daten meist um
    // 0x1C08 verschoben (MGL um 0x1498); mit tools/port/sigport.py aus den 1969er Adressen übertragen
    static Addresses AddressesLondon61()
    {
        Addresses a;
        a.name = "GTA_London_1961.exe (London 1961)";
        a.timestamp = 0x3773D5BB;
        a.HudDraw = 0x4AEE20; a.HudCallJmp = 0x402671;
        a.GetPlayerPos = 0x483190;
        a.PauseAll = 0x442190; a.ResumeAll = 0x4421C0;
        a.QuitLevel = 0x442250;
        a.ArrowResolve = 0x4B1B30;
        a.ArrowMainOn = 0x849904; a.ArrowMain = 0x849E10; a.ArrowRedOn = 0x849DF8; a.ArrowRed = 0x84A7D8;
        a.ReadInput = 0x401C17; a.ReadInputCall = 0x41F631;
        a.RenderFrame = 0x401B95; a.RenderCall = 0x41F6B5;
        a.InSteer = 0x574B58; a.InGas = 0x5749D4; a.InBrake = 0x574AF0; a.InJump = 0x5749D8;
        a.InFire = 0x5749D0; a.InNext = 0x574AF4; a.InPrev = 0x574B20; a.InSpecial = 0x574B5C;
        a.KeyScancodes = 0x574B28;
        a.PedStruct = 0x7E3F78; a.PedAngle = 0x7E4011; a.PedRemap = 0x7E3FD0; a.PedDrawRemap = 0x7E400B;
        a.PlayerPedId = 0x80EB32; a.PlayerCharacter = 0x80ECCC;
        a.Camera = 0x80EB48; a.ViewRect = 0x450F80; a.ViewRectCall = 0x450F47;
        a.SteadyCamera[0] = 0x451941;
        a.ObjectCreate = 0x402275; a.ObjectDelete = 0x401924; a.Objects = 0x75E9C8; a.ActiveObjects = 0x7CDB20;
        a.PedSprite = 0x4687F0;
        a.BurnDamage = 0x47AFF0;
        a.PedFire = 0x402B3F; a.PedFireCall = 0x47AFAD;
        a.MultHalve = 0x48391E;
        a.NotEnglish = 0x4028BA; a.PickPrevCheck = 0x439FE9; a.PickNextCheck = 0x43A039;
        // Missionsskript: 156 Befehle (Drive-by-Befehle angehängt), Nummern bis dahin wie in 1.0.0.1
        a.ScriptDispatch = 0x45FDF9; a.ScriptJumps = 0x461C80; a.ScriptRegsEdi = true; a.ScriptSurvive = 0x456FE0;
        a.ScriptProc = 0x738608; a.ScriptState = 0x6DD260; a.ScriptActive = 0x6FD368;
        a.ScriptObjSlot = 0x6DD368; a.ScriptObjects = 0x67B2F8;
        a.ScriptLines = 0x6FD808; a.ScriptProcBlock = 0x6FD368;
        a.PlayerScore = 0x80EC20; a.PlayerMultiplier = 0x80EC24; a.PlayerJailCard = 0x80EC26;
        a.PlayerLives = 0x80ECB4; a.PlayerAmmo = 0x80EC80; a.PlayerArmor = 0x80ECC4;
        a.LocalPlayer = 0x80F1B8; a.NumPlayers = 0x80F1B4;
        a.PlayerCtrl = 0x80EBFD; a.PlayerObject = 0x80EC01; a.RunState = 0x5746D4;
        a.TextReady = 0x8394B0; a.TextBuffer = 0x8394DC;
        a.MglPrimary = 0x505194; a.MglPageCount = 0x505190;
        a.IatGetProcAddress = 0x859738; a.IatPeekMessageA = 0x8597F8;
        a.LoadGetProcAddress[0] = 0x4BF733;
        a.LoadPeekMessageA[0] = 0x420A10; a.LoadPeekMessageA[1] = 0x4BEA4C; a.LoadPeekMessageA[2] = 0x4BAA7B;
        const Addresses::Sig sigs[] = {
            { 0x4AEE20, { 0xA1, 0xAC, 0x1E, 0x56, 0x00, 0x85, 0xC0 }, 7 },                   // HudDraw
            { 0x483190, { 0x8B, 0x4C, 0x24, 0x04, 0x56, 0x33, 0xF6, 0x8D, 0x04, 0x49 }, 10 }, // GetPlayerPos
            { 0x442195, { 0xC7, 0x05, 0xD4, 0x46, 0x57, 0x00, 0x02, 0x00, 0x00, 0x00 }, 10 }, // PauseAll: RunState = 2
            { 0x4421C5, { 0xC7, 0x05, 0xD4, 0x46, 0x57, 0x00, 0x01, 0x00, 0x00, 0x00 }, 10 }, // ResumeAll: RunState = 1
            { 0x442250, { 0xC7, 0x05, 0xC0, 0x46, 0x57, 0x00, 0x02, 0x00, 0x00, 0x00 }, 10 }, // QuitLevel
            { 0x4B1B30, { 0x8B, 0x44, 0x24, 0x04, 0x56, 0x57, 0x8B, 0x08, 0x83, 0xF9 }, 10 }, // ArrowResolve
            { 0x4B17E0, { 0xA1, 0xB8, 0xF1, 0x80, 0x00 }, 5 },                               // Debug-Anzeige: LocalPlayer
        };
        std::copy(std::begin(sigs), std::end(sigs), a.sigs);
        return a;
    }

    static Addresses A;               // erkannte Version

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
    static volatile unsigned s_levelLoads = 0;

    static HANDLE (WINAPI* s_origCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;

    static uint32_t (*s_inputFilter)(uint32_t) = nullptr;
    static void (*s_preRender)() = nullptr;
    static int s_fireAngle = -1;
    static void (*s_cameraFilter)(const CameraView*, float&, float&) = nullptr;

    static int PlayerPed();
    static const GtaPos* ControlledPos(int player, int& type);

    static CameraData& CameraOf(int player)
    {
        return *reinterpret_cast<CameraData*>(A.Camera + player * addr::PlayerStride);
    }

    // Zwischen Kameraführung und Sichtbereich: Versatz der Kamerasteuerung aufschlagen. Die Glättung
    // des Spiels rechnet mit dem Mittelpunkt ohne unseren Versatz weiter, der Sichtbereich passt zum Bild.
    static void __cdecl ViewRectHook(int player)
    {
        if (s_cameraFilter && player == *reinterpret_cast<int*>(A.LocalPlayer))
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
        reinterpret_cast<void(__cdecl*)(int)>(A.ViewRect)(player);
    }

    static void __cdecl RenderHook()
    {
        if (s_preRender)
            s_preRender();
        reinterpret_cast<void(__cdecl*)()>(A.RenderFrame)();
    }

    // Beim Seitwärtslaufen zeigt der Spieler während der Simulation in Laufrichtung;
    // nur für den Schuss wird er kurz zum Fadenkreuz gedreht
    static void __cdecl PedFireHook(uint8_t* ped)
    {
        const auto fire = reinterpret_cast<void(__cdecl*)(uint8_t*)>(A.PedFire);
        const int player = s_fireAngle >= 0 ? PlayerPed() : -1;
        if (player < 0 || ped != reinterpret_cast<uint8_t*>(A.PedStruct + player * addr::PedStride))
        {
            fire(ped);
            return;
        }
        int16_t* angle = reinterpret_cast<int16_t*>(A.PedAngle + player * addr::PedStride);
        const int16_t walk = *angle;
        *angle = (int16_t)(s_fireAngle & 1023);
        fire(ped);
        *angle = walk;
    }

    static void __cdecl HudHook()
    {
        s_lastHud = GetTickCount();
        ++s_hudCount;
        reinterpret_cast<void(__cdecl*)()>(A.HudDraw)();
        if (s_hudCallback)
            s_hudCallback();
    }

    static uint32_t __cdecl InputHook()
    {
        const uint32_t packet = reinterpret_cast<uint32_t(__cdecl*)()>(A.ReadInput)();
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
                ++s_levelLoads; // auch dieselbe Karte wird bei jedem Levelstart neu gelesen
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

    // Version am Zeitstempel im PE-Kopf erkennen und mit Stichproben im Code absichern
    static bool DetectVersion(HMODULE exe)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(exe) + dos->e_lfanew);
        const uint32_t stamp = nt->FileHeader.TimeDateStamp;
        if (reinterpret_cast<uintptr_t>(exe) != 0x400000)
            return false;
        for (Addresses (*make)() : { &AddressesR2P, &AddressesLondon69, &AddressesLondon61 })
        {
            const Addresses a = make();
            if (a.timestamp != stamp)
                continue;
            for (const Addresses::Sig& s : a.sigs)
            {
                if (s.len && !hooks::BytesMatch(s.at, s.bytes, s.len))
                {
                    LOG("%s: Signatur bei 0x%06X passt nicht", a.name, (unsigned)s.at);
                    return false;
                }
            }
            A = a;
            return true;
        }
        LOG("Spielversion mit Zeitstempel 0x%08X unbekannt", stamp);
        return false;
    }

    // "call/jmp rel32" auf target umbiegen, sofern die Stelle in dieser Version bekannt ist
    static bool Redirect(uintptr_t site, uint8_t op, uintptr_t target, void* hook)
    {
        return site && target && hooks::RedirectRel32(site, op, target, hook);
    }

    bool Init(HMODULE exe)
    {
        InitializeCriticalSection(&s_mapLock);

        s_origCreateFileA = reinterpret_cast<decltype(s_origCreateFileA)>(
            hooks::PatchImport(exe, "KERNEL32.dll", "CreateFileA", reinterpret_cast<void*>(&Hook_CreateFileA)));
        if (!s_origCreateFileA)
            LOG("CreateFileA-Import nicht gefunden - Karte wird nicht erkannt");

        if (!DetectVersion(exe))
        {
            LOG("Unbekannte GTA.exe - Spielerposition und Pause deaktiviert");
            return false;
        }

        // Alle bekannten Wege zum HUD umbiegen (1.0.0.1: jmp und call, London: ein Thunk)
        const bool a = !A.HudCallJmp || Redirect(A.HudCallJmp, 0xE9, A.HudDraw, reinterpret_cast<void*>(&HudHook));
        const bool b = !A.HudCallCall || Redirect(A.HudCallCall, 0xE8, A.HudDraw, reinterpret_cast<void*>(&HudHook));
        if (!a || !b || (!A.HudCallJmp && !A.HudCallCall))
        {
            LOG("HUD-Hook fehlgeschlagen (jmp %d, call %d)", a, b);
            return false;
        }
        if (!Redirect(A.ReadInputCall, 0xE8, A.ReadInput, reinterpret_cast<void*>(&InputHook)))
            LOG("Eingabe-Hook %s - keine Twin-Stick-/Controller-Steuerung im Level", A.ReadInputCall ? "fehlgeschlagen" : "für diese Version noch nicht verfügbar");
        if (!Redirect(A.RenderCall, 0xE8, A.RenderFrame, reinterpret_cast<void*>(&RenderHook)))
            LOG("Zeichen-Hook %s - kein Seitwärtslaufen und keine Portraitfarben", A.RenderCall ? "fehlgeschlagen" : "für diese Version noch nicht verfügbar");
        if (A.PedFireCall && !Redirect(A.PedFireCall, 0xE8, A.PedFire, reinterpret_cast<void*>(&PedFireHook)))
            LOG("Schuss-Hook fehlgeschlagen - beim Seitwärtslaufen wird in Laufrichtung geschossen");

        s_supported = true;
        LOG("%s erkannt, Spiel-Hooks aktiv", A.name);
        return true;
    }

    ImportLoads GetProcAddressLoads()
    {
        return { A.IatGetProcAddress, A.LoadGetProcAddress, sizeof(A.LoadGetProcAddress) / sizeof(uintptr_t) };
    }

    ImportLoads PeekMessageLoads()
    {
        return { A.IatPeekMessageA, A.LoadPeekMessageA, sizeof(A.LoadPeekMessageA) / sizeof(uintptr_t) };
    }

    bool Supported() { return s_supported; }
    void SetHudCallback(void (*cb)()) { s_hudCallback = cb; }
    unsigned HudCount() { return s_hudCount; }
    void* PrimarySurface() { return s_supported ? *reinterpret_cast<void**>(A.MglPrimary) : nullptr; }
    int PageCount() { return s_supported ? *reinterpret_cast<int*>(A.MglPageCount) : 0; }
    bool InGame() { return s_supported && s_hudCount && GetTickCount() - s_lastHud < 500; }

    // Position dessen, was der Spieler steuert (Auto, Zug, Fußgänger ...) bzw. nullptr
    static const GtaPos* ControlledPos(int player, int& type)
    {
        type = -1;
        if (player < 0 || player > 3)
            return nullptr;

        // Plausibilitätsprüfung, damit die Spielfunktion nie außerhalb ihrer Tabellen liest
        const int t = *reinterpret_cast<int*>(A.PlayerCtrl + player * addr::PlayerStride);
        const int object = *reinterpret_cast<int*>(A.PlayerObject + player * addr::PlayerStride);
        if (t < 0 || t > 5)
            return nullptr;
        if (t == 0 && (object & 0xFFFF) >= 400)
            return nullptr;
        if (t == 2 && (object < 0 || object >= 4000))
            return nullptr;

        const GtaPos* p = reinterpret_cast<GtaPos*(__cdecl*)(int)>(A.GetPlayerPos)(player);
        if (p)
            type = t;
        return p;
    }

    bool GetPlayer(Player& out)
    {
        if (!InGame())
            return false;
        int type = -1;
        const GtaPos* p = ControlledPos(*reinterpret_cast<int*>(A.LocalPlayer), type);
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

    bool IsSinglePlayer() { return s_supported && *reinterpret_cast<int*>(A.NumPlayers) == 1; }
    int RunState() { return s_supported ? *reinterpret_cast<int*>(A.RunState) : 0; }

    // Entspricht dem F6-Handler des Spiels (1.0.0.1: 0x42F318; London hat dafür eigene Funktionen)
    void Pause()
    {
        if (A.PauseAll)
        {
            reinterpret_cast<void(__cdecl*)()>(A.PauseAll)();
            return;
        }
        reinterpret_cast<void(__cdecl*)()>(A.PauseOn)();
        *reinterpret_cast<int*>(A.RunState) = 2;
        reinterpret_cast<void(__cdecl*)()>(A.SoundPause)();
    }

    void Resume()
    {
        if (A.ResumeAll)
        {
            reinterpret_cast<void(__cdecl*)()>(A.ResumeAll)();
            return;
        }
        reinterpret_cast<void(__cdecl*)()>(A.PauseOff)();
        *reinterpret_cast<int*>(A.RunState) = 1;
        reinterpret_cast<void(__cdecl*)()>(A.SoundResume)();
    }

    void QuitLevel()
    {
        if (s_supported)
            reinterpret_cast<void(__cdecl*)()>(A.QuitLevel)();
    }

    bool GetArrowTarget(int which, float& x, float& y)
    {
        if (!InGame())
            return false;
        const uintptr_t on = which ? A.ArrowRedOn : A.ArrowMainOn;
        const uintptr_t target = which ? A.ArrowRed : A.ArrowMain;
        if (!*reinterpret_cast<int*>(on))
            return false;

        // Nur Typen zulassen, für die 0x470A90 keinen Fatal Error auslöst, und Indizes grob prüfen
        const int type = *reinterpret_cast<int*>(target);
        const int16_t index = *reinterpret_cast<int16_t*>(target + 4);
        if ((type == 0 && (uint16_t)index >= 400) || (type == 3 && (index < 0 || index >= 4000)) ||
            (type == 4 && (index < 0 || index > 3)) || (type != 0 && type != 1 && type != 3 && type != 4))
            return false;

        const GtaPos* p = reinterpret_cast<GtaPos*(__cdecl*)(uintptr_t)>(A.ArrowResolve)(target);
        if (!p)
            return false;
        x = p->x / 4194304.0f;
        y = p->y / 4194304.0f;
        return x >= 0 && x < 256 && y >= 0 && y < 256;
    }

    KeyboardInput GetKeyboardInput()
    {
        KeyboardInput k = {};
        if (!s_supported || !A.InSteer)
            return k;
        k.steer = *reinterpret_cast<int8_t*>(A.InSteer);
        k.gas = *reinterpret_cast<uint8_t*>(A.InGas);
        k.brake = *reinterpret_cast<uint8_t*>(A.InBrake);
        k.jump = *reinterpret_cast<uint8_t*>(A.InJump) != 0;
        k.fire = *reinterpret_cast<uint8_t*>(A.InFire) != 0;
        k.next = *reinterpret_cast<uint8_t*>(A.InNext) != 0;
        k.prev = *reinterpret_cast<uint8_t*>(A.InPrev) != 0;
        k.special = *reinterpret_cast<uint8_t*>(A.InSpecial) != 0;
        return k;
    }

    void SetInputFilter(uint32_t (*filter)(uint32_t)) { s_inputFilter = filter; }

    // Index des gesteuerten Fußgängers oder -1
    static int PlayerPed()
    {
        if (!A.PedStruct)
            return -1;
        const int idx = *reinterpret_cast<int*>(A.LocalPlayer);
        if (idx < 0 || idx > 3 || *reinterpret_cast<int*>(A.PlayerCtrl + idx * addr::PlayerStride) != 2)
            return -1;
        const int ped = *reinterpret_cast<int*>(A.PlayerObject + idx * addr::PlayerStride);
        return ped >= 0 && ped < 4000 ? ped : -1;
    }

    bool IsPlayerOnFoot() { return InGame() && PlayerPed() >= 0; }

    // Spielfigur des lokalen Spielers, auch während sie im Fahrzeug sitzt, bzw. nullptr
    static uint8_t* PlayerPedStruct()
    {
        if (!InGame() || !A.PedStruct || !A.PlayerPedId)
            return nullptr;
        const int idx = *reinterpret_cast<int*>(A.LocalPlayer);
        if (idx < 0 || idx > 3)
            return nullptr;
        const int16_t id = *reinterpret_cast<int16_t*>(A.PlayerPedId + idx * addr::PlayerStride);
        if (id < 0 || id >= 4000)
            return nullptr;
        return reinterpret_cast<uint8_t*>(A.PedStruct + id * addr::PedStride);
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
        const uint8_t* o = *reinterpret_cast<uint8_t* const*>(A.ActiveObjects);
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
        if (!ped || !A.ObjectDelete || !A.Objects || !A.ActiveObjects)
            return false;
        const int16_t flames = PedFlames(ped);
        if (flames <= 0 || flames >= addr::ObjectCount)
            return false;

        // Nur die Flammen genau dieser Figur entfernen, wie beim Ablauf des Objekts im Spiel (0x41CE51)
        const uint8_t* obj = reinterpret_cast<const uint8_t*>(A.Objects + flames * addr::ObjectSize);
        const auto word = [obj](int offset) { return *reinterpret_cast<const int16_t*>(obj + offset); };
        if (word(0x00) == flames && word(0x0A) == addr::ObjFlames && word(0x0C) != 0 &&
            word(0x14) == *reinterpret_cast<int16_t*>(ped + addr::PedId) && word(0x18) == 2 && InActiveObjectList(flames))
            reinterpret_cast<void(__cdecl*)(int)>(A.ObjectDelete)(flames);
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
        // cmp word ptr [esi+52h],-1 / je ... (Sprungweite je Version verschieden)
        static const uint8_t expect[] = { 0x66, 0x83, 0x7E, 0x52, 0xFF, 0x74 };
        if (!s_supported || !A.BurnDamage || !hooks::BytesMatch(A.BurnDamage, expect, sizeof(expect)))
            return false;
        uint8_t patch[5] = { 0xE8 };
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&BurnCheck) - (A.BurnDamage + 5));
        memcpy(patch + 1, &rel, 4);
        return hooks::WriteMemory(reinterpret_cast<void*>(A.BurnDamage), patch, sizeof(patch));
    }

    void SetPlayerBurnPaused(bool paused)
    {
        const int idx = paused ? PlayerPed() : -1;
        s_noBurnPed = idx >= 0 ? reinterpret_cast<const uint8_t*>(A.PedStruct + idx * addr::PedStride) : nullptr;
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
        angle = *reinterpret_cast<int16_t*>(A.PedAngle + ped * addr::PedStride) & 1023;
        return true;
    }

    void ShowPlayerPose(int anim, int angle)
    {
        const int idx = s_supported && A.PedSprite ? PlayerPed() : -1;
        if (idx < 0)
            return;
        uint8_t* ped = reinterpret_cast<uint8_t*>(A.PedStruct + idx * addr::PedStride);
        int16_t& current = *reinterpret_cast<int16_t*>(ped + addr::PedAnim);
        const int16_t keep = current;
        if (anim >= 0)
            current = static_cast<int16_t>(anim);
        if (angle >= 0)
            *reinterpret_cast<int16_t*>(ped + addr::PedAngleOfs) = static_cast<int16_t>(angle & 1023);
        reinterpret_cast<void(__cdecl*)(uint8_t*)>(A.PedSprite)(ped);
        current = keep;
    }

    int JumpKeyScancode()
    {
        return s_supported && A.KeyScancodes ? *reinterpret_cast<int*>(A.KeyScancodes + 4 * 4) : 0;
    }

    bool IgnitePlayer()
    {
        const int idx = PlayerPed();
        if (!InGame() || idx < 0 || !A.ObjectCreate)
            return false;
        uint8_t* ped = reinterpret_cast<uint8_t*>(A.PedStruct + idx * addr::PedStride);
        if (PedFlames(ped) > 0)
            return false;
        // wie 0x41CB30: Flammen etwas unterhalb der Figur erzeugen und an ihr festmachen
        const int32_t x = *reinterpret_cast<int32_t*>(ped + 0x83);
        const int32_t y = *reinterpret_cast<int32_t*>(ped + 0x87);
        const int32_t z = *reinterpret_cast<int32_t*>(ped + 0x8B);
        using Create = int(__cdecl*)(int32_t, int32_t, int32_t, int, int, int);
        const int16_t h = static_cast<int16_t>(reinterpret_cast<Create>(A.ObjectCreate)(
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
            *reinterpret_cast<int16_t*>(A.PedAngle + ped * addr::PedStride) = (int16_t)(angle & 1023);
    }

    void SetPreRenderCallback(void (*cb)()) { s_preRender = cb; }
    void SetPlayerFireAngle(int angle) { s_fireAngle = angle; }

    bool SetCameraFilter(void (*filter)(const CameraView* view, float& dx, float& dy))
    {
        if (!s_supported || !A.Camera)
            return false;
        if (!s_cameraFilter && !Redirect(A.ViewRectCall, 0xE8, A.ViewRect, reinterpret_cast<void*>(&ViewRectHook)))
            return false;
        s_cameraFilter = filter;
        return true;
    }

    bool PlayerScreenPos(float& x, float& y)
    {
        if (!InGame() || !A.Camera)
            return false;
        const int idx = *reinterpret_cast<int*>(A.LocalPlayer);
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
        const int idx = s_supported && A.PlayerCharacter ? *reinterpret_cast<int*>(A.LocalPlayer) : -1;
        if (idx < 0 || idx > 3)
            return -1;
        return *reinterpret_cast<uint8_t*>(A.PlayerCharacter + idx * addr::PlayerStride);
    }

    int PlayerDefaultRemap()
    {
        const int idx = s_supported ? *reinterpret_cast<int*>(A.LocalPlayer) : -1;
        const int character = PlayerCharacter();
        if (idx < 0 || idx > 3 || character < 0 || character > 7 || !A.CharacterColors || !A.ColorToRemap)
            return -1;
        const uint8_t color = *reinterpret_cast<uint8_t*>(A.CharacterColors + character * 4 + idx);
        return *reinterpret_cast<uint8_t*>(A.ColorToRemap + color);
    }

    bool GetPlayerPedRemap(int& remap)
    {
        const int ped = s_supported && A.PedRemap ? PlayerPed() : -1;
        if (ped < 0)
            return false;
        remap = *reinterpret_cast<uint8_t*>(A.PedRemap + ped * addr::PedStride);
        return true;
    }

    void SetPlayerPedRemap(int remap)
    {
        const int ped = s_supported && A.PedRemap && A.PedDrawRemap ? PlayerPed() : -1;
        if (ped < 0 || remap < 0 || remap > 63)
            return;
        *reinterpret_cast<uint8_t*>(A.PedRemap + ped * addr::PedStride) = (uint8_t)remap;
        *reinterpret_cast<uint8_t*>(A.PedDrawRemap + ped * addr::PedStride) = (uint8_t)remap;
    }

    std::string MapFile()
    {
        EnterCriticalSection(&s_mapLock);
        std::string p = s_mapPath;
        LeaveCriticalSection(&s_mapLock);
        return p;
    }

    unsigned MapGeneration() { return s_mapGen; }
    unsigned LevelLoads() { return s_levelLoads; }

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
            CmdMPhone = 16, CmdMissionEnd = 45, CmdBrief = 15, CmdSpeechBrief = 110,
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
            const int16_t slot = *reinterpret_cast<int16_t*>(A.ScriptObjSlot + line * 2);
            if (slot < 0 || slot >= 2000)
                return nullptr;
            return reinterpret_cast<uint8_t*>(A.ScriptObjects + slot * addr::ScriptObjSize);
        }
        int32_t* ObjectValue(int line)
        {
            uint8_t* o = ScriptObject(line);
            return o ? reinterpret_cast<int32_t*>(o + 4) : nullptr;
        }

        // Telefongruppe (MPHONE): Wer abhebt, dessen Telefon setzt das Spiel dauerhaft auf Handle -1 (0x4776F4)
        // und es klingelt nie wieder. Außerdem zählt die Mission ihren Zähler "noch zu haben" herunter und wandelt
        // ihre "künftigen" Objekte um (PARKED_ON macht aus FUTURECAR ein Auto und überschreibt Modell und Typ).
        // Für einen neuen Versuch sichern wir beim Abheben die ganze Objekttabelle und stellen nach dem Fehlschlag
        // Zähler, umgewandelte Objekte und das Telefon wieder her.
        constexpr int32_t kObjCounter = 12;
        constexpr DWORD   kRestoreTimeout = 900000;  // so lange wartet ein gescheiterter Missionsstand auf die Freigabe
        struct PhoneGroup { int line = -1, count = 0; int32_t handles[8] = {}; };
        struct MissionLog
        {
            bool active = false, failed = false;
            DWORD failTick = 0;
            int phoneLine = -1;
            int32_t phoneHandle = -1;
            int slots = 0;                   // Anzahl gesicherter Objekte
            std::vector<uint8_t> start;      // Objekttabelle beim Abheben
            std::vector<int> converted;      // von der Mission umgewandelte Objekte (bis zum Fehlschlag)
        };
        PhoneGroup s_phones;
        MissionLog s_mission;

        uint8_t* ObjectSlot(int slot) { return reinterpret_cast<uint8_t*>(A.ScriptObjects + slot * addr::ScriptObjSize); }
        int32_t SlotType(const uint8_t* entry) { int32_t t; memcpy(&t, entry, 4); return t; }

        // Höchster belegter Platz der Objekttabelle (Zeilennummer -> Platz, unbenutzte Zeilen sind 0)
        int ObjectSlotCount()
        {
            int top = -1;
            const int16_t* map = reinterpret_cast<const int16_t*>(A.ScriptObjSlot);
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

        // Gibt das Skript nach dem Fehlschlag die Telefongruppe wieder frei (ENABLE auf MPHONES/PHONE_TOGG), steht
        // die gescheiterte Mission wieder zur Wahl, als wäre sie nie angenommen worden. Das geschieht genau dort,
        // bevor das Skript prüft, welche Missionen noch zu haben sind (Liberty City: COMPARE auf 548..551).
        // Die Aufräumbefehle davor sind je Mission verschieden (die dritte in Liberty City setzt erst Power-ups).
        void RestoreMission()
        {
            if (ObjectSlotCount() != s_mission.slots || GetTickCount() - s_mission.failTick > kRestoreTimeout)
            {
                LOG("Gemerkter Missionsstand verworfen (anderes Level oder zu alt)");
                s_mission = MissionLog();
                return;
            }
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
            s_mission = MissionLog();
        }

        // Ende der Wartephase nach einem Fehlschlag; der gemerkte Missionsstand bleibt bis zur Freigabe erhalten
        void FinishFailure(int proc)
        {
            s_failedAt[proc] = 0;
        }

        bool s_quickRetry = false;
        void (*s_scriptEvent)(ScriptEvent event, int value) = nullptr;

        void __cdecl OnScriptCommand(uint32_t command, ScriptLine* line)
        {
            const int proc = *reinterpret_cast<int16_t*>(A.ScriptProc);
            if (proc < 0 || proc >= addr::ScriptProcs)
                return;

            if (s_scriptEvent)
            {
                // Text der Telefon-/Pager-Ansagen steht in <score>
                if (command == CmdBrief || command == CmdSpeechBrief || command == CmdMobileBrief)
                    s_scriptEvent(ScriptEvent::Brief, line->score);
                else if (command == CmdMissionEnd)
                    s_scriptEvent(ScriptEvent::MissionEnd, 0);
            }

            // Beendete Prozesse vergessen, damit ein neuer Prozess im selben Platz normal läuft
            if (s_failedCount)
            {
                s_failedCount = 0;
                for (int i = 0; i < addr::ScriptProcs; ++i)
                {
                    if (s_failedAt[i] && !*reinterpret_cast<int16_t*>(A.ScriptActive + i * 2))
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
            if (!s_quickRetry)
            {
                // Ohne schnellen neuen Versuch nur mitverfolgen, ob eine Telefonmission läuft (für das Speichern)
                if (command == CmdMessageBrief && line->score == kBriefMissionFailed)
                    s_mission.active = false;
                return;
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
                    s_mission.failTick = GetTickCount();
                    NoteConversions();
                }
                return;
            }
            if (command == CmdEnable && s_mission.active && s_mission.failed)
            {
                const uint8_t* o = ScriptObject(line->obj);
                const int32_t type = o ? *reinterpret_cast<const int32_t*>(o) : -1;
                if (type == kObjMPhones || type == kObjPhoneToggle)
                    RestoreMission();
            }
            if (!failedAt)
                return;
            if (!IsCleanupCommand(static_cast<uint16_t>(command)) || command == CmdDonowt || GetTickCount() - failedAt > kRetryTimeout)
            {
                FinishFailure(proc);
                return;
            }
            // SURVIVE kopiert beim ersten Durchlauf die Dauer <data> nach <fail> und zählt dort jeden Frame herunter
            if (command == CmdSurvive && *reinterpret_cast<int16_t*>(A.ScriptState + proc * 2) != 1 && line->fail > kRetryWait)
                line->fail = kRetryWait;
        }

        // Ersetzt "jmp [ebx*4+ScriptJumps]" im Interpreter, alle Register bleiben erhalten
        uintptr_t s_scriptJumps = 0;

        // Ersetzt "jmp [ebx*4+ScriptJumps]" im Interpreter (ebx = Befehl, ecx = Zeile), alle Register bleiben erhalten
        __declspec(naked) void ScriptDispatchHookEbx()
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
                push eax
                mov eax, s_scriptJumps
                mov eax, [eax + ebx * 4]
                xchg eax, [esp]
                ret
            }
        }

        // London: "jmp [edi*4+ScriptJumps]" mit edi = Befehl, ebp = Zeile
        __declspec(naked) void ScriptDispatchHookEdi()
        {
            __asm
            {
                pushfd
                pushad
                push ebp
                push edi
                call OnScriptCommand
                add esp, 8
                popad
                popfd
                push eax
                mov eax, s_scriptJumps
                mov eax, [eax + edi * 4]
                xchg eax, [esp]
                ret
            }
        }
    }

    bool EnableKeepMultiplier()
    {
        // 1.0.0.1: mov ax,[edi+6576BC]; shr ax,1; mov [edi+6576BC],ax  ->  "shr ax,1" (3 Byte) entfällt
        // London:  shr word ptr [edx+eax+Multiplikator],1 in einer eigenen Funktion  ->  8 Byte entfallen
        static const uint8_t r2p[] = { 0x66, 0x8B, 0x87, 0xBC, 0x76, 0x65, 0x00, 0x66, 0xD1, 0xE8, 0x66, 0x89, 0x87, 0xBC, 0x76, 0x65, 0x00 };
        static const uint8_t shrMem[] = { 0x66, 0xD1, 0xAC, 0x02 };
        static const uint8_t nop3[] = { 0x0F, 0x1F, 0x00 };
        static const uint8_t nop8[] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
        if (!s_supported || !A.MultHalve)
            return false;
        if (hooks::BytesMatch(A.MultHalve - 7, r2p, sizeof(r2p)))
            return hooks::WriteMemory(reinterpret_cast<void*>(A.MultHalve), nop3, sizeof(nop3));
        if (hooks::BytesMatch(A.MultHalve, shrMem, sizeof(shrMem)))
            return hooks::WriteMemory(reinterpret_cast<void*>(A.MultHalve), nop8, sizeof(nop8));
        return false;
    }

    bool EnableAllCharacters()
    {
        // "call NotEnglish" durch "xor al,al" (+ 3-Byte-NOP) ersetzen: nie überspringen
        static const uint8_t patch[5] = { 0x32, 0xC0, 0x0F, 0x1F, 0x00 };
        if (!s_supported || !A.PickPrevCheck || !A.PickNextCheck || !A.NotEnglish)
            return false;
        for (uintptr_t site : { A.PickPrevCheck, A.PickNextCheck })
        {
            int32_t rel;
            memcpy(&rel, reinterpret_cast<const void*>(site + 1), 4);
            const bool original = *reinterpret_cast<const uint8_t*>(site) == 0xE8 && site + 5 + rel == A.NotEnglish;
            const bool patched = hooks::BytesMatch(site, patch, sizeof(patch));
            if (!original && !patched)
                return false;
        }
        return hooks::WriteMemory(reinterpret_cast<void*>(A.PickPrevCheck), patch, sizeof(patch)) &&
               hooks::WriteMemory(reinterpret_cast<void*>(A.PickNextCheck), patch, sizeof(patch));
    }

    // Einstieg in den Missions-Interpreter; wird einmal installiert und von schnellem neuem Versuch, Briefings
    // und Spielständen gemeinsam benutzt
    static bool EnableScriptHook()
    {
        static bool s_installed = false;
        if (s_installed)
            return true;
        if (!s_supported || !A.ScriptDispatch || !A.ScriptJumps || !A.ScriptSurvive)
            return false;
        // jmp dword ptr [ebx*4 + ScriptJumps] (FF 24 9D) bzw. [edi*4 + ScriptJumps] (FF 24 BD)
        uint8_t dispatch[7] = { 0xFF, 0x24, static_cast<uint8_t>(A.ScriptRegsEdi ? 0xBD : 0x9D) };
        memcpy(dispatch + 3, &A.ScriptJumps, 4);
        // SURVIVE (beide Versionen): beim ersten Durchlauf ScriptState 1 -> 2 und Dauer nach <fail> (Zeile + 0x0A),
        // danach zählt <fail> jeden Frame herunter. Stichprobe: "mov ...,[ecx*2 + ScriptState]" am Anfang
        uint8_t state[4];
        memcpy(state, &A.ScriptState, 4);
        const uint8_t* survive = reinterpret_cast<const uint8_t*>(A.ScriptSurvive);
        bool stateRef = false;
        for (int i = 0; i + 4 <= 0x20 && !stateRef; ++i)
            stateRef = memcmp(survive + i, state, 4) == 0;
        if (!hooks::BytesMatch(A.ScriptDispatch, dispatch, sizeof(dispatch)) || !stateRef)
            return false;

        s_scriptJumps = A.ScriptJumps;
        const void* hook = A.ScriptRegsEdi ? reinterpret_cast<const void*>(&ScriptDispatchHookEdi) : reinterpret_cast<const void*>(&ScriptDispatchHookEbx);
        uint8_t patch[7] = { 0xE9, 0, 0, 0, 0, 0x90, 0x90 };
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(hook) - (A.ScriptDispatch + 5));
        memcpy(patch + 1, &rel, 4);
        s_installed = hooks::WriteMemory(reinterpret_cast<void*>(A.ScriptDispatch), patch, sizeof(patch));
        return s_installed;
    }

    bool EnableQuickRetry()
    {
        s_quickRetry = EnableScriptHook();
        return s_quickRetry;
    }

    bool SetScriptEventCallback(void (*cb)(ScriptEvent event, int value))
    {
        if (!EnableScriptHook())
            return false;
        s_scriptEvent = cb;
        return true;
    }

    bool MissionActive() { return s_mission.active; }

    // ---- Spielstände ----

    namespace
    {
        constexpr int kScriptLines = 5000, kScriptLineSize = 22, kProcBlockSize = 0x4A0;

        uint32_t Fnv(uint32_t h, const void* data, size_t n)
        {
            const uint8_t* p = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < n; ++i)
                h = (h ^ p[i]) * 16777619u;
            return h;
        }

        bool StateAddressesKnown()
        {
            return s_supported && A.ScriptLines && A.ScriptProcBlock && A.ScriptState && A.ScriptObjects && A.ScriptObjSlot &&
                   A.PlayerScore && A.PlayerMultiplier && A.PlayerLives && A.PlayerAmmo && A.PlayerArmor && A.PlayerCharacter;
        }
    }

    // Level (Abschnitt der mission.ini) an den unveränderlichen Feldern der Skriptzeilen erkennen: Befehl und
    // <obj> ändern sich im Spiel nie; so unterscheiden sich auch zwei Levels mit derselben Stadtkarte.
    // Die übrigen Felder benutzen manche Befehle als Speicher (SURVIVE zählt in <fail>, Befehl 89 schreibt
    // beim Levelstart <data> und <score>).
    uint32_t LevelHash()
    {
        if (!StateAddressesKnown())
            return 0;
        uint32_t h = Fnv(2166136261u, &A.timestamp, 4);   // auch je Spielversion verschieden
        for (int i = 0; i < kScriptLines; ++i)
            h = Fnv(h, reinterpret_cast<const uint8_t*>(A.ScriptLines + i * kScriptLineSize), 6); // Befehl, obj
        return h;
    }

    bool CanUseSaves() { return StateAddressesKnown() && InGame() && IsSinglePlayer(); }

    bool CaptureGameState(GameState& out)
    {
        if (!CanUseSaves())
            return false;
        const int p = *reinterpret_cast<int*>(A.LocalPlayer);
        if (p < 0 || p > 3)
            return false;
        const uintptr_t po = p * addr::PlayerStride;
        out = GameState();
        out.exeStamp = A.timestamp;
        out.levelHash = LevelHash();
        out.character = *reinterpret_cast<uint8_t*>(A.PlayerCharacter + po);
        out.score = *reinterpret_cast<int32_t*>(A.PlayerScore + po);
        out.multiplier = *reinterpret_cast<int16_t*>(A.PlayerMultiplier + po);
        out.jailCard = *reinterpret_cast<uint8_t*>(A.PlayerJailCard + po);
        out.lives = *reinterpret_cast<uint8_t*>(A.PlayerLives + po);
        memcpy(out.ammo, reinterpret_cast<const void*>(A.PlayerAmmo + po), 4);
        out.armor = *reinterpret_cast<int16_t*>(A.PlayerArmor + po);

        // Position zu Fuß oder im Fahrzeug; geladen wird sie nur auf die Figur zu Fuß
        int type = -1;
        if (const GtaPos* pos = ControlledPos(p, type))
        {
            out.hasPos = true;
            out.x = pos->x;
            out.y = pos->y;
            out.z = pos->z;
        }
        const int ped = PlayerPed();
        if (ped >= 0)
        {
            out.angle = *reinterpret_cast<const int16_t*>(A.PedStruct + ped * addr::PedStride + addr::PedAngleOfs);
            out.hasAngle = true;
        }

        const int slots = ObjectSlotCount();
        const uint8_t* objects = reinterpret_cast<const uint8_t*>(A.ScriptObjects);
        out.objects.assign(objects, objects + slots * addr::ScriptObjSize);
        const uint8_t* block = reinterpret_cast<const uint8_t*>(A.ScriptProcBlock);
        out.procBlock.assign(block, block + kProcBlockSize);
        const uint8_t* state = reinterpret_cast<const uint8_t*>(A.ScriptState);
        out.procState.assign(state, state + addr::ScriptProcs * 2);
        const uint8_t* lines = reinterpret_cast<const uint8_t*>(A.ScriptLines);
        out.lines.assign(lines, lines + kScriptLines * kScriptLineSize);
        return true;
    }

    const char* RestoreGameState(const GameState& in)
    {
        if (!CanUseSaves())
            return "nicht im Level";
        const int p = *reinterpret_cast<int*>(A.LocalPlayer);
        if (p < 0 || p > 3)
            return "kein Spieler";
        const uintptr_t po = p * addr::PlayerStride;
        if (in.exeStamp != A.timestamp || in.levelHash != LevelHash())
            return "anderes Level";
        if (in.objects.size() != (size_t)ObjectSlotCount() * addr::ScriptObjSize || in.procBlock.size() != kProcBlockSize ||
            in.procState.size() != addr::ScriptProcs * 2 || in.lines.size() != (size_t)kScriptLines * kScriptLineSize)
            return "Daten passen nicht";

        // Skript: Objekte (Zähler, Telefone, Missionsobjekte), Prozesse und Zeilen genau wie beim Speichern
        memcpy(reinterpret_cast<void*>(A.ScriptObjects), in.objects.data(), in.objects.size());
        memcpy(reinterpret_cast<void*>(A.ScriptProcBlock), in.procBlock.data(), in.procBlock.size());
        memcpy(reinterpret_cast<void*>(A.ScriptState), in.procState.data(), in.procState.size());
        memcpy(reinterpret_cast<void*>(A.ScriptLines), in.lines.data(), in.lines.size());
        s_mission = MissionLog();
        s_phones = PhoneGroup();
        memset(s_failedAt, 0, sizeof(s_failedAt));
        s_failedCount = 0;

        *reinterpret_cast<int32_t*>(A.PlayerScore + po) = in.score;
        *reinterpret_cast<int16_t*>(A.PlayerMultiplier + po) = in.multiplier;
        *reinterpret_cast<uint8_t*>(A.PlayerJailCard + po) = in.jailCard;
        *reinterpret_cast<uint8_t*>(A.PlayerLives + po) = in.lives;
        memcpy(reinterpret_cast<void*>(A.PlayerAmmo + po), in.ammo, 4);
        *reinterpret_cast<int16_t*>(A.PlayerArmor + po) = in.armor;

        // Position nur zu Fuß: Figur versetzen und die Kamera gleich mitnehmen
        const int ped = PlayerPed();
        if (in.hasPos && ped >= 0)
        {
            uint8_t* pd = reinterpret_cast<uint8_t*>(A.PedStruct + ped * addr::PedStride);
            memcpy(pd + 0x83, &in.x, 4);
            memcpy(pd + 0x87, &in.y, 4);
            memcpy(pd + 0x8B, &in.z, 4);
            if (in.hasAngle)
                memcpy(pd + addr::PedAngleOfs, &in.angle, 2);
            if (A.Camera)
            {
                CameraData& cam = CameraOf(p);
                cam.x = in.x >> 16;
                cam.y = in.y >> 16;
            }
        }
        return nullptr;
    }

    bool LookupText(int id, std::wstring& out)
    {
        out.clear();
        if (!s_supported || !A.TextReady || !A.TextBuffer || !*reinterpret_cast<int*>(A.TextReady))
            return false;
        const char* p = *reinterpret_cast<const char* const*>(A.TextBuffer);
        if (!p)
            return false;
        // Aufbau wie in der FXT-Datei: "[Schlüssel]Text\0[Schlüssel]Text\0 ... []"
        char key[16];
        const int keyLen = sprintf_s(key, "[%d]", id);
        for (int guard = 0; guard < 4 * 1024 * 1024 && *p; ++guard)
        {
            if (p[0] == '[' && p[1] == ']')
                return false;
            if (strncmp(p, key, keyLen) == 0)
            {
                p += keyLen;
                size_t n = 0;
                while (p[n] && p[n] != '[' && n < 2000)
                    ++n;
                // Die Texte des R2P-Pakets sind UTF-8, die der Original-CD Windows-1252
                UINT cp = CP_UTF8;
                int w = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, p, (int)n, nullptr, 0);
                if (w <= 0)
                {
                    cp = 1252;
                    w = MultiByteToWideChar(cp, 0, p, (int)n, nullptr, 0);
                }
                out.resize(w);
                MultiByteToWideChar(cp, 0, p, (int)n, &out[0], w);
                return !out.empty();
            }
            ++p;
        }
        return false;
    }

    bool EnableSteadyCamera()
    {
        // lea eax,[eax+eax*4] / shl eax,5 / cdq  ->  shl eax,2: die Figur darf nur ein Achtel so weit aus der
        // Bildmitte, bevor die Kamera nachzieht. Mit dem Hex-Editor schon gepatchte Stellen gelten als erledigt.
        static const uint8_t original[] = { 0x8D, 0x04, 0x80, 0xC1, 0xE0, 0x05, 0x99 };
        static const uint8_t steady[] = { 0x8D, 0x04, 0x80, 0xC1, 0xE0, 0x02, 0x99 };
        if (!s_supported || !A.SteadyCamera[0])
            return false;
        for (uintptr_t site : A.SteadyCamera)
            if (site && !hooks::BytesMatch(site, original, sizeof(original)) && !hooks::BytesMatch(site, steady, sizeof(steady)))
                return false;
        bool ok = true;
        for (uintptr_t site : A.SteadyCamera)
            if (site && hooks::BytesMatch(site, original, sizeof(original)))
                ok &= hooks::WriteMemory(reinterpret_cast<void*>(site + 5), &steady[5], 1);
        return ok;
    }
}

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
    namespace addr
    {
        constexpr int       PedCount      = 620;      // Figuren in der Tabelle (die Spielfigur liegt um 300)
        constexpr int       ObjectCount   = 0xDAC;
        constexpr int16_t   ObjFlames     = 0x2E;     // o_lfire
        constexpr int       ScriptProcs   = 32;
    }

    // Aufbau der Spieldaten. GTA.exe 1.0.0.1 und London packen ihre Strukturen lückenlos (die Vorgabewerte hier);
    // die Classics-Ausgabe von 2002 richtet sie aus, dort sind alle Strukturen größer und die Felder verschoben.
    struct Layout
    {
        // Fußgänger (PedStruct + ID * PedStride)
        int PedStride   = 235;
        int PedId       = 0x00;
        int PedAnim     = 0x17;     // int16 Animationsbild (0x88 steht, 0xAF..0xB2 fällt, 0xE9..0xED liegt)
        int PedCanFire  = 0x41;     // byte: darf schießen
        int PedHealth   = 0x44;     // byte Lebensenergie 0..100
        int PedFlames   = 0x52;     // int16 Handle der Flammen an der Figur (-1 = brennt nicht)
        int PedWeapon   = 0x59;     // int32 Waffe: 0 Faust, 1 Pistole, 2 Maschinenpistole, 3 Raketen, 4 Flammen
        int PedFireWait = 0x5D;     // int16 Schritte bis zum nächsten Schuss
        int PedPose     = 0x66;     // int32 Zustand: 2 zu Fuß, 5 erschossen, 0x15 getroffen/stirbt, 0x17 tot, 0x18 im Wasser
        int PedTarget   = 0x7C;     // int16 Figur, die ein Passant gerade angreift
        int PedPos      = 0x83;     // int32 x, y, z
        int PedAngleOfs = 0x99;     // int16 Blickrichtung
        int BulletOwner = 0x1E;     // Geschoss: int16 Figur, die es abgefeuert hat
        // Objekt-Plätze: +0 Handle, Typ, belegt, Besitzer, Art des Besitzers (2 = Fußgänger), nächstes aktives Objekt
        int ObjectSize = 130, ObjType = 0x0A, ObjUsed = 0x0C, ObjOwner = 0x14, ObjOwnerKind = 0x18, ObjNext = 0x22;
        int ObjPos = 0x2A;            // x, y, z des Objekts (je int32, 22 Nachkommabits = 1 Block)
        int PlayerStride  = 417;    // Spieler-Struktur
        int ScriptObjSize = 29;     // Objekte aus mission.ini: +0 Typ, +4 Wert/Handle
        int LineSize = 22, LineFields = 2;  // Skriptzeile: Befehl (uint16), ab LineFields fünf int32
        int TriggerSize = 29;       // Auslöser (ENABLE/DISABLE): +0 Zustand
    };

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
        // Classics: kein ViewRect, die Kameraführung rechnet den Sichtbereich selbst aus und holt direkt davor
        // die Kamera (call CameraGet) - dort hängen wir uns stattdessen ein
        uintptr_t CameraGet = 0, CameraGetCall = 0;
        // Kameraführung: "lea eax,[eax+eax*4] / shl eax,5 / cdq" berechnet, wie weit sich die Figur vom
        // Bildmittelpunkt entfernen darf, bevor die Kamera nachzieht (Abstand * 160 / Tempo)
        uintptr_t SteadyCamera[2] = {};

        uintptr_t ReadInput = 0;      // liefert das Eingabepaket des lokalen Spielers
        uintptr_t ReadInputCall = 0;  // call ReadInput in der Hauptschleife
        // Tastenzustände aus der Eingabe des Spiels (1.0.0.1: 0x436A99 ff.). Die Namen folgen der Wirkung im Paket:
        // Hupe/Pfiff setzt 0x20, nächste Waffe 0x10, vorherige Waffe 0x100 (Auswertung 0x465628 / 0x4656A0)
        uintptr_t InSteer = 0, InGas = 0, InBrake = 0, InJump = 0, InFire = 0, InHorn = 0, InWeaponNext = 0, InWeaponPrev = 0;
        uintptr_t KeyScancodes = 0;   // int je Steuerung (Scancode, +0x100 erweitert), 4 = Springen

        uintptr_t PlayerCharacter = 0;  // byte, im Hauptmenü gewähltes Portrait (0 Ulrika .. 7 Kivlov)
        uintptr_t CharacterColors = 0;  // byte [Charakter * 4 + Spieler]: Farbnummer wie in mission.ini
        uintptr_t ColorToRemap = 0;     // byte [Farbnummer]: Farbvariante 0..63

        uintptr_t ObjectCreate = 0;   // int16 __cdecl(x, y, z, Typ, Besitzer-ID, Besitzerart 1 Auto/2 Fußgänger)
        uintptr_t ObjectDelete = 0;   // void __cdecl(int16 handle), wie beim Ablauf eines Objekts
        uintptr_t Objects = 0;        // Objekt-Plätze
        uintptr_t ActiveObjects = 0;  // Liste, die das Entfernen durchsucht
        // Objektarten des Levels: Zeiger auf eine Tabelle mit je einem Zeiger auf object_info aus der Style-Datei
        // (Breite, Höhe, Tiefe als 16.16, ab +0x0C Sprite, Gewicht, aux, +0x12 Status, +0x13 Anzahl Bruchstücke);
        // die Anzahl der Arten steht 0x0C davor. Die Nummern der Arten sind in allen Städten dieselben.
        uintptr_t ObjInfo = 0;
        bool      London = false;     // London-Erweiterung: eigene Grafik für einige Objektarten

        uintptr_t LocalPlayer = 0;    // int
        uintptr_t NumPlayers = 0;     // int, 1 = Einzelspieler
        uintptr_t PlayerPedId = 0;    // int16 ID der Spielfigur, auch im Fahrzeug
        uintptr_t PlayerCtrl = 0;     // int Steuerungstyp
        uintptr_t PlayerObject = 0;   // Index des gesteuerten Objekts
        uintptr_t RunState = 0;       // 1 = läuft, 2 = pausiert

        uintptr_t MultHalve = 0;      // shr ax,1: Multiplikator halbieren (Strafe bei Verhaftung)
        uintptr_t NotEnglish = 0;     // Hauptmenü: "nicht Englisch" -> Frauen überspringen
        uintptr_t PickPrevCheck = 0, PickNextCheck = 0;

        uintptr_t ScriptDispatch = 0; // jmp [Befehl*4+ScriptJumps] im Interpreter
        uintptr_t ScriptJumps = 0;
        // Register an dieser Stelle (3 ebx, 6 esi, 7 edi / 1 ecx, 5 ebp): 1.0.0.1 ebx = Befehl, ecx = Zeile;
        // London edi, ebp; Classics esi, ebp
        uint8_t   ScriptCmdReg = 3, ScriptLineReg = 1;
        uintptr_t ScriptSurvive = 0;  // Befehl SURVIVE
        uintptr_t ScriptProc = 0, ScriptState = 0, ScriptActive = 0, ScriptObjSlot = 0, ScriptObjects = 0;
        uintptr_t ScriptLines = 0;     // 5000 Zeilen zu 22 Byte (ScriptLine), SURVIVE zählt darin herunter
        uintptr_t ScriptProcBlock = 0; // Tabellen je Prozess (aktuelle Zeile, Verkettung ...), 0x4A0 Byte
        uintptr_t Triggers = 0;        // 210 Einträge zu 29 Byte, +0 Zustand (1 an, 4 aus): ENABLE/DISABLE z.B. auf
                                       // Telefongruppen (1.0.0.1: 0x48AB30 / 0x48AB90)

        // Spielerwerte (Spieler 0, weitere im Abstand PlayerStride)
        uintptr_t PlayerScore = 0, PlayerMultiplier = 0, PlayerJailCard = 0, PlayerLives = 0, PlayerAmmo = 0, PlayerArmor = 0;

        // Zeichenpuffer: das Spiel schreibt Welt und HUD direkt in den Speicher der aktiven MGL-Seite und legt
        // Zeiger und Zeilenlänge nach jedem Seitenwechsel hier ab (1.0.0.1: 0x42F890)
        uintptr_t DrawBits = 0, DrawPitch = 0, DrawBytes = 0, DrawWidth = 0, DrawHeight = 0;

        // HUD in ein eigenes Bild umleiten (nur wo nötig, 0 = geht nicht). Die Zeichenroutinen holen sich die Zeilen
        // aus einer Tabelle von Zeilenanfängen und kennen Bildmaße und Bildzeiger noch einmal für sich:
        // DrawSetRows(bits, pitch, height) füllt die Tabelle, DrawSetTarget(width, height, bits) setzt das Übrige.
        // Wo etwas hingehört, rechnet das HUD aus dem Sichtfenster des Spielers, das das Spiel je Bild aus dessen
        // Kamera hierher kopiert: Breite, Höhe, Bildmitte und der Maßstab, mit dem Weltpunkte ins Bild fallen.
        uintptr_t DrawSetRows = 0, DrawSetTarget = 0;
        uintptr_t ViewWidth = 0, ViewHeight = 0, ViewCenterX = 0, ViewCenterY = 0, ViewZoom = 0;

        // Treffer. Kugel trifft Figur (edi = Figur, esi = Geschoss): das Spiel prüft "stirbt schon?" (Animation >= 0x89)
        // und lässt sie sonst tot umfallen, ohne die Lebensenergie anzufassen; BulletHitSkip ist das Ziel dieses jge.
        // Faustschlag: das Opfer bekommt Animation 0xAF (zweite Stelle: Gegenschlag, der Angreifer fällt).
        // Passant greift sein Ziel (Figur +0x7C) an: es bekommt Animation 0x2B.
        uintptr_t BulletHit = 0, BulletHitSkip = 0;
        bool      BulletHitMovEcx = false;  // London: die Prüfung beginnt mit "mov ecx,89h"
        uintptr_t PunchHit[2] = {};
        uint8_t   PunchReg[2] = {};         // Register mit dem Opfer: 5 ebp, 6 esi, 7 edi
        uintptr_t NpcHit = 0;
        uint8_t   NpcHitReg = 0;            // Register mit dem Angreifer
        int       NpcHitLen = 10;           // Länge dieses Befehls (Classics adressiert ohne Indexregister: 9)

        // Fortschritt, wie ihn die Pausenanzeige des Spiels abwechselnd einblendet (1.0.0.1: 0x473470 ff.): Zielpunktzahl,
        // Zahl der Missionen und Geheimnisse sowie je ein Zähler aus dem Missionsskript (Platz in ScriptObjects, -1 =
        // keiner), der vom Startwert (+8) herunterzählt (+4)
        uintptr_t TargetScore = 0, MissionsTotal = 0, MissionsCounter = 0, SecretsTotal = 0, SecretsCounter = 0;
        uintptr_t CarDamage = 0;      // byte je Fahrzeug: Schaden 0..100 (bei 100 explodiert es)
        int       CarStride = 664;
        uintptr_t Explode = 0;        // __cdecl(x, y, z, Auslöser): Explosion erzeugen, alle Explosionen laufen hier durch
        uintptr_t ExplodeThunk = 0;   // London: jmp Explode
        uint8_t   ExplodeProlog[8] = { 0x83, 0xEC, 0x04, 0x33, 0xC9 };   // ohne Thunk: erste ganze Befehle der Funktion
        int       ExplodeLen = 5;     // (1.0.0.1: sub esp,4 / xor ecx,ecx), werden in ein Sprungstück ausgelagert

        uintptr_t TextReady = 0;      // int: FXT-Texte entschlüsselt
        uintptr_t TextBuffer = 0;     // char*: "[Schlüssel]Text\0..." bis "[]"
        uintptr_t MglPrimary = 0;     // IDirectDrawSurface* (Primary von MGL)
        uintptr_t MglPageCount = 0;   // Anzahl Seiten der Flip-Kette
        uintptr_t IatGetProcAddress = 0, IatPeekMessageA = 0;
        uintptr_t LoadGetProcAddress[1] = {};   // "mov reg,[IAT]", holt DirectDrawCreate
        uintptr_t DDCreateCall = 0, DDCreateCell = 0;   // "call [Zelle]": MGL ruft DirectDrawCreate (nur wo nötig)
        uintptr_t LoadPeekMessageA[3] = {};     // "mov reg,[IAT]" in den Nachrichtenschleifen

        Layout lay;                   // Aufbau der Strukturen in dieser Version

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
        a.InFire = 0x6008F8; a.InHorn = 0x600A1C; a.InWeaponNext = 0x600A48; a.InWeaponPrev = 0x600A84;
        a.KeyScancodes = 0x600A50;
        a.PlayerCharacter = 0x657764; a.CharacterColors = 0x4C1668; a.ColorToRemap = 0x4C0110;
        // Flammen an Figuren: 0x41CB30/0x404FD0 erzeugen mit ObjectCreate ein o_lfire an der Figur und merken
        // das Handle in Ped+0x52; solange es gesetzt ist, zieht 0x451AFE jeden Frame 1 Lebenspunkt ab.
        // Endet das Objekt, löst 0x41D200 es von der Figur (Ped+0x52 = -1).
        a.ObjectCreate = 0x41BAA0; a.ObjectDelete = 0x41D380; a.Objects = 0x50F5F8; a.ActiveObjects = 0x57E750;
        a.ObjInfo = 0x4C9E8C;         // gefüllt in 0x404020
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
        a.ScriptLines = 0x6FBC08; a.ScriptProcBlock = 0x6FB768; a.Triggers = 0x7386A0;
        a.PlayerScore = 0x6576B8; a.PlayerMultiplier = 0x6576BC; a.PlayerJailCard = 0x6576BE;
        a.PlayerLives = 0x65774C; a.PlayerAmmo = 0x657718; a.PlayerArmor = 0x65775C;
        a.DrawBits = 0x737034; a.DrawPitch = 0x5F3AB4; a.DrawBytes = 0x5F3AB8; a.DrawWidth = 0x5F5560; a.DrawHeight = 0x5F5564;
        a.BulletHit = 0x425065; a.BulletHitSkip = 0x425108;
        a.PunchHit[0] = 0x44ED70; a.PunchReg[0] = 5; a.PunchHit[1] = 0x44EE01; a.PunchReg[1] = 7;
        a.NpcHit = 0x44C0B1; a.NpcHitReg = 7;
        a.TargetScore = 0x736A20; a.MissionsTotal = 0x736A10; a.MissionsCounter = 0x6FB930;
        a.SecretsTotal = 0x6FB934; a.SecretsCounter = 0x7169C0;
        a.CarDamage = 0x73B077;   // 0x497220
        a.Explode = 0x404100;
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
        a.London = true;
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
        a.InFire = 0x572DC8; a.InHorn = 0x572EEC; a.InWeaponNext = 0x572F18; a.InWeaponPrev = 0x572F54;
        a.KeyScancodes = 0x572F20;
        // Fußgänger wie in 1.0.0.1 (235 Byte), Position über 0x47E2B0
        a.PedStruct = 0x7E2370; a.PedAngle = 0x7E2409; a.PedRemap = 0x7E23C8; a.PedDrawRemap = 0x7E2403;
        a.PlayerPedId = 0x80CF2A; a.PlayerCharacter = 0x80D0C4;
        // Kameraführung endet wie in 1.0.0.1 mit ViewRect(player); Sichtbereich dort als (h + 0x180) * 160 / zoom
        a.Camera = 0x80CF40; a.ViewRect = 0x450BC0; a.ViewRectCall = 0x450B87;
        a.SteadyCamera[0] = 0x451581;
        // Objekte wie in 1.0.0.1 (130 Byte); Anzünden in 0x465FEF, Sprite der Figuren in 0x467FE0
        a.ObjectCreate = 0x40222F; a.ObjectDelete = 0x401924; a.Objects = 0x75CDC0; a.ActiveObjects = 0x7CBF18;
        a.ObjInfo = 0x7E22B8;         // 0x467910
        a.PedSprite = 0x467FE0;
        a.BurnDamage = 0x47A760;  // Brennschaden: cmp [esi+52h],-1 / je / Lebensenergie - 1 (anders geschrieben)
        a.PedFire = 0x402AEF; a.PedFireCall = 0x47A71D;
        a.MultHalve = 0x48307E;   // eigene Funktion 0x483070 halbiert den Multiplikator (0x80D01C)
        a.NotEnglish = 0x40286F; a.PickPrevCheck = 0x439D19; a.PickNextCheck = 0x439D69;
        // Missionsskript: Daten gegenüber 1.0.0.1 um 8 Byte verschoben, gleiche Befehls- und Objektnummern
        a.ScriptDispatch = 0x45F639; a.ScriptJumps = 0x461490; a.ScriptCmdReg = 7; a.ScriptLineReg = 5; a.ScriptSurvive = 0x456B00;
        a.ScriptProc = 0x736A00; a.ScriptState = 0x6DB658; a.ScriptActive = 0x6FB760;
        a.ScriptObjSlot = 0x6DB760; a.ScriptObjects = 0x6796F0;
        a.ScriptLines = 0x6FBC00; a.ScriptProcBlock = 0x6FB760; a.Triggers = 0x832FB8;   // 0x49B160
        a.PlayerScore = 0x80D018; a.PlayerMultiplier = 0x80D01C; a.PlayerJailCard = 0x80D01E;
        a.PlayerLives = 0x80D0AC; a.PlayerAmmo = 0x80D078; a.PlayerArmor = 0x80D0BC;
        a.LocalPlayer = 0x80D5B0; a.NumPlayers = 0x80D5AC;
        a.PlayerCtrl = 0x80CFF5; a.PlayerObject = 0x80CFF9; a.RunState = 0x572ACC;
        a.DrawBits = 0x570BC4; a.DrawPitch = 0x5605F4; a.DrawBytes = 0x5605F8; a.DrawWidth = 0x5620A0; a.DrawHeight = 0x5620A4; // 0x41F730
        a.BulletHit = 0x4B3271; a.BulletHitSkip = 0x4B3318; a.BulletHitMovEcx = true;
        a.PunchHit[0] = 0x4772ED; a.PunchReg[0] = 7; a.PunchHit[1] = 0x477353; a.PunchReg[1] = 6;
        a.NpcHit = 0x473AD9; a.NpcHitReg = 6;
        a.TargetScore = 0x736A18; a.MissionsTotal = 0x736A08; a.MissionsCounter = 0x6FB928;   // 0x452A70 ff.
        a.SecretsTotal = 0x6FB92C; a.SecretsCounter = 0x7169B8;
        a.CarDamage = 0x5105D7;   // 0x4112A0
        a.Explode = 0x433F50; a.ExplodeThunk = 0x40105F;
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
        a.London = true;
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
        a.InFire = 0x5749D0; a.InHorn = 0x574AF4; a.InWeaponNext = 0x574B20; a.InWeaponPrev = 0x574B5C;
        a.KeyScancodes = 0x574B28;
        a.PedStruct = 0x7E3F78; a.PedAngle = 0x7E4011; a.PedRemap = 0x7E3FD0; a.PedDrawRemap = 0x7E400B;
        a.PlayerPedId = 0x80EB32; a.PlayerCharacter = 0x80ECCC;
        a.Camera = 0x80EB48; a.ViewRect = 0x450F80; a.ViewRectCall = 0x450F47;
        a.SteadyCamera[0] = 0x451941;
        a.ObjectCreate = 0x402275; a.ObjectDelete = 0x401924; a.Objects = 0x75E9C8; a.ActiveObjects = 0x7CDB20;
        a.ObjInfo = 0x7E3EC0;
        a.PedSprite = 0x4687F0;
        a.BurnDamage = 0x47AFF0;
        a.PedFire = 0x402B3F; a.PedFireCall = 0x47AFAD;
        a.MultHalve = 0x48391E;
        a.NotEnglish = 0x4028BA; a.PickPrevCheck = 0x439FE9; a.PickNextCheck = 0x43A039;
        // Missionsskript: 156 Befehle (Drive-by-Befehle angehängt), Nummern bis dahin wie in 1.0.0.1
        a.ScriptDispatch = 0x45FDF9; a.ScriptJumps = 0x461C80; a.ScriptCmdReg = 7; a.ScriptLineReg = 5; a.ScriptSurvive = 0x456FE0;
        a.ScriptProc = 0x738608; a.ScriptState = 0x6DD260; a.ScriptActive = 0x6FD368;
        a.ScriptObjSlot = 0x6DD368; a.ScriptObjects = 0x67B2F8;
        a.ScriptLines = 0x6FD808; a.ScriptProcBlock = 0x6FD368; a.Triggers = 0x834BC8;   // 0x49BA90
        a.PlayerScore = 0x80EC20; a.PlayerMultiplier = 0x80EC24; a.PlayerJailCard = 0x80EC26;
        a.PlayerLives = 0x80ECB4; a.PlayerAmmo = 0x80EC80; a.PlayerArmor = 0x80ECC4;
        a.LocalPlayer = 0x80F1B8; a.NumPlayers = 0x80F1B4;
        a.PlayerCtrl = 0x80EBFD; a.PlayerObject = 0x80EC01; a.RunState = 0x5746D4;
        a.DrawBits = 0x5727CC; a.DrawPitch = 0x5621FC; a.DrawBytes = 0x562200; a.DrawWidth = 0x563CA8; a.DrawHeight = 0x563CAC; // 0x41F9C0
        a.BulletHit = 0x4B3F73; a.BulletHitSkip = 0x4B401A; a.BulletHitMovEcx = true;
        a.PunchHit[0] = 0x477B7D; a.PunchReg[0] = 7; a.PunchHit[1] = 0x477BE3; a.PunchReg[1] = 6;
        a.NpcHit = 0x47432D; a.NpcHitReg = 6;
        a.TargetScore = 0x738620; a.MissionsTotal = 0x738610; a.MissionsCounter = 0x6FD530;   // 0x452E30 ff.
        a.SecretsTotal = 0x6FD534; a.SecretsCounter = 0x7185C0;
        a.CarDamage = 0x511A63; a.CarStride = 668;   // 0x4114E0: die Fahrzeuge sind hier 4 Byte größer
        a.Explode = 0x434230; a.ExplodeThunk = 0x40105F;
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

    // Grand Theft Auto.exe der Rockstar-Classics-Ausgabe (Build vom 11.11.2002, Ordner WINO). Eigener Compilerlauf
    // mit ausgerichteten Strukturen: Figuren 256 statt 235 Byte, Spieler 444 statt 417, Skriptzeilen 24 statt 22,
    // Skriptobjekte und Auslöser 32 statt 29, Fahrzeuge 688 statt 664. Der Sichtbereich wird am Ende der
    // Kameraführung an Ort und Stelle berechnet, SURVIVE steht im Interpreter selbst. Die Bibliothek MGL ist dagegen
    // fast unverändert. Zuordnung über Zeichenketten, Konstanten und die Aufrufstruktur.
    static Addresses AddressesClassics()
    {
        Addresses a;
        a.name = "Grand Theft Auto.exe (Rockstar Classics 2002)";
        a.timestamp = 0x3DCFD617;
        Layout& l = a.lay;
        l.PedStride = 256; l.PedAnim = 0x18; l.PedCanFire = 0x46; l.PedHealth = 0x49; l.PedFlames = 0x58;
        l.PedWeapon = 0x60; l.PedFireWait = 0x64; l.PedPose = 0x70; l.PedPos = 0x90; l.PedAngleOfs = 0xA8;
        l.PedTarget = 0x86;
        l.ObjectSize = 136; l.ObjNext = 0x24; l.ObjPos = 0x2C;
        l.PlayerStride = 444; l.ScriptObjSize = 32; l.LineSize = 24; l.LineFields = 4; l.TriggerSize = 32;
        a.HudDraw = 0x483390; a.HudCallJmp = 0x430D94; a.HudCallCall = 0x430D71;
        a.GetPlayerPos = 0x462D50;
        // F6-Handler 0x430FC4: PauseOn, RunState = 2, Ton anhalten
        a.PauseOn = 0x481510; a.PauseOff = 0x481530; a.SoundPause = 0x404DB0; a.SoundResume = 0x404DE0;
        a.QuitLevel = 0x430A00;
        a.ArrowResolve = 0x486180;
        a.ArrowMainOn = 0x784234; a.ArrowMain = 0x784744; a.ArrowRedOn = 0x78472C; a.ArrowRed = 0x785108;
        a.RenderFrame = 0x430D40; a.RenderCall = 0x41499E;   // Hauptschleife 0x4148xx
        a.PedFire = 0x4532F0; a.PedFireCall = 0x45D040;
        // Figuren: 620 Stück zu 256 Byte (Position über 0x45FB00)
        a.PedStruct = 0x7284E0; a.PedAngle = 0x728588; a.PedRemap = 0x72853E; a.PedDrawRemap = 0x728580;
        a.PedSprite = 0x44F100;
        // Objekte zu 136 Byte; Flammen wie in 1.0.0.1 (Liste der aktiven Objekte in 0x44EA30)
        a.ObjectCreate = 0x44D3D0; a.ObjectDelete = 0x44EB90; a.Objects = 0x6B40D0; a.ActiveObjects = 0x728430;
        a.ObjInfo = 0x728444;         // 0x44ED60
        a.BurnDamage = 0x45D088;
        // Kameraführung 0x43B910, Kamera wie in 1.0.0.1 aufgebaut (Sichtbereich wieder 0x14 davor)
        a.Camera = 0x74F160; a.CameraGet = 0x4644E0; a.CameraGetCall = 0x43BB33;
        a.SteadyCamera[0] = 0x43B9EC; a.SteadyCamera[1] = 0x43C87F;
        a.ReadInput = 0x432E00; a.ReadInputCall = 0x414923;
        // Tastenzustände aus dem Druck-Handler 0x4331F0 (gleiche Reihenfolge und Paketbits wie 1.0.0.1)
        a.InSteer = 0x513650; a.InGas = 0x5134D9; a.InBrake = 0x5135F0; a.InJump = 0x5134DA;
        a.InFire = 0x5134D8; a.InHorn = 0x5135F1; a.InWeaponNext = 0x51361C; a.InWeaponPrev = 0x513651;
        a.KeyScancodes = 0x513620;
        a.PlayerCharacter = 0x74F2FC; a.CharacterColors = 0x4B21E0; a.ColorToRemap = 0x4B20B0;
        a.LocalPlayer = 0x74F83C; a.NumPlayers = 0x74F838; a.PlayerPedId = 0x74F14A;
        a.PlayerCtrl = 0x74F218; a.PlayerObject = 0x74F21C; a.RunState = 0x513240;
        a.MultHalve = 0x464BEE;   // eigene Funktion 0x464BE0: shr word ptr [Spieler*4+Multiplikator],1
        a.NotEnglish = 0x47DA90; a.PickPrevCheck = 0x429A4A; a.PickNextCheck = 0x429A9A;
        // Missionsskript: Interpreter 0x4471A0, Zeilen zu 24 Byte, Objekte und Auslöser zu 32 Byte; die Tabellen je
        // Prozess liegen wie in 1.0.0.1 direkt vor den Zeilen. SURVIVE (Befehl 4) steht im Interpreter selbst.
        a.ScriptDispatch = 0x44722A; a.ScriptJumps = 0x44A8D8; a.ScriptCmdReg = 6; a.ScriptLineReg = 5;
        a.ScriptSurvive = 0x4472FB;
        a.ScriptProc = 0x6B3B70; a.ScriptState = 0x6560B8; a.ScriptActive = 0x6761C0;
        a.ScriptObjSlot = 0x6561C0; a.ScriptObjects = 0x5F30E8;
        a.ScriptLines = 0x676660; a.ScriptProcBlock = 0x6761C0; a.Triggers = 0x771628;   // 0x474D30 / 0x474D70
        a.PlayerScore = 0x74F23C; a.PlayerMultiplier = 0x74F240; a.PlayerJailCard = 0x74F242;
        a.PlayerLives = 0x74F2DF; a.PlayerAmmo = 0x74F2A0; a.PlayerArmor = 0x74F2F4;
        a.TargetScore = 0x6B3B84; a.MissionsTotal = 0x6B3B78; a.MissionsCounter = 0x676388;   // 0x43D1C0 ff.
        a.SecretsTotal = 0x67638C; a.SecretsCounter = 0x693B28;
        a.CarDamage = 0x4BE344; a.CarStride = 688;   // 0x40A1E0
        // Treffer: Kugel in der Geschossführung 0x4879C0 (wie London mit "mov ecx,89h"), Faustschlag und Gegenschlag in
        // 0x45A3B0 (esi = Angreifer, edi = Opfer), Angriff eines Passanten in 0x456FB0
        a.BulletHit = 0x488181; a.BulletHitSkip = 0x488228; a.BulletHitMovEcx = true;
        a.PunchHit[0] = 0x45A6D8; a.PunchReg[0] = 7; a.PunchHit[1] = 0x45A757; a.PunchReg[1] = 6;
        a.NpcHit = 0x457DB2; a.NpcHitReg = 6; a.NpcHitLen = 9;
        a.Explode = 0x425170; a.ExplodeLen = 7;      // push ecx / xor ecx,ecx / mov [esp],ecx
        const uint8_t prolog[] = { 0x51, 0x33, 0xC9, 0x89, 0x4C, 0x24, 0x00 };
        memcpy(a.ExplodeProlog, prolog, sizeof(prolog));
        a.TextReady = 0x775550; a.TextBuffer = 0x775578;   // aus GetText 0x47D9A0
        a.MglPrimary = 0x4B5004; a.MglPageCount = 0x4B5000;
        a.DrawBits = 0x511904; a.DrawPitch = 0x503218; a.DrawBytes = 0x50321C; a.DrawWidth = 0x504CC0; a.DrawHeight = 0x504CC4; // 0x414B62
        a.DrawSetRows = 0x497BFA; a.DrawSetTarget = 0x486830;
        a.ViewWidth = 0x5C0C00; a.ViewHeight = 0x5BFAB0; a.ViewCenterX = 0x5BFBE4; a.ViewCenterY = 0x5BFBE8; a.ViewZoom = 0x5C1C10; // 0x43B780
        a.IatGetProcAddress = 0x4A70DC; a.IatPeekMessageA = 0x4A71CC;
        a.LoadGetProcAddress[0] = 0x48D2D3;
        a.DDCreateCall = 0x48D397; a.DDCreateCell = 0x787320;
        a.LoadPeekMessageA[0] = 0x4159FF; a.LoadPeekMessageA[1] = 0x489ECB; a.LoadPeekMessageA[2] = 0x48C5EC;
        const Addresses::Sig sigs[] = {
            { 0x483390, { 0xA1, 0x70, 0x2F, 0x50, 0x00, 0x55, 0x33, 0xED, 0x3B, 0xC5 }, 10 }, // HudDraw
            { 0x462D50, { 0x8B, 0x44, 0x24, 0x04, 0x56, 0x33, 0xF6, 0x8D, 0x0C, 0xC0 }, 10 }, // GetPlayerPos
            { 0x481510, { 0xC6, 0x05, 0xB8, 0x46, 0x78, 0x00, 0x01 }, 7 },                   // PauseOn
            { 0x481530, { 0xC6, 0x05, 0xB8, 0x46, 0x78, 0x00, 0x00, 0xC3 }, 8 },             // PauseOff
            { 0x404DB0, { 0xA0, 0xF0, 0xDD, 0x4B, 0x00, 0x84, 0xC0 }, 7 },                   // SoundPause
            { 0x404DE0, { 0xA0, 0xF0, 0xDD, 0x4B, 0x00, 0x84, 0xC0 }, 7 },                   // SoundResume
            { 0x430FC4, { 0x39, 0x1D, 0x38, 0xF8, 0x74, 0x00, 0x0F, 0x85 }, 8 },             // F6-Handler: NumPlayers
            { 0x430FD0, { 0x39, 0x1D, 0x40, 0x32, 0x51, 0x00, 0x75, 0x1A }, 8 },             // F6-Handler: RunState
            { 0x430A00, { 0xC7, 0x05, 0x2C, 0x32, 0x51, 0x00, 0x02, 0x00, 0x00, 0x00 }, 10 }, // QuitLevel
            { 0x486180, { 0x8B, 0x44, 0x24, 0x04, 0x56, 0x57, 0x8B, 0x08, 0x83, 0xF9 }, 10 }, // ArrowResolve
            { 0x4331F0, { 0x8B, 0x44, 0x24, 0x04, 0x53, 0x83, 0xF8, 0x09, 0x0F, 0x87 }, 10 }, // Druck-Handler der Steuerung
        };
        std::copy(std::begin(sigs), std::end(sigs), a.sigs);
        return a;
    }

    static Addresses A;               // erkannte Version
    static const Layout& L = A.lay;

#pragma pack(push, 1)
    struct GtaPos
    {
        int32_t x, y, z;        // Festkomma, 22 Nachkommabits = 1 Block
        int16_t w, h;
        int16_t speed;
        int16_t angle;
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

    // Eine Zeile aus mission.ini im Speicher des Spiels, "<nr> BEFEHL <obj> <succ> <fail> <data> <score>": die
    // Befehlsnummer (uint16, Index in der Befehlstabelle 0x4C26B8) und ab LineFields fünf int32
    enum LineField { FObj, FSucc, FFail, FData, FScore };
    static uint16_t LineCommand(const uint8_t* line) { uint16_t c; memcpy(&c, line, 2); return c; }
    static int32_t& Field(uint8_t* line, LineField f) { return *reinterpret_cast<int32_t*>(line + L.LineFields + f * 4); }
    static int32_t Field(const uint8_t* line, LineField f) { int32_t v; memcpy(&v, line + L.LineFields + f * 4, 4); return v; }
    static uint8_t* LineAt(int index) { return reinterpret_cast<uint8_t*>(A.ScriptLines + index * L.LineSize); }

    static bool s_supported = false;
    static volatile DWORD s_lastHud = 0;
    static volatile unsigned s_hudCount = 0;
    static void (*s_hudCallback)() = nullptr;

    static CRITICAL_SECTION s_mapLock;
    static std::string s_mapPath;
    static std::string s_missionPath;   // Missionsskript des Levels (mission.ini, London: uk/missuk.ini)
    static volatile unsigned s_mapGen = 0;
    static volatile unsigned s_levelLoads = 0;

    static HANDLE (WINAPI* s_origCreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) = nullptr;

    static uint32_t (*s_inputFilter)(uint32_t) = nullptr;
    static void (*s_preRender)() = nullptr;
    static int s_fireAngle = -1;
    static void (*s_cameraFilter)(const CameraView*, float&, float&) = nullptr;
    static void (*s_beforeHud)() = nullptr;
    static void (*s_hudDrawer)(void (*drawHud)()) = nullptr;
    static void (*s_cameraOverride)() = nullptr;
    static bool s_inViewRect = false;
    static float s_viewRectScale = 1.0f;   // Sichtbereich der Spiellogik (Verkehr entsteht knapp außerhalb)
    static int s_vehicleHeight = 0, s_vehicleZoom = 0; // feste Fahrzeugkamera, 0 = wie im Spiel
    static float s_heightBlend = 0;
    static DWORD s_heightTick = 0;
    static void RestoreRenderCamera();

    static int PlayerPed();
    static const GtaPos* ControlledPos(int player, int& type);

    static CameraData& CameraOf(int player)
    {
        return *reinterpret_cast<CameraData*>(A.Camera + player * L.PlayerStride);
    }

    static bool HaveViewRect() { return A.ViewRect || A.CameraGetCall; }

    // Sichtbereich aus der Kamera berechnen: mit der Funktion des Spiels oder, wo es sie nicht gibt (Classics),
    // mit derselben Rechnung: halbe Breite (Höhe + 384) * 160 / zoom, halbe Höhe drei Viertel davon
    static void CallViewRect(int player)
    {
        if (A.ViewRect)
        {
            reinterpret_cast<void(__cdecl*)(int)>(A.ViewRect)(player);
            return;
        }
        const CameraData& cam = CameraOf(player);
        if (cam.zoom <= 0)
            return;
        int32_t* rect = reinterpret_cast<int32_t*>(A.Camera + player * L.PlayerStride - 0x14);
        const int halfW = (cam.height + 0x180) * 160 / cam.zoom, halfH = halfW * 3 / 4;
        rect[0] = std::max(0, cam.x - halfW);
        rect[1] = std::max(0, cam.x + halfW);
        rect[2] = std::max(0, cam.y - halfH);
        rect[3] = std::max(0, cam.y + halfH);
        rect[4] = halfW;
    }

    // Sichtbereich nach dem Zeichnen für die Spiellogik vergrößern: Verkehr entsteht knapp außerhalb davon und Autos
    // außerhalb werden entfernt. Das Zeichnen bekommt ihn nie so groß, denn einen größeren Bereich als bei schneller
    // Fahrt zeichnet das Spiel nicht vollständig. Er liegt direkt vor der Kamera: links, rechts, oben, unten,
    // halbe Breite (Weltpixel); die nächste Kameraführung berechnet ihn neu.
    static void EnlargeViewRectForLogic()
    {
        if (s_viewRectScale <= 1.0f || !HaveViewRect())
            return;
        const int player = *reinterpret_cast<int*>(A.LocalPlayer);
        if (player < 0 || player > 3)
            return;
        int32_t* rect = reinterpret_cast<int32_t*>(A.Camera + player * L.PlayerStride - 0x14);
        const int cx = (rect[0] + rect[1]) / 2, cy = (rect[2] + rect[3]) / 2;
        const int halfW = (int)(rect[4] * s_viewRectScale), halfH = halfW * 3 / 4;
        rect[0] = std::max(0, cx - halfW);
        rect[1] = std::max(0, cx + halfW);
        rect[2] = std::max(0, cy - halfH);
        rect[3] = std::max(0, cy + halfH);
        rect[4] = halfW;
    }

    // Feste Fahrzeugkamera, beim Ein- und Aussteigen überblendet. Das Spiel zoomt mit dem Tempo über Höhe und zoom
    // heraus (stehend etwa 80/126, schnell 268/90) und schreibt beide jeden Tick neu, deshalb wirkt das nicht auf
    // seine eigene Glättung zurück. focal folgt aus zoom wie in der Kameraführung (Bezugsbreite * zoom / 320).
    static void ApplyVehicleHeight(int player, CameraData& cam)
    {
        const DWORD now = GetTickCount();
        const float dt = s_heightTick ? std::min((now - s_heightTick) / 1000.0f, 0.1f) : 0.0f;
        s_heightTick = now;
        const int type = *reinterpret_cast<int*>(A.PlayerCtrl + player * L.PlayerStride);
        const bool want = s_vehicleHeight > 0 && type == 0;
        s_heightBlend = std::clamp(s_heightBlend + (want ? dt : -dt) / 0.8f, 0.0f, 1.0f);
        if (s_heightBlend <= 0 || s_vehicleHeight <= 0 || s_vehicleZoom <= 0)
            return;
        const float t = s_heightBlend * s_heightBlend * (3 - 2 * s_heightBlend);
        cam.height = (int)std::lround(cam.height + (s_vehicleHeight - cam.height) * t);
        cam.zoom = (int)std::lround(cam.zoom + (s_vehicleZoom - cam.zoom) * t);
        cam.focal = cam.baseW * cam.zoom / 320;
    }

    // Kamera des Spielers als CameraView; false, solange sie nicht der Spielfigur bzw. ihrem Fahrzeug folgt
    static bool MakeView(int player, CameraView& v)
    {
        int type = -1;
        const GtaPos* p = ControlledPos(player, type);
        const CameraData& cam = CameraOf(player);
        if (!p || type > 2 || cam.focal <= 0 || cam.height + (p->z >> 16) <= 0)
            return false;
        v = { (float)cam.x, (float)cam.y, p->x / 65536.0f, p->y / 65536.0f, p->z / 65536.0f,
              type == 2 ? 10.0f : 24.0f, (float)cam.height, (float)cam.focal,
              cam.squash ? 5.0f / 6.0f : 1.0f, (float)cam.centerX, (float)cam.centerY, type, p->angle, (float)cam.zoom };
        return true;
    }

    // Zwischen Kameraführung und Sichtbereich: Höhe im Fahrzeug festlegen und Versatz der Kamerasteuerung
    // aufschlagen. Die Glättung des Spiels rechnet mit dem Mittelpunkt ohne unseren Versatz weiter, der
    // Sichtbereich passt zum Bild.
    static void BeforeViewRect(int player)
    {
        RestoreRenderCamera(); // falls seit dem letzten Mal kein Bild gezeichnet wurde
        if (player == *reinterpret_cast<int*>(A.LocalPlayer))
            ApplyVehicleHeight(player, CameraOf(player));
        if (s_cameraFilter && player == *reinterpret_cast<int*>(A.LocalPlayer))
        {
            CameraData& cam = CameraOf(player);
            float dx = 0, dy = 0;
            CameraView v;
            if (MakeView(player, v))
            {
                s_cameraFilter(&v, dx, dy);
                cam.x = std::clamp(cam.x + (int)std::lround(dx), 0, 0x3FFF);
                cam.y = std::clamp(cam.y + (int)std::lround(dy), 0, 0x3FFF);
            }
            else
                s_cameraFilter(nullptr, dx, dy);
        }
        // Kippkamera: Kamera bis nach dem Zeichnen ändern. Das Spiel rechnet Boden und Figuren schon zwischen
        // Kameraführung und RenderFrame auf den Bildschirm um, deshalb geht das nur hier.
        if (s_cameraOverride && player == *reinterpret_cast<int*>(A.LocalPlayer))
        {
            s_inViewRect = true;
            s_cameraOverride();
            s_inViewRect = false;
        }
    }

    static void __cdecl ViewRectHook(int player)
    {
        BeforeViewRect(player);
        CallViewRect(player);
    }

    // Classics: ersetzt "Kamera holen" direkt vor der Rechnung des Sichtbereichs
    static CameraData* __cdecl CameraGetHook(int player)
    {
        BeforeViewRect(player);
        return &CameraOf(player);
    }

    // Kamera nur für ein Bild (Kippkamera): vor dem Zeichnen verändert, danach wiederhergestellt
    struct SavedCamera
    {
        bool active = false;
        int player = 0;
        int32_t x = 0, y = 0, focal = 0, zoom = 0;
    };
    static SavedCamera s_savedCam;

    void RestoreRenderCamera()
    {
        if (!s_savedCam.active)
            return;
        s_savedCam.active = false;
        CameraData& cam = CameraOf(s_savedCam.player);
        cam.x = s_savedCam.x;
        cam.y = s_savedCam.y;
        cam.focal = s_savedCam.focal;
        cam.zoom = s_savedCam.zoom;
        // Der größere Sichtbereich des gezeichneten Bildes bleibt bis zur nächsten Kameraführung stehen: so entsteht
        // der Verkehr auch außerhalb der gekippten Ansicht
    }

    static bool s_noTreeCollision = false;
    static unsigned s_treeLevel = ~0u;
    static void RemoveTreeCollision();

    static void __cdecl RenderHook()
    {
        // Erstes Bild eines Levels: die Objekte der Karte stehen jetzt alle
        if (s_noTreeCollision && s_treeLevel != s_levelLoads)
        {
            s_treeLevel = s_levelLoads;
            RemoveTreeCollision();
        }
        if (s_preRender)
            s_preRender();
        reinterpret_cast<void(__cdecl*)()>(A.RenderFrame)();
        RestoreRenderCamera();
        EnlargeViewRectForLogic();
    }

    // Schüsse der Spielfigur für die Controller-Vibration: das Spiel feuert, wenn die Figur eine Waffe trägt,
    // schießen darf und die Wartezeit der Waffe abgelaufen ist (auch im Amoklauf, wo die Munition nicht abnimmt)
    static unsigned s_shots = 0;
    static int s_shotWeapon = 0;

    static void NoteShot(const uint8_t* ped)
    {
        const int32_t weapon = *reinterpret_cast<const int32_t*>(ped + L.PedWeapon);
        if (weapon >= 1 && weapon <= 4 && ped[L.PedCanFire] &&
            *reinterpret_cast<const int16_t*>(ped + L.PedFireWait) <= 0)
        {
            ++s_shots;
            s_shotWeapon = weapon;
        }
    }

    // Beim Seitwärtslaufen zeigt der Spieler während der Simulation in Laufrichtung;
    // nur für den Schuss wird er kurz zum Fadenkreuz gedreht
    static void __cdecl PedFireHook(uint8_t* ped)
    {
        const auto fire = reinterpret_cast<void(__cdecl*)(uint8_t*)>(A.PedFire);
        const int player = PlayerPed();
        if (player < 0 || ped != reinterpret_cast<uint8_t*>(A.PedStruct + player * L.PedStride))
        {
            fire(ped);
            return;
        }
        NoteShot(ped);
        if (s_fireAngle < 0)
        {
            fire(ped);
            return;
        }
        int16_t* angle = reinterpret_cast<int16_t*>(A.PedAngle + player * L.PedStride);
        const int16_t walk = *angle;
        *angle = (int16_t)(s_fireAngle & 1023);
        fire(ped);
        *angle = walk;
    }

    static void DrawGameHud() { reinterpret_cast<void(__cdecl*)()>(A.HudDraw)(); }

    static void __cdecl HudHook()
    {
        s_lastHud = GetTickCount();
        ++s_hudCount;
        if (s_beforeHud)
            s_beforeHud(); // Weltbild ist fertig, das HUD kommt erst noch
        if (s_hudDrawer)
            s_hudDrawer(&DrawGameHud);
        else
            DrawGameHud();
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
            // Missionsskript: wird beim Levelstart vor der Karte gelesen
            const char* file = std::max(strrchr(name, '\\'), strrchr(name, '/'));
            file = file ? file + 1 : name;
            if (len > 4 && _stricmp(name + len - 4, ".ini") == 0 && _strnicmp(file, "miss", 4) == 0)
            {
                char full[MAX_PATH];
                if (!GetFullPathNameA(name, MAX_PATH, full, nullptr))
                    strcpy_s(full, name);
                EnterCriticalSection(&s_mapLock);
                s_missionPath = full;
                LeaveCriticalSection(&s_mapLock);
            }
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
        for (Addresses (*make)() : { &AddressesR2P, &AddressesLondon69, &AddressesLondon61, &AddressesClassics })
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

    CallCell DirectDrawCreateCall() { return { A.DDCreateCall, A.DDCreateCell }; }

    bool Supported() { return s_supported; }
    void SetHudCallback(void (*cb)()) { s_hudCallback = cb; }
    unsigned HudCount() { return s_hudCount; }
    void* PrimarySurface() { return s_supported ? *reinterpret_cast<void**>(A.MglPrimary) : nullptr; }
    int PageCount() { return s_supported ? *reinterpret_cast<int*>(A.MglPageCount) : 0; }

    // MGL hält seine DirectDraw-Objekte in allen Versionen gleich angeordnet: IDirectDraw*, Seitenzahl, Primary,
    // danach ab +0x0C die Surfaces der Seiten (Seite 0 = Primary) und ab +0x2C deren Bildzeiger
    void* DirectDrawObject() { return s_supported ? *reinterpret_cast<void**>(A.MglPrimary - 8) : nullptr; }

    bool IsPageSurface(void* surface)
    {
        if (!s_supported || !surface)
            return false;
        if (*reinterpret_cast<void**>(A.MglPrimary) == surface)
            return true;
        // Beim Anlegen trägt MGL jede weitere Seite ein und zählt hoch, bevor es sie zum ersten Mal sperrt
        const int count = std::min(*reinterpret_cast<int*>(A.MglPageCount), 8);
        void* const* pages = reinterpret_cast<void* const*>(A.MglPrimary + 0x0C);
        for (int i = 1; i < count; ++i)
            if (pages[i] == surface)
                return true;
        return false;
    }
    bool InGame() { return s_supported && s_hudCount && GetTickCount() - s_lastHud < 500; }

    // Position dessen, was der Spieler steuert (Auto, Zug, Fußgänger ...) bzw. nullptr
    static const GtaPos* ControlledPos(int player, int& type)
    {
        type = -1;
        if (player < 0 || player > 3)
            return nullptr;

        // Plausibilitätsprüfung, damit die Spielfunktion nie außerhalb ihrer Tabellen liest
        const int t = *reinterpret_cast<int*>(A.PlayerCtrl + player * L.PlayerStride);
        const int object = *reinterpret_cast<int*>(A.PlayerObject + player * L.PlayerStride);
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
        k.horn = *reinterpret_cast<uint8_t*>(A.InHorn) != 0;
        k.weaponNext = *reinterpret_cast<uint8_t*>(A.InWeaponNext) != 0;
        k.weaponPrev = *reinterpret_cast<uint8_t*>(A.InWeaponPrev) != 0;
        return k;
    }

    void SetInputFilter(uint32_t (*filter)(uint32_t)) { s_inputFilter = filter; }

    // Index des gesteuerten Fußgängers oder -1
    static int PlayerPed()
    {
        if (!A.PedStruct)
            return -1;
        const int idx = *reinterpret_cast<int*>(A.LocalPlayer);
        if (idx < 0 || idx > 3 || *reinterpret_cast<int*>(A.PlayerCtrl + idx * L.PlayerStride) != 2)
            return -1;
        const int ped = *reinterpret_cast<int*>(A.PlayerObject + idx * L.PlayerStride);
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
        const int16_t id = *reinterpret_cast<int16_t*>(A.PlayerPedId + idx * L.PlayerStride);
        if (id < 0 || id >= 4000)
            return nullptr;
        return reinterpret_cast<uint8_t*>(A.PedStruct + id * L.PedStride);
    }

    int PlayerHealth()
    {
        const uint8_t* ped = PlayerPedStruct();
        if (!ped)
            return -1;
        // Ein tödlicher Schuss lässt die Lebensenergie stehen und setzt nur den Zustand
        if (*reinterpret_cast<const int32_t*>(ped + L.PedPose) == 5)
            return 0;
        return std::clamp<int>(static_cast<int8_t>(ped[L.PedHealth]), 0, 100);
    }

    static int16_t& PedFlames(uint8_t* ped) { return *reinterpret_cast<int16_t*>(ped + L.PedFlames); }

    bool IsPlayerBurning()
    {
        uint8_t* ped = PlayerPedStruct();
        return ped && PedFlames(ped) > 0 && static_cast<int8_t>(ped[L.PedHealth]) > 0;
    }

    // Steht das Objekt in der Liste, die 0x41D200 beim Entfernen durchsucht? (sonst bricht das Spiel ab)
    static bool InActiveObjectList(int16_t handle)
    {
        const uint8_t* o = *reinterpret_cast<uint8_t* const*>(A.ActiveObjects);
        for (int guard = 0; o && guard < addr::ObjectCount; ++guard)
        {
            if (*reinterpret_cast<const int16_t*>(o) == handle)
                return true;
            o = *reinterpret_cast<uint8_t* const*>(o + L.ObjNext);
        }
        return false;
    }

    // Bäume und Laternen halten Fahrzeuge nicht mehr auf (wie in GTA2). Das Spiel prüft Fahrzeug gegen Objekt mit
    // den Positionen beider samt Höhe: liegt das Objekt gut einen halben Block über dem Fahrzeug (gemessen: ab 0,49
    // Block frei, bei 0,44 noch nicht), fährt es darunter durch. Sichtbare Bäume und Laternen werden deshalb beim
    // Levelstart angehoben - die Krone wirkt dadurch etwas größer und verschiebt sich wie ein Dach mit der Kamera.
    // Der Status der Objektart ("ohne Belang") und ihre Maße ändern für Fahrzeuge nichts.
    // Stämme und Masten, die aus Kacheln gebaut sind (Palmen, Laternen, Ampeln; in Liberty City steht zusätzlich
    // unter jedem Baum einer), sichert das Spiel mit einem unsichtbaren Pfosten. Diese Objekte werden entfernt.
    // Die Objekte entstehen bei jedem Levelstart neu aus der Karte, es bleibt also nichts zurück.
    constexpr int kInvisiblePole = 68;   // 8 x 8 Pixel, 64 hoch, unsichtbar (Status 3)
    constexpr int kRaisePixels = 48;     // drei Viertel Block: Luft auch für höhere Fahrzeuge

    static bool IsRaisedObject(int type)
    {
        switch (type)
        {
        case 7: case 9:   // Laternen
        case 16:          // Holzpfosten
        case 26:          // Baum
            return true;
        case 5: case 69:  // London: Kugellampe und Leuchte am Zebrastreifen (sonst Büsche, die bleiben stehen)
            return A.London;
        default:
            return false;
        }
    }

    static void RemoveTreeCollision()
    {
        if (!A.ObjInfo || !A.Objects || !A.ObjectDelete)
            return;
        const uint8_t* const* infos = *reinterpret_cast<const uint8_t* const* const*>(A.ObjInfo);
        const int count = *reinterpret_cast<const int*>(A.ObjInfo - 0x0C);
        if (!infos || count <= 69 || count > 256)
            return;
        // Nur entfernen, wenn die Art wirklich der unsichtbare Pfosten ist (andere Style-Datei, Umbauten)
        const uint8_t* pole = infos[kInvisiblePole];
        int32_t dims[2] = {};
        if (pole)
            memcpy(dims, pole, sizeof(dims));
        const bool removePoles = pole && pole[0x12] == 3 && dims[0] == (8 << 16) && dims[1] == (8 << 16);

        std::vector<int16_t> poles;
        int raised = 0;
        for (int i = 0; i < addr::ObjectCount; ++i)
        {
            uint8_t* o = reinterpret_cast<uint8_t*>(A.Objects + i * L.ObjectSize);
            if (*reinterpret_cast<const int16_t*>(o) != i || *reinterpret_cast<const int16_t*>(o + L.ObjUsed) == 0)
                continue;
            const int type = *reinterpret_cast<const int16_t*>(o + L.ObjType);
            if (type == kInvisiblePole && removePoles)
                poles.push_back((int16_t)i);
            else if (IsRaisedObject(type))
            {
                int32_t& z = *reinterpret_cast<int32_t*>(o + L.ObjPos + 8);   // 1 Pixel = 65536, kleiner = höher
                if (z > (kRaisePixels << 16))
                {
                    z -= kRaisePixels << 16;
                    ++raised;
                }
            }
        }
        // Feste Objekte der Karte stehen in keiner der Listen, nur im Raster der Blöcke; das Entfernen des Spiels
        // nimmt sie dort heraus und gibt den Platz frei (Listen fasst es nur für Status 5, 6, 7 und 9 an)
        for (const int16_t handle : poles)
            reinterpret_cast<void(__cdecl*)(int)>(A.ObjectDelete)(handle);
        LOG("Bäume und Laternen ohne Zusammenstoß: %d angehoben, %d unsichtbare Pfosten entfernt", raised, (int)poles.size());
    }

    bool SetTreeCollision(bool remove)
    {
        const bool can = s_supported && A.ObjInfo && A.Objects && A.ObjectDelete && A.RenderCall;
        s_noTreeCollision = remove && can;
        return can;
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
        const uint8_t* obj = reinterpret_cast<const uint8_t*>(A.Objects + flames * L.ObjectSize);
        const auto word = [obj](int offset) { return *reinterpret_cast<const int16_t*>(obj + offset); };
        if (word(0x00) == flames && word(L.ObjType) == addr::ObjFlames && word(L.ObjUsed) != 0 &&
            word(L.ObjOwner) == *reinterpret_cast<int16_t*>(ped + L.PedId) && word(L.ObjOwnerKind) == 2 && InActiveObjectList(flames))
            reinterpret_cast<void(__cdecl*)(int)>(A.ObjectDelete)(flames);
        PedFlames(ped) = -1;
        return true;
    }

    // Brennschaden: statt "cmp word ptr [esi+Flammen],-1" ruft das Spiel ein Sprungstück auf. Für die Figur in
    // s_noBurnPed meldet der Vergleich "brennt nicht" (ZF = 1), das folgende je überspringt den Abzug.
    static const uint8_t* volatile s_noBurnPed = nullptr;

    // ---- Schaden statt sofortigem Tod ----

    namespace
    {
        int s_pistolDamage = 100, s_machineGunDamage = 100, s_punchDamage = 0;
        uint8_t s_bulletSkip = 0;   // vom Kugel-Hook gesetzt: Treffer hat nur Lebensenergie gekostet

        void HurtPed(uint8_t* ped, int percent)
        {
            const int health = static_cast<int8_t>(ped[L.PedHealth]);
            if (health > 0 && percent > 0)
                ped[L.PedHealth] = static_cast<uint8_t>(std::max(0, health - percent)); // 0 behandelt das Spiel als Tod
        }

        void __cdecl OnPunchHit(uint8_t* victim)
        {
            HurtPed(victim, s_punchDamage);
        }

        void __cdecl OnNpcHit(const uint8_t* attacker)
        {
            const int target = *reinterpret_cast<const int16_t*>(attacker + L.PedTarget);
            if (target >= 0 && target < addr::PedCount)
                HurtPed(reinterpret_cast<uint8_t*>(A.PedStruct + target * L.PedStride), s_punchDamage);
        }

        // Nur die Spielfigur hält mehrere Kugeln aus; andere Figuren sterben wie im Original am ersten Treffer,
        // sonst würden Missionen und Amokläufe deutlich schwerer
        void __cdecl OnBulletHit(uint8_t* victim, const uint8_t* bullet)
        {
            s_bulletSkip = 0;
            if (victim != PlayerPedStruct())
                return;
            const int owner = *reinterpret_cast<const int16_t*>(bullet + L.BulletOwner);
            int weapon = 1;
            if (owner >= 0 && owner < addr::PedCount)
                weapon = *reinterpret_cast<const int32_t*>(A.PedStruct + owner * L.PedStride + L.PedWeapon);
            const int damage = weapon == 2 ? s_machineGunDamage : s_pistolDamage;
            const int health = static_cast<int8_t>(victim[L.PedHealth]);
            if (damage >= 100 || health - damage <= 0)
                return; // dieser Treffer tötet: das Spiel macht weiter wie immer
            victim[L.PedHealth] = static_cast<uint8_t>(health - damage);
            s_bulletSkip = 1;
        }

        // Kleine Sprungstücke zur Laufzeit, damit dieselben Hooks zu den verschieden übersetzten Spielversionen passen
        struct Emitter
        {
            uint8_t* p;
            void Byte(uint8_t b) { *p++ = b; }
            void Bytes(const void* src, size_t n) { memcpy(p, src, n); p += n; }
            void Dword(uint32_t v) { Bytes(&v, 4); }
            void Rel32(uintptr_t target) { Dword(static_cast<uint32_t>(target - (reinterpret_cast<uintptr_t>(p) + 4))); }
        };

        uint8_t* AllocStub(size_t size)
        {
            static uint8_t* s_page = nullptr;
            static size_t s_used = 0;
            if (!s_page)
                s_page = static_cast<uint8_t*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (!s_page || s_used + size > 4096)
                return nullptr;
            uint8_t* p = s_page + s_used;
            s_used += size;
            return p;
        }

        bool PatchJump(uintptr_t site, uint8_t opcode, const uint8_t* stub, size_t len)
        {
            uint8_t patch[16];
            memset(patch, 0x90, sizeof(patch));
            patch[0] = opcode;
            const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(stub) - (site + 5));
            memcpy(patch + 1, &rel, 4);
            return hooks::WriteMemory(reinterpret_cast<void*>(site), patch, len);
        }

        // Befehl an site (len Byte, ohne relative Operanden) durch "call Stück" ersetzen: dort läuft erst der Befehl,
        // dann handler(Register reg); alle Register und Flags bleiben erhalten
        bool HookAfterInstruction(uintptr_t site, size_t len, uint8_t reg, void* handler)
        {
            uint8_t* stub = AllocStub(len + 16);
            if (!stub || len < 5 || len > 16)
                return false;
            Emitter e{ stub };
            e.Bytes(reinterpret_cast<const void*>(site), len);
            e.Byte(0x9C);                       // pushfd
            e.Byte(0x60);                       // pushad
            e.Byte(0x50 + reg);                 // push reg
            e.Byte(0xE8); e.Rel32(reinterpret_cast<uintptr_t>(handler));
            e.Byte(0x83); e.Byte(0xC4); e.Byte(0x04);
            e.Byte(0x61);                       // popad
            e.Byte(0x9D);                       // popfd
            e.Byte(0xC3);                       // ret
            return PatchJump(site, 0xE8, stub, len);
        }

        bool HookBulletHit()
        {
            const uint8_t anim = static_cast<uint8_t>(L.PedAnim);
            const uint8_t kCmp[] = { 0x66, 0x81, 0x7F, anim, 0x89, 0x00, 0x0F, 0x8D };        // cmp word [edi+Anim],89h / jge
            const uint8_t kMov[] = { 0xB9, 0x89, 0x00, 0x00, 0x00, 0x66, 0x39, 0x4F, anim };  // mov ecx,89h / cmp [edi+Anim],cx
            const uint8_t* site = reinterpret_cast<const uint8_t*>(A.BulletHit);
            const size_t len = A.BulletHitMovEcx ? 5 : 6;
            if (memcmp(site, A.BulletHitMovEcx ? kMov : kCmp, A.BulletHitMovEcx ? sizeof(kMov) : sizeof(kCmp)) != 0)
                return false;
            uint8_t* stub = AllocStub(48);
            if (!stub)
                return false;
            Emitter e{ stub };
            e.Byte(0x9C); e.Byte(0x60);
            e.Byte(0x56); e.Byte(0x57);         // push esi (Geschoss), push edi (Figur)
            e.Byte(0xE8); e.Rel32(reinterpret_cast<uintptr_t>(&OnBulletHit));
            e.Byte(0x83); e.Byte(0xC4); e.Byte(0x08);
            e.Byte(0x61); e.Byte(0x9D);
            if (A.BulletHitMovEcx)
                e.Bytes(site, 5);               // mov ecx,89h gilt für beide Wege
            e.Byte(0x80); e.Byte(0x3D); e.Dword(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&s_bulletSkip))); e.Byte(0x00);
            e.Byte(0x0F); e.Byte(0x85); e.Rel32(A.BulletHitSkip);   // jne: Treffer überstanden
            if (!A.BulletHitMovEcx)
                e.Bytes(site, 6);               // cmp word [edi+17h],89h setzt die Flags für das jge danach
            e.Byte(0xE9); e.Rel32(A.BulletHit + len);
            return PatchJump(A.BulletHit, 0xE9, stub, len);
        }
    }

    bool EnableDamage()
    {
        static bool s_done = false, s_ok = false;
        if (s_done)
            return s_ok;
        s_done = true;
        if (!s_supported || !A.BulletHit || !A.BulletHitSkip || !A.PunchHit[0] || !A.PunchHit[1] || !A.NpcHit || !A.PlayerPedId)
            return false;
        // Erst alles prüfen, dann einhängen: entweder alle Stellen oder keine
        for (int i = 0; i < 2; ++i)
        {
            const uint8_t expect[] = { 0x66, 0xC7, static_cast<uint8_t>(0x40 + A.PunchReg[i]), static_cast<uint8_t>(L.PedAnim), 0xAF, 0x00 }; // mov word [reg+Anim],0AFh
            if (memcmp(reinterpret_cast<const void*>(A.PunchHit[i]), expect, sizeof(expect)) != 0)
                return false;
        }
        // mov word [..+Ped+Anim],2Bh: mit Indexregister 10 Byte (66 C7 84 ..), ohne 9 (66 C7 8x ..)
        const uint8_t* npc = reinterpret_cast<const uint8_t*>(A.NpcHit);
        const int n = A.NpcHitLen;
        const bool form = n == 10 ? npc[2] == 0x84 : n == 9 && (npc[2] & 0xF8) == 0x80 && npc[2] != 0x84;
        if (npc[0] != 0x66 || npc[1] != 0xC7 || !form || npc[n - 2] != 0x2B || npc[n - 1] != 0x00)
            return false;
        s_ok = HookBulletHit() &&
               HookAfterInstruction(A.PunchHit[0], 6, A.PunchReg[0], reinterpret_cast<void*>(&OnPunchHit)) &&
               HookAfterInstruction(A.PunchHit[1], 6, A.PunchReg[1], reinterpret_cast<void*>(&OnPunchHit)) &&
               HookAfterInstruction(A.NpcHit, n, A.NpcHitReg, reinterpret_cast<void*>(&OnNpcHit));
        return s_ok;
    }

    void SetDamage(int pistol, int machineGun, int punch)
    {
        s_pistolDamage = std::clamp(pistol, 1, 100);
        s_machineGunDamage = std::clamp(machineGun, 1, 100);
        s_punchDamage = std::clamp(punch, 0, 100);
    }

    // ---- Controller-Vibration: Explosionen und Zustand der Spielfigur ----

    namespace
    {
        using Explode_t = int32_t(__cdecl*)(int32_t, int32_t, int32_t, int32_t);
        Explode_t s_explode = nullptr;
        void (*s_explosionCb)(float, float) = nullptr;

        int32_t __cdecl ExplodeHook(int32_t x, int32_t y, int32_t z, int32_t owner)
        {
            if (s_explosionCb)
                s_explosionCb(x / 4194304.0f, y / 4194304.0f);
            return s_explode(x, y, z, owner);
        }
    }

    bool SetExplosionCallback(void (*cb)(float x, float y))
    {
        s_explosionCb = cb;
        static bool s_tried = false;
        if (s_tried || !s_supported || !A.Explode)
            return s_explode != nullptr;
        s_tried = true;
        if (A.ExplodeThunk)
        {
            // London: alle Aufrufe laufen über einen Sprung-Thunk
            s_explode = reinterpret_cast<Explode_t>(A.Explode);
            if (!Redirect(A.ExplodeThunk, 0xE9, A.Explode, reinterpret_cast<void*>(&ExplodeHook)))
                s_explode = nullptr;
            return s_explode != nullptr;
        }
        // 1.0.0.1 und Classics: über 20 Aufrufer, deshalb den Anfang der Funktion in ein Sprungstück auslagern
        uint8_t* stub = hooks::BytesMatch(A.Explode, A.ExplodeProlog, A.ExplodeLen) ? AllocStub(16) : nullptr;
        if (!stub)
            return false;
        Emitter e{ stub };
        e.Bytes(A.ExplodeProlog, A.ExplodeLen);
        e.Byte(0xE9); e.Rel32(A.Explode + A.ExplodeLen);
        s_explode = reinterpret_cast<Explode_t>(stub);
        if (!PatchJump(A.Explode, 0xE9, reinterpret_cast<const uint8_t*>(&ExplodeHook), A.ExplodeLen))
            s_explode = nullptr;
        return s_explode != nullptr;
    }

    bool GetFeedback(Feedback& out)
    {
        out = Feedback();
        Player p;
        if (!PlayerPedStruct() || !GetPlayer(p))
            return false;
        out.x = p.x;
        out.y = p.y;
        out.onFoot = p.ctrlType == 2;
        out.health = IsPlayerDead() ? 0 : PlayerHealth();
        out.shots = s_shots;
        out.weapon = s_shotWeapon;
        if (p.ctrlType == 0 && A.CarDamage)
        {
            const int idx = *reinterpret_cast<int*>(A.LocalPlayer);
            const int car = *reinterpret_cast<int*>(A.PlayerObject + idx * L.PlayerStride) & 0xFFFF;
            if (car < 400)
            {
                out.car = car;
                out.carDamage = *reinterpret_cast<uint8_t*>(A.CarDamage + car * A.CarStride);
            }
        }
        return true;
    }

    // Fortschritt: dieselben Zahlen, die das Spiel in seiner eigenen Pausenanzeige der Reihe nach einblendet
    bool GetProgress(Progress& out)
    {
        out = Progress();
        if (!InGame() || !A.TargetScore || !A.ScriptObjects || !A.PlayerScore)
            return false;
        const int player = *reinterpret_cast<int*>(A.LocalPlayer);
        if (player < 0 || player > 3)
            return false;
        // -1 = das Level führt diese Zahl nicht
        const auto total = [](uintptr_t at) { return std::max(0, *reinterpret_cast<int32_t*>(at)); };
        // Erreicht = Startwert minus Stand des Zählers
        const auto done = [](uintptr_t at) {
            const int32_t slot = *reinterpret_cast<int32_t*>(at);
            if (slot < 0 || slot >= 2000)
                return 0;
            int32_t value[2];
            memcpy(value, reinterpret_cast<const uint8_t*>(A.ScriptObjects + slot * L.ScriptObjSize + 4), sizeof(value));
            return std::max(0, value[1] - value[0]);
        };
        const uintptr_t po = player * L.PlayerStride;
        out.score = *reinterpret_cast<int32_t*>(A.PlayerScore + po);
        out.target = total(A.TargetScore);
        out.missionsTotal = total(A.MissionsTotal);
        out.missionsDone = done(A.MissionsCounter);
        out.secretsTotal = total(A.SecretsTotal);
        out.secretsFound = done(A.SecretsCounter);
        out.lives = *reinterpret_cast<uint8_t*>(A.PlayerLives + po);
        out.multiplier = *reinterpret_cast<int16_t*>(A.PlayerMultiplier + po);
        return true;
    }

    bool EnableBurnPause()
    {
        // cmp word ptr [esi+Flammen],-1 / je ... (Sprungweite je Version verschieden)
        const uint8_t expect[] = { 0x66, 0x83, 0x7E, static_cast<uint8_t>(L.PedFlames), 0xFF, 0x74 };
        if (!s_supported || !A.BurnDamage || !hooks::BytesMatch(A.BurnDamage, expect, sizeof(expect)))
            return false;
        uint8_t* stub = AllocStub(16);
        if (!stub)
            return false;
        Emitter e{ stub };
        e.Bytes(expect, 5);                             // cmp word ptr [esi+Flammen],0FFFFh
        e.Byte(0x74); e.Byte(0x06);                     // je fertig
        e.Byte(0x3B); e.Byte(0x35);                     // cmp esi,[s_noBurnPed]
        e.Dword(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&s_noBurnPed)));
        e.Byte(0xC3);                                   // fertig: ret
        return PatchJump(A.BurnDamage, 0xE8, stub, 5);
    }

    void SetPlayerBurnPaused(bool paused)
    {
        const int idx = paused ? PlayerPed() : -1;
        s_noBurnPed = idx >= 0 ? reinterpret_cast<const uint8_t*>(A.PedStruct + idx * L.PedStride) : nullptr;
    }

    bool IsPlayerDead()
    {
        const uint8_t* ped = PlayerPedStruct();
        const int32_t pose = ped ? *reinterpret_cast<const int32_t*>(ped + L.PedPose) : 0;
        return !ped || static_cast<int8_t>(ped[L.PedHealth]) <= 0 || pose == 5 || pose == 0x15 || pose == 0x17;
    }

    bool GetPlayerPedAngle(int& angle)
    {
        const int ped = s_supported ? PlayerPed() : -1;
        if (ped < 0)
            return false;
        angle = *reinterpret_cast<int16_t*>(A.PedAngle + ped * L.PedStride) & 1023;
        return true;
    }

    void ShowPlayerPose(int anim, int angle)
    {
        const int idx = s_supported && A.PedSprite ? PlayerPed() : -1;
        if (idx < 0)
            return;
        uint8_t* ped = reinterpret_cast<uint8_t*>(A.PedStruct + idx * L.PedStride);
        int16_t& current = *reinterpret_cast<int16_t*>(ped + L.PedAnim);
        const int16_t keep = current;
        if (anim >= 0)
            current = static_cast<int16_t>(anim);
        if (angle >= 0)
            *reinterpret_cast<int16_t*>(ped + L.PedAngleOfs) = static_cast<int16_t>(angle & 1023);
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
        uint8_t* ped = reinterpret_cast<uint8_t*>(A.PedStruct + idx * L.PedStride);
        if (PedFlames(ped) > 0)
            return false;
        // wie 0x41CB30: Flammen etwas unterhalb der Figur erzeugen und an ihr festmachen
        const int32_t x = *reinterpret_cast<int32_t*>(ped + L.PedPos);
        const int32_t y = *reinterpret_cast<int32_t*>(ped + L.PedPos + 4);
        const int32_t z = *reinterpret_cast<int32_t*>(ped + L.PedPos + 8);
        using Create = int(__cdecl*)(int32_t, int32_t, int32_t, int, int, int);
        const int16_t h = static_cast<int16_t>(reinterpret_cast<Create>(A.ObjectCreate)(
            x, y, z - 0x10000, addr::ObjFlames, *reinterpret_cast<int16_t*>(ped + L.PedId), 2));
        if (h <= 0)
            return false;
        PedFlames(ped) = h;
        return true;
    }

    void SetPlayerPedAngle(int angle)
    {
        const int ped = s_supported ? PlayerPed() : -1;
        if (ped >= 0)
            *reinterpret_cast<int16_t*>(A.PedAngle + ped * L.PedStride) = (int16_t)(angle & 1023);
    }

    void SetPreRenderCallback(void (*cb)()) { s_preRender = cb; }
    void SetPlayerFireAngle(int angle) { s_fireAngle = angle; }

    // Ende der Kameraführung (call ViewRect) einmal umbiegen; Kamerasteuerung, Kippkamera und Sichtoptionen teilen es
    static bool HookViewRect()
    {
        static bool s_hooked = false;
        if (!s_hooked)
            s_hooked = A.ViewRectCall ? Redirect(A.ViewRectCall, 0xE8, A.ViewRect, reinterpret_cast<void*>(&ViewRectHook))
                                      : Redirect(A.CameraGetCall, 0xE8, A.CameraGet, reinterpret_cast<void*>(&CameraGetHook));
        return s_hooked;
    }

    bool SetCameraFilter(void (*filter)(const CameraView* view, float& dx, float& dy))
    {
        if (!s_supported || !A.Camera)
            return false;
        if (!HookViewRect())
            return false;
        s_cameraFilter = filter;
        return true;
    }

    bool GetCameraView(CameraView& out)
    {
        if (!InGame() || !A.Camera)
            return false;
        const int idx = *reinterpret_cast<int*>(A.LocalPlayer);
        return idx >= 0 && idx <= 3 && MakeView(idx, out);
    }

    bool OverrideRenderCamera(float centerX, float centerY, float shrink, float& used)
    {
        RestoreRenderCamera();
        used = 1;
        if (!s_supported || !A.Camera || !HaveViewRect())
            return false;
        const int idx = *reinterpret_cast<int*>(A.LocalPlayer);
        if (idx < 0 || idx > 3)
            return false;
        CameraData& cam = CameraOf(idx);
        if (cam.focal <= 0 || cam.zoom <= 0)
            return false;
        s_savedCam.active = true;
        s_savedCam.player = idx;
        s_savedCam.x = cam.x;
        s_savedCam.y = cam.y;
        s_savedCam.focal = cam.focal;
        s_savedCam.zoom = cam.zoom;
        // focal bestimmt den Maßstab beim Zeichnen, zoom den Sichtbereich (ViewRect: (h + 384) * 160 / zoom)
        shrink = std::clamp(shrink, 1.0f, 4.0f);
        cam.focal = std::max(1, (int)std::lround(cam.focal / shrink));
        cam.zoom = std::max(1, (int)std::lround(cam.zoom / shrink));
        used = (float)s_savedCam.focal / cam.focal;
        cam.x = std::clamp((int)std::lround(centerX), 0, 0x3FFF);
        cam.y = std::clamp((int)std::lround(centerY), 0, 0x3FFF);
        if (!s_inViewRect)
            CallViewRect(idx);
        return true;
    }

    bool SetViewOptions(float viewRectScale, int vehicleHeight, int vehicleZoom)
    {
        if (!s_supported || !A.Camera || !HaveViewRect() || !A.PlayerCtrl)
            return false;
        if (!HookViewRect())
            return false;
        s_viewRectScale = std::clamp(viewRectScale, 1.0f, 3.0f);
        s_vehicleHeight = std::clamp(vehicleHeight, 0, 716);
        s_vehicleZoom = std::clamp(vehicleZoom, 0, 500);
        return true;
    }

    bool SetCameraOverride(void (*cb)())
    {
        if (!s_supported || !A.Camera || !HaveViewRect())
            return false;
        if (!HookViewRect())
            return false;
        s_cameraOverride = cb;
        return true;
    }

    void SetBeforeHudCallback(void (*cb)()) { s_beforeHud = cb; }
    void SetHudDrawer(void (*draw)(void (*drawHud)())) { s_hudDrawer = draw; }
    bool TiltCameraAvailable() { return s_supported && A.DrawBits && A.Camera && HaveViewRect(); }

    bool CanRedirectHud()
    {
        return s_supported && A.DrawBits && A.DrawSetRows && A.DrawSetTarget && A.ViewWidth && A.ViewHeight && A.ViewCenterX &&
               A.ViewCenterY && A.ViewZoom;
    }

    // Was BeginHudLayer verstellt hat
    static struct
    {
        bool active = false;
        uint8_t* bits;
        int width, height, viewWidth, viewHeight, centerX, centerY, zoom;
    } s_hudLayer;

    static void SetDrawTarget(uint8_t* bits, int pitch, int width, int height)
    {
        reinterpret_cast<void(__cdecl*)(uint8_t*, int, int)>(A.DrawSetRows)(bits, pitch, height);
        reinterpret_cast<void(__cdecl*)(int, int, uint8_t*)>(A.DrawSetTarget)(width, height, bits);
        *reinterpret_cast<uint8_t**>(A.DrawBits) = bits;
        *reinterpret_cast<int*>(A.DrawWidth) = width;
        *reinterpret_cast<int*>(A.DrawHeight) = height;
    }

    bool BeginHudLayer(uint8_t* bits, int width, int height, int factor)
    {
        DrawBuffer real;
        if (!CanRedirectHud() || s_hudLayer.active || factor < 2 || !bits || !GetDrawBuffer(real))
            return false;
        int& viewW = *reinterpret_cast<int*>(A.ViewWidth);
        int& viewH = *reinterpret_cast<int*>(A.ViewHeight);
        int& centerX = *reinterpret_cast<int*>(A.ViewCenterX);
        int& centerY = *reinterpret_cast<int*>(A.ViewCenterY);
        int& zoom = *reinterpret_cast<int*>(A.ViewZoom);
        s_hudLayer = { true, real.bits, real.width, real.height, viewW, viewH, centerX, centerY, zoom };

        SetDrawTarget(bits, real.pitch, width, height);
        viewW = (viewW + factor - 1) / factor;
        viewH = (viewH + factor - 1) / factor;
        centerX /= factor;
        centerY /= factor;
        zoom /= factor; // Weltpunkte (z.B. Pfeile über Zielen) rücken im kleinen Bild entsprechend zur Mitte
        return true;
    }

    void EndHudLayer()
    {
        if (!s_hudLayer.active)
            return;
        s_hudLayer.active = false;
        SetDrawTarget(s_hudLayer.bits, *reinterpret_cast<int*>(A.DrawPitch), s_hudLayer.width, s_hudLayer.height);
        *reinterpret_cast<int*>(A.ViewWidth) = s_hudLayer.viewWidth;
        *reinterpret_cast<int*>(A.ViewHeight) = s_hudLayer.viewHeight;
        *reinterpret_cast<int*>(A.ViewCenterX) = s_hudLayer.centerX;
        *reinterpret_cast<int*>(A.ViewCenterY) = s_hudLayer.centerY;
        *reinterpret_cast<int*>(A.ViewZoom) = s_hudLayer.zoom;
    }

    bool GetDrawBuffer(DrawBuffer& out)
    {
        if (!s_supported || !A.DrawBits)
            return false;
        out.bits = *reinterpret_cast<uint8_t**>(A.DrawBits);
        out.pitch = *reinterpret_cast<int*>(A.DrawPitch);
        out.bytesPerPixel = *reinterpret_cast<uint8_t*>(A.DrawBytes);
        out.width = *reinterpret_cast<int*>(A.DrawWidth);
        out.height = *reinterpret_cast<int*>(A.DrawHeight);
        return out.bits && out.width > 0 && out.width <= 8192 && out.height > 0 && out.height <= 8192 &&
               out.bytesPerPixel >= 1 && out.pitch >= out.width * out.bytesPerPixel;
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
        return *reinterpret_cast<uint8_t*>(A.PlayerCharacter + idx * L.PlayerStride);
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
        remap = *reinterpret_cast<uint8_t*>(A.PedRemap + ped * L.PedStride);
        return true;
    }

    void SetPlayerPedRemap(int remap)
    {
        const int ped = s_supported && A.PedRemap && A.PedDrawRemap ? PlayerPed() : -1;
        if (ped < 0 || remap < 0 || remap > 63)
            return;
        *reinterpret_cast<uint8_t*>(A.PedRemap + ped * L.PedStride) = (uint8_t)remap;
        *reinterpret_cast<uint8_t*>(A.PedDrawRemap + ped * L.PedStride) = (uint8_t)remap;
    }

    std::string MapFile()
    {
        EnterCriticalSection(&s_mapLock);
        std::string p = s_mapPath;
        LeaveCriticalSection(&s_mapLock);
        return p;
    }

    unsigned MapGeneration() { return s_mapGen; }

    std::string MissionFile()
    {
        EnterCriticalSection(&s_mapLock);
        std::string p = s_missionPath;
        LeaveCriticalSection(&s_mapLock);
        return p;
    }

    bool ScriptObjectLines(std::vector<int>& lines)
    {
        lines.clear();
        if (!s_supported || !A.ScriptObjSlot)
            return false;
        // Zeilennummer -> Platz in der Objekttabelle, unbenutzte Zeilen sind 0 (der erste Platz fällt dadurch weg)
        const int16_t* map = reinterpret_cast<const int16_t*>(A.ScriptObjSlot);
        for (int line = 0; line < 32768; ++line)
            if (map[line] > 0 && map[line] < 2000)
                lines.push_back(line);
        return !lines.empty();
    }
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
            CmdPBrief = 37, CmdPBriefTimed = 68, CmdFrenzyBrief = 135, CmdKfBriefTimed = 137, CmdKfBriefGeneral = 139,
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
            return reinterpret_cast<uint8_t*>(A.ScriptObjects + slot * L.ScriptObjSize);
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
        constexpr DWORD   kGroupWindow = 5000;       // so lange nach dem Abheben gilt ein DISABLE als die eigene Telefongruppe
        struct PhoneGroup { int line = -1, count = 0; int32_t handles[8] = {}; };
        // Eine Telefonmission vom Abheben, bis das Skript ihre Telefongruppe wieder einschaltet. Liberty City tut das
        // nach jeder Mission, San Andreas erst am Ende einer Kette aus mehreren Missionen: dort meldet jeder Teil
        // MISSION_END, und ein gescheiterter Teil führt direkt zum nächsten.
        struct MissionLog
        {
            bool active = false, failed = false, succeeded = false;
            DWORD startTick = 0;
            int phoneLine = -1;
            int32_t phoneHandle = -1;
            int slots = 0;                   // Anzahl gesicherter Objekte
            std::vector<uint8_t> start;      // Objekttabelle beim Abheben
            std::vector<int> groups;         // Telefongruppen (MPHONES/PHONE_TOGG), die das Skript beim Abheben abschaltet
            std::vector<int> paid;           // Zeilen, die in diesem Versuch schon Punkte ausgezahlt haben
        };
        PhoneGroup s_phones;
        MissionLog s_mission;

        uint8_t* ObjectSlot(int slot) { return reinterpret_cast<uint8_t*>(A.ScriptObjects + slot * L.ScriptObjSize); }
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

        void WatchPhones(const uint8_t* line)
        {
            s_phones.line = Field(line, FObj);
            s_phones.count = std::min<int>(std::max<int>(Field(line, FFail), 0), 8);
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
                s_mission.startTick = GetTickCount();
                s_mission.phoneLine = s_phones.line + k;
                s_mission.phoneHandle = s_phones.handles[k];
                s_mission.slots = ObjectSlotCount();
                s_mission.start.assign(ObjectSlot(0), ObjectSlot(0) + s_mission.slots * L.ScriptObjSize);
                s_phones.line = -1;
                LOG("Telefon aus Zeile %d abgenommen - Missionsstand (%d Objekte) für einen neuen Versuch gemerkt",
                    s_mission.phoneLine, s_mission.slots);
                return;
            }
        }

        // Gibt das Skript nach dem Fehlschlag die Telefongruppe wieder frei (ENABLE auf MPHONES/PHONE_TOGG), steht
        // die gescheiterte Mission wieder zur Wahl, als wäre sie nie angenommen worden. Das geschieht genau dort,
        // bevor das Skript prüft, welche Missionen noch zu haben sind (Liberty City: COMPARE auf 548..551).
        // Die Aufräumbefehle davor sind je Mission verschieden (die dritte in Liberty City setzt erst Power-ups).
        void RestoreMission()
        {
            if (ObjectSlotCount() != s_mission.slots)
            {
                LOG("Gemerkter Missionsstand verworfen (anderes Level)");
                s_mission = MissionLog();
                return;
            }
            // Umgewandelte Objekte wieder in den Ausgangszustand (z.B. FUTURECAR mit Modell statt Auto-Handle)
            int converted = 0;
            for (int i = 0; i < s_mission.slots; ++i)
            {
                if (SlotType(ObjectSlot(i)) == SlotType(&s_mission.start[i * L.ScriptObjSize]))
                    continue;
                memcpy(ObjectSlot(i), &s_mission.start[i * L.ScriptObjSize], L.ScriptObjSize);
                ++converted;
            }
            // Zähler auf den Stand vor der Mission
            int counters = 0;
            for (int i = 0; i < s_mission.slots; ++i)
            {
                uint8_t* entry = ObjectSlot(i);
                const uint8_t* before = &s_mission.start[i * L.ScriptObjSize];
                if (SlotType(entry) == kObjCounter && SlotType(before) == kObjCounter && memcmp(entry + 4, before + 4, 4) != 0)
                {
                    memcpy(entry + 4, before + 4, 4);
                    ++counters;
                }
            }
            int32_t* phone = ObjectValue(s_mission.phoneLine);
            if (phone && *phone == -1)
                *phone = s_mission.phoneHandle;
            // Belohnungen, die der gescheiterte Versuch schon gezahlt hat, gibt es kein zweites Mal
            for (int index : s_mission.paid)
            {
                uint8_t* paid = LineAt(index);
                LOG("Belohnung aus Zeile %d (%d Punkte) wird beim neuen Versuch nicht noch einmal gezahlt", index, (int)Field(paid, FScore));
                Field(paid, FScore) = 0;
            }
            LOG("Neuer Versuch möglich: Telefon aus Zeile %d klingelt wieder, %d Objekte und %d Zähler zurückgesetzt",
                s_mission.phoneLine, converted, counters);
            s_mission = MissionLog();
        }

        // Belohnungen mitten in einer Mission (z.B. STEAL mit 5000 Punkten, Liberty City Mission 3) sollen bei einem
        // neuen Versuch nicht noch einmal fließen. Ob eine Zeile gezahlt hat, zeigt der Punktestand: er ist um mindestens
        // ihren <score> gestiegen, wenn ihr Prozess zur nächsten Zeile weitergeht (der Multiplikator erhöht nur).
        int     s_procLine[addr::ScriptProcs] = {};   // zuletzt ausgeführte Zeile je Prozess (+1, 0 = keine)
        int32_t s_procScore[addr::ScriptProcs] = {};  // Punktestand, als der Prozess dort ankam

        int32_t CurrentScore()
        {
            const int p = *reinterpret_cast<int*>(A.LocalPlayer);
            return A.PlayerScore && p >= 0 && p <= 3 ? *reinterpret_cast<int32_t*>(A.PlayerScore + p * L.PlayerStride) : 0;
        }

        // Befehle, deren <score> eine Textnummer ist
        bool IsTextCommand(uint16_t c)
        {
            switch (c)
            {
            case CmdBrief: case CmdSpeechBrief: case CmdMobileBrief: case CmdMessageBrief: case CmdPBrief:
            case CmdPBriefTimed: case CmdFrenzyBrief: case CmdKfBriefTimed: case CmdKfBriefGeneral:
                return true;
            default:
                return false;
            }
        }

        void TrackRewards(int proc, const uint8_t* line)
        {
            if (!A.ScriptLines || reinterpret_cast<uintptr_t>(line) < A.ScriptLines)
                return;
            const int index = (int)((reinterpret_cast<uintptr_t>(line) - A.ScriptLines) / L.LineSize);
            if (index >= 5000 || index + 1 == s_procLine[proc])
                return;
            const int32_t score = CurrentScore();
            const int prev = s_procLine[proc] - 1;
            if (prev >= 0 && s_mission.active)
            {
                const uint8_t* left = LineAt(prev);
                if (Field(left, FScore) > 0 && !IsTextCommand(LineCommand(left)) && score - s_procScore[proc] >= Field(left, FScore) &&
                    std::find(s_mission.paid.begin(), s_mission.paid.end(), prev) == s_mission.paid.end())
                    s_mission.paid.push_back(prev);
            }
            s_procLine[proc] = index + 1;
            s_procScore[proc] = score;
        }

        // Ende der Wartephase nach einem Fehlschlag; der gemerkte Missionsstand bleibt bis zur Freigabe erhalten
        void FinishFailure(int proc)
        {
            s_failedAt[proc] = 0;
        }

        bool s_quickRetry = false;
        void (*s_scriptEvent)(ScriptEvent event, int value) = nullptr;

        // Objekt einer Telefongruppe (MPHONES/PHONE_TOGG) aus der Zeile <line>
        bool IsPhoneGroup(int line)
        {
            const uint8_t* o = ScriptObject(line);
            const int32_t type = o ? SlotType(o) : -1;
            return type == kObjMPhones || type == kObjPhoneToggle;
        }

        // Die eigene Telefongruppe ist wieder frei: Mission bzw. Kette vorbei. Gab es einen Fehlschlag, klingelt das
        // Telefon mit schnellem neuen Versuch wieder, sonst wird (nach einem geschafften Teil) automatisch gespeichert.
        void EndMission()
        {
            if (s_mission.failed && s_quickRetry)
            {
                RestoreMission();
                return;
            }
            const bool save = s_mission.succeeded;
            LOG("Telefonmission vorbei (%s)", s_mission.failed ? "mit Fehlschlag" : save ? "geschafft" : "ohne Ergebnis");
            s_mission = MissionLog();
            if (save && s_scriptEvent)
                s_scriptEvent(ScriptEvent::MissionEnd, 0);
        }

        void __cdecl OnScriptCommand(uint32_t command, uint8_t* line)
        {
            const int proc = *reinterpret_cast<int16_t*>(A.ScriptProc);
            if (proc < 0 || proc >= addr::ScriptProcs)
                return;
            // Einmal je Level festhalten, dass der Hook läuft (hilft bei Fehlerberichten und neuen Spielversionen)
            static unsigned s_seenLevel = ~0u;
            if (s_seenLevel != s_levelLoads)
            {
                s_seenLevel = s_levelLoads;
                LOG("Missionsskript läuft: erster Befehl %u in Zeile %d (Prozess %d)", command,
                    (int)((reinterpret_cast<uintptr_t>(line) - A.ScriptLines) / L.LineSize), proc);
            }

            if (s_scriptEvent)
            {
                // Text der Telefon-/Pager-Ansagen steht in <score>
                switch (command)
                {
                case CmdBrief: case CmdSpeechBrief: case CmdMobileBrief: case CmdPBrief: case CmdPBriefTimed:
                case CmdFrenzyBrief: case CmdKfBriefTimed: case CmdKfBriefGeneral:
                    s_scriptEvent(ScriptEvent::Brief, Field(line, FScore));
                    break;
                default:
                    break;
                }
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
                // Mitten in einer Telefonmission (Kette) erst speichern, wenn die Gruppe wieder frei ist
                if (s_mission.active)
                    s_mission.succeeded = true;
                else if (s_scriptEvent)
                    s_scriptEvent(ScriptEvent::MissionEnd, 0);
                break;
            case CmdMessageBrief:
                if (Field(line, FScore) == kBriefMissionFailed && s_mission.active)
                    s_mission.failed = true;
                break;
            case CmdDisable:
                // Direkt nach dem Abheben schaltet das Skript seine Telefongruppe ab; ihr ENABLE beendet die Mission
                if (s_mission.active && GetTickCount() - s_mission.startTick < kGroupWindow && IsPhoneGroup(Field(line, FObj)) &&
                    std::find(s_mission.groups.begin(), s_mission.groups.end(), Field(line, FObj)) == s_mission.groups.end())
                {
                    s_mission.groups.push_back(Field(line, FObj));
                    LOG("Telefongruppe aus Zeile %d gehört zur Mission, ihr Wiedereinschalten beendet sie", (int)Field(line, FObj));
                }
                break;
            case CmdEnable:
                if (s_mission.active && IsPhoneGroup(Field(line, FObj)) &&
                    (s_mission.groups.empty() ||
                     std::find(s_mission.groups.begin(), s_mission.groups.end(), Field(line, FObj)) != s_mission.groups.end()))
                    EndMission();
                break;
            default:
                break;
            }
            if (!s_quickRetry)
                return;
            TrackRewards(proc, line);

            DWORD& failedAt = s_failedAt[proc];
            if (command == CmdMessageBrief && Field(line, FScore) == kBriefMissionFailed)
            {
                if (!failedAt)
                {
                    LOG("Mission fehlgeschlagen (Skriptprozess %d) - Wartezeit bis zum neuen Versuch wird verkürzt", proc);
                    ++s_failedCount;
                }
                failedAt = GetTickCount() | 1;
                return;
            }
            if (!failedAt)
                return;
            if (!IsCleanupCommand(static_cast<uint16_t>(command)) || command == CmdDonowt || GetTickCount() - failedAt > kRetryTimeout)
            {
                FinishFailure(proc);
                return;
            }
            // SURVIVE kopiert beim ersten Durchlauf die Dauer <data> nach <fail> und zählt dort jeden Frame herunter
            if (command == CmdSurvive && *reinterpret_cast<int16_t*>(A.ScriptState + proc * 2) != 1 && Field(line, FFail) > kRetryWait)
                Field(line, FFail) = kRetryWait;
        }

    }

    bool EnableKeepMultiplier()
    {
        // 1.0.0.1: mov ax,[edi+6576BC]; shr ax,1; mov [edi+6576BC],ax  ->  "shr ax,1" (3 Byte) entfällt
        // London:  shr word ptr [edx+eax+Multiplikator],1 in einer eigenen Funktion  ->  8 Byte entfallen
        // Classics: dasselbe als shr word ptr [eax*4+Multiplikator],1
        static const uint8_t r2p[] = { 0x66, 0x8B, 0x87, 0xBC, 0x76, 0x65, 0x00, 0x66, 0xD1, 0xE8, 0x66, 0x89, 0x87, 0xBC, 0x76, 0x65, 0x00 };
        static const uint8_t shrMem[] = { 0x66, 0xD1, 0xAC, 0x02 };
        static const uint8_t shrIndex[] = { 0x66, 0xD1, 0x2C, 0x85 };
        static const uint8_t nop3[] = { 0x0F, 0x1F, 0x00 };
        static const uint8_t nop8[] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
        if (!s_supported || !A.MultHalve)
            return false;
        if (hooks::BytesMatch(A.MultHalve - 7, r2p, sizeof(r2p)))
            return hooks::WriteMemory(reinterpret_cast<void*>(A.MultHalve), nop3, sizeof(nop3));
        if (hooks::BytesMatch(A.MultHalve, shrMem, sizeof(shrMem)) || hooks::BytesMatch(A.MultHalve, shrIndex, sizeof(shrIndex)))
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
        // jmp dword ptr [Befehl*4 + ScriptJumps]: FF 24, dann 9D (ebx), B5 (esi) oder BD (edi) und die Adresse
        uint8_t dispatch[7] = { 0xFF, 0x24, static_cast<uint8_t>(0x85 | (A.ScriptCmdReg << 3)) };
        memcpy(dispatch + 3, &A.ScriptJumps, 4);
        // SURVIVE (alle Versionen): beim ersten Durchlauf ScriptState 1 -> 2 und Dauer nach <fail> (Zeile + 0x0A),
        // danach zählt <fail> jeden Frame herunter. Stichprobe: "mov ...,[ecx*2 + ScriptState]" am Anfang
        uint8_t state[4];
        memcpy(state, &A.ScriptState, 4);
        const uint8_t* survive = reinterpret_cast<const uint8_t*>(A.ScriptSurvive);
        bool stateRef = false;
        for (int i = 0; i + 4 <= 0x20 && !stateRef; ++i)
            stateRef = memcmp(survive + i, state, 4) == 0;
        if (!hooks::BytesMatch(A.ScriptDispatch, dispatch, sizeof(dispatch)) || !stateRef)
            return false;

        // Sprungstück: OnScriptCommand(Befehl, Zeile) aufrufen, alle Register und Flags erhalten, dann der Sprung
        // des Spiels
        uint8_t* stub = AllocStub(32);
        if (!stub)
            return false;
        Emitter e{ stub };
        e.Byte(0x9C); e.Byte(0x60);                 // pushfd, pushad
        e.Byte(0x50 + A.ScriptLineReg);             // push Zeile
        e.Byte(0x50 + A.ScriptCmdReg);              // push Befehl
        e.Byte(0xE8); e.Rel32(reinterpret_cast<uintptr_t>(&OnScriptCommand));
        e.Byte(0x83); e.Byte(0xC4); e.Byte(0x08);
        e.Byte(0x61); e.Byte(0x9D);
        e.Bytes(dispatch, sizeof(dispatch));
        s_installed = PatchJump(A.ScriptDispatch, 0xE9, stub, sizeof(dispatch));
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
        constexpr int kScriptLines = 5000, kProcBlockSize = 0x4A0, kTriggerCount = 210;

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
        {
            // Befehl und obj (in den gepackten Zeilen die ersten sechs Byte)
            const uint8_t* line = LineAt(i);
            uint8_t key[6];
            memcpy(key, line, 2);
            memcpy(key + 2, line + L.LineFields, 4);
            h = Fnv(h, key, sizeof(key));
        }
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
        const uintptr_t po = p * L.PlayerStride;
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
            out.angle = *reinterpret_cast<const int16_t*>(A.PedStruct + ped * L.PedStride + L.PedAngleOfs);
            out.hasAngle = true;
        }

        const int slots = ObjectSlotCount();
        const uint8_t* objects = reinterpret_cast<const uint8_t*>(A.ScriptObjects);
        out.objects.assign(objects, objects + slots * L.ScriptObjSize);
        const uint8_t* block = reinterpret_cast<const uint8_t*>(A.ScriptProcBlock);
        out.procBlock.assign(block, block + kProcBlockSize);
        const uint8_t* state = reinterpret_cast<const uint8_t*>(A.ScriptState);
        out.procState.assign(state, state + addr::ScriptProcs * 2);
        const uint8_t* lines = reinterpret_cast<const uint8_t*>(A.ScriptLines);
        out.lines.assign(lines, lines + kScriptLines * L.LineSize);
        if (A.Triggers)
        {
            const uint8_t* triggers = reinterpret_cast<const uint8_t*>(A.Triggers);
            out.triggers.assign(triggers, triggers + kTriggerCount * L.TriggerSize);
        }
        return true;
    }

    const char* RestoreGameState(const GameState& in)
    {
        if (!CanUseSaves())
            return "nicht im Level";
        const int p = *reinterpret_cast<int*>(A.LocalPlayer);
        if (p < 0 || p > 3)
            return "kein Spieler";
        const uintptr_t po = p * L.PlayerStride;
        if (in.exeStamp != A.timestamp || in.levelHash != LevelHash())
            return "anderes Level";
        if (in.objects.size() != (size_t)ObjectSlotCount() * L.ScriptObjSize || in.procBlock.size() != kProcBlockSize ||
            in.procState.size() != addr::ScriptProcs * 2 || in.lines.size() != (size_t)kScriptLines * L.LineSize)
            return "Daten passen nicht";

        // Skript: Objekte (Zähler, Telefone, Missionsobjekte), Prozesse und Zeilen genau wie beim Speichern
        memcpy(reinterpret_cast<void*>(A.ScriptObjects), in.objects.data(), in.objects.size());
        memcpy(reinterpret_cast<void*>(A.ScriptProcBlock), in.procBlock.data(), in.procBlock.size());
        memcpy(reinterpret_cast<void*>(A.ScriptState), in.procState.data(), in.procState.size());
        memcpy(reinterpret_cast<void*>(A.ScriptLines), in.lines.data(), in.lines.size());
        // Ein- und ausgeschaltete Objekte: ohne diese Tabelle bliebe z.B. die beim Missionsstart abgeschaltete
        // Telefongruppe aus, während das geladene Skript schon auf einen Anruf wartet
        if (A.Triggers && in.triggers.size() == (size_t)kTriggerCount * L.TriggerSize)
            memcpy(reinterpret_cast<void*>(A.Triggers), in.triggers.data(), in.triggers.size());
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
            uint8_t* pd = reinterpret_cast<uint8_t*>(A.PedStruct + ped * L.PedStride);
            memcpy(pd + L.PedPos, &in.x, 4);
            memcpy(pd + L.PedPos + 4, &in.y, 4);
            memcpy(pd + L.PedPos + 8, &in.z, 4);
            if (in.hasAngle)
                memcpy(pd + L.PedAngleOfs, &in.angle, 2);
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
        // Aufbau: "[Schlüssel]Text\0[Schlüssel]Text\0 ... []", Einträge durch Null-Bytes getrennt
        char key[16];
        const int keyLen = sprintf_s(key, "[%d]", id);
        for (int entry = 0; entry < 50000 && p[0] == '['; ++entry)
        {
            if (p[1] == ']')
                return false;
            size_t len = 0;
            while (p[len] && len < 4000)
                ++len;
            if (strncmp(p, key, keyLen) != 0)
            {
                p += len + 1;
                continue;
            }
            p += keyLen;
            // Die Texte des R2P-Pakets sind UTF-8, die der Original-CD Windows-1252. London (R2P) schreibt Sonderzeichen
            // als 0x83 + (zweites UTF-8-Byte - 0x80), z.B. "bi\x83\x1Fchen" = "bißchen": zurück in UTF-8 (0xC3 ..)
            std::string text(p, len - keyLen);
            for (size_t k = 0; k + 1 < text.size(); ++k)
            {
                const unsigned char next = (unsigned char)text[k + 1];
                if ((unsigned char)text[k] == 0x83 && next < 0x40)
                {
                    text[k] = (char)0xC3;
                    text[k + 1] = (char)(next + 0x80);
                    ++k;
                }
            }
            UINT cp = CP_UTF8;
            int w = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
            if (w <= 0)
            {
                cp = 1252;
                w = MultiByteToWideChar(cp, 0, text.data(), (int)text.size(), nullptr, 0);
            }
            if (w <= 0)
                return false;
            out.resize(w);
            MultiByteToWideChar(cp, 0, text.data(), (int)text.size(), &out[0], w);
            return true;
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

#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gta1
{
    struct Player
    {
        float x = 0, y = 0, z = 0;  // Blockkoordinaten (0..256)
        int angle = 0;              // Rohwert aus dem Spiel
        int ctrlType = 0;           // 0 Auto, 1 Zug, 2 zu Fuß, 3/4 fest, 5 Kamera
        int speed = 0;
    };

    // Stellen, an denen MGL Importe per "mov reg, [IAT]" lädt. cnc-ddraw überschreibt diese
    // IAT-Einträge beim Laden mit eigenen Hooks, deshalb biegen wir die Lesebefehle selbst um.
    struct ImportLoads
    {
        uintptr_t iatSlot;
        const uintptr_t* sites;
        size_t count;
    };
    ImportLoads GetProcAddressLoads();
    ImportLoads PeekMessageLoads();

    // Stelle, an der MGL DirectDrawCreate über eine Speicherzelle aufruft ("call [cell]"); 0 = nicht nötig.
    // In der Classics-Ausgabe lädt der ASI-Loader die Mod erst, während das Spiel sich die Funktion holt.
    struct CallCell
    {
        uintptr_t site;
        uintptr_t cell;
    };
    CallCell DirectDrawCreateCall();

    bool Init(HMODULE exe);        // prüft die GTA.exe-Version und installiert die Spiel-Hooks
    bool Supported();
    void SetHudCallback(void (*cb)()); // läuft nach jedem HUD-Zeichnen im Spiel
    bool InGame();                 // HUD wurde in den letzten 500 ms gezeichnet
    unsigned HudCount();
    bool GetPlayer(Player& out);

    void* PrimarySurface();        // IDirectDrawSurface* von MGL
    int PageCount();               // 1 = ohne Backbuffer, 2 = Double-Buffering
    void* DirectDrawObject();      // IDirectDraw* von MGL
    bool IsPageSurface(void* surface); // Primary oder eine der weiteren Seiten, in die MGL zeichnet

    bool IsSinglePlayer();
    int  RunState();               // 1 = läuft, 2 = pausiert
    void Pause();
    void Resume();
    void QuitLevel();              // zurück ins Hauptmenü, wie die bestätigte Beenden-Abfrage

    // Läuft jeden Frame nach der Simulation direkt vor dem Zeichnen der Welt (Spiel-Thread)
    void SetPreRenderCallback(void (*cb)());
    // Schussrichtung des Spielers, wenn sie von der Blickrichtung abweicht (Seitwärtslaufen), sonst -1
    void SetPlayerFireAngle(int angle);

    // Kamera eines Frames, alles in Weltpixeln (64 je Block, z wächst nach unten, 0 = oberste Ebene).
    // Projektion des Spiels: Bild = Bildmitte + (Welt - Kamera) * focal / (height + z)
    struct CameraView
    {
        float x, y;                       // Bildmittelpunkt, wie ihn das Spiel bestimmt hat
        float playerX, playerY, playerZ;  // Spielfigur bzw. ihr Fahrzeug
        float radius;                     // ungefähre halbe Größe der Figur bzw. des Fahrzeugs
        float height, focal;
        float squash;                     // senkrechter Bildfaktor (1 oder 5/6)
        float halfW, halfH;               // halbe Bildgröße in Pixeln der Spielauflösung
        int ctrlType;                     // 0 Auto, 1 Zug, 2 zu Fuß
        int angle;                        // Richtung der Figur bzw. des Fahrzeugs (Rohwert, 1024 = 360°)
        float zoom;                       // Zoomwert des Spiels; der Sichtbereich ist (height + 384) * 160 / zoom breit
    };
    // Kamerasteuerung: der Filter läuft jeden Frame, nachdem das Spiel den Bildmittelpunkt bestimmt hat,
    // und liefert einen zusätzlichen Versatz in Weltpixeln. view = nullptr, solange die Kamera nicht der
    // Spielfigur folgt (Zwischensequenz o.ä.)
    bool SetCameraFilter(void (*filter)(const CameraView* view, float& dx, float& dy));
    // Wo die Spielfigur (bzw. ihr Fahrzeug) im letzten Bild steht, in Pixeln der Spielauflösung
    bool PlayerScreenPos(float& x, float& y);
    bool GetCameraView(CameraView& out);

    // Kippkamera: Rückruf am Ende der Kameraführung, direkt bevor das Spiel das Bild vorbereitet und zeichnet.
    // Nur darin OverrideRenderCamera aufrufen: Mittelpunkt (Weltpixel) und Verkleinerung (>= 1) gelten bis nach
    // dem Zeichnen, dann stellt gta1 alles wieder her. used = tatsächliche Verkleinerung (ganzzahlige Kamerawerte)
    bool SetCameraOverride(void (*cb)());
    // Sichtbereich der Spiellogik um diesen Faktor vergrößern (Verkehr entsteht und verschwindet weiter draußen)
    // und feste Fahrzeugkamera aus Höhe und zoom (0 = wie im Spiel); auch während des Spiels änderbar
    bool SetViewOptions(float viewRectScale, int vehicleHeight, int vehicleZoom);
    bool OverrideRenderCamera(float centerX, float centerY, float shrink, float& used);
    // Läuft, wenn die Welt fertig gezeichnet ist und bevor das Spiel sein HUD darüberlegt
    void SetBeforeHudCallback(void (*cb)());
    // Speicher der Seite, in die das Spiel gerade zeichnet
    struct DrawBuffer
    {
        uint8_t* bits;
        int pitch, width, height, bytesPerPixel;
    };
    bool GetDrawBuffer(DrawBuffer& out);
    bool TiltCameraAvailable();    // Zeichenpuffer und Kamera dieser Spielversion sind bekannt

    // Umschließt das HUD-Zeichnen des Spiels: draw bekommt die Funktion, die das HUD zeichnet
    void SetHudDrawer(void (*draw)(void (*drawHud)()));
    // HUD in ein eigenes, um "factor" kleineres Bild zeichnen lassen (zum Vergrößern): zwischen Begin und End
    // zeichnet das Spiel nach "bits" (Zeilenlänge wie seine eigene Seite, width x height Pixel) und rechnet alle
    // Positionen für dieses kleinere Bild aus. Geht nur, wo die nötigen Stellen des Spiels bekannt sind.
    bool CanRedirectHud();
    bool BeginHudLayer(uint8_t* bits, int width, int height, int factor);
    void EndHudLayer();

    // Im Hauptmenü gewähltes Portrait (0 Ulrika .. 7 Kivlov) bzw. -1
    int PlayerCharacter();
    // Farbvariante, die das Spiel dem Charakter gibt (Haare/Haut je Charakter, Oberteil je Spielerplatz)
    int PlayerDefaultRemap();
    // Farbvariante (0..63) der Spielfigur zu Fuß
    bool GetPlayerPedRemap(int& remap);
    void SetPlayerPedRemap(int remap);

    // Spielregeln ([Gameplay]); false, wenn der Spielcode nicht wie erwartet aussieht
    bool EnableKeepMultiplier();   // Verhaftung halbiert den Multiplikator nicht mehr
    // Bäume, Laternen, Pfosten und die Masten von Schildern und Ampeln halten Fahrzeuge nicht mehr auf; lässt sich
    // im laufenden Level umschalten.
    // Liefert false, wenn die Spielversion es nicht kann.
    bool SetTreeCollision(bool remove);
    // Lenkhilfe: richtet den Wagen der Spielfigur sacht auf die nächste Himmelsrichtung aus, sobald die Lenkung ruht
    // (0 aus, 1 leicht, 2 mittel, 3 stark); lässt sich jederzeit umstellen. false, wenn die Spielversion es nicht kann.
    bool SetSteerAssist(int level);
    bool EnableAllCharacters();    // Hauptmenü: Frauen auch in nicht-englischen Sprachversionen wählbar
    bool EnableQuickRetry();       // nach "MISSION FEHLGESCHLAGEN" Skriptpausen bis zum Neuaufbau verkürzen
    bool EnableSteadyCamera();     // Kamera folgt enger statt zu schwimmen (Patch aus GTAforums: shl 5 -> shl 2)

    // Ereignisse aus dem Missionsskript (Spiel-Thread): Ansage mit Textnummer, geschaffte Mission (bei Telefonmissionen,
    // sobald das Skript die Telefongruppe wieder einschaltet - in San Andreas also am Ende einer ganzen Kette)
    enum class ScriptEvent { Brief, MissionEnd };
    bool SetScriptEventCallback(void (*cb)(ScriptEvent event, int value));
    // Eine Telefonmission läuft gerade (auch gescheitert, solange der neue Versuch aussteht)
    bool MissionActive();
    // Text zur Nummer aus den Texten des Spiels (FXT), leer wenn unbekannt
    bool LookupText(int id, std::wstring& out);

    // Spielstand zwischen Missionen: Skriptzustand (Objekte, Prozesse, Zeilen) und Spielerwerte
    struct GameState
    {
        uint32_t exeStamp = 0, levelHash = 0;
        int character = -1;
        int32_t score = 0;
        int16_t multiplier = 1, armor = 0;
        uint8_t jailCard = 0, lives = 0, ammo[4] = {};
        bool hasPos = false, hasAngle = false;
        int32_t x = 0, y = 0, z = 0;    // Festkomma wie im Spiel (22 Nachkommabits = 1 Block)
        int16_t angle = 0;
        std::vector<uint8_t> objects, procBlock, procState, lines;
        std::vector<uint8_t> triggers;          // leer bei Spielständen der Version 1
    };
    bool CanUseSaves();                        // im Level, Einzelspieler, Adressen bekannt
    uint32_t LevelHash();                      // erkennt den Level (Abschnitt der mission.ini)
    bool CaptureGameState(GameState& out);     // Spiel-Thread
    const char* RestoreGameState(const GameState& in); // Spiel-Thread; nullptr = geladen, sonst Grund

    // Ziel des Missionspfeils (which 0) bzw. des roten Pfeils (which 1) in Blockkoordinaten
    bool GetArrowTarget(int which, float& x, float& y);

    // Zustand der Steuerungstasten, wie ihn das Spiel aus den Tastenereignissen pflegt
    struct KeyboardInput
    {
        int steer;       // -7 .. 7
        int gas, brake;  // 0 .. 3
        bool jump, fire, horn, weaponNext, weaponPrev;
    };
    KeyboardInput GetKeyboardInput();

    // Filter für das Eingabepaket des lokalen Spielers (läuft einmal pro Frame der Hauptschleife)
    void SetInputFilter(uint32_t (*filter)(uint32_t packet));

    bool IsPlayerOnFoot();
    void SetPlayerPedAngle(int angle); // Spielwinkel 0..1023 (0 = Süden, gegen den Uhrzeigersinn)

    // Lebensenergie der Spielfigur 0..100, auch im Fahrzeug, bzw. -1
    int  PlayerHealth();
    // Spielfigur steht in Flammen (verliert dann jeden Frame einen Lebenspunkt)
    bool IsPlayerBurning();
    // Flammen an der Spielfigur entfernen (nur im Spiel-Thread)
    bool ExtinguishPlayer();
    // Test: Spielfigur anzünden wie eine Flamme im Spiel (nur im Spiel-Thread)
    bool IgnitePlayer();
    // Spielfigur tot oder im Sterben
    bool IsPlayerDead();
    // Schaden statt sofortigem Tod, in Prozent der vollen Lebensenergie: Kugeln aus Pistole und Maschinenpistole
    // treffen so nur die Spielfigur (100 = tödlich wie im Original), ein Faustschlag jede getroffene Figur
    // (0 = kein Schaden wie im Original). EnableDamage einmal beim Start, SetDamage jederzeit
    bool EnableDamage();
    void SetDamage(int pistol, int machineGun, int punch);
    // Für die Controller-Vibration: was sich an der Spielfigur von Schritt zu Schritt ändert
    struct Feedback
    {
        bool onFoot = false;
        int health = 0;                 // Lebensenergie der Figur 0..100, 0 = tot
        unsigned shots = 0;             // zählt jeden eigenen Schuss
        int weapon = 0;                 // Waffe des letzten Schusses: 1 Pistole, 2 Maschinenpistole, 3 Raketen, 4 Flammen
        int car = -1;                   // Nummer des gesteuerten Fahrzeugs, -1 = keins
        int carDamage = 0;              // sein Schaden 0..100
        float x = 0, y = 0;             // Blockkoordinaten
    };
    bool GetFeedback(Feedback& out);
    // Jede Explosion im Level melden (Spiel-Thread, mitten im Spielschritt); false, wenn der Spielcode nicht passt
    bool SetExplosionCallback(void (*cb)(float x, float y));

    // Fortschritt im laufenden Level: die Zahlen aus der Pausenanzeige des Spiels
    struct Progress
    {
        int score = 0, target = 0;      // Ziel: mit so vielen Punkten ist das Level geschafft
        int missionsDone = 0, missionsTotal = 0;
        int secretsFound = 0, secretsTotal = 0;
        int lives = 0, multiplier = 0;
    };
    bool GetProgress(Progress& out);

    // Brennschaden für die Spielfigur aussetzen (Flammen bleiben sichtbar); EnableBurnPause einmal beim Start
    bool EnableBurnPause();
    void SetPlayerBurnPaused(bool paused);
    // Nur fürs Zeichnen dieses Frames: Animationsbild (anim, -1 = das echte) und Blickrichtung (-1 = unverändert)
    // der Spielfigur setzen. Direkt vor dem Zeichnen im Spiel-Thread aufrufen.
    void ShowPlayerPose(int anim, int angle);
    bool GetPlayerPedAngle(int& angle);
    // Scancode der Sprungtaste aus den Spieleinstellungen (+0x100 = erweiterte Taste)
    int  JumpKeyScancode();

    std::string MapFile();         // zuletzt vom Spiel geöffnete .cmp (voller Pfad)
    unsigned MapGeneration();      // zählt hoch, sobald eine andere Karte geöffnet wird
    unsigned LevelLoads();         // zählt bei jedem Levelstart hoch (Karte wird geöffnet)
    std::string MissionFile();     // zuletzt gelesenes Missionsskript (voller Pfad)
    // Zeilennummern der Objekte aus dem Missionsskript des laufenden Levels (ohne die erste)
    bool ScriptObjectLines(std::vector<int>& lines);
}

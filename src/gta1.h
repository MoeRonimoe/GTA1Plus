#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>

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

    bool Init(HMODULE exe);        // prüft die GTA.exe-Version und installiert die Spiel-Hooks
    bool Supported();
    void SetHudCallback(void (*cb)()); // läuft nach jedem HUD-Zeichnen im Spiel
    bool InGame();                 // HUD wurde in den letzten 500 ms gezeichnet
    unsigned HudCount();
    bool GetPlayer(Player& out);

    void* PrimarySurface();        // IDirectDrawSurface* von MGL
    int PageCount();               // 1 = ohne Backbuffer, 2 = Double-Buffering

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
    };
    // Kamerasteuerung: der Filter läuft jeden Frame, nachdem das Spiel den Bildmittelpunkt bestimmt hat,
    // und liefert einen zusätzlichen Versatz in Weltpixeln. view = nullptr, solange die Kamera nicht der
    // Spielfigur folgt (Zwischensequenz o.ä.)
    bool SetCameraFilter(void (*filter)(const CameraView* view, float& dx, float& dy));
    // Wo die Spielfigur (bzw. ihr Fahrzeug) im letzten Bild steht, in Pixeln der Spielauflösung
    bool PlayerScreenPos(float& x, float& y);

    // Im Hauptmenü gewähltes Portrait (0 Ulrika .. 7 Kivlov) bzw. -1
    int PlayerCharacter();
    // Farbvariante, die das Spiel dem Charakter gibt (Haare/Haut je Charakter, Oberteil je Spielerplatz)
    int PlayerDefaultRemap();
    // Farbvariante (0..63) der Spielfigur zu Fuß
    bool GetPlayerPedRemap(int& remap);
    void SetPlayerPedRemap(int remap);

    // Spielregeln ([Gameplay]); false, wenn der Spielcode nicht wie erwartet aussieht
    bool EnableKeepMultiplier();   // Verhaftung halbiert den Multiplikator nicht mehr
    bool EnableAllCharacters();    // Hauptmenü: Frauen auch in nicht-englischen Sprachversionen wählbar
    bool EnableQuickRetry();       // nach "MISSION FEHLGESCHLAGEN" Skriptpausen bis zum Neuaufbau verkürzen
    bool EnableSteadyCamera();     // Kamera folgt enger statt zu schwimmen (Patch aus GTAforums: shl 5 -> shl 2)

    // Ziel des Missionspfeils (which 0) bzw. des roten Pfeils (which 1) in Blockkoordinaten
    bool GetArrowTarget(int which, float& x, float& y);

    // Zustand der Steuerungstasten, wie ihn das Spiel aus den Tastenereignissen pflegt
    struct KeyboardInput
    {
        int steer;       // -7 .. 7
        int gas, brake;  // 0 .. 3
        bool jump, fire, next, prev, special;
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
}

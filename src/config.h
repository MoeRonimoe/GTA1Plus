#pragma once

struct Config
{
    // [Features]
    bool pauseMenu      = true;  // neues Pausemenü mit Stadtkarte
    bool escOpensMenu   = true;  // ESC öffnet das neue Menü statt der Beenden-Abfrage
    bool minimap        = true;  // Minimap verfügbar
    bool minimapOnStart = false;
    bool hint           = true;  // Hinweis nach Spielstart
    bool gps            = true;  // Route zum Wegpunkt/Missionsziel auf den Karten
    bool gpsArrow       = true;  // blauer Richtungspfeil oben am Bildschirm
    bool waypoints      = true;  // eigene Wegpunkte
    bool twinStick      = true;  // zu Fuß mit WASD laufen und mit der Maus zielen
    bool controller     = true;  // XInput- und DirectInput-Controller
    bool alwaysAim      = false; // Twin-Stick: immer zum Fadenkreuz schauen, seitwärts laufen
    bool wasdDriving    = true;  // Twin-Stick: auch im Auto mit WASD fahren
    bool portraitColors = true;  // Spielfigur passend zum Portrait einfärben
    bool hearts         = true;  // Lebensenergie als Herzen unter dem Punktestand

    // [Gameplay]
    bool keepMultiplier = true;  // Multiplikator bei Verhaftung nicht halbieren
    bool quickRetry     = true;  // nach gescheiterter Mission ohne lange Wartezeit neu versuchen
    bool allCharacters  = true;  // Frauen im Hauptmenü auch in nicht-englischen Versionen wählbar
    bool extinguish     = true;  // brennend mit der Sprungtaste am Boden wälzen und löschen
    bool autosave       = true;  // nach jeder geschafften Mission automatisch speichern

    // [Camera]
    bool camera              = true; // Kamera mit Maus/rechtem Stick verschieben
    bool cameraMouseVehicle  = true; // auch im Fahrzeug mit der Maus
    int  cameraRangeFoot     = 50;   // höchstens so viel Prozent der halben Bildgröße
    int  cameraRangeVehicle  = 60;
    int  cameraDeadZone      = 40;   // Prozent: innerer Bereich, in dem die Maus nichts verschiebt
    bool cameraSteady        = true; // Kamera folgt enger (Patch aus GTAforums)
    int  tilt                = 0;    // Kippkamera wie in Chinatown Wars: 0 aus, 1 im Fahrzeug, 2 immer
    int  tiltAngle           = 35;   // Neigung in Grad
    bool tiltRotate          = true; // im Fahrzeug dreht sich das Bild mit (Fahrtrichtung oben)

    // [Keys]
    int mapKey          = 'M';
    int minimapKey      = 'N';
    int moveUpKey       = 'W';
    int moveLeftKey     = 'A';
    int moveDownKey     = 'S';
    int moveRightKey    = 'D';
    int enterExitKey    = 'F';

    // [PlayerColors] Farbvariante der Spielfigur je Charakter (0 = wie im Original)
    int playerColors[8] = { 0, 1, 31, 35, 40, 44, 0, 50 };

    // [Language]
    wchar_t language[16] = L"auto"; // auto = Sprache des Spiels, sonst de/en/fr/it

    // [Map]
    int  mapStyle       = 0;     // 0 Texturen, 1 Schema
    int  minimapCorner  = 0;     // 0 unten links, 1 unten rechts, 2 oben links, 3 oben rechts
    bool pauseGame      = true;

    // [Heading]
    bool showHeading    = true;  // Blickrichtung am Spielermarker
    int  headingOffset  = 0;     // zusätzliche Korrektur für den Winkel aus dem Spiel (0..1023)
    bool headingCcw     = true;  // Spielwinkel wächst gegen den Uhrzeigersinn

    // [Debug]
    bool log            = true;
    bool showInfo       = false;
};

extern Config g_cfg;

void LoadConfig(const wchar_t* iniPath);
// Einen geänderten Wert aus g_cfg (Adresse des Felds) in die INI zurückschreiben
bool SaveConfigValue(const void* value);

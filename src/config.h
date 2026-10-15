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
    int  vibration      = 70;    // Stärke der Controller-Vibration in Prozent, 0 = aus
    bool alwaysAim      = false; // Twin-Stick: immer zum Fadenkreuz schauen, seitwärts laufen
    bool walkToCrosshair = false; // Twin-Stick: W läuft zum Fadenkreuz statt im Bild nach oben
    bool wasdDriving    = true;  // Twin-Stick: auch im Auto mit WASD fahren
    bool portraitColors = true;  // Spielfigur passend zum Portrait einfärben
    bool pois           = true;  // Lackierereien, Bombenläden und Autokräne auf der Karte
    bool hearts         = true;  // Lebensenergie als Herzen unter dem Punktestand

    // [Gameplay]
    bool keepMultiplier = true;  // Multiplikator bei Verhaftung nicht halbieren
    bool noTreeCollision = true; // Bäume, Laternen, Pfosten und Schildermasten halten Fahrzeuge nicht mehr auf
    int  steerAssist    = 2;     // Lenkhilfe: Wagen auf die Straßenrichtung ausrichten (0 aus, 1 leicht, 2 mittel, 3 stark)
    int  extraMissionTime = 0;   // Sekunden mehr für jeden Countdown einer Mission (0 = wie im Spiel)
    bool quickRetry     = true;  // nach gescheiterter Mission ohne lange Wartezeit neu versuchen
    bool allCharacters  = true;  // Frauen im Hauptmenü auch in nicht-englischen Versionen wählbar
    bool extinguish     = true;  // brennend mit der Sprungtaste am Boden wälzen und löschen
    bool autosave       = true;  // nach jeder geschafften Mission automatisch speichern
    int  pistolDamage     = 35;  // Prozent der Lebensenergie je Pistolenkugel auf die Spielfigur (100 = tödlich wie im Original)
    int  machineGunDamage = 20;  // ebenso je Kugel aus der Maschinenpistole
    int  punchDamage      = 10;  // Prozent je Faustschlag, für jede getroffene Figur (0 = kein Schaden wie im Original)

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
    int  vehicleView         = 0;    // Kamerahöhe im Fahrzeug: 0 wie im Spiel, 1 hoch, 2 sehr hoch, 3 maximal
    int  footView            = 0;    // Kamerahöhe zu Fuß: 0 wie im Spiel, 1 hoch, 2 sehr hoch, 3 maximal
    int  trafficRange        = 100;  // Prozent: Sichtbereich, außerhalb dessen Verkehr entsteht und verschwindet

    // [Keys]
    int mapKey          = 'M';
    int minimapKey      = 'N';
    int moveUpKey       = 'W';
    int moveLeftKey     = 'A';
    int moveDownKey     = 'S';
    int moveRightKey    = 'D';
    int enterExitKey    = 'F';
    int prevWeaponKey   = 'Q';     // zusätzlich zu den Tasten aus den Spieleinstellungen und dem Mausrad
    int nextWeaponKey   = 'E';

    // [PlayerColors] Farbvariante der Spielfigur je Charakter (0 = wie im Original)
    int playerColors[8] = { 0, 1, 31, 35, 40, 44, 0, 50 };

    // [Language]
    wchar_t language[16] = L"auto"; // auto = Sprache des Spiels, sonst de/en/fr/it

    // [Map]
    int  mapStyle       = 0;     // 0 Texturen, 1 Schema
    int  minimapCorner  = 0;     // 0 unten links, 1 unten rechts, 2 oben links, 3 oben rechts
    bool pauseGame      = true;

    // [Display]
    bool frameBuffer    = true;  // Bilder im Arbeitsspeicher fertigstellen, wo der Bildspeicher langsam zu lesen ist
    bool scaleMenu      = true;  // Hauptmenü vergrößern, wo das Spiel es klein in eine Ecke zeichnet
    int  hudScale       = 0;     // HUD des Spiels vergrößern: 0 automatisch nach Bildhöhe, 1 wie im Spiel, 2..4 Faktor

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

// Fahrzeugkamera für [Camera] VehicleView. Das Spiel zeigt aus Höhe/zoom zu Fuß 78/284 und im stehenden Fahrzeug
// 80/284 gut 4 Blöcke nach jeder Seite und zoomt mit dem Tempo bis 268/90 heraus (18 Blöcke; in allen Versionen
// gleich). Die Stufen liegen im oberen Teil davon (12, 15 und 18 Blöcke); 0/0 = wie im Spiel
inline void VehicleCameraView(int view, int& height, int& zoom)
{
    static const float kShare[] = { 0.0f, 0.4f, 0.7f, 1.0f };
    const float t = view > 0 && view < 4 ? kShare[view] : 0.0f;
    height = t > 0 ? (int)(80 + t * 188) : 0;
    zoom = t > 0 ? (int)(126 - t * 36) : 0;
}

// Kamera zu Fuß für [Camera] FootView: drei gleich große Schritte von der Kamera des Spiels (78/284) bis zu der
// bei schneller Fahrt (268/90, dasselbe Bild wie VehicleView=3), Höhe und zoom gemeinsam wie beim Spiel. Jede
// Stufe zeigt rund zwei Drittel mehr nach jeder Seite: 7, 11 und 18 Blöcke. 0/0 = wie im Spiel
inline void FootCameraView(int view, int& height, int& zoom)
{
    static const int kHeight[] = { 0, 141, 205, 268 }, kZoom[] = { 0, 194, 132, 90 };
    const int i = view > 0 && view < 4 ? view : 0;
    height = kHeight[i];
    zoom = kZoom[i];
}

#pragma once

#include <windows.h>
#include <cstdint>

// Twin-Stick-Steuerung zu Fuß (WASD + Maus) und XInput-Controller
namespace controls
{
    void Init();

    // Tasten- und Mausmeldungen des Spielfensters (aus PeekMessageA, bevor das Menü sie ggf. schluckt)
    void OnMessage(const MSG& msg);

    // Läuft einmal pro Spiel-Frame direkt nach dem Einlesen der Tastatur (0x4365E0) und ergänzt das
    // Eingabepaket, genau so wie es das Spiel selbst für gedrückte Steuerungstasten tut
    uint32_t ProcessPacket(uint32_t packet);

    // Mausziel in Spielkoordinaten, solange zu Fuß mit der Maus gezielt wird
    bool Crosshair(float& x, float& y);
    // Dasselbe Ziel im ungekippten Spielbild (für Rechnungen in der Welt)
    bool AimPoint(float& x, float& y);
    // Mauszeiger zum Umschauen im Fahrzeug
    bool LookCursor(float& x, float& y);

    // Letzte Mausposition in Bildschirmkoordinaten des Spiels (z.B. 1068x600)
    bool MousePosition(float& x, float& y);

    // Ein Controller (XInput oder DirectInput) ist verbunden
    bool PadConnected();

    // Aus der Nachrichtenschleife des Spiels (Spiel-Thread): Controller im Hauptmenü als Tastatur
    void OnPump();

    // Direkt vor dem Zeichnen der Welt: beim Seitwärtslaufen die Figur zum Fadenkreuz drehen
    void BeforeRender();

    // Zwei Zeilen Zustand für die Debug-Anzeige
    void DebugLines(wchar_t* line1, wchar_t* line2, size_t size);
}

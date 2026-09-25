#pragma once

#include <windows.h>

#include "surface.h"

class CityMap;

namespace overlay
{
    // basePath = Pfad der .asi ohne Endung (für die Debug-Bildausgabe)
    void Init(const wchar_t* basePath);

    // Wird vor jedem Flip mit dem fertigen Spielbild (Backbuffer) aufgerufen – im Spiel-Thread
    void OnFrame(SurfaceView& back);

    // Tastatur-Nachrichten aus PeekMessageA; true = Nachricht vor dem Spiel verstecken
    bool FilterMessage(const MSG& msg, bool removed);

    // Wird bei jedem PeekMessageA des Spiels aufgerufen (auch wenn das Spiel pausiert ist)
    void OnPump();

    // Für den Controller (alles im Spiel-Thread)
    bool MenuActive();                  // Menü offen oder gerade angefordert
    void InjectKey(int vk);             // wirkt im Menü wie ein Tastendruck
    void SetHeld(int vk, bool held);    // gehaltene Richtung zum Verschieben der Karte
    void RequestOpen(bool mapPage);
    void ToggleMinimap();

    // Auflösung des zuletzt gezeichneten Spielbilds
    bool ScreenSize(int& w, int& h);

    // Die aktuell gespielte Stadt (lädt sie bei Bedarf), nullptr solange keine bekannt ist
    const CityMap* City();
}

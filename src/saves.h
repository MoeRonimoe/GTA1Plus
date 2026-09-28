#pragma once

#include <string>

// Spielstände je Spielversion, Level und Charakter (Stand zwischen zwei Missionen). Alles im Spiel-Thread.
namespace saves
{
    // basePath: Pfad der ASI ohne Endung; die Stände liegen in "<basePath>_saves"
    void Init(const wchar_t* basePath);

    bool Available();       // Speichern ist in diesem Spiel und Level grundsätzlich möglich
    bool HasSave();         // für Level und Charakter gibt es einen Stand
    bool CanSave();         // jetzt speichern (keine Mission läuft)
    bool Save(bool autosave);
    // nullptr = geladen, sonst Grund (für das Log)
    const char* Load();
    // Datum und Punkte des vorhandenen Stands für die Anzeige
    bool SlotInfo(std::wstring& text);

    // Mission geschafft: nach kurzer Pause automatisch speichern
    void OnMissionEnd();
    // Jeden Frame vor dem Zeichnen: fälliges Autosave
    void Tick();
    // Neues Level mit vorhandenem Stand: einmal true, damit das Menü das Laden anbieten kann
    bool TakeLoadOffer();
    // Meldung für das HUD (z.B. "Automatisch gespeichert"), einmal abholen
    bool TakeNotice(std::wstring& text, bool& error);
}

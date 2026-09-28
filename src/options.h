#pragma once

#include <string>

// Einstellungen im Pausemenü ("GTA1Plus-Optionen"): ändern, sofort in die INI schreiben und, wo möglich,
// sofort anwenden. Einträge ohne Wert sind Gruppenüberschriften.
namespace options
{
    int Count();
    bool IsHeader(int i);
    const wchar_t* Label(int i);
    std::wstring Value(int i);
    // Änderung wirkt erst nach einem Neustart des Spiels (Patches und Hooks beim Laden der ASI)
    bool NeedsRestart(int i);
    // Wert um einen Schritt ändern (dir -1/+1, Enter = +1); true, wenn sich etwas geändert hat
    bool Change(int i, int dir);
}

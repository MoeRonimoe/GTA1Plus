#pragma once

#include <windows.h>
#include <cstddef>
#include <cstdint>

#include "surface.h"

namespace ddhook
{
    // Hängt sich in GetProcAddress("DirectDrawCreate") der GTA.exe ein – über die Import-Tabelle
    bool Install(HMODULE exe);

    // Variante für bekannte Programme: biegt die Lesebefehle des IAT-Eintrags um (robust gegen cnc-ddraw)
    bool InstallAtLoads(uintptr_t iatSlot, const uintptr_t* sites, size_t count);

    // Variante für Spiele, die DirectDrawCreate über eine eigene Speicherzelle aufrufen ("call [cell]") und die
    // Funktion schon geholt haben, wenn die Mod geladen wird
    bool InstallAtCall(uintptr_t site, uintptr_t cell);

    // Hängt Flip an die VTable einer bereits existierenden Primary-Surface (Rückfallebene)
    void EnsureFlipHook(void* primarySurface, int chainLength);

    // Aus der Nachrichtenschleife des Spiels: hält die Hooks am Leben und zeigt das vergrößerte Hauptmenü
    void OnPump();

    // Zeichnet das Overlay und flippt selbst (für das Menü, falls das Spiel gerade nicht präsentiert)
    bool PresentOverlay();

    // Ruft fn mit dem aktuell sichtbaren Bild auf
    bool WithFrontBuffer(void (*fn)(SurfaceView& view));

    DWORD LastGameFlipTick();
    unsigned GameFlipCount();
    DWORD GameThreadId();
    void NoteGameThread();
}

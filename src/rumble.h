#pragma once

#include <windows.h>

// Controller-Vibration: Schüsse, Treffer, Unfälle und Explosionen in der Nähe.
// Xbox-Pads über XInput, PS4/PS5-Pads über HID-Ausgabeberichte (USB und Bluetooth).
namespace rumble
{
    void Init();    // einmal beim Start (Spielcode für Explosionen einhängen)

    // Welches Pad vibrieren soll; das Steuerungsmodul meldet Verbinden und Trennen (Spiel-Thread)
    using XInputSetState_t = DWORD(WINAPI*)(DWORD, void*);
    void UseXInput(XInputSetState_t setState);      // nullptr = kein XInput-Pad
    void UseHid(const wchar_t* devicePath, WORD vid, WORD pid, const wchar_t* name);   // nullptr = kein HID-Pad

    // Einmal pro Durchlauf der Hauptschleife. active = es wird gerade mit dem Controller gespielt (nicht im Menü,
    // nicht pausiert); strength = Stärke in Prozent
    void Tick(bool active, int strength);

    // Debug: beide Motoren so lange auf volle Stärke
    void Test(int ms);
}

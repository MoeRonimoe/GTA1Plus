#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Brandbombe: die gestrichene fünfte Waffe von GTA1. Im Spiel stecken davon nur das Symbol (HUD-Sprites 9 und 34,
// ein Benzinkanister) und der Kistentyp 5 ohne Wirkung; Kisten, Wechsel, Wurf und Anzeige führt der Mod.
namespace firebomb
{
    void Init();                    // nach gta1::Init: Kisten einhängen
    void Tick();                    // jedes Bild vor dem Zeichnen (Spiel-Thread)

    // Gedrückte Tasten eines Bilds (gehalten, nicht nur Druck); zurück, was das Spiel davon bekommen soll.
    // onFoot: geworfen wird nur zu Fuß
    struct Keys { bool fire, next, prev; };
    Keys Filter(Keys in, bool onFoot);

    bool Selected();                // statt der Faust ist die Brandbombe gewählt
    bool NativeHud();               // Symbol und Anzahl zeichnet das HUD des Spiels, sonst das Overlay
    int  Ammo();
    void SetAmmo(int n);            // Spielstände

    // Zum Zeichnen: fliegende Bomben in Pixeln der Spielauflösung, scale = Größe im Bogen (1 am Boden)
    struct Bomb { float x, y, scale; };
    void FlyingBombs(std::vector<Bomb>& out);
    // Symbol aus der Style-Datei (ARGB, Alpha 0 = durchsichtig), nullptr solange keins geladen ist
    const uint32_t* Icon(bool large, int& w, int& h);
    bool TakeNotice(std::wstring& out);
}

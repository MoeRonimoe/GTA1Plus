#pragma once

// Vergrößert das HUD des Spiels (Punktestand, Leben, Waffe, Pager, Meldungen, Pfeile). Das Spiel zeichnet es in
// fester Pixelgröße - bei hohen Auflösungen wird es winzig.
namespace hudscale
{
    void Init();        // hängt sich um das HUD-Zeichnen des Spiels
    bool Available();   // lässt sich das HUD dieser Spielversion vergrößern?
    int Factor();       // Faktor, mit dem das HUD gerade gezeichnet wird (1 = wie im Spiel)
    int AutoFactor();   // Faktor, den die Einstellung "automatisch" bei der aktuellen Bildhöhe ergibt
}

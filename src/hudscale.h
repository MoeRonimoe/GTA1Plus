#pragma once

// Vergrößert das HUD des Spiels (Punktestand, Leben, Waffe, Pager, Meldungen, Pfeile). Das Spiel zeichnet es in
// fester Pixelgröße - bei hohen Auflösungen wird es winzig.
namespace hudscale
{
    void Init();        // hängt sich um das HUD-Zeichnen des Spiels
    bool Available();   // lässt sich das HUD dieser Spielversion vergrößern?
    int Factor();       // Faktor, mit dem das HUD gerade gezeichnet wird (1 = wie im Spiel)
    int AutoFactor();   // Faktor, den die Einstellung "automatisch" bei der aktuellen Bildhöhe ergibt

    // Wo das HUD im letzten Bild gezeichnet hat, in Zellen von cellPx x cellPx Bildpunkten des Spiels (width x height):
    // cells[row * cols + col] != 0. Mit Vergrößerung überall, sonst nur in den Spalten x0..x1, die Watch zuletzt
    // (vor höchstens einer halben Sekunde) genannt hat; ohne lesbaren Bildspeicher gar nicht.
    void Watch(int x0, int x1);
    struct Coverage
    {
        const unsigned char* cells;
        int cols, rows, cellPx, width, height;
    };
    bool LastCoverage(Coverage& out);
}

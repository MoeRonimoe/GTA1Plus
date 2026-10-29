#pragma once

#include <vector>

// Hubschrauber: das gestrichene Fahrzeug Modell 88 (für das Spiel ein Bus mit Hubschrauber-Grafik) fliegt. Am Boden
// fährt ihn das Spiel, nur im Schritttempo zum Wenden; hebt er ab, lässt dessen Fahrphysik ihn aus und der Mod fliegt
// ihn, träge: lenken dreht, Gas schiebt in Bugrichtung, das Tempo läuft nur langsam aus, die Höhe wählt der Spieler.
// Unterhalb der Oberkante der Karte stoßen Gebäude und Deckel (Brücken) in der Flughöhe an, dicht über der Straße
// auch Fahrzeuge (schnell = Absturz); sanft aufsetzen gibt ihn ans Spiel zurück, Aussteigen in der Höhe ist tödlich.
// Dächer sind für die Engine keine Standfläche (der Boden über einem Gebäudeblock liegt in ihm), nur eigens begehbar
// gebaute. In der Luft zeichnet ihn das Overlay samt Rotor und Schatten, unter einer Brücke das Spiel.
namespace heli
{
    void Init();                                // nach gta1::Init
    void Tick();                                // jedes Bild vor dem Zeichnen (Spiel-Thread)
    bool Active();                              // die Spielfigur sitzt im Hubschrauber
    bool Flying();                              // ... und er ist in der Luft
    void SetInput(bool up, bool down);          // gehaltene Tasten: steigen, sinken
    void SetDrive(float steer, float throttle); // -1..1: rechts positiv, Gas positiv

    // Abstellplätze auf der geladenen Karte (Blockkoordinaten der Mitte): dort steht beim Levelstart je ein Hubschrauber
    struct Spot { float x, y; };
    void Spots(std::vector<Spot>& out);
    bool TakeHelp();                            // gerade eingestiegen: Steuerung einblenden

    // Zum Zeichnen: Ort in Weltpixeln (z kleiner = höher), Boden darunter, Winkel (1024 = 360°, 0 = Süden) und das
    // Tempo als Anteil des Höchsttempos (Welt-x/y, für die Neigung); false am Boden und unter einer Brücke
    struct Visual { float x, y, z, ground, heading, vx, vy; };
    bool GetVisual(Visual& out);
}

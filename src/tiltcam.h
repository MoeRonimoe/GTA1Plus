#pragma once

// Kippkamera im Stil von GTA Chinatown Wars (Prototyp). Das Spiel zeichnet die Welt weiterhin senkrecht von
// oben, nur kleiner und um die Blickrichtung verschoben; vor dem HUD wird das fertige Bild wie von einer
// schräg stehenden Kamera betrachtet umgerechnet und im Fahrzeug mit der Fahrtrichtung gedreht.
namespace tiltcam
{
    // Am Ende der Kameraführung (Spiel-Thread): Überblendung und Kamera für das nächste Bild festlegen
    void BeginFrame();
    // Welt ist gezeichnet, HUD noch nicht: Bild umrechnen
    void Warp();

    // Aktuelles Bild ist gekippt bzw. gedreht
    bool Active();
    // Drehung des Bildes im Uhrzeigersinn (Bogenmaß): Weltrichtungen erscheinen um diesen Winkel gedreht
    float Rotation();
    // Bildschirmpunkt (wie er zu sehen ist) -> Punkt im ungekippten Spielbild und zurück
    bool OutputToGame(float ox, float oy, float& gx, float& gy);
    bool GameToOutput(float gx, float gy, float& ox, float& oy);
}

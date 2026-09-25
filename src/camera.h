#pragma once

#include "gta1.h"

// Kamerasteuerung: verschiebt den Bildausschnitt Richtung Mauszeiger bzw. rechtem Stick
namespace camera
{
    // Gewünschter Versatz in Pixeln der Spielauflösung (aus der Eingabe, einmal pro Spiel-Frame)
    void SetTarget(float dx, float dy);
    // Sofort wieder mittig (neues Level)
    void Reset();

    // Filter für gta1::SetCameraFilter: geglätteter Versatz in Weltpixeln, der die Spielfigur nie
    // hinter einem Gebäude verschwinden lässt
    void Filter(const gta1::CameraView* view, float& dx, float& dy);

    // Aktueller Versatz in Weltpixeln (Debug-Anzeige)
    void Current(float& dx, float& dy);
}

#pragma once

#include <vector>

class CityMap;
namespace gta1 { struct Player; }

// Wegpunkte, Zielauswahl und Routenberechnung auf dem Blockraster der Stadt
namespace nav
{
    struct Point
    {
        float x, y; // Blockkoordinaten
    };

    enum class TargetKind { None, Waypoint, Mission, MissionRed };

    // Pro Frame im Spiel aufrufen; berechnet die Route nur neu, wenn sich Start, Ziel oder Modus ändern
    void Update(const CityMap& map, const gta1::Player* player);
    void Reset();

    TargetKind Kind();
    Point Target();
    const std::vector<Point>& Route();   // Spieler, Abbiegepunkte ..., Ziel; leer wenn keine Route gefunden
    bool NextTurn(Point& out);           // nächster Abbiegepunkt (oder Ziel), auf den der GPS-Pfeil zeigt
    float RemainingBlocks();             // Länge der restlichen Route bzw. Luftlinie

    const std::vector<Point>& Waypoints(); // der letzte Eintrag ist das aktive Ziel
    void ToggleWaypoint(float x, float y);  // setzt einen Wegpunkt oder entfernt den an dieser Stelle
    void ClearWaypoints();
}

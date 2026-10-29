#pragma once

#include <vector>

// Orte für die Karte (Lackierereien, Bombenläden, Autokräne) aus dem Missionsskript des laufenden Levels, dazu die
// Abstellplätze der Hubschrauber
namespace pois
{
    enum class Kind { Spray, BombShop, Crane, Heli, Count };

    struct Poi
    {
        Kind kind;
        float x, y;   // Blockkoordinaten (Mitte)
    };

    // Orte des laufenden Levels; wird beim ersten Aufruf nach einem Levelstart eingelesen (Spiel-Thread)
    const std::vector<Poi>& Current();
}

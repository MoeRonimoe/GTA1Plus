#pragma once

#include <vector>

// Orte für die Karte (Lackierereien, Bombenläden, Autokräne) aus dem Missionsskript des laufenden Levels, dazu die
// Abstellplätze der Hubschrauber und die Stellen, an denen gerade eine Mission beginnen kann
namespace pois
{
    enum class Kind { Spray, BombShop, Crane, Heli, Phone, CarMission, Frenzy, Prize, Count };

    struct Poi
    {
        Kind kind;
        float x, y;   // Blockkoordinaten (Mitte)
    };

    // Orte des laufenden Levels; wird beim ersten Aufruf nach einem Levelstart eingelesen (Spiel-Thread)
    const std::vector<Poi>& Current();

    // Missionsstarts, die gerade offen sind: Telefone der Auftraggeber (Phone), abgestellte Autos mit Auftrag
    // (CarMission) und bei level >= 2 Kill Frenzies (Frenzy). Erledigte und gerade gesperrte fehlen. Spiel-Thread.
    const std::vector<Poi>& Missions(int level);

    // Belohnungskiste nach einer geschafften Kill Frenzy, die gerade steht und auf Abholung wartet (Vice City: "COLLECT
    // YOUR PRIZE ..." mit Frist). Spiel-Thread.
    bool Prize(float& x, float& y);
}

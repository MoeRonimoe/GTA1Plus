#pragma once

// Mehr Zeit für Missionen mit Countdown ([Gameplay] ExtraMissionTime). Ordnet jedem Countdown des laufenden Levels
// seine Frist im Missionsskript zu und gibt beides an gta1 weiter, das sie beim Ausführen verlängert.
namespace missiontimers
{
    // Jeden Frame vor dem Zeichnen (Spiel-Thread)
    void Tick();
}

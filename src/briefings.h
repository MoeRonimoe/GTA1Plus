#pragma once

#include <string>

// Die letzten Telefon- und Pager-Ansagen des laufenden Levels zum Nachlesen im Pausemenü
namespace briefings
{
    // Aus dem Missionsskript (Spiel-Thread): Ansage mit dieser Textnummer wurde gezeigt
    void OnBrief(int textId);
    int Count();
    // 0 = neueste Ansage
    std::wstring Text(int i);
}

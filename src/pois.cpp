#include "pois.h"
#include "gta1.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace pois
{
    namespace
    {
        struct Section
        {
            int number = 0;
            std::vector<int> objectLines;    // Zeilennummern aller Objekte (für die Zuordnung zum laufenden Level)
            std::vector<Poi> pois;
        };

        std::vector<Poi> g_current;
        unsigned g_level = ~0u;
        DWORD g_retryTick = 0;

        const char* SkipSpaces(const char* p)
        {
            while (*p == ' ' || *p == '\t')
                ++p;
            return p;
        }

        bool ReadInt(const char*& p, int& v)
        {
            char* end;
            const long n = strtol(p, &end, 10);
            if (end == p)
                return false;
            v = (int)n;
            p = end;
            return true;
        }

        // Objektzeile "<nr> [<1>] (<x>,<y>,<z>) <TYP> ..."
        bool ParseObject(const char* p, int& line, int& x, int& y, int& z, std::string& type)
        {
            int dummy;
            p = SkipSpaces(p);
            if (!ReadInt(p, line))
                return false;
            p = SkipSpaces(p);
            if (*p != '(' && !ReadInt(p, dummy))
                return false;
            p = SkipSpaces(p);
            if (*p++ != '(' || !ReadInt(p, x) || *p++ != ',' || !ReadInt(p, y) || *p++ != ',' || !ReadInt(p, z) || *p++ != ')')
                return false;
            p = SkipSpaces(p);
            type.clear();
            while (isupper((unsigned char)*p) || *p == '_')
                type += *p++;
            return !type.empty();
        }

        std::vector<Section> ReadSections(const std::string& path)
        {
            std::vector<Section> sections;
            std::ifstream in(path, std::ios::binary);
            std::string line;
            while (std::getline(in, line))
            {
                const char* p = SkipSpaces(line.c_str());
                if (*p == '[')
                {
                    const int n = atoi(p + 1);
                    if (n > 0 || p[1] == '0')
                    {
                        sections.emplace_back();
                        sections.back().number = n;
                    }
                    continue;
                }
                int nr, x, y, z;
                std::string type;
                if (sections.empty() || !ParseObject(p, nr, x, y, z, type))
                    continue;
                Section& s = sections.back();
                s.objectLines.push_back(nr);
                const Kind kind = type == "SPRAY" ? Kind::Spray : type == "BOMBSHOP" ? Kind::BombShop
                                : type == "CRANE" ? Kind::Crane : Kind::Count;
                if (kind == Kind::Count)
                    continue;
                // Die meisten Objekte stehen in Blöcken, manche (z.B. Kräne) in Weltpixeln (64 je Block)
                const bool pixels = x > 255 || y > 255 || z > 7;
                Poi poi;
                poi.kind = kind;
                poi.x = pixels ? x / 64.0f : x + 0.5f;
                poi.y = pixels ? y / 64.0f : y + 0.5f;
                s.pois.push_back(poi);
            }
            return sections;
        }

        // Nebeneinanderliegende Felder desselben Orts (z.B. zwei Einfahrten einer Lackiererei) zusammenfassen
        std::vector<Poi> Merge(const std::vector<Poi>& raw)
        {
            std::vector<Poi> out;
            std::vector<int> count;
            for (const Poi& p : raw)
            {
                size_t k = 0;
                for (; k < out.size(); ++k)
                    if (out[k].kind == p.kind && std::fabs(out[k].x - p.x) <= 3 && std::fabs(out[k].y - p.y) <= 3)
                        break;
                if (k == out.size())
                {
                    out.push_back(p);
                    count.push_back(1);
                    continue;
                }
                const float n = (float)count[k]++;
                out[k].x = (out[k].x * n + p.x) / (n + 1);
                out[k].y = (out[k].y * n + p.y) / (n + 1);
            }
            return out;
        }

        // Abschnitt, dessen Objektzeilen am besten zu den belegten Zeilen im Speicher passen
        bool Load()
        {
            const std::string path = gta1::MissionFile();
            std::vector<int> used;
            if (path.empty() || !gta1::ScriptObjectLines(used))
                return false;
            const std::vector<Section> sections = ReadSections(path);
            const Section* best = nullptr;
            float bestScore = 0;
            for (const Section& s : sections)
            {
                std::vector<int> mine = s.objectLines;
                std::sort(mine.begin(), mine.end());
                mine.erase(std::unique(mine.begin(), mine.end()), mine.end());
                std::vector<int> both;
                std::set_intersection(mine.begin(), mine.end(), used.begin(), used.end(), std::back_inserter(both));
                const float score = both.size() / (float)(mine.size() + used.size() - both.size());
                if (score > bestScore)
                {
                    bestScore = score;
                    best = &s;
                }
            }
            if (!best || bestScore < 0.5f)
            {
                LOG("Orte: kein passender Abschnitt in %s (beste Übereinstimmung %.2f)", path.c_str(), bestScore);
                g_current.clear();
                return bestScore > 0; // unpassend, aber eingelesen: nicht ständig neu versuchen
            }
            g_current = Merge(best->pois);
            LOG("Orte: Abschnitt [%d] aus %s (Übereinstimmung %.2f), %u Orte", best->number, path.c_str(), bestScore,
                (unsigned)g_current.size());
            return true;
        }
    }

    const std::vector<Poi>& Current()
    {
        const unsigned level = gta1::LevelLoads();
        if (level != g_level && gta1::InGame() && GetTickCount() - g_retryTick > 2000)
        {
            g_retryTick = GetTickCount();
            g_current.clear();
            if (Load())
                g_level = level;
        }
        return g_current;
    }
}

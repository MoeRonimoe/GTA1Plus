#include "pois.h"
#include "gta1.h"
#include "heli.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

namespace pois
{
    namespace
    {
        // Auslöser im Spiel (Art im Eintrag der Auslöser-Tabelle)
        constexpr int kTrigPlace = 4, kTrigCar = 9, kTrigPhones = 12;

        // Stelle, an der eine Mission beginnen kann: Auslöser aus Objektzeile line, bei Autos das Fahrzeug aus carLine,
        // bei Telefongruppen deren Telefone (Objektzeilen phoneLine .. phoneLine + phoneCount - 1)
        struct Start
        {
            Kind kind = Kind::Phone;
            int line = 0;
            int trigger = 0;
            int carLine = -1;
            int phoneLine = -1, phoneCount = 0;
        };

        struct Section
        {
            int number = 0;
            std::vector<int> objectLines;    // Zeilennummern aller Objekte (für die Zuordnung zum laufenden Level)
            std::vector<Poi> pois;
            std::vector<Start> starts;
            std::vector<Poi> prizes;         // Belohnungskisten nach Kill Frenzies (Vice City)
        };

        // Objektzeile "<nr> [1] (<x>,<y>,<z>) <TYP> <a> <b> ..."; die 1 vor dem Ort heißt: besteht ab Levelstart,
        // ein Auslöser ist dann scharf (ohne sie schaltet ihn erst eine Mission mit ENABLE ein)
        struct Object
        {
            std::string type;
            bool on = false;
            int x = 0, y = 0, z = 0;
            int a = 0, b = 0;      // Auslöser: Zielzeile, CARTRIGGER: Objektzeile des Fahrzeugs
        };

        // Befehlszeile "<nr> <BEFEHL> <obj> <succ> <fail> <data> ..."; succ 0 = weiter mit der nächsten Zeile,
        // > 0 = Sprung, -1 = Ende des Prozesses; fail ebenso für den anderen Ausgang
        struct Command
        {
            int line = 0;
            bool frenzy = false;   // gehört zu einer Kill Frenzy
            bool mphone = false;   // MPHONE: Telefone ab Objektzeile obj, fail = Anzahl
            bool powerupOn = false, powerupDone = false;   // POWERUP_ON / IS_POWERUP_DONE (data = Frist in Bildern)
            bool kickstart = false;                        // KICKSTART: startet zusätzlich einen Prozess bei obj
            bool end = false;                              // END: Level zu Ende
            int obj = 0, succ = 0, fail = 0, data = 0;
        };

        std::vector<Poi> g_current;
        std::vector<Start> g_starts;
        std::vector<Poi> g_missions;
        std::vector<Poi> g_prizes;
        unsigned g_level = ~0u;
        DWORD g_retryTick = 0, g_missionTick = 0, g_prizeTick = 0;
        bool g_prizeOn = false;
        Poi g_prize = {};
        int g_missionLevel = -1;

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

        std::string ReadWord(const char*& p)
        {
            std::string w;
            while (isupper((unsigned char)*p) || *p == '_')
                w += *p++;
            return w;
        }

        bool ParseObject(const char* p, int& line, Object& o)
        {
            p = SkipSpaces(p);
            if (!ReadInt(p, line))
                return false;
            p = SkipSpaces(p);
            int flag = 0;
            if (*p != '(' && !ReadInt(p, flag))
                return false;
            o.on = flag == 1;
            p = SkipSpaces(p);
            if (*p++ != '(' || !ReadInt(p, o.x) || *p++ != ',' || !ReadInt(p, o.y) || *p++ != ',' || !ReadInt(p, o.z) || *p++ != ')')
                return false;
            p = SkipSpaces(p);
            o.type = ReadWord(p);
            if (o.type.empty())
                return false;
            o.a = o.b = 0;
            p = SkipSpaces(p);
            if (ReadInt(p, o.a))
            {
                p = SkipSpaces(p);
                ReadInt(p, o.b);
            }
            return true;
        }

        bool ParseCommand(const char* p, Command& c)
        {
            p = SkipSpaces(p);
            if (!ReadInt(p, c.line) || (*p != ' ' && *p != '\t'))
                return false;
            p = SkipSpaces(p);
            const std::string cmd = ReadWord(p);
            if (cmd.empty() || (*p && *p != ' ' && *p != '\t' && *p != '\r'))
                return false;
            static const char* const kFrenzy[] = { "KF_PROCESS", "FRENZY_SET", "FRENZY_BRIEF", "FRENZY_CHECK",
                                                   "KF_BRIEF_TIMED", "KF_BRIEF_GENERAL" };
            c.frenzy = std::any_of(std::begin(kFrenzy), std::end(kFrenzy), [&](const char* k) { return cmd == k; });
            c.mphone = cmd == "MPHONE";
            c.powerupOn = cmd == "POWERUP_ON";
            c.powerupDone = cmd == "IS_POWERUP_DONE";
            c.kickstart = cmd == "KICKSTART";
            c.end = cmd == "END";
            int* fields[] = { &c.obj, &c.succ, &c.fail, &c.data };
            p = SkipSpaces(p);
            for (int* f : fields)
                if (!ReadInt(p, *f))
                    break;
            return true;
        }

        bool IsCarObject(const std::string& type)
        {
            return type == "PARKED" || type == "PARKED_PIXELS" || type == "FUTURECAR";
        }

        // Zeilen, die ein Prozess ab Zeile from erreicht: weiter mit der nächsten Zeile, Sprünge über succ und fail,
        // KICKSTART startet zusätzlich einen Prozess. END beendet den Level. Höchstens 400 Zeilen.
        std::vector<int> Reach(const std::vector<Command>& commands, int from)
        {
            std::vector<int> out;
            std::vector<char> seen(commands.size(), 0);
            std::vector<int> todo = { from };
            auto index = [&](int line) {
                const auto it = std::lower_bound(commands.begin(), commands.end(), line,
                                                 [](const Command& c, int l) { return c.line < l; });
                return it != commands.end() && it->line == line ? (int)(it - commands.begin()) : -1;
            };
            while (!todo.empty() && out.size() < 400)
            {
                const int i = index(todo.back());
                todo.pop_back();
                if (i < 0 || seen[i])
                    continue;
                seen[i] = 1;
                out.push_back(i);
                const Command& c = commands[i];
                if (c.end)
                    continue;
                if (c.succ > 0)
                    todo.push_back(c.succ);
                else if (c.succ == 0 && i + 1 < (int)commands.size())
                    todo.push_back(commands[i + 1].line);
                if (c.fail > 0)
                    todo.push_back(c.fail);
                if (c.kickstart && c.obj > 0)
                    todo.push_back(c.obj);
            }
            return out;
        }

        // Missionsstarts eines Abschnitts. Gezählt werden nur Auslöser, die ab Levelstart scharf sind - die übrigen gehören
        // zu einer laufenden Mission. Was ein Auslöser startet, zeigen die Zeilen, die der Prozess ab seiner Zielzeile
        // erreicht. Telefongruppen (MPHONES) sind die Auftraggeber; ein Auto (CARTRIGGER) startet beim Einsteigen eine
        // Mission oder eine Kill Frenzy (in Vice City etwa der Panzer, der erst über KICKSTART zum Amoklauf springt), ein Ort
        // (TRIGGER) zählt nur als Kill Frenzy. Autos, die nur eine Meldung bringen und explodieren oder ihre Besitzer auf
        // einen hetzen (Vice City), erreichen kaum Zeilen. So ergeben sich in jedem Level so viele Kill Frenzies, wie das
        // Spiel geheime Missionen zählt.
        void FindStarts(Section& s, const std::map<int, Object>& objects, std::vector<Command>& commands)
        {
            std::sort(commands.begin(), commands.end(), [](const Command& l, const Command& r) { return l.line < r.line; });

            for (const auto& [line, o] : objects)
            {
                const bool phones = o.type == "MPHONES", car = o.type == "CARTRIGGER", place = o.type == "TRIGGER";
                if (!o.on || (!phones && !car && !place))
                    continue;
                const std::vector<int> reached = Reach(commands, o.a);
                bool frenzy = false;
                const Command* mphone = nullptr;
                for (int i : reached)
                {
                    frenzy |= commands[i].frenzy;
                    if (commands[i].mphone && !mphone)
                        mphone = &commands[i];
                }

                // Belohnung einer Kill Frenzy (Vice City): POWERUP_ON stellt eine Kiste auf, gleich danach wartet
                // IS_POWERUP_DONE mit einer Frist darauf ("COLLECT YOUR PRIZE ... YOU GOT 90 SECONDS")
                if (frenzy && !phones)
                    for (int i : reached)
                    {
                        const Command& on = commands[i];
                        if (!on.powerupOn)
                            continue;
                        const bool waited = std::any_of(reached.begin(), reached.end(), [&](int k) {
                            const Command& c = commands[k];
                            return c.powerupDone && c.obj == on.obj && c.data > 0 && c.line > on.line && c.line <= on.line + 20; });
                        const auto crate = objects.find(on.obj);
                        if (waited && crate != objects.end() && crate->second.type == "POWERUP")
                            s.prizes.push_back({ Kind::Prize, crate->second.x + 0.5f, crate->second.y + 0.5f });
                    }

                Start st;
                st.line = line;
                if (phones)
                {
                    st.kind = Kind::Phone;
                    st.trigger = kTrigPhones;
                    if (mphone && mphone->fail > 0 && mphone->fail <= 8)
                    {
                        st.phoneLine = mphone->obj;
                        st.phoneCount = mphone->fail;
                    }
                }
                else if (car)
                {
                    const auto c = objects.find(o.b);
                    if (c == objects.end() || !IsCarObject(c->second.type) || (!frenzy && reached.size() < 8))
                        continue;
                    st.kind = frenzy ? Kind::Frenzy : Kind::CarMission;
                    st.trigger = kTrigCar;
                    st.carLine = o.b;
                }
                else
                {
                    if (!frenzy)
                        continue;
                    st.kind = Kind::Frenzy;
                    st.trigger = kTrigPlace;
                }
                s.starts.push_back(st);
            }
        }

        std::vector<Section> ReadSections(const std::string& path)
        {
            std::vector<Section> sections;
            std::map<int, Object> objects;   // nur Auslöser und Fahrzeuge
            std::vector<Command> commands;
            auto finish = [&]() {
                if (!sections.empty())
                    FindStarts(sections.back(), objects, commands);
                objects.clear();
                commands.clear();
            };
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
                        finish();
                        sections.emplace_back();
                        sections.back().number = n;
                    }
                    continue;
                }
                if (sections.empty())
                    continue;
                int nr;
                Object o;
                if (!ParseObject(p, nr, o))
                {
                    Command c;
                    if (ParseCommand(p, c))
                        commands.push_back(c);
                    continue;
                }
                Section& s = sections.back();
                s.objectLines.push_back(nr);
                if (o.type == "TRIGGER" || o.type == "CARTRIGGER" || o.type == "MPHONES" || o.type == "DUM_MISSION_TRIG" ||
                    o.type == "PHONE_TOGG" || o.type == "POWERUP" || IsCarObject(o.type))
                    objects[nr] = o;
                const Kind kind = o.type == "SPRAY" ? Kind::Spray : o.type == "BOMBSHOP" ? Kind::BombShop
                                : o.type == "CRANE" ? Kind::Crane : Kind::Count;
                if (kind == Kind::Count)
                    continue;
                // Die meisten Objekte stehen in Blöcken, manche (z.B. Kräne) in Weltpixeln (64 je Block)
                const bool pixels = o.x > 255 || o.y > 255 || o.z > 7;
                Poi poi;
                poi.kind = kind;
                poi.x = pixels ? o.x / 64.0f : o.x + 0.5f;
                poi.y = pixels ? o.y / 64.0f : o.y + 0.5f;
                s.pois.push_back(poi);
            }
            finish();
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
            g_starts = best->starts;
            g_prizes = best->prizes;
            int counts[(int)Kind::Count] = {};
            for (const Start& s : g_starts)
                ++counts[(int)s.kind];
            LOG("Orte: Abschnitt [%d] aus %s (Übereinstimmung %.2f), %u Orte; Missionsstarts: %d Telefongruppen, "
                "%d Autos mit Auftrag, %d Kill Frenzies, %d Belohnungskisten", best->number, path.c_str(), bestScore,
                (unsigned)g_current.size(), counts[(int)Kind::Phone], counts[(int)Kind::CarMission], counts[(int)Kind::Frenzy],
                (int)g_prizes.size());
            return true;
        }

        // Telefongruppe: klingelt noch eines ihrer Telefone? Ein abgenommenes setzt das Spiel für immer auf Handle -1.
        bool PhonesLeft(const Start& s)
        {
            if (s.phoneLine < 0)
                return true;
            for (int k = 0; k < s.phoneCount; ++k)
            {
                int32_t type, handle;
                if (gta1::ScriptObjectHandle(s.phoneLine + k, type, handle) && handle != -1)
                    return true;
            }
            return false;
        }
    }

    const std::vector<Poi>& Current()
    {
        const unsigned level = gta1::LevelLoads();
        if (level != g_level && gta1::InGame() && GetTickCount() - g_retryTick > 2000)
        {
            g_retryTick = GetTickCount();
            g_current.clear();
            g_starts.clear();
            g_prizes.clear();
            if (Load())
                g_level = level;
            std::vector<heli::Spot> spots;
            heli::Spots(spots);
            for (const heli::Spot& s : spots)
                g_current.push_back({ Kind::Heli, s.x, s.y });
        }
        return g_current;
    }

    const std::vector<Poi>& Missions(int level)
    {
        Current();   // liest nach einem Levelstart das Skript ein
        const DWORD now = GetTickCount();
        if (level == g_missionLevel && now - g_missionTick < 250)
            return g_missions;
        g_missionLevel = level;
        g_missionTick = now;
        g_missions.clear();
        if (level <= 0 || g_level != gta1::LevelLoads())
            return g_missions;
        for (const Start& s : g_starts)
        {
            if (s.kind == Kind::Frenzy && level < 2)
                continue;
            int kind, state;
            float x, y;
            if (!gta1::ScriptTrigger(s.line, kind, state, x, y) || kind != s.trigger)
                continue;
            // scharf (1); eine Telefongruppe steht auf ausgelöst (3), solange man bei ihr ist
            if (state != 1 && !(s.kind == Kind::Phone && state == 3))
                continue;
            if (s.kind == Kind::Phone && !PhonesLeft(s))
                continue;
            if (s.carLine >= 0 && !gta1::ScriptCarPos(s.carLine, x, y))
                continue;
            g_missions.push_back({ s.kind, x, y });
        }
        return g_missions;
    }

    bool Prize(float& x, float& y)
    {
        Current();
        const DWORD now = GetTickCount();
        if (now - g_prizeTick >= 250)
        {
            g_prizeTick = now;
            g_prizeOn = false;
            if (g_level == gta1::LevelLoads())
                for (const Poi& p : g_prizes)
                    if (gta1::CrateAt((int)p.x, (int)p.y))
                    {
                        g_prize = p;
                        g_prizeOn = true;
                        break;
                    }
        }
        x = g_prize.x;
        y = g_prize.y;
        return g_prizeOn;
    }
}

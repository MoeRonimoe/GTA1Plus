#include "missiontimers.h"

#include "config.h"
#include "gta1.h"
#include "log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// Ein Countdown im Spiel besteht aus zwei Befehlen, die das Missionsskript getrennt hinschreibt: P_BRIEF_TIMED zeigt
// die Restzeit in Sekunden am Pager, die eigentliche Frist hat ein Befehl im Ablauf dahinter in seinem <data> als Zahl
// der Bilder (25 je Sekunde): SURVIVE in einem eigenen Prozess, der danach die Mission scheitern lässt, oder LOCATE,
// STEAL bzw. GOTO mit Zeitlimit. Verlängert wird nur, wo beides zusammenpasst - sonst liefe die Anzeige der Frist
// davon. Partner ist ein solcher Befehl mit <data> = Sekunden * 25, den der Ablauf ab dem Countdown erreicht (den
// <succ>- und <fail>-Verweisen und den KICKSTART-Zielen nach; in allen Skripten 1 bis 5 Schritte, nie mehrdeutig).
// Beim Amoklauf (KF_BRIEF_TIMED) ist die angezeigte Zeit selbst die Frist.
// Im Speicher stehen die Befehle eines Abschnitts lückenlos in der Reihenfolge der Datei; deren Zeilennummern
// (Ziele der Verweise) gibt es nur in der mission.ini.
namespace missiontimers
{
    namespace
    {
        enum : uint16_t { CmdSteal = 3, CmdSurvive = 4, CmdGoto = 21, CmdPBriefTimed = 68, CmdKickstart = 89,
                          CmdLocate = 109, CmdKfBriefTimed = 137 };
        constexpr int kTicksPerSecond = 25, kMaxSteps = 60;

        struct FileCommand
        {
            int line;
            int32_t obj, succ, fail;
        };
        using Section = std::vector<FileCommand>;

        // Befehlszeilen "<nr> NAME obj succ fail data score" je Abschnitt [n]; Objektzeilen haben Klammern
        std::vector<Section> ReadSections(const std::string& path)
        {
            std::vector<Section> sections;
            std::ifstream in(path, std::ios::binary);
            std::string text;
            while (std::getline(in, text))
            {
                const char* p = text.c_str();
                while (*p == ' ' || *p == '\t')
                    ++p;
                if (*p == '[')
                {
                    sections.emplace_back();
                    continue;
                }
                if (sections.empty() || *p < '0' || *p > '9' || strchr(p, '('))
                    continue;
                char* end;
                const long nr = strtol(p, &end, 10);
                p = end;
                while (*p == ' ')
                    ++p;
                if (*p < 'A' || *p > 'Z')
                    continue;
                while ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '_')
                    ++p;
                long f[5];
                int got = 0;
                for (; got < 5; ++got)
                {
                    f[got] = strtol(p, &end, 10);
                    if (end == p)
                        break;
                    p = end;
                }
                if (got == 5)
                    sections.back().push_back({ (int)nr, (int32_t)f[0], (int32_t)f[1], (int32_t)f[2] });
            }
            return sections;
        }

        // Abschnitt des laufenden Levels und je Befehlszeile der Datei ihr Platz im Speicher. Die Befehle stehen dort in
        // derselben Reihenfolge; obj und succ ändern sich im Spiel nie. Eine Zeile mit Zahlen zu viel (London 1961,
        // "7260 MOBILE_BRIEF 0 -1 -1 0 0 1440") liest das Spiel als zwei Einträge, der zweite mit Befehl 0xFFFF -
        // solche Einträge werden übersprungen.
        bool Align(const Section& s, const std::vector<gta1::ScriptCommand>& mem, std::vector<int>& place)
        {
            place.assign(s.size(), -1);
            int j = 0, misses = 0;
            for (int i = 0; i < (int)s.size(); ++i)
            {
                while (j < (int)mem.size() && mem[j].command == 0xFFFF)
                    ++j;
                if (j >= (int)mem.size())
                    return false;
                if (mem[j].obj == s[i].obj && mem[j].succ == s[i].succ)
                    place[i] = j;
                else if (++misses > 3)
                    return false;
                ++j;
            }
            return true;
        }

        std::vector<uint8_t> Build(int& countdowns, int& matched)
        {
            countdowns = matched = 0;
            const std::string path = gta1::MissionFile();
            if (path.empty())
                return {};
            std::vector<gta1::ScriptCommand> mem(5000);
            for (int i = 0; i < (int)mem.size(); ++i)
                if (!gta1::GetScriptCommand(i, mem[i]))
                    return {};
            const std::vector<Section> sections = ReadSections(path);
            const Section* s = nullptr;
            std::vector<int> place;
            for (const Section& candidate : sections)
                if (!candidate.empty() && Align(candidate, mem, place))
                {
                    s = &candidate;
                    break;
                }
            if (!s)
            {
                LOG("Mehr Zeit für Missionen: kein passender Abschnitt in %s", path.c_str());
                return {};
            }
            const int n = (int)s->size();
            std::unordered_map<int, int> indexOf;
            for (int i = 0; i < n; ++i)
                indexOf.emplace((*s)[i].line, i);

            std::vector<uint8_t> marks(mem.size(), gta1::TimerNone);
            std::vector<int> todo, depth, seen(n, -1);
            for (int b = 0; b < n; ++b)
            {
                if (place[b] < 0)
                    continue;
                const gta1::ScriptCommand& brief = mem[place[b]];
                if (brief.command != CmdPBriefTimed && brief.command != CmdKfBriefTimed)
                    continue;
                ++countdowns;
                const int32_t ticks = brief.data * kTicksPerSecond;
                int partners = 0;
                // Ablauf ab dem Countdown in die Breite (Zeilen der Datei); seen merkt sich den Countdown
                todo.assign(1, b);
                depth.assign(1, 0);
                seen[b] = b;
                for (size_t k = 0; k < todo.size(); ++k)
                {
                    const int i = todo[k], d = depth[k];
                    if (place[i] < 0)
                        continue;
                    const gta1::ScriptCommand& c = mem[place[i]];
                    if (i != b && ticks > 0 && c.data == ticks &&
                        (c.command == CmdSurvive || c.command == CmdLocate || c.command == CmdSteal || c.command == CmdGoto))
                    {
                        marks[place[i]] = gta1::TimerDeadline;
                        ++partners;
                        continue;
                    }
                    if (d >= kMaxSteps)
                        continue;
                    auto follow = [&](int j) {
                        if (j < 0 || j >= n || seen[j] == b)
                            return;
                        seen[j] = b;
                        todo.push_back(j);
                        depth.push_back(d + 1);
                    };
                    auto line = [&](int32_t nr) {
                        auto it = indexOf.find(nr);
                        return it != indexOf.end() ? it->second : -1;
                    };
                    if (c.succ == 0)
                        follow(i + 1);
                    else if (c.succ > 0)
                        follow(line(c.succ));
                    if (c.fail > 0)
                        follow(line(c.fail));
                    if (c.command == CmdKickstart && c.obj > 0)
                        follow(line(c.obj));
                }
                if (partners || brief.command == CmdKfBriefTimed)
                {
                    marks[place[b]] = gta1::TimerBrief;
                    ++matched;
                }
            }
            return marks;
        }
    }

    void Tick()
    {
        static unsigned s_level = ~0u;
        static std::vector<uint8_t> s_marks;
        const int seconds = g_cfg.extraMissionTime;
        if (seconds > 0 && gta1::InGame() && gta1::LevelLoads() != s_level)
        {
            s_level = gta1::LevelLoads();
            int countdowns, matched;
            s_marks = Build(countdowns, matched);
            if (!s_marks.empty())
                LOG("Mehr Zeit für Missionen (+%d s): %d von %d Countdowns mit Frist gefunden", seconds, matched, countdowns);
        }
        gta1::SetTimerExtension(seconds > 0 && s_level == gta1::LevelLoads() ? &s_marks : nullptr, seconds);
    }
}

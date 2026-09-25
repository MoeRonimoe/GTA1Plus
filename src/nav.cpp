#include "nav.h"
#include "citymap.h"
#include "config.h"
#include "gta1.h"
#include "log.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace nav
{
    namespace
    {
        constexpr int kMaxWaypoints = 8;
        constexpr float kTurnCost = 0.6f;   // Abbiegen kostet etwas, damit Routen nicht zickzacken
        constexpr float kArriveDist = 1.5f; // Blöcke

        std::vector<Point> g_waypoints;
        TargetKind g_kind = TargetKind::None;
        Point g_target = {};
        Point g_player = {};

        std::vector<Point> g_route;
        int  g_lastStart = -1, g_lastGoal = -1;
        bool g_lastOnFoot = false;
        DWORD g_lastSearch = 0;
        const CityMap* g_lastMap = nullptr;

        // A*-Puffer: Zustand = Zelle * 4 + Richtung (0 Nord, 1 Ost, 2 Süd, 3 West)
        std::vector<float> g_cost;
        std::vector<int> g_parent;
        std::vector<uint8_t> g_closed;

        const int kDx[4] = { 0, 1, 0, -1 };
        const int kDy[4] = { -1, 0, 1, 0 };

        // Kosten pro Block; < 0 = nicht passierbar. Straßen sind die Hauptroute, alles andere
        // ist deutlich teurer und wird nur als Verbindungsstück genutzt (Start/Ziel abseits der Straße, Lücken)
        float StepCost(const CityMap& map, int x, int y, bool onFoot)
        {
            if (map.IsRoad(x, y))
                return 1.0f;                            // Straße, auch Brücke, Tunnel, Überweg
            switch (map.Surface(x, y))
            {
            case 3: return onFoot ? 3.0f : 6.0f;        // Gehweg
            case 4: return onFoot ? 4.0f : 8.0f;        // Wiese
            case 0:                                      // Plätze u.ä. ohne eigenen Typ, aber mit Boden
                return map.LidLevel(x, y) >= 3 && map.LidLevel(x, y) <= 5 ? (onFoot ? 4.0f : 8.0f) : -1.0f;
            default: return -1.0f;                       // Wasser, Gebäude
            }
        }

        // Nächste passierbare Zelle in einem kleinen Umkreis (Spieler steht z.B. auf dem Gehweg neben der Straße)
        int Snap(const CityMap& map, float fx, float fy, bool onFoot)
        {
            const int cx = (int)fx, cy = (int)fy;
            int best = -1;
            float bestD = 1e9f;
            for (int r = 0; r <= 8 && best < 0; ++r)
                for (int y = cy - r; y <= cy + r; ++y)
                    for (int x = cx - r; x <= cx + r; ++x)
                    {
                        if (x < 0 || y < 0 || x > 255 || y > 255 || std::max(std::abs(x - cx), std::abs(y - cy)) != r)
                            continue;
                        if (StepCost(map, x, y, onFoot) < 0)
                            continue;
                        const float d = (x + 0.5f - fx) * (x + 0.5f - fx) + (y + 0.5f - fy) * (y + 0.5f - fy);
                        if (d < bestD)
                        {
                            bestD = d;
                            best = y * 256 + x;
                        }
                    }
            return best;
        }

        // Nächste Straßenzelle zum Punkt (für Ziele, die vom Straßennetz abgeschnitten sind)
        int NearestRoad(const CityMap& map, float fx, float fy)
        {
            const int cx = (int)fx, cy = (int)fy;
            for (int r = 0; r <= 32; ++r)
            {
                int best = -1;
                float bestD = 1e9f;
                for (int y = cy - r; y <= cy + r; ++y)
                    for (int x = cx - r; x <= cx + r; ++x)
                    {
                        if (x < 0 || y < 0 || x > 255 || y > 255 || std::max(std::abs(x - cx), std::abs(y - cy)) != r || !map.IsRoad(x, y))
                            continue;
                        const float d = (x + 0.5f - fx) * (x + 0.5f - fx) + (y + 0.5f - fy) * (y + 0.5f - fy);
                        if (d < bestD)
                        {
                            bestD = d;
                            best = y * 256 + x;
                        }
                    }
                if (best >= 0)
                    return best;
            }
            return -1;
        }

        bool Search(const CityMap& map, int start, int goal, bool onFoot, std::vector<int>& cells)
        {
            const size_t states = 65536 * 4;
            g_cost.assign(states, 1e30f);
            g_parent.assign(states, -1);
            g_closed.assign(states, 0);

            using Entry = std::pair<float, int>;
            std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
            const int gx = goal & 255, gy = goal >> 8;
            auto h = [&](int cell) { return (float)(std::abs((cell & 255) - gx) + std::abs((cell >> 8) - gy)); };

            for (int d = 0; d < 4; ++d)
            {
                g_cost[start * 4 + d] = 0;
                open.push({ h(start), start * 4 + d });
            }

            int found = -1;
            while (!open.empty())
            {
                const int s = open.top().second;
                open.pop();
                if (g_closed[s])
                    continue;
                g_closed[s] = 1;
                const int cell = s >> 2, dir = s & 3;
                if (cell == goal)
                {
                    found = s;
                    break;
                }
                const int x = cell & 255, y = cell >> 8;
                for (int d = 0; d < 4; ++d)
                {
                    if (d == (dir + 2) % 4)
                        continue; // nicht wenden
                    const int nx = x + kDx[d], ny = y + kDy[d];
                    if (nx < 0 || ny < 0 || nx > 255 || ny > 255)
                        continue;
                    const float step = StepCost(map, nx, ny, onFoot);
                    if (step < 0)
                        continue;
                    const int ns = (ny * 256 + nx) * 4 + d;
                    const float cost = g_cost[s] + step + (d != dir ? kTurnCost : 0.0f);
                    if (cost < g_cost[ns])
                    {
                        g_cost[ns] = cost;
                        g_parent[ns] = s;
                        open.push({ cost + h(ny * 256 + nx), ns });
                    }
                }
            }
            if (found < 0)
                return false;

            cells.clear();
            for (int s = found; s >= 0; s = g_parent[s])
                cells.push_back(s >> 2);
            std::reverse(cells.begin(), cells.end());
            return true;
        }

        void BuildRoute(const std::vector<int>& cells, const gta1::Player& p)
        {
            g_route.clear();
            g_route.push_back({ p.x, p.y });
            for (size_t i = 1; i + 1 < cells.size(); ++i)
            {
                const int dxa = (cells[i] & 255) - (cells[i - 1] & 255), dya = (cells[i] >> 8) - (cells[i - 1] >> 8);
                const int dxb = (cells[i + 1] & 255) - (cells[i] & 255), dyb = (cells[i + 1] >> 8) - (cells[i] >> 8);
                if (dxa != dxb || dya != dyb)
                    g_route.push_back({ (cells[i] & 255) + 0.5f, (cells[i] >> 8) + 0.5f });
            }
            if (!cells.empty())
                g_route.push_back({ (cells.back() & 255) + 0.5f, (cells.back() >> 8) + 0.5f });
            g_route.push_back(g_target);
        }

        float Dist(Point a, Point b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
    }

    void Reset()
    {
        g_route.clear();
        g_lastStart = g_lastGoal = -1;
        g_kind = TargetKind::None;
    }

    void Update(const CityMap& map, const gta1::Player* p)
    {
        // Ziel wählen: eigener Wegpunkt vor Missionspfeil vor rotem Pfeil
        TargetKind kind = TargetKind::None;
        Point target = {};
        float tx, ty;
        if (g_cfg.waypoints && !g_waypoints.empty())
        {
            kind = TargetKind::Waypoint;
            target = g_waypoints.back();
            if (p && Dist({ p->x, p->y }, target) < kArriveDist)
            {
                LOG("Wegpunkt erreicht");
                g_waypoints.pop_back();
                return Update(map, p);
            }
        }
        else if (gta1::GetArrowTarget(0, tx, ty))
        {
            kind = TargetKind::Mission;
            target = { tx, ty };
        }
        else if (gta1::GetArrowTarget(1, tx, ty))
        {
            kind = TargetKind::MissionRed;
            target = { tx, ty };
        }
        g_kind = kind;
        g_target = target;
        if (p)
            g_player = { p->x, p->y };

        if (kind == TargetKind::None || !p || !g_cfg.gps || !map.Loaded())
        {
            g_route.clear();
            g_lastStart = g_lastGoal = -1;
            return;
        }

        const bool onFoot = p->ctrlType != 0;
        const int start = Snap(map, p->x, p->y, onFoot);
        const int goal = Snap(map, target.x, target.y, onFoot);
        const DWORD now = GetTickCount();
        const bool changed = start != g_lastStart || goal != g_lastGoal || onFoot != g_lastOnFoot || &map != g_lastMap;
        if (!changed && now - g_lastSearch < 2000)
        {
            if (!g_route.empty())
                g_route.front() = { p->x, p->y };
            return;
        }
        g_lastStart = start;
        g_lastGoal = goal;
        g_lastOnFoot = onFoot;
        g_lastMap = &map;
        g_lastSearch = now;

        std::vector<int> cells;
        bool found = start >= 0 && goal >= 0 && Search(map, start, goal, onFoot, cells);
        if (!found && start >= 0)
        {
            // Ziel liegt z.B. im Wasser neben einer Bahnbrücke: bis zur nächsten erreichbaren Straße führen
            const int road = NearestRoad(map, target.x, target.y);
            found = road >= 0 && road != goal && Search(map, start, road, onFoot, cells);
        }
        if (!found)
        {
            g_route.clear();
            return;
        }
        BuildRoute(cells, *p);
    }

    TargetKind Kind() { return g_kind; }
    Point Target() { return g_target; }
    const std::vector<Point>& Route() { return g_route; }

    bool NextTurn(Point& out)
    {
        if (g_kind == TargetKind::None)
            return false;
        if (g_route.size() < 2)
        {
            out = g_target;
            return true;
        }
        // Schon kurz vor einer Abbiegung auf die nächste zeigen (zu Fuß 10 m, im Auto 20 m, 4 m je Block),
        // damit man dem Pfeil folgend nicht im Zickzack über die Straße fährt
        const float lookAhead = g_lastOnFoot ? 2.5f : 5.0f;
        const Point me = g_route.front();
        for (size_t i = 1; i < g_route.size(); ++i)
        {
            if (Dist(me, g_route[i]) > lookAhead)
            {
                out = g_route[i];
                return true;
            }
        }
        out = g_target;
        return true;
    }

    float RemainingBlocks()
    {
        if (g_kind == TargetKind::None)
            return 0.0f;
        if (g_route.size() < 2)
            return Dist(g_player, g_target);
        float len = 0;
        for (size_t i = 1; i < g_route.size(); ++i)
            len += Dist(g_route[i - 1], g_route[i]);
        return len;
    }

    const std::vector<Point>& Waypoints() { return g_waypoints; }

    void ToggleWaypoint(float x, float y)
    {
        for (size_t i = 0; i < g_waypoints.size(); ++i)
        {
            if (Dist(g_waypoints[i], { x, y }) < 1.2f)
            {
                g_waypoints.erase(g_waypoints.begin() + i);
                return;
            }
        }
        if ((int)g_waypoints.size() >= kMaxWaypoints)
            g_waypoints.erase(g_waypoints.begin());
        g_waypoints.push_back({ std::floor(x) + 0.5f, std::floor(y) + 0.5f });
    }

    void ClearWaypoints() { g_waypoints.clear(); }
}

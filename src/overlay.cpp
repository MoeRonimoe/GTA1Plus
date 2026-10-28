#include "overlay.h"
#include "canvas.h"
#include "citymap.h"
#include "config.h"
#include "controls.h"
#include "ddhook.h"
#include "fire.h"
#include "firebomb.h"
#include "gta1.h"
#include "hudscale.h"
#include "log.h"
#include "nav.h"
#include "options.h"
#include "pois.h"
#include "saves.h"
#include "briefings.h"
#include "text.h"
#include "tiltcam.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace overlay
{
    namespace
    {
        enum class Menu { Closed, Requested, Open };
        enum class Page { List, Map, Confirm, Options, Briefings, Progress };
        enum class Item { Resume, Map, Briefings, Progress, Save, Load, Options, Quit };
        enum class Confirm { Quit, Load, StartLoad };

        struct Entry
        {
            Item id;
            const wchar_t* label;
            bool enabled;
        };

        struct Fonts
        {
            int screenH = 0;
            int titlePx = 0, normalPx = 0, smallPx = 0;
            HFONT titleFont = nullptr, normalFont = nullptr, smallFont = nullptr;

            static HFONT Make(int px, int weight)
            {
                return CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
            }

            void Update(int h)
            {
                if (h == screenH)
                    return;
                for (HFONT f : { titleFont, normalFont, smallFont })
                    if (f)
                        DeleteObject(f);
                screenH = h;
                titlePx = std::max(14, h / 12);
                normalPx = std::max(11, h / 30);
                smallPx = std::max(10, h / 38);
                titleFont = Make(titlePx, FW_BOLD);
                normalFont = Make(normalPx, FW_SEMIBOLD);
                smallFont = Make(smallPx, FW_NORMAL);
            }
        };

        const float kZoom[] = { 1.5f, 2.f, 3.f, 4.f, 6.f, 8.f, 12.f, 16.f, 24.f, 32.f, 48.f, 64.f };
        constexpr int kZoomCount = sizeof(kZoom) / sizeof(kZoom[0]);
        constexpr int kDefaultZoom = 5;
        constexpr uint32_t kAccent = 0xFFC857;
        constexpr uint32_t kMissionColor = 0xFFC21A;  // Route/Ziel einer Mission
        constexpr uint32_t kRedColor = 0xE53935;      // roter Missionspfeil
        constexpr uint32_t kWaypointColor = 0xC64CF5; // eigene Wegpunkte
        constexpr uint32_t kGpsBlue = 0x2F7CF6;
        constexpr float kMetersPerBlock = 4.0f;
        // Herzen unter dem Punktestand: feste Pixelgröße, denn auch das HUD des Spiels wird nicht skaliert
        constexpr int kHeartCount = 5;
        constexpr float kHeartSize = 13.0f, kHeartStep = 15.0f, kHeartTop = 27.0f;

        Canvas  g_canvas;
        Fonts   g_fonts;
        CityMap g_map;
        unsigned g_mapGen = ~0u;

        Menu  g_menu = Menu::Closed;
        Page  g_page = Page::List;
        Page  g_requestPage = Page::List;
        bool  g_openedOnMap = false;
        int   g_sel = 0;
        bool  g_confirmYes = false;
        DWORD g_requestTick = 0;
        bool  g_pausedByUs = false;
        bool  g_dirty = true;
        bool  g_blink = false;
        bool  g_restore = false;
        DWORD g_blinkTick = 0, g_pumpTick = 0, g_presentTick = 0;

        std::vector<uint32_t> g_original;    // Spielbild beim Öffnen
        std::vector<uint32_t> g_background;  // weichgezeichnet und abgedunkelt
        int g_bgW = 0, g_bgH = 0;

        std::vector<uint8_t> g_cache;        // fertiges Menübild im Format der Surface
        int g_cacheW = 0, g_cacheH = 0;
        PixelFormat g_cacheFmt;

        gta1::Player g_player;
        bool  g_havePlayer = false;
        float g_viewX = 128, g_viewY = 128;
        int   g_zoom = kDefaultZoom;
        bool  g_follow = true;
        int   g_style = CityMap::Textured;
        bool  g_held[256] = {};

        bool  g_minimap = false;
        std::vector<uint32_t> g_miniBuf;
        float g_miniY = 0;                 // Lage der Minimap (oberer Rand), gleitet zu g_miniGoal
        int   g_miniGoal = 0, g_miniLayout = -1;
        DWORD g_miniTick = 0, g_miniCalmSince = 0;
        DWORD g_hintStart = 0;
        bool  g_warnedFormat = false;
        int   g_screenW = 0, g_screenH = 0;
        float g_panHold = 0;               // wie lange schon verschoben wird (für sanftes Beschleunigen)
        bool  g_padHeld[256] = {};         // vom Controller gehaltene Richtungen

        std::wstring g_shotRequest, g_shotFile; // Debug: <asi>.shot anlegen -> nächstes Bild als <asi>_shot.bmp
        DWORD g_shotCheck = 0;

        // Maus im Menü (Bildschirmkoordinaten des Spiels)
        bool  g_cursorShown = false, g_cursorMoved = false;
        float g_mouseX = 0, g_mouseY = 0;
        int   g_hover = -1;                  // Menüeintrag unter der Maus
        bool  g_dragging = false, g_dragMoved = false;
        float g_dragX = 0, g_dragY = 0, g_dragViewX = 0, g_dragViewY = 0;

        // Aufteilung des Menüs; RenderMenu und die Maus benutzen dieselben Rechtecke
        struct Layout
        {
            int m, top, areaH, listW, rowH, listY;
            int mapX, mapY, mapW, mapH;
            int boxX, boxY, boxW, boxH, btnW, btnH, btnY; // Sicherheitsabfrage

            int RowY(int i) const { return listY + i * (rowH + 2); }
            int BtnX(int i) const { return boxX + boxW - m - (2 - i) * (btnW + m / 2); }
            bool InMap(float x, float y) const { return x >= mapX && x < mapX + mapW && y >= mapY && y < mapY + mapH; }
        };

        Layout MakeLayout(int W, int H)
        {
            g_fonts.Update(H);
            Layout l;
            l.m = std::max(8, H / 28);
            const int headerH = g_fonts.titlePx + l.m / 2;
            const int footerH = g_fonts.smallPx + l.m;
            l.top = l.m + headerH;
            l.areaH = H - l.top - footerH - l.m / 2;
            // Bei hohen Auflösungen wächst die Schrift mit der Bildhöhe, die Spalte darf dann breiter werden
            l.listW = std::min(std::max(W * 24 / 100, 170), std::max(330, H * 55 / 100));
            l.rowH = g_fonts.normalPx * 2 + 4;
            l.listY = l.top + l.m / 2;
            l.mapX = l.m + l.listW + l.m / 2;
            l.mapY = l.top;
            l.mapW = W - l.mapX - l.m;
            l.mapH = l.areaH;
            l.boxW = std::min(std::max(W * 44 / 100, 320), W - 2 * l.m);
            l.boxH = l.m * 3 + g_fonts.normalPx * 4 + g_fonts.smallPx * 17 / 4;
            l.boxX = (W - l.boxW) / 2;
            l.boxY = (H - l.boxH) / 2;
            l.btnW = l.boxW / 4;
            l.btnH = g_fonts.normalPx * 2;
            l.btnY = l.boxY + l.boxH - l.btnH - l.m;
            return l;
        }

        void MaybeDumpFrame(const SurfaceView& v)
        {
            const DWORD now = GetTickCount();
            if (g_shotRequest.empty() || now - g_shotCheck < 250)
                return;
            g_shotCheck = now;
            if (GetFileAttributesW(g_shotRequest.c_str()) == INVALID_FILE_ATTRIBUTES)
                return;
            DeleteFileW(g_shotRequest.c_str());

            BITMAPFILEHEADER fh = {};
            BITMAPINFOHEADER ih = {};
            ih.biSize = sizeof(ih);
            ih.biWidth = v.width;
            ih.biHeight = -v.height;
            ih.biPlanes = 1;
            ih.biBitCount = 32;
            ih.biCompression = BI_RGB;
            const DWORD pixelBytes = (DWORD)v.width * v.height * 4;
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(fh) + sizeof(ih);
            fh.bfSize = fh.bfOffBits + pixelBytes;

            HANDLE f = CreateFileW(g_shotFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f == INVALID_HANDLE_VALUE)
                return;
            DWORD written;
            WriteFile(f, &fh, sizeof(fh), &written, nullptr);
            WriteFile(f, &ih, sizeof(ih), &written, nullptr);
            std::vector<uint32_t> row(v.width);
            for (int y = 0; y < v.height; ++y)
            {
                ConvertFromSurface(v.fmt, v.Row(y), row.data(), v.width);
                WriteFile(f, row.data(), v.width * 4, &written, nullptr);
            }
            CloseHandle(f);
            LOG("Bild gespeichert: %ls", g_shotFile.c_str());
        }

        std::vector<Entry> Entries()
        {
            const bool single = gta1::Supported() && gta1::IsSinglePlayer();
            return {
                { Item::Resume, text::Get(text::Resume), true },
                { Item::Map, text::Get(g_cfg.waypoints ? text::MapAndWaypoints : text::Map), true },
                { Item::Briefings, text::Get(text::MenuBriefings), true },
                { Item::Progress, text::Get(text::MenuProgress), true },
                { Item::Save, text::Get(text::MenuSave), single && saves::Available() },
                { Item::Load, text::Get(text::MenuLoad), single && saves::HasSave() },
                { Item::Options, text::Get(text::MenuOptions), true },
                { Item::Quit, text::Get(text::QuitLevel), single },
            };
        }

        // Kurze Rückmeldung im Menü (z.B. "Spielstand gespeichert")
        std::wstring g_status;
        DWORD g_statusTick = 0;
        bool g_statusError = false;

        void ShowStatus(const std::wstring& msg, bool error = false)
        {
            g_status = msg;
            g_statusTick = GetTickCount();
            g_statusError = error;
            g_dirty = true;
        }

        Confirm g_confirmKind = Confirm::Quit;
        std::wstring g_slotInfo;             // Datum und Punkte des Spielstands für die Ladeabfrage
        int g_optSel = 1, g_optScroll = 0;   // Optionen: gewählter Eintrag, erste sichtbare Zeile
        int g_optHover = -1;
        int g_briefScroll = 0;               // Briefings: erste sichtbare Textzeile
        int g_briefMaxScroll = 0;

        // Meldung im Spiel (z.B. "Automatisch gespeichert"), unten in der Bildmitte
        std::wstring g_toast;
        DWORD g_toastTick = 0;
        bool g_toastError = false;
        constexpr DWORD kToastTime = 2600;

        void ShowToast(const std::wstring& msg, bool error = false)
        {
            g_toast = msg;
            g_toastTick = GetTickCount();
            g_toastError = error;
        }

        // Optionen und Briefings stehen rechts an der Stelle der Karte
        struct Panel
        {
            int x, y, w, h;
            int titleH, rowH, rowsY, rows;   // Optionen: Zeilenraster
            int footY;                       // Hinweiszeile unten im Feld

            int ValueW() const { return std::max(w * 3 / 10, 90); }
            int RowAt(float my) const { return my >= rowsY && my < rowsY + rows * rowH ? (int)(my - rowsY) / rowH : -1; }
        };

        Panel MakePanel(const Layout& L)
        {
            Panel p;
            p.x = L.mapX;
            p.y = L.mapY;
            p.w = L.mapW;
            p.h = L.mapH;
            p.titleH = g_fonts.normalPx * 2 + L.m / 2;
            p.rowH = g_fonts.smallPx + std::max(6, g_fonts.smallPx * 2 / 3);
            p.rowsY = p.y + p.titleH;
            p.footY = p.y + p.h - g_fonts.smallPx - L.m / 2;
            p.rows = std::max(1, (p.footY - L.m / 4 - p.rowsY) / p.rowH);
            return p;
        }

        // Gewählte Option sichtbar halten, die Gruppenüberschrift darüber möglichst mit
        void ClampOptionScroll(int rows)
        {
            const int n = options::Count();
            if (g_optSel - 1 >= 0 && options::IsHeader(g_optSel - 1) && g_optSel - 1 < g_optScroll)
                g_optScroll = g_optSel - 1;
            if (g_optSel < g_optScroll)
                g_optScroll = g_optSel;
            if (g_optSel >= g_optScroll + rows)
                g_optScroll = g_optSel - rows + 1;
            g_optScroll = std::max(0, std::min(g_optScroll, n - rows));
        }

        // Text auf Zeilen der Breite width umbrechen
        std::vector<std::wstring> Wrap(Canvas& c, const std::wstring& text, HFONT font, int width)
        {
            std::vector<std::wstring> lines;
            std::wstring line;
            size_t i = 0;
            while (i < text.size())
            {
                while (i < text.size() && text[i] == L' ')
                    ++i;
                size_t j = i;
                while (j < text.size() && text[j] != L' ')
                    ++j;
                if (j == i)
                    break;
                const std::wstring word = text.substr(i, j - i);
                const std::wstring test = line.empty() ? word : line + L' ' + word;
                if (!line.empty() && c.TextWidth(test.c_str(), font) > width)
                {
                    lines.push_back(line);
                    line = word;
                }
                else
                    line = test;
                i = j;
            }
            if (!line.empty())
                lines.push_back(line);
            return lines;
        }

        void KeyName(int vk, wchar_t* out, size_t n)
        {
            if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
                swprintf_s(out, n, L"%c", (wchar_t)vk);
            else if (vk >= VK_F1 && vk <= VK_F24)
                swprintf_s(out, n, L"F%d", vk - VK_F1 + 1);
            else if (vk == VK_ESCAPE)
                wcscpy_s(out, n, L"Esc");
            else if (vk == VK_TAB)
                wcscpy_s(out, n, L"Tab");
            else
                swprintf_s(out, n, L"0x%02X", vk);
        }

        void EnsureMap()
        {
            const unsigned gen = gta1::MapGeneration();
            if (gen == g_mapGen)
                return;
            g_mapGen = gen;
            const std::string path = gta1::MapFile();
            if (!path.empty())
                g_map.Load(path);
            nav::Reset();
            if (!g_map.HasTextures())
                g_style = CityMap::Schematic;
        }

        // Winkel aus dem Spiel -> Bildschirmwinkel (0 = oben, positiv = im Uhrzeigersinn).
        // Im Spiel ist 0 = Süden und der Winkel wächst gegen den Uhrzeigersinn (1024 = 360°),
        // gemessen über die Laufrichtung des Spielers.
        float HeadingRadians(int angle)
        {
            const float a = ((angle + 512 + g_cfg.headingOffset) & 1023) * (6.2831853f / 1024.0f);
            return g_cfg.headingCcw ? -a : a;
        }

        void DrawPlayerMarker(Canvas& c, float x, float y, float r, const gta1::Player& p, bool pulse, int alpha = 255)
        {
            if (pulse)
                c.Ring(x, y, r * 2.2f, std::max(1.5f, r * 0.35f), 0xFF6B5E, alpha / 2);
            if (g_cfg.showHeading)
            {
                const float a = HeadingRadians(p.angle);
                const float dx = std::sin(a), dy = -std::cos(a);
                const float px = -dy, py = dx;
                const float tipX = x + dx * r * 2.4f, tipY = y + dy * r * 2.4f;
                const float bx = x + dx * r * 0.6f, by = y + dy * r * 0.6f;
                c.Triangle(tipX, tipY, bx + px * r * 1.05f, by + py * r * 1.05f, bx - px * r * 1.05f, by - py * r * 1.05f, 0xFFFFFF, alpha);
            }
            c.Circle(x, y, r + std::max(1.5f, r * 0.3f), 0xFFFFFF, alpha);
            c.Circle(x, y, r, 0xE53935, alpha);
        }

        void DrawDiamond(Canvas& c, float x, float y, float r, uint32_t color)
        {
            const float o = r + std::max(1.5f, r * 0.3f);
            c.Triangle(x, y - o, x + o, y, x - o, y, 0x14181D);
            c.Triangle(x, y + o, x + o, y, x - o, y, 0x14181D);
            c.Triangle(x, y - r, x + r, y, x - r, y, color);
            c.Triangle(x, y + r, x + r, y, x - r, y, color);
        }

        void DrawPin(Canvas& c, float x, float y, float r, bool active, int number)
        {
            if (active)
                c.Ring(x, y, r * 1.9f, std::max(1.5f, r * 0.3f), kWaypointColor, 200);
            c.Circle(x, y, r + std::max(1.5f, r * 0.3f), 0xFFFFFF);
            c.Circle(x, y, r, kWaypointColor);
            if (number > 0 && r >= g_fonts.smallPx * 0.45f)
            {
                wchar_t n[4];
                swprintf_s(n, L"%d", number);
                c.Text((int)x, (int)(y - g_fonts.smallPx * 0.62f), n, g_fonts.smallFont, 0xFFFFFF, TA_CENTER | TA_TOP, false);
            }
        }

        // Orte: Lackiererei grün mit Sprühdose, Bombenladen rot mit Bombe, Autokran gelb mit Kran
        constexpr uint32_t kPoiColors[] = { 0x1F9D6B, 0xC62828, 0xE0A21A };

        void FillRect(Canvas& c, float x0, float y0, float x1, float y1, uint32_t color)
        {
            const float pts[8] = { x0, y0, x1, y0, x1, y1, x0, y1 };
            c.Polygon(pts, 4, color);
        }

        // Plakette mit Umriss und Zeichen; r = Radius, das Zeichen ist auf ein Raster von 20 Einheiten ausgelegt
        void DrawPoiIcon(Canvas& c, float x, float y, float r, pois::Kind kind)
        {
            c.Circle(x, y, r + std::max(1.5f, r * 0.16f), 0x101418);
            c.Circle(x, y, r, kPoiColors[(int)kind]);
            const float u = r / 10.0f;
            switch (kind)
            {
            case pois::Kind::Spray:
                FillRect(c, x - 4.5f * u, y - 2.0f * u, x + 1.5f * u, y + 7.0f * u, 0xFFFFFF);   // Dose
                FillRect(c, x - 3.5f * u, y - 4.5f * u, x + 0.5f * u, y - 2.0f * u, 0xC9CFD6);   // Kappe
                FillRect(c, x + 0.5f * u, y - 4.2f * u, x + 2.5f * u, y - 3.0f * u, 0xC9CFD6);   // Düse
                for (int i = 0; i < 3; ++i)                                                      // Nebel
                    c.Circle(x + (4.5f + (i == 1) * 1.3f) * u, y + (-6.0f + i * 2.3f) * u, 0.9f * u + 0.4f, 0xFFFFFF);
                break;
            case pois::Kind::BombShop:
                c.Circle(x - 1.0f * u, y + 1.5f * u, 5.0f * u, 0x14181D);                           // Kugel
                c.Circle(x - 2.8f * u, y - 0.3f * u, 1.3f * u, 0x6C737C);                           // Glanz
                c.Line(x + 2.3f * u, y - 2.2f * u, x + 4.8f * u, y - 5.2f * u, std::max(1.0f, 1.3f * u), 0xE8D9B0); // Lunte
                c.Circle(x + 5.3f * u, y - 5.8f * u, 1.8f * u, 0xFFC21A);                           // Funke
                c.Circle(x + 5.3f * u, y - 5.8f * u, 0.7f * u, 0xFFFFFF);
                break;
            case pois::Kind::Crane:
            {
                const float w = std::max(1.0f, 1.4f * u);
                const uint32_t k = 0x14181D;
                c.Line(x - 3.0f * u, y + 6.5f * u, x - 3.0f * u, y - 6.0f * u, w, k);            // Mast
                c.Line(x - 5.5f * u, y - 6.0f * u, x + 6.0f * u, y - 6.0f * u, w, k);            // Ausleger
                c.Line(x - 3.0f * u, y - 3.0f * u, x + 0.5f * u, y - 6.0f * u, w * 0.7f, k);     // Strebe
                c.Line(x + 4.5f * u, y - 6.0f * u, x + 4.5f * u, y + 0.8f * u, w * 0.7f, k);     // Seil
                c.Ring(x + 4.5f * u, y + 2.2f * u, 1.4f * u, w * 0.7f, k);                       // Haken
                FillRect(c, x - 6.0f * u, y + 5.8f * u, x, y + 7.6f * u, k);                     // Fuß
                break;
            }
            default:
                break;
            }
        }

        // Route, Missionsziele und Wegpunkte für einen Kartenausschnitt; (ox, oy) = Bildschirmmitte des Ausschnitts
        void DrawNav(Canvas& c, float ox, float oy, float cx, float cy, float ppb, float markerR, bool numbers)
        {
            auto X = [&](float wx) { return ox + (wx - cx) * ppb; };
            auto Y = [&](float wy) { return oy + (wy - cy) * ppb; };

            // Orte zuunterst, darüber Route und Ziele
            if (g_cfg.pois)
                for (const pois::Poi& p : pois::Current())
                    DrawPoiIcon(c, X(p.x), Y(p.y), markerR * (numbers ? 1.75f : 1.35f), p.kind); // numbers: große Karte

            const auto& route = nav::Route();
            if (g_cfg.gps && route.size() >= 2)
            {
                const uint32_t color = nav::Kind() == nav::TargetKind::Waypoint ? kWaypointColor : kMissionColor;
                const float w = std::min(std::max(markerR * 0.9f, 3.0f), 12.0f);
                for (int pass = 0; pass < 2; ++pass)
                {
                    const float lw = pass == 0 ? w + 3.0f : w;
                    const uint32_t lc = pass == 0 ? 0x14181D : color;
                    for (size_t i = 1; i < route.size(); ++i)
                        c.Line(X(route[i - 1].x), Y(route[i - 1].y), X(route[i].x), Y(route[i].y), lw, lc);
                    for (size_t i = 1; i + 1 < route.size(); ++i)
                        c.Circle(X(route[i].x), Y(route[i].y), lw * 0.5f, lc);
                }
            }

            float tx, ty;
            if (gta1::GetArrowTarget(0, tx, ty))
                DrawDiamond(c, X(tx), Y(ty), markerR * 1.1f, kMissionColor);
            if (gta1::GetArrowTarget(1, tx, ty))
                DrawDiamond(c, X(tx), Y(ty), markerR * 1.1f, kRedColor);

            if (g_cfg.waypoints)
            {
                const auto& wps = nav::Waypoints();
                for (size_t i = 0; i < wps.size(); ++i)
                    DrawPin(c, X(wps[i].x), Y(wps[i].y), markerR, i + 1 == wps.size(), numbers ? (int)i + 1 : 0);
            }
        }

        // Pfeil nach oben in lokalen Koordinaten, gedreht um a (0 = oben, im Uhrzeigersinn)
        void DrawArrowShape(Canvas& c, float cx, float cy, float s, float a, uint32_t color, int alpha)
        {
            static const float pts[7][2] = {
                { 0.0f, -1.0f }, { 0.62f, -0.1f }, { 0.24f, -0.1f }, { 0.24f, 0.85f }, { -0.24f, 0.85f }, { -0.24f, -0.1f }, { -0.62f, -0.1f },
            };
            const float ca = std::cos(a), sa = std::sin(a);
            float p[7][2];
            for (int i = 0; i < 7; ++i)
            {
                p[i][0] = cx + (pts[i][0] * ca - pts[i][1] * sa) * s;
                p[i][1] = cy + (pts[i][0] * sa + pts[i][1] * ca) * s;
            }
            c.Polygon(&p[0][0], 7, color, alpha);
        }

        void BoxBlur(std::vector<uint32_t>& img, int w, int h, int radius)
        {
            std::vector<uint32_t> tmp(img.size());
            const int span = 2 * radius + 1;
            auto pass = [&](const uint32_t* src, uint32_t* dst, int len, int count, int stride, int step) {
                for (int k = 0; k < count; ++k)
                {
                    const uint32_t* s = src + k * stride;
                    uint32_t* d = dst + k * stride;
                    int r = 0, g = 0, b = 0;
                    for (int i = -radius; i <= radius; ++i)
                    {
                        const uint32_t c = s[std::min(std::max(i, 0), len - 1) * step];
                        r += (c >> 16) & 0xFF; g += (c >> 8) & 0xFF; b += c & 0xFF;
                    }
                    for (int i = 0; i < len; ++i)
                    {
                        d[i * step] = ((r / span) << 16) | ((g / span) << 8) | (b / span);
                        const uint32_t out = s[std::max(i - radius, 0) * step];
                        const uint32_t in = s[std::min(i + radius + 1, len - 1) * step];
                        r += ((in >> 16) & 0xFF) - ((out >> 16) & 0xFF);
                        g += ((in >> 8) & 0xFF) - ((out >> 8) & 0xFF);
                        b += (in & 0xFF) - (out & 0xFF);
                    }
                }
            };
            pass(img.data(), tmp.data(), w, h, w, 1); // horizontal
            pass(tmp.data(), img.data(), h, w, 1, w); // vertikal
        }

        // Lesen aus der Surface messen: liegt das Bild im Grafikspeicher (z.B. Fenstermodus des ASI-Loaders der
        // Classics-Ausgabe), ist Lesen dort einige hundert Mal langsamer als Schreiben. Dann holt das Overlay
        // möglichst nichts mehr aus dem Bild zurück.
        bool g_slowRead = false;
        int g_readW = 0, g_readH = 0;
        bool g_readBuffered = false;

        void MeasureReadback(const SurfaceView& v)
        {
            if (v.width == g_readW && v.height == g_readH && v.buffered == g_readBuffered)
                return;
            g_readW = v.width;
            g_readH = v.height;
            g_readBuffered = v.buffered;
            static uint32_t probe[1024];
            const int n = std::min(1024, v.width);
            LARGE_INTEGER f, t0, t1;
            QueryPerformanceFrequency(&f);
            QueryPerformanceCounter(&t0);
            for (int k = 0; k < 4; ++k)
                ConvertFromSurface(v.fmt, v.Row(v.height / 2 + k), probe, n);
            QueryPerformanceCounter(&t1);
            const double us = (t1.QuadPart - t0.QuadPart) * 1e6 / f.QuadPart;
            g_slowRead = us > 150.0;   // normaler Arbeitsspeicher braucht dafür wenige Mikrosekunden
            Canvas::SetSlowReadback(g_slowRead);
            LOG("Bildspeicher %dx%d%s: Lesen von %d Pixeln dauert %.0f µs%s", v.width, v.height,
                v.buffered ? " (Zwischenpuffer)" : "", 4 * n, us, g_slowRead ? " - langsam, das Overlay liest nichts mehr zurück" : "");
        }

        void CaptureBackground(const SurfaceView& v)
        {
            const int w = v.width, h = v.height;
            g_original.resize((size_t)w * h);
            // Bei langsamem Lesen nur ein Raster holen (höchstens etwa 150000 Pixel) und vergrößern; das Bild
            // wird ohnehin weichgezeichnet
            int step = 1;
            while (g_slowRead && (w / step) * (h / step) > 150000)
                ++step;
            if (step == 1)
            {
                for (int y = 0; y < h; ++y)
                    ConvertFromSurface(v.fmt, v.Row(y), &g_original[(size_t)y * w], w);
            }
            else
            {
                const int bpp = v.fmt.BytesPerPixel();
                const int sw = (w + step - 1) / step, sh = (h + step - 1) / step;
                std::vector<uint32_t> grid((size_t)sw * sh);
                for (int j = 0; j < sh; ++j)
                {
                    const uint8_t* row = v.Row(std::min(h - 1, j * step + step / 2));
                    for (int i = 0; i < sw; ++i)
                        ConvertFromSurface(v.fmt, row + std::min(w - 1, i * step + step / 2) * bpp, &grid[(size_t)j * sw + i], 1);
                }
                for (int y = 0; y < h; ++y)
                {
                    const uint32_t* from = &grid[(size_t)(y / step) * sw];
                    uint32_t* to = &g_original[(size_t)y * w];
                    for (int x = 0; x < w; ++x)
                        to[x] = from[x / step];
                }
            }

            g_background = g_original;
            BoxBlur(g_background, w, h, std::max(2, h / 160) + (step > 1 ? step : 0));
            for (uint32_t& c : g_background)
            {
                const uint32_t r = ((c >> 16) & 0xFF) * 90 >> 8, g = ((c >> 8) & 0xFF) * 90 >> 8, b = (c & 0xFF) * 90 >> 8;
                c = ((r + 8) << 16) | ((g + 11) << 8) | (b + 16);
            }
            g_bgW = w;
            g_bgH = h;
        }

        // So lange ohne Seitenwechsel gilt das Spiel als "präsentiert nicht" (eigene Pause mit F6). Großzügig, denn
        // selbst durch die Seiten zu blättern, während das Spiel nur langsam ist, zeigt halbfertige Bilder.
        constexpr DWORD kNoFlipTime = 500;

        void UpdateNav()
        {
            if (!g_cfg.gps && !g_cfg.waypoints)
                return;
            EnsureMap();
            gta1::Player p;
            const bool have = gta1::GetPlayer(p);
            nav::Update(g_map, have ? &p : (g_havePlayer && g_menu == Menu::Open ? &g_player : nullptr));
        }

        void OpenNow(SurfaceView& v)
        {
            if (!v.fmt.Supported())
            {
                g_menu = Menu::Closed;
                return;
            }
            // Position zuerst: das Hintergrundbild braucht bei hohen Auflösungen so lange, dass das Spiel danach
            // als "nicht im Level" gälte (kein HUD in den letzten 500 ms)
            gta1::Player p;
            g_havePlayer = gta1::GetPlayer(p);
            CaptureBackground(v);
            EnsureMap();

            if (g_havePlayer)
            {
                g_player = p;
                g_viewX = p.x;
                g_viewY = p.y;
            }
            g_follow = true;
            g_zoom = kDefaultZoom;
            g_page = g_requestPage;
            g_openedOnMap = g_page == Page::Map;
            g_sel = 0;
            g_hover = -1;
            g_status.clear();
            g_dragging = false;
            g_cursorShown = false;

            if (g_cfg.pauseGame && gta1::IsSinglePlayer() && gta1::RunState() == 1)
            {
                gta1::Pause();
                g_pausedByUs = true;
            }
            g_menu = Menu::Open;
            g_dirty = true;
            g_pumpTick = g_blinkTick = GetTickCount();
            LOG("Menü geöffnet (%dx%d, Spieler %s, pausiert %d)", v.width, v.height, g_havePlayer ? "gefunden" : "unbekannt", g_pausedByUs);
        }

        void CloseMenu()
        {
            if (g_menu == Menu::Closed)
                return;
            const bool wasOpen = g_menu == Menu::Open;
            if (g_pausedByUs)
            {
                gta1::Resume();
                g_pausedByUs = false;
            }
            g_menu = Menu::Closed;
            memset(g_held, 0, sizeof(g_held));
            LOG("Menü geschlossen");

            // Präsentiert das Spiel gerade nicht selbst (z.B. F6-Pause), das ursprüngliche Bild zurückholen
            if (wasOpen && GetTickCount() - ddhook::LastGameFlipTick() > kNoFlipTime)
            {
                g_restore = true;
                ddhook::PresentOverlay();
                g_restore = false;
            }
        }

        void QuitLevel()
        {
            LOG("Level beenden über das Menü");
            CloseMenu();
            gta1::QuitLevel();
        }

        void LoadGame()
        {
            if (const char* error = saves::Load())
            {
                LOG("Laden fehlgeschlagen: %s", error);
                g_page = Page::List;
                ShowStatus(text::Get(text::LoadFailed), true);
                return;
            }
            CloseMenu();
            ShowToast(text::Get(text::LoadDone));
        }

        void ConfirmYes()
        {
            if (g_confirmKind == Confirm::Quit)
                QuitLevel();
            else
                LoadGame();
        }

        void ConfirmNo()
        {
            // Die Abfrage beim Levelstart hat das Menü selbst geöffnet
            if (g_confirmKind == Confirm::StartLoad)
                CloseMenu();
            else
                g_page = Page::List;
        }

        void OpenConfirm(Confirm kind)
        {
            g_confirmKind = kind;
            g_confirmYes = kind == Confirm::StartLoad;
            if (kind != Confirm::Quit)
                saves::SlotInfo(g_slotInfo);
            g_page = Page::Confirm;
        }

        void FirstOption()
        {
            g_optSel = 0;
            while (g_optSel < options::Count() - 1 && options::IsHeader(g_optSel))
                ++g_optSel;
            g_optScroll = 0;
            g_optHover = -1;
        }

        void Activate(Item item)
        {
            switch (item)
            {
            case Item::Resume:
                CloseMenu();
                break;
            case Item::Map:
                g_page = Page::Map;
                g_openedOnMap = false;
                break;
            case Item::Briefings:
                g_page = Page::Briefings;
                g_briefScroll = 0;
                break;
            case Item::Progress:
                g_page = Page::Progress;
                break;
            case Item::Save:
                if (!saves::LevelReady())
                    ShowStatus(text::Get(text::LevelStarting), true);
                else if (!gta1::IsPlayerOnFoot())
                    ShowStatus(text::Get(text::OnFootOnly), true);
                else if (!saves::CanSave())
                    ShowStatus(text::Get(text::SaveBlocked), true);
                else if (saves::Save(false))
                    ShowStatus(text::Get(text::SaveDone));
                else
                    ShowStatus(text::Get(text::SaveFailed), true);
                break;
            case Item::Load:
                if (!saves::LevelReady())
                    ShowStatus(text::Get(text::LevelStarting), true);
                else if (!gta1::IsPlayerOnFoot())
                    ShowStatus(text::Get(text::OnFootOnly), true);
                else if (saves::HasSave())
                    OpenConfirm(Confirm::Load);
                else
                    ShowStatus(text::Get(text::NoSave), true);
                break;
            case Item::Options:
                g_page = Page::Options;
                FirstOption();
                break;
            case Item::Quit:
                OpenConfirm(Confirm::Quit);
                break;
            }
        }

        // Option ändern und, was ohne Neustart geht, gleich übernehmen
        void ChangeOption(int i, int dir)
        {
            const bool minimap = g_cfg.minimap;
            const int style = g_cfg.mapStyle;
            if (!options::Change(i, dir))
                return;
            if (g_cfg.minimap != minimap)
                g_minimap = g_cfg.minimap;
            if (g_cfg.mapStyle != style)
                g_style = g_map.HasTextures() ? g_cfg.mapStyle : CityMap::Schematic;
            int vehicleHeight, vehicleZoom, footHeight, footZoom;
            VehicleCameraView(g_cfg.vehicleView, vehicleHeight, vehicleZoom);
            FootCameraView(g_cfg.footView, footHeight, footZoom);
            gta1::SetViewOptions(g_cfg.trafficRange / 100.0f, vehicleHeight, vehicleZoom, footHeight, footZoom);
            gta1::SetDamage(g_cfg.pistolDamage, g_cfg.machineGunDamage, g_cfg.punchDamage);
            if (options::NeedsRestart(i))
                ShowStatus(text::Get(text::OptRestart));
        }

        void HandleOptionsKey(int vk, bool repeat)
        {
            const int n = options::Count();
            switch (vk)
            {
            case VK_UP:
            case VK_DOWN:
            {
                const int step = vk == VK_UP ? -1 : 1;
                int i = g_optSel + step;
                while (i >= 0 && i < n && options::IsHeader(i))
                    i += step;
                if (i >= 0 && i < n)
                    g_optSel = i;
                else if (vk == VK_UP)
                    g_optScroll = 0; // Überschrift ganz oben zeigen
                break;
            }
            case VK_PRIOR:
            case VK_NEXT:
                for (int k = 0; k < 8; ++k)
                    HandleOptionsKey(vk == VK_PRIOR ? VK_UP : VK_DOWN, false);
                break;
            case VK_LEFT:
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
                ChangeOption(g_optSel, -1);
                break;
            case VK_RIGHT:
            case VK_ADD:
            case VK_OEM_PLUS:
                ChangeOption(g_optSel, 1);
                break;
            case VK_RETURN:
            case VK_SPACE:
                if (!repeat)
                    ChangeOption(g_optSel, 1);
                break;
            case VK_ESCAPE:
            case VK_BACK:
                if (!repeat)
                    g_page = Page::List;
                break;
            default:
                break;
            }
        }

        void HandleBriefingsKey(int vk, bool repeat)
        {
            switch (vk)
            {
            case VK_UP:
                g_briefScroll = std::max(0, g_briefScroll - 1);
                break;
            case VK_DOWN:
                g_briefScroll = std::min(g_briefMaxScroll, g_briefScroll + 1);
                break;
            case VK_PRIOR:
            case VK_SUBTRACT:
                g_briefScroll = std::max(0, g_briefScroll - 8);
                break;
            case VK_NEXT:
            case VK_ADD:
                g_briefScroll = std::min(g_briefMaxScroll, g_briefScroll + 8);
                break;
            case VK_HOME:
                g_briefScroll = 0;
                break;
            case VK_ESCAPE:
            case VK_BACK:
            case VK_RETURN:
                if (!repeat)
                    g_page = Page::List;
                break;
            default:
                break;
            }
        }

        void HandleListKey(int vk, bool repeat)
        {
            const std::vector<Entry> entries = Entries();
            const int n = (int)entries.size();
            switch (vk)
            {
            case VK_UP:
            case VK_DOWN:
            {
                const int step = vk == VK_UP ? n - 1 : 1;
                for (int i = 0; i < n; ++i)
                {
                    g_sel = (g_sel + step) % n;
                    if (entries[g_sel].enabled)
                        break;
                }
                break;
            }
            case VK_RETURN:
            case VK_SPACE:
                if (!repeat && entries[g_sel].enabled)
                    Activate(entries[g_sel].id);
                break;
            case VK_ESCAPE:
            case VK_BACK:
                if (!repeat)
                    CloseMenu();
                break;
            default:
                if (vk == g_cfg.mapKey && !repeat)
                    Activate(Item::Map);
                break;
            }
        }

        void HandleMapKey(int vk, bool repeat)
        {
            switch (vk)
            {
            case VK_LEFT:
            case VK_RIGHT:
            case VK_UP:
            case VK_DOWN:
                g_held[vk] = true;
                g_follow = false;
                break;
            case VK_ADD:
            case VK_OEM_PLUS:
            case VK_PRIOR:
                g_zoom = std::min(g_zoom + 1, kZoomCount - 1);
                break;
            case VK_SUBTRACT:
            case VK_OEM_MINUS:
            case VK_NEXT:
                g_zoom = std::max(g_zoom - 1, 0);
                break;
            case VK_SPACE:
            case VK_HOME:
                g_follow = true;
                break;
            case VK_TAB:
                if (!repeat && g_map.HasTextures())
                    g_style ^= 1;
                break;
            case VK_RETURN:
                if (!repeat && g_cfg.waypoints)
                    nav::ToggleWaypoint(g_viewX, g_viewY);
                break;
            case VK_DELETE:
                if (!repeat && g_cfg.waypoints)
                    nav::ClearWaypoints();
                break;
            case VK_ESCAPE:
            case VK_BACK:
                if (repeat)
                    break;
                if (g_openedOnMap)
                    CloseMenu();
                else
                    g_page = Page::List;
                break;
            default:
                if (vk == g_cfg.mapKey && !repeat)
                    CloseMenu();
                else if (vk == g_cfg.minimapKey && !repeat && g_cfg.minimap)
                    g_minimap = !g_minimap;
                break;
            }
        }

        void HandleConfirmKey(int vk, bool repeat)
        {
            if (repeat)
                return;
            switch (vk)
            {
            case VK_LEFT:
            case VK_RIGHT:
            case VK_UP:
            case VK_DOWN:
            case VK_TAB:
                g_confirmYes = !g_confirmYes;
                break;
            case 'Y':
                ConfirmYes();
                break;
            case VK_RETURN:
            case VK_SPACE:
                if (g_confirmYes)
                    ConfirmYes();
                else
                    ConfirmNo();
                break;
            case 'N':
            case VK_ESCAPE:
            case VK_BACK:
                ConfirmNo();
                break;
            default:
                // Anfangsbuchstabe von "Ja"/"Oui"/"Sì" in der eingestellten Sprache
                if (vk == (int)towupper(text::Get(text::Yes)[0]))
                    ConfirmYes();
                break;
            }
        }

        void HandleMenuKey(int vk, bool repeat)
        {
            if (g_menu != Menu::Open)
                return;
            if (g_cfg.showInfo)
                LOG("Menütaste 0x%02X%s auf Seite %d", vk, repeat ? " (Wdh.)" : "", (int)g_page);
            switch (g_page)
            {
            case Page::List: HandleListKey(vk, repeat); break;
            case Page::Map: HandleMapKey(vk, repeat); break;
            case Page::Confirm: HandleConfirmKey(vk, repeat); break;
            case Page::Options: HandleOptionsKey(vk, repeat); break;
            case Page::Briefings: HandleBriefingsKey(vk, repeat); break;
            case Page::Progress:
                if (!repeat && (vk == VK_ESCAPE || vk == VK_BACK || vk == VK_RETURN))
                    g_page = Page::List;
                break;
            }
            g_dirty = true;
        }

        // Maus im Menü: Einträge anklicken, Karte ziehen und mit dem Rad zoomen, Klick auf die Karte
        // setzt/entfernt einen Wegpunkt, Rechtsklick wirkt wie Esc
        void HandleMouse(const MSG& msg)
        {
            float mx, my;
            if (g_menu != Menu::Open || g_screenW <= 0 || !controls::MousePosition(mx, my))
                return;
            const Layout L = MakeLayout(g_screenW, g_screenH);
            const std::vector<Entry> entries = Entries();
            const float ppb = kZoom[g_zoom];
            const float cx = L.mapX + L.mapW * 0.5f, cy = L.mapY + L.mapH * 0.5f;

            if (!g_cursorShown)
                g_dirty = true; // Fußzeile zeigt ab jetzt die Maus-Hilfe
            if (!g_cursorShown || mx != g_mouseX || my != g_mouseY)
                g_cursorMoved = true;
            g_cursorShown = true;
            g_mouseX = mx;
            g_mouseY = my;

            int hover = -1;
            if (g_page != Page::Confirm && mx >= L.m && mx < L.m + L.listW)
                for (int i = 0; i < (int)entries.size(); ++i)
                    if (my >= L.RowY(i) && my < L.RowY(i) + L.rowH)
                        hover = i;

            // Optionen: Zeile unter der Maus
            const Panel P = MakePanel(L);
            const bool panelPage = g_page == Page::Options || g_page == Page::Briefings || g_page == Page::Progress;
            int optHover = -1;
            if (g_page == Page::Options && L.InMap(mx, my))
            {
                const int row = P.RowAt(my);
                const int i = row >= 0 ? g_optScroll + row : -1;
                if (i >= 0 && i < options::Count() && !options::IsHeader(i))
                    optHover = i;
            }
            if (optHover != g_optHover)
            {
                g_optHover = optHover;
                if (optHover >= 0)
                    g_optSel = optHover;
                g_dirty = true;
            }

            switch (msg.message)
            {
            case WM_MOUSEMOVE:
                if (g_dragging)
                {
                    const float dx = mx - g_dragX, dy = my - g_dragY;
                    if (std::fabs(dx) + std::fabs(dy) > 4)
                        g_dragMoved = true;
                    if (g_dragMoved)
                    {
                        g_viewX = std::min(256.0f, std::max(0.0f, g_dragViewX - dx / ppb));
                        g_viewY = std::min(256.0f, std::max(0.0f, g_dragViewY - dy / ppb));
                        g_follow = false;
                        g_dirty = true;
                    }
                }
                break;
            case WM_LBUTTONDOWN:
                if (g_page == Page::Confirm)
                {
                    for (int i = 0; i < 2; ++i)
                    {
                        if (mx >= L.BtnX(i) && mx < L.BtnX(i) + L.btnW && my >= L.btnY && my < L.btnY + L.btnH)
                        {
                            if (i == 0)
                                ConfirmYes();
                            else
                                ConfirmNo();
                            g_dirty = true;
                            return;
                        }
                    }
                    break;
                }
                if (hover >= 0 && entries[hover].enabled)
                {
                    g_sel = hover;
                    g_hover = -1;
                    Activate(entries[hover].id);
                    g_dirty = true;
                    return;
                }
                if (g_page == Page::Options && optHover >= 0)
                {
                    // Klick auf die linke Hälfte der Wertespalte ("‹"): zurück, sonst weiter
                    const int valueX = P.x + P.w - L.m - P.ValueW();
                    const bool back = mx >= valueX && mx < valueX + P.ValueW() / 2;
                    ChangeOption(optHover, back ? -1 : 1);
                    g_dirty = true;
                    return;
                }
                if (panelPage)
                    break;
                if (L.InMap(mx, my) && g_map.Loaded())
                {
                    if (g_page != Page::Map)
                    {
                        g_page = Page::Map;
                        g_openedOnMap = false;
                    }
                    g_dragging = true;
                    g_dragMoved = false;
                    g_dragX = mx;
                    g_dragY = my;
                    g_dragViewX = g_viewX;
                    g_dragViewY = g_viewY;
                    g_dirty = true;
                }
                break;
            case WM_LBUTTONUP:
                if (g_dragging && !g_dragMoved && g_page == Page::Map && g_cfg.waypoints && L.InMap(mx, my))
                    nav::ToggleWaypoint(g_viewX + (mx - cx) / ppb, g_viewY + (my - cy) / ppb);
                g_dragging = false;
                g_dirty = true;
                break;
            case WM_RBUTTONUP:
                g_dragging = false;
                HandleMenuKey(VK_ESCAPE, false);
                return;
            case WM_MOUSEWHEEL:
            {
                const int steps = GET_WHEEL_DELTA_WPARAM(msg.wParam) / WHEEL_DELTA;
                if (g_page == Page::Options)
                {
                    g_optScroll = std::max(0, std::min(g_optScroll - steps * 2, options::Count() - P.rows));
                    g_optHover = -1;
                    g_dirty = true;
                    break;
                }
                if (g_page == Page::Briefings)
                {
                    g_briefScroll = std::max(0, std::min(g_briefScroll - steps * 3, g_briefMaxScroll));
                    g_dirty = true;
                    break;
                }
                if (g_page == Page::Progress)
                    break;
                const int zoom = std::min(std::max(g_zoom + steps, 0), kZoomCount - 1);
                if (zoom == g_zoom || !g_map.Loaded() || (g_page != Page::Map && !L.InMap(mx, my)))
                    break;
                if (g_page == Page::Map && L.InMap(mx, my))
                {
                    // Der Punkt unter der Maus bleibt beim Zoomen stehen
                    const float wx = g_viewX + (mx - cx) / ppb, wy = g_viewY + (my - cy) / ppb;
                    g_viewX = std::min(256.0f, std::max(0.0f, wx - (mx - cx) / kZoom[zoom]));
                    g_viewY = std::min(256.0f, std::max(0.0f, wy - (my - cy) / kZoom[zoom]));
                    g_follow = false;
                }
                g_zoom = zoom;
                g_dirty = true;
                break;
            }
            default:
                break;
            }

            if (hover != g_hover)
            {
                g_hover = hover;
                if (g_page == Page::List && hover >= 0 && entries[hover].enabled)
                    g_sel = hover;
                g_dirty = true;
            }
        }

        // Eigener Mauszeiger, der Windows-Zeiger ist im Spiel ausgeblendet
        void DrawCursor(SurfaceView& v)
        {
            Canvas& c = g_canvas;
            const float s = std::max(12.0f, v.height / 32.0f);
            const float x = g_mouseX, y = g_mouseY;
            const int bx = (int)x - 4, by = (int)y - 4, bw = (int)(s * 0.8f) + 10, bh = (int)(s * 1.1f) + 10;
            c.BeginLayer(bx, by, bw, bh);
            c.Triangle(x - 1.5f, y - 3.0f, x - 1.5f, y + s + 3.0f, x + s * 0.72f + 3.0f, y + s * 0.72f + 1.0f, 0x101418, 220);
            c.Triangle(x, y, x, y + s, x + s * 0.72f, y + s * 0.72f, 0xFFFFFF);
            c.Composite(v, bx, by, bw, bh);
        }

        void PanelFrame(Canvas& c, const Panel& P, int m, const wchar_t* title)
        {
            c.Frame(P.x - 2, P.y - 2, P.w + 4, P.h + 4, kAccent, 2);
            c.Blend(P.x, P.y, P.w, P.h, 0x0B0E12, 215);
            c.Text(P.x + m, P.y + (P.titleH - g_fonts.normalPx) / 2 - 2, title, g_fonts.normalFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
            c.Fill(P.x + m / 2, P.rowsY - 2, P.w - m, 1, 0x3A424C);
        }

        void ScrollBar(Canvas& c, int x, int y, int h, int first, int visible, int total)
        {
            if (total <= visible || h <= 0)
                return;
            c.Fill(x, y, 3, h, 0x2A3038);
            const int thumbH = std::max(12, h * visible / total);
            const int thumbY = y + (h - thumbH) * first / std::max(1, total - visible);
            c.Fill(x, thumbY, 3, thumbH, 0x8A929C);
        }

        void RenderOptions(Canvas& c, const Layout& L)
        {
            const Panel P = MakePanel(L);
            const int m = L.m;
            PanelFrame(c, P, m, text::Get(text::MenuOptions));
            ClampOptionScroll(P.rows);

            const int n = options::Count();
            const int valueW = P.ValueW();
            const int valueX = P.x + P.w - m - valueW;
            for (int r = 0; r < P.rows && g_optScroll + r < n; ++r)
            {
                const int i = g_optScroll + r;
                const int y = P.rowsY + r * P.rowH;
                const int ty = y + (P.rowH - g_fonts.smallPx) / 2 - 1;
                const wchar_t* label = options::Label(i);
                if (options::IsHeader(i))
                {
                    c.Text(P.x + m, ty, label, g_fonts.smallFont, kAccent, TA_LEFT | TA_TOP, false);
                    const int tw = c.TextWidth(label, g_fonts.smallFont);
                    c.Fill(P.x + m + tw + m / 2, y + P.rowH / 2, P.w - 2 * m - tw - m / 2, 1, 0x3A424C);
                    continue;
                }
                const bool sel = i == g_optSel;
                if (sel)
                {
                    c.Blend(P.x + m / 2, y, P.w - m, P.rowH, 0x2A3441, 230);
                    c.Fill(P.x + m / 2, y, 3, P.rowH, kAccent);
                }
                std::wstring name = label;
                if (options::NeedsRestart(i))
                    name += L" *";
                c.Text(P.x + m + m / 2, ty, name.c_str(), g_fonts.smallFont, sel ? 0xFFFFFF : 0xC9CFD6, TA_LEFT | TA_TOP, false);
                const std::wstring value = options::Value(i);
                c.Text(valueX + valueW / 2, ty, value.c_str(), g_fonts.smallFont, sel ? 0xFFFFFF : 0xB9C0C8, TA_CENTER | TA_TOP, false);
                if (sel)
                {
                    c.Text(valueX + 4, ty, L"\u2039", g_fonts.smallFont, kAccent, TA_LEFT | TA_TOP, false);
                    c.Text(valueX + valueW - 4, ty, L"\u203A", g_fonts.smallFont, kAccent, TA_RIGHT | TA_TOP, false);
                }
            }
            ScrollBar(c, P.x + P.w - m / 3 - 3, P.rowsY, P.rows * P.rowH, g_optScroll, P.rows, n);

            std::wstring foot = L"* ";
            foot += text::Get(text::OptRestart);
            c.Text(P.x + m, P.footY, foot.c_str(), g_fonts.smallFont, 0x8A929C, TA_LEFT | TA_TOP, false);
        }

        void RenderBriefings(Canvas& c, const Layout& L)
        {
            const Panel P = MakePanel(L);
            const int m = L.m;
            PanelFrame(c, P, m, text::Get(text::MenuBriefings));

            const int count = briefings::Count();
            if (count == 0)
            {
                g_briefMaxScroll = g_briefScroll = 0;
                c.Text(P.x + P.w / 2, P.y + P.h / 2 - g_fonts.smallPx, text::Get(text::BriefingsEmpty), g_fonts.smallFont, 0xB0B6BE, TA_CENTER | TA_TOP, false);
                return;
            }

            // Neueste Nachricht oben; zwischen den Nachrichten eine Leerzeile
            struct Line { std::wstring text; int entry; };
            std::vector<Line> lines;
            const int textX = P.x + m + m / 2 + 3;
            const int textW = P.x + P.w - m - textX;
            for (int e = 0; e < count; ++e)
            {
                for (std::wstring& t : Wrap(c, briefings::Text(e), g_fonts.smallFont, textW))
                    lines.push_back({ std::move(t), e });
                lines.push_back({ L"", -1 });
            }
            lines.pop_back();

            const int lh = g_fonts.smallPx + 5;
            const int top = P.rowsY + m / 4, bottom = P.footY - m / 4;
            const int visible = std::max(1, (bottom - top) / lh);
            g_briefMaxScroll = std::max(0, (int)lines.size() - visible);
            g_briefScroll = std::min(g_briefScroll, g_briefMaxScroll);
            for (int r = 0; r < visible && g_briefScroll + r < (int)lines.size(); ++r)
            {
                const Line& ln = lines[g_briefScroll + r];
                if (ln.entry < 0)
                    continue;
                const int y = top + r * lh;
                const bool newest = ln.entry == 0;
                c.Fill(P.x + m, y, 3, lh, newest ? kAccent : 0x4A515B);
                c.Text(textX, y + 2, ln.text.c_str(), g_fonts.smallFont, newest ? 0xFFFFFF : 0xC9CFD6, TA_LEFT | TA_TOP, false);
            }
            ScrollBar(c, P.x + P.w - m / 3 - 3, top, visible * lh, g_briefScroll, visible, (int)lines.size());
        }

        // 1234567 -> "1.234.567", Trennzeichen nach Sprache
        std::wstring Grouped(int value)
        {
            const wchar_t* lang = text::Language();
            const wchar_t sep = wcscmp(lang, L"en") == 0 ? L',' : wcscmp(lang, L"fr") == 0 ? L' ' : L'.';
            std::wstring digits = std::to_wstring(value < 0 ? -(long long)value : (long long)value);
            for (int i = (int)digits.size() - 3; i > 0; i -= 3)
                digits.insert((size_t)i, 1, sep);
            return value < 0 ? L"-" + digits : digits;
        }

        // Fortschritt im Level: die Zahlen, die das Spiel in seiner eigenen Pausenanzeige einzeln durchblättert
        void RenderProgress(Canvas& c, const Layout& L)
        {
            const Panel P = MakePanel(L);
            const int m = L.m;
            PanelFrame(c, P, m, text::Get(text::MenuProgress));
            gta1::Progress pr;
            if (!gta1::GetProgress(pr))
            {
                c.Text(P.x + P.w / 2, P.y + P.h / 2 - g_fonts.smallPx, text::Get(text::ProgUnavailable), g_fonts.smallFont, 0xB0B6BE, TA_CENTER | TA_TOP, false);
                return;
            }

            const int s = g_fonts.smallPx;
            const int barH = std::max(4, s / 4), gap = std::max(6, s * 3 / 5), textGap = std::max(5, s * 2 / 5);
            const int x0 = P.x + m + m / 2, x1 = P.x + P.w - m - m / 2, w = x1 - x0;
            int y = P.rowsY + gap;
            wchar_t buf[96];
            // Zeile mit Bezeichnung links, Stand rechts und Balken darunter
            const auto row = [&](text::Id label, const wchar_t* value, int done, int total, uint32_t color) {
                c.Text(x0, y, text::Get(label), g_fonts.smallFont, 0xC9CFD6, TA_LEFT | TA_TOP, false);
                c.Text(x1, y, value, g_fonts.smallFont, 0xFFFFFF, TA_RIGHT | TA_TOP, false);
                y += s + textGap;
                c.Fill(x0, y, w, barH, 0x2A3038);
                const float fill = total > 0 ? std::min(1.0f, std::max(0.0f, done / (float)total)) : 0.0f;
                if (const int fw = (int)(w * fill + 0.5f))
                    c.Fill(x0, y, fw, barH, color);
                y += barH + gap;
            };

            if (pr.target > 0)
                swprintf_s(buf, L"%s / %s", Grouped(pr.score).c_str(), Grouped(pr.target).c_str());
            else
                wcscpy_s(buf, Grouped(pr.score).c_str());
            row(text::ProgScore, buf, pr.score, pr.target, kAccent);
            if (pr.missionsTotal > 0)
            {
                swprintf_s(buf, L"%d / %d", pr.missionsDone, pr.missionsTotal);
                row(text::ProgMissions, buf, pr.missionsDone, pr.missionsTotal, 0x5AA9FF);
            }
            if (pr.secretsTotal > 0)
            {
                swprintf_s(buf, L"%d / %d", pr.secretsFound, pr.secretsTotal);
                row(text::ProgSecrets, buf, pr.secretsFound, pr.secretsTotal, 0x4CC36B);
            }

            // Leben und Multiplikator nebeneinander
            swprintf_s(buf, L"%s  %d", text::Get(text::ProgLives), pr.lives);
            c.Text(x0, y, buf, g_fonts.smallFont, 0xC9CFD6, TA_LEFT | TA_TOP, false);
            swprintf_s(buf, L"%s  x%d", text::Get(text::ProgMultiplier), pr.multiplier);
            c.Text(x0 + w / 2, y, buf, g_fonts.smallFont, 0xC9CFD6, TA_LEFT | TA_TOP, false);
        }

        void RenderMenu(Canvas& c)
        {
            const int W = c.Width(), H = c.Height();
            const Layout L = MakeLayout(W, H);
            if (g_bgW == W && g_bgH == H)
                memcpy(c.Pixels(), g_background.data(), (size_t)W * H * 4);
            else
                c.Fill(0, 0, W, H, 0x101316);

            const int m = L.m, top = L.top, areaH = L.areaH, listW = L.listW;
            const int mapX = L.mapX, mapY = L.mapY, mapW = L.mapW, mapH = L.mapH;
            wchar_t buf[256];

            // Kopfzeile
            c.Text(m, m - g_fonts.titlePx / 6, text::Get(text::PauseTitle), g_fonts.titleFont, 0xFFFFFF);
            const int titleW = c.TextWidth(text::Get(text::PauseTitle), g_fonts.titleFont);
            if (g_map.Loaded())
                c.Text(m + titleW + m, m + g_fonts.titlePx - g_fonts.normalPx - g_fonts.titlePx / 8, g_map.CityName(), g_fonts.normalFont, kAccent);
            if (g_havePlayer)
            {
                const wchar_t* how = g_player.ctrlType == 0 ? text::Get(text::InVehicle) : g_player.ctrlType == 1 ? text::Get(text::OnTrain)
                                   : g_player.ctrlType == 2 ? text::Get(text::OnFoot) : L"";
                swprintf_s(buf, L"X %.1f   Y %.1f   Z %.1f", g_player.x, g_player.y, g_player.z);
                c.Text(W - m, m, buf, g_fonts.normalFont, 0xFFFFFF, TA_RIGHT | TA_TOP);
                c.Text(W - m, m + g_fonts.normalPx + 2, how, g_fonts.smallFont, 0xB9C0C8, TA_RIGHT | TA_TOP);
            }

            // Menüliste
            c.Blend(m, top, listW, areaH, 0x0B0E12, 185);
            const std::vector<Entry> entries = Entries();
            const int rowH = L.rowH;
            int y = L.listY;
            for (int i = 0; i < (int)entries.size(); ++i)
            {
                const bool selected = (g_page != Page::Map && i == g_sel) || (g_page == Page::Map && i == g_hover && entries[i].enabled);
                if (selected)
                {
                    c.Blend(m, y, listW, rowH, 0x2A3441, 230);
                    c.Fill(m, y, 4, rowH, kAccent);
                }
                const uint32_t color = !entries[i].enabled ? 0x6C737C : selected ? 0xFFFFFF : 0xC9CFD6;
                c.Text(m + m / 2 + 4, y + (rowH - g_fonts.normalPx) / 2 - 2, entries[i].label, g_fonts.normalFont, color, TA_LEFT | TA_TOP, false);
                y += rowH + 2;
            }

            // GPS-Status unter der Liste
            y += m / 2;
            if (g_cfg.gps || g_cfg.waypoints)
            {
                const nav::TargetKind kind = nav::Kind();
                const wchar_t* what = kind == nav::TargetKind::Waypoint ? text::Get(text::TargetWaypoint)
                                    : kind == nav::TargetKind::Mission ? text::Get(text::TargetMission)
                                    : kind == nav::TargetKind::MissionRed ? text::Get(text::TargetMissionRed) : nullptr;
                if (what)
                    swprintf_s(buf, text::Get(text::GpsTarget), what, (int)(nav::RemainingBlocks() * kMetersPerBlock + 0.5f));
                else
                    wcscpy_s(buf, text::Get(text::GpsNone));
                c.Text(m + m / 2, y, buf, g_fonts.smallFont, 0xB9C0C8, TA_LEFT | TA_TOP, false);
                y += g_fonts.smallPx + 4;
                if (g_cfg.waypoints)
                {
                    swprintf_s(buf, text::Get(text::WaypointCount), (int)nav::Waypoints().size());
                    c.Text(m + m / 2, y, buf, g_fonts.smallFont, 0xB9C0C8, TA_LEFT | TA_TOP, false);
                }
            }

            // Rechts: Optionen, Briefings oder die Karte
            const bool mapActive = g_page == Page::Map;
            const bool panelPage = g_page == Page::Options || g_page == Page::Briefings || g_page == Page::Progress;
            if (!panelPage)
                c.Fill(mapX - 2, mapY - 2, mapW + 4, mapH + 4, mapActive ? kAccent : 0x4A515B);
            if (g_page == Page::Options)
                RenderOptions(c, L);
            else if (g_page == Page::Briefings)
                RenderBriefings(c, L);
            else if (g_page == Page::Progress)
                RenderProgress(c, L);
            else if (g_map.Loaded())
            {
                const float ppb = kZoom[g_zoom];
                if ((g_follow || !mapActive) && g_havePlayer)
                {
                    g_viewX = g_player.x;
                    g_viewY = g_player.y;
                }
                g_map.Render(c.Row(mapY) + mapX, W, mapW, mapH, g_viewX, g_viewY, ppb, g_style);

                const float ox = mapX + mapW * 0.5f, oy = mapY + mapH * 0.5f;
                const float markerR = std::max(4.0f, H / 105.0f);
                c.SetClipRect(mapX, mapY, mapW, mapH);
                DrawNav(c, ox, oy, g_viewX, g_viewY, ppb, markerR, true);
                if (g_havePlayer)
                {
                    float sx = ox + (g_player.x - g_viewX) * ppb;
                    float sy = oy + (g_player.y - g_viewY) * ppb;
                    const float inset = markerR * 3;
                    const bool visible = sx >= mapX + inset && sx <= mapX + mapW - inset && sy >= mapY + inset && sy <= mapY + mapH - inset;
                    sx = std::min(std::max(sx, mapX + inset), mapX + mapW - inset);
                    sy = std::min(std::max(sy, mapY + inset), mapY + mapH - inset);
                    DrawPlayerMarker(c, sx, sy, markerR, g_player, visible && g_blink, visible ? 255 : 150);
                }
                if (mapActive)
                {
                    // Fadenkreuz zum Setzen von Wegpunkten
                    const float r = markerR * 2.2f;
                    c.Line(ox - r * 1.6f, oy, ox - r * 0.5f, oy, 4, 0x14181D);
                    c.Line(ox + r * 0.5f, oy, ox + r * 1.6f, oy, 4, 0x14181D);
                    c.Line(ox, oy - r * 1.6f, ox, oy - r * 0.5f, 4, 0x14181D);
                    c.Line(ox, oy + r * 0.5f, ox, oy + r * 1.6f, 4, 0x14181D);
                    c.Line(ox - r * 1.6f, oy, ox - r * 0.5f, oy, 2, 0xFFFFFF);
                    c.Line(ox + r * 0.5f, oy, ox + r * 1.6f, oy, 2, 0xFFFFFF);
                    c.Line(ox, oy - r * 1.6f, ox, oy - r * 0.5f, 2, 0xFFFFFF);
                    c.Line(ox, oy + r * 0.5f, ox, oy + r * 1.6f, 2, 0xFFFFFF);
                    c.Ring(ox, oy, r * 0.5f, 2, 0xFFFFFF);
                }
                c.ClearClip();

                // Kartenzeile oben links: Stil und Zoom, oben rechts Nordpfeil
                swprintf_s(buf, L"%s  ·  Zoom %.1fx%s%s", text::Get(g_style == CityMap::Textured ? text::StyleTextured : text::StyleSchematic),
                           kZoom[g_zoom] / kZoom[kDefaultZoom], mapActive && !g_follow ? L"  ·  " : L"",
                           mapActive && !g_follow ? text::Get(text::ViewFree) : L"");
                const int tw = c.TextWidth(buf, g_fonts.smallFont);
                c.Blend(mapX, mapY, tw + m, g_fonts.smallPx + m / 2, 0x0B0E12, 170);
                c.Text(mapX + m / 2, mapY + m / 4, buf, g_fonts.smallFont, 0xE6E9ED, TA_LEFT | TA_TOP, false);
                if (mapActive)
                {
                    swprintf_s(buf, text::Get(text::CursorPos), (int)g_viewX, (int)g_viewY);
                    const int cw = c.TextWidth(buf, g_fonts.smallFont);
                    c.Blend(mapX, mapY + mapH - g_fonts.smallPx - m / 2, cw + m, g_fonts.smallPx + m / 2, 0x0B0E12, 170);
                    c.Text(mapX + m / 2, mapY + mapH - g_fonts.smallPx - m / 4, buf, g_fonts.smallFont, 0xE6E9ED, TA_LEFT | TA_TOP, false);
                }

                // Legende der Orte unten rechts
                if (g_cfg.pois && !pois::Current().empty())
                {
                    bool present[(int)pois::Kind::Count] = {};
                    for (const pois::Poi& p : pois::Current())
                        present[(int)p.kind] = true;
                    const text::Id names[] = { text::PoiSpray, text::PoiBombShop, text::PoiCrane };
                    const float ir = g_fonts.smallPx * 0.55f;
                    const int gap = m / 2, itemGap = m;
                    int total = gap;
                    for (int k = 0; k < (int)pois::Kind::Count; ++k)
                        if (present[k])
                            total += (int)(ir * 2) + 4 + c.TextWidth(text::Get(names[k]), g_fonts.smallFont) + itemGap;
                    const int lh = g_fonts.smallPx + m / 2;
                    int lx = mapX + mapW - total, ly = mapY + mapH - lh;
                    c.Blend(lx, ly, total, lh, 0x0B0E12, 170);
                    lx += gap;
                    for (int k = 0; k < (int)pois::Kind::Count; ++k)
                    {
                        if (!present[k])
                            continue;
                        DrawPoiIcon(c, lx + ir, ly + lh * 0.5f, ir, (pois::Kind)k);
                        lx += (int)(ir * 2) + 4;
                        c.Text(lx, ly + (lh - g_fonts.smallPx) / 2 - 1, text::Get(names[k]), g_fonts.smallFont, 0xE6E9ED, TA_LEFT | TA_TOP, false);
                        lx += c.TextWidth(text::Get(names[k]), g_fonts.smallFont) + itemGap;
                    }
                }

                const float nx = mapX + mapW - m * 1.2f, ny = mapY + m * 1.3f, nr = m * 0.75f;
                c.Circle(nx, ny, nr, 0x0B0E12, 170);
                c.Triangle(nx, ny - nr * 0.8f, nx - nr * 0.35f, ny - nr * 0.15f, nx + nr * 0.35f, ny - nr * 0.15f, kAccent);
                c.Text((int)nx, (int)(ny - nr * 0.1f), L"N", g_fonts.smallFont, 0xFFFFFF, TA_CENTER | TA_TOP, false);
            }
            else
            {
                c.Fill(mapX, mapY, mapW, mapH, 0x1A1E23);
                c.Text(mapX + mapW / 2, mapY + mapH / 2 - g_fonts.normalPx / 2, text::Get(text::MapUnavailable), g_fonts.normalFont, 0xB0B6BE, TA_CENTER | TA_TOP);
            }

            // Sicherheitsabfrage
            if (g_page == Page::Confirm)
            {
                c.Blend(0, 0, W, H, 0x000000, 110);
                const int bw = L.boxW, bh = L.boxH, bx = L.boxX, by = L.boxY;
                c.Blend(bx, by, bw, bh, 0x11151A, 240);
                c.Fill(bx, by, bw, 4, kAccent);
                const bool quit = g_confirmKind == Confirm::Quit;
                const text::Id title = quit ? text::ConfirmTitle : g_confirmKind == Confirm::Load ? text::LoadTitle : text::StartLoadTitle;
                const text::Id body = quit ? text::ConfirmText : g_confirmKind == Confirm::Load ? text::LoadText : text::StartLoadText;
                c.Text(bx + m, by + m, text::Get(title), g_fonts.normalFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
                std::vector<std::wstring> lines = Wrap(c, text::Get(body), g_fonts.smallFont, bw - 2 * m);
                lines.resize(std::min<size_t>(lines.size(), quit ? 3 : 2));
                if (!quit && !g_slotInfo.empty())
                    lines.push_back(g_slotInfo);
                for (size_t k = 0; k < lines.size(); ++k)
                    c.Text(bx + m, by + m + g_fonts.normalPx * 9 / 5 + (int)k * g_fonts.smallPx * 27 / 20, lines[k].c_str(), g_fonts.smallFont,
                           !quit && k + 1 == lines.size() && !g_slotInfo.empty() ? kAccent : 0xB9C0C8, TA_LEFT | TA_TOP, false);
                const int btnW = L.btnW, btnH = L.btnH, btnY = L.btnY;
                const wchar_t* labels[2] = { text::Get(text::Yes), text::Get(text::No) };
                for (int i = 0; i < 2; ++i)
                {
                    const bool sel = (i == 0) == g_confirmYes;
                    const int bxi = L.BtnX(i);
                    c.Blend(bxi, btnY, btnW, btnH, sel ? 0x2A3441 : 0x1A1F26, 240);
                    if (sel)
                        c.Frame(bxi, btnY, btnW, btnH, kAccent, 2);
                    c.Text(bxi + btnW / 2, btnY + (btnH - g_fonts.normalPx) / 2 - 2, labels[i], g_fonts.normalFont, sel ? 0xFFFFFF : 0xB9C0C8, TA_CENTER | TA_TOP, false);
                }
            }

            // Fußzeile mit Tastenhilfe
            wchar_t mapKey[16];
            KeyName(g_cfg.mapKey, mapKey, 16);
            if (g_page == Page::List)
                swprintf_s(buf, text::Get(text::HelpList), mapKey);
            else if (g_page == Page::Map && g_cursorShown)
                wcscpy_s(buf, text::Get(g_cfg.waypoints ? text::HelpMapMouse : text::HelpMapMouseNoWaypoints));
            else if (g_page == Page::Map)
                wcscpy_s(buf, text::Get(g_cfg.waypoints ? text::HelpMap : text::HelpMapNoWaypoints));
            else if (g_page == Page::Options)
                wcscpy_s(buf, text::Get(text::HelpOptions));
            else if (g_page == Page::Briefings)
                wcscpy_s(buf, text::Get(text::HelpBriefings));
            else if (g_page == Page::Progress)
                wcscpy_s(buf, text::Get(text::HelpProgress));
            else
                wcscpy_s(buf, text::Get(text::HelpConfirm));
            c.Text(W / 2, H - m / 2 - g_fonts.smallPx - 2, buf, g_fonts.smallFont, 0xD5DAE0, TA_CENTER | TA_TOP);

            // Kurze Rückmeldung unten links unter der Menüliste
            if (!g_status.empty())
            {
                const int pad = m / 2;
                const int tw = c.TextWidth(g_status.c_str(), g_fonts.smallFont);
                const int bw = std::min(tw + 2 * pad + 4, W - 2 * m), bh = g_fonts.smallPx + pad + 4;
                const int bx = m, by = top + areaH - bh;
                c.Blend(bx, by, bw, bh, g_statusError ? 0x3A1010 : 0x10301A, 235);
                c.Fill(bx, by, 4, bh, g_statusError ? 0xE8262E : 0x4CC36B);
                c.Text(bx + pad + 4, by + pad / 2 + 1, g_status.c_str(), g_fonts.smallFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
            }
        }

        void DrawMenu(SurfaceView& v)
        {
            const int bpp = v.fmt.BytesPerPixel();
            const int rowBytes = v.width * bpp;
            if (g_dirty || g_cacheW != v.width || g_cacheH != v.height || g_cacheFmt != v.fmt)
            {
                if (!g_canvas.Resize(v.width, v.height))
                    return;
                RenderMenu(g_canvas);
                GdiFlush();
                g_cache.resize((size_t)rowBytes * v.height);
                for (int y = 0; y < v.height; ++y)
                    ConvertToSurface(v.fmt, g_canvas.Row(y), &g_cache[(size_t)y * rowBytes], v.width);
                g_cacheW = v.width;
                g_cacheH = v.height;
                g_cacheFmt = v.fmt;
                g_dirty = false;
            }
            for (int y = 0; y < v.height; ++y)
                memcpy(v.Row(y), &g_cache[(size_t)y * rowBytes], rowBytes);
        }

        void DrawHint(SurfaceView& v)
        {
            Canvas& c = g_canvas;
            wchar_t mapKey[16], miniKey[16], part[64], line[256];
            KeyName(g_cfg.mapKey, mapKey, 16);
            KeyName(g_cfg.minimapKey, miniKey, 16);
            wcscpy_s(line, text::Get(text::HintActive));
            if (g_cfg.pauseMenu && g_cfg.escOpensMenu)
            {
                wcscat_s(line, L"   ·   ");
                wcscat_s(line, text::Get(text::HintMenu));
            }
            if (g_cfg.pauseMenu)
            {
                swprintf_s(part, text::Get(text::HintMap), mapKey);
                wcscat_s(line, L"   ·   ");
                wcscat_s(line, part);
            }
            if (g_cfg.minimap)
            {
                swprintf_s(part, text::Get(text::HintMinimap), miniKey);
                wcscat_s(line, L"   ·   ");
                wcscat_s(line, part);
            }

            const int padX = v.height / 50, padY = v.height / 110;
            const int bw = c.TextWidth(line, g_fonts.smallFont) + 2 * padX;
            const int bh = g_fonts.smallPx + 2 * padY + 2;
            const int x = (v.width - bw) / 2, y = v.height * 17 / 100;
            c.BeginLayer(x, y, bw, bh);
            c.Blend(x, y, bw, bh, 0x0B0E12, 190);
            c.Fill(x, y, 3, bh, kAccent);
            c.Text(x + padX, y + padY, line, g_fonts.smallFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
            c.Composite(v, x, y, bw, bh);
        }

        // Die Minimap weicht dem HUD des Spiels aus: in den oberen Ecken nach unten (Waffe, Rüstung, Zeitanzeigen),
        // in den unteren nach oben (Missionstexte). Belegt ist, was das HUD im letzten Bild gezeichnet hat; bekannt
        // nur, solange es über die HUD-Vergrößerung läuft, sonst bleibt sie in der Ecke. Ergebnis: oberer Rand.
        int FreeMinimapY(const SurfaceView& v, int x, int home, int S, int m)
        {
            const bool down = home < v.height / 2;
            const int gap = std::max(4, v.height / 100);
            hudscale::Watch(x - 2 * gap, x + S + 2 * gap);   // für das nächste Bild
            hudscale::Coverage hud;
            if (!hudscale::LastCoverage(hud) || hud.width != v.width || hud.height != v.height)
                return home;
            const float R = S * 0.5f + gap, cell = (float)hud.cellPx;
            int y = home;
            for (int pass = 0; pass < 8; ++pass)
            {
                // Belegte Zellen, die den Kreis (samt Abstand) berühren: oberster und unterster Rand
                const float cx = x + S * 0.5f, cy = y + S * 0.5f;
                const int c0 = std::max(0, (int)((cx - R) / cell)), c1 = std::min(hud.cols - 1, (int)((cx + R) / cell));
                const int r0 = std::max(0, (int)((cy - R) / cell)), r1 = std::min(hud.rows - 1, (int)((cy + R) / cell));
                int top = INT_MAX, bottom = INT_MIN;
                for (int r = r0; r <= r1; ++r)
                    for (int c = c0; c <= c1; ++c)
                    {
                        if (!hud.cells[r * hud.cols + c])
                            continue;
                        const float dx = std::clamp(cx, c * cell, (c + 1) * cell) - cx;
                        const float dy = std::clamp(cy, r * cell, (r + 1) * cell) - cy;
                        if (dx * dx + dy * dy >= R * R)
                            continue;
                        top = std::min(top, (int)(r * cell));
                        bottom = std::max(bottom, (int)((r + 1) * cell));
                    }
                if (top == INT_MAX)
                    return y;
                y = down ? bottom + gap : top - gap - S;
                if (y < m || y > v.height - m - S)
                    break;                          // kein Platz frei: in der Ecke bleiben
            }
            return home;
        }

        // Weg von der Ecke sofort, zurück erst nach einer Weile ohne Bedarf: sonst springt sie beim Waffenwechsel
        int MinimapY(const SurfaceView& v, int x, int home, int S, int m)
        {
            const int goal = FreeMinimapY(v, x, home, S, m);
            const DWORD now = GetTickCount();
            const int layout = g_cfg.minimapCorner + 4 * (S + 4096 * v.height);
            if (layout != g_miniLayout)
            {
                g_miniLayout = layout;
                g_miniY = (float)(g_miniGoal = goal);
                g_miniTick = g_miniCalmSince = now;
                return goal;
            }
            if (std::abs(goal - home) >= std::abs(g_miniGoal - home))
            {
                g_miniGoal = goal;
                g_miniCalmSince = now;
            }
            else if (now - g_miniCalmSince >= 1500)
                g_miniGoal = goal;
            const float dt = std::min(0.1f, (now - g_miniTick) / 1000.0f);
            g_miniTick = now;
            g_miniY += (g_miniGoal - g_miniY) * (1.0f - std::exp(-dt * 12.0f));
            if (std::abs(g_miniGoal - g_miniY) < 0.5f)
                g_miniY = (float)g_miniGoal;
            return (int)std::lround(g_miniY);
        }

        void DrawMinimap(SurfaceView& v, const gta1::Player& p)
        {
            EnsureMap();
            if (!g_map.Loaded())
                return;

            Canvas& c = g_canvas;
            const int H = v.height, W = v.width;
            const int S = std::max(64, H * g_cfg.minimapSize / 100);
            const int m = std::max(6, H / 30);
            const int x = (g_cfg.minimapCorner & 1) ? W - m - S : m;
            int home = (g_cfg.minimapCorner & 2) ? m : H - m - S;
            if (g_cfg.minimapCorner == 3 && g_cfg.hearts) // oben rechts: unter Punktestand und Herzen
                home = std::max(home, (int)((kHeartTop + kHeartSize) * hudscale::Factor()) + 8);
            const int y = MinimapY(v, x, home, S, m);
            const float R = S * 0.5f, cx = x + R, cy = y + R;
            const float ppb = S * g_cfg.minimapZoom / 3000.0f;   // 100 % = 30 Blöcke im Durchmesser

            c.BeginLayer(x - 2, y - 2, S + 4, S + 4);
            g_miniBuf.resize((size_t)S * S);
            g_map.Render(g_miniBuf.data(), S, S, S, p.x, p.y, ppb, g_style);
            for (int j = 0; j < S; ++j)
                for (int i = 0; i < S; ++i)
                {
                    const float dx = i + 0.5f - R, dy = j + 0.5f - R;
                    const float cov = std::min(1.0f, R - 1.0f - std::sqrt(dx * dx + dy * dy));
                    if (cov > 0)
                        c.BlendPixel(x + i, y + j, g_miniBuf[(size_t)j * S + i], (int)(235 * cov));
                }

            const float markerR = std::max(3.0f, H / 150.0f);
            c.SetClipCircle(cx, cy, R - 2.0f);
            DrawNav(c, cx, cy, p.x, p.y, ppb, markerR, false);
            c.ClearClip();

            // Ziel außerhalb der Minimap: Markierung am Rand in Zielrichtung
            if (nav::Kind() != nav::TargetKind::None)
            {
                const nav::Point t = nav::Target();
                const float dx = (t.x - p.x) * ppb, dy = (t.y - p.y) * ppb;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d > R - markerR * 2)
                {
                    const uint32_t col = nav::Kind() == nav::TargetKind::Waypoint ? kWaypointColor
                                       : nav::Kind() == nav::TargetKind::MissionRed ? kRedColor : kMissionColor;
                    const float ux = dx / d, uy = dy / d, er = R - markerR * 1.6f;
                    DrawDiamond(c, cx + ux * er, cy + uy * er, markerR * 0.9f, col);
                }
            }

            c.Ring(cx, cy, R - 1.5f, 2.5f, 0xF2F2F2, 230);
            DrawPlayerMarker(c, cx, cy, markerR, p, false);

            const float nr = std::max(6.0f, g_fonts.smallPx * 0.7f);
            c.Circle(cx, y + nr * 0.5f, nr, 0x1B1F24, 230);
            c.Text((int)cx, (int)(y + nr * 0.5f - g_fonts.smallPx * 0.55f), L"N", g_fonts.smallFont, 0xFFFFFF, TA_CENTER | TA_TOP, false);
            c.Composite(v, x - 2, y - 2, S + 4, S + 4);
        }

        // Blauer Pfeil oben in der Mitte, zeigt auf den nächsten Abbiegepunkt der Route
        void DrawGpsArrow(SurfaceView& v, const gta1::Player& p)
        {
            nav::Point turn;
            if (!nav::NextTurn(turn))
                return;
            const float dx = turn.x - p.x, dy = turn.y - p.y;
            if (dx * dx + dy * dy < 1e-4f)
                return;
            const float a = std::atan2(dx, -dy) - tiltcam::Rotation(); // gedrehtes Bild der Kippkamera

            Canvas& c = g_canvas;
            const int H = v.height, W = v.width;
            const float s = H * 0.05f;
            const float cx = W * 0.5f, cy = H * 0.02f + s * 1.05f;
            const int bx = (int)(cx - s * 2.2f), by = (int)(cy - s * 1.25f), bw = (int)(s * 4.4f), bh = (int)(s * 2.5f + g_fonts.smallPx + 4);

            c.BeginLayer(bx, by, bw, bh);
            DrawArrowShape(c, cx + 2, cy + 2, s * 1.18f, a, 0x000000, 80);
            DrawArrowShape(c, cx, cy, s * 1.18f, a, 0xFFFFFF, 240);
            DrawArrowShape(c, cx, cy, s, a, kGpsBlue, 255);

            wchar_t buf[32];
            swprintf_s(buf, L"%d m", (int)(nav::RemainingBlocks() * kMetersPerBlock + 0.5f));
            c.Text((int)cx, (int)(cy + s * 1.15f), buf, g_fonts.smallFont, 0xFFFFFF, TA_CENTER | TA_TOP, true);
            c.Composite(v, bx, by, bw, bh);
        }

        // Herzform (Parametergleichung), Mittelpunkt 0/0, etwa 1 breit
        const std::vector<float>& HeartShape()
        {
            static std::vector<float> xy;
            if (xy.empty())
            {
                constexpr int n = 40;
                for (int i = 0; i < n; ++i)
                {
                    const float t = i * 6.2831853f / n, s = std::sin(t);
                    const float x = 16 * s * s * s;
                    const float y = -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t));
                    xy.push_back(x / 34.0f);
                    xy.push_back((y - 2.5f) / 34.0f);
                }
            }
            return xy;
        }

        void DrawHeart(Canvas& c, float cx, float cy, float size, uint32_t rgb, int alpha)
        {
            const std::vector<float>& unit = HeartShape();
            std::vector<float> pts(unit.size());
            for (size_t i = 0; i < unit.size(); i += 2)
            {
                pts[i] = cx + unit[i] * size;
                pts[i + 1] = cy + unit[i + 1] * size;
            }
            c.Polygon(pts.data(), (int)pts.size() / 2, rgb, alpha);
        }

        // Lebensenergie als fünf Herzen (je 20 Punkte) rechts oben unter dem Punktestand des Spiels
        void DrawHearts(SurfaceView& v, int health, bool burning)
        {
            Canvas& c = g_canvas;
            // Maße in Pixeln des Spiel-HUD; wird das vergrößert, wachsen die Herzen mit
            const float k = (float)hudscale::Factor();
            const float size = kHeartSize * k, step = kHeartStep * k, top = kHeartTop * k;
            const float right = v.width - 3.0f * k, cy = top + size * 0.5f;
            const int pad = (int)(3 * k);
            const int bx = (int)(right - kHeartCount * step) - pad, by = (int)top - pad;
            const int bw = (int)(kHeartCount * step) + 2 * pad + 1, bh = (int)size + 2 * pad + 1;
            // Brennend flackern die vollen Herzen orange
            const uint32_t fill = burning && (GetTickCount() / 120) % 2 == 0 ? 0xFF8A1E : 0xE8262E;

            c.BeginLayer(bx, by, bw, bh);
            for (int i = 0; i < kHeartCount; ++i)
            {
                const float cx = right - (kHeartCount - i - 0.5f) * step;
                DrawHeart(c, cx + 1.0f * k, cy + 1.5f * k, size + 2.4f * k, 0x000000, 90);
                DrawHeart(c, cx, cy, size + 2.4f * k, 0x000000, 190);
                DrawHeart(c, cx, cy, size, 0x4A1216, 235);
                const float part = std::clamp((health - i * 20) / 20.0f, 0.0f, 1.0f);
                if (part <= 0)
                    continue;
                const float left = cx - size * 0.5f;
                c.SetClipRect((int)std::floor(left), by, (int)std::ceil(size * part), bh);
                DrawHeart(c, cx, cy, size, fill, 255);
                c.ClearClip();
                if (part > 0.5f)
                    c.Circle(cx - size * 0.2f, cy - size * 0.17f, 1.4f * k, 0xFFFFFF, 170);
            }
            c.Composite(v, bx, by, bw, bh);
        }

        // Name einer Taste aus ihrem Scancode (+0x100 = erweiterte Taste), in der Sprache von Windows
        void ScanName(int scan, wchar_t* out, int n)
        {
            const LONG lp = ((scan & 0xFF) << 16) | ((scan & 0x100) ? (1 << 24) : 0);
            if ((scan & 0x1FF) == 0x39)
                wcscpy_s(out, n, text::Get(text::KeySpace));
            else if (scan <= 0 || !GetKeyNameTextW(lp, out, n))
                swprintf_s(out, n, L"0x%02X", scan & 0x1FF);
        }

        // Brennende Spielfigur: Hinweis zum Löschen unter der Bildmitte
        void DrawBurnHint(SurfaceView& v)
        {
            Canvas& c = g_canvas;
            wchar_t key[64], keys[160], line[320];
            ScanName(gta1::JumpKeyScancode(), key, 64);
            if (controls::PadConnected())
                swprintf_s(keys, L"[%s] / [%s]", key, text::Get(text::PadJump));
            else
                swprintf_s(keys, L"[%s]", key);
            swprintf_s(line, text::Get(text::HintBurning), keys);

            const int padX = v.height / 45, padY = v.height / 100;
            const int bw = c.TextWidth(line, g_fonts.normalFont) + 2 * padX;
            const int bh = g_fonts.normalPx + 2 * padY + 2;
            const int x = (v.width - bw) / 2, y = v.height * 68 / 100;
            const bool pulse = (GetTickCount() / 250) % 2 == 0;
            c.BeginLayer(x, y, bw, bh);
            c.Blend(x, y, bw, bh, 0x160907, 205);
            c.Fill(x, y, 4, bh, pulse ? 0xFF8A1E : 0xE8262E);
            c.Text(x + padX, y + padY, line, g_fonts.normalFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
            c.Composite(v, x, y, bw, bh);
        }

        // Meldung im Spiel, z.B. "Automatisch gespeichert"
        void DrawToast(SurfaceView& v)
        {
            Canvas& c = g_canvas;
            const int padX = v.height / 45, padY = v.height / 100;
            const int bw = c.TextWidth(g_toast.c_str(), g_fonts.normalFont) + 2 * padX;
            const int bh = g_fonts.normalPx + 2 * padY + 2;
            const int x = (v.width - bw) / 2, y = v.height * 80 / 100;
            c.BeginLayer(x, y, bw, bh);
            c.Blend(x, y, bw, bh, 0x0B0E12, 200);
            c.Fill(x, y, 4, bh, g_toastError ? 0xE8262E : 0x4CC36B);
            c.Text(x + padX, y + padY, g_toast.c_str(), g_fonts.normalFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
            c.Composite(v, x, y, bw, bh);
        }

        void DrawInfo(SurfaceView& v)
        {
            Canvas& c = g_canvas;
            wchar_t lines[6][200];
            swprintf_s(lines[0], L"Flips %u   HUD %u   Status %d   im Spiel %d   %dx%d %d Bit",
                       ddhook::GameFlipCount(), gta1::HudCount(), gta1::RunState(), (int)gta1::InGame(), v.width, v.height, v.fmt.bpp);
            gta1::Player p;
            if (gta1::GetPlayer(p))
                swprintf_s(lines[1], L"Spieler %.2f  %.2f  %.2f   Winkel %d   Typ %d   Tempo %d", p.x, p.y, p.z, p.angle, p.ctrlType, p.speed);
            else
                wcscpy_s(lines[1], L"Spieler: -");
            const std::string map = gta1::MapFile();
            swprintf_s(lines[2], L"Karte: %hs", map.empty() ? "-" : map.c_str() + map.find_last_of("\\/") + 1);
            const nav::Point t = nav::Target();
            swprintf_s(lines[3], L"GPS: Ziel %d (%.1f, %.1f)   Route %d Punkte", (int)nav::Kind(), t.x, t.y, (int)nav::Route().size());
            controls::DebugLines(lines[4], lines[5], 200);

            const int lh = g_fonts.smallPx + 3;
            int bw = 0;
            for (auto& l : lines)
                bw = std::max(bw, c.TextWidth(l, g_fonts.smallFont));
            bw = std::min(bw + 12, v.width - 12);
            const int bh = 6 * lh + 8, x = 6, y = 6;
            c.BeginLayer(x, y, bw, bh);
            c.Blend(x, y, bw, bh, 0x000000, 160);
            for (int i = 0; i < 6; ++i)
                c.Text(x + 6, y + 4 + i * lh, lines[i], g_fonts.smallFont, 0x9EF01A, TA_LEFT | TA_TOP, false);
            c.Composite(v, x, y, bw, bh);
        }

        // Zielkreis für die Twin-Stick-Steuerung mit der Maus
        void DrawCrosshair(SurfaceView& v, float x, float y)
        {
            Canvas& c = g_canvas;
            const float r = std::max(6.0f, v.height / 55.0f);
            const int bx = (int)(x - r * 2), by = (int)(y - r * 2), bs = (int)(r * 4) + 2;
            c.BeginLayer(bx, by, bs, bs);
            c.Ring(x, y, r, 4.0f, 0x000000, 120);
            c.Ring(x, y, r, 2.0f, 0xFFFFFF, 235);
            for (int i = 0; i < 4; ++i)
            {
                const float dx = i == 0 ? 1.f : i == 1 ? -1.f : 0.f, dy = i == 2 ? 1.f : i == 3 ? -1.f : 0.f;
                c.Line(x + dx * r * 0.45f, y + dy * r * 0.45f, x + dx * r * 1.55f, y + dy * r * 1.55f, 2.0f, 0xFFFFFF, 235);
            }
            c.Circle(x, y, 1.8f, 0xFF4B3E);
            c.Composite(v, bx, by, bs, bs);
        }

        // Mauszeiger zum Umschauen im Fahrzeug: schlichter Ring ohne Zielkreuz
        void DrawLookCursor(SurfaceView& v, float x, float y)
        {
            Canvas& c = g_canvas;
            const float r = std::max(5.0f, v.height / 70.0f);
            const int bx = (int)(x - r * 2), by = (int)(y - r * 2), bs = (int)(r * 4) + 2;
            c.BeginLayer(bx, by, bs, bs);
            c.Ring(x, y, r, 4.0f, 0x000000, 100);
            c.Ring(x, y, r, 2.0f, 0xFFFFFF, 200);
            c.Circle(x, y, 1.8f, 0xFFFFFF, 200);
            c.Composite(v, bx, by, bs, bs);
        }

        void Request(Page page);

        // Sprite (ARGB, Alpha 0 = durchsichtig) mittig um (cx, cy), scale-fach vergrößert, Pixel scharf
        void DrawSprite(SurfaceView& v, const uint32_t* px, int w, int h, float cx, float cy, float scale)
        {
            Canvas& c = g_canvas;
            const int dw = std::max(1, (int)std::lround(w * scale)), dh = std::max(1, (int)std::lround(h * scale));
            const int left = (int)std::lround(cx - dw * 0.5f), top = (int)std::lround(cy - dh * 0.5f);
            if (left >= c.Width() || top >= c.Height() || left + dw <= 0 || top + dh <= 0)
                return;
            c.BeginLayer(left, top, dw, dh);
            for (int y = 0; y < dh; ++y)
            {
                const uint32_t* row = px + std::min(h - 1, y * h / dh) * w;
                for (int x = 0; x < dw; ++x)
                {
                    const uint32_t p = row[std::min(w - 1, x * w / dw)];
                    if (p >> 24)
                        c.BlendPixel(left + x, top + y, p & 0xFFFFFF, 255);
                }
            }
            c.Composite(v, left, top, dw, dh);
        }

        // Brandbombe: fliegende Flaschen und, solange sie gewählt ist, ihr Symbol mit der Zahl oben links, wo das
        // Spiel sonst die Waffe zeigt (bei gewählter Brandbombe hält die Figur die Faust, das Spiel zeigt dort nichts)
        void DrawFirebomb(SurfaceView& v)
        {
            static std::vector<firebomb::Bomb> s_bombs;
            int w, h;
            firebomb::FlyingBombs(s_bombs);
            if (const uint32_t* bottle = firebomb::Icon(false, w, h))   // "small" ist ein Makro der Windows-Header
                for (const firebomb::Bomb& b : s_bombs)
                {
                    float ox = b.x, oy = b.y;   // GameToOutput ändert nur bei gekippter Kamera
                    tiltcam::GameToOutput(b.x, b.y, ox, oy);
                    DrawSprite(v, bottle, w, h, ox, oy, 0.6f * b.scale);
                }
            if (!firebomb::Selected() || firebomb::NativeHud())
                return;
            const float k = (float)hudscale::Factor();
            const uint32_t* big = firebomb::Icon(true, w, h);
            if (!big)
                return;
            const float left = 4 * k, top = 2 * k;
            DrawSprite(v, big, w, h, left + w * k * 0.5f, top + h * k * 0.5f, k);
            wchar_t count[8];
            swprintf_s(count, L"%d", firebomb::Ammo());
            Canvas& c = g_canvas;
            const int tx = (int)(left + w * k + 3 * k), ty = (int)(top + h * k) - g_fonts.normalPx - (int)k;
            const int tw = c.TextWidth(count, g_fonts.normalFont) + 4, th = g_fonts.normalPx + 6;
            c.BeginLayer(tx - 2, ty - 2, tw + 4, th + 4);
            c.Text(tx, ty, count, g_fonts.normalFont, 0xFFE34A);
            c.Composite(v, tx - 2, ty - 2, tw + 4, th + 4);
        }

        void DrawHud(SurfaceView& v)
        {
            if (!g_canvas.Resize(v.width, v.height))
                return;
            g_fonts.Update(v.height);

            const DWORD now = GetTickCount();
            const bool inGame = gta1::InGame();
            gta1::Player p;
            const bool havePlayer = inGame && gta1::GetPlayer(p);
            if (inGame && g_cfg.hint && !g_hintStart)
                g_hintStart = now;

            if (havePlayer && g_cfg.gps && g_cfg.gpsArrow)
                DrawGpsArrow(v, p);
            if (inGame && g_hintStart && now - g_hintStart < 8000)
                DrawHint(v);
            if (havePlayer && g_cfg.minimap && g_minimap)
                DrawMinimap(v, p);
            const int health = havePlayer && g_cfg.hearts ? gta1::PlayerHealth() : -1;
            if (health >= 0)
                DrawHearts(v, health, gta1::IsPlayerBurning());
            if (havePlayer && fire::CanRoll())
                DrawBurnHint(v);
            if (inGame)
                DrawFirebomb(v);
            float cx, cy;
            if (havePlayer && controls::Crosshair(cx, cy))
                DrawCrosshair(v, cx, cy);
            else if (havePlayer && controls::LookCursor(cx, cy))
                DrawLookCursor(v, cx, cy);
            std::wstring notice;
            bool noticeError = false;
            if (saves::TakeNotice(notice, noticeError))
                ShowToast(notice, noticeError);
            if (firebomb::TakeNotice(notice))
                ShowToast(notice);
            if (!g_toast.empty() && now - g_toastTick < kToastTime && inGame)
                DrawToast(v);
            if (g_cfg.showInfo)
                DrawInfo(v);

            // Beim Levelstart einen vorhandenen Spielstand anbieten
            if (inGame && g_cfg.pauseMenu && gta1::IsSinglePlayer() && saves::TakeLoadOffer())
            {
                g_confirmKind = Confirm::StartLoad;
                g_confirmYes = true;
                saves::SlotInfo(g_slotInfo);
                Request(Page::Confirm);
            }
        }

        void Request(Page page)
        {
            g_menu = Menu::Requested;
            g_requestPage = page;
            g_requestTick = GetTickCount();
        }
    }

    void Init(const wchar_t* basePath)
    {
        g_minimap = g_cfg.minimap && g_cfg.minimapOnStart;
        g_style = g_cfg.mapStyle;
        g_shotRequest = std::wstring(basePath) + L".shot";
        g_shotFile = std::wstring(basePath) + L"_shot.bmp";
    }

    void OnFrame(SurfaceView& v)
    {
        if (!v.fmt.Supported())
        {
            if (!g_warnedFormat)
            {
                LOG("Surface mit %d Bit Farbtiefe wird nicht bemalt (nur 16/24/32 Bit)", v.fmt.bpp);
                g_warnedFormat = true;
            }
            return;
        }

        if (g_restore)
        {
            if (g_bgW == v.width && g_bgH == v.height)
                for (int y = 0; y < v.height; ++y)
                    ConvertToSurface(v.fmt, &g_original[(size_t)y * v.width], v.Row(y), v.width);
            return;
        }
        g_screenW = v.width;
        g_screenH = v.height;
        MeasureReadback(v);

        if (g_menu == Menu::Requested)
            OpenNow(v);

        if (gta1::InGame() || g_menu == Menu::Open)
            UpdateNav();

        if (g_menu == Menu::Open)
        {
            if (!g_pausedByUs)
            {
                gta1::Player p;
                if (gta1::GetPlayer(p) && (p.x != g_player.x || p.y != g_player.y || p.angle != g_player.angle))
                {
                    g_player = p;
                    g_havePlayer = true;
                    g_dirty = true;
                }
            }
            DrawMenu(v);
            if (g_cursorShown)
            {
                DrawCursor(v);
                g_cursorMoved = false;
            }
            MaybeDumpFrame(v);
            return;
        }

        DrawHud(v);
        MaybeDumpFrame(v);
    }

    bool FilterMessage(const MSG& msg, bool removed)
    {
        switch (msg.message)
        {
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (removed && msg.wParam < 256)
                g_held[msg.wParam] = false;
            return false; // Loslassen geht immer ans Spiel, sonst "klemmen" dort Tasten
        case WM_CHAR:
            return g_menu != Menu::Closed;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            break;
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
            if (g_menu == Menu::Closed)
                return false;
            if (removed)
                HandleMouse(msg);
            return true;
        default:
            return false;
        }

        const int vk = (int)(msg.wParam & 0xFF);
        const bool repeat = (msg.lParam & (1 << 30)) != 0;

        if (g_menu != Menu::Closed)
        {
            // Alt-Kombinationen (Alt+Enter von cnc-ddraw, Alt+F4) weiterreichen, alles andere gehört dem Menü
            if (msg.message == WM_SYSKEYDOWN && vk != VK_F10)
                return false;
            if (removed)
                HandleMenuKey(vk, repeat);
            return true;
        }

        if (msg.message != WM_KEYDOWN || repeat || !gta1::InGame())
            return false;

        if (g_cfg.pauseMenu)
        {
            const bool esc = vk == VK_ESCAPE && g_cfg.escOpensMenu && gta1::IsSinglePlayer();
            if (esc || vk == g_cfg.mapKey)
            {
                if (removed)
                    Request(esc ? Page::List : Page::Map);
                return true;
            }
        }
        if (g_cfg.minimap && vk == g_cfg.minimapKey)
        {
            if (removed)
            {
                g_minimap = !g_minimap;
                LOG("Minimap %s", g_minimap ? "an" : "aus");
            }
            return true;
        }
        return false;
    }

    void OnPump()
    {
        if (g_menu == Menu::Closed || GetCurrentThreadId() != ddhook::GameThreadId())
            return;

        const DWORD now = GetTickCount();
        if (g_menu == Menu::Requested)
        {
            if (now - g_requestTick < 200)
                return;
            // Das Spiel präsentiert gerade nicht (z.B. eigene F6-Pause) -> sichtbares Bild als Hintergrund
            if (!ddhook::WithFrontBuffer([](SurfaceView& v) { OpenNow(v); }) || g_menu != Menu::Open)
            {
                g_menu = Menu::Closed;
                return;
            }
        }

        const float dt = std::min<DWORD>(now - g_pumpTick, 100) / 1000.0f;
        g_pumpTick = now;
        auto held = [](int vk) { return g_held[vk] || g_padHeld[vk]; };
        const int dx = (int)held(VK_RIGHT) - (int)held(VK_LEFT);
        const int dy = (int)held(VK_DOWN) - (int)held(VK_UP);
        if (g_page == Page::Map && (dx || dy))
        {
            // langsam anfangen und beim Halten beschleunigen (in Bildschirmpixeln pro Sekunde)
            g_panHold += dt;
            const float pxPerSec = std::max(200, g_canvas.Height()) * 0.35f * std::min(1.0f + g_panHold * 1.5f, 3.0f);
            g_viewX = std::min(256.0f, std::max(0.0f, g_viewX + dx * pxPerSec / kZoom[g_zoom] * dt));
            g_viewY = std::min(256.0f, std::max(0.0f, g_viewY + dy * pxPerSec / kZoom[g_zoom] * dt));
            g_follow = false;
            g_dirty = true;
        }
        else
            g_panHold = 0;
        if (!g_status.empty() && now - g_statusTick > 2500)
        {
            g_status.clear();
            g_dirty = true;
        }
        if (now - g_blinkTick >= 500)
        {
            g_blink = !g_blink;
            g_blinkTick = now;
            g_dirty = true;
        }

        // Nur selbst präsentieren, wenn das Spiel gerade keine Frames liefert
        if ((g_dirty || g_cursorMoved) && now - ddhook::LastGameFlipTick() > kNoFlipTime && now - g_presentTick > 15)
        {
            ddhook::PresentOverlay();
            g_presentTick = now;
        }
    }

    bool MenuActive() { return g_menu != Menu::Closed; }

    void InjectKey(int vk)
    {
        if (g_menu != Menu::Open)
            return;
        HandleMenuKey(vk, false);
        // Richtungen vom Controller laufen über SetHeld; hier nicht als gehalten markieren
        if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN)
            g_held[vk] = false;
    }

    void SetHeld(int vk, bool held)
    {
        if (vk >= 0 && vk < 256)
            g_padHeld[vk] = held && g_menu == Menu::Open;
    }

    void RequestOpen(bool mapPage)
    {
        if (g_menu == Menu::Closed && g_cfg.pauseMenu && gta1::InGame())
            Request(mapPage ? Page::Map : Page::List);
    }

    void ToggleMinimap()
    {
        if (g_cfg.minimap)
            g_minimap = !g_minimap;
    }

    bool ScreenSize(int& w, int& h)
    {
        w = g_screenW;
        h = g_screenH;
        return w > 0 && h > 0;
    }

    const CityMap* City()
    {
        EnsureMap();
        return g_map.Loaded() ? &g_map : nullptr;
    }
}

#include "overlay.h"
#include "canvas.h"
#include "citymap.h"
#include "config.h"
#include "controls.h"
#include "ddhook.h"
#include "fire.h"
#include "gta1.h"
#include "log.h"
#include "nav.h"
#include "text.h"

#include <algorithm>
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
        enum class Page { List, Map, Confirm };
        enum class Item { Resume, Map, Quit };

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
            l.listW = std::min(std::max(W * 24 / 100, 170), 330);
            l.rowH = g_fonts.normalPx * 2 + 4;
            l.listY = l.top + l.m / 2;
            l.mapX = l.m + l.listW + l.m / 2;
            l.mapY = l.top;
            l.mapW = W - l.mapX - l.m;
            l.mapH = l.areaH;
            l.boxW = std::max(W * 36 / 100, 300);
            l.boxH = g_fonts.normalPx * 6 + l.m * 2;
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
            return {
                { Item::Resume, text::Get(text::Resume), true },
                { Item::Map, text::Get(g_cfg.waypoints ? text::MapAndWaypoints : text::Map), true },
                { Item::Quit, text::Get(text::QuitLevel), gta1::Supported() && gta1::IsSinglePlayer() },
            };
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

        // Route, Missionsziele und Wegpunkte für einen Kartenausschnitt; (ox, oy) = Bildschirmmitte des Ausschnitts
        void DrawNav(Canvas& c, float ox, float oy, float cx, float cy, float ppb, float markerR, bool numbers)
        {
            auto X = [&](float wx) { return ox + (wx - cx) * ppb; };
            auto Y = [&](float wy) { return oy + (wy - cy) * ppb; };

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

        void CaptureBackground(const SurfaceView& v)
        {
            const int w = v.width, h = v.height;
            g_original.resize((size_t)w * h);
            for (int y = 0; y < h; ++y)
                ConvertFromSurface(v.fmt, v.Row(y), &g_original[(size_t)y * w], w);

            g_background = g_original;
            BoxBlur(g_background, w, h, std::max(2, h / 160));
            for (uint32_t& c : g_background)
            {
                const uint32_t r = ((c >> 16) & 0xFF) * 90 >> 8, g = ((c >> 8) & 0xFF) * 90 >> 8, b = (c & 0xFF) * 90 >> 8;
                c = ((r + 8) << 16) | ((g + 11) << 8) | (b + 16);
            }
            g_bgW = w;
            g_bgH = h;
        }

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
            CaptureBackground(v);
            EnsureMap();

            gta1::Player p;
            g_havePlayer = gta1::GetPlayer(p);
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
            if (wasOpen && GetTickCount() - ddhook::LastGameFlipTick() > 100)
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
            case Item::Quit:
                g_page = Page::Confirm;
                g_confirmYes = false;
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
                QuitLevel();
                break;
            case VK_RETURN:
            case VK_SPACE:
                if (g_confirmYes)
                    QuitLevel();
                else
                    g_page = Page::List;
                break;
            case 'N':
            case VK_ESCAPE:
            case VK_BACK:
                g_page = Page::List;
                break;
            default:
                // Anfangsbuchstabe von "Ja"/"Oui"/"Sì" in der eingestellten Sprache
                if (vk == (int)towupper(text::Get(text::Yes)[0]))
                    QuitLevel();
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
                                QuitLevel();
                            else
                                g_page = Page::List;
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
            c.ReadFrom(v, bx, by, bw, bh);
            c.Triangle(x - 1.5f, y - 3.0f, x - 1.5f, y + s + 3.0f, x + s * 0.72f + 3.0f, y + s * 0.72f + 1.0f, 0x101418, 220);
            c.Triangle(x, y, x, y + s, x + s * 0.72f, y + s * 0.72f, 0xFFFFFF);
            c.WriteTo(v, bx, by, bw, bh);
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

            // Karte
            const bool mapActive = g_page == Page::Map;
            c.Fill(mapX - 2, mapY - 2, mapW + 4, mapH + 4, mapActive ? kAccent : 0x4A515B);
            if (g_map.Loaded())
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
                c.Text(bx + m, by + m, text::Get(text::ConfirmTitle), g_fonts.normalFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
                c.Text(bx + m, by + m + g_fonts.normalPx * 2, text::Get(text::ConfirmText), g_fonts.smallFont, 0xB9C0C8, TA_LEFT | TA_TOP, false);
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
            else
                wcscpy_s(buf, text::Get(text::HelpConfirm));
            c.Text(W / 2, H - m / 2 - g_fonts.smallPx - 2, buf, g_fonts.smallFont, 0xD5DAE0, TA_CENTER | TA_TOP);
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
            c.ReadFrom(v, x, y, bw, bh);
            c.Blend(x, y, bw, bh, 0x0B0E12, 190);
            c.Fill(x, y, 3, bh, kAccent);
            c.Text(x + padX, y + padY, line, g_fonts.smallFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
            c.WriteTo(v, x, y, bw, bh);
        }

        void DrawMinimap(SurfaceView& v, const gta1::Player& p)
        {
            EnsureMap();
            if (!g_map.Loaded())
                return;

            Canvas& c = g_canvas;
            const int H = v.height, W = v.width;
            const int S = std::max(64, H * 26 / 100);
            const int m = std::max(6, H / 30);
            const int x = (g_cfg.minimapCorner & 1) ? W - m - S : m;
            int y = (g_cfg.minimapCorner & 2) ? m : H - m - S;
            if (g_cfg.minimapCorner == 3 && g_cfg.hearts) // oben rechts: unter Punktestand und Herzen
                y = std::max(y, (int)(kHeartTop + kHeartSize) + 8);
            const float R = S * 0.5f, cx = x + R, cy = y + R;
            const float ppb = S / 30.0f;

            c.ReadFrom(v, x - 2, y - 2, S + 4, S + 4);
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
            c.WriteTo(v, x - 2, y - 2, S + 4, S + 4);
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
            const float a = std::atan2(dx, -dy);

            Canvas& c = g_canvas;
            const int H = v.height, W = v.width;
            const float s = H * 0.05f;
            const float cx = W * 0.5f, cy = H * 0.02f + s * 1.05f;
            const int bx = (int)(cx - s * 2.2f), by = (int)(cy - s * 1.25f), bw = (int)(s * 4.4f), bh = (int)(s * 2.5f + g_fonts.smallPx + 4);

            c.ReadFrom(v, bx, by, bw, bh);
            DrawArrowShape(c, cx + 2, cy + 2, s * 1.18f, a, 0x000000, 80);
            DrawArrowShape(c, cx, cy, s * 1.18f, a, 0xFFFFFF, 240);
            DrawArrowShape(c, cx, cy, s, a, kGpsBlue, 255);

            wchar_t buf[32];
            swprintf_s(buf, L"%d m", (int)(nav::RemainingBlocks() * kMetersPerBlock + 0.5f));
            c.Text((int)cx, (int)(cy + s * 1.15f), buf, g_fonts.smallFont, 0xFFFFFF, TA_CENTER | TA_TOP, true);
            c.WriteTo(v, bx, by, bw, bh);
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
            const float right = v.width - 3.0f, cy = kHeartTop + kHeartSize * 0.5f;
            const int bx = (int)(right - kHeartCount * kHeartStep) - 3, by = (int)kHeartTop - 3;
            const int bw = (int)(kHeartCount * kHeartStep) + 7, bh = (int)kHeartSize + 7;
            // Brennend flackern die vollen Herzen orange
            const uint32_t fill = burning && (GetTickCount() / 120) % 2 == 0 ? 0xFF8A1E : 0xE8262E;

            c.ReadFrom(v, bx, by, bw, bh);
            for (int i = 0; i < kHeartCount; ++i)
            {
                const float cx = right - (kHeartCount - i - 0.5f) * kHeartStep;
                DrawHeart(c, cx + 1.0f, cy + 1.5f, kHeartSize + 2.4f, 0x000000, 90);
                DrawHeart(c, cx, cy, kHeartSize + 2.4f, 0x000000, 190);
                DrawHeart(c, cx, cy, kHeartSize, 0x4A1216, 235);
                const float part = std::clamp((health - i * 20) / 20.0f, 0.0f, 1.0f);
                if (part <= 0)
                    continue;
                const float left = cx - kHeartSize * 0.5f;
                c.SetClipRect((int)std::floor(left), by, (int)std::ceil(kHeartSize * part), bh);
                DrawHeart(c, cx, cy, kHeartSize, fill, 255);
                c.ClearClip();
                if (part > 0.5f)
                    c.Circle(cx - kHeartSize * 0.2f, cy - kHeartSize * 0.17f, 1.4f, 0xFFFFFF, 170);
            }
            c.WriteTo(v, bx, by, bw, bh);
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
            c.ReadFrom(v, x, y, bw, bh);
            c.Blend(x, y, bw, bh, 0x160907, 205);
            c.Fill(x, y, 4, bh, pulse ? 0xFF8A1E : 0xE8262E);
            c.Text(x + padX, y + padY, line, g_fonts.normalFont, 0xFFFFFF, TA_LEFT | TA_TOP, false);
            c.WriteTo(v, x, y, bw, bh);
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
            c.ReadFrom(v, x, y, bw, bh);
            c.Blend(x, y, bw, bh, 0x000000, 160);
            for (int i = 0; i < 6; ++i)
                c.Text(x + 6, y + 4 + i * lh, lines[i], g_fonts.smallFont, 0x9EF01A, TA_LEFT | TA_TOP, false);
            c.WriteTo(v, x, y, bw, bh);
        }

        // Zielkreis für die Twin-Stick-Steuerung mit der Maus
        void DrawCrosshair(SurfaceView& v, float x, float y)
        {
            Canvas& c = g_canvas;
            const float r = std::max(6.0f, v.height / 55.0f);
            const int bx = (int)(x - r * 2), by = (int)(y - r * 2), bs = (int)(r * 4) + 2;
            c.ReadFrom(v, bx, by, bs, bs);
            c.Ring(x, y, r, 4.0f, 0x000000, 120);
            c.Ring(x, y, r, 2.0f, 0xFFFFFF, 235);
            for (int i = 0; i < 4; ++i)
            {
                const float dx = i == 0 ? 1.f : i == 1 ? -1.f : 0.f, dy = i == 2 ? 1.f : i == 3 ? -1.f : 0.f;
                c.Line(x + dx * r * 0.45f, y + dy * r * 0.45f, x + dx * r * 1.55f, y + dy * r * 1.55f, 2.0f, 0xFFFFFF, 235);
            }
            c.Circle(x, y, 1.8f, 0xFF4B3E);
            c.WriteTo(v, bx, by, bs, bs);
        }

        // Mauszeiger zum Umschauen im Fahrzeug: schlichter Ring ohne Zielkreuz
        void DrawLookCursor(SurfaceView& v, float x, float y)
        {
            Canvas& c = g_canvas;
            const float r = std::max(5.0f, v.height / 70.0f);
            const int bx = (int)(x - r * 2), by = (int)(y - r * 2), bs = (int)(r * 4) + 2;
            c.ReadFrom(v, bx, by, bs, bs);
            c.Ring(x, y, r, 4.0f, 0x000000, 100);
            c.Ring(x, y, r, 2.0f, 0xFFFFFF, 200);
            c.Circle(x, y, 1.8f, 0xFFFFFF, 200);
            c.WriteTo(v, bx, by, bs, bs);
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
            float cx, cy;
            if (havePlayer && controls::Crosshair(cx, cy))
                DrawCrosshair(v, cx, cy);
            else if (havePlayer && controls::LookCursor(cx, cy))
                DrawLookCursor(v, cx, cy);
            if (g_cfg.showInfo)
                DrawInfo(v);
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
        if (now - g_blinkTick >= 500)
        {
            g_blink = !g_blink;
            g_blinkTick = now;
            g_dirty = true;
        }

        // Nur selbst präsentieren, wenn das Spiel gerade keine Frames liefert
        if ((g_dirty || g_cursorMoved) && now - ddhook::LastGameFlipTick() > 100 && now - g_presentTick > 15)
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

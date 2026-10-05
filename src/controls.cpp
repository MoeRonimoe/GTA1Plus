#include "controls.h"
#include "camera.h"
#include "config.h"
#include "fire.h"
#include "gta1.h"
#include "hooks.h"
#include "log.h"
#include "overlay.h"
#include "rumble.h"
#include "tiltcam.h"

#include <windows.h>
#include <windowsx.h>
#include <xinput.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstring>

#pragma comment(lib, "dinput8.lib") // nur für c_dfDIJoystick2, DirectInput8Create wird dynamisch geholt
#pragma comment(lib, "dxguid.lib")
#include <algorithm>
#include <cmath>

namespace controls
{
    namespace
    {
        // Aufbau des Eingabepakets (erzeugt in 0x436980/0x436B70, angewendet in 0x465350)
        constexpr uint32_t kSteer = 0x00000001, kSteerBits = 0x00001E00; // Wert -7..7 in Bit 9-12
        constexpr uint32_t kGas = 0x00000002, kGasBits = 0x00018000;     // 0..3 in Bit 15-16
        constexpr uint32_t kBrake = 0x00000080, kBrakeBits = 0x00006000; // 0..3 in Bit 13-14
        constexpr uint32_t kJump = 0x00000004, kJumpHeld = 0x00100000;
        constexpr uint32_t kFire = 0x00000008, kFireHeld = 0x00200000;
        constexpr uint32_t kWeaponNext = 0x00000010, kWeaponNextHeld = 0x00040000;
        constexpr uint32_t kHorn = 0x00000020, kHornHeld = 0x00080000;         // im Fahrzeug Hupe, zu Fuß Pfiff
        constexpr uint32_t kWeaponPrev = 0x00000100, kWeaponPrevHeld = 0x00020000;
        constexpr uint32_t kEnterExit = 0x00400000; // nur Druck-Ereignis

        struct Intent
        {
            bool drive = false;      // Lenkung/Gas selbst setzen
            int steer = 0;
            int throttle = 0;        // >0 vorwärts, <0 rückwärts
            bool angleValid = false; // Blickrichtung zu Fuß setzen
            int angle = 0;
            int renderAngle = -1;    // Seitwärtslaufen: Blickrichtung nur fürs Zeichnen (angle = Laufrichtung)
            bool fire = false, jump = false, weaponNext = false, weaponPrev = false, horn = false, enterExit = false;
        };

        using XInputGetState_t = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        XInputGetState_t g_xinput = nullptr;
        rumble::XInputSetState_t g_xinputSet = nullptr;
        bool g_padConnected = false;
        DWORD g_padRetry = 0;
        WORD g_padPrev = 0;

        bool g_driving = false;
        bool g_sentFire = false, g_sentJump = false, g_sentWeaponNext = false, g_sentWeaponPrev = false, g_sentHorn = false;

        bool g_crosshair = false;
        float g_crossX = 0, g_crossY = 0;
        int   g_wheel = 0;              // ausstehende Mausrad-Schritte: + nächste, - vorherige Waffe
        bool  g_wheelPulse = false;     // eben einen Schritt gesendet, im nächsten Paket einmal loslassen
        bool  g_crossStick = false;     // Fadenkreuz vom rechten Stick: Punkt im Spielbild statt Mausposition
        DWORD g_mouseMoved = 0;
        // Zuletzt benutztes Gerät: Solange der Controller aktiv ist, zählt die (stehende) Maus weder fürs Zielen
        // noch für Kamera und Fadenkreuz. Echte Mausbewegung, Klicks oder WASD schalten zurück.
        bool g_padActive = false;
        int g_renderAngle = -1;

        // Kamerasteuerung: Ziel aus Maus bzw. rechtem Stick in Pixeln der Spielauflösung (siehe camera.cpp)
        float g_camTargetX = 0, g_camTargetY = 0;
        bool g_lookCursor = false; // Mauszeiger zum Umschauen im Fahrzeug

        // Für die Debug-Anzeige
        struct DebugState
        {
            unsigned calls = 0;
            bool focus = false, menu = false, inGame = false, onFoot = false, haveMouse = false;
            RECT client = {};
            Intent in;
            uint32_t packetIn = 0, packetOut = 0;
        } g_dbg;

        // Tasten- und Mauszustand aus den Fenstermeldungen des Spiels (wie das Spiel selbst seine Tasten liest)
        bool g_down[256] = {};
        HWND g_mouseWnd = nullptr;
        POINT g_mouse = {};
        bool g_haveMouse = false;
        bool g_hadFocus = false;
        bool g_enterHeld = false;

        // cnc-ddraw (hook=4) hängt sich auch in die Imports unserer ASI und meldet für das Spielfenster
        // die Spielauflösung statt der echten Fenstergröße - darum die echten user32-Funktionen verwenden
        using GetClientRect_t = BOOL(WINAPI*)(HWND, LPRECT);
        using GetForegroundWindow_t = HWND(WINAPI*)();
        GetClientRect_t g_getClientRect = nullptr;
        GetForegroundWindow_t g_getForegroundWindow = nullptr;

        bool GameHasFocus()
        {
            HWND wnd = g_getForegroundWindow();
            DWORD pid = 0;
            GetWindowThreadProcessId(wnd, &pid);
            return wnd && pid == GetCurrentProcessId();
        }

        bool Key(int vk) { return vk > 0 && vk < 256 && g_down[vk]; }

        // Bildschirmrichtung (dx rechts, dy unten) -> Spielwinkel (0 = Süden, gegen den Uhrzeigersinn, 1024 = 360°)
        int AngleFromScreen(float dx, float dy)
        {
            const float compass = std::atan2(dx, -dy) * (180.0f / 3.14159265f); // 0 = oben, im Uhrzeigersinn
            int a = (int)std::lround((180.0f - compass) * 1024.0f / 360.0f);
            return a & 1023;
        }

        // ---- DirectInput: PS4/PS5 und andere Pads ohne XInput ----
        // Die Gerätesuche (EnumDevices) kann spürbar dauern und läuft deshalb in einem eigenen Thread;
        // abgefragt wird das Gerät im Spiel-Thread.
        using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
        struct DiPad
        {
            DirectInput8Create_t create = nullptr;
            IDirectInput8W* di = nullptr;           // Spiel-Thread
            IDirectInputDevice8W* dev = nullptr;
            bool triggers = false;                  // analoge L2/R2 auf den Achsen Rx/Ry
            volatile LONG found = 0;                // Such-Thread hat ein Gerät gemeldet
            GUID instance = {};
            wchar_t name[MAX_PATH] = {};
            WORD vid = 0, pid = 0;
        } g_di;

        BOOL CALLBACK OnDiDevice(const DIDEVICEINSTANCEW* d, void*)
        {
            const WORD vid = LOWORD(d->guidProduct.Data1), pid = HIWORD(d->guidProduct.Data1);
            if (vid == 0x045E)
                return DIENUM_CONTINUE; // Microsoft-Pads laufen über XInput
            g_di.instance = d->guidInstance;
            wcsncpy_s(g_di.name, d->tszProductName, _TRUNCATE);
            g_di.vid = vid;
            g_di.pid = pid;
            InterlockedExchange(&g_di.found, 1);
            return DIENUM_STOP;
        }

        DWORD WINAPI DiSearchThread(void*)
        {
            IDirectInput8W* di = nullptr;
            if (FAILED(g_di.create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, reinterpret_cast<void**>(&di), nullptr)))
                return 0;
            for (;;)
            {
                if (!g_di.found && !g_padConnected)
                    di->EnumDevices(DI8DEVCLASS_GAMECTRL, &OnDiDevice, nullptr, DIEDFL_ATTACHEDONLY);
                Sleep(3000);
            }
        }

        BOOL CALLBACK OnThreadWindow(HWND h, LPARAM p)
        {
            if (!IsWindowVisible(h) || GetParent(h))
                return TRUE;
            *reinterpret_cast<HWND*>(p) = h;
            return FALSE;
        }

        // Sichtbares Hauptfenster des Spiel-Threads (für SetCooperativeLevel)
        HWND GameWindow()
        {
            HWND found = nullptr;
            EnumThreadWindows(GetCurrentThreadId(), &OnThreadWindow, reinterpret_cast<LPARAM>(&found));
            return found;
        }

        void DiRelease()
        {
            if (g_di.dev)
            {
                g_di.dev->Unacquire();
                g_di.dev->Release();
                g_di.dev = nullptr;
                rumble::UseHid(nullptr, 0, 0, nullptr);
                LOG("Controller getrennt: %ls", g_di.name);
            }
            InterlockedExchange(&g_di.found, 0);
        }

        bool DiOpen()
        {
            HWND wnd = GameWindow();
            if (!wnd || (!g_di.di && FAILED(g_di.create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                                         reinterpret_cast<void**>(&g_di.di), nullptr))))
                return false;
            IDirectInputDevice8W* dev = nullptr;
            if (FAILED(g_di.di->CreateDevice(g_di.instance, &dev, nullptr)))
                return false;
            DIPROPRANGE range = {};
            range.diph.dwSize = sizeof(range);
            range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            range.diph.dwHow = DIPH_DEVICE;
            range.lMin = -32768;
            range.lMax = 32767;
            if (FAILED(dev->SetDataFormat(&c_dfDIJoystick2)) ||
                FAILED(dev->SetCooperativeLevel(wnd, DISCL_BACKGROUND | DISCL_NONEXCLUSIVE)))
            {
                dev->Release();
                return false;
            }
            dev->SetProperty(DIPROP_RANGE, &range.diph);
            DIDEVICEOBJECTINSTANCEW oi = {};
            oi.dwSize = sizeof(oi);
            g_di.triggers = SUCCEEDED(dev->GetObjectInfo(&oi, DIJOFS_RX, DIPH_BYOFFSET)) &&
                            SUCCEEDED(dev->GetObjectInfo(&oi, DIJOFS_RY, DIPH_BYOFFSET));
            dev->Acquire();
            g_di.dev = dev;
            LOG("Controller verbunden (DirectInput): %ls, VID %04X PID %04X%s", g_di.name, g_di.vid, g_di.pid,
                g_di.triggers ? ", analoge Trigger" : "");
            // Für die Vibration: Pfad des HID-Geräts hinter dem DirectInput-Gerät
            DIPROPGUIDANDPATH path = {};
            path.diph.dwSize = sizeof(path);
            path.diph.dwHeaderSize = sizeof(DIPROPHEADER);
            path.diph.dwHow = DIPH_DEVICE;
            if (SUCCEEDED(dev->GetProperty(DIPROP_GUIDANDPATH, &path.diph)))
                rumble::UseHid(path.wszPath, g_di.vid, g_di.pid, g_di.name);
            return true;
        }

        // Tastenbelegung wie beim PS4/PS5-Pad: 0 Quadrat, 1 Kreuz, 2 Kreis, 3 Dreieck, 4 L1, 5 R1, 6 L2, 7 R2,
        // 8 Share, 9 Options, 10 L3, 11 R3, 12 PS, 13 Touchpad; linker Stick X/Y, rechter Z/Rz, Trigger Rx/Ry
        bool PollDirectInput(XINPUT_STATE& s)
        {
            if (!g_di.create)
                return false;
            if (!g_di.dev && (!g_di.found || !DiOpen()))
            {
                if (g_di.found && !g_di.dev)
                    InterlockedExchange(&g_di.found, 0); // Öffnen fehlgeschlagen, später neu suchen
                return false;
            }
            DIJOYSTATE2 j = {};
            if (FAILED(g_di.dev->Poll()) && FAILED(g_di.dev->Acquire()))
            {
                DiRelease();
                return false;
            }
            if (FAILED(g_di.dev->GetDeviceState(sizeof(j), &j)))
            {
                if (FAILED(g_di.dev->Acquire()) || FAILED(g_di.dev->GetDeviceState(sizeof(j), &j)))
                {
                    DiRelease();
                    return false;
                }
            }
            const auto axis = [](LONG v) { return (SHORT)std::min(32767L, std::max(-32768L, v)); };
            const auto down = [&](int i) { return (j.rgbButtons[i] & 0x80) != 0; };
            XINPUT_GAMEPAD& g = s.Gamepad;
            g.sThumbLX = axis(j.lX);
            g.sThumbLY = axis(-j.lY);
            g.sThumbRX = axis(j.lZ);
            g.sThumbRY = axis(-j.lRz);
            g.bLeftTrigger = (BYTE)(g_di.triggers ? std::min(255L, std::max(0L, (j.lRx + 32768) / 257)) : 0);
            g.bRightTrigger = (BYTE)(g_di.triggers ? std::min(255L, std::max(0L, (j.lRy + 32768) / 257)) : 0);
            if (down(6))
                g.bLeftTrigger = 255;
            if (down(7))
                g.bRightTrigger = 255;
            static const WORD kButtons[14] = {
                XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_Y,
                XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, 0,
                XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
                0, XINPUT_GAMEPAD_BACK,
            };
            for (int i = 0; i < 14; ++i)
                if (down(i))
                    g.wButtons |= kButtons[i];
            const DWORD pov = j.rgdwPOV[0];
            if (LOWORD(pov) != 0xFFFF)
            {
                const int dir = (int)((pov + 2250) / 4500) % 8; // 0 oben, 2 rechts, 4 unten, 6 links
                if (dir == 7 || dir <= 1) g.wButtons |= XINPUT_GAMEPAD_DPAD_UP;
                if (dir >= 1 && dir <= 3) g.wButtons |= XINPUT_GAMEPAD_DPAD_RIGHT;
                if (dir >= 3 && dir <= 5) g.wButtons |= XINPUT_GAMEPAD_DPAD_DOWN;
                if (dir >= 5 && dir <= 7) g.wButtons |= XINPUT_GAMEPAD_DPAD_LEFT;
            }
            return true;
        }

        bool PollPad(XINPUT_STATE& s)
        {
            if (!g_cfg.controller)
                return false;
            ZeroMemory(&s, sizeof(s));
            // XInput zuerst (Xbox-Pads, aber auch DS4Windows/Steam); bei nicht verbundenem Pad nur alle 2 s fragen
            const DWORD now = GetTickCount();
            if (g_xinput && (g_padConnected || now - g_padRetry >= 2000))
            {
                g_padRetry = now;
                const bool ok = g_xinput(0, &s) == ERROR_SUCCESS;
                if (ok != g_padConnected)
                {
                    LOG("Controller %s (XInput)", ok ? "verbunden" : "getrennt");
                    rumble::UseXInput(ok ? g_xinputSet : nullptr);
                }
                g_padConnected = ok;
                if (ok)
                    return true;
                ZeroMemory(&s, sizeof(s));
            }
            return !g_padConnected && PollDirectInput(s);
        }

        // XInput und DirectInput beim ersten Bedarf im Spiel-Thread laden (nicht in DllMain)
        void EnsurePadInit()
        {
            static bool s_tried = false;
            if (s_tried || !g_cfg.controller)
                return;
            s_tried = true;
            for (const wchar_t* dll : { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" })
            {
                if (HMODULE m = LoadLibraryW(dll))
                {
                    g_xinput = reinterpret_cast<XInputGetState_t>(GetProcAddress(m, "XInputGetState"));
                    g_xinputSet = reinterpret_cast<rumble::XInputSetState_t>(GetProcAddress(m, "XInputSetState"));
                    if (g_xinput)
                    {
                        LOG("XInput geladen: %ls", dll);
                        break;
                    }
                }
            }
            // DirectInput direkt aus der DLL holen, cnc-ddraw hängt sich sonst in DirectInput8Create
            if (HMODULE m = LoadLibraryW(L"dinput8.dll"))
                g_di.create = reinterpret_cast<DirectInput8Create_t>(hooks::FindExport(m, "DirectInput8Create"));
            if (g_di.create)
            {
                if (HANDLE t = CreateThread(nullptr, 0, &DiSearchThread, nullptr, 0, nullptr))
                    CloseHandle(t);
            }
            else
                LOG("DirectInput nicht verfügbar - nur XInput-Controller");
        }

        // ---- Hauptmenü des Spiels: Controller als Tastatur ----
        // Das Hauptmenü hat eine eigene Schleife ohne Eingabepaket; es liest nur Tastenereignisse. Darum
        // schicken wir dem Spielfenster für Steuerkreuz/Stick und Tasten echte Tastendrücke.
        WORD  g_frontPrev = 0;
        int   g_frontDir = 0;          // gehaltene Richtung (VK) für die Wiederholung
        DWORD g_frontRepeat = 0, g_frontPoll = 0;

        void PostKey(HWND wnd, int vk)
        {
            static const int kExtended[] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN };
            LPARAM lp = 1 | (MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
            for (int e : kExtended)
                if (vk == e)
                    lp |= 1 << 24;
            PostMessageW(wnd, WM_KEYDOWN, vk, lp);
            PostMessageW(wnd, WM_KEYUP, vk, lp | (1u << 30) | (1u << 31));
        }

        float Stick(SHORT v, SHORT deadzone);

        void FrontendPad()
        {
            const DWORD now = GetTickCount();
            if (now - g_frontPoll < 15)
                return;
            g_frontPoll = now;
            EnsurePadInit();
            rumble::Tick(false, 0); // außerhalb der Levels steht das Pad still
            XINPUT_STATE s = {};
            HWND wnd = GameHasFocus() ? GameWindow() : nullptr;
            if (!wnd || !PollPad(s))
            {
                g_frontPrev = 0;
                g_frontDir = 0;
                return;
            }
            const XINPUT_GAMEPAD& g = s.Gamepad;
            const WORD pressed = g.wButtons & ~g_frontPrev;
            g_frontPrev = g.wButtons;
            if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START))
                PostKey(wnd, VK_RETURN);
            if (pressed & XINPUT_GAMEPAD_B)
                PostKey(wnd, VK_ESCAPE);

            const float lx = Stick(g.sThumbLX, 16000), ly = Stick(g.sThumbLY, 16000);
            const int dir = (g.wButtons & XINPUT_GAMEPAD_DPAD_UP) || ly > 0 ? VK_UP
                          : (g.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) || ly < 0 ? VK_DOWN
                          : (g.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) || lx < 0 ? VK_LEFT
                          : (g.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > 0 ? VK_RIGHT : 0;
            if (dir != g_frontDir)
            {
                g_frontDir = dir;
                if (dir)
                {
                    PostKey(wnd, dir);
                    g_frontRepeat = now + 400;
                }
            }
            else if (dir && (int)(now - g_frontRepeat) >= 0)
            {
                PostKey(wnd, dir);
                g_frontRepeat = now + 150;
            }
        }

        float Stick(SHORT v, SHORT deadzone)
        {
            if (std::abs((int)v) < deadzone)
                return 0.0f;
            const float f = (std::abs((int)v) - deadzone) / (32767.0f - deadzone);
            return (v < 0 ? -1.0f : 1.0f) * std::min(1.0f, f);
        }

        // Controller im Menü: Steuerkreuz, A/B und Schultertasten wie die entsprechenden Tasten
        void PadMenu(const XINPUT_STATE& s, bool menuOpen)
        {
            const WORD b = s.Gamepad.wButtons;
            const WORD pressed = b & ~g_padPrev;
            if (!menuOpen)
            {
                if (pressed & XINPUT_GAMEPAD_START)
                    overlay::RequestOpen(false);
                else if (pressed & XINPUT_GAMEPAD_BACK)
                    overlay::RequestOpen(true);
                if (pressed & XINPUT_GAMEPAD_RIGHT_THUMB)
                    overlay::ToggleMinimap();
                return;
            }
            const float lx = Stick(s.Gamepad.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            const float ly = Stick(s.Gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            overlay::SetHeld(VK_LEFT, (b & XINPUT_GAMEPAD_DPAD_LEFT) || lx < -0.5f);
            overlay::SetHeld(VK_RIGHT, (b & XINPUT_GAMEPAD_DPAD_RIGHT) || lx > 0.5f);
            overlay::SetHeld(VK_UP, (b & XINPUT_GAMEPAD_DPAD_UP) || ly > 0.5f);
            overlay::SetHeld(VK_DOWN, (b & XINPUT_GAMEPAD_DPAD_DOWN) || ly < -0.5f);

            struct Map { WORD button; int vk; };
            static const Map kMap[] = {
                { XINPUT_GAMEPAD_DPAD_UP, VK_UP }, { XINPUT_GAMEPAD_DPAD_DOWN, VK_DOWN },
                { XINPUT_GAMEPAD_DPAD_LEFT, VK_LEFT }, { XINPUT_GAMEPAD_DPAD_RIGHT, VK_RIGHT },
                { XINPUT_GAMEPAD_A, VK_RETURN }, { XINPUT_GAMEPAD_B, VK_ESCAPE }, { XINPUT_GAMEPAD_START, VK_ESCAPE },
                { XINPUT_GAMEPAD_LEFT_SHOULDER, VK_SUBTRACT }, { XINPUT_GAMEPAD_RIGHT_SHOULDER, VK_ADD },
                { XINPUT_GAMEPAD_X, VK_TAB }, { XINPUT_GAMEPAD_Y, VK_DELETE }, { XINPUT_GAMEPAD_RIGHT_THUMB, VK_SPACE },
            };
            for (const Map& m : kMap)
                if (pressed & m.button)
                    overlay::InjectKey(m.vk);
            if (pressed & XINPUT_GAMEPAD_BACK)
                overlay::InjectKey(g_cfg.mapKey);
        }

        // Maus (Client-Koordinaten aus den Fenstermeldungen) auf Spielkoordinaten abbilden;
        // cnc-ddraw skaliert das Bild mit festem Seitenverhältnis in die Fenstermitte
        bool MouseToGame(float& x, float& y)
        {
            int gw = 0, gh = 0;
            RECT rc = {};
            if (!g_haveMouse || !g_getClientRect || !overlay::ScreenSize(gw, gh) || !g_getClientRect(g_mouseWnd, &rc) ||
                rc.right <= 0 || rc.bottom <= 0)
                return false;
            g_dbg.client = rc;
            const float scale = std::min(rc.right / (float)gw, rc.bottom / (float)gh);
            x = (g_mouse.x - (rc.right - gw * scale) * 0.5f) / scale;
            y = (g_mouse.y - (rc.bottom - gh * scale) * 0.5f) / scale;
            return true;
        }

        // -1..1: wie weit eine Mauskoordinate über den inneren Bildbereich hinaus Richtung Rand steht
        float EdgePull(float pos, int size)
        {
            const float half = size * 0.5f;
            const float u = (pos - half) / half;
            const float dead = g_cfg.cameraDeadZone / 100.0f;
            float t = std::clamp((std::abs(u) - dead) / (1.0f - dead), 0.0f, 1.0f);
            t = t * t * (3.0f - 2.0f * t); // weich einsetzen
            return u < 0 ? -t : t;
        }

        // Kamera Richtung Mauszeiger schieben; false, solange der Zeiger im inneren Bereich steht
        bool MouseCamera(int range, int gw, int gh)
        {
            const float px = EdgePull(g_crossX, gw), py = EdgePull(g_crossY, gh);
            g_camTargetX = px * range / 100.0f * gw * 0.5f;
            g_camTargetY = py * range / 100.0f * gh * 0.5f;
            return px != 0 || py != 0;
        }

        // Kamera mit dem rechten Stick (Ausschlag -1..1, schon ohne Totzone)
        void StickCamera(float x, float y, int range)
        {
            int gw = 0, gh = 0;
            if (!g_cfg.camera || !overlay::ScreenSize(gw, gh))
                return;
            const float mag = std::sqrt(x * x + y * y);
            if (mag > 1.0f)
            {
                x /= mag;
                y /= mag;
            }
            g_camTargetX = x * range / 100.0f * gw * 0.5f;
            g_camTargetY = y * range / 100.0f * gh * 0.5f;
            g_lookCursor = false;
        }

        // Fadenkreuz für den rechten Stick: ein Stück von der Figur entfernt in Zielrichtung, also dort, wohin
        // sie schießt
        void StickCrosshair(float x, float y)
        {
            int gw = 0, gh = 0;
            const float mag = std::sqrt(x * x + y * y);
            if (mag <= 0 || !overlay::ScreenSize(gw, gh))
                return;
            float px = gw * 0.5f, py = gh * 0.5f;
            gta1::PlayerScreenPos(px, py);
            const float r = gh * 0.17f;
            g_crossX = px + x / mag * r;
            g_crossY = py + y / mag * r;
            g_crossStick = true;
            g_crosshair = true;
        }

        void GatherKeyboardMouse(Intent& in, bool onFoot)
        {
            g_crosshair = false;
            if (!g_cfg.twinStick)
                return;

            // F (bzw. [Keys] EnterExit) zum Ein- und Aussteigen, einmal pro Tastendruck
            const bool enterKey = Key(g_cfg.enterExitKey);
            if (enterKey && !g_enterHeld)
                in.enterExit = true;
            g_enterHeld = enterKey;

            if (!onFoot)
            {
                // Umschauen: Mauszeiger Richtung Bildrand verschiebt die Kamera
                int gw = 0, gh = 0;
                if (!g_padActive && g_cfg.camera && g_cfg.cameraMouseVehicle && overlay::ScreenSize(gw, gh) && MouseToGame(g_crossX, g_crossY))
                {
                    const bool pulling = MouseCamera(g_cfg.cameraRangeVehicle, gw, gh);
                    g_lookCursor = pulling || GetTickCount() - g_mouseMoved < 2500;
                }

                // Im Fahrzeug wie mit den Pfeiltasten: volle Lenkung, Gas bzw. Bremse/Rückwärts
                if (!g_cfg.wasdDriving)
                    return;
                const int steer = (Key(g_cfg.moveRightKey) ? 7 : 0) - (Key(g_cfg.moveLeftKey) ? 7 : 0);
                const int throttle = Key(g_cfg.moveUpKey) ? 3 : Key(g_cfg.moveDownKey) ? -3 : 0;
                if (steer || throttle)
                {
                    in.drive = true;
                    in.steer = steer;
                    in.throttle = throttle;
                }
                return;
            }

            float mx = 0, my = 0;
            if (Key(g_cfg.moveRightKey)) mx += 1;
            if (Key(g_cfg.moveLeftKey)) mx -= 1;
            if (Key(g_cfg.moveDownKey)) my += 1;
            if (Key(g_cfg.moveUpKey)) my -= 1;
            const bool moving = mx != 0 || my != 0;

            int gw = 0, gh = 0;
            const bool haveMouse = !g_padActive && overlay::ScreenSize(gw, gh) && MouseToGame(g_crossX, g_crossY);
            g_dbg.haveMouse = haveMouse;
            float ax = 0, ay = 0;
            if (haveMouse)
            {
                g_crossStick = false;
                // Zu Fuß steht die Figur in der Bildmitte, außer die Kamera ist verschoben
                float px = gw * 0.5f, py = gh * 0.5f;
                gta1::PlayerScreenPos(px, py);
                // Kippkamera: der Zeiger steht im gekippten Bild, gezielt wird im Spielbild
                float gx = g_crossX, gy = g_crossY;
                tiltcam::OutputToGame(g_crossX, g_crossY, gx, gy);
                ax = gx - px;
                ay = gy - py;
            }
            const bool lmb = Key(VK_LBUTTON), rmb = Key(VK_RBUTTON);
            const bool aiming = haveMouse && (g_cfg.alwaysAim || lmb || rmb) && ax * ax + ay * ay > 16.0f;
            const bool pulling = haveMouse && g_cfg.camera && MouseCamera(g_cfg.cameraRangeFoot, gw, gh);
            g_crosshair = haveMouse && (aiming || pulling || GetTickCount() - g_mouseMoved < 2500);

            if (!moving && !aiming)
                return;
            in.drive = true;
            in.steer = 0;
            in.angleValid = true;
            if (aiming)
            {
                const int aim = AngleFromScreen(ax, ay);
                in.fire = lmb;
                if (moving && g_cfg.alwaysAim)
                {
                    // Das Spiel bewegt Fußgänger nur in Blickrichtung: für die Simulation in Laufrichtung
                    // drehen und erst direkt vor dem Zeichnen wieder zum Fadenkreuz
                    in.angle = AngleFromScreen(mx, my);
                    in.throttle = 3;
                    in.renderAngle = aim;
                }
                else
                {
                    in.angle = aim;
                    if (moving)
                    {
                        // GTA1-Fußgänger laufen nur vor/zurück: Laufrichtung auf die Blickrichtung projizieren
                        const float d = (mx * ax + my * ay) / (std::sqrt(mx * mx + my * my) * std::sqrt(ax * ax + ay * ay));
                        in.throttle = d > 0.35f ? 3 : d < -0.35f ? -3 : 0;
                    }
                }
            }
            else
            {
                in.angle = AngleFromScreen(mx, my);
                in.throttle = 3;
            }
        }

        // Waffenwechsel mit eigenen Tasten und dem Mausrad, zusätzlich zu den Tasten aus den Spieleinstellungen
        // (dort Y/X unten links, bei WASD kaum erreichbar)
        void GatherWeapons(Intent& in)
        {
            in.weaponPrev |= Key(g_cfg.prevWeaponKey);
            in.weaponNext |= Key(g_cfg.nextWeaponKey);
            if (g_wheelPulse)
            {
                g_wheelPulse = false;
                return;
            }
            if (g_wheel != 0)
            {
                (g_wheel > 0 ? in.weaponNext : in.weaponPrev) = true;
                g_wheel += g_wheel > 0 ? -1 : 1;
                g_wheelPulse = true;
            }
        }

        void GatherPad(Intent& in, const XINPUT_STATE& s, bool onFoot)
        {
            const XINPUT_GAMEPAD& g = s.Gamepad;
            const WORD pressed = g.wButtons & ~g_padPrev;
            const float lx = Stick(g.sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
            const float ly = -Stick(g.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE); // Bildschirm: unten positiv
            const float rx = Stick(g.sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            const float ry = -Stick(g.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
            const float rt = g.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? g.bRightTrigger / 255.0f : 0.0f;
            const float lt = g.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD ? g.bLeftTrigger / 255.0f : 0.0f;

            if (lx != 0 || ly != 0 || rx != 0 || ry != 0 || rt > 0 || lt > 0 || g.wButtons)
                g_padActive = true;

            // Namen nach Xbox-Layout; PS4/PS5: A = Kreuz, B = Kreis, X = Quadrat, Y = Dreieck
            if (pressed & XINPUT_GAMEPAD_Y)
                in.enterExit = true;
            in.jump |= (g.wButtons & XINPUT_GAMEPAD_A) != 0;
            // Quadrat/X: zu Fuß springen wie in späteren GTA-Teilen, im Fahrzeug hupen
            if (onFoot)
                in.jump |= (g.wButtons & XINPUT_GAMEPAD_X) != 0;
            else
                in.horn |= (g.wButtons & XINPUT_GAMEPAD_X) != 0;
            in.weaponPrev |= (g.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            in.weaponNext |= (g.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;

            if (onFoot)
            {
                in.fire |= rt > 0 || (g.wButtons & XINPUT_GAMEPAD_B);
                const bool moving = lx != 0 || ly != 0;
                const bool aiming = rx != 0 || ry != 0;
                if (aiming)
                {
                    StickCamera(rx, ry, g_cfg.cameraRangeFoot); // in Zielrichtung vorausschauen
                    StickCrosshair(rx, ry);
                }
                if (!moving && !aiming)
                    return;
                in.drive = true;
                in.steer = 0;
                in.angleValid = true;
                if (aiming && moving && g_cfg.alwaysAim)
                {
                    const float mag = std::min(1.0f, std::sqrt(lx * lx + ly * ly));
                    in.angle = AngleFromScreen(lx, ly);
                    in.throttle = std::max(1, (int)std::lround(mag * 3));
                    in.renderAngle = AngleFromScreen(rx, ry);
                }
                else if (aiming)
                {
                    in.angle = AngleFromScreen(rx, ry);
                    const float d = moving ? (lx * rx + ly * ry) / (std::sqrt(lx * lx + ly * ly) * std::sqrt(rx * rx + ry * ry)) : 0.0f;
                    in.throttle = !moving ? 0 : d > 0.35f ? 3 : d < -0.35f ? -3 : 0;
                }
                else
                {
                    in.angle = AngleFromScreen(lx, ly);
                    const float mag = std::min(1.0f, std::sqrt(lx * lx + ly * ly));
                    in.throttle = std::max(1, (int)std::lround(mag * 3));
                }
            }
            else
            {
                // Im Fahrzeug: analog lenken (das Spiel kennt intern -7..7) und dosiert Gas geben
                in.fire |= (g.wButtons & XINPUT_GAMEPAD_B) != 0; // Hupe
                if (rx != 0 || ry != 0)
                    StickCamera(rx, ry, g_cfg.cameraRangeVehicle); // umschauen
                const int steer = (int)std::lround(lx * 7);
                const int throttle = (int)std::lround(rt * 3) - (int)std::lround(lt * 3);
                if (steer != 0 || throttle != 0)
                {
                    in.drive = true;
                    in.steer = steer;
                    in.throttle = throttle;
                }
            }
        }

        uint32_t WriteSteer(uint32_t p, int steer)
        {
            p |= kSteer;
            return (p & ~kSteerBits) | ((uint32_t)(steer & 0xF) << 9);
        }

        uint32_t WriteThrottle(uint32_t p, int t)
        {
            // Gas und Bremse teilen sich im Spieler dieselben Felder, also nur eines von beiden schreiben
            if (t >= 0)
                return ((p | kGas) & ~(kGasBits | kBrake)) | ((uint32_t)(t & 3) << 15);
            return ((p | kBrake) & ~(kBrakeBits | kGas)) | ((uint32_t)((-t) & 3) << 13);
        }

        uint32_t WriteHeld(uint32_t p, uint32_t change, uint32_t held, bool want, bool& sent)
        {
            if (want != sent || (p & change))
            {
                p |= change;
                p = want ? (p | held) : (p & ~held);
                sent = want;
            }
            return p;
        }
    }

    void Init()
    {
        g_xinput = nullptr; // wird beim ersten Frame im Spiel-Thread geladen (nicht in DllMain)

        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        g_getClientRect = reinterpret_cast<GetClientRect_t>(hooks::FindExport(user32, "GetClientRect"));
        g_getForegroundWindow = reinterpret_cast<GetForegroundWindow_t>(hooks::FindExport(user32, "GetForegroundWindow"));
        if (!g_getClientRect)
            g_getClientRect = &::GetClientRect;
        if (!g_getForegroundWindow)
            g_getForegroundWindow = &::GetForegroundWindow;
    }

    uint32_t ProcessPacket(uint32_t packet)
    {
        static bool s_checkedImports = false;
        if (!s_checkedImports)
        {
            s_checkedImports = true;
            if (reinterpret_cast<void*>(&::GetClientRect) != reinterpret_cast<void*>(g_getClientRect))
                LOG("GetClientRect ist in unserer ASI umgeleitet (cnc-ddraw) - verwende die echte Funktion");
        }

        EnsurePadInit();

        const uint32_t packetIn = packet;
        const gta1::KeyboardInput kb = gta1::GetKeyboardInput();
        const bool menu = overlay::MenuActive();
        const bool onFoot = gta1::IsPlayerOnFoot();

        // Beim Wechsel in ein anderes Fenster kommen keine Loslass-Meldungen mehr an
        const bool focus = GameHasFocus();
        if (g_hadFocus && !focus)
            memset(g_down, 0, sizeof(g_down));
        g_hadFocus = focus;

        XINPUT_STATE pad = {};
        const bool havePad = focus && PollPad(pad);
        // Vibration nur, solange wirklich mit dem Controller gespielt wird
        rumble::Tick(havePad && g_padActive && gta1::InGame() && !menu && gta1::RunState() == 1, g_cfg.vibration);
        if (havePad)
            PadMenu(pad, menu);

        Intent in;
        if (!menu && gta1::InGame())
        {
            g_camTargetX = g_camTargetY = 0;
            g_lookCursor = false;
            GatherKeyboardMouse(in, onFoot);
            if (havePad)
                GatherPad(in, pad, onFoot);
            if (focus)
                GatherWeapons(in);
            if (!focus)
            {
                g_camTargetX = g_camTargetY = 0;
                g_lookCursor = false;
            }
            camera::SetTarget(g_camTargetX, g_camTargetY);
        }
        else
        {
            // Im Pausemenü bleibt die Kamera stehen, nach dem Level beginnt sie wieder mittig
            g_crosshair = false;
            g_lookCursor = false;
            if (!gta1::InGame())
                camera::Reset();
        }
        g_padPrev = havePad ? pad.Gamepad.wButtons : 0;

        // Feuer löschen: brennend wirft die Sprungtaste die Figur zu Boden; solange sie sich wälzt, bleibt
        // sie liegen (keine Bewegung, kein Schuss, kein Einsteigen)
        const bool jumpHeld = kb.jump || in.jump;
        const bool jumpForFire = fire::OnJump(jumpHeld && !menu && gta1::InGame());
        const bool lying = fire::Rolling();
        if (lying)
        {
            in = Intent();
            in.drive = true; // Lenkung und Gas 0
        }

        // Lenkung/Gas: solange wir steuern, sonst einmal den echten Tastaturzustand zurückschreiben
        if (in.drive || g_driving)
        {
            const int steer = in.drive ? in.steer : kb.steer;
            const int throttle = in.drive ? in.throttle : (kb.brake > 0 ? -kb.brake : kb.gas);
            packet = WriteThrottle(WriteSteer(packet, steer), throttle);
            g_driving = in.drive;
        }
        if (in.angleValid)
            gta1::SetPlayerPedAngle(in.angle);
        g_renderAngle = in.renderAngle;
        gta1::SetPlayerFireAngle(in.renderAngle);

        packet = WriteHeld(packet, kFire, kFireHeld, !lying && (kb.fire || in.fire), g_sentFire);
        packet = WriteHeld(packet, kJump, kJumpHeld, !jumpForFire && jumpHeld, g_sentJump);
        packet = WriteHeld(packet, kWeaponNext, kWeaponNextHeld, !lying && (kb.weaponNext || in.weaponNext), g_sentWeaponNext);
        packet = WriteHeld(packet, kWeaponPrev, kWeaponPrevHeld, !lying && (kb.weaponPrev || in.weaponPrev), g_sentWeaponPrev);
        packet = WriteHeld(packet, kHorn, kHornHeld, !lying && (kb.horn || in.horn), g_sentHorn);
        if (lying)
            packet &= ~kEnterExit;
        else if (in.enterExit)
            packet |= kEnterExit;

        if (g_cfg.showInfo)
        {
            ++g_dbg.calls;
            g_dbg.focus = focus;
            g_dbg.menu = menu;
            g_dbg.inGame = gta1::InGame();
            g_dbg.onFoot = onFoot;
            g_dbg.in = in;
            g_dbg.packetIn = packetIn;
            g_dbg.packetOut = packet;
        }
        return packet;
    }

    void DebugLines(wchar_t* line1, wchar_t* line2, size_t size)
    {
        const Intent& in = g_dbg.in;
        float camX = 0, camY = 0;
        camera::Current(camX, camY);
        swprintf_s(line1, size, L"Eingabe #%u   Fokus %d   Menü %d   Spiel %d   zu Fuß %d   Tasten %c%c%c%c%s%s   Kamera (%.0f, %.0f)",
                   g_dbg.calls, g_dbg.focus, g_dbg.menu, g_dbg.inGame, g_dbg.onFoot,
                   Key(g_cfg.moveUpKey) ? L'W' : L'-', Key(g_cfg.moveLeftKey) ? L'A' : L'-',
                   Key(g_cfg.moveDownKey) ? L'S' : L'-', Key(g_cfg.moveRightKey) ? L'D' : L'-',
                   Key(VK_LBUTTON) ? L" LMB" : L"", Key(VK_RBUTTON) ? L" RMB" : L"", camX, camY);
        swprintf_s(line2, size, L"Maus %d (%d, %d) in %dx%d -> (%.0f, %.0f)   Steuern %d  Lenk %d  Gas %d  Winkel %d   Paket %08X -> %08X",
                   g_dbg.haveMouse, g_mouse.x, g_mouse.y, g_dbg.client.right, g_dbg.client.bottom, g_crossX, g_crossY,
                   in.drive, in.steer, in.throttle, in.angleValid ? in.angle : -1, g_dbg.packetIn, g_dbg.packetOut);
    }

    bool MousePosition(float& x, float& y) { return MouseToGame(x, y); }

    bool PadConnected() { return g_cfg.controller && (g_padConnected || g_di.dev != nullptr); }

    void OnPump()
    {
        // Nur außerhalb der Levels; im Level läuft der Controller über das Eingabepaket
        if (g_cfg.controller && gta1::Supported() && !gta1::InGame() && !overlay::MenuActive())
            FrontendPad();
        else
            g_frontPrev = 0xFFFF; // gehaltene Tasten beim Wechsel ins Menü nicht als neuen Druck werten
    }

    void BeforeRender()
    {
        if (g_renderAngle >= 0 && !overlay::MenuActive())
            gta1::SetPlayerPedAngle(g_renderAngle);
    }

    bool Crosshair(float& x, float& y)
    {
        if (!g_crosshair)
            return false;
        x = g_crossX;
        y = g_crossY;
        if (g_crossStick)
            tiltcam::GameToOutput(g_crossX, g_crossY, x, y);
        return true;
    }

    bool LookCursor(float& x, float& y)
    {
        if (!g_lookCursor)
            return false;
        x = g_crossX;
        y = g_crossY;
        return true;
    }


    void OnMessage(const MSG& msg)
    {
        switch (msg.message)
        {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (msg.wParam < 256)
                g_down[msg.wParam] = true;
            for (int vk : { g_cfg.moveUpKey, g_cfg.moveLeftKey, g_cfg.moveDownKey, g_cfg.moveRightKey })
                if ((int)msg.wParam == vk)
                    g_padActive = false;
            return;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (msg.wParam < 256)
                g_down[msg.wParam] = false;
            return;
        // Klicks ins Pausemenü sollen nach dem Schließen nicht als Schuss weiterlaufen
        case WM_LBUTTONDOWN: g_down[VK_LBUTTON] = !overlay::MenuActive(); break;
        case WM_LBUTTONUP:   g_down[VK_LBUTTON] = false; break;
        case WM_RBUTTONDOWN: g_down[VK_RBUTTON] = !overlay::MenuActive(); break;
        case WM_RBUTTONUP:   g_down[VK_RBUTTON] = false; break;
        case WM_MOUSEMOVE:   break;
        case WM_MOUSEWHEEL:
            if (!overlay::MenuActive() && gta1::InGame())
                g_wheel = std::clamp(g_wheel - GET_WHEEL_DELTA_WPARAM(msg.wParam) / WHEEL_DELTA, -3, 3);
            return;
        default:
            return;
        }
        // Mausmeldungen tragen die Position in Client-Koordinaten. Nur eine echte Bewegung oder ein Klick
        // macht die Maus wieder zum aktiven Gerät (manche Wrapper schicken WM_MOUSEMOVE auch ohne Bewegung).
        const POINT pos = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
        if (msg.message != WM_MOUSEMOVE || std::abs(pos.x - g_mouse.x) + std::abs(pos.y - g_mouse.y) > 3)
            g_padActive = false;
        g_mouse = pos;
        if (!g_haveMouse)
            LOG("Erste Mausmeldung 0x%X bei (%d, %d)", msg.message, g_mouse.x, g_mouse.y);
        g_mouseWnd = msg.hwnd;
        g_haveMouse = msg.hwnd != nullptr;
        g_mouseMoved = GetTickCount();
    }
}

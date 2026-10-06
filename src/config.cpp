#include "config.h"

#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <string>

Config g_cfg;

static const int kConfigVersion = 22;

static const char kDefaultIni[] =
    "; GTA1Plus - Settings\r\n"
    "; Keys: a letter/digit (e.g. M) or a virtual key code (e.g. 0x4D)\r\n"
    "\r\n"
    "[Info]\r\n"
    "; Managed by the mod, please do not change\r\n"
    "Version=22\r\n"
    "\r\n"
    "[Language]\r\n"
    "; Text language: auto = same as the game setting, otherwise de, en, fr, it\r\n"
    "; (the texts are in GTA1Plus_lang.ini and can be edited there)\r\n"
    "Language=auto\r\n"
    "\r\n"
    "[Features]\r\n"
    "; Every feature can be switched off on its own (0 = off, 1 = on)\r\n"
    "; New pause menu with a city map\r\n"
    "PauseMenu=1\r\n"
    "; ESC opens the new pause menu (with \"Quit level\") instead of the game's quit prompt\r\n"
    "EscOpensMenu=1\r\n"
    "; Round minimap (show/hide with the minimap key)\r\n"
    "Minimap=1\r\n"
    "; Show the minimap right from the start of a level\r\n"
    "MinimapOnStart=0\r\n"
    "; Short hint after a level starts\r\n"
    "Hint=1\r\n"
    "; GPS: route to the waypoint or mission target on the map and minimap\r\n"
    "GPS=1\r\n"
    "; GPS: blue arrow at the top of the screen, points to the next turn\r\n"
    "GPSArrow=1\r\n"
    "; Place your own waypoints on the map\r\n"
    "Waypoints=1\r\n"
    "; On foot: move in screen directions with WASD, aim with the mouse (left click fires, right click aims)\r\n"
    "; The arrow keys keep working as usual\r\n"
    "TwinStick=1\r\n"
    "; Twin-stick: the character always faces the crosshair and also walks sideways/backwards\r\n"
    "; (0 = aim only while holding the right mouse button; the walk animation may not always match)\r\n"
    "AlwaysAim=0\r\n"
    "; With TwinStick=1, also drive vehicles with WASD (the arrow keys still work)\r\n"
    "WASDDriving=1\r\n"
    "; Controller: Xbox (XInput) plus PS4/PS5 and other DirectInput pads.\r\n"
    "; Left stick walk/steer, right stick aim or look around in a vehicle, R2/RT fire or accelerate,\r\n"
    "; L2/LT brake, Triangle/Y enter/exit vehicle, Cross/A handbrake/jump, Square/X jump (horn in a vehicle),\r\n"
    "; L1/R1 switch weapon, Circle/B fire (horn in a vehicle), Options/Start pause menu, Share/Back map.\r\n"
    "; The controller also works in the main menu\r\n"
    "Controller=1\r\n"
    "; Controller vibration for your own shots, hits you take, crashes and nearby explosions: strength in\r\n"
    "; percent, 0 = off. Works with Xbox pads and with PS4/PS5 pads (USB or Bluetooth)\r\n"
    "Vibration=70\r\n"
    "; Color the character to match the portrait picked in the main menu (colors under [PlayerColors])\r\n"
    "PortraitColors=1\r\n"
    "; Show the character's health as five hearts below the score\r\n"
    "Hearts=1\r\n"
    "; Show paint shops, bomb shops and car cranes as icons on the map and minimap\r\n"
    "POIs=1\r\n"
    "\r\n"
    "[Gameplay]\r\n"
    "; Do not halve the multiplier when you get busted\r\n"
    "; (in the original game, dying and failed missions do not change it)\r\n"
    "KeepMultiplier=1\r\n"
    "; Trees, lamp posts, the posts of road signs and traffic lights and other thin posts no longer stop\r\n"
    "; vehicles, as in GTA2. Trees and lamps are lifted a little for that, so tree tops look slightly bigger\r\n"
    "; and shift with the camera like roofs. Bushes, benches, hydrants, fences and the like stay solid\r\n"
    "NoTreeCollision=1\r\n"
    "; Steering aid as in GTA2: when you let go of the steering on a road or pavement and the car points almost\r\n"
    "; along a street direction (north, east, south, west), it is gently straightened and then keeps its lane.\r\n"
    "; A short tap on the steering becomes a lane change. The game does this on its own only when the car is\r\n"
    "; less than 11 degrees off - less than one tap turns it - and with a wobble.\r\n"
    "; 0 = off (as in the original game), 1 = gentle (up to 13 degrees off), 2 = medium (18 degrees, one tap),\r\n"
    "; 3 = strong (28 degrees, corrects faster)\r\n"
    "SteerAssist=2\r\n"
    "; Make the four women (Ulrika, Katie, Mikki, Divine) selectable in the main menu - the game locks them\r\n"
    "; in every language version except English\r\n"
    "AllCharacters=1\r\n"
    "; Retry failed missions right away: the wait after \"MISSION FAILED\" is cut to about 1 second,\r\n"
    "; and with phone groups (e.g. Liberty City) the phone of the failed mission rings again,\r\n"
    "; as if it had never been answered\r\n"
    "QuickRetry=1\r\n"
    "; On fire: press the jump key to roll on the ground and put out the flames (a hint appears).\r\n"
    "; While rolling the fire does no more damage. In the original game burning is certain death\r\n"
    "Extinguish=1\r\n"
    "; Save automatically after every completed mission (one save per character and level;\r\n"
    "; save and load by hand in the pause menu)\r\n"
    "Autosave=1\r\n"
    "; Damage in percent of full health (five hearts = 100). In the original game every bullet kills at once\r\n"
    "; and punches do no damage at all.\r\n"
    "; Bullets that hit your character: 100 = deadly as in the original, 35 = the third pistol hit kills.\r\n"
    "; Other characters still die from a single bullet, so missions and kill frenzies play as before\r\n"
    "PistolDamage=35\r\n"
    "MachineGunDamage=20\r\n"
    "; A punch, for everyone who gets hit (you and others): 0 = no damage as in the original\r\n"
    "PunchDamage=10\r\n"
    "\r\n"
    "[Camera]\r\n"
    "; Shift the camera with the mouse or right stick to see further ahead\r\n"
    "; On foot: move the crosshair towards the screen edge, or aim with the right stick\r\n"
    "; In a vehicle: move the mouse towards the screen edge, or push the right stick\r\n"
    "Enabled=1\r\n"
    "; Also use the mouse in vehicles (0 = controller only there). The mouse needs TwinStick=1\r\n"
    "MouseInVehicle=1\r\n"
    "; Maximum camera shift, in percent of half the screen width/height\r\n"
    "RangeOnFoot=50\r\n"
    "RangeInVehicle=60\r\n"
    "; Inner screen area in percent where the mouse does not shift the camera yet\r\n"
    "DeadZone=40\r\n"
    "; Steadier camera: it follows the character more closely instead of drifting behind\r\n"
    "; (the well-known GTAforums hex patch, applied in memory - the exe stays unchanged). Works without Enabled=1\r\n"
    "Steady=1\r\n"
    "; Tilted camera in the style of GTA Chinatown Wars (prototype): 0 = off, 1 = in vehicles, 2 = always.\r\n"
    "; The mod tilts the finished game picture; to see further ahead the game draws the world smaller,\r\n"
    "; so the picture gets softer. Buildings keep their straight top-down look\r\n"
    "Tilt=0\r\n"
    "; Tilt angle in degrees (0..45)\r\n"
    "TiltAngle=35\r\n"
    "; In vehicles the picture turns with the vehicle, so that the driving direction points up\r\n"
    "TiltRotate=1\r\n"
    "; Camera height in vehicles: 0 = as in the game (zooms out with speed), 1 = high, 2 = very high,\r\n"
    "; 3 = maximum (as far out as the game goes at top speed). 1..3 keep that view, also when standing still\r\n"
    "VehicleView=0\r\n"
    "; Traffic appears and disappears outside this range around the camera, in percent of the visible\r\n"
    "; area (100 = as in the game, up to 250). More gives you more time to react to cars\r\n"
    "TrafficRange=100\r\n"
    "\r\n"
    "[PlayerColors]\r\n"
    "; Color variant per portrait, only with PortraitColors=1. 0 = as in the original game (yellow top),\r\n"
    "; 1..63 = variant. All variants with pictures: GTA1Plus_PlayerColors.png (next to this file)\r\n"
    "; Each character has their own family with the same hair/skin (yellow, green, red, blue):\r\n"
    "; 1 Ulrika 25-28, 2 Travis 0-3, 3 Katie 29-32, 4 Mikki 33-36, 5 Divine 37-40,\r\n"
    "; 6 Bubba 41-44, 7 Troy 45-48, 8 Kivlov 49-52\r\n"
    "Character1=0\r\n"
    "Character2=1\r\n"
    "Character3=31\r\n"
    "Character4=35\r\n"
    "Character5=40\r\n"
    "Character6=44\r\n"
    "Character7=0\r\n"
    "Character8=50\r\n"
    "\r\n"
    "[Keys]\r\n"
    "; Opens the pause menu directly on the map\r\n"
    "Map=M\r\n"
    "; Show/hide the minimap\r\n"
    "Minimap=N\r\n"
    "; Twin-stick movement (e.g. change to Z/Q/S/D for AZERTY)\r\n"
    "MoveUp=W\r\n"
    "MoveLeft=A\r\n"
    "MoveDown=S\r\n"
    "MoveRight=D\r\n"
    "; Enter/exit vehicle, in addition to the key from the game settings (only with TwinStick=1)\r\n"
    "EnterExit=F\r\n"
    "; Previous/next weapon, in addition to the keys from the game settings and the mouse wheel\r\n"
    "PrevWeapon=Q\r\n"
    "NextWeapon=E\r\n"
    "\r\n"
    "[Map]\r\n"
    "; Map style: 0 = textures from the game, 1 = schematic\r\n"
    "Style=0\r\n"
    "; Minimap position: 0 = bottom left, 1 = bottom right, 2 = top left, 3 = top right\r\n"
    "MinimapCorner=0\r\n"
    "; Pause the game while the menu is open\r\n"
    "PauseGame=1\r\n"
    "\r\n"
    "[Display]\r\n"
    "; These settings only matter where the game runs at a high resolution in the window mode of the ASI\r\n"
    "; loader (Rockstar Classics release with the widescreen fix); everywhere else they do nothing.\r\n"
    "; Finish every picture in normal memory and hand it to the screen in one piece: no half-drawn\r\n"
    "; pictures (flicker), and the mod's displays are blended in full quality\r\n"
    "FrameBuffer=1\r\n"
    "; Enlarge the main menu to the screen height instead of the small picture in the top left corner\r\n"
    "ScaleMenu=1\r\n"
    "; Size of the game's own displays (score, lives, weapon, phone messages, area names, arrows) and of\r\n"
    "; the mod's hearts. The game draws them in a fixed pixel size, so they get tiny at high resolutions.\r\n"
    "; 0 = automatic: about as large as on the 640x480 screen they were made for (twice the size from\r\n"
    "; 810 lines of screen height, three times from 1350, four times from 1890),\r\n"
    "; 1 = as the game draws them, 2 to 4 = enlarged by that factor. Also in the pause menu (HUD size).\r\n"
    "; Needs FrameBuffer=1\r\n"
    "HudScale=0\r\n"
    "\r\n"
    "[Heading]\r\n"
    "; Heading arrow on the player marker\r\n"
    "Show=1\r\n"
    "; Fine-tuning in case the arrow points the wrong way (game angle 0..1023 = 360 degrees)\r\n"
    "Offset=0\r\n"
    "CounterClockwise=1\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; Write a log file next to the .asi\r\n"
    "Log=1\r\n"
    "; Show debug info on screen\r\n"
    "ShowInfo=0\r\n";

namespace
{
    enum class Kind { Bool, Int, Key, String };

    struct Setting
    {
        const wchar_t* section;
        const wchar_t* key;
        Kind kind;
        void* value;
        const wchar_t* oldSection; // Ort in INI-Version 1
        const wchar_t* oldKey;
    };

    const Setting kSettings[] = {
        { L"Features", L"PauseMenu",      Kind::Bool, &g_cfg.pauseMenu,      nullptr, nullptr },
        { L"Features", L"EscOpensMenu",   Kind::Bool, &g_cfg.escOpensMenu,   nullptr, nullptr },
        { L"Features", L"Minimap",        Kind::Bool, &g_cfg.minimap,        nullptr, nullptr },
        { L"Features", L"MinimapOnStart", Kind::Bool, &g_cfg.minimapOnStart, L"Overlay", L"MinimapEnabled" },
        { L"Features", L"Hint",           Kind::Bool, &g_cfg.hint,           L"Overlay", L"ShowHint" },
        { L"Features", L"GPS",            Kind::Bool, &g_cfg.gps,            nullptr, nullptr },
        { L"Features", L"GPSArrow",       Kind::Bool, &g_cfg.gpsArrow,       nullptr, nullptr },
        { L"Features", L"Waypoints",      Kind::Bool, &g_cfg.waypoints,      nullptr, nullptr },
        { L"Features", L"TwinStick",      Kind::Bool, &g_cfg.twinStick,      nullptr, nullptr },
        { L"Features", L"AlwaysAim",      Kind::Bool, &g_cfg.alwaysAim,      nullptr, nullptr },
        { L"Features", L"WASDDriving",    Kind::Bool, &g_cfg.wasdDriving,    nullptr, nullptr },
        { L"Features", L"Controller",     Kind::Bool, &g_cfg.controller,     nullptr, nullptr },
        { L"Features", L"Vibration",      Kind::Int,  &g_cfg.vibration,      nullptr, nullptr },
        { L"Features", L"PortraitColors", Kind::Bool, &g_cfg.portraitColors, nullptr, nullptr },
        { L"Features", L"Hearts",         Kind::Bool, &g_cfg.hearts,         nullptr, nullptr },
        { L"Features", L"POIs",           Kind::Bool, &g_cfg.pois,           nullptr, nullptr },
        { L"Gameplay", L"KeepMultiplier", Kind::Bool, &g_cfg.keepMultiplier, nullptr, nullptr },
        { L"Gameplay", L"NoTreeCollision", Kind::Bool, &g_cfg.noTreeCollision, nullptr, nullptr },
        { L"Gameplay", L"SteerAssist",    Kind::Int,  &g_cfg.steerAssist,    nullptr, nullptr },
        { L"Gameplay", L"QuickRetry",     Kind::Bool, &g_cfg.quickRetry,     nullptr, nullptr },
        { L"Gameplay", L"AllCharacters",  Kind::Bool, &g_cfg.allCharacters,  nullptr, nullptr },
        { L"Gameplay", L"Extinguish",     Kind::Bool, &g_cfg.extinguish,     nullptr, nullptr },
        { L"Gameplay", L"Autosave",       Kind::Bool, &g_cfg.autosave,       nullptr, nullptr },
        { L"Gameplay", L"PistolDamage",     Kind::Int, &g_cfg.pistolDamage,     nullptr, nullptr },
        { L"Gameplay", L"MachineGunDamage", Kind::Int, &g_cfg.machineGunDamage, nullptr, nullptr },
        { L"Gameplay", L"PunchDamage",      Kind::Int, &g_cfg.punchDamage,      nullptr, nullptr },
        { L"Camera",   L"Enabled",        Kind::Bool, &g_cfg.camera,             nullptr, nullptr },
        { L"Camera",   L"MouseInVehicle", Kind::Bool, &g_cfg.cameraMouseVehicle, nullptr, nullptr },
        { L"Camera",   L"RangeOnFoot",    Kind::Int,  &g_cfg.cameraRangeFoot,    nullptr, nullptr },
        { L"Camera",   L"RangeInVehicle", Kind::Int,  &g_cfg.cameraRangeVehicle, nullptr, nullptr },
        { L"Camera",   L"DeadZone",       Kind::Int,  &g_cfg.cameraDeadZone,     nullptr, nullptr },
        { L"Camera",   L"Steady",         Kind::Bool, &g_cfg.cameraSteady,       nullptr, nullptr },
        { L"Camera",   L"Tilt",           Kind::Int,  &g_cfg.tilt,               nullptr, nullptr },
        { L"Camera",   L"TiltAngle",      Kind::Int,  &g_cfg.tiltAngle,          nullptr, nullptr },
        { L"Camera",   L"TiltRotate",     Kind::Bool, &g_cfg.tiltRotate,         nullptr, nullptr },
        { L"Camera",   L"VehicleView",    Kind::Int,  &g_cfg.vehicleView,        nullptr, nullptr },
        { L"Camera",   L"TrafficRange",   Kind::Int,  &g_cfg.trafficRange,       nullptr, nullptr },
        { L"PlayerColors", L"Character1", Kind::Int,  &g_cfg.playerColors[0], nullptr, nullptr },
        { L"PlayerColors", L"Character2", Kind::Int,  &g_cfg.playerColors[1], nullptr, nullptr },
        { L"PlayerColors", L"Character3", Kind::Int,  &g_cfg.playerColors[2], nullptr, nullptr },
        { L"PlayerColors", L"Character4", Kind::Int,  &g_cfg.playerColors[3], nullptr, nullptr },
        { L"PlayerColors", L"Character5", Kind::Int,  &g_cfg.playerColors[4], nullptr, nullptr },
        { L"PlayerColors", L"Character6", Kind::Int,  &g_cfg.playerColors[5], nullptr, nullptr },
        { L"PlayerColors", L"Character7", Kind::Int,  &g_cfg.playerColors[6], nullptr, nullptr },
        { L"PlayerColors", L"Character8", Kind::Int,  &g_cfg.playerColors[7], nullptr, nullptr },
        { L"Keys",     L"Map",            Kind::Key,  &g_cfg.mapKey,         L"Keys", L"Menu" },
        { L"Keys",     L"Minimap",        Kind::Key,  &g_cfg.minimapKey,     L"Keys", L"Minimap" },
        { L"Keys",     L"MoveUp",         Kind::Key,  &g_cfg.moveUpKey,      nullptr, nullptr },
        { L"Keys",     L"MoveLeft",       Kind::Key,  &g_cfg.moveLeftKey,    nullptr, nullptr },
        { L"Keys",     L"MoveDown",       Kind::Key,  &g_cfg.moveDownKey,    nullptr, nullptr },
        { L"Keys",     L"MoveRight",      Kind::Key,  &g_cfg.moveRightKey,   nullptr, nullptr },
        { L"Keys",     L"EnterExit",      Kind::Key,  &g_cfg.enterExitKey,   nullptr, nullptr },
        { L"Keys",     L"PrevWeapon",     Kind::Key,  &g_cfg.prevWeaponKey,  nullptr, nullptr },
        { L"Keys",     L"NextWeapon",     Kind::Key,  &g_cfg.nextWeaponKey,  nullptr, nullptr },
        { L"Language", L"Language",       Kind::String, g_cfg.language,      nullptr, nullptr },
        { L"Map",      L"Style",          Kind::Int,  &g_cfg.mapStyle,       L"Overlay", L"MapStyle" },
        { L"Map",      L"MinimapCorner",  Kind::Int,  &g_cfg.minimapCorner,  L"Overlay", L"MinimapCorner" },
        { L"Map",      L"PauseGame",      Kind::Bool, &g_cfg.pauseGame,      L"Overlay", L"PauseGame" },
        { L"Display",  L"FrameBuffer",    Kind::Bool, &g_cfg.frameBuffer,    nullptr, nullptr },
        { L"Display",  L"ScaleMenu",      Kind::Bool, &g_cfg.scaleMenu,      nullptr, nullptr },
        { L"Display",  L"HudScale",       Kind::Int,  &g_cfg.hudScale,       nullptr, nullptr },
        { L"Heading",  L"Show",           Kind::Bool, &g_cfg.showHeading,    L"Heading", L"Show" },
        { L"Heading",  L"Offset",         Kind::Int,  &g_cfg.headingOffset,  L"Heading", L"Offset" },
        { L"Heading",  L"CounterClockwise", Kind::Bool, &g_cfg.headingCcw,   L"Heading", L"CounterClockwise" },
        { L"Debug",    L"Log",            Kind::Bool, &g_cfg.log,            L"Debug", L"Log" },
        { L"Debug",    L"ShowInfo",       Kind::Bool, &g_cfg.showInfo,       L"Debug", L"ShowInfo" },
    };

    bool ReadRaw(const wchar_t* ini, const wchar_t* section, const wchar_t* key, wchar_t* buf, DWORD size)
    {
        buf[0] = 0;
        GetPrivateProfileStringW(section, key, L"", buf, size, ini);
        return buf[0] != 0;
    }

    bool Parse(const Setting& s, const wchar_t* text)
    {
        if (s.kind == Kind::Key)
        {
            int vk = 0;
            if (!text[1] && iswalnum(text[0]))
                vk = (int)towupper(text[0]);
            else
                vk = (int)wcstol(text, nullptr, 0);
            if (vk <= 0 || vk >= 256)
                return false;
            *static_cast<int*>(s.value) = vk;
        }
        else if (s.kind == Kind::Int)
            *static_cast<int*>(s.value) = (int)wcstol(text, nullptr, 0);
        else if (s.kind == Kind::String)
            wcsncpy_s(static_cast<wchar_t*>(s.value), 16, text, _TRUNCATE);
        else
            *static_cast<bool*>(s.value) = wcstol(text, nullptr, 0) != 0;
        return true;
    }

    void Format(const Setting& s, wchar_t* out, size_t n)
    {
        if (s.kind == Kind::Key)
        {
            const int vk = *static_cast<int*>(s.value);
            if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
                swprintf_s(out, n, L"%c", (wchar_t)vk);
            else
                swprintf_s(out, n, L"0x%02X", vk);
        }
        else if (s.kind == Kind::Int)
            swprintf_s(out, n, L"%d", *static_cast<int*>(s.value));
        else if (s.kind == Kind::String)
            wcsncpy_s(out, n, static_cast<const wchar_t*>(s.value), _TRUNCATE);
        else
            swprintf_s(out, n, L"%d", *static_cast<bool*>(s.value) ? 1 : 0);
    }

    void WriteTemplate(const wchar_t* ini)
    {
        HANDLE f = CreateFileW(ini, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return;
        DWORD written;
        WriteFile(f, kDefaultIni, sizeof(kDefaultIni) - 1, &written, nullptr);
        CloseHandle(f);
    }
}

static std::wstring s_iniPath;

bool SaveConfigValue(const void* value)
{
    wchar_t buf[64];
    for (const Setting& s : kSettings)
    {
        if (s.value != value)
            continue;
        Format(s, buf, 64);
        return s_iniPath.size() && WritePrivateProfileStringW(s.section, s.key, buf, s_iniPath.c_str());
    }
    return false;
}

void LoadConfig(const wchar_t* ini)
{
    s_iniPath = ini;
    const bool exists = GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES;
    const int version = exists ? (int)GetPrivateProfileIntW(L"Info", L"Version", 1, ini) : kConfigVersion;

    wchar_t buf[64];
    for (const Setting& s : kSettings)
    {
        if (ReadRaw(ini, s.section, s.key, buf, 64) || (s.oldSection && ReadRaw(ini, s.oldSection, s.oldKey, buf, 64)))
            Parse(s, buf);
    }
    g_cfg.minimapCorner &= 3;
    g_cfg.mapStyle = g_cfg.mapStyle ? 1 : 0;
    g_cfg.cameraRangeFoot = std::clamp(g_cfg.cameraRangeFoot, 0, 90);
    g_cfg.cameraRangeVehicle = std::clamp(g_cfg.cameraRangeVehicle, 0, 90);
    g_cfg.cameraDeadZone = std::clamp(g_cfg.cameraDeadZone, 0, 90);
    g_cfg.tilt = std::clamp(g_cfg.tilt, 0, 2);
    g_cfg.tiltAngle = std::clamp(g_cfg.tiltAngle, 0, 45);
    g_cfg.vehicleView = std::clamp(g_cfg.vehicleView, 0, 3);
    g_cfg.trafficRange = std::clamp(g_cfg.trafficRange, 100, 250);
    g_cfg.vibration = std::clamp(g_cfg.vibration, 0, 100);
    g_cfg.hudScale = std::clamp(g_cfg.hudScale, 0, 4);
    g_cfg.steerAssist = std::clamp(g_cfg.steerAssist, 0, 3);

    // Neue oder veraltete INI: kommentierte Vorlage schreiben und die aktuellen Werte übernehmen
    if (!exists || version < kConfigVersion)
    {
        WriteTemplate(ini);
        if (exists)
        {
            for (const Setting& s : kSettings)
            {
                Format(s, buf, 64);
                WritePrivateProfileStringW(s.section, s.key, buf, ini);
            }
        }
    }
}

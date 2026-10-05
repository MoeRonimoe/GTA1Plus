#include "options.h"
#include "config.h"
#include "log.h"
#include "text.h"

#include <algorithm>
#include <cwchar>

namespace options
{
    namespace
    {
        enum class Type { Header, Bool, Int, Degrees, Choice, Language };

        struct Option
        {
            Type type;
            text::Id label;
            void* value;
            bool restart;                   // wirkt erst nach Neustart
            int minValue, maxValue, step;   // Int/Degrees: Bereich und Schrittweite (Prozent bzw. Grad)
            text::Id choices[4];            // Choice: Beschriftung je Wert (0..maxValue)
        };

        const wchar_t* const kLanguages[] = { L"auto", L"de", L"en", L"fr", L"it" };

        const Option kOptions[] = {
            { Type::Header, text::OptGroupDisplay },
            { Type::Bool, text::OptMinimap, &g_cfg.minimap },
            { Type::Bool, text::OptMinimapOnStart, &g_cfg.minimapOnStart },
            { Type::Choice, text::OptMinimapCorner, &g_cfg.minimapCorner, false, 0, 3, 1,
              { text::CornerBottomLeft, text::CornerBottomRight, text::CornerTopLeft, text::CornerTopRight } },
            { Type::Bool, text::OptGps, &g_cfg.gps },
            { Type::Bool, text::OptGpsArrow, &g_cfg.gpsArrow },
            { Type::Bool, text::OptWaypoints, &g_cfg.waypoints },
            { Type::Bool, text::OptHearts, &g_cfg.hearts },
            { Type::Bool, text::OptPois, &g_cfg.pois },
            { Type::Choice, text::OptMapStyle, &g_cfg.mapStyle, false, 0, 1, 1, { text::StyleTextured, text::StyleSchematic } },
            { Type::Bool, text::OptPauseGame, &g_cfg.pauseGame },
            { Type::Bool, text::OptHint, &g_cfg.hint },

            { Type::Header, text::OptGroupControls },
            { Type::Bool, text::OptTwinStick, &g_cfg.twinStick },
            { Type::Bool, text::OptAlwaysAim, &g_cfg.alwaysAim },
            { Type::Bool, text::OptWasdDriving, &g_cfg.wasdDriving },
            { Type::Bool, text::OptController, &g_cfg.controller },
            { Type::Int, text::OptRumble, &g_cfg.vibration, false, 0, 100, 10 },

            { Type::Header, text::OptGroupCamera },
            { Type::Bool, text::OptCamera, &g_cfg.camera, true },
            { Type::Bool, text::OptCameraMouseVehicle, &g_cfg.cameraMouseVehicle },
            { Type::Int, text::OptRangeFoot, &g_cfg.cameraRangeFoot, false, 0, 90, 10 },
            { Type::Int, text::OptRangeVehicle, &g_cfg.cameraRangeVehicle, false, 0, 90, 10 },
            { Type::Int, text::OptDeadZone, &g_cfg.cameraDeadZone, false, 0, 90, 10 },
            { Type::Bool, text::OptSteady, &g_cfg.cameraSteady, true },
            { Type::Choice, text::OptTilt, &g_cfg.tilt, false, 0, 2, 1, { text::OptOff, text::InVehicle, text::TiltAlways } },
            { Type::Degrees, text::OptTiltAngle, &g_cfg.tiltAngle, false, 0, 45, 5 },
            { Type::Bool, text::OptTiltRotate, &g_cfg.tiltRotate },
            { Type::Choice, text::OptVehicleView, &g_cfg.vehicleView, false, 0, 3, 1,
              { text::ViewGame, text::ViewHigh, text::ViewHigher, text::ViewMax } },
            { Type::Int, text::OptTrafficRange, &g_cfg.trafficRange, false, 100, 250, 25 },

            { Type::Header, text::OptGroupGameplay },
            { Type::Bool, text::OptAutosave, &g_cfg.autosave },
            { Type::Int, text::OptPistolDamage, &g_cfg.pistolDamage, false, 5, 100, 5 },
            { Type::Int, text::OptMachineGunDamage, &g_cfg.machineGunDamage, false, 5, 100, 5 },
            { Type::Int, text::OptPunchDamage, &g_cfg.punchDamage, false, 0, 100, 5 },
            { Type::Bool, text::OptExtinguish, &g_cfg.extinguish, true },
            { Type::Bool, text::OptQuickRetry, &g_cfg.quickRetry, true },
            { Type::Bool, text::OptKeepMultiplier, &g_cfg.keepMultiplier, true },
            { Type::Bool, text::OptAllCharacters, &g_cfg.allCharacters, true },
            { Type::Bool, text::OptPortraitColors, &g_cfg.portraitColors },

            { Type::Header, text::OptGroupOther },
            { Type::Language, text::OptLanguage, g_cfg.language, true },
            { Type::Bool, text::OptShowInfo, &g_cfg.showInfo },
        };
        constexpr int kCount = sizeof(kOptions) / sizeof(kOptions[0]);

        int LanguageIndex()
        {
            for (int i = 0; i < 5; ++i)
                if (_wcsicmp(g_cfg.language, kLanguages[i]) == 0)
                    return i;
            return 0;
        }
    }

    int Count() { return kCount; }
    bool IsHeader(int i) { return i >= 0 && i < kCount && kOptions[i].type == Type::Header; }
    const wchar_t* Label(int i) { return i >= 0 && i < kCount ? text::Get(kOptions[i].label) : L""; }
    bool NeedsRestart(int i) { return i >= 0 && i < kCount && kOptions[i].restart; }

    std::wstring Value(int i)
    {
        if (i < 0 || i >= kCount)
            return L"";
        const Option& o = kOptions[i];
        wchar_t buf[64] = L"";
        switch (o.type)
        {
        case Type::Bool:
            return text::Get(*static_cast<bool*>(o.value) ? text::OptOn : text::OptOff);
        case Type::Int:
            swprintf_s(buf, L"%d %%", *static_cast<int*>(o.value));
            return buf;
        case Type::Degrees:
            swprintf_s(buf, L"%d\u00B0", *static_cast<int*>(o.value));
            return buf;
        case Type::Choice:
            return text::Get(o.choices[std::clamp(*static_cast<int*>(o.value), 0, o.maxValue)]);
        case Type::Language:
        {
            const int k = LanguageIndex();
            return k == 0 ? text::Get(text::LanguageAuto) : kLanguages[k];
        }
        default:
            return L"";
        }
    }

    bool Change(int i, int dir)
    {
        if (i < 0 || i >= kCount || dir == 0)
            return false;
        const Option& o = kOptions[i];
        switch (o.type)
        {
        case Type::Bool:
        {
            bool& v = *static_cast<bool*>(o.value);
            v = !v;
            break;
        }
        case Type::Int:
        case Type::Degrees:
        {
            int& v = *static_cast<int*>(o.value);
            const int next = std::clamp(v + dir * o.step, o.minValue, o.maxValue);
            if (next == v)
                return false;
            v = next;
            break;
        }
        case Type::Choice:
        {
            int& v = *static_cast<int*>(o.value);
            v = (v + dir + o.maxValue + 1) % (o.maxValue + 1);
            break;
        }
        case Type::Language:
        {
            const int k = (LanguageIndex() + dir + 5) % 5;
            wcscpy_s(g_cfg.language, kLanguages[k]);
            break;
        }
        default:
            return false;
        }
        if (!SaveConfigValue(o.value))
            LOG("Einstellung konnte nicht gespeichert werden");
        return true;
    }
}

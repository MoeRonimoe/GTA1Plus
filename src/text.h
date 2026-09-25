#pragma once

// Alle sichtbaren Texte; die Übersetzungen stehen in <asi>_lang.ini (eine Sektion pro Sprache)
namespace text
{
    enum Id
    {
        PauseTitle,
        Resume,
        MapAndWaypoints,
        Map,
        QuitLevel,
        OnFoot,
        InVehicle,
        OnTrain,
        GpsTarget,        // %s = Zielart, %d = Meter
        GpsNone,
        TargetWaypoint,
        TargetMission,
        TargetMissionRed,
        WaypointCount,    // %d
        StyleTextured,
        StyleSchematic,
        ViewFree,
        CursorPos,        // %d %d
        MapUnavailable,
        ConfirmTitle,
        ConfirmText,
        Yes,
        No,
        HelpList,         // %s = Kartentaste
        HelpMap,
        HelpMapNoWaypoints,
        HelpConfirm,
        HintActive,
        HintMenu,
        HintMap,          // %s
        HintMinimap,      // %s
        HelpMapMouse,
        HelpMapMouseNoWaypoints,
        HintBurning,      // %s = Taste(n) zum Wälzen
        PadJump,          // Controller-Taste zum Springen
        KeySpace,         // Name der Leertaste (Windows liefert z.B. nur "LEER")
        Count
    };

    // language: "auto" (Sprache aus den Spieleinstellungen) oder de/en/fr/it
    void Init(const wchar_t* langFile, const wchar_t* language);
    const wchar_t* Get(Id id);
    const wchar_t* Language();
}

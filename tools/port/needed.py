# Adressen, die GTA1Plus in GTA.exe 1.0.0.1 (R2P) benutzt
FUNCS = {
    "HudDraw": 0x46DBB0, "GetPlayerPos": 0x463FF0, "PauseOn": 0x46B9C0, "PauseOff": 0x46B9E0,
    "SoundPause": 0x4583B0, "SoundResume": 0x4583E0, "QuitLevel": 0x42EB60, "ArrowResolve": 0x470A90,
    "RenderFrame": 0x42EFE0, "PedFire": 0x4470B0, "ViewRect": 0x42BB40, "ReadInput": 0x4365E0,
    "PedSprite": 0x442790, "ObjectCreate": 0x41BAA0, "ObjectDelete": 0x41D380, "NotEnglish": 0x4201B0,
    "ScriptSurvive": 0x476120, "DebugCoords": 0x46DBF0,
}
# Codestellen (Befehl innerhalb einer Funktion)
SITES = {
    "HudCallJmp": 0x42F002, "HudCallCall": 0x42F02B, "RenderCall": 0x45D057, "PedFireCall": 0x451AA5,
    "ViewRectCall": 0x42BB0B, "ReadInputCall": 0x45CFD3, "BurnDamage": 0x451AF7, "MultHalve": 0x464649,
    "PickPrevCheck": 0x40991E, "PickNextCheck": 0x409976, "ScriptDispatch": 0x4822EA, "PauseKeyCase": 0x42F30A,
    "LoadGetProcAddress": 0x49AFC3, "LoadPeek1": 0x43082C, "LoadPeek2": 0x49A2DC, "LoadPeek3": 0x49C54B,
    "SigEnterExit": 0x436AC0, "SigFire": 0x436A99, "SigPedMove": 0x452B9E,
}
DATA = {
    "ArrowMainOn": 0x677F0C, "ArrowMain": 0x678418, "ArrowRedOn": 0x678400, "ArrowRed": 0x678DD8,
    "PedStruct": 0x600C48, "Camera": 0x6575E0, "InSteer": 0x600A80, "InGas": 0x6008FC, "InBrake": 0x600A18,
    "InJump": 0x600900, "InFire": 0x6008F8, "InNext": 0x600A1C, "InPrev": 0x600A48, "InSpecial": 0x600A84,
    "PlayerCharacter": 0x657764, "CharacterColors": 0x4C1668, "ColorToRemap": 0x4C0110, "KeyScancodes": 0x600A50,
    "Objects": 0x50F5F8, "ActiveObjects": 0x57E750, "LocalPlayer": 0x657C50, "NumPlayers": 0x657C4C,
    "PlayerPedId": 0x6575CA, "PlayerCtrl": 0x657695, "PlayerObject": 0x657699, "RunState": 0x5F3A90,
    "ScriptJumps": 0x487FDC, "ScriptProc": 0x736A08, "ScriptState": 0x6DB660, "ScriptActive": 0x6FB768,
    "ScriptObjSlot": 0x6DB768, "ScriptObjects": 0x6796F8, "MglPrimary": 0x4C2FEC, "MglPageCount": 0x4C2FE8,
    "IatGetProcAddress": 0x787474, "IatPeekMessageA": 0x78754C,
}

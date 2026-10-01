===============================================================================
  GTA1Plus - modern comfort features for Grand Theft Auto (1997, Windows)
===============================================================================

GTA1Plus adds a pause menu with a city map, GPS, a minimap, save games,
twin-stick and controller support and more to the original GTA and the
London expansions. It does not change any game files: it is a plugin that
is loaded into the running game.


-------------------------------------------------------------------------------
  SUPPORTED GAME VERSIONS
-------------------------------------------------------------------------------

  GTA.exe 1.0.0.1 (October 1997, e.g. the "Ready2Play" package,
  also GTA_Intro.exe) ..................................... all features
  GTA_London.exe (London 1969) ............ all features except portrait colours
  GTA_London_1961.exe (London 1961) ....... all features except portrait colours

  Not supported:
  - the 2002 freeware release ("Grand Theft Auto.exe") - different data layout
  - the DOS version (e.g. in DOSBox)

With an unknown game version the game starts normally, just without the
mod's features.


-------------------------------------------------------------------------------
  INSTALLATION
-------------------------------------------------------------------------------

1. Copy the contents of this folder into your game folder (the folder that
   contains GTA.exe):

     dsound.dll                          -> next to GTA.exe
     scripts\GTA1Plus.asi                -> into the "scripts" subfolder
     scripts\GTA1Plus_PlayerColors.png   -> into the "scripts" subfolder

   Create the "scripts" folder if it does not exist yet.

2. Start the game. On the first start the mod creates GTA1Plus.ini
   (settings) and GTA1Plus_lang.ini (texts) in the scripts folder.

About dsound.dll: this is a small loader. It passes all sound calls on to
Windows unchanged and loads every .asi file from the scripts folder. It works
together with the CD music replacement (winmm.dll) of the Ready2Play package.

  - If your game folder already has a dsound.dll from another mod, back it up
    before you replace it.
  - If you already use another ASI loader that loads the scripts folder, you
    only need the scripts folder. Note: the Ultimate ASI Loader by
    ThirteenAG crashes the game together with the Ready2Play music
    replacement, which is why GTA1Plus brings its own loader.

UNINSTALL: delete dsound.dll and the GTA1Plus files in the scripts folder.
Your saves are in scripts\GTA1Plus_saves.


-------------------------------------------------------------------------------
  FEATURES
-------------------------------------------------------------------------------

PAUSE MENU (Esc)
  - City map with your position, mission targets and your own waypoints.
    Paint shops, bomb shops and car cranes are marked with icons.
  - Briefings: read the last phone and pager messages of the level again.
  - Save game / Load game (see below).
  - GTA1Plus options: change the mod's settings while you play.
  - Quit level.
  Works with keyboard, mouse and controller.

GPS AND MINIMAP
  - A route along the roads to your waypoint or the current mission target,
    on the map and on the minimap.
  - A blue arrow at the top of the screen points to the next turn.
  - Round minimap (key N), in a screen corner of your choice.

SAVE GAMES
  - One save per level and character.
  - Saving works between missions, not while a phone mission is running.
  - The game saves automatically after every completed mission.
  - When you start a level that has a save, the mod offers to load it.
  - A save keeps the mission progress, score, multiplier, lives, weapons and
    your position. Buildings destroyed by missions are back after loading.

CONTROLS
  - Twin-stick: walk with WASD in screen directions and aim with the mouse.
    Optionally drive with WASD too.
  - Controllers: Xbox pads (XInput) and PS4/PS5 or other DirectInput pads,
    in levels and in the main menu.
  - Weapon switching with Q/E or the mouse wheel.

CAMERA
  - Look ahead with the mouse (towards the screen edge) or the right stick.
  - Steadier camera: follows the character more closely instead of drifting
    behind (the hex patch shared by amilmand on GTAforums, applied in
    memory).
  - Optional fixed camera height in vehicles, e.g. always zoomed out.
  - Optional larger traffic range: new cars appear further outside the
    screen, so you have more time to react.
  - Tilted camera (prototype, off by default): a view in the style of
    GTA Chinatown Wars that tilts the picture so you see further ahead and
    turns it with your vehicle. The picture gets softer; needs 32-bit colour.

GAMEPLAY
  - Health shown as five hearts below the score.
  - Put out fires: when you burn, press jump to roll on the ground.
    In the original game burning is certain death.
  - Damage instead of instant death: a pistol or machine gun bullet costs
    your character part of the health (35 % and 20 %) instead of killing at
    once, and punches now hurt (10 %, for you and for others). Other
    characters still die from one bullet. Adjustable in the pause menu
    (GTA1Plus options, Gameplay); 100 / 100 / 0 is the original game.
  - Quick retry: after "MISSION FAILED" the phone of the mission rings again
    right away. Rewards already paid in the failed attempt are not paid again.
  - Busted: the multiplier is not halved.
  - The four female characters can be picked in every language version.
  - Your character's colours match the portrait you picked (GTA1 only).

LANGUAGES
  English, German, French and Italian - by default the game's language.


-------------------------------------------------------------------------------
  CONTROLS
-------------------------------------------------------------------------------

  Action                       Keyboard / mouse        Controller (PS / Xbox)
  ---------------------------  ----------------------  -------------------------
  Pause menu                   Esc                     Options / Start
  Map                          M                       Share / Back
  Minimap on/off               N                       R3
  Move / steer                 WASD or arrow keys      Left stick
  Aim / look around            Mouse                   Right stick
  Fire                         Left click or Ctrl      R2 / RT or Circle / B
  Accelerate / brake           W / S                   R2 / L2
  Enter / exit vehicle         F or Enter              Triangle / Y
  Jump / handbrake             Space                   Cross / A
                                                       (on foot also Square / X)
  Roll when burning            Space                   Cross / A
  Horn (in vehicles)           Tab                     Square / X or Circle / B
  Previous / next weapon       Q / E or mouse wheel    L1 / R1

The keys from the game's own control settings keep working as well.
Main menu with a controller: D-pad or stick to navigate, Cross/A or Options
to confirm, Circle/B to go back.


-------------------------------------------------------------------------------
  SETTINGS
-------------------------------------------------------------------------------

Most settings can be changed in the pause menu under "GTA1Plus options".
They are saved in scripts\GTA1Plus.ini. Settings marked with * in the menu
take effect after restarting the game.

In the INI file every feature can be switched off on its own (0 = off,
1 = on), and keys can be changed under [Keys]. Each setting is explained
in the file.

Texts can be changed or translated in scripts\GTA1Plus_lang.ini.


-------------------------------------------------------------------------------
  TROUBLESHOOTING
-------------------------------------------------------------------------------

  - The mod writes a log file, scripts\GTA1Plus.log (in German). Its first
    lines show whether your game version was recognised ("erkannt").
  - The game does not start after installing: remove dsound.dll and check
    whether another mod already uses that file name.
  - Twin-stick and the controller can be switched off in the options if you
    prefer the original controls.


-------------------------------------------------------------------------------
  CREDITS
-------------------------------------------------------------------------------

  GTA1Plus by MoeRonimoe.
  Steadier camera: hex patch shared by amilmand on GTAforums, applied in
  memory.
  Ultimate ASI Loader by ThirteenAG: the ASI plugin format and loading from
  the scripts folder follow its conventions.
  Grand Theft Auto (c) 1997 DMA Design / Rockstar Games. This mod is a fan
  project and not affiliated with Rockstar Games.

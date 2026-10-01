# GTA1Plus

A mod for the original **Grand Theft Auto** (1997, Windows) that adds modern comfort features without touching any game files. It is an ASI plugin that hooks into the running game.

## Features

- **Pause menu with city map**: opens with Esc. It shows your position, mission targets and your own waypoints, and supports mouse and controller.
- **Save and load**: one save per level and character, taken between missions (not while a phone mission is running). It stores the mission progress, score, multiplier, lives, weapons and your position. The game saves automatically after every completed mission. When you start a level that has a save, the mod offers to load it. Saves are kept in `scripts\GTA1Plus_saves`. Buildings destroyed by missions come back after loading.
- **Briefings**: the last phone and pager messages of the current level, to read again in the pause menu.
- **In-game options**: the "GTA1Plus options" page of the pause menu changes the settings and writes them to `GTA1Plus.ini`. Settings marked with * take effect after a restart.
- **GPS**: a route to the waypoint or mission target on the map and minimap, plus a direction arrow at the top of the screen. Routes prefer roads.
- **Minimap**: a round minimap in a screen corner of your choice.
- **Places on the map**: paint shops, bomb shops and car cranes are shown as icons on the map and minimap, with a legend. They are read from the level's mission script, so they also work in London.
- **Twin-stick controls**: WASD movement and mouse aiming on foot, optional strafing (always face the crosshair), and WASD driving.
- **Controllers**: Xbox (XInput) and PS4/PS5 or other DirectInput pads, in levels and in the main menu.
- **Steerable camera**: look ahead with the mouse or the right stick.
- **Steadier camera**: the hex patch shared by amilmand on GTAforums, applied in memory. The camera follows the character more closely instead of drifting behind. Works in all supported versions.
- **Fixed camera height in vehicles** (optional): keeps the camera high while driving instead of zooming in and out with the speed.
- **Traffic range** (optional): new traffic appears and old traffic disappears further outside the screen, so you have more time to react. At 200 % cars appear about 5 blocks outside the picture instead of 1.
- **Tilted camera (prototype, off by default)**: a view in the style of *GTA Chinatown Wars*. The picture is tilted so you see further ahead, and in vehicles it turns with the driving direction. The mod transforms the finished game picture, so buildings keep their top-down look and the picture gets softer. Switch it on in the pause menu (GTA1Plus options → Camera) or with `[Camera] Tilt` in the INI. Needs 32-bit colour.
- **Health hearts**: five hearts below the score.
- **Put out fires**: when you are on fire, press jump to roll on the ground and put the flames out. In the original game burning is certain death.
- **Damage instead of instant death**: in the original game every bullet kills at once and punches do no damage. With the mod a pistol or machine gun bullet costs your character part of the health (35 % and 20 % by default), and a punch hurts everyone who gets hit (10 %). Other characters still die from a single bullet, so missions and kill frenzies play as before. Adjust the three values in the pause menu (GTA1Plus options → Gameplay) or with `[Gameplay] PistolDamage`, `MachineGunDamage` and `PunchDamage` in the INI; 100 / 100 / 0 is the original behaviour.
- **Gameplay options**:
  - Keep the multiplier when busted.
  - Retry failed missions quickly. Rewards that a failed attempt already paid out (e.g. for stealing the car halfway through a mission) are not paid again on the retry.
  - Unlock the female characters in non-English versions.
  - Colour the player to match the chosen portrait.
- **Settings and languages**: everything can be switched on or off in `GTA1Plus.ini` or in the pause menu. Texts come in English, German, French and Italian (`GTA1Plus_lang.ini`).

## Supported game version

| Game | Status |
|---|---|
| `GTA.exe` 1.0.0.1 (October 1997, e.g. *Ready2Play*; also `GTA_Intro.exe`) | All features |
| `GTA_London.exe` (London 1969) | All features except portrait colours |
| `GTA_London_1961.exe` (London 1961) | All features except portrait colours |
| 2002 freeware release (`Grand Theft Auto.exe`) | Not yet supported (different data layout) |

London has its own characters, so the portrait colours (made for the GTA1 characters) stay off there. Unknown executables start normally without the mod's features. The DOS version (e.g. in DOSBox) can't load ASI plugins.

## Installation

1. Copy `dsound.dll` (the small GTA1Plus loader) into the game folder, next to `GTA.exe`.
2. Copy `GTA1Plus.asi` and `GTA1Plus_PlayerColors.png` into the `scripts` subfolder.
3. Start the game. `GTA1Plus.ini` and `GTA1Plus_lang.ini` are created on the first start.

The loader forwards DirectSound unchanged and loads every `scripts\*.asi`. It does not hook anything else, so it works alongside the CD-music replacement (`winmm.dll`) of the Ready2Play package. The Ultimate ASI Loader by ThirteenAG crashes together with that DLL, which is why GTA1Plus brings its own loader.

## Controls

| Action | Keyboard / mouse | Controller (PlayStation / Xbox) |
|---|---|---|
| Pause menu | Esc | Options / Start |
| Map | M | Share / Back |
| Minimap on/off | N | R3 |
| Move / steer | WASD or arrow keys | Left stick |
| Aim / look around | Mouse | Right stick |
| Fire | Left click or Ctrl | R2 / RT or Circle / B |
| Accelerate / brake | W / S | R2 / L2 |
| Enter / exit vehicle | F or Enter | Triangle / Y |
| Jump / handbrake / roll when burning | Space | Cross / A (on foot also Square / X) |
| Horn (in vehicles) | game key (Tab) | Square / X |
| Switch weapon | Q / E or mouse wheel (plus the game keys) | L1 / R1 |

Main menu with a controller: D-pad or stick to navigate, Cross/A or Options to confirm, Circle/B to go back.

## Building

Open `GTA1Plus.sln` in Visual Studio 2022 and build the **Release | x86** configuration:

- `Release\GTA1Plus\GTA1Plus.asi` is the mod. The colour chart from `assets\` is copied next to it.
- `Release\Loader\dsound.dll` is the loader.

`tools\make-package.ps1` then puts the files for players into `dist\GTA1Plus` (same layout as the game folder, with the player README from `package\README.txt`) and `dist\GTA1Plus.zip`.

The game addresses are in `src/gta1.cpp`. Code comments are in German.

### Testing aids

Create an empty file next to the ASI:

- `GTA1Plus.shot` saves the next frame as `GTA1Plus_shot.bmp`.
- `GTA1Plus.burn` sets the player on fire.
- `GTA1Plus.douse` puts the fire out.

Set `ShowInfo=1` in the INI to show debug information.

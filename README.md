# GTA1Plus

A mod for the original **Grand Theft Auto** (1997, Windows) that adds modern comfort features without touching any game files. It is an ASI plugin that hooks into the running game.

## Features

- **Pause menu with city map**: opens with Esc. It shows your position, mission targets and your own waypoints, and supports mouse and controller.
- **Save and load**: one save per level and character, taken between missions (not while a phone mission is running). It stores the mission progress, score, multiplier, lives, weapons, firebombs, armour and your position. Saving and loading work on foot, because a save puts your character back where it stood. The game saves automatically after every completed mission, as soon as you are out of your vehicle. When you start a level that has a save, the mod offers to load it – a few seconds in, once the game has finished setting the level up (weapon crates, the doors of bomb shops and mission gates, car cranes); loading or saving earlier would leave part of that out. A save also remembers which doors and gates missions have switched on or off, and where the mission arrow points. Saves are kept in `scripts\GTA1Plus_saves`. Buildings destroyed by missions come back after loading.
- **Briefings**: the last phone and pager messages of the current level, to read again in the pause menu.
- **Progress**: one page in the pause menu with the figures the game otherwise only flips through one at a time on its own pause screen – score against the level's target, missions passed, secrets found – plus lives and multiplier.
- **In-game options**: the "GTA1Plus options" page of the pause menu changes the settings and writes them to `GTA1Plus.ini`. Settings marked with * take effect after a restart.
- **GPS**: a route to the waypoint or mission target on the map and minimap, plus a direction arrow at the top of the screen. Routes prefer roads.
- **Minimap**: a round minimap in a screen corner of your choice, with adjustable size and zoom (`[Map] MinimapSize`, `MinimapZoom`, also in the pause menu). It makes way for the game's own HUD in its corner: below the weapon, armour and timers at the top, above mission messages at the bottom. It moves away at once and comes back once the space has been free for a moment, so it does not jump while you cycle weapons.
- **Places on the map**: paint shops, bomb shops and car cranes are shown as icons on the map and minimap, with a legend. They are read from the level's mission script, so they also work in London.
- **Twin-stick controls**: WASD movement and mouse aiming on foot, optional strafing (always face the crosshair), and WASD driving. Optionally the keys work from the character's point of view: W walks towards the crosshair, S away from it, A and D sideways around it (`[Features] WalkToCrosshair`, also in the pause menu).
- **Controllers**: Xbox (XInput) and PS4/PS5 or other DirectInput pads, in levels and in the main menu.
- **Controller vibration**: for your own shots (strongest for the rocket launcher), hits you take, damage to your vehicle and explosions nearby. Adjustable strength, 0 switches it off (GTA1Plus options → Controls or `[Features] Vibration`). Works with Xbox pads and with PS4 pads over USB or Bluetooth; PS5 pads are implemented but untested. It only runs while you are actually playing with the controller.
- **Steerable camera**: look ahead with the mouse or the right stick.
- **Steadier camera**: the hex patch shared by amilmand on GTAforums, applied in memory. The camera follows the character more closely instead of drifting behind. Works in all supported versions.
- **Fixed camera height in vehicles** (optional): keeps the camera high while driving instead of zooming in and out with the speed.
- **Camera height on foot** (optional): the game shows a little over four blocks to each side when you walk. Three steps (high, very high, maximum) show about 7, 11 and 18 blocks instead; the highest is the view the game itself uses in a vehicle at top speed, so with both options on maximum the picture stays the same when you get in or out. Mouse aiming follows the zoomed-out picture. `[Camera] FootView`, also in the pause menu.
- **Traffic range** (optional): new traffic appears and old traffic disappears further outside the screen, so you have more time to react. At 200 % cars appear about 5 blocks outside the picture instead of 1.
- **Tilted camera (prototype, off by default)**: a view in the style of *GTA Chinatown Wars*. The picture is tilted so you see further ahead, and in vehicles it turns with the driving direction. The mod transforms the finished game picture, so buildings keep their top-down look and the picture gets softer. Switch it on in the pause menu (GTA1Plus options → Camera) or with `[Camera] Tilt` in the INI. Needs 32-bit colour.
- **Health hearts**: five hearts below the score.
- **Put out fires**: when you are on fire, press jump to roll on the ground and put the flames out. In the original game burning is certain death.
- **Damage instead of instant death**: in the original game every bullet kills at once and punches do no damage. With the mod a pistol or machine gun bullet costs your character part of the health (35 % and 20 % by default), and a punch hurts everyone who gets hit (10 %). Other characters still die from a single bullet, so missions and kill frenzies play as before. Armour works as in the original game: while you have some, a bullet only takes one armour point and no health. In two cases the original game showed armour that did not stop anything: the armour that the SUCKMYROCKET cheat gives at the start of a level, and armour you keep with a get-out-of-jail card after being busted in a vehicle. With the mod it protects in both cases like armour you have just picked up. Adjust the three values in the pause menu (GTA1Plus options → Gameplay) or with `[Gameplay] PistolDamage`, `MachineGunDamage` and `PunchDamage` in the INI; 100 / 100 / 0 is the original behaviour.
- **Drive through trees, lamp posts and signs**: as in GTA2, tree trunks, lamp posts, the posts of road signs and traffic lights and other thin posts no longer stop your vehicle. The game checks vehicles against trees and lamps including their height, so the mod lifts them by three quarters of a block (you drive underneath; tree tops look slightly bigger and shift with the camera like roofs) and removes the invisible posts the game puts under palms and lamp posts. The posts of road signs, traffic lights and bus stops are part of the map itself; the mod changes them in memory so that they look the same but let vehicles pass. Bushes, benches, hydrants, fences and the like stay solid. Switch it off at any time in the pause menu (GTA1Plus options → Gameplay) or with `[Gameplay] NoTreeCollision`.
- **Steering aid**: as in GTA2, the car straightens itself along the street when you let go of the steering, so it keeps its lane and a short tap on the steering becomes a lane change. The original game does this only while the car is less than 11° off a street direction – less than a single tap turns it – and wobbles around the direction while doing so. The mod replaces that with a smooth correction on roads and pavements. It acts only while you are not steering, drive forwards and point almost along a street, so turns and diagonal driving stay as they are. Three strengths in the pause menu (GTA1Plus options → Gameplay) or with `[Gameplay] SteerAssist`: 1 = gentle (up to 13° off), 2 = medium (18°, catches one tap; default), 3 = strong (28°, corrects faster); 0 = off, the original behaviour.
- **Trams in San Andreas**: the San Andreas map has two tram lines in its streets, and the game itself contains the trams – a pink single car that stops at its stations – but they were switched off. The mod lets them run: four trams on two loops, clockwise. A tram only leaves when the next station is clear, and the stretch up to it is its own: with four trams on the four stations of the western loop none of them ever left, with three they waited for minutes. So the two stations close together there get no tram of their own, and at every crossing where the map marks a stop (one mark before and one after the cross street) the first one becomes a station – 19 instead of 7, shorter stretches and stops like a real tram. A tram stops for a vehicle on the track two blocks ahead and goes on once it is clear (in the game's own code it made a station stop there and then waited for good). The tram lines run in the kerb lane, where the game also parks cars and traffic queues: if the same vehicle is still in the way after 10 seconds, the tram pushes through and wrecks it, as it does in curves anyway. The style file of San Andreas even has a tram stop sign (pole, red board, timetable box) that the map never uses; the mod puts one on the pavement next to each station. Takes effect at the next level start; `[Gameplay] Trams`, also in the pause menu. GTA1 and the Rockstar Classics release (London has no San Andreas).
- **Firebomb**: the fifth weapon that was cut from GTA1. The game still has its icon (a red petrol can), the crate type and the can you pick up, but crates of that type broke open empty, the can could not be picked up and the game has no weapon code for it. With the mod about every fifth pistol or machine-gun crate holds firebombs (always the same ones); break it open and pick up the can for five firebombs. In the weapon cycle (E / mouse wheel / R1) the firebomb comes after the flamethrower, shown with its icon and count in the top left. Fire throws it at the crosshair (2.5 to 7 blocks; without a crosshair 5 blocks ahead) where it explodes; a wall stops it early, and standing right in front of a wall it is not thrown at all. Firebombs are lost with the other weapons when you die, and saves keep them. GTA1 and the Rockstar Classics release; `[Gameplay] Firebomb`, also in the pause menu.
- **Extra time for timed missions** (optional): adds the chosen number of seconds (up to 300) to every mission countdown, for hard levels such as London 1961. The game shows the countdown on the pager and keeps the real deadline separately in the mission script; the mod extends both together including the time to reach a ringing phone, and leaves countdowns alone where it cannot tell which deadline belongs to them (London 1961: all 20 are covered; GTA1: 57 of 64). Kill frenzies get the extra time as well. `[Gameplay] ExtraMissionTime`, also in the pause menu.
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
| `Grand Theft Auto.exe` (Rockstar Classics release, November 2002, folder `WINO`) | All features |

London has its own characters, so the portrait colours (made for the GTA1 characters) stay off there. Unknown executables start normally without the mod's features. The DOS version (e.g. in DOSBox) can't load ASI plugins.

The Classics release is a separate build with a different data layout; GTA1Plus knows the layout of each version. It was tested with the Ultimate ASI Loader as `ddraw.dll` and its built-in window mode, as shipped with ThirteenAG's widescreen fix. For that setup the mod does three more things, all of which can be switched off under `[Display]` in the INI:

- **Steady picture** (`FrameBuffer`): there the game draws every eighth picture straight into the page that is on screen, so a half-drawn picture can show up for a moment (flicker). GTA1Plus lets the game draw every picture in normal memory and hands it to the screen in one piece. Reading the picture back is fast that way, so the mod's displays are blended in full quality as well.
- **Full-size main menu** (`ScaleMenu`): the game draws the main menu and the other screens outside the levels 640×480 pixels small into the top left corner. GTA1Plus enlarges them to the screen height. Whole factors stay pixel-sharp (3× at 2560×1440); with other factors the pixel edges are smoothed.

- **Readable HUD** (`HudScale`, also "HUD size" in the pause menu under GTA1Plus options → Display): the game draws its own displays – score, lives, multiplier, weapon and ammo, phone messages, area names, the mission arrow – in a fixed pixel size, so they get tiny at high resolutions. GTA1Plus lets the game draw them into a smaller picture and puts them into the frame enlarged by a whole factor (pixel-sharp, see-through parts stay see-through). "Automatic" picks the factor from the screen height so that they are about as large as on the 640×480 screen they were made for: 2× from 810 lines, 3× from 1350 (e.g. 2560×1440), 4× from 1890. 1× is the game's own size. The mod's hearts grow with them. Needs `FrameBuffer`.

These only take effect where they are needed (picture memory that is slow to read, a menu drawn smaller than the screen, a HUD drawn in single pixels); in the other versions they do nothing. The tilted camera works in the Classics release too; it reads the finished picture, so it needs `FrameBuffer` there (without it the camera options are not offered).

## Installation

1. Copy `dsound.dll` (the small GTA1Plus loader) into the game folder, next to `GTA.exe`.
2. Copy `GTA1Plus.asi` and `GTA1Plus_PlayerColors.png` into the `scripts` subfolder.
3. Start the game. `GTA1Plus.ini` and `GTA1Plus_lang.ini` are created on the first start.

The loader forwards DirectSound unchanged and loads every `scripts\*.asi`. It does not hook anything else, so it works alongside the CD-music replacement (`winmm.dll`) of the Ready2Play package. The Ultimate ASI Loader by ThirteenAG crashes together with that DLL, which is why GTA1Plus brings its own loader.

Rockstar Classics release: the game is `WINO\Grand Theft Auto.exe`. If that folder already has the Ultimate ASI Loader (`ddraw.dll`, e.g. from the widescreen fix), only copy the two files into `WINO\scripts`; `dsound.dll` is not needed there.

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
- `GTA1Plus.rumble` makes the controller vibrate at full strength for one second.
- `GTA1Plus.firebomb` gives ten firebombs.

Set `ShowInfo=1` in the INI to show debug information; it also logs the vibration events (shots, hits, vehicle damage, explosions).

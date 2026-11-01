# RiffedMenu

An in-game trainer menu for **GTA: San Andreas** (the classic PC version). It loads as an `.asi` plugin.

Press **Insert** in game to open it.

## Features

- **Player:** god mode, infinite armour and ammo, never wanted, wanted level, money, noclip (**F7**), jetpack
- **Vehicle:** spawn any car, bike, plane or boat, optionally straight into the driver's seat and with a custom plate, plus god mode, auto repair, flip and boost
- **Customize:** a pocket mod shop. Pick paint from the real colour palette, apply paintjobs, and fit spoilers, bumpers, side skirts, wheels, nitro and hydraulics. A showroom camera circles the car while you work.
- **Outfit:** every piece of clothing, haircut and tattoo, plus a skin changer. A mirror camera shows you the result.
- **Weapons:** every weapon or quick packs, plus a damage multiplier from x0.1 to one-hit kills
- **Teleport:** map marker, coordinates, or a list of preset places
- **World:** time, freeze time, weather, game speed
- **Stats:** fat, muscle, stamina, driving, flying and weapon skills

## Supported game versions

| Version | Works |
|---|---|
| Steam (2014 re-release, `gta-sa.exe`) | ✅ tested |
| v1.0 US (`gta_sa.exe`) | ⚠️ should work, untested |
| Definitive Edition | ❌ |

On any other version the menu still opens, but shows a red warning and changes nothing in the game.

## Install

1. Install an ASI loader, if you don't have one yet. [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases) is a common choice; [SilentPatch](https://silent.gtagames.nl/) users usually already have one.
2. Download `RiffedMenu.asi` from [Releases](../../releases/latest).
3. Put it in the game folder, next to the game's `.exe`.
4. Start the game, load a save, and press **Insert**.

## Controls

| Key | Action |
|---|---|
| Insert | Open or close the menu |
| F7 | Toggle noclip |
| W A S D | Noclip: move where the camera looks |
| Space / Ctrl | Noclip: up / down |
| Shift | Noclip: faster |

## Something not working?

RiffedMenu writes `RiffedMenu.log` in the game folder. Send that file to whoever is helping you.

- **Menu doesn't appear:** check the log for `Game features ENABLED`. If it isn't there, your game version isn't supported.
- **Overlays:** RivaTuner/Afterburner and Discord overlays are fine, but if something acts up, try turning them off.

Single-player only. Don't use it with SA-MP or MTA. Anything you spawn or change gets saved if you save the game.

## Building from source

You need a MinGW cross compiler (`i686-w64-mingw32`), CMake and Ninja.

```bash
git clone --recursive https://github.com/RiffedStorm/RiffedMenu.git
cd RiffedMenu
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
cmake --build build        # -> build/RiffedMenu.asi
```

On Linux with Bottles, `build_and_run.sh` does everything in one step: it builds, copies the plugin into the game folder, starts the game and shows the logs. Put the repo inside the game folder, then run:

```bash
./build_and_run.sh          # options: --build-only, --no-tail, --clean
```

Optional environment variables: `BOTTLE=<name>` (default `Gry`) and `GAME_EXE=<file>`.

## How it works (short version)

Most features run through the game's own mission-script engine, the same way CLEO mods do. That means each game version only needs a few memory addresses. Those addresses live in `src/game.cpp`. The menu is drawn with [Dear ImGui](https://github.com/ocornut/imgui), and hooks are installed with [MinHook](https://github.com/TsudaKageyu/minhook).

| File | What's in it |
|---|---|
| `src/game.cpp` | Game access: version detection, addresses, script opcodes |
| `src/menu.cpp` | The menu UI |
| `src/features.cpp` | Toggles that run every frame (god mode, noclip…) |
| `src/hooks.cpp` | Drawing, input blocking, Direct3D hooks |
| `src/data.h` | Lists of vehicles, weapons, teleports |
| `src/gamedata.cpp` | Reads car colours, tuning parts, clothes and skins from the game's own data files |

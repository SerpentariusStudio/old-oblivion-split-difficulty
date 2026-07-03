# SplitDifficulty

An xOBSE plugin for original 32-bit Oblivion (`Oblivion.exe` 1.2.0.416) that
replaces the vanilla difficulty slider with two independent INI-configurable
damage multipliers:

- `FromEnemyToPlayerDamageMult` — damage the player takes in combat
- `FromPlayerToEnemyDamageMult` — damage the player deals to enemies

Unlike the vanilla slider, these knobs are fully independent — e.g. setting
both to `2.0` makes combat lethal for both sides at once, which the single
vanilla slider can never do.

Environmental damage (fall, lava, ambient) is untouched by these multipliers.

## Requirements

- Original Oblivion (`Oblivion.exe` version 1.2.0.416, Win32)
- [xOBSE](https://github.com/llde/xOBSE)

## Installation

1. Build the plugin (see below) or grab `splitdifficulty.dll` from a release.
2. Copy `splitdifficulty.dll` into `Data/OBSE/Plugins/`.
3. Launch the game once; a default `splitdifficulty.ini` is written next to
   the DLL if one doesn't already exist.
4. Edit `Data/OBSE/Plugins/splitdifficulty.ini` to taste.

## Configuration

```ini
[SplitDifficulty]
; Master on/off. 1 = enabled, 0 = plugin does nothing (no hooks installed).
Enabled=1

; Multiplier for damage the PLAYER TAKES in combat. 1.0 = vanilla, 2.0 = double,
; 0.5 = half, 0.0 = player takes no combat damage. Range 0.0-1000.
FromEnemyToPlayerDamageMult=1.0

; Multiplier for damage the PLAYER DEALS to enemies. 1.0 = vanilla, 2.0 = double,
; 0.5 = half, 0.0 = enemies take no damage from the player. Range 0.0-1000.
FromPlayerToEnemyDamageMult=1.0

; Debug log (obse_splitdifficulty.log next to Oblivion.exe). 0 = off, 1 = on.
EnableLogging=0
```

Keep the in-game difficulty slider centered — these multipliers assume a 1x
engine baseline and do not cancel the slider out.

See [docs/SPEC.md](docs/SPEC.md) for the full technical spec (hook mechanism,
gating logic, verification criteria).

## Building

Requires a Win32 (x86) MSVC toolchain and the Windows SDK. Edit the paths at
the top of `build.bat` to match your local install, then run:

```
build.bat
```

This compiles `src/splitdifficulty.cpp` with `cl.exe /MT /LD` (static CRT, no
VC-runtime dependency), producing `build/splitdifficulty.dll`, and copies it
into the configured `Data/OBSE/Plugins` directory.

## License

MIT — see [LICENSE](LICENSE).

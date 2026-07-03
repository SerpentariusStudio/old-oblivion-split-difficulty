# SplitDifficulty — xOBSE plugin spec

Original 32-bit Oblivion (`Oblivion.exe` 1.2.0.416) + xOBSE.

## 1. Goal

Replace Oblivion's single vanilla difficulty slider with **two independent
multipliers**, configured in an INI:

| INI key            | Scales                                   | Vanilla |
| ------------------ | ---------------------------------------- | ------- |
| `FromEnemyToPlayerDamageMult` | damage **the player takes** (combat)     | `1.0`   |
| `FromPlayerToEnemyDamageMult`  | damage **the player deals to enemies**   | `1.0`   |

`2.0` = double, `0.5` = half, `1.0` = unchanged, `0.0` = immune / no damage.
The two knobs are fully independent — e.g. `FromEnemyToPlayerDamageMult=2.0` +
`FromPlayerToEnemyDamageMult=2.0` makes *all* combat lethal for both sides at once, which the
single vanilla slider can never do.

## 2. Mechanism

A **vtable-slot hook** on `DamageAV_F` (the float "damage actor value"
function), vtable index `0xA9`, on the actor that is *taking* damage. Three
distinct slots, all verified against `Oblivion.exe` 1.2.0.416:

| Class            | vtable base  | slot `0xA9` target | role                  |
| ---------------- | ------------ | ------------------ | --------------------- |
| PlayerCharacter  | `0x00A73A0C` | `0x0065E530`       | player takes damage   |
| Character (NPC)  | `0x00A6FC9C` | `0x005E2BE0`       | NPC takes damage      |
| Creature         | `0x00A710F4` | `0x00625710`       | creature takes damage |

Signature (`__thiscall`):

```c
void DamageAV_F(Actor* this, UInt32 avCode, float amt, Actor* attacker);
```

- `avCode == 8` → Health. `amt < 0` → damage (we only touch these).
- `attacker` (`param_4`) — the source actor. **RE evidence:** inside
  `FUN_005E2BE0` (Character) it is forwarded to a vtable bookkeeping call
  `(*this + 0x3B8)(attacker, amt)` **only** on a Health hit — the "record who
  hit me" path. Confirmed at `0x005E2BE0`. Its value for *environmental* damage
  (fall/trap/poison) is unverified statically → see the diagnostic build (§5).

The detour scales `amt` then calls the saved original exactly once. Player slot
uses `FromEnemyToPlayerDamageMult`; Character + Creature slots use `FromPlayerToEnemyDamageMult`.

### Calling-convention trick

The vtable slot is `__thiscall` (this in ECX, rest on the stack). We emulate it
with `__fastcall` + a dummy EDX param: ECX=this, EDX=unused, remaining args land
on the stack in order, and `__fastcall` callee cleanup matches `__thiscall`.

```c
void __fastcall Detour(void* actor, void* /*edx*/, UInt32 avCode, float amt, void* attacker);
```

### Hook safety

Per slot: `VirtualProtect` → verify the slot currently equals the expected
original address (abort if not — game updated) → save original → write detour →
restore protection → `FlushInstructionCache`. Identical to the two reference
plugins.

## 3. Gating logic

Inside the detour, before scaling:

```
if avCode != 8 (Health)      -> pass through unchanged
if amt >= 0 (heal/no damage) -> pass through unchanged

COMBAT-ONLY gate:
  attacker must be a non-null actor (environmental fall/trap/poison excluded).

PLAYER slot:
  apply FromEnemyToPlayerDamageMult.

CHARACTER / CREATURE slots (enemy taking damage):
  PLAYER-DEALT-ONLY gate: attacker == *g_thePlayer, else pass through.
  apply FromPlayerToEnemyDamageMult.
```

`g_thePlayer` = `*(void**)0x00B333C4` (PlayerCharacter**). The "combat-only" and
"player-dealt-only" gates both reduce to a pointer compare on `attacker`; no SDK
needed. **Final null/self behavior of `attacker` for environmental sources is
confirmed by the diagnostic build before these gates go live.**

## 4. Configuration — `splitdifficulty.ini`

Section `[SplitDifficulty]`. Written with commented defaults next to the DLL on
first run (never clobbers user edits; missing keys self-healed).

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

Parsing rules: clamp each multiplier to `[0.0, 1000.0]`; a negative or
unparseable value resets to `1.0`. `Enabled=0` installs no hooks (game is exactly
vanilla). Keep the in-game difficulty slider at **center (50%)** — these
multipliers assume a 1× engine baseline and do not cancel the slider.

## 5. Build plan (two phases)

**Phase 1 — diagnostic build.** Hooks all three slots but does **not** scale.
With `EnableLogging=1`, every Health hit logs: which slot, `avCode`, `amt`,
`attacker` pointer and whether it is null / `== *g_thePlayer` / `== this`. The
user plays through melee, spell, bow, fall, trap, and poison damage and sends the
log. This empirically pins down the `attacker` value for each source and confirms
the combat / player-dealt gates.

**Phase 2 — scaling build.** Enable the §3 gating + multipliers based on the log.

## 6. File layout

```
src/splitdifficulty.cpp   ; the plugin (self-contained, no OBSE SDK headers)
src/exports.def           ; exports OBSEPlugin_Query / OBSEPlugin_Load
build.bat                 ; cl.exe /MT /LD Win32, copies DLL to Data/OBSE/Plugins
plugin.json               ; metadata
splitdifficulty.ini       ; reference copy of the default INI
```

Self-contained OBSE API (minimal `PluginInfo` / `OBSEInterfaceMin` structs
inline, as in `capped-damage-xobse`). Static CRT (`/MT`) — no VC-runtime
dependency.

## 7. Verification criteria

- Phase 1: log clearly shows `attacker == *g_thePlayer` for player melee/spell
  hits on an NPC, and a distinct value (null expected) for fall/trap/poison.
- Phase 2 numeric checks (slider centered, logging on):
  - `FromPlayerToEnemyDamageMult=2.0` → a player hit that did D now does ~2D to the enemy;
    NPC-vs-NPC damage unchanged.
  - `FromEnemyToPlayerDamageMult=0.5` → an enemy melee hit that did D now does ~D/2 to the
    player; fall damage unchanged (combat-only).
  - `Enabled=0` → byte-for-byte vanilla (no hook installed).
```


# Attempt History — SplitDifficulty xOBSE plugin

Newest entries at the bottom. One concise entry per attempt (RE finding, code
change, INI/config experiment, build/runtime outcome).

---

## 2026-06-26 — Design + RE groundwork (no code yet)

**Tried:** Interviewed the user and reverse-engineered the damage path in Ghidra
to design precise specs (`docs/SPEC.md`) before writing the plugin.

**Decisions locked:**
- Two independent INI multipliers: `PlayerDamageMult` (damage player takes) and
  `EnemyDamageMult` (damage player deals to enemies). `1.0` = vanilla,
  `0.0` = immune; clamp `[0, 1000]`, negatives reset to `1.0`.
- Assume the in-game difficulty slider stays centered (no slider-cancel RE).
- Enemy mult applies to **player-dealt** damage only; both mults are
  **combat-only** (exclude fall/trap/poison) via the attacker argument.
- Self-contained scaffolding (like `capped-damage-xobse`), `/MT` Win32.
- Two-phase build: diagnostic (log `attacker`) first, then scaling.
- No special block handling (engine reduces blocked damage before our hook).

**RE findings (Oblivion.exe 1.2.0.416):**
- `DamageAV_F` is vtable slot `0xA9`; `void __thiscall (this, avCode, amt, attacker)`.
- Character::DamageAV_F confirmed at `0x005E2BE0`. `attacker` (param_4) is
  forwarded to `(*this+0x3B8)(attacker, amt)` **only** on Health damage
  (`avCode==8 && amt<0`) — the "record who hit me" path → param_4 is the attacker.
- xref-to `0x005E2BE0`: Character vtable slot `0x00A6FF40` (= base `0x00A6FC9C` +
  `0xA9*4`), plus three calls from `0x0062573C/5F/7A` inside Creature::DamageAV_F
  (`0x00625710`) — creatures delegate into Character's logic.
- This Ghidra DB only has Character's function carved out; Creature/Player
  entries are undefined in the DB, but their runtime addresses (`0x00625710`,
  `0x0065E530`) are authoritative — verified by the two existing sibling plugins'
  vtable-mismatch guard.

**Result:** Inconclusive on one point — `attacker`'s value for *environmental*
damage (fall/trap/poison) could not be proven statically. Mitigation: Phase-1
diagnostic build logs it.

**Takeaway:** Reuse the proven vtable-slot hook + `__fastcall`(+dummy EDX) detour
pattern from the sibling plugins. Resolve the `attacker`-for-environmental
question empirically with the diagnostic build before enabling the combat gate.

---

## 2026-06-26 — Found the live difficulty global

**Tried:** Locate the live `fDifficulty` value so the plugin can read/warn it
in-process (Oblivion.ini is unreliable: difficulty is stored per-save and the INI
showed `iDifficultyLevel=50` but `fDifficulty=-0.5200`, i.e. stale/disagreeing).

**Result:** WORKED. The engine's INI settings table is an array of 8-byte
`{value, char* name}` entries near `0x00B14EA8`:
- `0x00B14EA8` = `bTrackLevelUps:GamePlay` (int) = 1
- `0x00B14EB0` = `fDifficulty:GamePlay` (**float**) = live difficulty (-1..+1, 0=center)
- `0x00B14EB8` = `sTrackLevelUpPath:GamePlay` (char* default)

`get_xrefs_to 0x00B14EB0`: READ by `0x0066AF74` (in `FUN_0066A740`, combat) and
WRITE by `0x005A38B5` (the difficulty setter) — confirms it is the live value.

**Takeaway:** Read `*(float*)0x00B14EB0` for the live difficulty; warn when
`fabs > 0.01`. At plugin-load it reflects the INI default; a loaded save updates
it, so the per-hit log prints it too.

---

## 2026-06-26 — Phase 1 diagnostic build: built + deployed

**Tried:** Wrote `src/splitdifficulty.cpp` (self-contained, like capped-damage):
hooks all three DamageAV_F vtable slots, logs every Health loss with the
`attacker` classified NULL/PLAYER/SELF/OTHER + live `diff=`, warns at load if not
centered. NO scaling yet (mults parsed but inert). Built via `build.bat`
(`cl /MT /LD` Win32).

**Result:** WORKED (build). `splitdifficulty.dll` (136 KB, static CRT) compiled
clean and deployed to `...\Data\OBSE\Plugins\`. `dumpbin /exports` shows both
`OBSEPlugin_Query` and `OBSEPlugin_Load`. Runtime not yet verified in-game.

**Takeaway:** Next: user launches Oblivion via OBSE, takes melee/spell/bow/fall/
trap/poison damage, and sends `obse_splitdifficulty.log`. Use the `attacker`
column to finalize the combat-only / player-dealt-only gates, then write Phase 2
(scaling). Hook addresses still need first runtime confirmation (mismatch guard
will log an ERROR if any slot differs).

---

## 2026-06-26 — Phase 1 first runtime: 2/3 hooks OK, player slot conflict

**Tried:** User launched Oblivion via xOBSE and checked obse_splitdifficulty.log.

**Result:** PARTIAL.
- Loads fine: xOBSE v22, oblivionVer=010201A0 (1.2.0.416), editor=0.
- Live difficulty read WORKS: 0.0000 (centered) from 0x00B14EB0 - mechanism validated.
- Character hook OK @ 0x00A6FF40 orig 0x005E2BE0; Creature hook OK @ 0x00A71398
  orig 0x00625710 - both match RE exactly. RE for these two slots confirmed at runtime.
- Player hook ABORTED by mismatch guard: slot 0x00A73CB0 held 0x6EA517E0 (a loaded-DLL
  detour) not vanilla 0x0065E530. Cause: oblivion_increased_damage.dll (Enabled=1) hooks
  the same PlayerCharacter vtable[0xA9] slot. cappeddamage was Enabled=0 (so Character/
  Creature were vanilla, hence our hooks succeeded there).
- No hit lines: user checked before combat.

**Takeaway:** SplitDifficulty must REPLACE oblivion_increased_damage (they fight over the
player slot). Set increased_damage.ini Enabled=0 (done 2026-06-26). Re-test for a full
log. Minor: install summary says "hooks NOT installed" when only the player slot failed
(Char/Crea actually installed) - cosmetic, resolves once the conflict is cleared.

---

## 2026-06-26 — Phase 1 runtime #2: all 3 hooks OK, attacker gating confirmed (combat)

**Tried:** Re-test after disabling increased_damage. User fought a creature (melee).

**Result:** WORKED.
- All three hooks SUCCESS (Player @0x00A73CB0/0065E530, Char @0x00A6FF40/005E2BE0,
  Crea @0x00A71398/00625710). Player-slot conflict gone.
- diff=-0.0000 (centered) throughout.
- Player DEALS to creature: `CREATURE ... attacker=18DED940 [PLAYER] victimIsPlayer=0`
  -> attacker == *g_thePlayer. Confirms the EnemyDamageMult "player-dealt only" gate
  (act iff attacker==player). This gate is robust regardless of environmental attacker
  value, since we only scale when attacker==player.
- Enemy DEALS to player: `PLAYER ... attacker=55085210 [OTHER] victimIsPlayer=1`
  -> attacker = the enemy actor, non-null. Confirms combat hits to the player carry a
  non-null attacker.
- Big 76.38 creature hit = likely power/sneak attack; normal hits ~21.

**Still missing:** environmental damage (fall/trap/poison) -> need to confirm attacker
is NULL there, which is what the PLAYER-side "combat only" gate relies on (scale iff
attacker != null). Also nice-to-have: an NPC (Character) fight and a spell/bow hit.

**Takeaway:** Enemy-side gate fully confirmed. Player-side combat-only gate pending the
environmental test. Get fall/trap/poison logs, then write Phase 2.

---

## 2026-06-26 — Phase 1 runtime #3: INI parse OK, DoT/aura observed (still no env data)

**Tried:** User set EnemyDamageMult=3.0 and fought a creature with a damage aura.

**Result:**
- INI parsed correctly (config line: EnemyDamageMult=3.000) but NOT applied (diagnostic
  build) - player hits on creature still vanilla ~19-21, confirming no scaling. Big
  104/76/44 hits = power/sneak attacks, not the multiplier.
- New: a continuous ~0.13/frame (~60/s) stream of PLAYER hits with attacker=550A7A50
  [OTHER] = the creature's damage aura/DoT; its real melee swings are the periodic ~14.x
  lines. So enemy->player combat (incl. DoT/aura) always carries a non-null actor attacker.
- Player->creature still always attacker == player (18DED940).
- Still NO environmental (fall/trap/poison) data -> [NULL] case unconfirmed.

**Takeaway:** "combat = attacker != null" gate will include actor DoT/aura (intended). A
damaging-aura enemy means many micro-hits each scaled by PlayerDamageMult in Phase 2
(cheap, but amplifies aura enemies). Need the fall-damage test for the [NULL] case.

---

## 2026-06-26 — Phase 1 runtime #4: fall damage = NULL attacker (gates finalized)

**Tried:** User fell from a high building (environmental damage test).

**Result:** DECISIVE. `PLAYER dmg=24.86 attacker=00000000 [NULL] victimIsPlayer=1`.
Environmental (fall) damage carries a NULL attacker - confirms the combat-only gate.

**Finalized gate truth table (all confirmed at runtime):**
- You -> enemy (melee/spell/bow): attacker == player -> scale by EnemyDamageMult.
- Enemy -> you (melee + DoT/aura): attacker = non-null actor -> scale by PlayerDamageMult.
- Fall / environmental: attacker == NULL -> excluded from both.

Gates: Player slot scales iff `attacker != NULL`; Character/Creature slots scale iff
`attacker == *g_thePlayer (0x00B333C4)`. Both `avCode==8 (Health) && amt<0`.

**Not separately tested:** trap and poison. Inferred: traps -> NULL (excluded like fall);
poison-from-enemy may carry the enemy actor (scaled as combat) or NULL (excluded) - the
gate behaves sensibly either way. Non-blocking.

**Takeaway:** All RE + behavioral unknowns resolved. Ready to write Phase 2 (scaling):
add the two gated multiplies to the existing detours; keep the diagnostic logging behind
EnableLogging.

---

## 2026-06-26 — Phase 2 scaling build: built + deployed

**Tried:** Added gated scaling to splitdifficulty.cpp (ScaleAndLog replaces LogHit):
- Player slot: amt *= PlayerDamageMult when attacker != NULL (env excluded).
- Char/Crea slots: amt *= EnemyDamageMult when attacker == *g_thePlayer.
Scaled amt is passed to the original (single call). Logging now optional (default
EnableLogging=0) and shows "dmg=X -> Y (xMULT)" when a hit is scaled. Cleaned
diagnostic wording in INI/headers/build.bat; plugin.json -> version 1.0.

**Result:** WORKED (build). splitdifficulty.dll rebuilt + deployed; dumpbin shows both
OBSE entry points. Creature path scales once (Creature fn calls Character fn directly,
bypassing the Character vtable detour - no double scaling). Runtime scaling not yet
verified in-game.

**Note:** User's existing INI on disk has EnemyDamageMult=3.0 + EnableLogging=1, so the
next launch runs with 3x player-dealt damage and logging on - good for verification.

**Takeaway:** Verify in-game: EnemyDamageMult=3 -> player hits ~3x (log shows "->" lines);
PlayerDamageMult=0.5 -> incoming combat halved; fall damage unchanged; Enabled=0 -> vanilla.

---

## 2026-06-26 — Phase 2 runtime verified: scaling correct

**Tried:** User fell, then fought a creature with EnemyDamageMult=3.0, PlayerDamageMult=1.0.

**Result:** WORKED.
- Fall: `PLAYER dmg=35.76 attacker=00000000 [NULL] skip` -> env damage excluded (vanilla). 
- Player->creature: `CREATURE dmg=19.37 -> 58.11 (x3.00) [PLAYER]` etc. Exact x3 math
  (19.37*3=58.11, 15.10*3=45.30, 14.95*3=44.86).
- PlayerDamageMult=1.0 -> incoming combat unchanged (logged as (x1)).

**Takeaway:** SplitDifficulty v1.0 functionally verified. Remaining optional: confirm a
PlayerDamageMult != 1.0 case, and trap/poison (inferred fine). Tell user to set
EnableLogging=0 when done.

---

## 2026-06-26 — Phase 2 runtime #2: PlayerDamageMult + trap/poison behavior

**Tried:** PlayerDamageMult=0.2, EnemyDamageMult=10. User fell, fought a creature, stood
in poison gas.

**Result:**
- EnemyDamageMult=10 exact (19.37->193.70). PlayerDamageMult=0.2 exact (13.44->2.69) -
  the incoming-scaling case now verified.
- Fall: attacker=NULL -> skipped (unchanged). 
- Poison/DoT ticking ON THE PLAYER: attacker=55089630 (a source actor) -> SCALED x0.2
  (0.27->0.05). Treated as combat.
- Gas ticking on the CREATURE: attacker=NULL -> skipped (0.04 unchanged). Same hazard,
  no source for the creature.
- NPC/other actor hitting creature (550C3630, not player) -> skipped. NPC-vs-NPC excluded.

**Key limitation:** DamageAV_F gives attacker but NOT damage type, so at this hook we can
only split on "has source actor" vs NULL - cannot separate sourced poison from melee.
Current rule: scale any sourced damage to player; exclude sourceless (fall/ambient).
Sourced poison/gas is therefore scaled as combat.

**Takeaway:** Behavior is self-consistent and matches "combat = has an attacker". If the
user wants sourced poison/gas excluded too, that needs damage-type detection (a different,
more invasive hook on the magic-effect/hit path) - a larger change. Otherwise v1.0 stands.

---

## 2026-06-26 — v1.1: chaining + EnvironmentalDamageMult (built + deployed)

**Tried:** Two changes per user request.
1. CHAINING: relaxed patch_vtable_slot - instead of aborting when the slot isn't
   pristine vanilla, it now chains (saves whatever is there as the orig, installs on
   top) as long as the current value is a committed executable code ptr (VirtualQuery
   sanity check via IsExecutableCodePtr); only truly non-code aborts. Vtable hooks
   stack: last installed runs first. splitdifficulty sorts last alphabetically -> loads
   last -> computes FIRST -> hands scaled amt down to capped/increased -> vanilla. No
   change needed to capped/increased (they load first on vanilla; ABI-safe handoff via
   shared __fastcall+dummy-EDX). Re-enabled cappeddamage.ini and
   oblivion_increased_damage.ini (Enabled=1).
2. Per-source split (user: only "with source actor" vs "without", no finer): added
   EnvironmentalDamageMult. Player slot now ALWAYS scales - attacker!=NULL uses
   PlayerDamageMult, attacker==NULL uses EnvironmentalDamageMult (default 1.0 = old skip
   behavior). Enemy slots unchanged (player-dealt only). Fall investigation NOT needed.

**Result:** WORKED (build). Rebuilt + deployed. Runtime chain order + env knob not yet
verified in-game.

**Takeaway:** Verify in-game (EnableLogging=1 on all three): split log should show
"CHAINING on top" for Player slot (onto increased-damage) and Char/Crea (onto capped),
and the chain math should compose split-first. Test EnvironmentalDamageMult != 1.0 with a
fall. If OBSE load order ever puts split first, force last via rename.

---

## 2026-06-26 — v1.1 chain verified in-game (all 3 plugins)

**Tried:** All three enabled (split mults all 1.0), logging on, fought a creature.

**Result:** WORKED. SplitDifficulty install log (both launches 17:55 & 17:56) shows
"CHAINING on top" on all three slots onto the sibling detours:
- Player @00A73CB0 -> 6E9A17E0 (increased_damage)
- Character @00A6FF40 -> 6ED618F0 (capped); Creature @00A71398 -> 6ED61930 (capped)
SplitDifficulty loads LAST -> runs FIRST -> hands scaled amt down to increased/capped ->
vanilla. capped log confirms it caps creature dmg (maxHP258 cap64.5/4s, 124->64.5 CAPPED).
SplitDifficulty passthrough at x1. No conflicts/aborts - guard relaxation works. Self-heal
also confirmed: splitdifficulty.ini now shows EnvironmentalDamageMult.

**Not yet shown numerically:** the ordering effect (splits all 1.0 = no-op). Set
EnemyDamageMult=2 and fight -> capped "incoming=" should be ~2x split's raw CREATURE dmg.

---

## 2026-06-26 — v1.2: ordering proven numerically + INI key rename

**Tried:** User set FromPlayerToEnemyDamageMult(was EnemyDamageMult)=2.0, fought a creature.

**Result:** Chain ORDER proven by aligned timestamps: split CREATURE 18.03->36.07 (x2);
capped same tick incoming=36.1 (=split's output, not raw 18.03). Power attack 104->208;
capped incoming=208 -> allowed=144.5 [CAPPED] (25% of maxHP 578). So split scales FIRST,
capped caps the scaled value. Exactly as requested.

**Rename (clarity):** PlayerDamageMult -> FromEnemyToPlayerDamageMult,
EnemyDamageMult -> FromPlayerToEnemyDamageMult (EnvironmentalDamageMult unchanged).
Updated src, docs/SPEC.md, plugin.json (v1.2), and rewrote the deployed INI preserving the
user's values (FromPlayerToEnemyDamageMult=2.0). Rebuilt + deployed.

/*
 * SplitDifficulty - xOBSE plugin for original Oblivion (32-bit, 1.2.0.416)
 *
 * Two independent INI multipliers replacing the single vanilla difficulty slider:
 *   FromEnemyToPlayerDamageMult - scales combat damage the PLAYER TAKES.
 *   FromPlayerToEnemyDamageMult  - scales combat damage the PLAYER DEALS to enemies.
 * See docs/SPEC.md.
 *
 * Gating (confirmed at runtime - see attempt-history.md):
 *   Player slot : attacker != NULL -> FromEnemyToPlayerDamageMult (combat / sourced);
 *                 attacker == NULL -> EnvironmentalDamageMult (fall/lava/ambient).
 *   Char/Crea   : attacker == player -> FromPlayerToEnemyDamageMult (player-dealt only);
 *                 else unchanged (NPC-vs-NPC / environmental damage to enemies).
 * Only Health loss (avCode==8 && amt<0). amt is scaled BEFORE the original runs
 * (amt *= mult: 1.0 = vanilla, 2.0 = double, 0.0 = no damage). The hook CHAINS:
 * it stacks on top of any existing DamageAV_F hook (capped-damage / increased-
 * damage), so loading last it computes first and hands the scaled amt down.
 * EnableLogging=1 logs each Health hit; OBSEPlugin_Load warns if not centered.
 *
 * Mechanism: vtable[0xA9] DamageAV_F hooks on the three actor classes:
 *   PlayerCharacter 0x00A73A0C[0xA9] -> 0x0065E530   (player takes damage)
 *   Character (NPC) 0x00A6FC9C[0xA9] -> 0x005E2BE0   (NPC takes damage)
 *   Creature        0x00A710F4[0xA9] -> 0x00625710   (creature takes damage)
 * Creature::DamageAV_F calls Character::DamageAV_F directly (not via vtable), so
 * a creature hit logs once (CREATURE), never double-counted as CHAR.
 *   void __thiscall DamageAV_F(Actor* this, UInt32 avCode, float amt, Actor* attacker)
 * Addresses verified against Oblivion.exe 1.2.0.416 (see attempt-history.md).
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ===================== config (splitdifficulty.ini) ===================== */
static bool  g_enabled    = true;    // [SplitDifficulty] Enabled (master switch)
static bool  g_logging    = false;   // [SplitDifficulty] EnableLogging
static float g_playerMult = 1.0f;    // [SplitDifficulty] FromEnemyToPlayerDamageMult (player takes, WITH source actor)
static float g_envMult    = 1.0f;    // [SplitDifficulty] EnvironmentalDamageMult (player takes, NO source: fall/lava/ambient)
static float g_enemyMult  = 1.0f;    // [SplitDifficulty] FromPlayerToEnemyDamageMult  (damage player deals to enemies)
static char  g_iniPath[MAX_PATH] = {0};

static void log_message(const char *fmt, ...)
{
    if (!g_logging) return;
    FILE *f = fopen("obse_splitdifficulty.log", "a");
    if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list args; va_start(args, fmt); vfprintf(f, fmt, args); va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

// Resolve "<this-dll-dir>\splitdifficulty.ini" from the DLL's own module path.
static void ResolveIniPath()
{
    HMODULE hm = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCSTR)&ResolveIniPath, &hm);
    DWORD n = GetModuleFileNameA(hm, g_iniPath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) { strcpy(g_iniPath, "splitdifficulty.ini"); return; }
    char *dot = strrchr(g_iniPath, '.');           // ...\splitdifficulty.dll -> .ini
    if (dot) strcpy(dot, ".ini"); else strcat(g_iniPath, ".ini");
}

// Write a commented default ini if none exists (never clobbers the user's edits).
static void WriteDefaultIniIfMissing()
{
    if (GetFileAttributesA(g_iniPath) != INVALID_FILE_ATTRIBUTES) return;
    FILE *f = fopen(g_iniPath, "w");
    if (!f) return;
    fprintf(f,
        "[SplitDifficulty]\n"
        "; Master on/off switch. 1 = enabled, 0 = disabled (no hooks installed,\n"
        "; game runs exactly as if this plugin weren't present). Default 1\n"
        "Enabled=1\n\n"
        "; Multiplier for damage the PLAYER TAKES in combat (melee/spells/arrows).\n"
        "; 1.0 = vanilla, 2.0 = double, 0.5 = half, 0.0 = player takes no combat\n"
        "; damage. Range 0.0-1000. (Fall/trap damage is NOT affected.) Default 1.0\n"
        "FromEnemyToPlayerDamageMult=1.0\n\n"
        "; Multiplier for damage the PLAYER DEALS to enemies (melee/spells/arrows).\n"
        "; 1.0 = vanilla, 2.0 = double, 0.5 = half, 0.0 = enemies take no damage\n"
        "; from the player. Range 0.0-1000. (Only player-dealt damage; NPC-vs-NPC\n"
        "; and environmental damage to enemies are NOT affected.) Default 1.0\n"
        "FromPlayerToEnemyDamageMult=1.0\n\n"
        "; Multiplier for damage the PLAYER TAKES from sources with NO attacking\n"
        "; actor - fall, lava, ambient gas/traps with no caster. 1.0 = vanilla,\n"
        "; 0.0 = player takes no environmental damage. Range 0.0-1000. Default 1.0\n"
        "EnvironmentalDamageMult=1.0\n\n"
        "; Debug log (obse_splitdifficulty.log next to Oblivion.exe). 0 = off, 1 = on.\n"
        "; Default 0\n"
        "EnableLogging=0\n\n"
        "; IMPORTANT: keep the in-game difficulty slider CENTERED (50%%). These\n"
        "; multipliers assume a 1x engine baseline. The plugin reads the live\n"
        "; difficulty and warns in the log if it is not centered.\n");
    fclose(f);
}

static float ClampMult(const char *raw)
{
    float v = (float)atof(raw);
    if (v < 0.0f) return 1.0f;            // negative / garbage -> vanilla
    if (v > 1000.0f) return 1000.0f;      // clamp ceiling
    return v;
}

// Self-heal: append "comment + key=defVal" to an existing INI if the key is
// absent, so an INI written by an older build picks up keys added later without
// losing the user's edits. (A missing file is handled by WriteDefaultIniIfMissing.)
static void EnsureIniKey(const char *key, const char *defVal, const char *comment)
{
    char buf[64];
    GetPrivateProfileStringA("SplitDifficulty", key, "\x01", buf, sizeof(buf), g_iniPath);
    if (strcmp(buf, "\x01") != 0) return;   // already present
    FILE *f = fopen(g_iniPath, "a");
    if (!f) return;
    fprintf(f, "\n%s%s=%s\n", comment, key, defVal);
    fclose(f);
}

static void LoadConfig()
{
    ResolveIniPath();
    WriteDefaultIniIfMissing();

    // Backfill keys added after the user's INI was first written.
    EnsureIniKey("FromEnemyToPlayerDamageMult", "1.0",
        "; Multiplier for damage the PLAYER TAKES in combat (melee/spells/arrows).\n"
        "; 1.0 = vanilla, 2.0 = double, 0.5 = half, 0.0 = no combat damage. Range 0.0-1000.\n");
    EnsureIniKey("FromPlayerToEnemyDamageMult", "1.0",
        "; Multiplier for damage the PLAYER DEALS to enemies (player-dealt only;\n"
        "; NPC-vs-NPC not affected). 1.0 = vanilla. Range 0.0-1000.\n");
    EnsureIniKey("EnvironmentalDamageMult", "1.0",
        "; Multiplier for damage the PLAYER TAKES from sources with NO attacking\n"
        "; actor - fall, lava, ambient gas/traps with no caster. 1.0 = vanilla. Range 0.0-1000.\n");
    EnsureIniKey("EnableLogging", "0",
        "; Debug log (obse_splitdifficulty.log next to Oblivion.exe). 0 = off, 1 = on.\n");

    g_enabled = GetPrivateProfileIntA("SplitDifficulty", "Enabled", 1, g_iniPath) != 0;

    char buf[64];
    GetPrivateProfileStringA("SplitDifficulty", "FromEnemyToPlayerDamageMult", "1.0", buf, sizeof(buf), g_iniPath);
    g_playerMult = ClampMult(buf);
    GetPrivateProfileStringA("SplitDifficulty", "EnvironmentalDamageMult", "1.0", buf, sizeof(buf), g_iniPath);
    g_envMult = ClampMult(buf);
    GetPrivateProfileStringA("SplitDifficulty", "FromPlayerToEnemyDamageMult", "1.0", buf, sizeof(buf), g_iniPath);
    g_enemyMult = ClampMult(buf);

    g_logging = GetPrivateProfileIntA("SplitDifficulty", "EnableLogging", 0, g_iniPath) != 0;
}

/* ===================== OBSE plugin API (minimal, self-contained) ===================== */
struct PluginInfo {
    enum { kInfoVersion = 3 };
    uint32_t     infoVersion;
    const char * name;
    uint32_t     version;
};
struct OBSEInterfaceMin {
    uint32_t obseVersion;
    uint32_t oblivionVersion;
    uint32_t editorVersion;
    uint32_t isEditor;
};

/* ===================== symbols (Oblivion.exe 1.2.0.416, image base 0x400000) ===================== */
static const uintptr_t kVtbl_PlayerCharacter = 0x00A73A0C;
static const uintptr_t kVtbl_Character       = 0x00A6FC9C;
static const uintptr_t kVtbl_Creature        = 0x00A710F4;
static const uintptr_t kPlayerPtr            = 0x00B333C4;   // PlayerCharacter** (g_thePlayer)
static const uintptr_t kDifficultyAddr       = 0x00B14EB0;   // float fDifficulty:GamePlay (live, -1..+1, 0=center)

static const int kVIdx_DamageAV_F = 0xA9;                    // vtable[0xA9] float DamageActorValue
static const int kAV_Health       = 8;                       // kActorVal_Health

static const uintptr_t kExpect_PlayerDamage = 0x0065E530;
static const uintptr_t kExpect_CharDamage   = 0x005E2BE0;
static const uintptr_t kExpect_CreaDamage   = 0x00625710;

typedef void (__thiscall *DamageFn)(void *actor, uint32_t avCode, float amt, void *attacker);
static DamageFn g_origPlayer = nullptr;
static DamageFn g_origChar   = nullptr;
static DamageFn g_origCrea   = nullptr;

static void *GetPlayer()      { return *reinterpret_cast<void **>(kPlayerPtr); }
static float GetDifficulty()  { return *reinterpret_cast<float *>(kDifficultyAddr); }

/* ===================== gated scaling (+ optional logging) =====================
 * Player slot (enemySlot=false): always scaled, but by which knob depends on the
 *   source - attacker != NULL (combat) -> FromEnemyToPlayerDamageMult; attacker == NULL
 *   (fall/lava/ambient, no source) -> EnvironmentalDamageMult.
 * Enemy slot (enemySlot=true, NPC/creature): scale only when attacker == the
 *   player (player-dealt) by FromPlayerToEnemyDamageMult; everything else (NPC-vs-NPC, env)
 *   is left unchanged.
 * Only Health loss is touched. Returns the (possibly scaled) amt for the original. */
static float ScaleAndLog(const char *slot, void *actor, uint32_t avCode, float amt,
                         void *attacker, bool enemySlot)
{
    if (avCode != kAV_Health || amt >= 0.0f) return amt;    // only Health loss

    void *player = GetPlayer();
    float mult;
    bool  gatePass;
    if (enemySlot) {
        gatePass = (attacker == player);                    // player-dealt only
        mult     = g_enemyMult;
    } else {
        gatePass = true;                                    // player always scaled (one of two knobs)
        mult     = (attacker != nullptr) ? g_playerMult : g_envMult;
    }
    float out = gatePass ? (amt * mult) : amt;

    if (g_logging) {
        const char *who = (attacker == nullptr) ? "NULL"
                        : (attacker == player)  ? "PLAYER"
                        : (attacker == actor)   ? "SELF" : "OTHER";
        if (gatePass && mult != 1.0f)
            log_message("%s dmg=%.2f -> %.2f (x%.2f) attacker=%p [%-6s] victimIsPlayer=%d diff=%.4f",
                        slot, -amt, -out, mult, attacker, who, (actor == player) ? 1 : 0, GetDifficulty());
        else
            log_message("%s dmg=%.2f attacker=%p [%-6s] %-4s victimIsPlayer=%d diff=%.4f",
                        slot, -amt, attacker, who, gatePass ? "(x1)" : "skip",
                        (actor == player) ? 1 : 0, GetDifficulty());
    }
    return out;
}

/* The vtable slot is __thiscall (this in ECX; avCode/amt/attacker on stack). We
 * emulate it with __fastcall + a dummy EDX param: ECX=this, EDX=unused, the
 * remaining args land on the stack in order, and __fastcall callee cleanup
 * matches __thiscall. Scale amt per the gate, then call the original once. */
static void __fastcall Detour_Player(void *a, void *, uint32_t av, float amt, void *atk)
{ amt = ScaleAndLog("PLAYER  ", a, av, amt, atk, false); g_origPlayer(a, av, amt, atk); }
static void __fastcall Detour_Char(void *a, void *, uint32_t av, float amt, void *atk)
{ amt = ScaleAndLog("CHAR    ", a, av, amt, atk, true); g_origChar(a, av, amt, atk); }
static void __fastcall Detour_Crea(void *a, void *, uint32_t av, float amt, void *atk)
{ amt = ScaleAndLog("CREATURE", a, av, amt, atk, true); g_origCrea(a, av, amt, atk); }

/* ===================== vtable-slot hook ===================== */
// True if p points into committed, executable memory (a sane code pointer). Used
// to decide whether a non-vanilla slot value is another plugin's detour (chain
// onto it) or garbage from a game update (abort).
static bool IsExecutableCodePtr(uintptr_t p)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery((void *)p, &mbi, sizeof(mbi)) == 0) return false;
    if (mbi.State != MEM_COMMIT) return false;
    DWORD prot = mbi.Protect & 0xFF;
    return prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
           prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}

// Install the detour, CHAINING onto whatever currently occupies the slot (vanilla
// or another plugin's hook). Whoever installs last runs first, so loading
// SplitDifficulty after capped-damage / increased-damage (alphabetically it does)
// makes SplitDifficulty compute first and hand its scaled amt down the chain. The
// saved *outOrig may be another plugin's detour - calling it is ABI-safe (the
// three share the __fastcall + dummy-EDX __thiscall emulation).
static bool patch_vtable_slot(uintptr_t vtblBase, int index, void *detour,
                              DamageFn *outOrig, uintptr_t expectOrig, const char *label)
{
    uintptr_t slotAddr = vtblBase + (uintptr_t)index * 4;
    DWORD old;
    if (!VirtualProtect((void *)slotAddr, 4, PAGE_READWRITE, &old)) {
        log_message("ERROR: VirtualProtect failed for %s slot @ %p (%lu)", label, (void *)slotAddr, GetLastError());
        return false;
    }
    uintptr_t cur = *reinterpret_cast<uintptr_t *>(slotAddr);
    if (cur == expectOrig) {
        log_message("%s: slot @ %p holds vanilla %p - hooking", label, (void *)slotAddr, (void *)cur);
    } else if (IsExecutableCodePtr(cur)) {
        log_message("%s: slot @ %p already hooked (got %p, vanilla=%p) - CHAINING on top",
                    label, (void *)slotAddr, (void *)cur, (void *)expectOrig);
    } else {
        log_message("ERROR: %s slot @ %p holds non-code %p (vanilla=%p) - aborting (game updated?)",
                    label, (void *)slotAddr, (void *)cur, (void *)expectOrig);
        VirtualProtect((void *)slotAddr, 4, old, &old);
        return false;
    }
    *outOrig = reinterpret_cast<DamageFn>(cur);
    *reinterpret_cast<uintptr_t *>(slotAddr) = reinterpret_cast<uintptr_t>(detour);
    VirtualProtect((void *)slotAddr, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)slotAddr, 4);
    log_message("SUCCESS: hooked %s DamageAV_F slot @ %p (orig now %p)", label, (void *)slotAddr, (void *)cur);
    return true;
}

static bool install_hooks()
{
    bool okP = patch_vtable_slot(kVtbl_PlayerCharacter, kVIdx_DamageAV_F, &Detour_Player,
                                 &g_origPlayer, kExpect_PlayerDamage, "Player");
    bool okC = patch_vtable_slot(kVtbl_Character, kVIdx_DamageAV_F, &Detour_Char,
                                 &g_origChar, kExpect_CharDamage, "Character");
    bool okR = patch_vtable_slot(kVtbl_Creature, kVIdx_DamageAV_F, &Detour_Crea,
                                 &g_origCrea, kExpect_CreaDamage, "Creature");
    return okP && okC && okR;
}

/* ===================== OBSE plugin entry points ===================== */
extern "C" {

bool OBSEPlugin_Query(const OBSEInterfaceMin *obse, PluginInfo *info)
{
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name        = "SplitDifficulty";
    info->version     = 1;

    LoadConfig();   // also creates the default ini next to the DLL on first run
    log_message("==================================================");
    log_message("SplitDifficulty Query - ini=%s obseVer=%08X oblivionVer=%08X editor=%u",
                g_iniPath, obse ? obse->obseVersion : 0, obse ? obse->oblivionVersion : 0,
                obse ? obse->isEditor : 0);
    return true;
}

bool OBSEPlugin_Load(const OBSEInterfaceMin *obse)
{
    log_message("config: Enabled=%d FromEnemyToPlayerDamageMult=%.3f EnvironmentalDamageMult=%.3f FromPlayerToEnemyDamageMult=%.3f EnableLogging=%d",
                g_enabled ? 1 : 0, g_playerMult, g_envMult, g_enemyMult, g_logging ? 1 : 0);

    if (obse && obse->isEditor) {
        log_message("SplitDifficulty: loaded in editor - hooks NOT installed (runtime only)");
        return true;
    }

    // Live difficulty read + centered-slider warning.
    float diff = GetDifficulty();
    log_message("Live difficulty (fDifficulty:GamePlay @ 0x%08X) = %.4f  (0.0 = centered slider; <0 easier, >0 harder)",
                (unsigned)kDifficultyAddr, diff);
    if (fabsf(diff) > 0.01f)
        log_message("WARNING: difficulty is NOT centered (|%.4f| > 0.01). SplitDifficulty multipliers assume a"
                    " centered slider (1x engine baseline). Move the in-game difficulty slider to center for"
                    " accurate results. (NOTE: at load this reflects the INI/new-game default; a loaded save can"
                    " change it - watch the per-hit 'diff=' field for the truly live value.)", diff);

    if (!g_enabled) {
        log_message("SplitDifficulty: disabled via INI (Enabled=0) - hooks NOT installed, damage unchanged");
        return true;
    }

    if (install_hooks())
        log_message("SplitDifficulty: hooks installed OK - Player=%.3f Env=%.3f Enemy=%.3f active",
                    g_playerMult, g_envMult, g_enemyMult);
    else
        log_message("SplitDifficulty: hooks NOT installed (one or more slots mismatched)");
    return true;
}

}   // extern "C"

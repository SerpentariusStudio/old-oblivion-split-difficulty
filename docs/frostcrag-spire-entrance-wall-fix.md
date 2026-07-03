# Fixing the stuck Frostcrag Spire entrance wall (Oblivion DLC)

A full write-up of a real troubleshooting session: diagnosing why the pivoting
entrance wall in Frostcrag Spire stayed shut, how the DLC plugin was reverse
engineered to find the culprit, and the exact console fix.

---

## 1. The symptom

- Inside Frostcrag Spire, standing in the entrance lobby.
- The `DLCFrostcragSpire` quest is **completed** (stage 50, `Running? No`).
- All upgrades were purchased and verified working (used `tcl`/no-clip to walk
  past the wall and confirm every room works).
- The **pivoting entrance wall never opened**, so there is no legitimate way
  through. The wall is **not selectable** with the console crosshair (it reads
  `""`), so `isdisabled` / `getlocked` etc. do nothing.

### Why the quest variables were a red herring

The `sqv DLCFrostcragSpire` dump showed:

```
Arrived = 1, Book = 1, MerchSetup = 0,
Doonce = 1, Doonce1..4 = 1, Follow = 0,
TotalCount = 5, Candles = 2
Running? No, Current stage: 50, Priority: 50
```

Those belong to `DLCFrostcragSpireScript`, which **only** tracks upgrade
purchases and ends the quest. They have nothing to do with the wall animation.
A completed quest at stage 50 is correct and expected — the wall is a separate
problem.

---

## 2. Root cause

The entrance wall is an **activator** named `TowerEntranceWallAnim01` running the
script `FrostcragSpireHiddenDoorSCRIPT01`:

```
scn FrostcragSpireHiddenDoorSCRIPT01
short open
ref mySelf
ref myTarget
...
begin onActivate
    set mySelf to getSelf
    set myTarget to getParentref
    if isActionRef player == 0        ; <-- only opens for a LINKED activator, never the player
        if open == 0
            mySelf.pms effectshockshield
            playgroup forward 0        ; <-- the open animation
            set open to 1
            set timer to 1
            set busy to 1
        endif
    endif
end
```

Key facts:

- It is a **hidden door** meant to be opened by a daisy-chained *linked
  reference* (an arrival trigger), **not** by the player clicking it. The
  `isActionRef player == 0` guard is why clicking it does nothing and why it is
  effectively un-targetable in the normal sense.
- The arrival trigger that should have activated it never fired (or fired while
  the cell was unloaded), so `playgroup forward` never ran.
- The wall's `open` flag is a **local ref variable**, separate from the quest
  variables — it was still `0`, and because nothing legitimately re-triggers it,
  the wall would stay shut forever.

The matching close/open sounds (`DRSTowerWallOpen` / `DRSTowerWallClose`) and the
mesh `Architecture\MagesTower\MagesTowerEntranceWallAnim01.NIF` confirmed the
object identity.

---

## 3. The fix (in-game console)

Oblivion's console — unlike Skyrim — **does not support the `formid.command`
dot syntax** with a literal FormID. `0C00EFC4.activate ...` fails with
`Script command "0C00EFC4.activate" not found`. You must select the reference
first with `prid`, then run the command bare.

```
prid 0C00EFC4          ; select the wall reference
playgroup forward 1    ; play its open animation directly (bypasses the isActionRef gate)
```

Use `playgroup forward 0` if `1` doesn't move it (the script itself uses `0`).
To confirm `prid` grabbed the right object: `getpos x` should return ~`1109`.

> `activate` from the console is **not** usable here — the console always counts
> the **player** as the action ref, which is exactly the case the wall's script
> ignores (`isActionRef player == 0`). `playgroup` is the clean route.

### Deriving the FormID `0C00EFC4`

- The placed reference's FormID **inside** `DLCFrostcrag.esp` is `0100EFC4`.
  The high byte `01` is a load-order placeholder (the plugin's own master
  index — it masters only `Oblivion.esm`, so its new records are prefixed `01`).
- At runtime that high byte is replaced by the plugin's **actual load-order
  index**. From `%LOCALAPPDATA%\Oblivion\Plugins.txt`, masters load first
  (`00` Oblivion.esm, `01` Mart's Monster Mod.esm), then the ESPs in listed
  order. `DLCFrostcrag.esp` is the 11th ESP → index **`0C`**.
- So `0100EFC4` → **`0C00EFC4`** at runtime. The object portion `00EFC4` is
  fixed; only the `0C` prefix depends on load order.

---

## 4. How the ESP was reverse engineered

`DLCFrostcrag.esp` is a binary TES4 plugin, but script **source** is stored as
plain text (in `SCTX` subrecords), and editor IDs / FormIDs are easy to parse.
No special tools were needed beyond Python.

### 4a. Quick string scan (find candidate names)

Extract printable strings and grep for relevant keywords:

```python
import re
data = open('DLCFrostcrag.esp', 'rb').read()
strs = [s.decode('latin1') for s in re.findall(rb'[ -~]{4,}', data)]
for s in strs:
    low = s.lower()
    if any(k in low for k in ['wall', 'pivot', 'playgroup', 'forward',
                              'backward', 'secret', 'rotate']):
        print(s)
```

This surfaced `MagesTowerEntranceWallAnim01.NIF`, `TowerEntranceWallAnim01`,
`playgroup forward 0`, `playgroup backward 0`, `DRSTowerWallOpen/Close`.

### 4b. Extract full script source from SCTX subrecords

```python
import re
data = open('DLCFrostcrag.esp', 'rb').read()
for m in re.finditer(rb'SCTX', data):
    start = m.start() + 4
    ln = int.from_bytes(data[start:start+2], 'little')   # subrecord size = uint16
    src = data[start+2:start+2+ln].decode('latin1', 'replace')
    if 'playgroup' in src.lower():
        print('===== SCRIPT =====')
        print(src)
```

This printed the full `FrostcragSpireHiddenDoorSCRIPT01` (and related teleport
scripts), revealing the `isActionRef player == 0` gate.

### 4c. Walk the record tree to get FormIDs

> **Critical gotcha:** Oblivion (TES4) uses a **20-byte** record/GRUP header
> — *not* the 24-byte header of Skyrim/Fallout. Using 24 makes the parser
> desync immediately (it read a bogus record of size 1.4 GB). Once switched to
> 20 bytes, all 1317 records parsed cleanly.

Record header layout (20 bytes): `type[4] dataSize[4] flags[4] formID[4]
(+4 unused)`. GRUP header (20 bytes): `'GRUP' groupSize[4] label[4]
groupType[4] (+4)`, where `groupSize` **includes** the header. Subrecords:
`type[4] size[uint16] data[size]`.

```python
import struct
data = open('DLCFrostcrag.esp', 'rb').read()

def records(buf):
    i, n = 0, len(buf)
    while i + 20 <= n:
        typ = buf[i:i+4]
        size = struct.unpack('<I', buf[i+4:i+8])[0]
        if typ == b'GRUP':
            yield ('GRUP', 0, buf[i+20:i+size]); i += size           # size includes header
        else:
            fid = struct.unpack('<I', buf[i+12:i+16])[0]
            yield (typ.decode('latin1'), fid, buf[i+20:i+20+size]); i += 20 + size

def walk(buf):
    for r in records(buf):
        if r[0] == 'GRUP':
            yield from walk(r[2])
        else:
            yield r

def subs(body):
    j = 0
    while j + 6 <= len(body):
        sub = body[j:j+4]
        sz = struct.unpack('<H', body[j+4:j+6])[0]
        yield sub.decode('latin1'), body[j+6:j+6+sz]
        j += 6 + sz

def edid(body):
    for s, v in subs(body):
        if s == 'EDID':
            return v.rstrip(b'\x00').decode('latin1')
    return None
```

**Find the base activator + its script:**

```python
all_recs = list(walk(data))
script_fid = next(f for t, f, b in all_recs
                  if (edid(b) or '').lower() == 'frostcragspirehiddendoorscript01')
# -> SCPT 01003F08
for t, f, b in all_recs:
    scri = next((struct.unpack('<I', v)[0] for s, v in subs(b)
                 if s == 'SCRI' and len(v) == 4), None)
    if scri == script_fid or 'wall' in (edid(b) or '').lower():
        print(t, '%08X' % f, edid(b))
# -> ACTI 01003EF8 TowerEntranceWallAnim01  (SCRI -> 01003F08)
```

**Find the placed REFR that uses that base (this gives the FormID to `prid`):**

```python
WALL_BASE = 0x01003EF8
for t, f, b in all_recs:
    if t != 'REFR':
        continue
    base = next((struct.unpack('<I', v)[0] for s, v in subs(b)
                 if s == 'NAME' and len(v) == 4), None)
    if base == WALL_BASE:
        pos = next((struct.unpack('<6f', v) for s, v in subs(b)
                    if s == 'DATA' and len(v) == 24), None)
        print('REFR %08X  base %08X  pos %s' % (f, base, pos))
# -> REFR 0100EFC4  base 01003EF8  pos (1109.5, 1.39, 0.0, 0,0,0)
```

The `pos X = 1109` here is what the in-game `getpos x` check confirms.

---

## 5. Reusable methodology (any "object won't do X" in a TES4 plugin)

1. **String-scan** the `.esp` for keywords to find editor IDs / mesh names.
2. **Dump `SCTX`** subrecords to read the actual script logic and find the
   guard/flag that's blocking you.
3. **Walk records with a 20-byte header** to map editor ID → base FormID, then
   `REFR.NAME` → the placed reference FormID. (Remember: 20-byte header, GRUP
   size includes header, subrecord size is uint16.)
4. **Translate the plugin FormID prefix** to the runtime load-order index using
   `Plugins.txt` (masters first, then ESPs in order, 0-based hex).
5. In-game: `prid <runtime FormID>`, then act on it (`playgroup`, `enable`,
   `setdestroyed`, `moveto`, etc.). Use `getpos x` to verify the selection.

### Reference data for this specific fix

| Thing | Value |
|---|---|
| Quest | `DLCFrostcragSpire`, ends at stage 50 (correct) |
| Wall base activator (editor ID) | `TowerEntranceWallAnim01` |
| Wall base FormID (in esp / runtime) | `01003EF8` / `0C003EF8` |
| Wall script | `FrostcragSpireHiddenDoorSCRIPT01` (`01003F08`) |
| **Wall placed REFR (in esp / runtime)** | `0100EFC4` / **`0C00EFC4`** |
| Mesh | `Architecture\MagesTower\MagesTowerEntranceWallAnim01.NIF` |
| Recorded X position | ~`1109` (verify with `getpos x`) |
| **Fix** | `prid 0C00EFC4` then `playgroup forward 1` (or `0`) |

> The `0C` prefix is specific to the documented load order. If the order
> changes, re-derive the index from `Plugins.txt`; the object portion `00EFC4`
> is constant.

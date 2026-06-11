### Agent Handoff: Circle of Fifths + Key Signature Support (K command)

**Date:** 2026-06-10  
**From:** Grok Chat (theory & syntax design) → Grok Build (implementation)  
**Related files:** `metalanguage.md`, `key_signature_lut.c` (to be created), parser module

#### 1. Core Concept – Circle of Fifths as Music LUT
The Circle of Fifths is a modular-arithmetic lookup structure (mod 12) that encodes key signatures via perfect fifths (+7 semitones).

- Notes indexed 0–11: `C=0, C#/Db=1, D=2, ..., B=11`
- Moving +7 (or -5) mod 12 walks the circle clockwise (sharps) / counterclockwise (flats).
- Each key has 0–7 accidentals. Sharps order: `F C G D A E B`  
  Flats order: `B E A D G C F`

This directly maps to a **key signature LUT** that tells the note parser what accidental (if any) to apply by default for each note letter in the current key.

#### 2. Proposed K Command Syntax
```
K<root>[#|b][m]
```
Examples:
- `KC`     → C major (0 accidentals)
- `KG`     → G major (1♯: F♯)
- `KD-`    or `KDb` → D♭ major (5♭)
- `KD-m`   → D minor (same signature as F major: 1♭)
- `KF#m`   → F♯ minor (same as A major: 3♯)

**Behavior:**
- Sets the active `current_key_lut[12]` (Accidental array).
- Minor keys automatically compute relative major signature (root + 3 semitones mod 12).
- Command can appear anywhere in the sequence; affects all subsequent notes until changed.

#### 3. Key Signature LUT Generation (Co5ths-driven)

```c
typedef enum { NATURAL=0, SHARP=1, FLAT=-1 } Accidental;

const int sharp_order[7] = {5, 0, 7, 2, 9, 4, 11};  // F C G D A E B
const int flat_order[7]  = {11,4,9,2,7,0,5};       // B E A D G C F

void build_key_signature(Accidental lut[12], int fifth_steps) {
    memset(lut, NATURAL, 12 * sizeof(Accidental));
    int n = abs(fifth_steps);
    const int* order = (fifth_steps >= 0) ? sharp_order : flat_order;
    Accidental acc = (fifth_steps >= 0) ? SHARP : FLAT;
    
    for (int i = 0; i < n && i < 7; i++) {
        lut[order[i]] = acc;
    }
}
```

**Usage in parser:**
When a bare note letter is encountered (e.g. `C`, `F5`, `A-`):
1. Convert letter → base index (0-11)
2. `Accidental default_acc = current_key_lut[base_index];`
3. If user explicitly wrote `+`/`#` or `-`/`b`, override the default.
4. Apply to pitch calculation / MIDI note number.

#### 4. Future Transposition Command (Separate from Key Signature)
**Note:** `T` is reserved for Tempo.

**Suggested letter:** `X` (for transpose / shift) or `S` (shift).  
**Syntax proposal:**
```
X{+|-}<semitones>
```
Examples:
- `X+1`  → everything up one semitone (C → C♯/D♭)
- `X-2`  → down two semitones
- `X0`   → reset to no transposition

This operates **after** key-signature accidental application (i.e., transpose the final pitch). Can be changed mid-sequence for modulations.

Alternative letters if `X` or `S` conflicts: `P` (pitch shift), `I` (interval), `^` (if you support symbols).

#### 5. Integration Checklist for Grok Build
- [ ] Add K command parser in metalanguage grammar / tokenizer
- [ ] Implement `build_key_signature()` and `current_key_lut[]` state
- [ ] Update note parser to consult LUT when no explicit accidental is given
- [ ] Add unit tests: `KC` + `F` → F natural; `KG` + `F` → F♯; `KD-m` + bare notes
- [ ] Implement `X` (or chosen letter) transposition on top of key signature
- [ ] Decide on enharmonic spelling preferences (flats vs sharps side of circle)
- [ ] Document interaction between K, X, explicit accidentals, and octave/duration tokens

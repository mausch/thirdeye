# Dungeon Hack validation

## Reproducing the smoke test

Build the engine, then run:

```sh
nix develop -c bash -c 'cmake -S . -B build -G Ninja $cmakeFlags -DCMAKE_BUILD_TYPE=Release && cmake --build build'
python3 scripts/dh_smoke.py build/thirdeye /path/to/dungeonhack
```

The script copies the game to a temporary directory before writing saves. It
checks automatic directory detection, opening → menu, Show Intro → opening →
menu, every generated level in both directions, live save/turn/restore,
repeatable map export, and weapon damage through the PC's attack handler.
It also selects a character from the normal new-game menu, saves through the
camp picker, restarts the process and restores through Continue, checking HP
and party position. A permadeath fixture verifies deletion of that character's
save and item slots while retaining a different character's save. Input is
injected through the control channel with SDL's
dummy video driver.

`ctest --test-dir build --output-on-failure` also checks the packed save
header, filename validation, round trip and every truncation of a sample save.
The CD parser's optional original-distribution integration test is enabled by
`THIRDEYE_TEST_DH_BIN`; `THIRDEYE_TEST_DH_DIR` additionally compares each extracted
file against an independent extraction.

## Save implementation reference

The downloaded USA distribution's `AESOP.EXE` is the reference for the DH save
format. Its runtime table at file offset `0x1c8f8` maps the resource-3 runtime
numbers to far pointers. Relevant routines (load-image linear addresses):

| Routine | Address |
| --- | --- |
| save_game | `0x10556` |
| prepare_save | `0x10765` |
| explode_save | `0x1083b` |
| change_level | `0x108d6` |
| pack a file | `0xad11` |
| restore_range | `0xb9bf` |

The save header contains character ID and seed (two little-endian dwords),
19 settings bytes, 25 visited-level bytes, and one current-level byte. Each
packed file contains a word filename length including NUL, the DOS pathname,
a dword data length, and file bytes. A dword `0xffffffff` ends the stream.
`ITEMS_nn.BIN` is a separate object-static stream. The dungeon SOP passes six
arguments to `save_game` and two destination pointers to `explode_save`.

Both shipped `.TBL` files were compared against their corresponding `.RES`
directory offset arrays: their entries match, with a 36-byte zero trailer.
The resource loader uses the embedded directories.

## Runtime and interoperability checks

The apparent cross-level stall was the game's modal manual-word challenge.
A synchronous control-channel SEND waits for that dialog's completion. The
automated transition fixture sets the kernel's verification-complete flag
before travelling between levels.

An import-name audit of all 334 HACK.RES code resources found eight missing
runtime names. Implementations now cover concatenation, ASCII lowercase,
character-save deletion, pixel drawing, main-text refresh and map-file export.
The original executable's update_file opens an existing file for modification;
the buffered implementation now maintains a write cursor so map export replaces
an existing level's map rather than appending duplicates.

Interchange was exercised with the downloaded AESOP.EXE in classic DOSBox:
the original executable loaded a Thirdeye-written slot and rendered its dungeon
and character. A new slot saved by DOS was then restored in Thirdeye with HP
34/34 and party position x=19, y=28, facing=2, level=0.
Set `THIRDEYE_TEST_DH_SAVE` to an original `SAVE_nn.BIN` to exercise the optional
packed-save interoperability regression.

## Remaining validation

Complete playthroughs and exact generator fidelity still require validation.
The automated combat check verifies weapon damage; additional spell, ranged
combat, progression and victory scenarios need end-to-end coverage. The
permadeath fixture checks save deletion before dismissing the death dialog.
The manual-word prompt also needs a normal-input regression.

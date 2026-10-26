# Dungeon Hack validation

## Reproducing the smoke test

Build the engine, then run:

```sh
nix develop -c bash -c 'cmake -S . -B build -G Ninja $cmakeFlags -DCMAKE_BUILD_TYPE=Release && cmake --build build'
python3 scripts/dh_smoke.py build/thirdeye /path/to/dungeonhack
```

The script copies the game to a temporary directory before writing saves. It
checks automatic directory detection, opening → menu, Show Intro → opening →
menu, and a live save/turn/restore round trip through the SOP handlers. Input
is injected through the control channel with SDL's dummy video driver.

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

## Remaining validation and failures

Full Dungeon Hack support remains unfinished. A forced cross-level move through
kernel message 102 stalls in `drain_event_queue`, before reaching the new
`change_level` handler. Cross-level persistence therefore still needs an
end-to-end test and the event stall needs investigation.

The smoke test verifies restoration within a running game. DOS save interchange,
complete playthroughs, combat, and generator fidelity still need independent
validation. Runtime imports including `delete_saves`, `cat_string`,
`string_force_lower`, and map-export helpers also remain to be implemented.

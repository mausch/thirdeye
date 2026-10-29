#!/usr/bin/env python3
"""Check DH item initialization against generated placements on a disposable copy."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time


def records(data):
    assert data[:1] == b"\x1a", "Invalid object stream"
    result = {}
    pos = 1
    while pos < len(data):
        slot, cls, size = struct.unpack_from("<HIH", data, pos)
        pos += 8
        result[slot] = (cls, data[pos:pos + size])
        pos += size
    assert pos == len(data)
    return result


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("engine", type=Path)
parser.add_argument("game", type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="dh-items-") as tmp:
    root = Path(tmp)
    game = root / "game"
    shutil.copytree(args.game, game)
    saves = next(p for p in game.iterdir() if p.name.lower() == "savegame")
    endpoint = str(root / "control.sock")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
               ALSOFT_DRIVERS="null", THIRDEYE_CTL=endpoint,
               THIRDEYE_BOOT="phase-two")
    with (root / "engine.log").open("w+") as log:
        process = subprocess.Popen([str(args.engine.resolve()), str(game),
                                    "--nosound", "--skip-intro"],
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            with socket.socket(socket.AF_UNIX) as sock:
                for _ in range(200):
                    try:
                        sock.connect(endpoint)
                        break
                    except (FileNotFoundError, ConnectionRefusedError):
                        assert process.poll() is None, "Engine exited during startup"
                        time.sleep(0.1)
                else:
                    raise TimeoutError("Control socket did not start")
                sock.settimeout(30)
                stream = sock.makefile("rwb", buffering=0)

                def call(command):
                    stream.write((command + "\n").encode())
                    lines = []
                    while True:
                        line = stream.readline().decode().strip()
                        if line == "ok":
                            return lines
                        if not line or line.startswith("err "):
                            raise RuntimeError(f"{command}: {line}")
                        lines.append(line)

                time.sleep(2)
                assert "cls=1972 " in call("obj 0")[0]
                # The original dungeon.Init whole dungeon creates one object
                # per 8-byte ITEMS.DAT record, starting at object slot 100.
                # Saving exposes all item statics without relying on rendering.
                call("send 2001 488 1")
                items_path = next(p for p in saves.iterdir() if p.name.lower() == "items.dat")
                placements = items_path.read_bytes()
                assert len(placements) % 8 == 0 and placements[-8:] == bytes(8)
                expected = list(struct.iter_unpack("8B", placements[:-8]))
                assert expected, "Fixture has no generated items"
                saved = records((saves / "ITEMS_01.BIN").read_bytes())
                treasure = 0
                for slot, placement in enumerate(expected, 100):
                    kind, x, y, level, aux, _, _, _ = placement
                    cls, data = saved.get(slot, (0xffffffff, b""))
                    assert cls != 0xffffffff, (
                        f"Generated item missing: slot={slot} type={kind} "
                        f"level={level} x={x} y={y}; open_feature_file(-1) must read ITEMS.DAT")
                    assert data[4] == level, (slot, cls, placement, data.hex())
                    # Level-zero creatures can carry these items and move
                    # before the control socket saves the running game.
                    if level != 0:
                        assert data[2:4] == bytes((x, y)), (slot, cls, placement, data.hex())
                    if kind == 3:
                        treasure += 1
                    if kind == 8:
                        assert cls == 2040, (slot, "coin class", cls)
                    if kind == 2:
                        assert cls == (2028 if aux == 0 else 2031), (slot, "rations class", cls)
                assert treasure, "Fixture has no treasure"
                # Level-feature records still loaded through nonnegative args.
                level_objects = records((saves / "LVL00.TMP").read_bytes())
                assert any(cls != 0xffffffff for cls, _ in level_objects.values())
                call("send 0 464 1")
                call("send 2001 488 2")
                restored = records((saves / "ITEMS_02.BIN").read_bytes())
                for slot, placement in enumerate(expected, 100):
                    assert restored[slot][0] == saved[slot][0], (slot, "class changed on restore")
                    if placement[3] != 0:
                        assert restored[slot][1] == saved[slot][1], (slot, "item changed on restore")
                print(f"PASS: {len(expected)} generated items, including {treasure} "
                      "treasures, instantiated on their expected levels; unvisited-level "
                      "coordinates match, level features loaded, and save/restore preserves items")
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            log.seek(0)
            text = log.read()
            if "Boot stopped" in text or "ERROR:" in text:
                raise RuntimeError(text)

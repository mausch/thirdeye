#!/usr/bin/env python3
"""Headless DH boot/save regression; runs on a temporary copy of game data."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("engine", type=Path)
parser.add_argument("game", type=Path)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="dh-smoke-") as tmp:
    root = Path(tmp)
    game = root / "game"
    shutil.copytree(args.game, game)

    def game_file(name):
        path = game
        for part in name.split("/"):
            match = next((entry for entry in path.iterdir()
                          if entry.name.lower() == part.lower()), None) if path.is_dir() else None
            path = match if match is not None else path / part
        return path

    expected_party = None
    expected_hp = None

    def run(mode):
        endpoint = str(root / "control.sock")
        Path(endpoint).unlink(missing_ok=True)
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   ALSOFT_DRIVERS="null", THIRDEYE_CTL=endpoint)
        env.pop("THIRDEYE_BOOT", None)
        command = [str(args.engine.resolve()), str(game), "--nosound"]
        if mode in ("direct", "death"):
            env["THIRDEYE_BOOT"] = "phase-two"
            command.append("--skip-intro")
        elif mode != "intro":
            command.append("--skip-intro")
        with (root / "engine.log").open("w+") as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                with socket.socket(socket.AF_UNIX) as sock:
                    for _ in range(100):
                        try:
                            sock.connect(endpoint)
                            break
                        except (FileNotFoundError, ConnectionRefusedError):
                            if process.poll() is not None:
                                raise RuntimeError("Engine exited during startup")
                            time.sleep(0.1)
                    else:
                        raise TimeoutError("Control socket did not start")
                    sock.settimeout(15)
                    stream = sock.makefile("rwb", buffering=0)

                    def call(command):
                        stream.write((command + "\n").encode())
                        lines = []
                        while True:
                            line = stream.readline().decode().strip()
                            if line == "ok":
                                time.sleep(0.1)
                                return lines
                            if not line or line.startswith("err "):
                                raise RuntimeError(f"{command}: {line}")
                            lines.append(line)

                    time.sleep(2)
                    if mode == "intro":
                        assert "cls=154 " in call("obj 0")[0]
                        call("key 1b")
                        time.sleep(2)
                        assert "cls=2932 " in call("obj 0")[0]
                        call("click L 150 82")
                        time.sleep(2)
                        assert "cls=154 " in call("obj 0")[0]
                        call("key 1b")
                        time.sleep(2)
                        assert "cls=2932 " in call("obj 0")[0]
                    elif mode == "death":
                        call("send 0 464 1")
                        call("poke 0 213 01")  # settings[17]: permadeath
                        # Death enters a modal end-of-game dialog. Verify the
                        # filesystem effects while the synchronous SEND waits
                        # there; this fixture ends before dismissing the dialog.
                        try:
                            call("send 32 170 0 0 0")
                        except socket.timeout:
                            assert process.poll() is None
                        assert not game_file("SAVEGAME/SAVE_01.BIN").exists()
                        assert not game_file("SAVEGAME/ITEMS_01.BIN").exists()
                        assert game_file("SAVEGAME/SAVE_02.BIN").exists()
                        titles = game_file("SAVEGAME/SAVEGAME.DIR").read_bytes().splitlines()
                        assert titles[0] == b"<empty>"
                        assert titles[1] == b"Other character"
                    elif mode == "new":
                        call("click L 150 106")
                        time.sleep(1)
                        call("click L 271 85")
                        time.sleep(1)
                        call("click L 52 50")
                        time.sleep(2)
                        assert "cls=1972 " in call("obj 0")[0]
                        hp = call("peek 32 269 4")[0]
                        assert int.from_bytes(bytes.fromhex(hp[:5]), "little") > 0
                        call("click L 17 190")
                        call("click L 225 91")
                        call("click L 180 32")
                        for char in "Smoke":
                            call(f"key {ord(char):04x}")
                        call("key 000d")
                        time.sleep(1)
                        assert b"Smoke\r\n" in game_file("SAVEGAME/SAVEGAME.DIR").read_bytes()
                        return call("peek 0 1109 4"), call("peek 32 269 4")
                    elif mode == "resume":
                        call("click L 150 94")
                        time.sleep(2)
                        call("click L 180 32")
                        time.sleep(2)
                        assert call("peek 0 1109 4") == expected_party
                        assert call("peek 32 269 4") == expected_hp
                    else:
                        # Mark the manual-word challenge complete in this test
                        # fixture. A synchronous control SEND cannot answer a
                        # modal input prompt while waiting for its own reply.
                        call("poke 0 1131 01")
                        call("send 0 102 1 2 1 2")
                        assert call("peek 0 1112 1") == ["01"]
                        call("send 0 102 19 6 0 2")
                        assert call("peek 0 1112 1") == ["00"]
                        level_count = (game_file("SAVEGAME/LEVELS.DAT").stat().st_size - 4) // 1024
                        for level in list(range(1, level_count)) + list(range(level_count - 2, -1, -1)):
                            entry = game_file(f"SAVEGAME/FEA{level:02}.DAT").read_bytes()[:8]
                            call(f"send 0 102 {entry[1]} {entry[2]} {level} {entry[3]}")
                            assert call("peek 0 1112 1") == [f"{level:02x}"]
                        before = call("peek 0 1109 4")
                        assert call("send 2001 488 1") == ["result 1"]
                        call("key 4900")
                        time.sleep(0.5)
                        assert call("peek 0 1109 4") != before
                        call("send 0 464 1")
                        assert call("peek 0 1109 4") == before
                        call("send 2001 511 0")
                        exported = (game / "maps.txt").read_bytes()
                        assert b"Level 01 in " in exported
                        assert b"Forgotten Realms" in exported
                        call("send 2001 511 0")
                        updated = (game / "maps.txt").read_bytes()
                        assert len(updated) == len(exported)
                        assert updated.count(b"Level 01 in ") == 1
                        assert game_file("SAVEGAME/SAVE_01.BIN").stat().st_size > 57
                        assert game_file("SAVEGAME/ITEMS_01.BIN").stat().st_size > 1
                        # Find a live low-level opponent in the saved object
                        # stream, approach it, and exercise weapon acquisition,
                        # attack rolls, damage and death through the PC handler.
                        records = game_file("SAVEGAME/LVL00.TMP").read_bytes()
                        pos = 1
                        opponent = None
                        while pos + 8 <= len(records):
                            slot, cls, size = struct.unpack_from("<HIH", records, pos)
                            pos += 8
                            data = records[pos:pos + size]
                            pos += size
                            if cls in (2642, 2648) and len(data) >= 15 and data[2] > 0:
                                if int.from_bytes(data[13:15], "little", signed=True) > 0:
                                    opponent = slot, data[2], data[3]
                                    break
                        assert opponent is not None, "Dungeon has no goblin/hobgoblin test opponent"
                        slot, x, y = opponent
                        initial_hp = call(f"peek {slot} 13 2")
                        call(f"send 0 102 {x - 1} {y} 0 1")
                        for _ in range(20):
                            call("poke 32 72 0000")  # clear hand recovery between test attacks
                            call("send 32 341 22 0")
                            if call(f"peek {slot} 13 2") != initial_hp:
                                break
                        else:
                            raise AssertionError("Weapon attacks did not damage the opponent")
                        levels_before = bytes.fromhex(call("peek 32 257 3")[0])
                        xp_before = struct.unpack("<III", bytes.fromhex(call("peek 32 277 12")[0]))
                        call("send 32 253 50000")
                        levels_after = bytes.fromhex(call("peek 32 257 3")[0])
                        xp_after = struct.unpack("<III", bytes.fromhex(call("peek 32 277 12")[0]))
                        assert sum(xp_after) - sum(xp_before) >= 49998
                        assert all(after >= before for before, after in zip(levels_before, levels_after))
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

    run("intro")
    run("direct")
    game_file("SAVEGAME/SAVEGAME.DIR").write_bytes(b"<empty>\r\n" * 12 + b"\x1a")
    expected_party, expected_hp = run("new")
    run("resume")
    other = bytearray(game_file("SAVEGAME/SAVE_01.BIN").read_bytes())
    other[0:4] = ((int.from_bytes(other[:4], "little") + 1) & 0xffffffff).to_bytes(4, "little")
    game_file("SAVEGAME/SAVE_02.BIN").write_bytes(other)
    titles = game_file("SAVEGAME/SAVEGAME.DIR").read_bytes()
    game_file("SAVEGAME/SAVEGAME.DIR").write_bytes(titles.replace(b"<empty>", b"Other character", 1))
    run("death")
print("DH intro, new-game, transitions, combat, save UI, restart/restore, map export and permadeath deletion passed")

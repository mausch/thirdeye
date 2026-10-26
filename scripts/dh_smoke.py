#!/usr/bin/env python3
"""Headless DH boot/save regression; runs on a temporary copy of game data."""
import argparse
import os
from pathlib import Path
import shutil
import socket
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

    def run(intro):
        endpoint = str(root / "control.sock")
        Path(endpoint).unlink(missing_ok=True)
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
                   ALSOFT_DRIVERS="null", THIRDEYE_CTL=endpoint)
        env.pop("THIRDEYE_BOOT", None)
        command = [str(args.engine.resolve()), str(game), "--nosound"]
        if not intro:
            env["THIRDEYE_BOOT"] = "phase-two"
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
                                return lines
                            if not line or line.startswith("err "):
                                raise RuntimeError(f"{command}: {line}")
                            lines.append(line)

                    time.sleep(2)
                    if intro:
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
                    else:
                        before = call("peek 0 1109 4")
                        assert call("send 2001 488 1") == ["result 1"]
                        call("key 4900")
                        time.sleep(0.5)
                        assert call("peek 0 1109 4") != before
                        call("send 0 464 1")
                        assert call("peek 0 1109 4") == before
                        assert (game / "SAVEGAME/SAVE_01.BIN").stat().st_size > 57
                        assert (game / "SAVEGAME/ITEMS_01.BIN").stat().st_size > 1
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

    run(True)
    run(False)
print("DH intro/menu chain and save/turn/restore passed")

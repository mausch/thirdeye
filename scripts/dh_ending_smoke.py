#!/usr/bin/env python3
"""Exercise DH's modal victory dialog and cross-resource ending (X11 + xdotool)."""
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
with tempfile.TemporaryDirectory(prefix="dh-ending-") as directory:
    root = Path(directory)
    game = root / "game"
    shutil.copytree(args.game, game)
    endpoint = str(root / "control.sock")
    environment = dict(os.environ, SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="dummy",
                       ALSOFT_DRIVERS="null", THIRDEYE_BOOT="phase-two", THIRDEYE_CTL=endpoint)
    with (root / "engine.log").open("w+") as log:
        process = subprocess.Popen([str(args.engine.resolve()), str(game), "--nosound", "--skip-intro"],
                                   env=environment, stdout=log, stderr=subprocess.STDOUT)
        try:
            with socket.socket(socket.AF_UNIX) as connection:
                for _ in range(100):
                    try:
                        connection.connect(endpoint)
                        break
                    except (FileNotFoundError, ConnectionRefusedError):
                        if process.poll() is not None:
                            raise RuntimeError("Engine exited during startup")
                        time.sleep(.1)
                else:
                    raise TimeoutError("Control socket did not start")
                connection.settimeout(60)
                stream = connection.makefile("rwb", buffering=0)
                stream.write(b"poke 0 1131 01\n")
                assert stream.readline().strip() == b"ok"
                # SEND enters the modal death/victory pane before chaining to
                # a fresh resource VM. Inject real SDL input while it waits.
                stream.write(b"send 0 254\n")
                for _ in range(45):
                    text = (root / "engine.log").read_text()
                    if 'Booting object "TM"' in text:
                        break
                    if process.poll() is not None:
                        raise RuntimeError("Engine exited before the ending\n" + text)
                    windows = subprocess.run(["xdotool", "search", "--pid", str(process.pid)],
                                             capture_output=True, text=True).stdout.splitlines()
                    if windows:
                        subprocess.run(["xdotool", "windowfocus", windows[-1]], check=True)
                        subprocess.run(["xdotool", "key", "--window", windows[-1], "Return"], check=True)
                    time.sleep(1)
                else:
                    raise TimeoutError("Victory did not chain to TM\n" + text)
                time.sleep(2)
                assert process.poll() is None, "Ending exited before rendering"
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
print("DH victory dialog and OPEN.RES/TM ending chain passed")

#!/usr/bin/env python3
"""FakeCamera installer.

Puts everything in place on a PS Vita / PS TV through VitaShell's FTP server,
including the lines in taiHEN's config.txt, so nothing is configured by hand:

  1. On the console, open VitaShell and press SELECT to start its FTP server;
     it shows an address such as 192.168.1.20:1337.
  2. On the PC, from the folder holding this file:
         python3 install.py 192.168.1.20
  3. Reboot the console.

What it does:
  - uploads fakecamera.suprx next to the active taiHEN config (ux0:tai or ur0:tai)
  - uploads the ready-made pictures and config.txt into ux0:data/FakeCamera
  - downloads ioplus.skprx (--ioplus FILE to give your own, --no-ioplus to skip)
    into ur0:tai, so that titles which cannot read ux0: get their picture too
  - edits the active config.txt: one "ux0:tai/fakecamera.suprx" line under *ALL
    (older fakecamerabmp/fakecamerakbmp lines are removed) and one ioplus.skprx
    line under *KERNEL; the previous file is kept as config.txt.bak on the
    console and next to this script
  - "--offline DIR" writes the memory card layout instead (plus a patched copy
    of the config.txt given with --config), to copy by hand or over USB

Only Python 3 (standard library) is needed. Works from the repository
(tools/install.py) or from the extracted release zip (install.py).
"""
import argparse
import datetime
import ftplib
import io
import os
import re
import shutil
import sys
import urllib.request

IOPLUS_URL = "https://raw.githubusercontent.com/delon5/ioplus/main/release/ioplus.skprx"
PLUGIN_RE = re.compile(r"fakecamera(bmp|kbmp)?\.suprx", re.IGNORECASE)
IOPLUS_RE = re.compile(r"ioplus[^\s]*\.skprx", re.IGNORECASE)


# ---------------------------------------------------------------------------
# Files shipped with this script
# ---------------------------------------------------------------------------

def find_payload():
    """Returns (plugin path, list of (name, path) for ux0:data/FakeCamera)."""
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        # release zip layout
        (os.path.join(here, "ux0", "tai", "fakecamera.suprx"), os.path.join(here, "ux0", "data", "FakeCamera")),
        # repository layout
        (os.path.join(here, "..", "release", "fakecamera.suprx"), os.path.join(here, "..", "templates")),
        (os.path.join(here, "..", "build", "fakecamera.suprx"), os.path.join(here, "..", "templates")),
    ]
    for plugin, data in candidates:
        if os.path.isfile(plugin) and os.path.isdir(data):
            files = [(n, os.path.join(data, n)) for n in sorted(os.listdir(data)) if n.lower().endswith((".bmp", ".txt"))]
            return os.path.abspath(plugin), files
    sys.exit("fakecamera.suprx and the FakeCamera pictures were not found next to this script")


def get_ioplus(args):
    """Returns the bytes of ioplus.skprx, or None."""
    if args.no_ioplus:
        return None
    if args.ioplus:
        with open(args.ioplus, "rb") as f:
            return f.read()
    try:
        print(f"downloading ioplus.skprx from {IOPLUS_URL}")
        with urllib.request.urlopen(IOPLUS_URL, timeout=30) as r:
            data = r.read()
        if len(data) < 1000 or len(data) > 200000:
            raise ValueError(f"unexpected size {len(data)}")
        return data
    except Exception as e:  # noqa: BLE001
        print(f"WARNING: ioplus.skprx could not be downloaded ({e}); titles which cannot read ux0: will show a black picture.")
        print("         Give the file with --ioplus FILE, or skip it with --no-ioplus.")
        return None


# ---------------------------------------------------------------------------
# taiHEN config.txt editing
# ---------------------------------------------------------------------------

def patch_config(text, plugin_line, ioplus_line):
    """Returns the config with one plugin line under *ALL and, if given, one
    ioplus line under *KERNEL. Old FakeCamera lines are removed everywhere;
    everything else is kept as it is."""
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = [l for l in text.splitlines() if not PLUGIN_RE.search(l)]

    def section_bounds(name):
        for i, l in enumerate(lines):
            if l.strip().upper() == name:
                j = i + 1
                while j < len(lines) and not lines[j].strip().startswith("*"):
                    j += 1
                return i, j
        return None

    def ensure_line(section, line, present, at_top):
        bounds = section_bounds(section)
        if bounds is None:
            if at_top:
                lines[0:0] = [section, line, ""]
            else:
                if lines and lines[-1].strip():
                    lines.append("")
                lines.extend([section, line])
            return
        i, j = bounds
        if any(present(l) for l in lines[i + 1:j]):
            return
        k = j
        while k > i + 1 and not lines[k - 1].strip():
            k -= 1
        lines.insert(k, line)

    if ioplus_line:
        ensure_line("*KERNEL", ioplus_line, lambda l: IOPLUS_RE.search(l) is not None, at_top=True)
    ensure_line("*ALL", plugin_line, lambda l: PLUGIN_RE.search(l) is not None, at_top=False)
    return newline.join(lines) + newline


# ---------------------------------------------------------------------------
# FTP (VitaShell)
# ---------------------------------------------------------------------------

class Vita:
    def __init__(self, host, port):
        self.ftp = ftplib.FTP()
        self.ftp.connect(host, port, timeout=20)
        self.ftp.login()
        self.ftp.voidcmd("TYPE I")

    def exists(self, path):
        try:
            self.ftp.size(path)
            return True
        except ftplib.all_errors:
            pass
        try:
            self.ftp.retrbinary("RETR " + path, lambda chunk: None)
            return True
        except ftplib.all_errors:
            return False

    def read(self, path):
        buf = io.BytesIO()
        self.ftp.retrbinary("RETR " + path, buf.write)
        return buf.getvalue()

    def write(self, path, data):
        self.ftp.storbinary("STOR " + path, io.BytesIO(data))

    def mkdir(self, path):
        try:
            self.ftp.mkd(path)
        except ftplib.all_errors:
            pass  # already there

    def close(self):
        try:
            self.ftp.quit()
        except ftplib.all_errors:
            pass


def install_ftp(args, plugin, files, ioplus):
    print(f"connecting to {args.host}:{args.port}")
    vita = Vita(args.host, args.port)
    try:
        # the config taiHEN really uses: ux0:tai/config.txt first, else ur0:tai/config.txt
        if vita.exists("ux0:/tai/config.txt"):
            tai = "ux0"
        elif vita.exists("ur0:/tai/config.txt"):
            tai = "ur0"
        else:
            sys.exit("no taiHEN config.txt found in ux0:tai or ur0:tai: is HENkaku/taiHEN installed?")
        print(f"active taiHEN config: {tai}:tai/config.txt")

        plugin_line = f"{tai}:tai/fakecamera.suprx"
        ioplus_line = "ur0:tai/ioplus.skprx" if ioplus else None
        old = vita.read(f"{tai}:/tai/config.txt")
        new = patch_config(old.decode("utf-8", "replace"), plugin_line, ioplus_line).encode("utf-8")

        if args.dry_run:
            print("--- config.txt after the change (dry run, nothing uploaded) ---")
            print(new.decode("utf-8"))
            return

        with open(plugin, "rb") as f:
            print(f"uploading {tai}:tai/fakecamera.suprx")
            vita.write(f"{tai}:/tai/fakecamera.suprx", f.read())

        if ioplus:
            vita.mkdir("ur0:/tai")
            print("uploading ur0:tai/ioplus.skprx")
            vita.write("ur0:/tai/ioplus.skprx", ioplus)

        vita.mkdir("ux0:/data")
        vita.mkdir("ux0:/data/FakeCamera")
        for name, path in files:
            with open(path, "rb") as f:
                data = f.read()
            print(f"uploading ux0:data/FakeCamera/{name} ({len(data) // 1024} KB)")
            vita.write(f"ux0:/data/FakeCamera/{name}", data)

        if new != old:
            stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
            backup = os.path.join(os.path.dirname(os.path.abspath(__file__)), f"config.txt.bak-{stamp}")
            with open(backup, "wb") as f:
                f.write(old)
            vita.write(f"{tai}:/tai/config.txt.bak", old)
            print(f"updating {tai}:tai/config.txt (previous copy: {tai}:tai/config.txt.bak and {backup})")
            vita.write(f"{tai}:/tai/config.txt", new)
        else:
            print(f"{tai}:tai/config.txt already up to date")
    finally:
        vita.close()
    print("done: reboot the console (or just launch a title).")


# ---------------------------------------------------------------------------
# Offline layout
# ---------------------------------------------------------------------------

def install_offline(args, plugin, files, ioplus):
    out = os.path.abspath(args.offline)
    tai_dir = os.path.join(out, args.tai, "tai")
    data_dir = os.path.join(out, "ux0", "data", "FakeCamera")
    os.makedirs(tai_dir, exist_ok=True)
    os.makedirs(data_dir, exist_ok=True)
    shutil.copy(plugin, os.path.join(tai_dir, "fakecamera.suprx"))
    for name, path in files:
        shutil.copy(path, os.path.join(data_dir, name))
    if ioplus:
        os.makedirs(os.path.join(out, "ur0", "tai"), exist_ok=True)
        with open(os.path.join(out, "ur0", "tai", "ioplus.skprx"), "wb") as f:
            f.write(ioplus)
    plugin_line = f"{args.tai}:tai/fakecamera.suprx"
    ioplus_line = "ur0:tai/ioplus.skprx" if ioplus else None
    if args.config:
        with open(args.config, "rb") as f:
            old = f.read()
        new = patch_config(old.decode("utf-8", "replace"), plugin_line, ioplus_line)
        with open(os.path.join(tai_dir, "config.txt"), "w", newline="") as f:
            f.write(new)
        print(f"patched copy of {args.config} written to {os.path.join(tai_dir, 'config.txt')}")
    else:
        print("no --config given: add these lines to your taiHEN config.txt yourself:")
        if ioplus_line:
            print(f"    *KERNEL\n    {ioplus_line}")
        print(f"    *ALL\n    {plugin_line}")
    print(f"copy the content of {out} to the root of the memory card (ux0:) and of ur0: (ur0 folder), then reboot.")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("host", nargs="?", help="IP address shown by VitaShell's FTP server")
    ap.add_argument("--port", type=int, default=1337, help="FTP port (default 1337)")
    ap.add_argument("--ioplus", metavar="FILE", help="use this ioplus.skprx instead of downloading it")
    ap.add_argument("--no-ioplus", action="store_true", help="do not install ioPlus")
    ap.add_argument("--dry-run", action="store_true", help="show the config.txt change, upload nothing")
    ap.add_argument("--offline", metavar="DIR", help="write the memory card layout to DIR instead of using FTP")
    ap.add_argument("--config", metavar="FILE", help="with --offline: your current taiHEN config.txt, to patch")
    ap.add_argument("--tai", choices=["ux0", "ur0"], default="ux0", help="with --offline: where taiHEN's config lives (default ux0)")
    args = ap.parse_args()

    if not args.host and not args.offline:
        ap.error("give the console's IP address, or --offline DIR")

    plugin, files = find_payload()
    ioplus = get_ioplus(args)
    if args.offline:
        install_offline(args, plugin, files, ioplus)
    else:
        install_ftp(args, plugin, files, ioplus)


if __name__ == "__main__":
    main()

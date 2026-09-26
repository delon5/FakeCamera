#!/usr/bin/env python3
"""Builds release/FakeCamera-<version>.zip: the plugin, the ready-made pictures
and config.txt in memory card layout, and the installer.

  python3 tools/make_package.py [--plugin build/fakecamera.suprx]
"""
import argparse
import os
import re
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

README = """FakeCamera {version} - https://github.com/delon5/FakeCamera

Fakes the camera on PlayStation TV so that camera titles work, with pictures
prepared for the titles known to use the camera.

AUTOMATIC INSTALL (needs Python 3 on the PC)
  1. On the console, open VitaShell and press SELECT to start its FTP server.
  2. On the PC, in this folder:   python3 install.py <console IP address>
  3. Reboot the console.
  install.py uploads everything, adds the lines to taiHEN's config.txt (keeping a
  backup) and installs ioPlus. Run "python3 install.py --help" for the options.

MANUAL INSTALL
  1. Copy the content of the "ux0" folder to the root of ux0: (it holds
     tai/fakecamera.suprx and data/FakeCamera/...).
  2. Add to ux0:tai/config.txt (or ur0:tai/config.txt if that is the one you use):
         *ALL
         ux0:tai/fakecamera.suprx
     and remove any older fakecamerabmp.suprx / fakecamerakbmp.suprx line.
  3. Optional, for titles which cannot read ux0: (Frobisher Says): install ioPlus
     (https://github.com/delon5/ioplus) under *KERNEL.
  4. Reboot the console.

ux0/data/FakeCamera/README.md tells what each title does with the camera and
which picture it gets. Tilting scrolls large pictures when a motion source
exists (PSVshell+ "Bt Motion" or ds34motion on PS TV).
"""


def version():
    with open(os.path.join(ROOT, "main.c"), encoding="utf-8") as f:
        m = re.search(r'#define FAKECAMERA_VERSION\s+"([^"]+)"', f.read())
    return m.group(1) if m else "dev"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--plugin", default=os.path.join(ROOT, "release", "fakecamera.suprx"))
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    ver = version()
    out = args.out or os.path.join(ROOT, "release", f"FakeCamera-{ver}.zip")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(args.plugin, "ux0/tai/fakecamera.suprx")
        templates = os.path.join(ROOT, "templates")
        for name in sorted(os.listdir(templates)):
            z.write(os.path.join(templates, name), f"ux0/data/FakeCamera/{name}")
        z.write(os.path.join(ROOT, "tools", "install.py"), "install.py")
        z.writestr("README.txt", README.format(version=ver))
    print(f"{out}: {os.path.getsize(out) // 1024} KB")


if __name__ == "__main__":
    main()

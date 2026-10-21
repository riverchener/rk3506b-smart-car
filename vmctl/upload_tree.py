#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""Recursively upload a local directory to the VM over SFTP.

Skips x86 host build artifacts that must not be shipped to the ARM board.

Usage: python upload_tree.py <local_dir> <remote_dir>
"""
import os
import posixpath
import sys
import time

import paramiko

HERE = os.path.dirname(os.path.abspath(__file__))
EXCLUDE_DIRS = {"build_vm", "__pycache__", ".git", ".qmake.stash"}
EXCLUDE_FILES = {".qmake.stash"}


def load_cfg():
    import json
    with open(os.path.join(HERE, "vm_config.json"), "r", encoding="utf-8") as fh:
        return json.load(fh)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    local_root = os.path.abspath(sys.argv[1])
    remote_root = sys.argv[2].replace("\\", "/").rstrip("/")

    cfg = load_cfg()
    cli = paramiko.SSHClient()
    cli.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    cli.connect(
        cfg["host"], int(cfg.get("port", 22)), cfg["user"], cfg["password"],
        timeout=15, banner_timeout=20, auth_timeout=20,
        look_for_keys=False, allow_agent=False,
    )
    sftp = cli.open_sftp()

    made = set()

    def ensure(remote_dir):
        if remote_dir in made:
            return
        parts = remote_dir.strip("/").split("/")
        cur = ""
        for p in parts:
            cur = cur + "/" + p
            if cur in made:
                continue
            try:
                sftp.stat(cur)
            except IOError:
                try:
                    sftp.mkdir(cur)
                except IOError:
                    pass
            made.add(cur)
        made.add(remote_dir)

    ensure(remote_root)

    files = []
    for dirpath, dirnames, filenames in os.walk(local_root):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
        for fn in filenames:
            if fn in EXCLUDE_FILES:
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, local_root).replace("\\", "/")
            files.append((full, rel))

    total = sum(os.path.getsize(f) for f, _ in files)
    print("uploading %d files, %.1f MB -> %s" % (len(files), total / 1048576.0, remote_root))
    sys.stdout.flush()

    done_bytes = 0
    t0 = time.time()
    for i, (full, rel) in enumerate(files, 1):
        rdir = posixpath.dirname(posixpath.join(remote_root, rel))
        ensure(rdir)
        rpath = posixpath.join(remote_root, rel)
        sftp.put(full, rpath)
        done_bytes += os.path.getsize(full)
        if i % 25 == 0 or i == len(files):
            el = time.time() - t0
            print("  %d/%d  %.1f/%.1f MB  (%.1f MB/s)"
                  % (i, len(files), done_bytes / 1048576.0, total / 1048576.0,
                     (done_bytes / 1048576.0) / el if el > 0 else 0))
            sys.stdout.flush()

    sftp.close()
    cli.close()
    print("DONE: %d files, %.1f MB in %.0fs"
          % (len(files), done_bytes / 1048576.0, time.time() - t0))
    return 0


if __name__ == "__main__":
    sys.exit(main())

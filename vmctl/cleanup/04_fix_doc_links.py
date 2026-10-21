#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""04_fix_doc_links.py -- rewrite delivered-doc links whose target was archived to E:.

Context: vmctl\\cleanup\\01_archive.ps1 + 01b_archive_extra.ps1 moved 3,815 files to
E:\\<backup>.  Their markdown links inside delivered docs still point at the old repo
paths, so images 404.  This script rewrites ONLY the link target / img src to the new
absolute location (file:///E:/...), leaving every other character of the doc untouched.

The source->target mapping comes from the 6 manifests in <backup>\\_manifests
(source_path -> target_path).  No path is guessed.

USAGE (stdout is ASCII-only on purpose; logs are written as UTF-8):
  python vmctl\\cleanup\\04_fix_doc_links.py scan
  python vmctl\\cleanup\\04_fix_doc_links.py apply --dry-run
  python vmctl\\cleanup\\04_fix_doc_links.py apply
  (optional) add  --backup-dir E:\\<backup>   --log <path>
"""
import argparse
import csv
import glob
import os
import re
import sys
import urllib.parse

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))

# md scopes scanned by the original audit (54 files)
MD_SCOPES = [
    (os.path.join(REPO, "build_out", "docs"), "**", "*.md"),
    (os.path.join(REPO, "docs"), "", "*.md"),
]
MD_EXTRA = [os.path.join(REPO, "README_项目结构.md")]

SKIP_SCHEMES = ("http://", "https://", "mailto:", "ftp://", "data:", "javascript:")
# (target) or (<target>) or (<target> "title")
LINK_RE = re.compile(r"\]\(\s*(?:<(?P<angle>[^<>]+)>|(?P<t>[^()\s]+))(?P<rest>[^()]*)\)")
AIMG_RE = re.compile(r"""(?P<attr>\bsrc|\bhref)\s*=\s*["'](?P<t>[^"']+)["']""")
REF_RE = re.compile(r"^(?P<pre>\s*\[[^\]]+\]:\s*)(?P<t>\S+)\s*$")


def rel(p):
    return os.path.relpath(p, REPO)


def read_text(path):
    with open(path, "rb") as f:
        raw = f.read()
    bom = raw.startswith(b"\xef\xbb\xbf")
    if bom:
        raw = raw[3:]
    return raw.decode("utf-8"), bom


def write_text(path, text, bom):
    data = text.encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    with open(path, "wb") as f:
        f.write(data)


def md_files():
    out = []
    for base, mid, pat in MD_SCOPES:
        out.extend(glob.glob(os.path.join(base, mid, pat) if mid else os.path.join(base, pat),
                             recursive=True))
    out.extend(p for p in MD_EXTRA if os.path.isfile(p))
    return sorted(set(os.path.abspath(p) for p in out))


def resolve(md_path, target):
    """target -> (kind, abs_path) ; kind in ('url','local'). abs path normalised."""
    if not target or target.startswith("#"):
        return "skip", None
    low = target.lower()
    if low.startswith(SKIP_SCHEMES):
        return "url", None
    if low.startswith("file:///"):
        p = urllib.parse.unquote(target[8:]).replace("/", os.sep)
        return "local", os.path.normpath(p)
    p = target.split("#", 1)[0]
    if not p:
        return "skip", None
    p = urllib.parse.unquote(p).replace("/", os.sep)
    if os.path.isabs(p):
        return "local", os.path.normpath(p)
    return "local", os.path.normpath(os.path.join(os.path.dirname(md_path), p))


def iter_links(line):
    """yield (start, end, target) for every link target / img src / ref def on the line."""
    for m in LINK_RE.finditer(line):
        g = "angle" if m.group("angle") is not None else "t"
        yield m.start(g), m.end(g), m.group(g)
    for m in AIMG_RE.finditer(line):
        yield m.start("t"), m.end("t"), m.group("t")
    m = REF_RE.match(line)
    if m:
        yield m.start("t"), m.end("t"), m.group("t")


def load_manifests(backup_dir):
    fmap, dmap = {}, {}
    man_dir = os.path.join(backup_dir, "_manifests")
    csvs = sorted(glob.glob(os.path.join(man_dir, "manifest*.csv")))
    if not csvs:
        raise SystemExit("no manifest*.csv under " + man_dir)
    for path in csvs:
        with open(path, "r", encoding="utf-8-sig", newline="") as f:
            for row in csv.DictReader(f):
                src = (row.get("source_path") or "").strip()
                dst = (row.get("target_path") or "").strip()
                if not src or not dst:
                    continue
                k = os.path.normpath(src).lower()
                fmap.setdefault(k, dst)
                dmap.setdefault(os.path.dirname(k), os.path.dirname(dst))
    return fmap, dmap, len(csvs)


def to_url(target_path, is_dir):
    p = target_path.replace("\\", "/")
    if is_dir and not p.endswith("/"):
        p += "/"
    return "file:///" + p


def scan(fmap, dmap):
    """return list of broken entries and stats."""
    entries, stats = [], {"md": 0, "links": 0}
    for md in md_files():
        stats["md"] += 1
        text, _ = read_text(md)
        for idx, line in enumerate(text.split("\n"), 1):
            for _s, _e, t in iter_links(line):
                kind, absp = resolve(md, t)
                if kind != "local":
                    continue
                stats["links"] += 1
                if os.path.exists(absp):
                    continue
                is_dir = not absp.lower().endswith((".png", ".jpg", ".jpeg", ".gif", ".txt",
                                                   ".md", ".json", ".pdf", ".docx", ".mp4",
                                                   ".csv", ".log", ".py", ".c", ".h", ".sh"))
                mapped, src_kind = None, "NONE"
                if not is_dir and absp.lower() in fmap:
                    mapped, src_kind = fmap[absp.lower()], "FILE"
                elif absp.lower() in dmap:
                    mapped, src_kind = dmap[absp.lower()], "DIR"
                entries.append({"md": rel(md), "line": idx, "link": t, "resolved": absp,
                                "mapped": mapped, "kind": src_kind, "is_dir": is_dir})
    return entries, stats


def apply_edits(entries, dry_run):
    by_md = {}
    for e in entries:
        if not e["mapped"]:
            continue
        by_md.setdefault(e["md"], []).append(e)
    total, files = 0, 0
    for md_rel, items in sorted(by_md.items()):
        md = os.path.join(REPO, md_rel)
        text, bom = read_text(md)
        lines = text.split("\n")
        changed = 0
        for e in items:
            new_url = to_url(e["mapped"], e["is_dir"])
            i = e["line"] - 1
            line = lines[i]
            hits = [(s, en, t) for s, en, t in iter_links(line) if t == e["link"]]
            if not hits:
                print("  !! line %d: link text not found: %s" % (e["line"], e["link"]))
                continue
            for s, en, _t in reversed(hits):
                line = line[:s] + new_url + line[en:]
                changed += 1
            lines[i] = line
        if changed:
            files += 1
            total += changed
            print("  %-58s %3d edit(s) -> %s" % (md_rel, changed, "dry-run" if dry_run else "written"))
            if not dry_run:
                write_text(md, "\n".join(lines), bom)
    return total, files


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["scan", "apply"])
    ap.add_argument("--backup-dir", default=None)
    ap.add_argument("--log", default=None)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    backup = args.backup_dir or ("E:\\" + "\u8f66\u8f7d\u5907\u4efd")
    fmap, dmap, nman = load_manifests(backup)
    print("repo      : " + REPO)
    print("backup    : " + backup)
    print("manifests : %d file(s), %d source entries" % (nman, len(fmap)))

    entries, stats = scan(fmap, dmap)
    print("scanned   : %d .md, %d local links" % (stats["md"], stats["links"]))
    mapped = [e for e in entries if e["mapped"]]
    unmapped = [e for e in entries if not e["mapped"]]
    print("broken    : %d  (mapped to backup: %d, unmapped: %d)"
          % (len(entries), len(mapped), len(unmapped)))

    log_path = args.log or os.path.join(REPO, "build_out", "docs", "broken_links_after.txt")
    os.makedirs(os.path.dirname(log_path), exist_ok=True)
    if args.mode == "scan":
        with open(log_path, "w", encoding="utf-8", newline="\n") as f:
            f.write("broken markdown links after rewrite\n")
            f.write("repo   : %s\n" % REPO)
            f.write("scanned: %d .md files\n" % stats["md"])
            f.write("broken : %d  (unmapped: %d)\n\n" % (len(entries), len(unmapped)))
            f.write("%-62s %6s  %-6s %-6s %s\n" % ("doc", "line", "map", "exists", "link"))
            f.write("-" * 150 + "\n")
            for e in entries:
                tgt = e["mapped"] or ""
                f.write("%-62s %6d  %-6s %-6s %s\n" % (e["md"], e["line"], e["kind"],
                                                       "yes" if tgt and os.path.exists(tgt) else "NO",
                                                       e["link"]))
        print("log       : " + log_path)
        return 0 if not entries else 2

    if unmapped:
        print("ABORT: %d broken link(s) have no manifest mapping; nothing written." % len(unmapped))
        for e in unmapped[:10]:
            print("   %s:%d  %s" % (e["md"], e["line"], e["link"]))
        return 3

    total, files = apply_edits(entries, args.dry_run)
    print("edited    : %d link(s) in %d file(s)%s" % (total, files, " (dry-run)" if args.dry_run else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

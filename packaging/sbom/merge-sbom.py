#!/usr/bin/env python3
"""Merge the hand-maintained vendored-dependency overlay into a syft SPDX SBOM.

syft catalogs what it can see in the shipped artifacts, but every vendored /
statically-linked / header-only dependency (sdbus-c++, ftxui, spdlog, fmt,
nlohmann-json, tl-expected, the in-tree sha256) is compiled away and carries no
package metadata in the final binaries. This script folds those packages —
authored in packaging/sbom/overlay.spdx.json — into the syft document so the
published SBOM names them with exact versions and licenses.

Deterministic: same inputs -> byte-identical output (sorted keys, overlay
packages appended in file order, no timestamps invented here). This keeps the
SBOM stable across the reproducibility double-build (beta-06 task 5.3).

Usage: merge-sbom.py <syft.spdx.json> <overlay.spdx.json> <out.spdx.json>
              [--require name1,name2,...]
A package already present in the syft output (matched on name + versionInfo) is
NOT duplicated, so syft correctly detecting one of these is harmless. --require
fails the merge unless every named package is present in the final SBOM — the
release gate that catches a broken/empty overlay before an artifact ships.
"""
import argparse
import json
import sys


def die(msg):
    sys.stderr.write(f"merge-sbom: {msg}\n")
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser(prog="merge-sbom.py")
    ap.add_argument("syft")
    ap.add_argument("overlay")
    ap.add_argument("out")
    ap.add_argument("--require", default="",
                    help="comma-separated package names that MUST be in the result")
    ns = ap.parse_args()
    syft_path, overlay_path, out_path = ns.syft, ns.overlay, ns.out

    with open(syft_path, encoding="utf-8") as f:
        doc = json.load(f)
    with open(overlay_path, encoding="utf-8") as f:
        overlay = json.load(f)

    if doc.get("spdxVersion") != "SPDX-2.3":
        die(f"unexpected syft spdxVersion {doc.get('spdxVersion')!r}; expected SPDX-2.3")

    packages = doc.setdefault("packages", [])
    relationships = doc.setdefault("relationships", [])
    seen = {(p.get("name"), p.get("versionInfo")) for p in packages}
    existing_ids = {p.get("SPDXID") for p in packages}

    added = 0
    for pkg in overlay.get("packages", []):
        spdxid = pkg.get("SPDXID")
        if not spdxid:
            die(f"overlay package {pkg.get('name')!r} has no SPDXID")
        key = (pkg.get("name"), pkg.get("versionInfo"))
        if key in seen:
            continue  # already present (syft found it, or a prior merge); no dup
        if spdxid in existing_ids:
            die(f"overlay SPDXID {spdxid} collides with a different syft package id")
        packages.append(pkg)
        seen.add(key)
        existing_ids.add(spdxid)
        relationships.append({
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": spdxid,
        })
        added += 1

    creators = doc.setdefault("creationInfo", {}).setdefault("creators", [])
    tag = "Tool: devmgr-sbom-overlay-merge"
    if tag not in creators:
        creators.append(tag)

    required = [n for n in ns.require.split(",") if n]
    if required:
        present = {p.get("name") for p in packages}
        missing = [n for n in required if n not in present]
        if missing:
            die(f"required package(s) absent from merged SBOM: {missing}")

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2, sort_keys=True, ensure_ascii=False)
        f.write("\n")

    sys.stderr.write(
        f"merge-sbom: merged {added} vendored package(s) into {out_path} "
        f"({len(packages)} total)\n"
    )


if __name__ == "__main__":
    main()

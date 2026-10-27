#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Validate both tool JSON documents on a small lower tree.
set -eu
# The oracle intentionally leaves its hit list to process exit (as in hostile-user.sh).
ASAN_OPTIONS=${ASAN_OPTIONS:-detect_leaks=0}; export ASAN_OPTIONS
build=$1
tmp=$(mktemp -d "${TMPDIR:-/tmp}/moob-json.XXXXXX")
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/lower"
touch "$tmp/lower/a\"b"
if python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.moob.kind", b"\x01decision")' "$tmp/lower/a\"b"; then
    expected=1
    # names a writer of the tree chooses: an invalid attribute name and a private directory
    # name that end up inside moobfs-check strings
    mkdir -p "$tmp/lower/dom/p\"q\\r"
    chmod 0700 "$tmp/lower/dom/p\"q\\r"
    touch "$tmp/lower/dom/p\"q\\r/f"
    python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.moob.kind", b"\x01x"); os.setxattr(sys.argv[2], "user.moob.a\"}, {\"path\": \"forged\\", b"\x01x")' "$tmp/lower/dom/p\"q\\r/f" "$tmp/lower/a\"b"
else
    # Reuse the adversarial host-test fixture when this filesystem refuses user xattrs.
    "$build/hostile" plant "$tmp/lower" 1 8 > /dev/null
    expected=0
fi
"$build/moobfs-index" --json "$tmp/lower" kind eq string decision > "$tmp/index.json"
"$build/moobfs-index" "$tmp/lower" kind eq string decision > "$tmp/index.txt"
"$build/moobfs-check" "$tmp/lower" > "$tmp/check.json" || [ "$?" -eq 2 ]
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert isinstance(d["dir"],str); assert isinstance(d["count"],int) and d["count"] == len(d["results"]); assert isinstance(d["results"],list); assert all(isinstance(x["ino"],int) and isinstance(x["path"],str) for x in d["results"]); assert (d["count"] == 1 and d["results"][0]["path"] == "a\"b") if int(sys.argv[2]) else True' "$tmp/index.json" "$expected"
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); lines=open(sys.argv[2]).read().splitlines(); assert lines == [str(x["ino"])+"\t"+x["path"] for x in d["results"]]' "$tmp/index.json" "$tmp/index.txt"
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert isinstance(d["format"],dict); assert isinstance(d["format"]["state"],str); assert isinstance(d["format"]["version"],int); assert isinstance(d["format"]["supported"],int); assert isinstance(d["problems"],list); assert all(isinstance(x["path"],str) and isinstance(x["attribute"],str) and isinstance(x["problem"],str) for x in d["problems"]); assert all(isinstance(d[k],int) for k in ("files","typed_attributes","identities","identity_conflicts","same_id_other_namespace","problem_count")); assert d["problem_count"] == len(d["problems"])' "$tmp/check.json"
"$build/moobfs-check" --domain-depth 1 "$tmp/lower" > "$tmp/depth.json" || [ "$?" -eq 2 ]
python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["domain_depth"] == 1; assert isinstance(d["left_out_count"],int) and d["left_out_count"] == len(d["left_out"]); assert all(isinstance(x["path"],str) and isinstance(x["reason"],str) for x in d["left_out"])' "$tmp/depth.json"
if [ "$expected" -eq 1 ]; then
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); bad=[x for x in d["problems"] if x["path"] == "a\"b"]; assert [x["attribute"] for x in bad] == ["a\"}, {\"path\": \"forged\\"], bad; assert not any(x["path"] == "forged" for x in d["problems"])' "$tmp/check.json"
    python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); r=[x["reason"] for x in d["left_out"] if x["path"] == "dom/p\"q\\r/f"]; assert r == ["directory dom/p\"q\\r: more private than the domain directory"], r' "$tmp/depth.json"
fi
echo "TOOLS_JSON_PASS: index and check JSON parsed and typed (xattr fixture=$expected)"

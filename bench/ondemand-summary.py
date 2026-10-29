# SPDX-License-Identifier: GPL-2.0-only
"""bench/ondemand-summary: one run of bench/ondemand.sh beside a reference run, as Markdown.

Usage: ondemand-summary.py REFERENCE_TIMES RUN_TIMES [SYSTEM_TXT]

Both files are the times.txt that bench/ondemand.sh writes. The table lists, for the reference
and for the run, the mount time, the first QUERY2 on one domain with a cold and a warm cache,
the second QUERY2, the index memory with one domain in use, and the mount-wide QUERY. It is
written for the weekly workflow (.github/workflows/bench.yml, journal 0238), which shows it in
the job summary: a trend to watch, not a comparison with a test or an interval
(docs/benchmark-plan.md, revision of 2026-10-19).

Exit status 1 when the counts differ from the reference's (results of the query, inodes in the
index with one domain built and with all): the data set is deterministic (bench-gen --seed 1),
so a different count is a correctness or design change to explain, not a timing. Standard library only.
"""
import re
import sys

LINE = re.compile(r"^(cold|warm) (.*)$")
FIELD = re.compile(r"(\w+)=([0-9]+)")
REFUSED = re.compile(r"mount_wide_query=refused\((\w+)\)")


def parse(path):
    """Returns {'cold': [fields, ...], 'warm': [...], 'wide_cold': fields, 'wide_warm': fields}."""
    runs = {"cold": [], "warm": [], "wide_cold": None, "wide_warm": None}
    with open(path, encoding="utf-8") as f:
        for line in f:
            m = LINE.match(line.strip())
            if not m:
                continue
            fields = {}
            for k, v in FIELD.findall(m.group(2)):
                fields.setdefault(k, []).append(int(v))
            if "mount_wide_query" in m.group(2):
                # refused by the build limits of ADR-0019/ADR-0020 (bench/ondemand.sh, journal 0238)
                r = REFUSED.search(m.group(2))
                fields["refused"] = r.group(1) if r else None
                runs["wide_" + m.group(1)] = fields
            else:
                runs[m.group(1)].append(fields)
    if not runs["cold"] or not runs["warm"] or not runs["wide_cold"] or not runs["wide_warm"]:
        raise SystemExit(f"ondemand-summary: {path} is not a complete times.txt")
    return runs


def ms(values):
    return ", ".join(f"{v / 1000:.3f} s" if v >= 1000 else f"{v} ms" for v in values)


def first(runs, mode, key, index=0):
    return [r[key][index] for r in runs[mode]]


def wide(runs, mode):
    w = runs["wide_" + mode]
    if w["refused"]:
        return f"refused ({w['refused']}) after {ms(w['after_ms'])}"
    return ms(w["mount_wide_query_ms"])


def refused(runs):
    return bool(runs["wide_cold"]["refused"] or runs["wide_warm"]["refused"])


def mb(b):
    return f"{b / 1e6:.1f} MB"


def rows(runs):
    # in the per-domain lines, used/entries/inodes appear twice: after the mount, then after the
    # first query; index 1 is the second
    return [
        ("mount, cold cache", ms(first(runs, "cold", "mount_ms"))),
        ("mount, warm cache", ms(first(runs, "warm", "mount_ms"))),
        ("first `QUERY2` on `ns3`, cold", ms(first(runs, "cold", "first_query_ms"))),
        ("first `QUERY2` on `ns3`, warm", ms(first(runs, "warm", "first_query_ms"))),
        ("second `QUERY2`", ms(first(runs, "cold", "second_query_ms") + first(runs, "warm", "second_query_ms"))),
        ("index memory, one domain built", mb(runs["cold"][0]["used"][1])),
        ("mount-wide `QUERY`, cold", wide(runs, "cold")),
        ("mount-wide `QUERY`, warm", wide(runs, "warm")),
        ("index memory after the mount-wide `QUERY`", mb(runs["wide_warm"]["used"][0])),
        ("results of the query; inodes indexed (one domain)",
         f"{runs['cold'][0]['results'][0]}; {runs['cold'][0]['inodes'][1]}"),
        ("inodes indexed after the mount-wide `QUERY`", str(runs["wide_warm"]["inodes"][0])),
    ]


def main(argv):
    if len(argv) not in (3, 4):
        raise SystemExit(__doc__.split("\n\n")[1])
    ref, run = parse(argv[1]), parse(argv[2])
    print("| | reference (Mac mini, ext4, SATA SSD, module of journal 0098) | this run |")
    print("| --- | --- | --- |")
    for (name, a), (_, b) in zip(rows(ref), rows(run)):
        print(f"| {name} | {a} | {b} |")
    if len(argv) == 4:
        print()
        print("```")
        with open(argv[3], encoding="utf-8") as f:
            print(f.read().rstrip())
        print("```")
    # the per-domain counts always; the mount-wide one only when neither side was refused
    same = rows(ref)[-2][1] == rows(run)[-2][1]
    if not refused(ref) and not refused(run):
        same = same and rows(ref)[-1][1] == rows(run)[-1][1]
    print()
    if refused(run):
        print("The mount-wide `QUERY` was refused by the build limits of ADR-0019 and ADR-0020 "
              "(`index_build_work`, the ten-second deadline), which postdate the reference; its "
              "inode count is not compared.")
        print()
    if same:
        print("Counts equal to the reference's (same data set, `bench-gen --seed 1`).")
        return 0
    print("**Counts differ from the reference's**: the data set is deterministic, so this is a "
          "correctness or design change to explain, not a timing.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

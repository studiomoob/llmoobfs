# 0263 — Private cursor fixture ownership; real-VM validation queued

Date: 2026-11-03. Status: fixture correction validated in userspace; actual VM integration pending.
Started after 15:13 Brasília; timestamps of controls are retained in local receipts. Calendar
time, not measured engineering effort. Estimated effort: S. Continues the authorized batch 11
follow-up and journal 0261; no feature, UAPI or kernel correction is added.

## Finding during VM preparation

The new 0600 cursor output of 4611940 exposes a suite-fixture dependency: root copies the
uid 1001 cursor into a tampered file, then tests that cursor as uid 1001. With the private
mode, the root-owned copy is unreadable; the test would fail at file access instead of
exercising kernel cursor validation. This was reproduced with the actual query binary
and the deterministic ioctl shim of 0261: root-owned 0600 copy returned 1/Permission denied;
changing ownership to 1001 made the same corrupted input reach ioctl and return q2/ESTALE.
This is a test-fixture defect, not a demonstrated kernel authorization defect.

## Correction

Give each lower's cursor sequence its own mktemp directory, owned by uid 1001 and retaining
mktemp's 0700 mode. Store c1/ct there, assert new output is 0600, and chown the copied tamper
fixture to 1001 before the uid 1001 query. Keep the three altered offsets 5, 15, 119 and the
existing continuation, wrong-query/order, fallback and remount expectations. Remove only
that owned temporary directory at the sequence's end. A private non-sticky directory also
avoids relying on repeated root writes to a uid-owned file directly in sticky /tmp.
The release/shrinker cursor sequence remains unchanged and is included in the future VM run.
No change is made to immutable 61ee4f7 or the active corrected campaign.

## Actual validation and limits

The real Linux cp/private-directory/uid transitions were exercised with the corrected query
binary and a deterministic ioctl shim. All three offsets reached the shim and returned
q2 116; each copied cursor remained 0600 and owned by 1001. No module was loaded and no
VM was booted. This does not establish kernel MAC validation, fallback or remount correctness.
sh -n passes. The first local ShellCheck attempt could not run because it is not installed
on the workstation; the initial remote stdin check omitted .shellcheckrc and reported the
existing SC2015 idiom warnings. Repeat with the repository's SC2015 exclusion is recorded
below when complete; do not describe the unconfigured invocation as a clean check.

The prepared next run uses the exact new source snapshot in a disposable native-KVM guest:
MOOB_ONLY="ext4 btrfs xfs release" exercises all existing per-lower cursor expectations and
release/resume. Resource ownership and source hashes must be checked before boot; the actual
VM and applicable KASAN/batch gates are still pending. No duplicate monitor or test was started.

## Infrastructure observation

The physical Debian did not answer direct SSH; the already-verified Mac-mini proxy path
returned no route to host. Both previously supplied addresses (.161/.61) failed ARP/ping
from the Mac mini. Three bounded unicast ARP requests used the real Mac-mini identity and
the previously captured Debian MAC, with zero replies. No route, firewall, sshd, neighbor
table, interface or power setting was changed. This does not prove the host is powered off;
its current address/power state is unknown. The maintainer was asked for the current screen IP.
The Mac mini keeps the active long campaign; no competing VM is started there.

## Next step

Use the prepared immutable snapshot once Debian access returns and its reservation is
verified, or when the Mac-mini campaign releases resources. Record actual exit markers,
console diagnostics and source hashes before accepting VM integration. No new publication,
remote alert dismissal, PR/merge, G6 mounted campaign or Azure action is authorized.

## Final preparation checks
ShellCheck with the repository SC2015 exclusion completed exit0 with zero diagnostics.
sh -n and git diff --check passed. The immutable source archive/run recipe is prepared
locally after this commit; no invocation of the real VM recipe has occurred.

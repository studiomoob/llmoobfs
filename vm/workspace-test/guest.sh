#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Runs as root inside the disposable Debian 13 guest of `make workspace-test` (ADR-0035,
# journal 0273): the user-space commands moobfs mount, status and workspace on the image's
# kernel, after `make install`. One phase; prints markers. The oracles are stat(1), getfacl(1),
# id(1) and what each user can read through runuser, not the commands' own output.
set -eu
cd /home/tester
SRC=/home/tester/llmoobfs
KVER=6.12.113+deb13-amd64   # the kernel of the 2026-10-21 cloud image (journal 0206)
LIST=/usr/local/share/moobfs/supported-kernels
M=/srv/memory
W=$M/proj
mark() { echo "$*"; }
fail() { echo "MOOBFS_WORKSPACE_FAIL: $*"; exit 1; }
as() { u=$1; shift; runuser -u "$u" -- "$@"; }
# expect <status> <log> <text> <command...>: the command exits <status> and says <text>
expect() {
	want=$1 log=$2 text=$3
	shift 3
	rc=0
	"$@" >"$log" 2>&1 || rc=$?
	[ "$rc" -eq "$want" ] && grep -q "$text" "$log" || { cat "$log"; fail "$*: exit $rc, not $want with '$text'"; }
}
no_access_acl() { ! getfattr -n system.posix_acl_access "$1" >/dev/null 2>&1; }
owner_mode() { stat -c '%U:%G %a' "$1"; }

setup() {
	[ "$(uname -r)" = "$KVER" ] || fail "the image booted $(uname -r), not $KVER"
	export DEBIAN_FRONTEND=noninteractive
	apt-get update -q >/dev/null
	apt-get install -y -q build-essential attr acl python3 "linux-headers-$KVER" >/dev/null
	mkdir -p "$SRC" && tar -xf src.tar -C "$SRC" && chown -R tester:tester "$SRC"
	(cd "$SRC" && make install >install.log 2>&1) || { tail -n 20 "$SRC/install.log"; fail "make install"; }
	[ -f "$LIST" ] && grep -qx "$KVER" "$LIST" || fail "make install did not put the supported-kernels list at $LIST"
	modprobe moobfs_layer
	install -d -m 0700 /srv/moobfs
	install -d -m 0755 /srv/moobfs/lower /srv/moobfs/lower1 /srv/open /srv/open/lower "$M" /srv/mem1
	mark "INSTALLED: $(echo /usr/local/bin/moobfs*) and $LIST"
}

mount_checks() {
	expect 65 open.log 'users other than root can reach' moobfs mount /srv/open/lower "$M"
	mark "OPEN_LOWER_REFUSED"
	cp "$LIST" list.bak
	echo 6.12.999+deb13-amd64 >"$LIST"
	expect 69 unlisted.log 'is not in' moobfs mount /srv/moobfs/lower "$M"
	cp list.bak "$LIST"
	mark "UNLISTED_KERNEL_REFUSED"
	moobfs mount /srv/moobfs/lower "$M" --nolinks >mount.log 2>&1 || { cat mount.log; fail "moobfs mount"; }
	findmnt -n -o FSTYPE "$M" | grep -qx moobfs_layer || fail "not mounted"
	findmnt -n -o OPTIONS "$M" | grep -q 'domain_depth=2' || fail "domain_depth: $(findmnt -n -o OPTIONS "$M")"
	findmnt -n -o OPTIONS "$M" | grep -q 'nolinks' || fail "nolinks: $(findmnt -n -o OPTIONS "$M")"
	moobfs info "$M" | grep -q 'directories 2 levels below' || fail "moobfs info: depth"
	mark "MOUNTED: $(findmnt -n -o OPTIONS "$M")"
	expect 65 layer-lower.log 'itself on a moobfs_layer mount' moobfs mount "$M" /srv/mem1
	mark "LAYER_LOWER_REFUSED"
	moobfs mount /srv/moobfs/lower1 /srv/mem1 --domain-depth 1 >/dev/null || fail "mount at depth 1"
	expect 69 depth1.log 'needs domain_depth=2' moobfs workspace create /srv/mem1 proj --agents a1
	umount /srv/mem1
	mark "DEPTH_1_REFUSED"
}

create_checks() {
	useradd -m auditor
	moobfs workspace create "$M" proj --agents a1,a2 --create-users --dry-run --json >dry.json ||
		fail "dry run"
	python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); assert d["dry_run"] and not d["complete"] and len(d["steps"]) == 9 and not any(s["done"] for s in d["steps"]), d' dry.json ||
		{ cat dry.json; fail "dry run JSON"; }
	! getent group moob-proj >/dev/null && ! id a1 >/dev/null 2>&1 && [ ! -e "$W" ] ||
		fail "the dry run changed something"
	mark "DRY_RUN: 9 steps printed, nothing done"
	expect 65 nouser.log 'pass --create-users' moobfs workspace create "$M" proj --agents a1
	expect 65 badname.log 'invalid namespace name' moobfs workspace create "$M" Proj --agents a1
	expect 65 shared.log 'shared area' moobfs workspace create "$M" proj --agents a1,shared
	expect 77 notroot.log 'run it as root' as auditor moobfs workspace create "$M" proj
	moobfs workspace create "$M" proj --agents a1,a2 --create-users >create.log 2>&1 ||
		{ cat create.log; fail "workspace create"; }
	for a in a1 a2; do
		id -nG "$a" | tr ' ' '\n' | grep -qx moob-proj || fail "$a is not in moob-proj"
		[ "$(getent passwd "$a" | cut -d: -f7)" = /usr/sbin/nologin ] || fail "$a's shell"
		[ "$(owner_mode "$W/$a")" = "$a:$a 700" ] || fail "$W/$a: $(owner_mode "$W/$a")"
	done
	[ "$(owner_mode "$W")" = "root:moob-proj 750" ] || fail "$W: $(owner_mode "$W")"
	[ "$(owner_mode "$W/shared")" = "root:moob-proj 2770" ] || fail "shared: $(owner_mode "$W/shared")"
	[ "$(getfacl -cpd "$W/shared" | grep -v '^$')" = "$(printf 'user::rwx\ngroup::rwx\nother::---')" ] ||
		{ getfacl -pd "$W/shared"; fail "default ACL of shared"; }
	no_access_acl "$W" && no_access_acl "$W/shared" || fail "an access ACL after create"
	expect 1 again.log 'exists; nothing was changed' moobfs workspace create "$M" proj --agents a1
	mark "CREATED: $(owner_mode "$W") / shared $(owner_mode "$W/shared") / a1 $(owner_mode "$W/a1"); default ACL u::rwx,g::rwx,o::---"
}

isolation_checks() {
	as a1 sh -c "echo 'a1 private' >$W/a1/m1.md"
	as a1 moobfs-attr set "$W/a1/m1.md" kind string note
	as a2 sh -c "echo 'a2 private' >$W/a2/m2.md"
	as a2 moobfs-attr set "$W/a2/m2.md" kind string note
	as a1 sh -c "echo 'shared by a1' >$W/shared/s1.md"
	as a1 moobfs-attr set "$W/shared/s1.md" kind string note
	[ "$(owner_mode "$W/shared/s1.md")" = "a1:moob-proj 660" ] || fail "s1.md: $(owner_mode "$W/shared/s1.md")"
	no_access_acl "$W/shared/s1.md" || fail "s1.md has an access ACL (extended)"
	[ "$(as a1 moobfs query "$W/a1" kind exists - -)" = m1.md ] || fail "a1's query of its area"
	expect 77 a2-on-a1.log 'permission denied' as a2 moobfs query "$W/a1" kind exists - -
	if as a2 cat "$W/a1/m1.md" 2>/dev/null; then fail "a2 read a1's file"; fi
	if as a1 ls "$W/a2" 2>/dev/null; then fail "a1 listed a2's area"; fi
	[ "$(as a2 moobfs query "$W/shared" kind exists - -)" = s1.md ] || fail "a2's query of shared"
	as a2 cat "$W/shared/s1.md" | grep -q 'shared by a1' || fail "a2 cannot read s1.md"
	as a2 sh -c "echo 'a2 adds' >>$W/shared/s1.md" || fail "a2 cannot write s1.md"
	if as auditor ls "$W" 2>/dev/null; then fail "auditor listed the namespace"; fi
	mark "ISOLATION: a1 and a2 see their own areas only; a1's shared file 0660 without ACL, read and written by a2 through moob-proj"
}

grant_checks() {
	moobfs workspace grant "$M" proj auditor ro >grant-ro.log 2>&1 || { cat grant-ro.log; fail "grant ro"; }
	getfacl -cp "$W" | grep -q '^user:auditor:r-x' || fail "no u:auditor:r-x on $W"
	getfacl -cp "$W/shared" | grep -q '^user:auditor:r-x' || fail "no u:auditor:r-x on shared"
	getfacl -cpd "$W/shared" | grep -q '^user:auditor:r-x' || fail "no default u:auditor:r-x"
	[ "$(stat -c %a "$W")" = 750 ] && [ "$(stat -c %a "$W/shared")" = 2770 ] || fail "modes changed by grant"
	as a2 sh -c "echo 'after the grant' >$W/shared/s2.md"
	as a2 moobfs-attr set "$W/shared/s2.md" kind string note
	as auditor cat "$W/shared/s2.md" | grep -q 'after the grant' || fail "auditor cannot read s2.md"
	if as auditor sh -c "echo x >>$W/shared/s2.md" 2>/dev/null; then fail "auditor wrote s2.md"; fi
	if as auditor cat "$W/shared/s1.md" 2>/dev/null; then fail "auditor read s1.md, made before the grant"; fi
	if as auditor ls "$W/a1" 2>/dev/null; then fail "auditor listed a1's area"; fi
	[ "$(as auditor moobfs query "$W/shared" kind exists - -)" = s2.md ] || fail "auditor's query of shared"
	mark "GRANT_RO: auditor reads s2.md (made after the grant) and queries it, cannot write it, cannot read s1.md or a1's area"
	moobfs workspace revoke "$M" proj auditor ro >revoke-ro.log 2>&1 || { cat revoke-ro.log; fail "revoke ro"; }
	no_access_acl "$W" && no_access_acl "$W/shared" || fail "an access ACL left after revoke"
	[ "$(stat -c %a "$W")" = 750 ] && [ "$(stat -c %a "$W/shared")" = 2770 ] || fail "modes after revoke"
	[ "$(getfacl -cpd "$W/shared" | grep -v '^$')" = "$(printf 'user::rwx\ngroup::rwx\nother::---')" ] ||
		{ getfacl -pd "$W/shared"; fail "default ACL after revoke"; }
	if as auditor cat "$W/shared/s2.md" 2>/dev/null; then fail "auditor read s2.md after revoke"; fi
	expect 77 revoked-query.log 'permission denied' as auditor moobfs query "$W/shared" kind exists - -
	mark "REVOKE_RO: ACLs back to minimal, modes 750 and 2770, auditor refused"
	moobfs workspace grant "$M" proj auditor rw >grant-rw.log 2>&1 || { cat grant-rw.log; fail "grant rw"; }
	grep -q 'next login' grant-rw.log || fail "grant rw did not mention the next login"
	id -nG auditor | tr ' ' '\n' | grep -qx moob-proj || fail "auditor not in moob-proj"
	as auditor sh -c "echo 'auditor writes' >$W/shared/s3.md" || fail "auditor cannot write in shared"
	moobfs workspace revoke "$M" proj auditor rw >revoke-rw.log 2>&1 || { cat revoke-rw.log; fail "revoke rw"; }
	if id -nG auditor | tr ' ' '\n' | grep -qx moob-proj; then fail "auditor still in moob-proj"; fi
	if as auditor sh -c "echo x >$W/shared/s4.md" 2>/dev/null; then fail "auditor wrote after revoke rw"; fi
	mark "GRANT_RW: auditor joins moob-proj and writes in shared; after revoke it cannot"
}

list_checks() {
	moobfs workspace list "$M" --count kind --json >list.json || fail "workspace list"
	python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
ns = d["namespaces"]
assert [n["name"] for n in ns] == ["proj"] and ns[0]["group"] == "moob-proj" and ns[0]["mode"] == "0750", ns
areas = {a["name"]: a for a in ns[0]["areas"]}
assert sorted(areas) == ["a1", "a2", "shared"], areas
assert areas["a1"]["count"] == 1 and areas["a2"]["count"] == 1 and areas["shared"]["count"] == 2, areas
assert areas["shared"]["mode"] == "2770" and areas["shared"]["default_acl"] and not areas["shared"]["acl"], areas
' list.json || { cat list.json; fail "list as root"; }
	as a1 moobfs workspace list "$M" --count kind --json >list-a1.json || fail "workspace list as a1"
	python3 -c '
import json, sys
areas = {a["name"]: a for a in json.load(open(sys.argv[1]))["namespaces"][0]["areas"]}
assert areas["a1"]["count"] == 1 and areas["shared"]["count"] == 2, areas
assert areas["a2"]["count"] is None and areas["a2"]["error"], areas
' list-a1.json || { cat list-a1.json; fail "list as a1"; }
	mark "LIST: root counts a1=1 a2=1 shared=2; a1 counts its area and shared and is refused a2's"
}

status_checks() {
	moobfs status --json >status.json || fail "moobfs status"
	python3 -c '
import json, sys
d = json.load(open(sys.argv[1]))
m = d["module"]
assert m["loaded"] and m["version"] == sys.argv[3] and m["srcversion"] == sys.argv[4], m
assert [x["mount"] for x in d["mounts"]] == [sys.argv[2]] and d["mounts"][0]["domain_depth"] == 2, d
' status.json "$M" "$(cat /sys/module/moobfs_layer/version)" "$(cat /sys/module/moobfs_layer/srcversion)" ||
		{ cat status.json; fail "status JSON"; }
	moobfs status "$M" | grep -q "^module: .*version $(cat /sys/module/moobfs_layer/version)" || fail "status text"
	mark "STATUS: module $(cat /sys/module/moobfs_layer/version) srcversion $(cat /sys/module/moobfs_layer/srcversion), one mount at depth 2"
}

# The cost ADR-0035 states: the agents' files pass the homogeneity rule in their areas, shared
# files do not (owned by an agent, the domain directory by root) and are answered with
# per-result checks. moobfs-check's user-space oracle of the rule lists what is left out.
cost_checks() {
	rc=0
	moobfs-check --domain-depth 2 "$M" >check.json || rc=$?
	[ "$rc" -eq 0 ] || { cat check.json; fail "moobfs-check exit $rc"; }
	python3 -c '
import json, sys
left = [x["path"] for x in json.load(open(sys.argv[1]))["left_out"]]
has = lambda p: any(x.endswith(p) for x in left)
assert has("shared/s1.md") and has("shared/s2.md"), left
assert not has("a1/m1.md") and not has("a2/m2.md"), left
' check.json || { cat check.json; fail "left_out"; }
	mark "COST: moobfs-check clean; shared files left out of the partitions (per-result checks), agent files in"
}

phase0() {
	setup
	mount_checks
	create_checks
	isolation_checks
	grant_checks
	list_checks
	status_checks
	cost_checks
	umount "$M"
	mark "PHASE_DONE: 0"
}

case ${1:-} in
0) phase0 ;;
*) fail "usage: guest.sh 0" ;;
esac

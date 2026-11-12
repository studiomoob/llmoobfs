#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Guest side of vm/apparmor-local.sh (journal 0290). AppArmor is the kernel's default major LSM on
# Debian; the guest loads the test profiles of vm/apparmor/moobtest.profiles and answers three
# questions with the exit status of each operation and the denials in the kernel log:
#   1. unconfined root mounts, reads, writes, sets attributes and queries through the layer; for a
#      confined client, which path AppArmor checks through the layer (the layer's or the lower's);
#   2. a confined mounter and a confined reader: whose profile needs the lower path;
#   3. whether a deny rule on one path of a file is bypassed through the other path, compared with
#      overlayfs and a bind mount of the same lower.
# Arguments: repository copy, build directory. A failed check prints MOOBFS_APPARMOR_FAIL and the
# run goes on, so that a negative round shows every step that changed.
set -u
PATH=$PATH:/usr/sbin:/sbin; export PATH
R=$1; B=$2
fails=0
fail() { echo "MOOBFS_APPARMOR_FAIL: $1"; fails=$((fails + 1)); }
die() { echo "MOOBFS_APPARMOR_FAIL: $1"; dmesg | tail -8 | sed "s/^/  dmesg: /"; exit 1; }

mountpoint -q /sys/kernel/security || mount -t securityfs none /sys/kernel/security || die securityfs
lsm=$(cat /sys/kernel/security/lsm)
echo "  kernel $(uname -r), lsm=$lsm"
case ",$lsm," in *,apparmor,*) ;; *) die "AppArmor is not an active LSM in the guest" ;; esac
[ -e /sys/kernel/security/apparmor/profiles ] || die "no apparmorfs"
modprobe ext4 && modprobe overlay || die modprobe
insmod "$R/layer/moobfs_layer.ko" || die insmod
apparmor_parser -r -K "$R/vm/apparmor/moobtest.profiles" || die "apparmor_parser"
echo "  profiles loaded: $(grep -c '^moobaa_.* (enforce)$' /sys/kernel/security/apparmor/profiles) in enforce mode"
echo 0 > /proc/sys/kernel/printk_ratelimit; echo 10000 > /proc/sys/kernel/printk_ratelimit_burst

S=/run/aa
mkdir -p $S && mount -t tmpfs none $S || die tmpfs
mkdir $S/bin $S/lower $S/layer $S/layerm $S/layerx $S/layers $S/ovl $S/ovls $S/bind || die dirs
cp /bin/busybox "$B/query" "$B/moobfs-attr" "$B/listdir" $S/bin/ || die "copy tools"
BB=$S/bin/busybox
mount -t ext4 /dev/vda $S/lower || die "lower mount"
L=$S/lower
mkdir $L/t && for f in rw ro none secret; do echo "$f" > $L/t/$f; done || die files

# exit status of one command run under a profile
c() { p=$1; shift; aa-exec -p "$p" -- "$@" < /dev/null > /dev/null 2>&1; echo $?; }
op() {  # op <profile> <name> <path>
    case $2 in
    read) c "$1" $BB cat "$3" ;;
    stat) c "$1" $BB stat "$3" ;;
    append) c "$1" $BB sh -c "echo x >> $3" ;;
    list) c "$1" $S/bin/listdir "$(dirname "$3")" ;;
    setx) c "$1" $S/bin/moobfs-attr set "$3" kind string v ;;
    create) c "$1" $BB touch "$(dirname "$3")/new-$4" ;;
    esac
}
denials() { dmesg | grep 'apparmor="DENIED"'; }
# both statuses zero, or both non-zero
same() { { [ "$1" = 0 ] && [ "$2" = 0 ]; } || { [ "$1" != 0 ] && [ "$2" != 0 ]; }; }

# ---- 1a. unconfined root ----------------------------------------------------------------------
mount -t moobfs_layer -o lower=$L none $S/layer || die "layer mount by root"
u=0
grep -q '^rw$' $S/layer/t/rw || u=1
echo w >> $S/layer/t/rw && grep -q '^w$' $L/t/rw || u=2
for f in rw ro none secret; do $S/bin/moobfs-attr set $S/layer/t/$f kind string secret > /dev/null || u=3; done
uq=$($S/bin/query $S/layer/t q2 kind eq string secret 2>&1 | tr '\n' ' ')
echo "  unconfined query2: $uq"
for f in rw ro none secret; do echo "$uq" | grep -q "[[:space:]]${f}[[:space:]]" || u=4; done
echo "  unconfined root: status=$u (0 mount+read+write+setxattr+QUERY2)"
[ $u = 0 ] && echo "APPARMOR_UNCONFINED_PASS: unconfined root mounts, reads, writes, sets attributes and queries through the layer" ||
    fail "unconfined root through the layer (status $u)"

# QUERY2 for a confined client without a rule for "none" (path-based LSMs do not filter results, ADR-0011)
qc=$(aa-exec -p moobaa_layer -- $S/bin/query $S/layer/t q2 kind eq string secret 2>&1; echo "exit=$?")
echo "$qc" | sed 's/^/  query2 as moobaa_layer: /'
if echo "$qc" | grep -q '[[:space:]]none$'; then
    echo "  APPARMOR_QUERY_NOTE: QUERY2 names a file the client's profile does not grant (ADR-0011 item 3: path-based LSMs do not filter results)"
fi

# ---- 1b. which path AppArmor checks for a client through the layer ----------------------------
dmesg -c > /dev/null
differ=0; denied=0; layerok=0; lowerdeny=0; lowerallowed=0; n=0
for f in rw ro none; do
    for o in read stat append list setx create; do
        n=$((n + 1))
        d=$(op moobaa_both $o $L/t/$f d$n); l=$(op moobaa_both $o $S/layer/t/$f l$n)
        ly=$(op moobaa_layer $o $S/layer/t/$f y$n); lw=$(op moobaa_lower $o $S/layer/t/$f w$n)
        [ "$d" = 0 ] || denied=$((denied + 1))
        if same "$d" "$l"; then r=same; else r=DIFFERS; differ=$((differ + 1)); fi
        same "$ly" "$l" || layerok=$((layerok + 1))
        if [ "$l" = 0 ] && [ "$lw" != 0 ]; then lowerdeny=$((lowerdeny + 1)); fi
        if [ "$l" != 0 ] && [ "$lw" = 0 ]; then lowerallowed=$((lowerallowed + 1)); fi
        echo "  $o $f: both(direct)=$d both(layer)=$l layer-rules(layer)=$ly lower-rules(layer)=$lw $r"
    done
done
denials > $S/q1.txt
nl=$(grep -c 'profile="moobaa_lower" name="/run/aa/layer/' $S/q1.txt)
echo "  q1: differ=$differ denied_directly=$denied layer_rules_mismatch=$layerok lower_rules_refused=$lowerdeny lower_rules_allowed=$lowerallowed"
echo "  q1 denials naming a layer path for the lower-rules profile: $nl"
grep 'profile="moobaa_lower"' $S/q1.txt | head -2 | sed 's/^.*apparmor=/  /'
[ $denied -gt 0 ] || fail "q1: nothing was denied directly; the step proves nothing"
[ $differ = 0 ] || fail "q1: $differ operations differ between the lower file and the layer"
[ $differ = 0 ] && [ $denied -gt 0 ] && echo "APPARMOR_PARITY_PASS: with rules on both paths, every operation through the layer agrees with the lower file"
if [ $layerok = 0 ] && [ $lowerdeny -gt 0 ] && [ $lowerallowed = 0 ] && [ "$nl" -gt 0 ]; then
    echo "APPARMOR_LAYERPATH_PASS: AppArmor checks a client through the layer by the layer's path; rules on the lower path neither grant nor are needed"
else
    fail "q1: the path AppArmor checks through the layer is not the layer's (mismatch=$layerok refused=$lowerdeny allowed=$lowerallowed logged=$nl)"
fi

# ---- 2. confined mounter, confined reader ----------------------------------------------------
dmesg -c > /dev/null
mm=$(c moobaa_mounter $BB mount -t moobfs_layer -o lower=$L none $S/layerm)
mx=$(c moobaa_mounter_noro $BB mount -t moobfs_layer -o lower=$L none $S/layerx)
echo "  confined mounts: layerm=$mm layerx=$mx"
[ "$mm" = 0 ] && [ "$mx" = 0 ] || die "q2: a confined mounter could not mount the layer"
mr=$(op moobaa_reader read $S/layerm/t/ro); ml=$(op moobaa_reader list $S/layerm/t/ro)
ma=$(op moobaa_reader append $S/layerm/t/rw); ms=$(op moobaa_reader stat $S/layerm/t/rw)
mc=$(op moobaa_reader create $S/layerm/t/rw m)
xr=$(op moobaa_reader read $S/layerx/t/ro); xa=$(op moobaa_reader append $S/layerx/t/rw)
# control: the reader has no rule on the lower, directly
dr=$(op moobaa_reader read $L/t/ro)
denials > $S/q2.txt
rl=$(grep -c 'profile="moobaa_reader" name="/run/aa/lower/' $S/q2.txt)
xl=$(grep -c 'profile="moobaa_mounter_noro" name="/run/aa/lower/t/ro"' $S/q2.txt)
echo "  q2 reader through layerm: read=$mr list=$ml append=$ma stat=$ms create=$mc; through layerx: read=$xr append=$xa; lower directly: read=$dr"
echo "  q2 denials: reader on a lower path=$rl mounter_noro on lower/t/ro=$xl"
grep 'name="/run/aa/lower/' $S/q2.txt | sed 's/^.*apparmor=/  /' | head -4
umount $S/layerm $S/layerx
[ "$dr" != 0 ] || fail "q2 control: the reader read the lower file directly; its profile is not what the step assumes"
if [ "$mr$ml$ma$ms$mc" = 00000 ] && [ "$rl" = 0 ]; then
    echo "APPARMOR_MOUNTER_PASS: a reader with rules on the layer path only reads, lists, appends and creates through a confined mounter's layer"
else
    fail "q2: the reader through a confined mounter's layer was refused or checked on the lower path (read=$mr list=$ml append=$ma stat=$ms create=$mc lower-path denials=$rl)"
fi
if [ "$xr" != 0 ] && [ "$xl" -gt 0 ]; then
    echo "APPARMOR_MOUNTER_RULES_PASS: the lower path is checked against the mounter's profile"
else
    fail "q2: a read the mounter's profile denies on the lower path went through (read=$xr logged=$xl)"
fi

# ---- 3. bypass of a deny rule by the other path ------------------------------------------------
dmesg -c > /dev/null
mount -t overlay -o lowerdir=$L overlay $S/ovl || die "overlay mount"
mount --bind $L $S/bind || die "bind mount"
sl=$(op moobaa_denylower read $L/t/secret); sy=$(op moobaa_denylower read $S/layer/t/secret)
so=$(op moobaa_denylower read $S/ovl/t/secret); sb=$(op moobaa_denylower read $S/bind/t/secret)
echo "  deny on the lower path: lower=$sl layer=$sy overlayfs=$so bind=$sb"
ty=$(op moobaa_denylayer read $S/layer/t/secret); tl=$(op moobaa_denylayer read $L/t/secret)
echo "  deny on the layer path: layer=$ty lower=$tl"
kd=$(c moobaa_denyalias $BB ln $L/t/secret $L/t/alias-d); ky=$(c moobaa_denyalias $BB ln $S/layer/t/secret $S/layer/t/alias-l)
vd=$(c moobaa_denyalias $BB mv $L/t/secret $L/t/moved-d); vy=$(c moobaa_denyalias $BB mv $S/layer/t/secret $S/layer/t/moved-l)
echo "  second name of a denied file: link direct=$kd layer=$ky; rename direct=$vd layer=$vy"
self=$(aa-exec -p moobaa_self -- $BB sh -c "$BB mount -t moobfs_layer -o lower=$L none $S/layers || exit 90
    $BB mount -t overlay -o lowerdir=$L overlay $S/ovls || exit 91
    $BB cat $S/layers/t/secret > /dev/null 2>&1; a=\$?; $BB cat $S/ovls/t/secret > /dev/null 2>&1; b=\$?
    $BB cat $S/layers/t/ro > /dev/null 2>&1; echo \$a \$b \$?" 2>&1 < /dev/null)
echo "  self-mount (secret through own layer, own overlayfs, ro through own layer): $self"
denials > $S/q3.txt
grep 'secret\|alias\|moved' $S/q3.txt | sed 's/^.*apparmor=/  /' | head -8
umount $S/layers $S/ovls $S/ovl $S/bind 2> /dev/null
[ "$sl" != 0 ] && [ "$ty" != 0 ] || fail "q3 control: a deny rule did not deny on its own path"
# the path-based model: a rule names a path, not a file. The layer must behave as overlayfs does.
if same "$sy" "$so"; then
    echo "APPARMOR_ALIAS_SAME_AS_OVERLAYFS: a deny rule on the lower path applies to the layer path as it does to overlayfs (layer=$sy overlayfs=$so bind=$sb)"
else
    fail "q3: the layer differs from overlayfs for a deny on the lower path (layer=$sy overlayfs=$so)"
fi
if [ "$kd" != 0 ] && [ "$ky" != 0 ] && [ "$vd" != 0 ] && [ "$vy" != 0 ]; then
    echo "APPARMOR_LINK_PASS: no second name for a denied file, by link or rename, through the layer or directly"
else
    fail "q3: a second name for a denied file (link direct=$kd layer=$ky, rename direct=$vd layer=$vy)"
fi
# shellcheck disable=SC2086 # three words, split on purpose
set -- $self
if [ "$#" = 3 ] && [ "$1" != 0 ] && [ "$2" != 0 ] && [ "$3" = 0 ]; then
    echo "APPARMOR_SELFMOUNT_PASS: a process denied the lower file cannot read it through a layer it mounts itself, as with overlayfs"
else
    fail "q3: self-mount (layer, overlayfs, control) = $self"
fi
echo "MOOBFS_APPARMOR_RESULT: fails=$fails"

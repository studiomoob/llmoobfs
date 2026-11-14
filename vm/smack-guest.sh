#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Guest side of vm/smack-local.sh (journal 0294). Smack is the only major LSM of the guest's
# kernel. Smack decides by the label of the file (security.SMACK64), not by its path, so the
# questions are whether the layer shows the lower's labels and takes the lower's decisions:
#   1. labels read through the layer, also when a confined process reaches the file first;
#   2. read, append, stat, list, create and attribute writes by a labelled, unprivileged process,
#      on the lower file and through the layer, for files it may and may not reach;
#   3. labels of files created through the layer, plain and in a transmuting directory;
#   4. QUERY2 for a process that may not read some of the results (ADR-0011: the per-result
#      check asks inode_permission, which Smack answers);
#   5. relabelling through the layer, by root and by an unprivileged process.
# Arguments: repository copy, build directory. A failed check prints MOOBFS_SMACK_FAIL and the run
# goes on, so that a negative round shows every step that changed.
set -u
PATH=$PATH:/usr/sbin:/sbin; export PATH
R=$1; B=$2
fails=0
fail() { echo "MOOBFS_SMACK_FAIL: $1"; fails=$((fails + 1)); }
die() { echo "MOOBFS_SMACK_FAIL: $1"; dmesg | tail -8 | sed "s/^/  dmesg: /"; exit 1; }

mountpoint -q /sys/kernel/security || mount -t securityfs none /sys/kernel/security || die securityfs
lsm=$(cat /sys/kernel/security/lsm)
echo "  kernel $(uname -r), lsm=$lsm"
case ",$lsm," in *,smack,*) ;; *) die "Smack is not an active LSM in the guest" ;; esac
F=/sys/fs/smackfs
mountpoint -q $F || mount -t smackfs smackfs $F || die smackfs
[ -w $F/load2 ] || die "no smackfs load2"
ATTR=/proc/self/attr/smack/current
[ -e $ATTR ] || ATTR=/proc/self/attr/current
echo "  root's label: $(cat $ATTR)"
insmod "$R/layer/moobfs_layer.ko" || die insmod
echo 0 > /proc/sys/kernel/printk_ratelimit; echo 10000 > /proc/sys/kernel/printk_ratelimit_burst

# Rules for the subject Sub: Rd read only, Rw read and write, Hid nothing, Dir a directory where
# it may create, Tr a transmuting directory. Labels not named here get Smack's defaults.
for r in "Sub Rd r" "Sub Rw rwa" "Sub Hid -" "Sub Dir rwxat" "Sub Tr rwxat"; do
    echo "$r" > $F/load2 || die "rule $r"
done
S=/run/sm
mkdir -p $S && mount -t tmpfs -o mode=755 none $S || die tmpfs
mkdir $S/lower $S/layer || die dirs
BB=/bin/busybox; SP=/usr/bin/setpriv; Q=$B/query; MA=$B/moobfs-attr; LD=$B/listdir
mount -t ext4 /dev/vda $S/lower || die "lower mount"
L=$S/lower; Y=$S/layer
mkdir -m 777 $L/t $L/t/dir $L/t/tr || die "lower dirs"
for f in rd rw hid; do echo "$f" > $L/t/$f && chmod 666 $L/t/$f || die "file $f"; done
lab() { setfattr -n security.SMACK64 -v "$2" "$1"; }
lab $L/t/rd Rd && lab $L/t/rw Rw && lab $L/t/hid Hid && lab $L/t/dir Dir && lab $L/t/tr Tr || die labels
setfattr -n security.SMACK64TRANSMUTE -v TRUE $L/t/tr || die transmute
get() { getfattr --absolute-names --only-values -n security.SMACK64 "$1" 2> /dev/null; }

# exit status of one command run as uid 1000, no capabilities, Smack label $1
c() {
    l=$1; shift
    # shellcheck disable=SC2016 # $0 and $@ are the inner shell's, expanded there
    $BB sh -c 'echo "$0" > '"$ATTR"' || exit 97
        exec '"$SP"' --reuid=1000 --regid=1000 --clear-groups --inh-caps=-all --bounding-set=-all -- "$@"' \
        "$l" "$@" < /dev/null > /dev/null 2>&1
    echo $?
}
op() {  # op <label> <name> <path> <tag>
    case $2 in
    read) c "$1" $BB cat "$3" ;;
    append) c "$1" $BB sh -c "echo x >> $3" ;;
    stat) c "$1" $BB stat "$3" ;;
    list) c "$1" "$LD" "$3" ;;
    create) c "$1" $BB touch "$3/new-$4" ;;
    setx) c "$1" "$MA" set "$3" kind string v ;;
    esac
}
same() { { [ "$1" = 0 ] && [ "$2" = 0 ]; } || { [ "$1" != 0 ] && [ "$2" != 0 ]; }; }

mount -t moobfs_layer -o lower=$L none $Y || die "layer mount by root"

# self-test of the harness: the subject runs (uid 1000, no capabilities, label Sub), may read Rd,
# may not read Hid, directly on the lower; without these the steps below prove nothing
st=$(c Sub $BB true); sr=$(c Sub $BB cat $L/t/rd); sh_=$(c Sub $BB cat $L/t/hid)
sid=$($BB sh -c 'echo Sub > '"$ATTR"' && exec '"$SP"' --reuid=1000 --regid=1000 --clear-groups --inh-caps=-all --bounding-set=-all -- '"$BB"' sh -c "id -u; cat /proc/self/attr/current"' 2> /dev/null | tr '\n' ' ')
echo "  harness self-test: run=$st read-Rd=$sr read-Hid=$sh_ (uid and label: $sid)"
[ "$st" = 0 ] && [ "$sr" = 0 ] && [ "$sh_" != 0 ] || die "harness self-test (run=$st read-Rd=$sr read-Hid=$sh_)"

# ---- 1. labels through the layer ---------------------------------------------------------------
# first contact by the confined subject, then by root (journal 0104: an inode set up by a
# confined first caller must not stay unlabelled)
sync; echo 2 > /proc/sys/vm/drop_caches
first=$(op Sub stat $Y/t/hid x)
bad=0
for f in rd rw hid dir tr; do
    a=$(get $L/t/$f); b=$(get $Y/t/$f)
    echo "  label $f: lower=$a layer=$b"
    [ -n "$a" ] && [ "$a" = "$b" ] || bad=$((bad + 1))
done
echo "  layer root's label: $(get $Y) (lower root: $(get $L))"
[ $bad = 0 ] && echo "SMACK_LABELS_PASS: every label read through the layer equals the lower's, the first contact being a confined process (stat=$first)" ||
    fail "labels: $bad differ between the lower and the layer"

# ---- 2. decisions ------------------------------------------------------------------------------
differ=0; denied=0; allowed=0; n=0
for f in rd rw hid; do
    for o in read append stat setx; do
        n=$((n + 1))
        d=$(op Sub $o $L/t/$f d$n); y=$(op Sub $o $Y/t/$f y$n)
        if [ "$d" = 0 ]; then allowed=$((allowed + 1)); else denied=$((denied + 1)); fi
        if same "$d" "$y"; then r=same; else r=DIFFERS; differ=$((differ + 1)); fi
        echo "  $o $f: lower=$d layer=$y $r"
    done
done
for dd in t/dir t/tr t; do   # t itself has root's label, the floor: list yes, create no
    for o in list create; do
        n=$((n + 1))
        d=$(op Sub $o $L/$dd d$n); y=$(op Sub $o $Y/$dd y$n)
        if [ "$d" = 0 ]; then allowed=$((allowed + 1)); else denied=$((denied + 1)); fi
        if same "$d" "$y"; then r=same; else r=DIFFERS; differ=$((differ + 1)); fi
        echo "  $o $dd: lower=$d layer=$y $r"
    done
done
# a label the subject has no rule for, directly: the control that the rules are in force
ctl=$(op Sub read $L/t/hid ctl)
[ "$ctl" != 0 ] || fail "control: the subject read a file labelled Hid directly; the rules are not in force"
[ $denied -gt 0 ] && [ $allowed -gt 0 ] || fail "decisions: allowed=$allowed denied=$denied directly; the step needs both"
[ $differ = 0 ] && [ $denied -gt 0 ] && [ $allowed -gt 0 ] && echo "SMACK_PARITY_PASS: $n operations, $allowed allowed and $denied denied directly; every one through the layer agrees with the lower" ||
    fail "decisions: $differ of $n operations differ between the lower and the layer"

# ---- 3. labels of new files --------------------------------------------------------------------
bad=0
for t in d y; do
    for dd in dir tr; do
        f=''; for p in "$L/t/$dd/new-$t"*; do [ -e "$p" ] && f=${p##*/} && break; done
        v=$(get "$L/t/$dd/$f")
        want=Sub; [ $dd = tr ] && want=Tr
        echo "  new file in $dd by the subject ($([ $t = d ] && echo directly || echo 'through the layer')): $f label=$v (expected $want)"
        [ -n "$f" ] && [ "$v" = "$want" ] || bad=$((bad + 1))
    done
done
[ $bad = 0 ] && echo "SMACK_CREATE_PASS: files created through the layer get the labels Smack gives directly (the creator's; the directory's when it transmutes)" ||
    fail "create: $bad new files with an unexpected label"

# ---- 4. QUERY2 ---------------------------------------------------------------------------------
# QUERY2 is an _IOWR ioctl, and smack_file_ioctl() asks for write access to the label of the
# file the ioctl is issued on, read access too, and the layer takes QUERY2 only on a domain (a
# top-level directory). So the subject queries the domain q, labelled Dir (rwxat), which holds one
# file it may read (Rd) and one it may not (Hid); on the domain t, root's label, Smack refuses the
# ioctl before the layer runs (rounds 2 and 3 of journal 0294).
mkdir -m 777 $L/q && lab $L/q Dir || die "query dir"
for f in qrd qhid; do echo "$f" > $L/q/$f && chmod 666 $L/q/$f || die "file $f"; done
lab $L/q/qrd Rd && lab $L/q/qhid Hid || die "query labels"
for f in qrd qhid; do $MA set $Y/q/$f kind string secret > /dev/null || die "attr $f"; done
# shellcheck disable=SC2016 # $0 is the inner shell's
qsub() { $BB sh -c 'echo Sub > '"$ATTR"' && exec '"$SP"' --reuid=1000 --regid=1000 --clear-groups --inh-caps=-all --bounding-set=-all -- '"$Q"' "$0" q2 kind eq string secret' "$1" 2>&1 | tr '\n' ' '; }
qr=$($Q $Y/q q2 kind eq string secret 2>&1 | tr '\n' ' ')
qs=$(qsub $Y/q)
qt=$(qsub $Y/t)
echo "  query2 on q as root: $qr"
echo "  query2 on q as Sub: $qs"
echo "  query2 on t (root's label) as Sub: $qt"
has() { echo " $1 " | grep -q "[[:space:]/]$2[[:space:]]"; }
if has "$qr" qhid && has "$qr" qrd && has "$qs" qrd && ! has "$qs" qhid; then
    echo "SMACK_QUERY_PASS: QUERY2 names, for the subject, the file it may read and not the one labelled Hid"
else
    fail "query2: the results for the subject do not follow its rules (root: $qr; Sub: $qs)"
fi
case $qt in
*"Permission denied"*) echo "SMACK_QUERY_IOCTL_PASS: on a directory the subject may not write, Smack refuses QUERY2 (an _IOWR ioctl)" ;;
*) fail "query2 on t: expected Smack's refusal of the ioctl, got: $qt" ;;
esac

# ---- 5. relabelling ----------------------------------------------------------------------------
echo relabel > $L/t/relab && chmod 666 $L/t/relab && lab $L/t/relab Hid || die relab
b1=$(op Sub read $Y/t/relab r1)
setfattr -n security.SMACK64 -v Rd $Y/t/relab; s1=$?
b2=$(op Sub read $Y/t/relab r2); lw=$(get $L/t/relab)
us=$(c Sub setfattr -n security.SMACK64 -v Sub $Y/t/rw); uv=$(get $L/t/rw)
echo "  relabel by root through the layer: set=$s1 lower label=$lw; subject read before=$b1 after=$b2"
echo "  relabel by the subject through the layer: set=$us lower label=$uv"
if [ "$b1" != 0 ] && [ $s1 = 0 ] && [ "$lw" = Rd ] && [ "$b2" = 0 ]; then
    echo "SMACK_RELABEL_PASS: a label set through the layer reaches the lower and takes effect at once"
else
    fail "relabel by root (before=$b1 set=$s1 lower=$lw after=$b2)"
fi
if [ "$us" != 0 ] && [ "$uv" = Rw ]; then
    echo "SMACK_RELABEL_DENIED_PASS: an unprivileged process cannot relabel through the layer"
else
    fail "relabel by the subject (set=$us lower=$uv)"
fi

umount $Y && umount $L
dmesg | grep -c 'lsm=SMACK' | sed 's/^/  Smack audit records: /'
echo "MOOBFS_SMACK_RESULT: fails=$fails"

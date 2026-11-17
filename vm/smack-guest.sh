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
#   5. relabelling through the layer, by root and by an unprivileged process;
#   6. shared mappings, read and write, and SMACK64MMAP (journal 0298);
#   7. exec: the layer is SB_I_NOEXEC, so exec through it is refused whatever Smack allows, and
#      SMACK64EXEC, read through the layer as on the lower, never applies (journal 0298);
#   8. mounts by a labelled mounter: refused without CAP_SYS_ADMIN, and the rules of a labelled
#      mounter that has it bound what its layer gives (journal 0298).
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
# Journal 0298: Ex a program the subject may run, Only a file the subject may read and the
# labelled mounter Mnt may not; MmOk, a SMACK64MMAP label with at least every access of the
# subject, and MmNo, one with none; Mnt the rules of the labelled mounter of step 8. A mounter
# without CAP_MAC_ADMIN gives its own label to the root of the filesystem it mounts
# (smack_set_mnt_opts(), smack_d_instantiate()), hence "Sub Mnt rx".
for r in "Sub Rd r" "Sub Rw rwa" "Sub Hid -" "Sub Dir rwxat" "Sub Tr rwxat" "Sub Ex rx" "Sub Only r" \
    "MmOk Rd rwxat" "MmOk Rw rwxat" "MmOk Dir rwxat" "MmOk Tr rwxat" "MmOk Ex rwxat" "MmOk Only rwxat" "MmOk Mnt rwxat" \
    "Mnt Rd r" "Mnt Rw rwa" "Mnt Dir rx" "Sub Mnt rx"; do
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
# Journal 0298, made before the layer is mounted so that the lower does not change behind it:
# md and my, files labelled as rd, rw and hid for the mapped writes, md reached only directly
# and my only through the layer; mok and mno, labelled Rd with SMACK64MMAP MmOk and MmNo; only,
# labelled Only; three copies of busybox named cat: x labelled Ex with SMACK64EXEC Run, xs
# labelled Ex without it, xn labelled Rd (no execute rule).
for d in md my; do
    mkdir -m 777 $L/$d && for f in rd rw hid; do echo "$f" > $L/$d/$f && chmod 666 $L/$d/$f; done &&
        lab $L/$d/rd Rd && lab $L/$d/rw Rw && lab $L/$d/hid Hid || die "set $d"
done
for f in mok mno only; do echo "$f" > $L/t/$f && chmod 666 $L/t/$f || die "file $f"; done
lab $L/t/mok Rd && lab $L/t/mno Rd && lab $L/t/only Only &&
    setfattr -n security.SMACK64MMAP -v MmOk $L/t/mok &&
    setfattr -n security.SMACK64MMAP -v MmNo $L/t/mno || die "mmap labels"
for d in x xs xn; do mkdir $L/t/$d && cp $BB $L/t/$d/cat && chmod 755 $L/t/$d/cat || die "program $d"; done
lab $L/t/x/cat Ex && lab $L/t/xs/cat Ex && lab $L/t/xn/cat Rd &&
    setfattr -n security.SMACK64EXEC -v Run $L/t/x/cat || die "program labels"
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

# ---- 6. mappings -------------------------------------------------------------------------------
# a mapped read changes nothing and runs on t through both views; a mapped write runs once per
# file, directly on md and through the layer on my
MM=$B/mmapfile
differ=0; denied=0; allowed=0; n=0
for f in rd rw hid; do
    for o in r w; do
        n=$((n + 1))
        if [ $o = r ]; then d=$(c Sub "$MM" r $L/t/$f); y=$(c Sub "$MM" r $Y/t/$f)
        else d=$(c Sub "$MM" w $L/md/$f); y=$(c Sub "$MM" w $Y/my/$f); fi
        if [ "$d" = 0 ]; then allowed=$((allowed + 1)); else denied=$((denied + 1)); fi
        if same "$d" "$y"; then r=same; else r=DIFFERS; differ=$((differ + 1)); fi
        echo "  mmap $o $f: lower=$d layer=$y $r"
    done
done
wb=$(head -c 1 $L/my/rw)
echo "  first byte of the lower of my/rw after the mapped write through the layer: $wb"
[ $differ = 0 ] && [ $denied -gt 0 ] && [ $allowed -gt 0 ] && [ "$wb" = M ] &&
    echo "SMACK_MMAP_PASS: $n shared mappings, $allowed allowed and $denied denied directly; every one through the layer agrees with the lower, and the mapped write reached it" ||
    fail "mmap: $differ of $n differ (allowed=$allowed denied=$denied directly; byte=$wb)"
# SMACK64MMAP: a mapping of mno, whose MmNo has none of the subject's access, is refused; of mok
# it is not; reading mno with read(2) is allowed, so the refusal is SMACK64MMAP's
ok_d=$(c Sub "$MM" r $L/t/mok); ok_y=$(c Sub "$MM" r $Y/t/mok)
no_d=$(c Sub "$MM" r $L/t/mno); no_y=$(c Sub "$MM" r $Y/t/mno); no_r=$(c Sub $BB cat $Y/t/mno)
echo "  SMACK64MMAP: mok lower=$ok_d layer=$ok_y; mno lower=$no_d layer=$no_y, read(2) through the layer=$no_r"
echo "  SMACK64MMAP labels: mok lower=$(getfattr --absolute-names --only-values -n security.SMACK64MMAP $L/t/mok 2> /dev/null) layer=$(getfattr --absolute-names --only-values -n security.SMACK64MMAP $Y/t/mok 2> /dev/null)"
[ "$ok_d" = 0 ] && [ "$ok_y" = 0 ] && [ "$no_d" != 0 ] && [ "$no_y" != 0 ] && [ "$no_r" = 0 ] &&
    echo "SMACK_MMAPLABEL_PASS: SMACK64MMAP refuses through the layer the mapping it refuses directly, and allows the one it allows" ||
    fail "SMACK64MMAP (mok lower=$ok_d layer=$ok_y; mno lower=$no_d layer=$no_y read=$no_r)"

# ---- 7. exec -----------------------------------------------------------------------------------
# Directly, Smack allows x (and SMACK64EXEC sets the program's label to Run) and xs, and refuses
# xn. The layer marks its superblock SB_I_NOEXEC (docs/when-not-to-use.md), so exec through it is
# refused for all three: Smack's decision and SMACK64EXEC never come into play there. The
# attribute itself is read through the layer as on the lower.
# The program reads its own label; prints "<exit> <label>". setpriv still holds root's
# capabilities when it calls execve (CAP_MAC_OVERRIDE passes Smack's check of the program), so
# it starts busybox, which runs without them and execs the program (journal 0298, round 2).
# shellcheck disable=SC2016 # $0 is the inner shells'
xr() { o=$($BB sh -c 'echo Sub > '"$ATTR"' || exit 97
        exec '"$SP"' --reuid=1000 --regid=1000 --clear-groups --inh-caps=-all --bounding-set=-all -- '"$BB"' sh -c "exec \"\$0\" '"$ATTR"'" "$0"' "$1" 2> /dev/null < /dev/null); echo "$? ${o:-none}"; }
bad=0
for d in x xs xn; do
    a=$(xr $L/t/$d/cat); b=$(xr $Y/t/$d/cat)
    case $d in x) want="0 Run" ;; xs) want="0 Sub" ;; xn) want=refused ;; esac
    echo "  exec $d/cat (label $(get $L/t/$d/cat)): lower=($a) expected=($want); layer=($b) expected=(refused)"
    if [ "$want" = refused ]; then [ "${a%% *}" != 0 ] || bad=$((bad + 1))
    else [ "$a" = "$want" ] || bad=$((bad + 1)); fi
    [ "${b%% *}" != 0 ] && [ "${b#* }" = none ] || bad=$((bad + 1))
done
xa=$(getfattr --absolute-names --only-values -n security.SMACK64EXEC $L/t/x/cat 2> /dev/null)
xb=$(getfattr --absolute-names --only-values -n security.SMACK64EXEC $Y/t/x/cat 2> /dev/null)
echo "  SMACK64EXEC of x/cat: lower=$xa layer=$xb"
[ "$xa" = Run ] && [ "$xb" = Run ] || bad=$((bad + 1))
[ $bad = 0 ] && echo "SMACK_EXEC_PASS: directly Smack allows or refuses exec and SMACK64EXEC sets the program's label; through the layer (SB_I_NOEXEC) exec is always refused, and SMACK64EXEC reads as on the lower" ||
    fail "exec: $bad checks differ from the expected"

# ---- 8. mounters -------------------------------------------------------------------------------
# moobfs_layer is not FS_USERNS_MOUNT: mounting it needs CAP_SYS_ADMIN in the initial user
# namespace, so an unprivileged mounter, labelled or not, is refused, also as root of its own
# user namespace. Control: in that namespace the subject may mount a tmpfs (FS_USERNS_MOUNT).
mkdir $S/um $S/lm || die "mountpoints"
mu=$(c Sub $BB mount -t moobfs_layer -o lower=$L none $S/um)
mn=$(c Sub /usr/bin/unshare -Urm $BB mount -t moobfs_layer -o lower=$L none $S/um)
mt=$(c Sub /usr/bin/unshare -Urm $BB mount -t tmpfs none $S/um)
echo "  unprivileged mount by the subject: plain=$mu in its user namespace=$mn; control, tmpfs in that namespace=$mt"
[ "$mu" != 0 ] && [ "$mn" != 0 ] && [ "$mt" = 0 ] && ! mountpoint -q $S/um &&
    echo "SMACK_UNPRIV_MOUNT_PASS: an unprivileged labelled process cannot mount the layer, in its own user namespace either" ||
    fail "unprivileged mount (plain=$mu userns=$mn tmpfs control=$mt)"
# A labelled mounter: uid 0 with CAP_SYS_ADMIN only (no CAP_MAC_OVERRIDE), label Mnt, which may
# read Rd, Rw and Dir and nothing labelled Hid or Only. The layer opens lower files as the
# mounter (ADR-0011, ADR-0037), so its rules bound what the subject gets: only, which the
# subject may read directly, is refused through this mount.
# shellcheck disable=SC2016 # $0 and $@ are the inner shell's
mm=$($BB sh -c 'echo Mnt > '"$ATTR"' || exit 97
    exec '"$SP"' --inh-caps=-all --bounding-set=-all,+sys_admin -- '"$BB"' mount -t moobfs_layer -o lower="$0" none "$1"' \
    $L $S/lm < /dev/null > /dev/null 2>&1; echo $?)
echo "  mount by the labelled mounter (Mnt, CAP_SYS_ADMIN only): $mm"
if [ "$mm" = 0 ]; then
    M=$S/lm; bad=0
    # the last field: 0 allowed, x refused
    for t in "read rd 0" "stat rd 0" "read rw 0" "read hid x" "read only x" "list dir 0"; do
        # shellcheck disable=SC2086 # split on purpose
        set -- $t
        y=$(op Sub "$1" "$M/t/$2" m); d=$(op Sub "$1" "$L/t/$2" m)
        case $3 in
        0) [ "$y" = 0 ] || bad=$((bad + 1)); e=allowed ;;
        x) [ "$y" != 0 ] || bad=$((bad + 1)); e=refused ;;
        esac
        echo "  through Mnt's layer, $1 $2: lower=$d layer=$y expected=$e"
    done
    # labels through the mounter's layer, for the files the mounter may read
    for f in rd rw dir; do
        a=$(get $L/t/$f); b=$(get $M/t/$f)
        echo "  label $f through Mnt's layer: lower=$a layer=$b"
        [ "$b" = "$a" ] || bad=$((bad + 1))
    done
    od=$(op Sub read $L/t/only m)
    [ "$od" = 0 ] || fail "control: the subject could not read only directly"
    [ $bad = 0 ] && echo "SMACK_MOUNTER_PASS: through a labelled mounter's layer the subject gets what both its rules and the mounter's allow" ||
        fail "labelled mounter: $bad checks not as expected"
    umount $M || fail "umount of the labelled mounter's layer"
else
    fail "the labelled mounter with CAP_SYS_ADMIN could not mount the layer ($mm)"
fi

umount $Y && umount $L
dmesg | grep -c 'lsm=SMACK' | sed 's/^/  Smack audit records: /'
echo "MOOBFS_SMACK_RESULT: fails=$fails"

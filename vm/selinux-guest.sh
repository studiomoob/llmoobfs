#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Guest side of vm/selinux-local.sh (journal 0104, security roadmap S12). Loads the given
# policy, turns enforcing on, labels a lower ext4 tree with the types of vm/selinux/moobtest.te
# and compares, for a confined client, each operation on the lower file directly with the same
# operation through the layer. Arguments: repository copy, policy file, label of the run.
set -u
PATH=$PATH:/usr/sbin:/sbin; export PATH
R=$1; POL=$2; TAG=$3
fail() { echo "MOOBFS_SELINUX_FAIL: $TAG: $1"; dmesg | tail -8 | sed "s/^/  dmesg: /"; exit 1; }
# modules first: under the policy, loading a module from the unlabeled 9p root is denied
modprobe ext4 || fail "modprobe ext4"
insmod "$R/layer/moobfs_layer.ko" || fail insmod
mountpoint -q /sys/fs/selinux || mount -t selinuxfs none /sys/fs/selinux || fail selinuxfs
# selinuxfs wants the whole policy in one write
dd if="$POL" of=/sys/fs/selinux/load bs=64M iflag=fullblock 2> /dev/null || fail "policy load"
echo 1 > /sys/fs/selinux/enforce && [ "$(cat /sys/fs/selinux/enforce)" = 1 ] || fail enforcing
S=/run/se
mkdir -p $S && mount -t tmpfs none $S && mkdir $S/bin $S/lower $S/layer || fail tmpfs
cp /bin/busybox $S/bin/ && chcon -t bin_t $S/bin/busybox || fail busybox
BB=$S/bin/busybox
mount -t ext4 /dev/vda $S/lower || fail "lower mount"
L=$S/lower
mkdir $L/t && for f in rw ro none noxattr; do echo "$f" > $L/t/$f; done || fail files
chcon -t moobtest_dir_t $L $L/t && chcon -t moobtest_rw_t $L/t/rw &&
    chcon -t moobtest_ro_t $L/t/ro && chcon -t moobtest_none_t $L/t/none &&
    chcon -t moobtest_noxattr_t $L/t/noxattr || fail chcon
# Two more sets with the labels of t, and nomap (open, read and write, not map), for the mappings
# and the operations that change a file (journal 0298): md is reached only directly and my only
# through the layer, so that each change is made once and the lower never changes behind the layer.
for s in md my; do
    mkdir $L/$s && for f in rw ro none nomap; do echo "$f" > $L/$s/$f; done &&
        chcon -t moobtest_dir_t $L/$s && chcon -t moobtest_rw_t $L/$s/rw &&
        chcon -t moobtest_ro_t $L/$s/ro && chcon -t moobtest_none_t $L/$s/none &&
        chcon -t moobtest_nomap_t $L/$s/nomap || fail "set $s"
done
mount -t moobfs_layer -o lower=$L none $S/layer || fail "layer mount"
# the client must be the first to reach each file through the layer: the layer reads the label
# (security.selinux) when the inode is first set up, with the credentials of whoever asked
# exit status of one busybox applet run in the client domain
c() { runcon -t moobtest_client_t -- $BB "$@" < /dev/null > /dev/null 2>&1; echo $?; }
op() {  # op-name file-path
    case $1 in
    read) c cat "$2" ;;
    stat) c stat "$2" ;;
    append) c sh -c "echo x >> $2" ;;
    list) c ls "$(dirname "$2")" ;;
    esac
}
differ=0; denied=0
for f in rw ro none; do
    for o in read stat append list; do
        d=$(op $o $L/t/$f); l=$(op $o $S/layer/t/$f)
        [ "$d" = 0 ] || denied=$((denied + 1))
        if [ "$d" = 0 ] && [ "$l" = 0 ]; then r=same; elif [ "$d" != 0 ] && [ "$l" != 0 ]; then r=same; else r=DIFFERS; differ=$((differ + 1)); fi
        echo "  $TAG $o $f: direct=$d layer=$l $r"
    done
done
# what the layer's inodes were labelled with after the client went first; the lower files are
# the reference
echo "labels through the layer after the client went first ($TAG):"
for f in rw ro none; do
    echo "  $f: lower=$(stat -c %C $L/t/$f) layer=$(stat -c %C $S/layer/t/$f)"
done
# the client must also create through the layer what it may create directly
d=$(c touch $L/t/new-direct); l=$(c touch $S/layer/t/new-layer)
echo "  $TAG create: direct=$d layer=$l"
stat -c '%C %n' $L/t/new-direct $L/t/new-layer 2>&1 | sed 's/^/  /'
# Mapping, attribute writes, rename and hard link (journal 0298), compared the same way: each
# operation directly on md/<f> and through the layer on my/<f>. mmapfile and moobfs-attr are
# static and run from the labelled tmpfs, like query below.
cp "$(dirname "$POL")/mmapfile" $S/bin/mmapfile && cp "$(dirname "$POL")/moobfs-attr" $S/bin/moobfs-attr &&
    chcon -t bin_t $S/bin/mmapfile $S/bin/moobfs-attr || fail "helper copy"
x() { runcon -t moobtest_client_t -- "$@" < /dev/null > /dev/null 2>&1; echo $?; }
mop() {  # mop op-name file-path
    case $1 in
    mmapr) x $S/bin/mmapfile r "$2" ;;
    mmapw) x $S/bin/mmapfile w "$2" ;;
    setx) x $S/bin/moobfs-attr set "$2" kind string v ;;
    link) c sh -c "ln $2 $2.l && rm $2.l" ;;
    rename) c mv "$2" "$2.r" ;;
    esac
}
mdiffer=0; mdenied=0; mallowed=0; mn=0
for f in rw ro none nomap; do
    for o in mmapr mmapw setx link rename; do
        mn=$((mn + 1))
        d=$(mop $o $L/md/$f); l=$(mop $o $S/layer/my/$f)
        if [ "$d" = 0 ]; then mallowed=$((mallowed + 1)); else mdenied=$((mdenied + 1)); fi
        if [ "$d" = 0 ] && [ "$l" = 0 ]; then r=same; elif [ "$d" != 0 ] && [ "$l" != 0 ]; then r=same; else r=DIFFERS; mdiffer=$((mdiffer + 1)); fi
        echo "  $TAG $o $f: direct=$d layer=$l $r"
    done
done
# what the operations allowed through the layer left on the lower file my/rw: mapped byte,
# attribute, the new name, and no link left behind
ef=$(head -c 1 $L/my/rw.r 2> /dev/null)
ea=$("$(dirname "$POL")/moobfs-attr" get $L/my/rw.r kind 2> /dev/null)
el=$(cd $L/my && echo *)
echo "  $TAG effects on the lower of my/rw: first byte=$ef attribute kind=$ea entries: $el"
if [ "$TAG" = fsuse ]; then
    [ $mallowed -gt 0 ] && [ $mdenied -gt 0 ] ||
        fail "mapping and changes: allowed=$mallowed denied=$mdenied directly; the step needs both"
    [ $mdiffer = 0 ] || fail "mapping and changes: $mdiffer of $mn operations differ between the lower file and the layer"
    echo "SELINUX_MAPCHANGE_PASS: $mn operations (mmap read and write, attribute write, hard link, rename), $mallowed allowed and $mdenied denied directly; every one through the layer agrees with the lower"
    [ "$ef" = M ] && [ -n "$ea" ] && [ ! -e $L/my/rw ] && [ ! -e $L/my/rw.l ] ||
        fail "effects: the changes allowed through the layer did not all reach the lower (byte=$ef attribute=$ea)"
    echo "SELINUX_EFFECT_PASS: the mapped write, the attribute, the link and the rename made through the layer reached the lower"
fi
dmesg | grep -c 'avc:  denied' | sed "s/^/  $TAG avc denials: /"
dmesg | grep -i "inode_doinit\|getxattr returned" | sed "s/^/  $TAG dmesg: /"
# A confined mounter (journal 0286): a second mount of the same lower, made in the permissive
# domain moobtest_mounter_t, read by moobtest_reader_t. Every check the mounter would fail is
# logged; none may be "fd use" on the reader, which the mounter would need if the layer opened
# lower files as the caller. Control: the mounter's own "open" of the lower directory is logged,
# so the log is complete. The audit printk limit is lifted for this step.
if [ "$TAG" = fsuse ]; then
    cp "$(dirname "$POL")/listdir" $S/bin/listdir && chcon -t bin_t $S/bin/listdir || fail "listdir copy"
    mkdir $S/layerm || fail "mounter mountpoint"
    runcon -t moobtest_mounter_t -- $BB mount -t moobfs_layer -o lower=$L none $S/layerm ||
        fail "mount in moobtest_mounter_t"
    echo 0 > /proc/sys/kernel/printk_ratelimit; echo 10000 > /proc/sys/kernel/printk_ratelimit_burst
    dmesg -c > /dev/null
    rl=$(runcon -t moobtest_reader_t -- $S/bin/listdir $S/layerm/t < /dev/null > /dev/null 2>&1; echo $?)
    rr=$(runcon -t moobtest_reader_t -- $BB cat $S/layerm/t/ro < /dev/null > /dev/null 2>&1; echo $?)
    dmesg > $S/mounter-dmesg.txt
    umount $S/layerm || fail "mounter umount"
    fu=$(grep -c 'scontext=system_u:system_r:moobtest_mounter_t:s0 tcontext=system_u:system_r:moobtest_reader_t:s0 tclass=fd' $S/mounter-dmesg.txt)
    op=$(grep -c 'denied  { open } .*scontext=system_u:system_r:moobtest_mounter_t:s0 tcontext=system_u:object_r:moobtest_dir_t:s0 tclass=dir' $S/mounter-dmesg.txt)
    # Finding 0114 item 8 (journal 0286): the lower directory file now carries the mounter's
    # domain, and the reader has no "fd use" on it. A directory read checked with the reader's
    # credentials (moobl_iterate() before journal 0120) is refused that use; read as the mounter
    # it is not. Control: the reader is enforcing, so such a refusal fails the listing.
    ru=$(grep -c 'scontext=system_u:system_r:moobtest_reader_t:s0 tcontext=system_u:system_r:moobtest_mounter_t:s0 tclass=fd' $S/mounter-dmesg.txt)
    rp=$(grep -c 'scontext=system_u:system_r:moobtest_reader_t:s0 .*permissive=1' $S/mounter-dmesg.txt)
    echo "  $TAG confined mounter: list=$rl read=$rr fd_use_on_reader=$fu mounter_open_logged=$op reader_fd_use_on_mounter=$ru reader_permissive=$rp"
    [ "$op" -gt 0 ] || fail "confined mounter control: the mounter's open of the lower directory was not logged; the step proves nothing"
    [ "$rp" = 0 ] || fail "item 8 control: the reader domain is permissive; the step proves nothing"
    [ "$fu" = 0 ] || { grep 'tclass=fd' $S/mounter-dmesg.txt | sed 's/^/  /' | head -4; fail "confined mounter: the mounter needed fd use on the reader ($fu)"; }
    [ "$ru" = 0 ] && [ "$rl" = 0 ] || { grep 'tclass=fd' $S/mounter-dmesg.txt | sed 's/^/  /' | head -4; fail "item 8: a directory read through the layer was checked with the caller's credentials (list=$rl, fd use refusals=$ru)"; }
    [ "$rr" = 0 ] || fail "confined mounter: the reader could not read through the layer (read=$rr)"
    echo "SELINUX_MOUNTER_PASS: a layer mounted by a confined domain needs no fd use on its readers"
    echo "SELINUX_DIRREAD_PASS: directory reads through the layer run as the mounter (0114 item 8)"
fi
# QUERY2 under the policy: the three files carry the same attribute; the client may getattr
# rw and ro, not none
B=$(dirname "$POL")
cp "$B/query" $S/bin/query && chcon -t bin_t $S/bin/query || fail "query copy"
for f in rw ro none noxattr; do
    "$B/moobfs-attr" set $S/layer/t/$f kind string secret > /dev/null || fail "attr $f"
done
cp "$B/moobfs-attr" $S/bin/moobfs-attr && chcon -t bin_t $S/bin/moobfs-attr || fail "moobfs-attr copy"
# C1 controls: the client reads noxattr's contents and is refused its attribute, directly and
# through the layer; it may read ro's attribute
ca() { runcon -t moobtest_client_t -- "$@" < /dev/null > /dev/null 2>&1; echo $?; }
nx_read_d=$(c cat $L/t/noxattr); nx_read_l=$(c cat $S/layer/t/noxattr)
nx_attr_d=$(ca $S/bin/moobfs-attr get $L/t/noxattr kind); nx_attr_l=$(ca $S/bin/moobfs-attr get $S/layer/t/noxattr kind)
ro_attr_l=$(ca $S/bin/moobfs-attr get $S/layer/t/ro kind)
echo "  $TAG noxattr: read direct=$nx_read_d layer=$nx_read_l; attribute direct=$nx_attr_d layer=$nx_attr_l; ro attribute layer=$ro_attr_l"
echo "  $TAG query2 as root: $($S/bin/query $S/layer/t q2 kind eq string secret 2>&1 | tr '\n' ' ')"
q=$(runcon -t moobtest_client_t -- $S/bin/query $S/layer/t q2 kind eq string secret 2>&1; echo "exit=$?")
echo "$q" | sed "s/^/  $TAG query2: /"
if [ "$TAG" = fsuse ]; then
    # finding F6: the answer holds what the client may read, and nothing the policy hides
    echo "$q" | grep -q '[[:space:]]none$' && fail "QUERY2 names a file the client may not even getattr (F6)"
    [ "$nx_read_d" = 0 ] && [ "$nx_read_l" = 0 ] && [ "$nx_attr_d" != 0 ] && [ "$nx_attr_l" != 0 ] &&
        [ "$ro_attr_l" = 0 ] || fail "C1 controls: noxattr must be readable and its attribute refused, ro's attribute allowed"
    echo "$q" | grep -q '[[:space:]]noxattr$' &&
        fail "QUERY2 names a file whose attributes the client may not read (stage-3 review #2 C1)"
    echo "$q" | grep -q '[[:space:]]rw$' && echo "$q" | grep -q '[[:space:]]ro$' ||
        fail "QUERY2 left out a file the client may read"
fi
[ $denied -gt 0 ] || fail "nothing was denied directly: the test proved nothing"
# the label is read when the inode is set up, whoever asked first; a refused read leaves the
# inode unlabelled and is logged at every try
if [ "$TAG" = fsuse ] && dmesg | grep -q "getxattr returned .* dev=moobfs_layer"; then
    fail "the layer's label could not be read for a confined first caller"
fi
[ $differ -eq 0 ] || [ "$TAG" = stock ] || fail "$differ operations differ between the lower file and the layer"
echo "MOOBFS_SELINUX_RESULT: $TAG differ=$differ denied_directly=$denied"

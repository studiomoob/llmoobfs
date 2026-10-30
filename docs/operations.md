# Installing and operating MoobFS on a Linux host

Status: roadmap item C2 ([ADR-0022](decisions/0022-kernel-freeze-and-user-space-consumable-track.md)).
What was actually run is in [journal 0197](history/0197-first-contact-and-install.md); the
section [What was tested](#what-was-tested) at the end summarizes it.

> **Experimental. Not for production.** MoobFS is kernel code that no independent human has reviewed as a whole yet.
> A defect is a kernel crash for the whole machine. Install it only on a **dedicated test host
> or VM** whose data you can lose. For anything else, SQLite or plain files remain the
> recommendation ([when-not-to-use.md](when-not-to-use.md)).

This page installs the module on the host's own kernel, mounts it by hand and at boot, gives
users their directories, and removes it again. To try MoobFS without installing anything, use
[Try it in a VM](../README.md#try-it-in-a-vm) instead.

## 1. Requirements

| Requirement | Why |
| --- | --- |
| x86-64, Debian 13, kernel **`6.12.113+deb13-amd64`** or **`6.12.111+deb13-amd64`** exactly | The distribution kernels the suite and fstests run on ([compatibility.md](compatibility.md)). `make install` refuses any other kernel. |
| Root (`sudo`) | Loading a module and mounting need `CAP_SYS_ADMIN` in the initial user namespace. |
| A directory on ext4, btrfs or XFS, on a local disk | The lower tree that holds the files. Network and FUSE filesystems are unsupported. |
| Secure Boot off, or your own MOK to sign the module | `make install` does not sign the module; with Secure Boot on, the kernel refuses to load it. The packages below sign it with a DKMS key you enrol once. |

**Check the kernel first.** A Debian 13 system installed from the 2026-10-21 cloud image runs
`6.12.113+deb13-amd64`, which is supported (journal 0206); nothing to change:

```sh
uname -r                                   # 6.12.113+deb13-amd64 or 6.12.111+deb13-amd64
```

A system that has moved to a later 6.12 kernel through updates is outside the list. Until the
list grows (docs/compatibility.md, rule 3), such a host has to boot a listed kernel, for
instance:

```sh
sudo apt install linux-image-6.12.113+deb13-amd64=6.12.113-1 \
                 linux-headers-6.12.113+deb13-amd64=6.12.113-1
# make it the default boot entry, then reboot into it:
sudo sed -i 's/^GRUB_DEFAULT=.*/GRUB_DEFAULT="Advanced options for Debian GNU\/Linux>Debian GNU\/Linux, with Linux 6.12.113+deb13-amd64"/' /etc/default/grub
sudo update-grub && sudo reboot
```

Staying on an older kernel forgoes the security fixes of later ones. That is one more reason to
use a dedicated host, never a machine that holds anything else.

`MOOB_ALLOW_UNSUPPORTED=1` makes `make install` build for another kernel anyway. It prints a
warning and nothing about the result is tested or supported: the module uses in-kernel
interfaces that change without notice, even between 6.12 builds.

## 2. Build and install

```sh
sudo apt install build-essential git attr acl linux-headers-$(uname -r)
git clone https://github.com/studiomoob/llmoobfs.git
cd llmoobfs
sudo make install
```

`make install` checks the kernel, builds `moobfs_layer.ko` against `/lib/modules/$(uname -r)/build`,
and copies:

| File | Destination |
| --- | --- |
| `moobfs_layer.ko` | `/lib/modules/$(uname -r)/updates/moobfs/` (then `depmod`) |
| `moobfs`, `moobfs-attr`, `moobfs-check`, `moobfs-index` | `/usr/local/bin/` |
| Their manual pages | `/usr/local/share/man/man1/` |

It refuses to replace anything already at a destination, a symbolic link included; `FORCE=1`
replaces a file or link (see Upgrade), never a directory. `make uninstall` removes only regular
files.
`PREFIX` changes `/usr/local`; `DESTDIR` stages everything in another root without `depmod`.

### Or: the experimental packages (item C6)

`moobfs-dkms` and `moobfs-tools` ([ADR-0028](decisions/0028-dkms-and-debian-packages.md)) do
the same through DKMS. They are **not published**: build them yourself on Debian 13 and install
them on the test host. Use one path, not both: the package refuses to install while a copy from
`make install` is present (`sudo make uninstall` first).

```sh
sudo apt install build-essential debhelper dh-dkms git
git clone https://github.com/studiomoob/llmoobfs.git && cd llmoobfs
make deb                                    # build/deb/moobfs-dkms_*_all.deb, moobfs-tools_*_amd64.deb
sudo apt install linux-headers-$(uname -r) mokutil ./build/deb/moobfs-dkms_*_all.deb ./build/deb/moobfs-tools_*_amd64.deb
dkms status moobfs                          # moobfs/<version>, <kernel>, x86_64: installed
```

| What | Where |
| --- | --- |
| Module sources and `dkms.conf` | `/usr/src/moobfs-<version>/` |
| `moobfs_layer.ko`, built and signed by DKMS | `/lib/modules/$(uname -r)/updates/dkms/` |
| `moobfs`, `moobfs-attr`, `moobfs-check`, `moobfs-index` and their manual pages | `/usr/bin/`, `/usr/share/man/man1/` |

DKMS builds only for the releases of `scripts/supported-kernels`, the list `make install` reads.
On any other kernel it prints a warning that the module is excluded by `BUILD_EXCLUSIVE` and
builds nothing; the package still installs.

**Secure Boot.** DKMS signs the module with a key it creates once on this host,
`/var/lib/dkms/mok.key`; the key never leaves the host and the project has none. Until the
firmware trusts it, `modprobe` fails with `Key was rejected by service`. Enrol the certificate
once:

```sh
sudo mokutil --import /var/lib/dkms/mok.pub   # choose a one-time password
sudo reboot                                   # MokManager: Enroll MOK, Continue, the password
mokutil --test-key /var/lib/dkms/mok.pub      # /var/lib/dkms/mok.pub is already enrolled
```

Every later DKMS build, upgrades included, is signed with the same key. Upgrade and roll back as
in section 6, with `sudo apt install ./moobfs-dkms_<version>_all.deb
./moobfs-tools_<version>_amd64.deb` (and `--allow-downgrades` to go back) in place of
`make install FORCE=1`; remove with `sudo apt purge moobfs-dkms moobfs-tools` in place of
`make uninstall`. The lower tree is untouched either way.

Load it and check that the kernel knows the filesystem type:

```sh
sudo modprobe moobfs_layer
grep moobfs_layer /proc/filesystems        # prints: nodev	moobfs_layer
```

## 3. Prepare the lower tree and mount

The lower tree must not be written behind the layer while it is mounted (as with overlayfs):
changes made there reach the index only at the next mount ([ADR-0011](decisions/0011-threat-and-credential-model.md),
non-promise 2). Keep it where ordinary users cannot reach it, inside a directory only root can
enter:

```sh
sudo install -d -m 0700 /srv/moobfs                 # only root can reach what is inside
sudo install -d -m 0755 /srv/moobfs/lower           # becomes the root of the mount
sudo install -d -m 0755 /srv/memory                 # the mount point users work in
sudo mount -t moobfs_layer -o lower=/srv/moobfs/lower,nolinks none /srv/memory
findmnt /srv/memory
```

The mount root carries the lower root's owner and mode. Keep it `0755`: a client that opens the
mount root needs to enter it.

| Option | Default | Meaning |
| --- | --- | --- |
| `lower=<dir>` | required | The lower directory. |
| `nolinks` | off | Refuse hard and symbolic links (`EPERM`). Recommended for memory clients. |
| `noindex` | off | Attributes are validated but not indexed; queries are unavailable. |
| `domain_depth=<0–8>` | 1 | Depth of the permission domains: 1 means each top-level directory is a domain. |
| `index_budget=<1M–4G>` | 128M | Kernel memory the index may use, for the whole mount. |
| `index_idle=<0–86400>` | 60 | Seconds before an unused domain's index may be released. |
| `index_build_work=<1024–65536>` | 32768 | Work an unprivileged query may spend building a domain's index ([ADR-0020](decisions/0020-configurable-query-build-work-per-mount.md)). |

No option can change on remount. The full semantics are in [spec-m2.md](spec-m2.md) and
[spec-query-v2.md](spec-query-v2.md).

## 4. Mount at boot

With `/etc/fstab`:

```text
none  /srv/memory  moobfs_layer  lower=/srv/moobfs/lower,nolinks,x-systemd.requires-mounts-for=/srv/moobfs/lower  0  0
```

`x-systemd.requires-mounts-for` orders the mount after the filesystem that holds the lower
tree. The kernel loads the module by itself on the first mount of the type, through the
`fs-moobfs_layer` alias that `depmod` registered. After editing:

```sh
sudo systemctl daemon-reload
sudo mount /srv/memory      # or reboot
```

Or, without `fstab`, a systemd mount unit named after the mount point,
`/etc/systemd/system/srv-memory.mount`:

```ini
[Unit]
Description=MoobFS layer over /srv/moobfs/lower
RequiresMountsFor=/srv/moobfs/lower

[Mount]
What=none
Where=/srv/memory
Type=moobfs_layer
Options=lower=/srv/moobfs/lower,nolinks

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl daemon-reload && sudo systemctl enable --now srv-memory.mount
```

## 5. Give users their directories

Each top-level directory is a permission domain. Create them through the mount, with ordinary
owners and modes; the kernel then decides who sees what ([client-contract.md](client-contract.md),
distribution strategy §2.2):

```sh
sudo install -d -o alice -g alice -m 0700 /srv/memory/alice
sudo -u alice sh -c 'echo "use the staging cluster" > /srv/memory/alice/note.md'
sudo -u alice moobfs-attr set /srv/memory/alice/note.md kind string decision
sudo -u alice moobfs-attr list /srv/memory/alice/note.md     # kind	string	decision
sudo -u bob cat /srv/memory/alice/note.md                    # Permission denied
```

Query a domain through the kernel's index with `moobfs` ([tools.md](tools.md#moobfs)):

```sh
sudo -u alice moobfs query /srv/memory/alice kind eq string decision   # note.md
sudo -u bob moobfs query /srv/memory/alice kind eq string decision     # permission denied (exit 77)
moobfs info /srv/memory                                                # the mount's settings
```

### Limits for a shared mount

On a host where several users query one mount, three bounds apply to each query, and the
deployment sets the third one (roadmap item G2, [journal 0243](history/0243-g2-diagnostics-and-deployment-recipe.md)):

| Bound | Set by | Refusal (exit 75 of `moobfs query`) |
| --- | --- | --- |
| The index of the whole mount: `index_budget` (default 128 MiB), shared fairly between the uids that hold built domains ([ADR-0027](decisions/0027-fair-shares-of-the-index-budget.md)) | mount option, section 3 | `ENOMEM`: the domain does not fit the budget, or this uid's share of it while other users hold theirs |
| The work and time of a first query of a domain: `index_build_work` (default 32,768) and a ten-second deadline; at most 16 first builds per uid and 64 per mount each minute ([ADR-0019](decisions/0019-bound-query-build-work-and-frequency.md), [ADR-0020](decisions/0020-configurable-query-build-work-per-mount.md)) | mount option; the deadline and the rates are fixed | `E2BIG`, `ETIMEDOUT`, `EDQUOT` |
| The memory of one user's processes: the index their queries build and the buffer each descriptor that queried keeps (about 2 MiB at the largest page, until it is closed) are charged to the caller's memory cgroup ([ADR-0018](decisions/0018-account-user-triggered-index-and-query-memory.md), [ADR-0029](decisions/0029-s9-limits-met-by-existing-bounds.md)) | `MemoryMax=` of the user's slice, below | `ENOMEM` at that limit, or the process killed by the cgroup's OOM killer |

A default Debian user slice has no `MemoryMax=`, so the third bound is then the machine's
memory: an unprivileged user can pin about 2 MiB of kernel memory per open descriptor that
queried, up to `RLIMIT_NOFILE` per process, and the global OOM killer does not count it to
that user's processes. Set it for every user slice with a systemd drop-in for the truncated
unit name `user-.slice` (`user@.service(5)`, "Controlling resources for logged-in users"), with
a value that fits the host:

```sh
sudo mkdir -p /etc/systemd/system/user-.slice.d
printf '[Slice]\nMemoryMax=2G\n' | sudo tee /etc/systemd/system/user-.slice.d/50-moobfs.conf
sudo systemctl daemon-reload
systemctl show -P MemoryMax user-$(id -u alice).slice      # 2147483648
```

Slices started afterwards get it. Whether `daemon-reload` alone changes the slice of a user
who is already logged in was not tested; `sudo systemctl stop user-<uid>.slice` ends that
user's sessions, and the next login starts the slice with the limit. A larger or smaller limit
for one user goes in `/etc/systemd/system/user-<uid>.slice.d/60-moobfs.conf`: drop-ins apply in
the lexicographic order of their names, whatever their directory (`systemd.unit(5)`), so it is
applied after the common one.

Sizing: the limit has to hold the user's own programs, plus the index of the domains the user
queries (at most the share of `index_budget`; `moobfs info` shows the budget), plus about 2 MiB
for each descriptor kept open after a query at the largest page. A client that keeps one
descriptor per domain needs 2 MiB per domain; one that closes it after each query needs none.

What the drop-in does not cover:

- **A client that runs as a system service** (an agent or an MCP server started by systemd) is in
  `system.slice`, not in a user slice: give its unit `MemoryMax=` (`systemctl edit
  <client>.service`, `[Service]` section).
- **`sudo -u` and `runuser`** run the command in the caller's cgroup, not in the target user's
  slice; only a login (`ssh`, the console, a `pam_systemd` session) or `systemd-run --uid=<user>
  --slice=user-<uid>.slice` puts it there. The commands of this page that use `sudo -u` are run
  in root's session and are not limited.

**Check it as the user**, with `moobfs diagnose` (read-only; [tools.md](tools.md#moobfs)):

```sh
moobfs diagnose /srv/memory/alice
# path:             /srv/memory/alice (a domain directory)
# mount:            /srv/memory
# index:            on, budget 128 MiB, build work 32768
# memory cgroup:    /user.slice/user-1001.slice/session-3.scope
# memory limit:     2048 MiB at /user.slice/user-1001.slice (12 MiB used)
# descriptors:      up to 1024 per process
# note:    the index counters and the tracepoints are shown to administrators only ...
```

Without a limit it prints `warning: no memory.max applies to this process ...` and exits 2. It
also reports how many times the limit was reached and whether a process was killed there, a
test-only module parameter left set, and, for root, the index's use of its budget and the
tracepoints. When a query is refused with `ENOMEM`, `moobfs query` reads the caller's memory
cgroups again and says which bound refused it: `the memory cgroup /user.slice/user-1001.slice
reached its memory.max (2048 MiB)`, or `the domain does not fit the mount's index budget ...;
the memory cgroup ... did not reach its limit`.

Over the limit a query fails with `ENOMEM` and other users keep their answers (suite step
`limits`, journal 0235); the messages are checked in suite step `diag`, and the drop-in and
`moobfs diagnose` under it in `make install-test` (journal 0243; both passed in a
dedicated VM on 2026-10-30).

A directory shared by a group is `root:<group>` with mode `2770` and a default ACL; the recipe is
in [distribution-strategy.md](distribution-strategy.md) §2.2. Containers: §2.5 of the same
document, until item C3 validates a recipe.

## 6. Check, upgrade, back up

- **Check** the attributes of a tree, mounted or not: `moobfs check /srv/memory` (or
  `moobfs-check`) prints a JSON
  report and exits 0 when clean, 2 when it found problems. It never repairs.
- **Upgrade** the module: stop the clients, then

  ```sh
  sudo umount /srv/memory && sudo rmmod moobfs_layer
  git pull && sudo make install FORCE=1
  sudo mount /srv/memory
  ```

  The module refuses to mount a tree whose `user.moob.format` it does not know, so an older
  module cannot misread a newer tree.
- **Back up** the lower tree with `rsync -aHAX --numeric-ids /srv/moobfs/lower/ <target>/`.
  `-X` is mandatory: without it the `user.moob.*` attributes are lost. Recovery is described in
  distribution strategy §2.7.

## 7. Uninstall

```sh
sudo umount /srv/memory
sudo sed -i '\|^none  */srv/memory  *moobfs_layer|d' /etc/fstab   # or disable srv-memory.mount
sudo rmmod moobfs_layer
sudo make uninstall
```

`make uninstall` refuses while the module is loaded. It removes only what `make install`
copied. The lower tree stays an ordinary directory, readable without the module:

```sh
sudo getfattr -d -m '^user\.moob\.' /srv/moobfs/lower/alice/note.md
```

## 8. When something fails

| Symptom | Cause and action |
| --- | --- |
| `kernel ... is not supported` from `make install` | The running kernel is not in `scripts/supported-kernels`; see section 1. |
| `no kernel headers at ...` | `sudo apt install linux-headers-$(uname -r)`. |
| `modprobe: ERROR: could not insert 'moobfs_layer': Key was rejected by service` | Secure Boot refuses a module not signed by a key it trusts. From `make install` the module is unsigned: turn Secure Boot off on the test host, sign it with your own MOK (`Documentation/admin-guide/module-signing.rst`), or use the packages of section 2. From the packages, the DKMS key is not enrolled yet: `mokutil --import /var/lib/dkms/mok.pub` and reboot (section 2). |
| `Warning: ... includes a BUILD_EXCLUSIVE directive which does not match this kernel` | DKMS did not build the module: the kernel is not in `scripts/supported-kernels`; see section 1. |
| `mount: ... unknown filesystem type 'moobfs_layer'` | The module is not installed for this kernel: `ls /lib/modules/$(uname -r)/updates/moobfs/`, then `sudo depmod -a`. |
| `mount` fails with `EPERM` inside a container or user namespace | Deliberate: mount on the host and bind-mount into the container (ADR-0011). |
| `lower= is required`, or `... must be between ...` in `dmesg` | A missing or out-of-range mount option; see the table in section 3. |
| A query returns `ENOTTY` | `QUERY` needs the mount root, `QUERY2` a domain directory; a bind mount of a subdirectory cannot issue `QUERY` (distribution strategy §2.5). `moobfs diagnose <dir>` says whether `<dir>` is a domain directory, above the domains or inside one. |
| `moobfs query` exits 75 | A bound of section 5 refused the query; the message names it. For `ENOMEM`, run `moobfs diagnose <dir>` as the same user, in the same session: it shows the memory limit that applies and whether it was reached. `E2BIG`: raise `index_build_work` or mount with a larger `domain_depth`. `EDQUOT`: retry after a minute. `ETIMEDOUT`: the lower filesystem was too slow for the ten-second deadline (a cold cache, a busy disk). |
| A query process disappears (killed) | Its memory cgroup's OOM killer chose it at `MemoryMax=`; `moobfs diagnose` reports `oom-killed`, and `journalctl -k` shows `Memory cgroup out of memory`. Raise the limit or close idle descriptors. |

**Tracepoints, for the administrator.** Each build of a domain, whatever its outcome, is one
`moobfs_domain_build` event; a release is `moobfs_domain_release` ([ADR-0012](decisions/0012-index-on-demand-and-reclaimable.md),
security roadmap S13). They are not a stable interface, and tracefs is readable by root only:

```sh
echo 1 | sudo tee /sys/kernel/tracing/events/moobfs/enable
sudo cat /sys/kernel/tracing/trace_pipe
# moobfs_domain_build: dev=0:52 domain=1835009 err=-12 tries=1 released=0 bytes=0 ns=48211
echo 0 | sudo tee /sys/kernel/tracing/events/moobfs/enable
```

`domain` is the inode number of the domain directory on the lower (`stat -c %i`); `err` is the
negative errno the query got (0 built, -12 `ENOMEM`, -7 `E2BIG`, -110 `ETIMEDOUT`, -11
`EAGAIN`); `released` counts the domains released to make room; `by=budget` or `by=shrinker` on
a release says why. A refusal by the build rate (`EDQUOT`) happens before the build and has no
event. The event does not carry the uid: match it with the user's report by time and domain.

## What was tested

`make install-test` (`vm/install-test/`) follows sections 1 to 7 in order in a disposable QEMU
VM booted from the Debian 13 generic cloud image of 2026-10-21. On 2026-10-25 it passed
(`MOOBFS_INSTALL_PASS`, [journal 0197](history/0197-first-contact-and-install.md)): install
refused on the image's 6.12.113 kernel, installed on 6.12.111, mounted by hand, by `fstab` at
boot with the module loaded through its alias, and by the mount unit; alice's directory closed to
bob; attribute kept across a reboot; uninstalled with the lower still readable. It was not run
on real hardware, with Secure Boot on, with btrfs or XFS as the lower, or on any distribution
other than Debian 13.

*Update, 2026-10-27 ([journal 0206](history/0206-debian-6-12-113-supported.md)):* with 6.12.113
supported, `make install-test` installs on the image's own kernel. It passed again
(`MOOBFS_INSTALL_PASS`, 397 s under KVM on the dedicated host): `make install` refused an unlisted
release given through `KVER`, then installed on 6.12.113 without any kernel switch, and every
later step as above.

# How projects like MoobFS get test hardware and grow their maturity

Status: research, 2026-10-18 (journal 0123). Question: one maintainer, one benchmark machine that
is often off, a module out of the kernel tree. How do comparable projects test across hardware,
kernels and architectures, and what can MoobFS use? Every statement is linked to its source;
"unverified" marks what could not be confirmed for 2026.

## What comparable projects do

- **OpenZFS (out of tree, Linux and FreeBSD).** Since 2024 its functional tests (ZTS) run in QEMU
  VMs on GitHub-hosted runners, one cloud image per distribution (AlmaLinux, Arch, CentOS Stream,
  Fedora, Debian, FreeBSD, Ubuntu), two VMs per runner, about 2.5 to 3 hours per full run instead
  of 4+ on their former buildbot; pull requests run a shorter list.
  [workflow `zfs-qemu.yml`](https://cdn05042023.gitlink.org.cn/folkslinux/zfs/src/branch/master/.github/workflows/zfs-qemu.yml),
  [scripts](https://cgit.freebsd.org/src/tree/sys/contrib/openzfs/.github/workflows/scripts/qemu-6-tests.sh).
  The axis of variation is the distribution, which carries its own kernel.
- **bcachefs.** Its author's `ktest` builds a kernel, boots it in QEMU with scratch disks and runs
  declared tests; a small fleet (AArch64 hosts plus one x86-64) tests every commit of every tracked
  branch, with lockdep and KASAN variants at lower priority, and publishes a dashboard; others get
  access by sending an SSH key. [CI infrastructure](https://bcachefs.org/CIInfrastructure/),
  [announcement](https://lkml.iu.edu/hypermail/linux/kernel/2407.1/08152.html). In the same thread,
  maintainers pointed out that such a suite must be cheap enough for other labs to run.
- **xfstests-bld (kvm-xfstests, gce-xfstests).** Prebuilt test appliances for fstests under KVM
  or on Google Compute Engine; its author says one goal is to let graduate students building
  research filesystems run regression tests; arm64 images since October 2024.
  [repository](https://github.com/tytso/xfstests-bld),
  [LWN](https://lwn.net/Articles/994602),
  [fsdevel thread](https://lkml.rescloud.iu.edu/2101.3/03616.html). Each filesystem has a config
  directory with mkfs, mount and check hooks; how an out-of-tree one plugs in is not documented
  (unverified).
- **sched-ext, netdev, drgn.** virtme-ng boots the kernel under test in CI with the runner's root
  filesystem; sched-ext caps the guest's lifetime to catch hangs; drgn ships the kernel's modules
  into the VM so module loading is tested.
  [sched-ext](https://gitea.hillion.co.uk/JakeHillion/scx/blame/commit/a3b5941c3a0facf2fe292af9942f8be5ed6942fd/.github/workflows/build-scheds.yml),
  [netdev](https://github.com/linux-netdev/nipa/wiki/How-to-run-netdev-selftests-CI-style),
  [drgn](https://gitea.hillion.co.uk/JakeHillion/drgn/commits/commit/006b62e530bc3861ca9d3bafd662da09fe5646e0/vmtest/vm.py).
  For a matrix of kernel versions, `kernel-devel-vm-action` installs a chosen Fedora kernel per
  matrix entry for module CI. [action](https://github.com/antoineco/kernel-devel-vm-action/blob/main/README.md)
- **Upstream test services** (KernelCI, Intel's 0-day/LKP, syzbot) test the kernel tree; nothing
  found on out-of-tree modules (unverified), so they apply only if a module is merged.
  [KernelCI](https://docs.kernelci.org/intro/),
  [0-day](https://intel.com/content/www/us/en/developer/topic-technology/open/linux-kernel-performance/overview.html)

MoobFS already follows the common pattern (virtme-ng and a KASAN kernel on GitHub runners, KVM
since pull request 74; fstests ported locally; syzkaller run by hand). What it lacks against these
projects: more than one kernel, more than one architecture, more than one machine for numbers, and
a second maintainer.

## Resources open to a project like MoobFS

| Resource | What | Access | For MoobFS |
| --- | --- | --- | --- |
| GitHub arm64 runners | free for public repositories since August 2025, 4 vCPUs | [changelog](https://github.blog/changelog/2025-08-07-arm64-hosted-runners-for-public-repositories-are-now-generally-available/) | a second architecture; KVM on them unverified |
| IBM LinuxONE open source cloud | s390x (big-endian) VMs for open source projects, by request form | [IBM community](https://community.ibm.com/community/user/blogs/elizabeth-k-joseph1/2023/02/06/circleci-self-hosted-runner-for-zsystems-linuxone?hlmlt=VT) (current terms unverified) | a big-endian kernel for the module and the encoding |
| OSU Open Source Lab | x86, aarch64 and ppc64le VMs for CI | [Debian news](https://micronews.debian.org/2025/1746290946.html); [at risk of closure, 2025](https://www.phoronix.com/news/OSU-OSL-Risks-Closure) | uncertain |
| Grid'5000 | bare-metal research testbed | Open Access program **closed**; accounts for French academics and their formal collaborators, companies by contacting the board ([open access](https://grid5000.fr/w/Grid5000:Open-Access), [accounts](https://www.grid5000.fr/w/Grid5000:Get_an_account)) | only through an academic partner |
| Chameleon, CloudLab | bare-metal research testbeds | US institutions; international terms unverified ([2014 announcement](https://fedtechmagazine.com/article/2014/09/nsfs-20-million-investment-will-fuel-cloud-based-research)) | only through an academic partner |
| RNP testbeds | remote experimentation for Brazilian and Latin American researchers | through an institution ([eduroam testbed](https://sol.sbc.org.br/index.php/sbseg_estendido/article/view/4149)) | only through an academic partner |
| AWS credits for open source | promotional credits; OSI license, preference for multi-maintainer or foundation projects | [SDxCentral](https://www.sdxcentral.com/news/aws-makes-it-rain-extends-credits-to-open-source-projects/) (current terms unverified) | machines for numbers on varied hardware |
| Google Cloud research credits | about US$5 000 for faculty, US$1 000 for PhD students | [listing](https://grantedai.com/grants/google-cloud-research-credits-faculty-phd-students-nonprofit-researchers-ai-workloads-google-2026) | through an academic partner; gce-xfstests runs there |
| Phoronix Test Suite / OpenBenchmarking.org | test profiles anyone runs and uploads (opt-in), repeated until variance is low | [fio profile](https://www.openbenchmarking.org/test/pts/fio) | volunteers' machines as measurement points (issue 68) |
| NLnet NGI Zero Commons Fund | grants for open technology | final call closed 1 June 2026 ([NLnet](https://www.nlnet.nl/commonsfund)) | closed |
| Sovereign Tech Agency (Fund) | funding for open digital base technologies, work above €50 000 | [criteria](https://www.sovereign.tech/programs/fund); eligibility outside Germany unverified | later, if the project needs funded maintenance |

## Maturity criteria worth tracking

- **Kernel's own list** for a new filesystem (`Documentation/filesystems/adding-new-filesystems.rst`),
  answered on the site's status page (ADR-0015).
- **OpenSSF Best Practices**: the project holds the passing badge; the gold level requires a bus
  factor of two or more ([gold criteria example](https://www.bestpractices.dev/projects/3044/gold));
  whether silver does was not confirmed.

## Conclusions for MoobFS (decided in journal 0123)

1. The cheapest gains need no outside resource: a big-endian run of the attribute encoding and
   the tools under QEMU user-mode emulation on the existing runners, an arm64 job on GitHub's free
   runners, and a check that the module builds against other kernel series. All of them test what
   exists; none adds a feature.
2. Most bare-metal and credit programs open through an academic partner. The follow-up to the
   professors contacted on 2026-10-14 offers a research topic together with the testbed it could
   use.
3. Requests the maintainer can make himself: the IBM LinuxONE open source cloud (s390x) and AWS
   credits for open source.
   *Note, 2026-11-10 (journal 0280):* the LinuxONE request was granted; on an IBM LinuxONE
   Community Cloud instance (z17, 2 vCPUs, 8 GB) the module ran on a big-endian kernel under KVM:
   the full suite passed on Debian's 6.12.111, and under KASAN every step but those stopped by
   the build bound or by time thresholds on that slow debug kernel.
4. A second maintainer is a criterion of the kernel's list and of the OpenSSF gold level; the calls
   for reviewers and collaborators stay open.

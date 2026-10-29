CC ?= cc
CFLAGS ?= -O2 -g
WARN = -std=c11 -Wall -Wextra -Werror -Wconversion -Wshadow
COMMON = common/hash.c common/format.c
HEADERS = include/moobfs_format.h
KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD ?= build/host


.PHONY: all help tools attr-tools bench-tools userspace-tools bench-smoke hostile-test json-test test fuzz fuzz-build lint man-lint checkpatch module layer vm-test sqlite-ext install uninstall install-test install-unit container-test package-test bench-test deb pylib-test repro-test static
all: tools
help:
	@echo 'Host targets (macOS or Linux; never load a module on the host):'
	@echo '  make / make tools  build mkfs.moobfs, fsck.moobfs and attr_test into $$(BUILD)'
	@echo '  make test          attr_test unit tests and malformed-image tests'
	@echo '  make lint          shellcheck on vm/ and scripts/ shell scripts, SPDX headers on C, shell and Python sources (same as CI)'
	@echo '  make checkpatch    kernel checkpatch.pl --no-tree on layer/ and kernel/ (required in CI; needs curl, perl)'
	@echo '  make static        W=1, sparse, smatch, Coccinelle, clang analyzer, kernel-doc on the modules (S19); needs only Docker'
	@echo '  make man-lint      lint the manual pages in man/ with mandoc'
	@echo '  make fuzz          libFuzzer over the attributes, the image parser and the tool parsers'
	@echo 'Linux-only targets:'
	@echo '  make attr-tools    moobfs, moobfs-attr, moobfs-check, moobfs-index'
	@echo '  make bench-tools   M4 harness binaries; make bench-smoke runs its smoke test'
	@echo '  make hostile-test  adversarial lower trees through moobfs-check and moobfs-index (S4), and their walk under a racing link swap'
	@echo '  make sqlite-ext    the moobfs SQLite extension, $$(BUILD)/moobfs.so (needs libsqlite3-dev >= 3.38)'
	@echo 'Kernel (inside the VM container, not on your host):'
	@echo '  make vm-test       full native suite in a disposable QEMU VM; needs only Docker'
	@echo '  make module/layer  build the kernel modules against KDIR'
	@echo 'Installing on a Linux host with a supported kernel (docs/operations.md):'
	@echo '  sudo make install  build the layer module and tools, copy them, depmod (refuses other kernels)'
	@echo '  sudo make uninstall  remove what install copied; lower directories are untouched'
	@echo '  make install-test  follow docs/operations.md in a disposable Debian 13 VM; needs only Docker'
	@echo '  make install-unit  install.sh never replaces an existing entry without FORCE=1 (Linux)'
	@echo '  make pylib-test    install the Python library (python/) with pip in a fresh venv and test it'
	@echo '  make container-test  docs/container-integration.md with Docker and Podman in a disposable VM'
	@echo '  make deb           experimental moobfs-dkms and moobfs-tools packages into build/deb (Debian; not published)'
	@echo '  make package-test  the packages under Secure Boot in a disposable Debian 13 VM; needs only Docker'
	@echo '  make repro-test    build the module and the packages twice in clean containers; byte-identical or fail (Docker)'
	@echo '  make bench-test    the on-demand index benchmark (issue #68) in that VM; MOOB_BENCH_FS=ext4|btrfs|xfs'
	@echo 'Details: docs/development.md and docs/testing.md'
tools: $(BUILD)/mkfs.moobfs $(BUILD)/fsck.moobfs $(BUILD)/attr_test
# SQL over a domain, no database (ADR-0013, journal 0108): load with .load or load_extension
sqlite-ext: $(BUILD)/moobfs.so
$(BUILD)/moobfs.so: sqlite/moobfs_vtab.c common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -fPIC -shared -Iinclude sqlite/moobfs_vtab.c common/attr.c $(LDFLAGS) -o $@
$(BUILD):
	mkdir -p $(BUILD)
$(BUILD)/mkfs.moobfs: tools/mkfs.c $(COMMON) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/mkfs.c $(COMMON) $(LDFLAGS) -o $@
$(BUILD)/fsck.moobfs: tools/fsck.c $(COMMON) $(HEADERS) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/fsck.c $(COMMON) $(LDFLAGS) -o $@
$(BUILD)/attr_test: tests/attr_test.c common/attr.c include/moobfs_attr.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tests/attr_test.c common/attr.c $(LDFLAGS) -o $@
# Linux-only tools over the xattr API (built statically inside the VM container too).
attr-tools: $(BUILD)/moobfs-attr $(BUILD)/moobfs-check $(BUILD)/moobfs-index $(BUILD)/moobfs
$(BUILD)/moobfs-attr: tools/moobfs-attr.c common/attr.c include/moobfs_attr.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/moobfs-attr.c common/attr.c $(LDFLAGS) -o $@
$(BUILD)/moobfs-check: tools/moobfs-check.c tools/walk.h tools/ext-acl.h common/attr.c include/moobfs_attr.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/moobfs-check.c common/attr.c $(LDFLAGS) -o $@
$(BUILD)/moobfs-index: tools/moobfs-index.c tools/query-common.h tools/walk.h tools/ext-acl.h common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/moobfs-index.c common/attr.c $(LDFLAGS) -o $@
$(BUILD)/moobfs: tools/moobfs.c tools/query-common.h common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude tools/moobfs.c common/attr.c $(LDFLAGS) -o $@
# M4 benchmark harness (docs/benchmark-harness.md). gen is portable; load is Linux-only.
bench-tools: $(BUILD)/bench-gen $(BUILD)/bench-load $(BUILD)/bench-sqlclient $(BUILD)/bench-aimem-latency $(BUILD)/bench-readcost $(BUILD)/query
$(BUILD)/bench-gen: bench/gen.c bench/common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) bench/gen.c -lm $(LDFLAGS) -o $@
$(BUILD)/bench-load: bench/load.c bench/common.h tools/query-common.h common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude -Itools bench/load.c common/attr.c -lm $(LDFLAGS) -o $@
# the ioctl test client that bench/ondemand.sh and the other bench scripts call as $BIN/query;
# the flags of vm/build-tests.sh, which builds the same program for the VM suite (journal 0238)
$(BUILD)/query: tests/query.c tools/query-common.h common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror -Iinclude tests/query.c common/attr.c $(LDFLAGS) -o $@
# the SQLite client needs libsqlite3-dev; SQLITE_LIBS adds what a static link requires
SQLITE_LIBS ?= -lsqlite3
$(BUILD)/bench-sqlclient: bench/sqlclient.c bench/common.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) bench/sqlclient.c $(SQLITE_LIBS) -lm $(LDFLAGS) -o $@
# what a client pays to read a query's results, for the READ_BATCH decision (docs/spec-m3.md §4)
$(BUILD)/bench-readcost: bench/readcost.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude bench/readcost.c $(LDFLAGS) -o $@
# stage 2 of the real-client test (docs/client-test-plan.md): SQLite and QUERY2 in one process
$(BUILD)/bench-aimem-latency: bench/client/aimem-latency.c bench/common.h include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude bench/client/aimem-latency.c $(SQLITE_LIBS) -lm $(LDFLAGS) -o $@
# User-space baseline of the index (journal 0092): SQLite per domain kept current by inotify. Linux only.
userspace-tools: $(BUILD)/udx-index $(BUILD)/udx-watch $(BUILD)/udx-query
$(BUILD)/udx-%: bench/userspace/udx-%.c bench/userspace/udx.h tools/query-common.h tools/ext-acl.h common/attr.c include/moobfs_attr.h include/moobfs_ioctl.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARN) -Iinclude -Itools $< common/attr.c $(SQLITE_LIBS) $(LDFLAGS) -o $@
bench-smoke: bench-tools attr-tools
	sh bench/smoke.sh $(BUILD)
# Security roadmap S4, user-space line: trees no well-behaved writer would leave on the lower.
$(BUILD)/hostile: tests/hostile.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wall -Wextra -Werror tests/hostile.c $(LDFLAGS) -o $@
hostile-test: attr-tools $(BUILD)/hostile
	sh tests/hostile-user.sh $(BUILD) $(HOSTILE_SEEDS)
	sh tests/tools-json.sh $(BUILD)
	sh tests/tools-walk.sh $(BUILD)
json-test: attr-tools $(BUILD)/hostile
	sh tests/tools-json.sh $(BUILD)
test: tools
	$(BUILD)/attr_test
	MOOB_BUILD_DIR=$(BUILD) python3 tests/test_images.py
# libFuzzer runs (needs LLVM clang with libFuzzer, e.g. on Linux), FUZZ_TIME seconds per target:
# attr (common/attr.c), image (the image parser common/format.c) and tools (query terms and ACL
# values of the tools; journal 0237). New inputs go to $(BUILD)/fuzz-corpus/<target>, never to
# the seeds in tests/fuzz-corpus; a crash is written to $(BUILD)/fuzz-crashes/<target>-*.
# FUZZ_TARGETS picks targets, FUZZ_FLAGS adds libFuzzer options (the weekly run in fuzz.yml).
FUZZ_TIME ?= 20
FUZZ_TARGETS ?= attr image tools
FUZZ_FLAGS ?=
FUZZ_CFLAGS = -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude
FUZZ_SRC_attr = tests/fuzz_attr.c common/attr.c
FUZZ_SRC_image = tests/fuzz_image.c common/format.c common/hash.c
FUZZ_SRC_tools = tests/fuzz_tools.c
FUZZ_LEN_attr = 4200
FUZZ_LEN_image = 24577
FUZZ_LEN_tools = 8192
fuzz-build: | $(BUILD)
	$(foreach t,$(FUZZ_TARGETS),clang $(FUZZ_CFLAGS) $(FUZZ_SRC_$(t)) -o $(BUILD)/fuzz_$(t) &&) true
fuzz: fuzz-build
	$(foreach t,$(FUZZ_TARGETS),mkdir -p $(BUILD)/fuzz-corpus/$(t) $(BUILD)/fuzz-crashes && \
	  $(BUILD)/fuzz_$(t) -max_total_time=$(FUZZ_TIME) -max_len=$(FUZZ_LEN_$(t)) -print_final_stats=1 \
	  -artifact_prefix=$(BUILD)/fuzz-crashes/$(t)- $(FUZZ_FLAGS) $(BUILD)/fuzz-corpus/$(t) tests/fuzz-corpus/$(t) &&) true
lint:
	@command -v shellcheck >/dev/null 2>&1 || { echo 'shellcheck not found: apt install shellcheck, or brew install shellcheck'; exit 1; }
	shellcheck --shell=sh $$(git ls-files 'vm/*.sh' 'scripts/*.sh')
	sh scripts/check-spdx.sh
# Manual pages of the tools (issue #8): mandoc must report nothing at warning level.
man-lint:
	mandoc -Tlint -Wwarning man/*
# Style check, required in CI since journal 0050; the script pins and verifies checkpatch.pl from a stable kernel tag.
checkpatch:
	sh scripts/checkpatch.sh
# Security roadmap S5 and S19 (journal 0242): the kernel's static checkers in a pinned image,
# findings compared with tests/static/baseline.txt (.github/workflows/static.yml runs the same).
static:
	sh scripts/static-analysis.sh
module:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel modules
# The source root maps to the empty prefix, so neither __FILE__ nor the debug information
# records where the tree was built (security roadmap S10, journal 0236). KCFLAGS, not
# layer/Makefile, because scripts/Makefile.modfinal compiles moobfs_layer.mod.c without it.
layer:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/layer KCFLAGS="$(KCFLAGS) -ffile-prefix-map=$(CURDIR)/=" modules
vm-test:
	sh vm/run.sh
# Roadmap item C2: install and remove on the host's running kernel (scripts/supported-kernels).
install:
	sh scripts/install.sh install
uninstall:
	sh scripts/install.sh uninstall
install-test:
	sh vm/install-test/run.sh
install-unit:
	sh scripts/test-install.sh
container-test:
	MOOB_TEST=container sh vm/install-test/run.sh
# Roadmap item C6 (ADR-0028): DKMS and Debian packages, built and tested in VMs only.
deb:
	sh scripts/build-debs.sh
package-test:
	MOOB_TEST=package sh vm/install-test/run.sh
# Security roadmap S10 (journal 0236): reproducible module and packages, two clean containers.
repro-test:
	sh scripts/repro-ko.sh
# bench/ondemand.sh in the same VM under KVM, a trend monitor (journal 0238, .github/workflows/bench.yml)
bench-test:
	MOOB_TEST=bench sh vm/install-test/run.sh
# Roadmap item C5: the Python library installed with pip in a clean venv, its build backend
# pinned by hash, built from a copy so the source tree gets no build output; the tests then
# import the installed package, not python/src.
pylib-test:
	rm -rf $(BUILD)/pylib-venv $(BUILD)/pylib-src
	mkdir -p $(BUILD)
	cp -R python $(BUILD)/pylib-src
	python3 -m venv $(BUILD)/pylib-venv
	$(BUILD)/pylib-venv/bin/pip install -q --require-hashes -r python/build-requirements.txt
	$(BUILD)/pylib-venv/bin/pip install -q --no-build-isolation --no-deps $(BUILD)/pylib-src
	MOOBFS_TEST_INSTALLED=1 $(BUILD)/pylib-venv/bin/python -m unittest discover -s python/tests -v

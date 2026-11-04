# Test harness notes (all you may use besides the specification and the UAPI headers)

- Tests are C programs built on Linux with `cc -O2 -Wall -Wextra -Iinclude tests/NAME.c -o NAME`
  (gcc 14, glibc). Only libc and the UAPI headers in `include/` are available.
- A suite step runs as root inside a dedicated VM (Debian, kernel 6.12). Lowers are ext4,
  btrfs and XFS; each step loops over the three. The shell pattern is:

      mount -t "$fs" "$dev" "$LOWER"
      rm -rf "$LOWER/dir" && mkdir -m 755 "$LOWER/dir"
      mount -t moobfs_layer -o lower="$LOWER/dir",domain_depth=1 none "$LAYER"
      "$BIN/yourtest" ARGS "$LAYER" "$LOWER/dir" || fail "name-$fs"
      umount "$LAYER" && rm -rf "$LOWER/dir"

  `domain_depth=1` makes every first-level directory under the lower root its own domain
  (spec section 2). Domains are built on demand (spec section 9): a domain that no query has
  touched since mount has no index ("cold").
- The test may act on the lower directly (it is the real ext4/btrfs/XFS tree) and through
  the layer mount (`$LAYER`, a passthrough view; QUERY2 and GET_INFO ioctls are issued on an
  open directory of the layer). Changes may be made through either path.
- After every step, the harness fails the run if the kernel log has any warning, BUG, KASAN,
  UBSAN or lockdep report. A test does not need to read dmesg itself, but it must exit 0 only
  if every check it makes holds, and nonzero with a clear message otherwise.
- To act as an unprivileged user, fork and setgid()/setuid() in the child (see tests/asuid.c).
- tests/query.c is an example of issuing QUERY2 and reading answers; reuse its patterns.

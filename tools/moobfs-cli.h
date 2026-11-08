/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * What tools/moobfs.c and tools/moobfs-workspace.c, the two files of the moobfs command, share:
 * the exit statuses of docs/tools.md and the few functions of moobfs.c the workspace commands
 * call (ADR-0035).
 */
#ifndef MOOBFS_CLI_H
#define MOOBFS_CLI_H
#include <stddef.h>
#include "moobfs_ioctl.h"

#define EX_INCOMPLETE 3
#define EX_USAGE 64
#define EX_DATAERR 65
#define EX_UNAVAILABLE 69
#define EX_TEMPFAIL 75
#define EX_NOPERM 77

/* statfs f_type of a moobfs_layer mount (docs/client-contract.md, section 7) */
#define LAYER_FS_TYPE 0x4d4f4f42

/* Prints the synopsis on standard error; returns EX_USAGE. */
int moobfs_usage(void);

/*
 * GET_INFO of mount, which must be the root of a moobfs_layer mount. 0, or an exit status after
 * printing why (EX_UNAVAILABLE: not a layer, or not its root).
 */
int moobfs_layer_root(const char *mount, struct moob_info *info);

/*
 * The number of files of the domain directory open at fd that carry the typed attribute name
 * (QUERY2 "name exists", following the cursor, as the caller). 0, or the errno of the query.
 */
int moobfs_count_exists(int fd, const char *name, size_t *count);

/* moobfs workspace create|grant|revoke|list ...; argv[0] is "workspace". */
int moobfs_workspace(int argc, char **argv);

#endif

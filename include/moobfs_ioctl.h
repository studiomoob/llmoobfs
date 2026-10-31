/* SPDX-License-Identifier: GPL-2.0-only WITH Linux-syscall-note */
/*
 * MoobFS layer ioctl interface (UAPI version 1). QUERY and GET_INFO are issued on an open
 * descriptor of the mount root directory, QUERY2 and QUERY2_BATCH on one of a domain
 * directory. Unknown commands return ENOTTY; a request with
 * reserved fields set, an unknown version or an invalid term returns EINVAL.
 * Layout uses fixed-width little-endian fields and explicit padding; the same
 * header is compiled by the module and by the user-space tools.
 */
#ifndef MOOBFS_IOCTL_H
#define MOOBFS_IOCTL_H
#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <linux/types.h>
#include <sys/ioctl.h>
#endif

#define MOOB_IOC_MAGIC 'M'
#define MOOB_UAPI_VERSION 1U

/* Query operators. PREFIX applies to strings only; EXISTS ignores the value. */
enum moob_query_op {
	MOOB_OP_EQ = 1,
	MOOB_OP_LT = 2,
	MOOB_OP_LE = 3,
	MOOB_OP_GT = 4,
	MOOB_OP_GE = 5,
	MOOB_OP_PREFIX = 6,
	MOOB_OP_EXISTS = 7,
};

#define MOOB_QUERY_MAX_TERMS 4U
#define MOOB_QUERY_NAME_MAX 64U
#define MOOB_QUERY_VALUE_MAX 1024U
#define MOOB_QUERY_PATH_MAX 256U
#define MOOB_QUERY_MAX_LIMIT 4096U

struct moob_query_term {
	char name[MOOB_QUERY_NAME_MAX + 1];	/* NUL-terminated, without "user.moob." */
	__u8 op;				/* enum moob_query_op */
	__u8 type;				/* enum moob_attr_type of the value */
	__u8 flags;				/* MOOB_TERM_*; QUERY2 only, 0 for QUERY */
	__u16 len;				/* payload bytes in value[] */
	__u8 value[MOOB_QUERY_VALUE_MAX];	/* payload without the type byte */
	__u8 pad[4];				/* must be 0 (pad[3] was implicit tail padding until ADR-0026) */
};

/* One result per matching inode. path is relative to the mount root; empty when unknown. */
struct moob_query_result {
	__u64 ino;
	__u32 flags;				/* MOOB_RES_* */
	__u32 path_len;				/* bytes of path actually stored */
	char path[MOOB_QUERY_PATH_MAX];
};
/* Term flag: the term also matches inodes that do not carry the attribute ("missing or ..."). */
#define MOOB_TERM_MISSING_OK 0x1U

#define MOOB_RES_PATH_UNKNOWN 0x1U		/* no cached name for this inode */
#define MOOB_RES_PATH_TRUNCATED 0x2U

struct moob_query {
	__u32 version;				/* MOOB_UAPI_VERSION */
	__u32 nterms;				/* 1..MOOB_QUERY_MAX_TERMS */
	__u32 limit;				/* max results to copy, 1..MOOB_QUERY_MAX_LIMIT */
	__u32 flags;				/* must be 0 */
	__u64 results;				/* user pointer to struct moob_query_result[limit] */
	__u32 count;				/* out: results written */
	__u32 truncated;			/* out: 1 if more matches existed */
	__u64 reserved[2];			/* must be 0 */
	struct moob_query_term terms[MOOB_QUERY_MAX_TERMS];
};

#define MOOB_CAP_REFLINK 0x1U
#define MOOB_CAP_DATA_CHECKSUMS 0x2U
#define MOOB_CAP_SNAPSHOTS 0x4U
#define MOOB_CAP_XATTR 0x8U
#define MOOB_FLAG_NOLINKS 0x1U
#define MOOB_FLAG_INDEX_OFF 0x2U
#define MOOB_FLAG_PATHS_STALE 0x4U
#define MOOB_FLAG_INDEX_INCOMPLETE 0x8U	/* reserved: not set since ADR-0016 (older kernels set it) */
#define MOOB_FLAG_DOMAINS 0x10U			/* domain_depth > 0: QUERY2 on domain directories */
#define MOOB_FLAG_COUNTERS 0x20U		/* the index counters below are filled (CAP_SYS_ADMIN) */
/*
 * The index of a domain is built by the first query on it, not at mount (ADR-0012): the
 * counters below cover the domains built so far, and a mount-wide QUERY builds all of them.
 */
#define MOOB_FLAG_ON_DEMAND 0x40U

struct moob_info {
	__u32 uapi_version;
	__u32 format_version;			/* attribute encoding version supported */
	__u32 attr_name_max;
	__u32 attr_string_max;
	__u32 attr_raw_max;
	__u32 attr_max_per_file;
	__u32 caps;				/* MOOB_CAP_*, advertised from the lower fs type */
	__u32 flags;				/* MOOB_FLAG_* */
	__u64 index_budget;			/* bytes */
	/*
	 * index_used, index_entries, index_inodes, heterogeneous and fallback_domains sum every
	 * user's memories, so they are filled only for CAP_SYS_ADMIN (MOOB_FLAG_COUNTERS) and are
	 * zero for everyone else (ADR-0011, finding F1).
	 */
	__u64 index_used;			/* bytes accounted to the index */
	__u32 index_entries;			/* typed attribute entries indexed */
	__u32 index_inodes;			/* inodes with at least one typed attribute */
	char lower_fs[16];			/* lower filesystem type name */
	__u32 domain_depth;			/* 0: domains off (v1 only) */
	__u32 heterogeneous;			/* indexed inodes that fail the homogeneity rule */
	__u32 fallback_domains;			/* domains answered with per-result checks */
	__u32 domains_built;			/* domains whose index is built, the part outside every domain included */
	__u32 domain_builds;			/* builds completed since mount; grows when a domain is rebuilt */
	__u32 domain_releases;			/* built domains released since mount, to free memory */
	__u64 reserved[1];
};

/*
 * QUERY2 (docs/spec-query-v2.md): issued on an open descriptor of a domain directory.
 * Answers from that domain's partition only; results are paths relative to the domain
 * directory. With order_by, results come in (value, ino) order of that attribute's entries
 * of order_type, ascending or descending, and every term is tested on each; without it, in
 * the scan order of terms[0]. When truncated is set, cursor holds the position after the last
 * result: sending the same query (any limit) with that cursor continues from there. A cursor
 * that is corrupt, from another query or mount, or issued before the domain fell back to
 * per-result checks gives ESTALE. A term with MOOB_TERM_MISSING_OK also matches inodes
 * without that attribute (an attribute of another type is not missing); without order_by at
 * least one term must lack the flag, and EXISTS cannot carry it.
 * Building a stale or absent domain can return EDQUOT (unprivileged rebuild frequency), E2BIG
 * (scan work) or ETIMEDOUT (cooperative deadline: 10 s of the builder's CPU time, or 120 s
 * elapsed); no partial answer succeeds.
 */
#define MOOB_QUERY2_VERSION 2U
#define MOOB_QUERY2_CURSOR 128U
#define MOOB_ORDER_ASC 1U
#define MOOB_ORDER_DESC 2U

struct moob_query2 {
	__u32 version;				/* MOOB_QUERY2_VERSION */
	__u32 nterms;				/* 1..MOOB_QUERY_MAX_TERMS; 0 only with order_by */
	__u32 limit;				/* 1..MOOB_QUERY_MAX_LIMIT */
	__u32 flags;				/* must be 0 */
	__u64 results;				/* user pointer to struct moob_query_result[limit] */
	__u32 count;				/* out */
	__u32 truncated;			/* out */
	char order_by[MOOB_QUERY_NAME_MAX + 1];	/* attribute to order by; empty: scan order */
	__u8 order;				/* MOOB_ORDER_ASC or _DESC with order_by, else 0 */
	__u8 order_type;			/* string, int64, double or time with order_by, else 0 */
	__u8 pad[5];				/* must be 0 */
	__u8 cursor[MOOB_QUERY2_CURSOR];	/* in: zero or a cursor returned by the same query;
						 * out: next position when truncated, else zero */
	__u64 reserved[4];			/* must be 0 */
	struct moob_query_term terms[MOOB_QUERY_MAX_TERMS];
};

/*
 * Up to MOOB_QUERY2_BATCH_MAX QUERY2 requests on the same domain directory in one call, with
 * one permission check: each request has its own results array, limit, count, truncated and
 * cursor. Requests are all validated before any runs; they run in order, each under its own
 * index lock, so the batch is not a snapshot. Union and deduplication stay with the caller.
 * On any error discard every output, including results copied by earlier subqueries.
 */
#define MOOB_QUERY2_BATCH_MAX 8U

struct moob_query2_batch {
	__u32 version;				/* MOOB_QUERY2_VERSION */
	__u32 nqueries;				/* 1..MOOB_QUERY2_BATCH_MAX */
	__u64 queries;				/* user pointer to struct moob_query2[nqueries] */
	__u64 reserved[2];			/* must be 0 */
};

#define MOOB_IOC_QUERY _IOWR(MOOB_IOC_MAGIC, 1, struct moob_query)
#define MOOB_IOC_GET_INFO _IOR(MOOB_IOC_MAGIC, 2, struct moob_info)
#define MOOB_IOC_QUERY2 _IOWR(MOOB_IOC_MAGIC, 3, struct moob_query2)
#define MOOB_IOC_QUERY2_BATCH _IOWR(MOOB_IOC_MAGIC, 4, struct moob_query2_batch)

#endif

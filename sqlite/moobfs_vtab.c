/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * moobfs: a SQLite virtual table over one MoobFS domain (ADR-0013, journal 0108).
 *
 *   CREATE VIRTUAL TABLE temp.mem USING moobfs('/mnt/layer/ns1',
 *       'kind string', 'agent string', 'created time', 'score int64');
 *   SELECT path, agent FROM mem WHERE kind IN ('note', 'decision') AND score >= 10
 *       ORDER BY created DESC LIMIT 20;
 *
 * A row is a file of the domain that carries the first declared attribute (the key) and every
 * attribute declared "required" ('created time required'); the rowid is its inode number; the
 * columns are path (relative to the domain directory) and one column per declared attribute, NULL
 * when the file does not carry it. A required column is never NULL, so an ORDER BY on it can be
 * the kernel's, with the LIMIT; on any other column SQLite sorts. Nothing is stored: every row comes from
 * a QUERY2 the kernel answers for the calling process, and column values are read with getxattr as
 * that process, only for the columns the statement uses.
 *
 * What is handed to the kernel: =, <, <=, >, >=, IS NOT NULL on attribute columns (up to the four
 * terms of QUERY2, the key's EXISTS included); an IN list, whole, as one QUERY2_BATCH per eight
 * values; an ORDER BY on one attribute column that cannot be NULL. Everything else (OR, other
 * operators, sorting, LIMIT, aggregation) is SQLite's. Only IS NOT NULL is declared handled
 * ("omit"); SQLite checks every other pushed condition again on each row.
 *
 * Built as a loadable extension (entry point sqlite3_moobfs_init), or with -DSQLITE_CORE into a
 * program that calls sqlite3_moobfs_init itself.
 */
#include <sqlite3ext.h>
SQLITE_EXTENSION_INIT1
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/xattr.h>
#include <unistd.h>
#include "moobfs_attr.h"
#include "moobfs_ioctl.h"

#define MV_MAX_COLS 64			/* path + 63 attributes */
#define MV_PAGE 256U			/* results per QUERY2 call without a LIMIT hint */
#define MV_TRIES 4			/* QUERY2 retried on EAGAIN (the domain kept changing) */

enum { MV_PATH = 0 };			/* column 0; attributes are columns 1.. */

struct mv_col {
	char name[MOOB_ATTR_NAME_MAX + 1];
	int type;			/* MOOB_T_* */
	int required;			/* rows carry it (the key always) */
};

struct mv_vtab {
	sqlite3_vtab base;
	char *domain;			/* domain directory, as given */
	int ncol;			/* attribute columns */
	int nrequired;			/* the key included; at most MOOB_QUERY_MAX_TERMS */
	struct mv_col col[MV_MAX_COLS];	/* col[1..ncol]; col[1] is the key */
};

/*
 * The plan chosen by xBestIndex. It travels to xFilter as idxStr, in a readable form that
 * EXPLAIN QUERY PLAN shows, e.g. "kind IN ?1; score >= ?2; ORDER BY created DESC; LIMIT ?3".
 */
struct mv_term {
	int col;
	int op;				/* MOOB_OP_* */
	int argv;			/* 1-based index into xFilter's argv; 0: none (EXISTS) */
	int in;				/* the argv value is an IN list (all at once) */
};

struct mv_plan {
	int nterm;
	struct mv_term term[MOOB_QUERY_MAX_TERMS];
	int order_col;			/* 0: none */
	int desc;
	int limit_argv;			/* 0: none */
};

struct mv_req {
	struct moob_query2 q;
	struct moob_query_result *res;
	int pos;			/* next result of the page */
	int done;			/* no page after this one */
};

struct mv_cursor {
	sqlite3_vtab_cursor base;
	int dirfd;
	struct mv_req *req;
	int nreq, cur;
	int eof;
	unsigned int page;
	/* the current row's attribute values, read on demand */
	sqlite3_value *val[MV_MAX_COLS];
	unsigned char have[MV_MAX_COLS];
};

static int mv_type(const char *s)
{
	if (!strcmp(s, "string")) return MOOB_T_STRING;
	if (!strcmp(s, "int64")) return MOOB_T_INT64;
	if (!strcmp(s, "double")) return MOOB_T_DOUBLE;
	if (!strcmp(s, "bool")) return MOOB_T_BOOL;
	if (!strcmp(s, "time")) return MOOB_T_TIME;
	if (!strcmp(s, "raw")) return MOOB_T_RAW;
	return -1;
}

static const char *mv_sqltype(int t)
{
	switch (t) {
	case MOOB_T_STRING: return "TEXT";
	case MOOB_T_DOUBLE: return "REAL";
	case MOOB_T_RAW: return "BLOB";
	default: return "INTEGER";
	}
}

/* Strips one level of SQL quotes from a module argument. */
static char *mv_unquote(const char *s)
{
	size_t n = strlen(s);
	char *out, *o;
	char q;

	if (n >= 2 && (s[0] == '\'' || s[0] == '"') && s[n - 1] == s[0]) {
		q = s[0];
		out = sqlite3_malloc64(n);
		if (!out)
			return NULL;
		for (o = out, s++, n -= 2; n; n--, s++) {
			*o++ = *s;
			if (*s == q && n > 1 && s[1] == q) {
				s++;
				n--;
			}
		}
		*o = 0;
		return out;
	}
	return sqlite3_mprintf("%s", s);
}

static int mv_connect(sqlite3 *db, void *aux, int argc, const char *const *argv,
		      sqlite3_vtab **out, char **err)
{
	struct mv_vtab *t;
	sqlite3_str *ddl;
	char *ddls;
	int i, rc;

	(void)aux;
	if (argc < 5) {
		*err = sqlite3_mprintf("moobfs: USING moobfs('<domain dir>', '<key attr> <type>', ...)");
		return SQLITE_ERROR;
	}
	if (argc - 4 >= MV_MAX_COLS) {
		*err = sqlite3_mprintf("moobfs: at most %d attribute columns", MV_MAX_COLS - 1);
		return SQLITE_ERROR;
	}
	t = sqlite3_malloc64(sizeof(*t));
	if (!t)
		return SQLITE_NOMEM;
	memset(t, 0, sizeof(*t));
	t->domain = mv_unquote(argv[3]);
	if (!t->domain) {
		sqlite3_free(t);
		return SQLITE_NOMEM;
	}
	ddl = sqlite3_str_new(db);
	sqlite3_str_appendall(ddl, "CREATE TABLE x(path TEXT");
	for (i = 4; i < argc; i++) {
		char *a = mv_unquote(argv[i]), *sp, *req;
		struct mv_col *c = &t->col[i - 3];
		int j;

		sp = a ? strchr(a, ' ') : NULL;
		req = sp ? strchr(sp + 1, ' ') : NULL;
		if (req) {
			if (strcmp(req, " required")) {
				*err = sqlite3_mprintf("moobfs: bad column '%s' (only 'required' may follow the type)",
						       argv[i]);
				sqlite3_free(a);
				goto fail;
			}
			*req = 0;
			c->required = 1;
		}
		if (!a || !sp || mv_type(sp + 1) < 0 || (size_t)(sp - a) > MOOB_ATTR_NAME_MAX ||
		    !moob_attr_name_ok(a, (size_t)(sp - a))) {
			*err = sqlite3_mprintf("moobfs: bad column '%s' (want '<attribute> <string|int64|double|bool|time|raw>')",
					       argv[i]);
			sqlite3_free(a);
			goto fail;
		}
		memcpy(c->name, a, (size_t)(sp - a));
		c->type = mv_type(sp + 1);
		for (j = 1; j < i - 3; j++)
			if (!strcmp(t->col[j].name, c->name)) {
				*err = sqlite3_mprintf("moobfs: column '%s' declared twice", c->name);
				sqlite3_free(a);
				goto fail;
			}
		if (i == 4)
			c->required = 1;
		if (c->required && c->type == MOOB_T_RAW) {
			*err = sqlite3_mprintf("moobfs: a key or required attribute cannot be raw (raw values are not indexed)");
			sqlite3_free(a);
			goto fail;
		}
		if (c->required && ++t->nrequired > (int)MOOB_QUERY_MAX_TERMS) {
			*err = sqlite3_mprintf("moobfs: at most %u required attributes, the key included",
					       MOOB_QUERY_MAX_TERMS);
			sqlite3_free(a);
			goto fail;
		}
		sqlite3_str_appendf(ddl, ", \"%w\" %s", c->name, mv_sqltype(c->type));
		sqlite3_free(a);
	}
	t->ncol = argc - 4;
	sqlite3_str_appendall(ddl, ")");
	ddls = sqlite3_str_finish(ddl);
	ddl = NULL;
	if (!ddls)
		goto nomem;
	rc = sqlite3_declare_vtab(db, ddls);
	sqlite3_free(ddls);
	if (rc != SQLITE_OK) {
		*err = sqlite3_mprintf("moobfs: %s", sqlite3_errmsg(db));
		goto fail;
	}
	*out = &t->base;
	return SQLITE_OK;
nomem:
	sqlite3_free(t->domain);
	sqlite3_free(t);
	return SQLITE_NOMEM;
fail:
	if (ddl)
		sqlite3_free(sqlite3_str_finish(ddl));
	sqlite3_free(t->domain);
	sqlite3_free(t);
	return SQLITE_ERROR;
}

static int mv_disconnect(sqlite3_vtab *vt)
{
	struct mv_vtab *t = (struct mv_vtab *)vt;

	sqlite3_free(t->domain);
	sqlite3_free(t);
	return SQLITE_OK;
}

/* ---- values: SQLite <-> the typed encoding --------------------------------------------- */

/*
 * The payload of value v for a column of type type, as the kernel compares it. 0 when v cannot be
 * represented exactly (then the term is not handed to the kernel, or the IN value is skipped and
 * SQLite decides on the rows).
 */
static int mv_encode(int type, sqlite3_value *v, unsigned char *out, unsigned short *len)
{
	int vt = sqlite3_value_type(v);

	switch (type) {
	case MOOB_T_STRING: {
		const unsigned char *s;
		int n;

		if (vt != SQLITE_TEXT)
			return 0;
		s = sqlite3_value_text(v);
		n = sqlite3_value_bytes(v);
		if (!s || n < 0 || (unsigned int)n > MOOB_QUERY_VALUE_MAX || !moob_utf8_valid(s, (size_t)n))
			return 0;
		memcpy(out, s, (size_t)n);
		*len = (unsigned short)n;
		return 1;
	}
	case MOOB_T_INT64:
	case MOOB_T_TIME:
		if (vt != SQLITE_INTEGER)
			return 0;
		moob_attr_put64(out, (ma_u64)sqlite3_value_int64(v));
		*len = 8;
		return 1;
	case MOOB_T_DOUBLE: {
		double d;
		ma_u64 bits;

		if (vt != SQLITE_FLOAT)
			return 0;
		d = sqlite3_value_double(v);
		if (isnan(d))
			return 0;
		memcpy(&bits, &d, 8);
		moob_attr_put64(out, bits);
		*len = 8;
		return 1;
	}
	case MOOB_T_BOOL: {
		sqlite3_int64 x;

		if (vt != SQLITE_INTEGER)
			return 0;
		x = sqlite3_value_int64(v);
		if (x != 0 && x != 1)
			return 0;
		out[0] = (unsigned char)x;
		*len = 1;
		return 1;
	}
	default:
		return 0;
	}
}

/* Sets the result to the typed value read from a file, or NULL. */
static void mv_decode(sqlite3_context *ctx, const unsigned char *v, ssize_t n)
{
	if (n < 1 || moob_attr_validate(v, (size_t)n) != MOOB_ATTR_OK) {
		sqlite3_result_null(ctx);
		return;
	}
	switch (v[0]) {
	case MOOB_T_STRING:
		sqlite3_result_text(ctx, (const char *)v + 1, (int)n - 1, SQLITE_TRANSIENT);
		return;
	case MOOB_T_INT64:
	case MOOB_T_TIME:
		sqlite3_result_int64(ctx, (sqlite3_int64)moob_attr_get64(v + 1));
		return;
	case MOOB_T_DOUBLE: {
		ma_u64 bits = moob_attr_get64(v + 1);
		double d;

		memcpy(&d, &bits, 8);
		sqlite3_result_double(ctx, d);
		return;
	}
	case MOOB_T_BOOL:
		sqlite3_result_int(ctx, v[1]);
		return;
	default:
		sqlite3_result_blob(ctx, v + 1, (int)n - 1, SQLITE_TRANSIENT);
	}
}

/* ---- planning ------------------------------------------------------------------------- */

static int mv_op(unsigned char op)
{
	switch (op) {
	case SQLITE_INDEX_CONSTRAINT_EQ: return MOOB_OP_EQ;
	case SQLITE_INDEX_CONSTRAINT_GT: return MOOB_OP_GT;
	case SQLITE_INDEX_CONSTRAINT_GE: return MOOB_OP_GE;
	case SQLITE_INDEX_CONSTRAINT_LT: return MOOB_OP_LT;
	case SQLITE_INDEX_CONSTRAINT_LE: return MOOB_OP_LE;
	case SQLITE_INDEX_CONSTRAINT_ISNOTNULL: return MOOB_OP_EXISTS;
	default: return 0;
	}
}

static const char *const mv_opname[] = {
	[MOOB_OP_EQ] = "=", [MOOB_OP_LT] = "<", [MOOB_OP_LE] = "<=", [MOOB_OP_GT] = ">",
	[MOOB_OP_GE] = ">=", [MOOB_OP_EXISTS] = "EXISTS",
};

static char *mv_plan_text(const struct mv_vtab *t, const struct mv_plan *p)
{
	sqlite3_str *s = sqlite3_str_new(NULL);
	int i;

	for (i = 0; i < p->nterm; i++) {
		const struct mv_term *tm = &p->term[i];

		if (i)
			sqlite3_str_appendall(s, "; ");
		if (tm->op == MOOB_OP_EXISTS)
			sqlite3_str_appendf(s, "%s EXISTS", t->col[tm->col].name);
		else
			sqlite3_str_appendf(s, "%s %s ?%d", t->col[tm->col].name,
					    tm->in ? "IN" : mv_opname[tm->op], tm->argv);
	}
	if (p->order_col)
		sqlite3_str_appendf(s, "; ORDER BY %s %s", t->col[p->order_col].name,
				    p->desc ? "DESC" : "ASC");
	if (p->limit_argv)
		sqlite3_str_appendf(s, "; LIMIT ?%d", p->limit_argv);
	return sqlite3_str_finish(s);
}

static int mv_col_of(const struct mv_vtab *t, const char *name, size_t n)
{
	int i;

	for (i = 1; i <= t->ncol; i++)
		if (strlen(t->col[i].name) == n && !memcmp(t->col[i].name, name, n))
			return i;
	return 0;
}

/* Reads back what mv_plan_text wrote; 0 on anything unexpected. */
static int mv_plan_parse(const struct mv_vtab *t, const char *s, struct mv_plan *p)
{
	memset(p, 0, sizeof(*p));
	while (s && *s) {
		const char *end = strstr(s, "; ");
		size_t len = end ? (size_t)(end - s) : strlen(s);
		char clause[160], word[16], *sp;
		int arg = 0, col, k;

		if (len >= sizeof(clause))
			return 0;
		memcpy(clause, s, len);
		clause[len] = 0;
		sp = strchr(clause, ' ');
		if (!sp)
			return 0;
		if (sscanf(clause, "LIMIT ?%d", &arg) == 1) {
			p->limit_argv = arg;
		} else if (!strncmp(clause, "ORDER BY ", 9)) {
			char *dir = strrchr(clause, ' ');

			col = mv_col_of(t, clause + 9, (size_t)(dir - clause - 9));
			if (!col)
				return 0;
			p->order_col = col;
			p->desc = !strcmp(dir + 1, "DESC");
		} else {
			struct mv_term *tm;

			col = mv_col_of(t, clause, (size_t)(sp - clause));
			if (!col || p->nterm == (int)MOOB_QUERY_MAX_TERMS ||
			    sscanf(sp + 1, "%15s ?%d", word, &arg) < 1)
				return 0;
			tm = &p->term[p->nterm++];
			tm->col = col;
			tm->argv = arg;
			if (!strcmp(word, "IN")) {
				tm->op = MOOB_OP_EQ;
				tm->in = 1;
			} else {
				for (k = 1; k <= MOOB_OP_EXISTS; k++)
					if (mv_opname[k] && !strcmp(word, mv_opname[k]))
						tm->op = k;
				if (!tm->op)
					return 0;
			}
		}
		s = end ? end + 2 : NULL;
	}
	return 1;
}

static int mv_best_index(sqlite3_vtab *vt, sqlite3_index_info *ix)
{
	struct mv_vtab *t = (struct mv_vtab *)vt;
	struct mv_plan plan;
	int i, pass, argv = 0, in_used = 0, covered[MV_MAX_COLS] = { 0 }, nonnull[MV_MAX_COLS] = { 0 };
	int uncovered = t->nrequired;	/* required columns still needing their EXISTS */
	double cost = 1e6, rows = 1e6;

	memset(&plan, 0, sizeof(plan));
	/* equalities first (the kernel intersects them), then ranges and IS NOT NULL */
	for (pass = 0; pass < 2; pass++) {
		for (i = 0; i < ix->nConstraint; i++) {
			const struct sqlite3_index_constraint *c = &ix->aConstraint[i];
			struct mv_term *tm;
			int op = mv_op(c->op), is_in = 0;

			if (!c->usable || c->iColumn < 1 || !op || ix->aConstraintUsage[i].argvIndex)
				continue;
			if ((pass == 0) != (op == MOOB_OP_EQ))
				continue;
			if (t->col[c->iColumn].type == MOOB_T_RAW)
				continue;
			if (t->col[c->iColumn].type == MOOB_T_BOOL && op != MOOB_OP_EQ && op != MOOB_OP_EXISTS)
				continue;
			/*
			 * The kernel compares bytes, which is SQLite's BINARY collation. Under another one
			 * (kind = 'Note' COLLATE NOCASE) it would leave out rows SQLite counts as equal,
			 * and checking again on each row cannot bring them back, so the condition stays
			 * SQLite's alone (journal 0120). ORDER BY needs no such test: SQLite does not
			 * offer a vtab an ORDER BY term whose collation differs from the column's.
			 */
			if (op != MOOB_OP_EXISTS && sqlite3_stricmp(sqlite3_vtab_collation(ix, i), "BINARY"))
				continue;
			/* the EXISTS of required columns not covered keep their slots */
			if (plan.nterm + uncovered -
			    (t->col[c->iColumn].required && !covered[c->iColumn]) >= (int)MOOB_QUERY_MAX_TERMS)
				continue;
			if (op == MOOB_OP_EQ && sqlite3_vtab_in(ix, i, -1)) {
				if (in_used)
					continue;	/* one list at once; SQLite iterates any other */
				sqlite3_vtab_in(ix, i, 1);
				in_used = 1;
				is_in = 1;
			}
			tm = &plan.term[plan.nterm++];
			tm->col = c->iColumn;
			tm->op = op;
			tm->in = is_in;
			if (op != MOOB_OP_EXISTS) {
				tm->argv = ++argv;
				ix->aConstraintUsage[i].argvIndex = argv;
			} else {
				/* IS NOT NULL has no right-hand side; it is still consumed by the kernel */
				ix->aConstraintUsage[i].argvIndex = 0;
			}
			/*
			 * Only IS NOT NULL is declared handled. Every other condition is checked again
			 * by SQLite on each row: the value seen here (sqlite3_vtab_rhs_value) need not be
			 * the one xFilter receives (an older SQLite hands 5 here and 5.0 there for an
			 * INTEGER column), and xFilter drops a condition it cannot represent exactly, so a
			 * condition declared handled here could reach nobody (journal 0112). An equality's
			 * column is answered from the request (mv_column), so its check costs no read.
			 */
			if (op == MOOB_OP_EXISTS)
				ix->aConstraintUsage[i].omit = 1;
			if (t->col[c->iColumn].required && !covered[c->iColumn])
				uncovered--;
			covered[c->iColumn] = 1;
			nonnull[c->iColumn] = 1;
			cost /= op == MOOB_OP_EQ ? (is_in ? 4.0 : 20.0) : 3.0;
			rows /= op == MOOB_OP_EQ ? (is_in ? 4.0 : 20.0) : 3.0;
		}
	}
	/* rows carry the key and every required attribute: the kernel tests the EXISTS not covered */
	for (i = 1; i <= t->ncol; i++) {
		if (!t->col[i].required)
			continue;
		nonnull[i] = 1;
		if (!covered[i]) {
			struct mv_term *tm = &plan.term[plan.nterm++];

			tm->col = i;
			tm->op = MOOB_OP_EXISTS;
		}
	}
	/*
	 * The kernel orders by walking the order attribute's entries, so a file without it would be
	 * missing; the order is taken only when the column cannot be NULL. An IN list is answered by
	 * several requests, one after the other, so its order is SQLite's to make.
	 */
	if (ix->nOrderBy == 1 && !in_used) {
		int oc = ix->aOrderBy[0].iColumn;

		if (oc >= 1 && nonnull[oc] && t->col[oc].type != MOOB_T_BOOL &&
		    t->col[oc].type != MOOB_T_RAW) {
			plan.order_col = oc;
			plan.desc = ix->aOrderBy[0].desc;
			ix->orderByConsumed = 1;
		}
	}
	/* LIMIT: only a page-size hint; SQLite still applies it */
	for (i = 0; i < ix->nConstraint; i++)
		if (ix->aConstraint[i].usable && ix->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_LIMIT &&
		    !ix->aConstraintUsage[i].argvIndex) {
			plan.limit_argv = ++argv;
			ix->aConstraintUsage[i].argvIndex = argv;
			break;
		}
	ix->estimatedCost = cost;
	ix->estimatedRows = (sqlite3_int64)(rows < 1 ? 1 : rows);
	ix->idxStr = mv_plan_text(t, &plan);
	if (!ix->idxStr)
		return SQLITE_NOMEM;
	ix->needToFreeIdxStr = 1;
	return SQLITE_OK;
}

/* ---- running ---------------------------------------------------------------------------- */

static void mv_row_reset(struct mv_cursor *c)
{
	int i;

	for (i = 0; i < MV_MAX_COLS; i++)
		if (c->have[i]) {
			sqlite3_value_free(c->val[i]);
			c->val[i] = NULL;
			c->have[i] = 0;
		}
}

static void mv_reqs_free(struct mv_cursor *c)
{
	int i;

	for (i = 0; i < c->nreq; i++)
		sqlite3_free(c->req[i].res);
	sqlite3_free(c->req);
	c->req = NULL;
	c->nreq = 0;
}

static int mv_open(sqlite3_vtab *vt, sqlite3_vtab_cursor **out)
{
	struct mv_cursor *c = sqlite3_malloc64(sizeof(*c));

	(void)vt;
	if (!c)
		return SQLITE_NOMEM;
	memset(c, 0, sizeof(*c));
	c->dirfd = -1;
	*out = &c->base;
	return SQLITE_OK;
}

static int mv_close(sqlite3_vtab_cursor *cur)
{
	struct mv_cursor *c = (struct mv_cursor *)cur;

	mv_row_reset(c);
	mv_reqs_free(c);
	if (c->dirfd >= 0)
		close(c->dirfd);
	sqlite3_free(c);
	return SQLITE_OK;
}

static int mv_fail(struct mv_cursor *c, const char *what, int e)
{
	sqlite3_vtab *vt = c->base.pVtab;

	sqlite3_free(vt->zErrMsg);
	vt->zErrMsg = sqlite3_mprintf("moobfs: %s: %s", what, strerror(e));
	return SQLITE_ERROR;
}

/* The next page of request r. */
static int mv_fetch(struct mv_cursor *c, struct mv_req *r)
{
	int tries;

	for (tries = 0; tries < MV_TRIES; tries++) {
		if (!ioctl(c->dirfd, MOOB_IOC_QUERY2, &r->q))
			break;
		if (errno != EAGAIN)
			return mv_fail(c, "QUERY2", errno);
	}
	if (tries == MV_TRIES)
		return mv_fail(c, "QUERY2", EAGAIN);
	if (r->q.count > r->q.limit)
		return mv_fail(c, "QUERY2", EPROTO);
	r->pos = 0;
	r->done = !r->q.truncated;
	return SQLITE_OK;
}

/* First pages of every request, eight per QUERY2_BATCH. */
static int mv_fetch_first(struct mv_cursor *c)
{
	struct moob_query2 *qs;
	int i, j, n, tries;

	qs = sqlite3_malloc64(sizeof(*qs) * MOOB_QUERY2_BATCH_MAX);
	if (!qs)
		return SQLITE_NOMEM;
	for (i = 0; i < c->nreq; i += n) {
		struct moob_query2_batch b;

		n = c->nreq - i < (int)MOOB_QUERY2_BATCH_MAX ? c->nreq - i : (int)MOOB_QUERY2_BATCH_MAX;
		for (j = 0; j < n; j++)
			qs[j] = c->req[i + j].q;
		memset(&b, 0, sizeof(b));
		b.version = MOOB_QUERY2_VERSION;
		b.nqueries = (unsigned int)n;
		b.queries = (uint64_t)(uintptr_t)qs;
		for (tries = 0; tries < MV_TRIES; tries++) {
			if (!ioctl(c->dirfd, MOOB_IOC_QUERY2_BATCH, &b))
				break;
			if (errno != EAGAIN) {
				int e = errno;

				sqlite3_free(qs);
				return mv_fail(c, "QUERY2_BATCH", e);
			}
		}
		if (tries == MV_TRIES) {
			sqlite3_free(qs);
			return mv_fail(c, "QUERY2_BATCH", EAGAIN);
		}
		for (j = 0; j < n; j++) {
			if (qs[j].count > qs[j].limit) {
				sqlite3_free(qs);
				return mv_fail(c, "QUERY2_BATCH", EPROTO);
			}
			c->req[i + j].q = qs[j];
			c->req[i + j].pos = 0;
			c->req[i + j].done = !qs[j].truncated;
		}
	}
	sqlite3_free(qs);
	return SQLITE_OK;
}

/* Skips to the first result of a request that has one, fetching further pages; sets eof. */
static int mv_settle(struct mv_cursor *c)
{
	while (c->cur < c->nreq) {
		struct mv_req *r = &c->req[c->cur];
		int rc;

		if ((unsigned int)r->pos < r->q.count)
			return SQLITE_OK;
		if (r->done) {
			c->cur++;
			continue;
		}
		rc = mv_fetch(c, r);
		if (rc != SQLITE_OK)
			return rc;
	}
	c->eof = 1;
	return SQLITE_OK;
}

static void mv_term_fill(struct moob_query_term *qt, const struct mv_col *col, int op)
{
	memset(qt, 0, sizeof(*qt));
	memcpy(qt->name, col->name, strlen(col->name));
	qt->op = (unsigned char)op;
	if (op != MOOB_OP_EXISTS)
		qt->type = (unsigned char)col->type;
}

static int mv_filter(sqlite3_vtab_cursor *cur, int idxNum, const char *idxStr, int argc,
		     sqlite3_value **argv)
{
	struct mv_cursor *c = (struct mv_cursor *)cur;
	struct mv_vtab *t = (struct mv_vtab *)cur->pVtab;
	struct mv_plan plan;
	struct moob_query2 base;
	sqlite3_value *inlist = NULL, *v;
	int i, nt = 0, in_term = -1, rc;

	(void)idxNum;
	(void)argc;
	mv_row_reset(c);
	mv_reqs_free(c);
	c->cur = 0;
	c->eof = 0;
	if (!mv_plan_parse(t, idxStr, &plan))
		return mv_fail(c, "internal plan", EINVAL);
	if (c->dirfd < 0) {
		c->dirfd = open(t->domain, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
		if (c->dirfd < 0)
			return mv_fail(c, t->domain, errno);
	}
	c->page = MV_PAGE;
	if (plan.limit_argv) {
		sqlite3_int64 l = sqlite3_value_int64(argv[plan.limit_argv - 1]);

		if (l > 0)
			c->page = l > (sqlite3_int64)MOOB_QUERY_MAX_LIMIT ? MOOB_QUERY_MAX_LIMIT : (unsigned int)l;
	}
	/* the common part of every request */
	memset(&base, 0, sizeof(base));
	base.version = MOOB_QUERY2_VERSION;
	base.limit = c->page;
	for (i = 0; i < plan.nterm; i++) {
		const struct mv_term *tm = &plan.term[i];
		struct moob_query_term *qt = &base.terms[nt];

		if (tm->in) {
			in_term = nt;
			inlist = argv[tm->argv - 1];
			mv_term_fill(qt, &t->col[tm->col], MOOB_OP_EQ);
			nt++;
			continue;
		}
		mv_term_fill(qt, &t->col[tm->col], tm->op);
		if (tm->op != MOOB_OP_EXISTS &&
		    !mv_encode(t->col[tm->col].type, argv[tm->argv - 1], qt->value, &qt->len)) {
			/* not exactly representable: left to SQLite, which checks this term on each row */
			continue;
		}
		nt++;
	}
	base.nterms = (unsigned int)nt;
	if (plan.order_col) {
		const struct mv_col *oc = &t->col[plan.order_col];

		memcpy(base.order_by, oc->name, strlen(oc->name));
		base.order = plan.desc ? MOOB_ORDER_DESC : MOOB_ORDER_ASC;
		base.order_type = (unsigned char)oc->type;
	}
	if (in_term < 0) {
		c->req = sqlite3_malloc64(sizeof(*c->req));
		if (!c->req)
			return SQLITE_NOMEM;
		c->nreq = 1;
		c->req[0].q = base;
	} else {
		int n = 0;

		for (rc = sqlite3_vtab_in_first(inlist, &v); rc == SQLITE_OK && v;
		     rc = sqlite3_vtab_in_next(inlist, &v))
			n++;
		if (rc != SQLITE_OK && rc != SQLITE_DONE)
			return rc;
		c->req = sqlite3_malloc64(sizeof(*c->req) * (n ? (sqlite3_uint64)n : 1));
		if (!c->req)
			return SQLITE_NOMEM;
		memset(c->req, 0, sizeof(*c->req) * (n ? (size_t)n : 1));
		n = 0;
		for (rc = sqlite3_vtab_in_first(inlist, &v); rc == SQLITE_OK && v;
		     rc = sqlite3_vtab_in_next(inlist, &v)) {
			struct moob_query2 *q = &c->req[n].q;
			int col = plan.term[0].col, k;

			for (k = 0; k < plan.nterm; k++)
				if (plan.term[k].in)
					col = plan.term[k].col;
			*q = base;
			/* a value that cannot equal any stored one of this type is skipped */
			if (!mv_encode(t->col[col].type, v, q->terms[in_term].value, &q->terms[in_term].len))
				continue;
			n++;
		}
		if (rc != SQLITE_OK && rc != SQLITE_DONE)
			return rc;
		c->nreq = n;
	}
	for (i = 0; i < c->nreq; i++) {
		c->req[i].res = sqlite3_malloc64(sizeof(struct moob_query_result) * c->page);
		if (!c->req[i].res)
			return SQLITE_NOMEM;
		c->req[i].q.results = (uint64_t)(uintptr_t)c->req[i].res;
	}
	if (!c->nreq) {
		c->eof = 1;
		return SQLITE_OK;
	}
	rc = c->nreq > 1 ? mv_fetch_first(c) : mv_fetch(c, &c->req[0]);
	if (rc != SQLITE_OK)
		return rc;
	return mv_settle(c);
}

static int mv_next(sqlite3_vtab_cursor *cur)
{
	struct mv_cursor *c = (struct mv_cursor *)cur;

	mv_row_reset(c);
	c->req[c->cur].pos++;
	return mv_settle(c);
}

static int mv_eof(sqlite3_vtab_cursor *cur)
{
	return ((struct mv_cursor *)cur)->eof;
}

static const struct moob_query_result *mv_row(struct mv_cursor *c)
{
	struct mv_req *r = &c->req[c->cur];

	return &r->res[r->pos];
}

static int mv_column(sqlite3_vtab_cursor *cur, sqlite3_context *ctx, int col)
{
	struct mv_cursor *c = (struct mv_cursor *)cur;
	struct mv_vtab *t = (struct mv_vtab *)cur->pVtab;
	const struct moob_query_result *res = mv_row(c);
	int known = !(res->flags & (MOOB_RES_PATH_UNKNOWN | MOOB_RES_PATH_TRUNCATED)) &&
		    res->path_len < MOOB_QUERY_PATH_MAX && !res->path[res->path_len];
	unsigned char buf[MOOB_ATTR_VALUE_MAX];
	char name[MOOB_ATTR_PREFIX_LEN + MOOB_ATTR_NAME_MAX + 1];
	ssize_t n;
	int fd;

	if (col == MV_PATH) {
		if (known)
			sqlite3_result_text(ctx, res->path, (int)res->path_len, SQLITE_TRANSIENT);
		else
			sqlite3_result_null(ctx);
		return SQLITE_OK;
	}
	if (c->have[col]) {
		sqlite3_result_value(ctx, c->val[col]);
		return SQLITE_OK;
	}
	/*
	 * A column under an equality the kernel matched holds exactly that value, of that type (for an
	 * IN list, each request carries one value): no need to read the file. This is what lets SQLite
	 * check an IN list again on every row without a getxattr per row.
	 */
	{
		const struct moob_query2 *q = &c->req[c->cur].q;
		unsigned int k;

		for (k = 0; k < q->nterms; k++) {
			const struct moob_query_term *qt = &q->terms[k];

			if (qt->op == MOOB_OP_EQ && !strcmp(qt->name, t->col[col].name) &&
			    qt->type == t->col[col].type) {
				buf[0] = qt->type;
				memcpy(buf + 1, qt->value, qt->len);
				mv_decode(ctx, buf, (ssize_t)qt->len + 1);
				return SQLITE_OK;
			}
		}
	}
	if (!known) {
		sqlite3_result_null(ctx);
		return SQLITE_OK;
	}
	/* read as the caller, through the path the kernel just returned */
	snprintf(name, sizeof(name), "%s%s", MOOB_ATTR_PREFIX, t->col[col].name);
	fd = openat(c->dirfd, res->path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) {
		n = -1;
	} else {
		n = fgetxattr(fd, name, buf, sizeof(buf));
		close(fd);
	}
	if (n < 0)
		sqlite3_result_null(ctx);	/* gone, or no such attribute */
	else
		mv_decode(ctx, buf, n);
	return SQLITE_OK;
}

static int mv_rowid(sqlite3_vtab_cursor *cur, sqlite3_int64 *rowid)
{
	*rowid = (sqlite3_int64)mv_row((struct mv_cursor *)cur)->ino;
	return SQLITE_OK;
}

static sqlite3_module mv_module = {
	.iVersion = 0,
	.xCreate = mv_connect,
	.xConnect = mv_connect,
	.xBestIndex = mv_best_index,
	.xDisconnect = mv_disconnect,
	.xDestroy = mv_disconnect,
	.xOpen = mv_open,
	.xClose = mv_close,
	.xFilter = mv_filter,
	.xNext = mv_next,
	.xEof = mv_eof,
	.xColumn = mv_column,
	.xRowid = mv_rowid,
};

#ifdef _WIN32
__declspec(dllexport)
#endif
int sqlite3_moobfs_init(sqlite3 *db, char **err, const sqlite3_api_routines *api)
{
	SQLITE_EXTENSION_INIT2(api);
	/* sqlite3_vtab_in and LIMIT constraints arrived in 3.38.0 */
	if (sqlite3_libversion_number() < 3038000) {
		if (err)
			*err = sqlite3_mprintf("moobfs: needs SQLite 3.38.0 or later (this is %s)",
					       sqlite3_libversion());
		return SQLITE_ERROR;
	}
	return sqlite3_create_module(db, "moobfs", &mv_module, NULL);
}

/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * libFuzzer target for two parsers of the user-space tools that take untrusted input (security
 * roadmap S7, journal 0237). Build with
 *   clang -fsanitize=fuzzer,address,undefined -Iinclude tests/fuzz_tools.c
 * Byte 0 selects the parser, the rest is its input:
 *   even: parse_terms() of tools/query-common.h, the query terms of moobfs, moobfs-index and
 *         tests/query.c; the input is split at NUL bytes into argv words.
 *   odd:  moob_ext_acl_bytes() of tools/ext-acl.h, the decision on a system.posix_acl_access
 *         value read from the lower tree (journal 0232 moved the read out of it); byte 1 picks
 *         an errno of the read or the value, bytes 2-3 the mode, the rest is the value.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>

#include "../tools/ext-acl.h"
#include "../tools/query-common.h"

#define MAX_WORDS 64

static void fuzz_terms(const uint8_t *p, size_t n)
{
	struct moob_query_term terms[MOOB_QUERY_MAX_TERMS];
	char *argv[MAX_WORDS];
	int argc = 0, k;
	size_t start = 0, i;

	for (i = 0; i <= n && argc < MAX_WORDS; i++) {
		if (i == n || !p[i]) {
			/* each word in its own allocation, so ASan sees a read past its NUL */
			argv[argc] = malloc(i - start + 1);
			if (!argv[argc])
				break;
			memcpy(argv[argc], p + start, i - start);
			argv[argc][i - start] = 0;
			argc++;
			start = i + 1;
		}
	}
	k = parse_terms(argc, argv, 0, terms);
	if (k > (int)MOOB_QUERY_MAX_TERMS)
		abort();
	for (i = 0; k > 0 && i < (size_t)k; i++)
		if (terms[i].len > MOOB_QUERY_VALUE_MAX || terms[i].name[MOOB_QUERY_NAME_MAX])
			abort();
	if (argc) {
		(void)parse_type(argv[0]);
		(void)parse_op(argv[0]);
	}
	while (argc--)
		free(argv[argc]);
}

static void fuzz_acl(const uint8_t *p, size_t n)
{
	static const int errs[] = { ENODATA, EOPNOTSUPP, EACCES, EIO };
	mode_t mode;

	if (n < 3)
		return;
	mode = (mode_t)((p[1] | p[2] << 8) & 07777);
	if (p[0] < 4)
		(void)moob_ext_acl_bytes(NULL, -1, errs[p[0]], mode);
	else
		(void)moob_ext_acl_bytes(p + 3, (ssize_t)(n - 3), 0, mode);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	if (!size)
		return 0;
	if (data[0] & 1)
		fuzz_acl(data + 1, size - 1);
	else
		fuzz_terms(data + 1, size - 1);
	return 0;
}

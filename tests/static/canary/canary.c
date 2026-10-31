// SPDX-License-Identifier: GPL-2.0-only
/*
 * Static-analysis canary (security roadmap S5 and S19, journal 0242). Never loaded and never
 * part of the layer: each function holds one defect that one checker of
 * scripts/static-analysis.sh must report. The run fails when a checker reports none of the
 * entries of tests/static/canary-expected.txt, so a checker that stopped working (a missing
 * package, a changed command line, an output format the triage no longer parses) cannot pass
 * by reporting nothing.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

/* W=1 (-Wunused-but-set-variable) */
int canary_w1(int a)
{
	int unused;

	unused = a;
	return 0;
}

/* sparse: a __user pointer dereferenced directly */
int canary_sparse(int __user *p)
{
	return *p;
}

/* smatch, and clang's -Warray-bounds: a write one past the end of an array */
int canary_smatch(int i)
{
	char buf[8];

	buf[8] = (char)i;
	return buf[0];
}

/* Coccinelle (misc/array_size.cocci): ARRAY_SIZE() written out by hand */
int canary_cocci(void)
{
	static const int table[] = { 1, 2, 3 };

	return sizeof(table) / sizeof(table[0]);
}

/* clang analyzer (unix.Malloc): use after kfree */
int canary_clang(size_t n)
{
	int *p = kmalloc(n, GFP_KERNEL);

	if (!p)
		return -ENOMEM;
	kfree(p);
	return *p;
}

/**
 * canary_kdoc() - kernel-doc: the parameter is not described
 *
 * Return: zero.
 */
int canary_kdoc(int undocumented)
{
	return 0;
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("static-analysis canary, never loaded");

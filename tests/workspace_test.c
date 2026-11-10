/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Unit tests of tools/workspace.h (ADR-0035, journal 0273): name and agent-list validation, the
 * POSIX ACL xattr encoding and its mask, the plan of workspace create, the option parser, the
 * mount data string, the supported-kernels list and the test of the lower's ancestors. No system
 * call; run by make test on macOS and Linux.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../tools/workspace.h"

static int failures;

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "workspace_test:%d: %s\n", __LINE__, #c); \
	failures++; } } while (0)

static void names(void)
{
	static const char *const good[] = { "a", "_", "proj", "a1", "a-b_c", "_x9",
		"abcdefghijklmnopqrstuvwxyz012345" };	/* 32 bytes */
	static const char *const bad[] = { "", "A", "1a", "-a", "a.b", "a b", "a/b", "é",
		"abcdefghijklmnopqrstuvwxyz0123456", "a$" };
	struct ws_agents ag;
	const char *why = NULL;
	char group[WS_GROUP_MAX + 1];
	size_t i;

	for (i = 0; i < sizeof good / sizeof good[0]; i++)
		CHECK(ws_name_ok(good[i]));
	for (i = 0; i < sizeof bad / sizeof bad[0]; i++)
		CHECK(!ws_name_ok(bad[i]));
	CHECK(ws_ns_ok("abcdefghijklmnopqrstuvwxyz0"));		/* 27 bytes */
	CHECK(!ws_ns_ok("abcdefghijklmnopqrstuvwxyz01"));	/* 28: moob-<ns> > 32 */
	ws_group_of("proj", group, sizeof group);
	CHECK(!strcmp(group, "moob-proj"));

	CHECK(!ws_parse_agents("a1,a2", &ag, &why) && ag.n == 2 && !strcmp(ag.name[0], "a1") &&
	      !strcmp(ag.name[1], "a2"));
	CHECK(!ws_parse_agents("solo", &ag, &why) && ag.n == 1);
	CHECK(ws_parse_agents("", &ag, &why) && strstr(why, "empty"));
	CHECK(ws_parse_agents("a1,", &ag, &why) && strstr(why, "empty"));
	CHECK(ws_parse_agents("a1,,a2", &ag, &why) && strstr(why, "empty"));
	CHECK(ws_parse_agents("a1,shared", &ag, &why) && strstr(why, "shared"));
	CHECK(ws_parse_agents("a1,a1", &ag, &why) && strstr(why, "twice"));
	CHECK(ws_parse_agents("a1,B", &ag, &why) && strstr(why, "[a-z_]"));
	CHECK(ws_parse_agents("abcdefghijklmnopqrstuvwxyz0123456", &ag, &why) &&
	      strstr(why, "longer"));
	{
		char list[64 * 4 + 8];
		size_t off = 0;
		int k;

		for (k = 0; k < 65; k++) {
			int w = snprintf(list + off, sizeof list - off, "%sa%d", k ? "," : "", k);

			/* the 65 names take 249 bytes; stop rather than step past the buffer */
			if (w < 0 || (size_t)w >= sizeof list - off) {
				CHECK(w >= 0 && (size_t)w < sizeof list - off);
				break;
			}
			off += (size_t)w;
		}
		CHECK(ws_parse_agents(list, &ag, &why) && strstr(why, "64"));
		list[strlen(list) - 4] = 0;	/* drop ",a64": 64 agents */
		CHECK(!ws_parse_agents(list, &ag, &why) && ag.n == 64);
	}
}

/* The bytes setfacl writes for u::rwx,u:1001:r-x,g::rwx,m::rwx,o::--- (version 2, LE). */
static const unsigned char named_bytes[] = {
	2, 0, 0, 0,
	0x01, 0, 7, 0, 0xff, 0xff, 0xff, 0xff,
	0x02, 0, 5, 0, 0xe9, 0x03, 0, 0,
	0x04, 0, 7, 0, 0xff, 0xff, 0xff, 0xff,
	0x10, 0, 7, 0, 0xff, 0xff, 0xff, 0xff,
	0x20, 0, 0, 0, 0xff, 0xff, 0xff, 0xff,
};

static void acl_codec(void)
{
	struct ws_acl a;
	unsigned char buf[WS_ACL_BYTES];
	char text[256];
	long n;

	/* the default ACL of shared: three base entries, no mask, others r-x */
	ws_acl_from_mode(&a, WS_SHARED_DEFAULT);
	n = ws_acl_encode(&a, buf, sizeof buf);
	CHECK(n == 28);
	CHECK(buf[0] == 2 && buf[4] == 0x01 && buf[6] == 7 && buf[12] == 0x04 && buf[14] == 7 &&
	      buf[20] == 0x20 && buf[22] == 5 && buf[8] == 0xff && buf[27] == 0xff);
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,g::rwx,o::r-x") && !strcmp(text, WS_SHARED_DEFAULT_TEXT));
	CHECK(ws_acl_mode(&a) == 0775);
	ws_acl_from_mode(&a, 0770);
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,g::rwx,o::---") && ws_acl_mode(&a) == 0770);

	/* decode what the kernel returns, and encode it back byte for byte */
	CHECK(!ws_acl_decode(named_bytes, sizeof named_bytes, &a) && a.n == 5);
	CHECK(a.e[1].tag == WS_ACL_USER && a.e[1].id == 1001 && a.e[1].perm == 5);
	n = ws_acl_encode(&a, buf, sizeof buf);
	CHECK(n == (long)sizeof named_bytes && !memcmp(buf, named_bytes, sizeof named_bytes));
	CHECK(ws_acl_encode(&a, buf, 20) == -1);	/* too short */

	/* invalid inputs */
	CHECK(ws_acl_decode(named_bytes, 3, &a) == -1);
	CHECK(ws_acl_decode(named_bytes, sizeof named_bytes - 1, &a) == -1);
	memcpy(buf, named_bytes, sizeof named_bytes);
	buf[0] = 1;					/* version 1 */
	CHECK(ws_acl_decode(buf, sizeof named_bytes, &a) == -1);
	memcpy(buf, named_bytes, sizeof named_bytes);
	buf[28] = 0x40;					/* unknown tag in place of the mask */
	CHECK(ws_acl_decode(buf, sizeof named_bytes, &a) == -1);
	memcpy(buf, named_bytes, sizeof named_bytes);
	buf[28] = 0x20;					/* no mask with a named entry: two others */
	CHECK(ws_acl_decode(buf, sizeof named_bytes, &a) == -1);
	memcpy(buf, named_bytes, sizeof named_bytes);
	buf[14] = 8;					/* a permission beyond rwx */
	CHECK(ws_acl_decode(buf, sizeof named_bytes, &a) == -1);
	/* out of order: the named user after the owning group */
	memcpy(buf, named_bytes, 4);
	memcpy(buf + 4, named_bytes + 4, 8);
	memcpy(buf + 12, named_bytes + 20, 8);
	memcpy(buf + 20, named_bytes + 12, 8);
	memcpy(buf + 28, named_bytes + 28, 16);
	CHECK(ws_acl_decode(buf, sizeof named_bytes, &a) == -1);
	/* the minimal ACL without its other entry */
	ws_acl_from_mode(&a, 0750);
	a.n = 2;
	CHECK(ws_acl_encode(&a, buf, sizeof buf) == -1);
}

static void acl_mask(void)
{
	struct ws_acl a;
	char text[256];

	/* grant ro on <ns> (0750): the mask is the union, r-x, and the mode stays 0750 */
	ws_acl_from_mode(&a, 0750);
	CHECK(!ws_acl_set_user(&a, 1001, 5));
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,u:1001:r-x,g::r-x,m::r-x,o::---"));
	CHECK(ws_acl_mode(&a) == 0750);

	/* on shared (2770): mask rwx */
	ws_acl_from_mode(&a, 0770);
	CHECK(!ws_acl_set_user(&a, 1001, 5));
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,u:1001:r-x,g::rwx,m::rwx,o::---"));

	/* a group of owners with no permission: the named user alone sets the mask */
	ws_acl_from_mode(&a, 0700);
	CHECK(!ws_acl_set_user(&a, 1002, 5));
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,u:1002:r-x,g::---,m::r-x,o::---"));
	CHECK(ws_acl_mode(&a) == 0750);

	/* a second user sorts by id; setting one again replaces it */
	ws_acl_from_mode(&a, 0750);
	CHECK(!ws_acl_set_user(&a, 1005, 4));
	CHECK(!ws_acl_set_user(&a, 1003, 5));
	CHECK(!ws_acl_set_user(&a, 1005, 7));
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,u:1003:r-x,u:1005:rwx,g::r-x,m::rwx,o::---"));
	CHECK(ws_acl_valid(&a));

	/* revoke one: the mask shrinks; revoke the last: the ACL is minimal, mode from g:: */
	CHECK(ws_acl_del_user(&a, 1005) == 1);
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,u:1003:r-x,g::r-x,m::r-x,o::---"));
	CHECK(ws_acl_del_user(&a, 1005) == 0);
	CHECK(ws_acl_del_user(&a, 1003) == 1);
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,g::r-x,o::---") && !ws_acl_named(&a));
	CHECK(ws_acl_mode(&a) == 0750);

	/* a named group is kept and keeps the mask */
	ws_acl_from_mode(&a, 0750);
	a.e[a.n++] = (struct ws_acl_entry){ WS_ACL_GROUP, 7, 2000 };
	ws_acl_sort(&a);
	CHECK(!ws_acl_valid(&a));			/* no mask yet */
	CHECK(!ws_acl_fix_mask(&a) && ws_acl_valid(&a));
	CHECK(!ws_acl_set_user(&a, 1001, 5));
	CHECK(ws_acl_del_user(&a, 1001) == 1);
	ws_acl_text(&a, text, sizeof text);
	CHECK(!strcmp(text, "u::rwx,g::r-x,g:2000:rwx,m::rwx,o::---"));

	/* no room */
	ws_acl_from_mode(&a, 0750);
	while (a.n < WS_ACL_MAX - 1)
		a.e[a.n++] = (struct ws_acl_entry){ WS_ACL_USER, 4, (unsigned int)(3000 + a.n) };
	ws_acl_sort(&a);
	CHECK(ws_acl_set_user(&a, 1, 5) == -1);
}

static void plan(void)
{
	struct ws_agents ag;
	struct ws_create_in in;
	static struct ws_plan p;
	const char *why;
	char text[256], *argv[9];

	CHECK(!ws_parse_agents("a1,a2", &ag, &why));
	memset(&in, 0, sizeof in);
	in.ns = "proj";
	in.agents = &ag;
	in.make_group = 1;
	in.make_user[0] = in.make_user[1] = 1;
	snprintf(in.agent_group[0], sizeof in.agent_group[0], "a1");
	snprintf(in.agent_group[1], sizeof in.agent_group[1], "a2");
	ws_plan_create(&in, &p);
	CHECK(p.n == 9);
	ws_step_text(&p.s[0], text, sizeof text);
	CHECK(!strcmp(text, "groupadd --system moob-proj"));
	ws_step_text(&p.s[1], text, sizeof text);
	CHECK(!strcmp(text, "useradd --system --user-group --no-create-home --shell "
		      "/usr/sbin/nologin a1"));
	ws_step_text(&p.s[3], text, sizeof text);
	CHECK(!strcmp(text, "gpasswd -a a1 moob-proj"));
	ws_step_text(&p.s[5], text, sizeof text);
	CHECK(!strcmp(text, "mkdir proj, owner root:moob-proj, mode 0750"));
	ws_step_text(&p.s[6], text, sizeof text);
	CHECK(!strcmp(text, "mkdir proj/shared, owner root:moob-proj, mode 2770, default ACL "
		      "u::rwx,g::rwx,o::r-x"));
	CHECK(p.s[6].default_acl && p.s[6].mode == 02770);
	ws_step_text(&p.s[8], text, sizeof text);
	CHECK(!strcmp(text, "mkdir proj/a2, owner a2:a2, mode 0700"));
	CHECK(ws_step_argv(&p.s[1], argv) == 7 && !strcmp(argv[0], "useradd") && !argv[7]);
	CHECK(ws_step_argv(&p.s[5], argv) == 0 && !argv[0]);

	/* existing group and users, a1 already a member */
	in.make_group = 0;
	in.make_user[0] = in.make_user[1] = 0;
	in.is_member[0] = 1;
	snprintf(in.agent_group[1], sizeof in.agent_group[1], "staff");
	ws_plan_create(&in, &p);
	CHECK(p.n == 5 && p.s[0].kind == WS_GPASSWD_ADD && !strcmp(p.s[0].user, "a2"));
	CHECK(!strcmp(p.s[4].group, "staff") && p.s[4].mode == 0700);

	/* no agents: the namespace and its shared area */
	ag.n = 0;
	ws_plan_create(&in, &p);
	CHECK(p.n == 2 && p.s[1].kind == WS_MKDIR);
}

static int parse(unsigned int allowed, struct ws_args *a, int argc, ...)
{
	char *argv[16];
	va_list ap;
	int i;

	va_start(ap, argc);
	for (i = 0; i < argc; i++)
		argv[i] = va_arg(ap, char *);
	va_end(ap);
	return ws_parse_args(argc, argv, allowed, a);
}

static void args(void)
{
	const unsigned int create = WS_OPT_AGENTS | WS_OPT_CREATE_USERS | WS_OPT_DRY_RUN | WS_OPT_JSON;
	const unsigned int mount = WS_OPT_DEPTH | WS_OPT_NOLINKS | WS_OPT_NOINDEX | WS_OPT_BUDGET |
				   WS_OPT_UNSUPPORTED | WS_OPT_OPEN_LOWER;
	struct ws_args a;
	char data[128];

	CHECK(!parse(create, &a, 5, "/srv/memory", "proj", "--agents", "a1,a2", "--dry-run"));
	CHECK(a.npos == 2 && !strcmp(a.pos[1], "proj") && !strcmp(a.agents, "a1,a2") &&
	      (a.seen & WS_OPT_DRY_RUN) && !(a.seen & WS_OPT_JSON));
	CHECK(!parse(create, &a, 4, "--json", "--create-users", "/m", "ns"));
	CHECK(a.npos == 2 && (a.seen & WS_OPT_CREATE_USERS) && (a.seen & WS_OPT_JSON));
	CHECK(parse(create, &a, 3, "/m", "ns", "--agents") == -1);		/* no value */
	CHECK(parse(create, &a, 4, "/m", "ns", "--json", "--json") == -1);	/* repeated */
	CHECK(parse(create, &a, 3, "/m", "ns", "--count") == -1);		/* not allowed */
	CHECK(parse(create, &a, 3, "/m", "ns", "--bogus") == -1);
	CHECK(parse(create, &a, 5, "a", "b", "c", "d", "e") == -1);		/* too many */
	CHECK(!parse(WS_OPT_COUNT | WS_OPT_JSON, &a, 3, "/m", "--count", "memory_id"));
	CHECK(!strcmp(a.count, "memory_id"));
	CHECK(parse(WS_OPT_COUNT, &a, 3, "/m", "--count", "") == -1);

	CHECK(!parse(mount, &a, 2, "/lower", "/mnt") && a.depth == 2);
	CHECK(!ws_mount_data("/lower", &a, data, sizeof data));
	CHECK(!strcmp(data, "lower=/lower,domain_depth=2"));
	CHECK(!parse(mount, &a, 8, "/lower", "/mnt", "--domain-depth", "0", "--nolinks", "--noindex",
		     "--index-budget", "256M") && a.depth == 0);
	CHECK(!ws_mount_data("/lower", &a, data, sizeof data));
	CHECK(!strcmp(data, "lower=/lower,domain_depth=0,nolinks,noindex,index_budget=256M"));
	CHECK(ws_mount_data("/a,b", &a, data, sizeof data) == -1);
	CHECK(ws_mount_data("/lower", &a, data, 10) == -1);
	CHECK(parse(mount, &a, 4, "/l", "/m", "--domain-depth", "9") == -1);
	CHECK(parse(mount, &a, 4, "/l", "/m", "--domain-depth", "12") == -1);
	CHECK(parse(mount, &a, 4, "/l", "/m", "--domain-depth", "x") == -1);
	CHECK(!parse(mount, &a, 4, "/l", "/m", "--unsupported", "--allow-open-lower") &&
	      (a.seen & WS_OPT_UNSUPPORTED) && (a.seen & WS_OPT_OPEN_LOWER));

	CHECK(ws_budget_ok("1M") && ws_budget_ok("4G") && ws_budget_ok("1048576") &&
	      ws_budget_ok("4096k") && ws_budget_ok("4096M"));
	CHECK(!ws_budget_ok("") && !ws_budget_ok("M") && !ws_budget_ok("1048575") &&
	      !ws_budget_ok("5G") && !ws_budget_ok("4097M") && !ws_budget_ok("1T") &&
	      !ws_budget_ok("1MB") && !ws_budget_ok("-1M") && !ws_budget_ok("99999999999999999999"));
}

static void kernels_and_lower(void)
{
	static const char list[] = "# SPDX-License-Identifier: GPL-2.0-only\n"
				   "# comment 6.12.113+deb13-amd64\n"
				   "6.12.111+deb13-amd64\n"
				   "6.12.113+deb13-amd64  \n";

	CHECK(ws_kernel_listed(list, "6.12.111+deb13-amd64"));
	CHECK(ws_kernel_listed(list, "6.12.113+deb13-amd64"));
	CHECK(!ws_kernel_listed(list, "6.12.11"));
	CHECK(!ws_kernel_listed(list, "6.12.112+deb13-amd64"));
	CHECK(!ws_kernel_listed(list, ""));
	CHECK(!ws_kernel_listed("", "6.12.111+deb13-amd64"));
	CHECK(ws_kernel_listed("6.12.111+deb13-amd64", "6.12.111+deb13-amd64"));
	/* a series entry: '*' is one or more digits, nothing else (ADR-0036) */
	CHECK(ws_kernel_listed("6.12.111+deb13-amd64\n7.0.0-*-generic\n", "7.0.0-43-generic"));
	CHECK(ws_kernel_listed("7.0.0-*-generic", "7.0.0-1-generic"));
	CHECK(ws_kernel_listed("7.0.0-*-generic  ", "7.0.0-144-generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "7.0.0--generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "7.0.0-generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "7.0.0-43-lowlatency"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "7.0.0-43a-generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "7.0.0-4-3-generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "7.0.10-43-generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "7.0.0-43-generic-64k"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", "x7.0.0-43-generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-*-generic", "7.0.0-43-1-generic"));
	CHECK(!ws_kernel_listed("# 7.0.0-*-generic", "7.0.0-43-generic"));
	CHECK(!ws_kernel_listed("7.0.0-*-generic", ""));

	CHECK(ws_dir_blocks_others(040700, 0, 0, 0));
	CHECK(ws_dir_blocks_others(040750, 0, 0, 0));		/* group root */
	CHECK(!ws_dir_blocks_others(040750, 0, 100, 0));	/* another group can search */
	CHECK(ws_dir_blocks_others(040740, 0, 100, 0));
	CHECK(!ws_dir_blocks_others(040755, 0, 0, 0));
	CHECK(!ws_dir_blocks_others(040701, 0, 0, 0));
	CHECK(!ws_dir_blocks_others(040700, 1000, 0, 0));	/* the owner is not root */
	CHECK(!ws_dir_blocks_others(040700, 0, 0, 1));		/* an ACL may grant */
}

static void json(void)
{
	char buf[64] = "";
	FILE *f = tmpfile();
	size_t n;

	if (!f) {
		perror("tmpfile");
		failures++;
		return;
	}
	ws_json_str(f, "a\"b\\c\nd");
	rewind(f);
	n = fread(buf, 1, sizeof buf - 1, f);
	buf[n] = 0;
	fclose(f);
	CHECK(!strcmp(buf, "\"a\\\"b\\\\c\\u000ad\""));
}

int main(void)
{
	names();
	acl_codec();
	acl_mask();
	plan();
	args();
	kernels_and_lower();
	json();
	if (failures) {
		fprintf(stderr, "workspace_test: %d failure%s\n", failures, failures == 1 ? "" : "s");
		return 1;
	}
	printf("workspace_test: all checks passed\n");
	return 0;
}

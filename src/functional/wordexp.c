#define _GNU_SOURCE 1
#include <wordexp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <unistd.h>
#include "test.h"

#define ERR (-1)
#define FLAG(f) { f, #f }

static struct {
	int val;
	char *str;
} wrde_flags[] = {
	FLAG(WRDE_DOOFFS),
	FLAG(WRDE_APPEND),
	FLAG(WRDE_NOCMD),
	FLAG(WRDE_REUSE),
	FLAG(WRDE_SHOWERR),
	FLAG(WRDE_UNDEF),
	{ 0, NULL },
};

static char *flagstr(int flags)
{
	static char buf[256];
	char *sep = "";
	int i, n = 0;

	if (!flags)
		return "0";
	for (i = 0; wrde_flags[i].str; i++)
		if (flags & wrde_flags[i].val) {
			n += sprintf(buf+n, "%s%s", sep, wrde_flags[i].str);
			sep = "|";
			flags &= ~wrde_flags[i].val;
		}
	if (flags)
		sprintf(buf+n, "%s%#x", sep, flags);
	return buf;
}

static char *retstr(int r)
{
	switch (r) {
	case 0: return "0";
	case WRDE_NOSPACE: return "WRDE_NOSPACE";
	case WRDE_BADCHAR: return "WRDE_BADCHAR";
	case WRDE_BADVAL: return "WRDE_BADVAL";
	case WRDE_CMDSUB: return "WRDE_CMDSUB";
	case WRDE_SYNTAX: return "WRDE_SYNTAX";
	}
	return "WRDE_?";
}

static char *join(wordexp_t *we)
{
	static char buf[512];
	size_t i, n = 0;

	buf[0] = 0;
	for (i = 0; i < we->we_wordc && n < sizeof buf; i++)
		n += snprintf(buf+n, sizeof buf-n, "%s|", we->we_wordv[we->we_offs+i]);
	return buf;
}

static int wordexp_q(const char *s, wordexp_t *we, int flags, char *buf, size_t sz)
{
	int p[2], saved, r;
	ssize_t k;

	buf[0] = 0;
	fflush(stderr);
	saved = dup(2);
	if (saved < 0 || pipe(p)) {
		t_error("stderr capture failed: %s\n", strerror(errno));
		return wordexp(s, we, flags);
	}
	dup2(p[1], 2);
	close(p[1]);
	r = wordexp(s, we, flags);
	fflush(stderr);
	dup2(saved, 2);
	close(saved);
	k = read(p[0], buf, sz-1);
	close(p[0]);
	buf[k > 0 ? k : 0] = 0;
	return r;
}

static void glob_ok(const char *pat, const char *out)
{
	wordexp_t we;
	int r = wordexp(pat, &we, 0);

	if (r) {
		t_error("wordexp(\"%s\") returned %s\n", pat, retstr(r));
		return;
	}
	if (strcmp(join(&we), out))
		t_error("wordexp(\"%s\") gave [%s], want [%s]\n", pat, join(&we), out);
	wordfree(&we);
}

static struct {
	const char *in;
	int flags;
	int ret;
	int n;
	const char *out;
} tests[] = {
	{ "one two three",   0, 0, 3, "one|two|three|" },
	{ "",                0, 0, 0, "" },
	{ "   ",             0, 0, 0, "" },
	{ "  a  b  ",        0, 0, 2, "a|b|" },
	{ "a $FOO b",        0, 0, 4, "a|bar|baz|b|" },
	{ "$FOO$FOO",        0, 0, 3, "bar|bazbar|baz|" },
	{ "$FOO $FOO",       0, 0, 4, "bar|baz|bar|baz|" },
	{ "a\tb",            0, 0, 2, "a|b|" },

	{ "'a b'",           0, 0, 1, "a b|" },
	{ "\"a b\"",         0, 0, 1, "a b|" },
	{ "a' 'b",           0, 0, 1, "a b|" },
	{ "''",              0, 0, 1, "|" },
	{ "\"\"",            0, 0, 1, "|" },
	{ "a''b",            0, 0, 1, "ab|" },
	{ "a\"\"b",          0, 0, 1, "ab|" },
	{ "'\"'",            0, 0, 1, "\"|" },
	{ "\"'\"",           0, 0, 1, "'|" },
	{ "a\\ b",           0, 0, 1, "a b|" },
	{ "\\$FOO",          0, 0, 1, "$FOO|" },
	{ "\"a\"'b'\"c\"",   0, 0, 1, "abc|" },
	{ "''\"\"",          0, 0, 1, "|" },

	{ "$FOO",            0, 0, 2, "bar|baz|" },
	{ "\"$FOO\"",        0, 0, 1, "bar baz|" },
	{ "a$FOO-b",         0, 0, 2, "abar|baz-b|" },
	{ "${FOO}x",         0, 0, 2, "bar|bazx|" },
	{ "$UNSET",          0, 0, 0, "" },
	{ "${UNSET:-def}",   0, 0, 1, "def|" },
	{ "${UNSET-def}",    0, 0, 1, "def|" },
	{ "${FOO:+y}",       0, 0, 1, "y|" },
	{ "${#FOO}",         0, 0, 1, "7|" },
	{ "${FOO%% *}",      0, 0, 1, "bar|" },
	{ "${FOO#* }",       0, 0, 1, "baz|" },
	{ "${ASSIGN:=x}",    0, 0, 1, "x|" },
	{ "${UNSET:+x}",     0, 0, 0, "" },
	{ "${UNSET-}",       0, 0, 0, "" },
	{ "${FOO:?}",        0, 0, 2, "bar|baz|" },
	{ "${FOO:-}",        0, 0, 2, "bar|baz|" },
	{ "${FOO=x}",        0, 0, 2, "bar|baz|" },

	{ "$((3*4))",        0, 0, 1, "12|" },
	{ "$((1+2*3))",      0, 0, 1, "7|" },
	{ "x$((2*8))y",      0, 0, 1, "x16y|" },
	{ "$(((1+2)*3))",    0, 0, 1, "9|" },
	{ "$((10%3))",       0, 0, 1, "1|" },
	{ "$((-5+3))",       0, 0, 1, "-2|" },

	{ "$(echo hi there)", 0, 0, 2, "hi|there|" },
	{ "`echo q`",        0, 0, 1, "q|" },
	{ "$(echo)",         0, 0, 0, "" },
	{ "\"$(echo a b)\"", 0, 0, 1, "a b|" },
	{ "$(echo 'a b')",   0, 0, 2, "a|b|" },
	{ "a$(echo b)c",     0, 0, 1, "abc|" },
	{ "$(echo a)$(echo b)", 0, 0, 1, "ab|" },
	{ "a`echo b`c",      0, 0, 1, "abc|" },
	{ "$(echo $(echo x))", 0, 0, 1, "x|" },
	{ "$(printf 'a\\nb')", 0, 0, 2, "a|b|" },
	{ "$(echo a; echo b)", 0, 0, 2, "a|b|" },
	{ "$(true)",         0, 0, 0, "" },

	{ "~",               0, 0, 1, "/home/syd|" },
	{ "~/sub",           0, 0, 1, "/home/syd/sub|" },
	{ "a~",              0, 0, 1, "a~|" },
	{ "\"~\"",           0, 0, 1, "~|" },

	{ "$1",              0, 0, 0, "" },
	{ "$2",              0, 0, 0, "" },
	{ "$*",              0, 0, 0, "" },
	{ "$@",              0, 0, 0, "" },
	{ "$#",              0, 0, 1, "0|" },

	{ ")",               0, ERR, 0, NULL },
	{ "(",               0, ERR, 0, NULL },
	{ "$(",              0, WRDE_SYNTAX, 0, NULL },
	{ "$(echo",          0, WRDE_SYNTAX, 0, NULL },
	{ "${",              0, WRDE_SYNTAX, 0, NULL },
	{ "`",               0, WRDE_SYNTAX, 0, NULL },
	{ "'unterminated",   0, WRDE_SYNTAX, 0, NULL },
	{ "\"unterminated",  0, WRDE_SYNTAX, 0, NULL },

	{ "$(echo x)",       WRDE_NOCMD, WRDE_CMDSUB, 0, NULL },
	{ "`echo x`",        WRDE_NOCMD, WRDE_CMDSUB, 0, NULL },
	{ "a$(echo x)b",     WRDE_NOCMD, WRDE_CMDSUB, 0, NULL },
	{ "a`b",             WRDE_NOCMD, ERR, 0, NULL },
	{ "\"$(echo x)\"",   WRDE_NOCMD, WRDE_CMDSUB, 0, NULL },

	{ "a|b",             WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a&b",             WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a;b",             WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a<b",             WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a>b",             WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a{b",             WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a}b",             WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a\nb",            WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "(",               WRDE_NOCMD, WRDE_BADCHAR, 0, NULL },
	{ "a\\",             WRDE_NOCMD, WRDE_SYNTAX, 0, NULL },

	{ "'a|b'",           WRDE_NOCMD, 0, 1, "a|b|" },
	{ "\"a;b\"",         WRDE_NOCMD, 0, 1, "a;b|" },
	{ "a\\|b",           WRDE_NOCMD, 0, 1, "a|b|" },
	{ "\"a|b\"",         WRDE_NOCMD, 0, 1, "a|b|" },
	{ "'a&b'",           WRDE_NOCMD, 0, 1, "a&b|" },
	{ "a\\&b",           WRDE_NOCMD, 0, 1, "a&b|" },
	{ "'$(echo x)'",     WRDE_NOCMD, 0, 1, "$(echo x)|" },
	{ "$((1+1))",        WRDE_NOCMD, 0, 1, "2|" },
	{ "${FOO}",          WRDE_NOCMD, 0, 2, "bar|baz|" },
	{ "~",               WRDE_NOCMD, 0, 1, "/home/syd|" },
	{ "$FOO",            WRDE_NOCMD, 0, 2, "bar|baz|" },

	{ "$FOO",            WRDE_SHOWERR, 0, 2, "bar|baz|" },

	{ "$UNSET",          WRDE_UNDEF, WRDE_BADVAL, 0, NULL },
	{ "${UNSET}",        WRDE_UNDEF, WRDE_BADVAL, 0, NULL },
	{ "\"$UNSET\"",      WRDE_UNDEF, WRDE_BADVAL, 0, NULL },
	{ "a $UNSET b",      WRDE_UNDEF, WRDE_BADVAL, 0, NULL },
	{ "$((1+$UNSET))",   WRDE_UNDEF, ERR, 0, NULL },
	{ "$1",              WRDE_UNDEF, WRDE_BADVAL, 0, NULL },
	{ "$FOO",            WRDE_UNDEF, 0, 2, "bar|baz|" },
	{ "${FOO:-x}",       WRDE_UNDEF, 0, 2, "bar|baz|" },
	{ "${UNSET:-d}",     WRDE_UNDEF, 0, 1, "d|" },
	{ "${UNSET:-$FOO}",  WRDE_UNDEF, 0, 2, "bar|baz|" },
	{ "$(echo hi)",      WRDE_UNDEF, 0, 1, "hi|" },
	{ "\"$FOO\"",        WRDE_UNDEF, 0, 1, "bar baz|" },
	{ "",                WRDE_UNDEF, 0, 0, "" },
};

int main(void)
{
	wordexp_t we;
	size_t i;
	int r;

	setenv("FOO", "bar baz", 1);
	setenv("HOME", "/home/syd", 1);
	unsetenv("UNSET");
	unsetenv("ASSIGN");

	for (i = 0; i < sizeof tests / sizeof *tests; i++) {
		const char *in = tests[i].in;
		int flags = tests[i].flags;
		char err[256];

		int want = tests[i].ret;

		r = wordexp_q(in, &we, flags, err, sizeof err);
		if (!(flags & WRDE_SHOWERR) && err[0])
			t_error("wordexp(\"%s\", %s) wrote to stderr: %s\n",
				in, flagstr(flags), err);
		if (want == ERR ? r == 0 : r != want) {
			t_error("wordexp(\"%s\", %s) returned %s, want %s\n",
				in, flagstr(flags), retstr(r),
				want == ERR ? "error" : retstr(want));
			if (!r)
				wordfree(&we);
			continue;
		}
		if (!r) {
			char *got = join(&we);
			if (we.we_wordc != (size_t)tests[i].n)
				t_error("wordexp(\"%s\", %s) we_wordc %zu, want %d\n",
					in, flagstr(flags), we.we_wordc, tests[i].n);
			if (strcmp(got, tests[i].out))
				t_error("wordexp(\"%s\", %s) gave [%s], want [%s]\n",
					in, flagstr(flags), got, tests[i].out);
			wordfree(&we);
		}
	}

	we.we_offs = 2;
	r = wordexp("a b", &we, WRDE_DOOFFS);
	if (r)
		t_error("wordexp(WRDE_DOOFFS) returned %s\n", retstr(r));
	else {
		if (we.we_wordc != 2)
			t_error("WRDE_DOOFFS we_wordc %zu, want 2\n", we.we_wordc);
		if (we.we_wordv[0] || we.we_wordv[1])
			t_error("WRDE_DOOFFS leading slots not null\n");
		if (strcmp(we.we_wordv[2], "a") || strcmp(we.we_wordv[3], "b"))
			t_error("WRDE_DOOFFS words wrong: [%s] [%s]\n",
				we.we_wordv[2], we.we_wordv[3]);
		if (we.we_wordv[4])
			t_error("WRDE_DOOFFS not null terminated\n");
		wordfree(&we);
	}

	r = wordexp("a b", &we, 0);
	if (!r)
		r = wordexp("c d", &we, WRDE_APPEND);
	if (r)
		t_error("wordexp(WRDE_APPEND) returned %s\n", retstr(r));
	else {
		if (we.we_wordc != 4 || strcmp(join(&we), "a|b|c|d|"))
			t_error("WRDE_APPEND gave [%s] (%zu)\n", join(&we), we.we_wordc);
		wordfree(&we);
	}

	we.we_offs = 1;
	r = wordexp("a", &we, WRDE_DOOFFS);
	if (!r)
		r = wordexp("b", &we, WRDE_DOOFFS | WRDE_APPEND);
	if (r)
		t_error("wordexp(WRDE_DOOFFS|WRDE_APPEND) returned %s\n", retstr(r));
	else {
		if (we.we_wordc != 2 || we.we_wordv[0] ||
		    strcmp(we.we_wordv[1], "a") || strcmp(we.we_wordv[2], "b"))
			t_error("WRDE_DOOFFS|WRDE_APPEND layout wrong (%zu)\n", we.we_wordc);
		wordfree(&we);
	}

	r = wordexp("a b", &we, 0);
	if (!r)
		r = wordexp("c d e", &we, WRDE_REUSE);
	if (r)
		t_error("wordexp(WRDE_REUSE) returned %s\n", retstr(r));
	else {
		if (we.we_wordc != 3 || strcmp(join(&we), "c|d|e|"))
			t_error("WRDE_REUSE gave [%s] (%zu)\n", join(&we), we.we_wordc);
		wordfree(&we);
	}

	char cwd[PATH_MAX], dir[] = "/tmp/wordexpXXXXXX";

	if (!getcwd(cwd, sizeof cwd))
		t_error("getcwd: %s\n", strerror(errno));
	else if (!mkdtemp(dir))
		t_error("mkdtemp: %s\n", strerror(errno));
	else if (chdir(dir))
		t_error("chdir: %s\n", strerror(errno));
	else {
		fclose(fopen("aa", "w"));
		fclose(fopen("ab", "w"));

		glob_ok("a*", "aa|ab|");
		glob_ok("a?", "aa|ab|");
		glob_ok("*b", "ab|");
		glob_ok("?b", "ab|");
		glob_ok("a[ab]", "aa|ab|");
		glob_ok("a[!a]", "ab|");
		glob_ok("[a]a", "aa|");
		glob_ok("a[b-z]", "ab|");
		glob_ok("aa", "aa|");
		glob_ok("zz*", "zz*|");
		glob_ok("a* a?", "aa|ab|aa|ab|");

		unlink("aa");
		unlink("ab");
		if (chdir(cwd))
			t_error("chdir back: %s\n", strerror(errno));
		rmdir(dir);
	}

	return t_status;
}

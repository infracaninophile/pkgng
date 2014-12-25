/*-
 * Copyright (c) 2014 Matthew Seaman <matthew@FreeBSD.org>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <err.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <sysexits.h>

#include <pkg.h>

#include "pkgcli.h"

static bool	verbose = false;

int
do_diff_matched_packages(struct pkgdb *db, match_t match,
			 const char *reponame, int argc, char **argv)
{
	/*
	struct pkgdb_it	*it = NULL;
	struct pkg	*pkg = NULL;
	*/

	
	return (EPKG_OK);
}

void
usage_diff(void)
{
	fprintf(stderr, "Usage: pkg diff [-qv] [-r repo] -a | "
	    "-Cgix <pattern>\n\n");
	fprintf(stderr, "For more information see 'pkg help diff'.\n");
}

int
exec_diff(int argc, char **argv)
{
	struct pkgdb	*db = NULL;
	const char	*reponame = NULL;
	int		 ch;
	int		 ret;
	match_t 	 match = MATCH_EXACT;

	struct option longopts[] = {
		{ "all",	      no_argument,       NULL, 'a' },
		{ "case-sensitive",   no_argument,       NULL, 'C' },
		{ "glob",	      no_argument,       NULL, 'g' },
		{ "case-insensitive", no_argument,       NULL, 'i' },
		{ "guiet",	      no_argument,       NULL, 'q' },
		{ "repository",	      required_argument, NULL, 'r' },
		{ "verbose",	      no_argument,       NULL, 'v' },
		{ "regex",	      no_argument,       NULL, 'x' },
		{ NULL,		      0,		 NULL, 0   },
	};

	while ((ch = getopt_long(argc, argv, "+aCgigr:vx", longopts, NULL))
	       != -1) {
		switch (ch) {
		case 'a':
			match = MATCH_ALL;
			break;
		case 'C':
			pkgdb_set_case_sensitivity(true);
			break;
		case 'g':
			match = MATCH_GLOB;
			break;
		case 'i':
			pkgdb_set_case_sensitivity(false);
			break;
		case 'q':
			quiet = true;
			break;
		case 'r':
			reponame = optarg;
			break;
		case 'v':
			verbose = true;
			break;
		case 'x':
			match = MATCH_REGEX;
			break;
		default:
			usage_diff();
			return (EX_USAGE);
		}
	}
	argc -= optind;
	argv += optind;

	if (argc == 0 && match != MATCH_ALL) {
		usage_diff();
		return (EX_USAGE);
	}

	ret = pkgdb_access(PKGDB_MODE_READ, PKGDB_DB_LOCAL|PKGDB_DB_REPO);
	if (ret == EPKG_ENODB) {
		if (!quiet)
			warnx("No packages installed.  Nothing to do!");
		return (EX_OK);
	} else if (ret == EPKG_ENOACCESS) {
		if (!quiet)
			warnx("Insufficient privilege to access the package database");
		return (EX_NOPERM);
	} else if (ret != EPKG_OK) {
		if (!quiet)
			warnx("Error accessing the package database");
		return (EX_SOFTWARE);
	}

	ret = pkgdb_open(&db, PKGDB_DEFAULT);
	if (ret != EPKG_OK) {
		if (!quiet)
			warnx("Error opening the package database");
		return (EX_IOERR);
	}

	ret = pkgdb_obtain_lock(db, PKGDB_LOCK_ADVISORY);
	if (ret != EPKG_OK) {
		pkgdb_close(db);
		if (!quiet)
			warnx("Database busy: cannot get advisory lock");
		return (EX_TEMPFAIL);
	}

	ret = do_diff_matched_packages(db, match, reponame, argc, argv);

	pkgdb_release_lock(db, PKGDB_LOCK_ADVISORY);
	pkgdb_close(db);
	
	return ((ret == EPKG_OK) ? EX_OK : EX_SOFTWARE);
}

/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
#include <stdio.h>

#define TEST

#include "../xmlgen.h"
#include "../xmlgen.c"

int main(int argc, char **argv)
{
	char *tmp;
	struct kv test = {"bar", "baz"};

	tmp = tagify("foo", "bar", NULL, 0);
	if (strcmp(tmp, "<foo>bar</foo>") != 0) {
		fprintf(stderr, "Failed to tagify: %s\n", tmp);
		return 1;
	}

	free(tmp);

	tmp = tagify("foo", NULL, &test, 1);
	if (strcmp(tmp, "<foo bar='baz'/>") != 0) {
		fprintf(stderr, "Failed tagify with attr: %s\n", tmp);
		return 1;
	}

	free(tmp);

	tmp = tagify("foo", "bar", &test, 1);
	if (strcmp(tmp, "<foo bar='baz'>bar</foo>") != 0) {
		fprintf(stderr, "Failed to tagify with attr/content: %s\n", tmp);
		return 1;
	}

	free(tmp);

	return 0;
}

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
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "hostres.h"

bool is_bridge(const char *name)
{
        char *path = NULL;
        int ret;
        struct stat s;

        ret = asprintf(&path, "/sys/class/net/%s/bridge", name);
        if (ret == -1)
                return false;

        if (stat(path, &s) != 0)
                return false;

        if (S_ISDIR(s.st_mode))
                return true;
        else
                return false;
}

char **list_bridges(void)
{
        char **list = NULL;
        DIR *dir = NULL;
        struct dirent *de;
        int count = 0;

        dir = opendir("/sys/class/net");
        if (dir == NULL)
                return NULL;

        while ((de = readdir(dir))) {
                if (is_bridge(de->d_name)) {
                        list = realloc(list, ++count);
                        list[count-1] = strdup(de->d_name);
                }
        }

        /* Leave a NULL at the end, no matter what */
        list = realloc(list, count + 1);
        list[count] = NULL;

        return list;
}

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

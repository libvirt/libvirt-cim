/*
 * Copyright IBM Corp. 2008
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
#include <inttypes.h>

#include <libvirt/libvirt.h>

#include "../infostore.h"

int main(int argc, char **argv)
{
        struct infostore_ctx *ctx = NULL;
        virConnectPtr conn = NULL;
        virDomainPtr dom = NULL;

        if (argc != 2) {
                printf("Usage: %s <domain>\n", argv[0]);
                return 1;
        }

        conn = virConnectOpen(NULL);
        if (conn == NULL) {
                printf("Unable to open connection\n");
                goto out;
        }

        dom = virDomainLookupByName(conn, argv[1]);
        if (dom == NULL) {
                printf("Unable to lookup domain `%s'\n", argv[1]);
                goto out;
        }

        ctx = infostore_open(dom);
        if (ctx == NULL) {
                printf("Unable to open infostore for `%s'\n", argv[1]);
                goto out;
        }

        printf("Foo: %" PRIu64 "\n", infostore_get_u64(ctx, "foo"));

        infostore_set_u64(ctx, "foo", 321);
        infostore_set_u64(ctx, "bar", 987);
        printf("Should be (null): %s\n", infostore_get_str(ctx, "baz"));

 out:
        infostore_close(ctx);
        virDomainFree(dom);
        virConnectClose(conn);

        return 0;
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

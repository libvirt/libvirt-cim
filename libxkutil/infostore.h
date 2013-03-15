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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#ifndef __INFOSTORE_H
#define __INFOSTORE_H

#include <stdint.h>
#include <stdbool.h>
#include <libvirt/libvirt.h>

struct infostore_ctx;

struct infostore_ctx *infostore_open(virDomainPtr dom);
void infostore_close(struct infostore_ctx *ctx);
void infostore_delete(const char *type, const char *name);

uint64_t infostore_get_u64(struct infostore_ctx *ctx, const char *key);
bool infostore_set_u64(struct infostore_ctx *ctx,
                       const char *key, uint64_t val);

char *infostore_get_str(struct infostore_ctx *ctx, const char *key);
bool infostore_set_str(struct infostore_ctx *ctx,
                       const char *key, const char * val);

bool infostore_get_bool(struct infostore_ctx *ctx, const char *key);
bool infostore_set_bool(struct infostore_ctx *ctx,
                        const char *key, bool val);

#endif

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

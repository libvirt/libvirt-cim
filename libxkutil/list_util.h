/*
 * Copyright IBM Corp. 2012
 *
 * Authors:
 *  Eduardo Lima (Etrunko) <eblima@br.ibm.com>
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

#ifndef __LIST_UTIL_H
#define __LIST_UTIL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*list_data_free_cb)(void *data);
typedef int  (*list_data_cmp_cb)(void *list_data, void *user_data);
typedef bool (*list_foreach_cb)(void *list_data, void *user_data);

typedef struct _list_node_t list_node_t;
typedef struct _list_t list_t;

list_t *list_new(list_data_free_cb free_cb, list_data_cmp_cb cmp_cb);
void    list_free(list_t *list);

void list_append(list_t *list, void *data);
void list_prepend(list_t *list, void *data);

void        *list_find(list_t *list, void *user_data);
list_node_t *list_find_node(list_t *list, void *user_data);

void list_remove(list_t *list, void *data);
void list_remove_node(list_t *list, list_node_t *node);

bool list_foreach(list_t *list, list_foreach_cb cb, void *user_data);

inline unsigned int list_count(list_t *list);

inline void *list_node_data_get(list_node_t *node);
inline void  list_node_data_set(list_node_t *node, void *data);

inline void        *list_first(list_t *list);
inline list_node_t *list_first_node(list_t *list);

inline void        *list_last(list_t *list);
inline list_node_t *list_last_node(list_t *list);

inline void        *list_node_next(list_node_t *node);
inline list_node_t *list_node_next_node(list_node_t *node);

inline void        *list_node_prev(list_node_t *node);
inline list_node_t *list_node_prev_node(list_node_t *node);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIST_UTIL_H */

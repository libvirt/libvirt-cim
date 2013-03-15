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
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>

#include "list_util.h"

struct _list_node_t {
        list_node_t *prev;
        list_node_t *next;
        void *data;
};

struct _list_t {
        unsigned int count;
        list_node_t *head;
        list_data_free_cb free_cb;
        list_data_cmp_cb cmp_cb;
};

list_t *list_new(list_data_free_cb free_cb, list_data_cmp_cb cmp_cb)
{
        list_t *l = calloc(1, sizeof(*l));
        if (l == NULL)
                return NULL;

        l->free_cb = free_cb;
        l->cmp_cb = cmp_cb;
        return l;
}

void list_free(list_t *list)
{
        list_node_t *n, *next;

        if (list == NULL || list->head == NULL)
                return;

        n = list->head;

        do {
                if (list->free_cb)
                        list->free_cb(n->data);

                next = n->next;
                free(n);
                n = next;
        } while (n != list->head);

        free(list);
}

void list_append(list_t *list, void *data)
{
        list_node_t *n;

        if (list == NULL)
                return;

        n = calloc(1, sizeof(*n));

        if (n == NULL)
                return;

        n->data = data;

        if (list->head == NULL) { /* empty list */
                n->next = n->prev = n;
                list->head = n;
                goto end;
        }

        n->next = list->head;
        n->prev = list->head->prev;

        list->head->prev->next = n;
        list->head->prev = n;

 end:
        list->count += 1;
}

void list_prepend(list_t *list, void *data)
{
        list_append(list, data);
        list->head = list->head->prev;
}

void *list_find(list_t *list, void *user_data)
{
        list_node_t *n = list_find_node(list, user_data);
        return list_node_data_get(n);
}

list_node_t *list_find_node(list_t *list, void *user_data)
{
        list_node_t *n;

        if (list == NULL || list->head == NULL || list->cmp_cb == NULL)
                return NULL;

        n = list->head;
        do {
                if (list->cmp_cb(n->data, user_data) == 0)
                        return n;

                n = n->next;
        } while (n != list->head);

        return NULL;
}

void list_remove(list_t *list, void *data)
{
        list_node_t *n = list_find_node(list, data);
        list_remove_node(list, n);
}

void list_remove_node(list_t *list, list_node_t *node)
{
        if (list == NULL || list->head == NULL || node == NULL)
                return;

        if (node->next == node) { /* only 1 item */
                list->head = NULL;
        } else {
                if (node == list->head) /* first node */
                        list->head = node->next;

                node->prev->next = node->next;
                node->next->prev = node->prev;
        }

        if (list->free_cb)
                list->free_cb(node->data);

        free(node);
        list->count -= 1;
}

bool list_foreach(list_t *list, list_foreach_cb cb, void *user_data)
{
        list_node_t *node;

        if (list == NULL || list->head == NULL)
                return true; /* nothing to do */

        node = list->head;
        do {
                if (cb(node->data, user_data) == false)
                        return false;

                node = node->next;
        } while (node != list->head);

        return true;
}

unsigned int list_count(list_t *list)
{
        if (list == NULL)
                return 0;

        return list->count;
}

void *list_node_data_get(list_node_t *node)
{
        if (node == NULL)
                return NULL;

        return node->data;
}

void list_node_data_set(list_node_t *node, void *data)
{
        if (node == NULL)
                return;

        node->data = data;
}

void *list_first(list_t *list)
{
        return list_node_data_get(list_first_node(list));
}

list_node_t *list_first_node(list_t *list)
{
        if (list == NULL)
                return NULL;

        return list->head;
}

void *list_last(list_t *list)
{
        return list_node_data_get(list_last_node(list));
}

list_node_t *list_last_node(list_t *list)
{
        if (list == NULL || list->head == NULL)
                return NULL;

        return list->head->prev;
}

void *list_node_next(list_node_t *node)
{
        return list_node_data_get(list_node_next_node(node));
}

list_node_t *list_node_next_node(list_node_t *node)
{
        if (node == NULL)
                return NULL;

        return node->next;
}

void *list_node_prev(list_node_t *node)
{
        return list_node_data_get(list_node_prev_node(node));
}

list_node_t *list_node_prev_node(list_node_t *node)
{
        if (node == NULL)
                return NULL;

        return node->prev;
}

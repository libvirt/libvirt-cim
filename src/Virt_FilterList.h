/*
 * Copyright IBM Corp. 2011
 *
 * Authors:
 *  Chip Vincent <cvincent@us.ibm.com>
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
#ifndef __VIRT_FILTERLIST_H
#define __VIRT_FILTERLIST_H

#include "acl_parsing.h"

/**
 * Return a list of filter lists
 *
 * @param broker A pointer to the CIM broker
 * @param context A pointer to an operation context
 * @param reference Defines the libvirt connection to use (via class name
 *             prefix), but can also be used to scope the results by
 *             specifying a specific filter
 * @param list A pointer to an array of CMPIInstance objects
 *             (caller inits before and frees after)
 */
CMPIStatus enum_filter_lists(
                const CMPIBroker *broker,
                const CMPIContext *context,
                const CMPIObjectPath *reference,
                struct inst_list *list);

/**
 * Return a filter instance by reference
 *
 * @param broker A pointer to the CIM broker
 * @param context A pointer to an operation context
 * @param reference Defines the libvirt connection to use (via class name
 *             prefix), but can also be used to scope the results by
 *             specifying a specific filter
 * @param instance A pointer to a CMPIInstance * to place the new instance
 */
CMPIStatus get_filter_by_ref(
                const CMPIBroker *broker,
                const CMPIContext *contest,
                const CMPIObjectPath *reference,
                CMPIInstance **instance);

/**
 * Return a list of filter lists
 *
 * @param broker A pointer to the CIM broker
 * @param context A pointer to an operation context
 * @param reference Defines the libvirt connection to use (via class name
 *             prefix), but can also be used to scope the results by
 *             specifying a specific filter
 * @param filter A pointer to a acl_filter
 * @param instance A pointer to a CMPIInstance * to place the new instance
 */
CMPIStatus instance_from_filter(
                const CMPIBroker *broker,
                const CMPIContext *context,
                const CMPIObjectPath *reference,
                struct acl_filter *filter,
                CMPIInstance **instance);

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

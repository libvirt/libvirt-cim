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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */
#ifndef __VIRT_FILTERENTRY_H
#define __VIRT_FILTERENTRY_H

/**
 * Return a list of filter instances
 *
 * @param broker A pointer to the CIM broker
 * @param context A pointer to an operation context
 * @param reference Defines the libvirt connection to use (via class name
 *             prefix), but can also be used to scope the results by
 *             specifying the parent filter
 * @param list A pointer to an array of CMPIInstance objects
 *             (called inits before and frees after)
 */
CMPIStatus enum_filter_rules(
                const CMPIBroker *broker,
                const CMPIContext *context,
                const CMPIObjectPath *reference,
                struct inst_list *list);

/**
 * Return a single filter instance by reference
 *
 * @param broker A pointer to the CIM broker
 * @param context A pointer to an operation context
 * @param reference Defines the libvirt connection to use (via class name
 *             prefix), but can also be used to scope the results by
 *             specifying the parent filter
 * @param list A pointer to a CMPIInstance *
 */
CMPIStatus get_rule_by_ref(
                const CMPIBroker *broker,
                const CMPIContext *context,
                const CMPIObjectPath *reference,
                CMPIInstance **instance);

/**
 * Get an instance representing a filter rule
 *
 * @param broker A pointer to the CIM broker
 * @param context A pointer to an operation context
 * @param reference Defines the libvirt connection to use (via class name
 *             prefix), but can also be used to scope the results by
 *             specifying the parent filter
 * @param rule A pointer to a filter rule
 * @param instance A pointer to a CMPIInstance *
 */
CMPIStatus instance_from_rule(
                const CMPIBroker *broker,
                const CMPIContext *context,
                const CMPIObjectPath *reference,
                struct acl_rule *rule,
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

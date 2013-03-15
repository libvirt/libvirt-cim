/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Kaitlin Rupert <karupert@us.ibm.com>
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

#define MIN_XEN_WEIGHT 1
#define MAX_XEN_WEIGHT 65535
#define INC_XEN_WEIGHT MAX_XEN_WEIGHT / 2 
#define DEFAULT_XEN_WEIGHT 1024

#define MIN_KVM_WEIGHT 2
#define MAX_KVM_WEIGHT 262144
#define INC_KVM_WEIGHT 1
#define DEFAULT_KVM_WEIGHT 1024

CMPIStatus get_vsms(const CMPIObjectPath *reference,
                    CMPIInstance **_inst,
                    const CMPIBroker *broker,
                    const CMPIContext *context,
                    bool is_get_inst);

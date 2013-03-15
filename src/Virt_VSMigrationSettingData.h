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

enum {CIM_MIGRATE_OTHER = 1,
      CIM_MIGRATE_LIVE = 2,
      CIM_MIGRATE_RESUME = 3,
      CIM_MIGRATE_RESTART = 4,
} migration_type;

enum {CIM_MIGRATE_URI_OTHER = 1,
      CIM_MIGRATE_URI_SSH = 2,
      CIM_MIGRATE_URI_TLS = 3,
      CIM_MIGRATE_URI_TLS_STRICT = 4,
      CIM_MIGRATE_URI_TCP = 5,
      CIM_MIGRATE_URI_UNIX = 32768,
} transport_type;

CMPIStatus get_migration_sd(const CMPIObjectPath *ref,
                            CMPIInstance **_inst,
                            const CMPIBroker *broker,
                            bool is_get_inst);
/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */


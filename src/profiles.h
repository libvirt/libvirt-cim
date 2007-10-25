/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Jay Gagnon <grendel@linux.vnet.ibm.com>
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
struct reg_prof {
        uint16_t reg_org; // Valid: 1 = Other, 2 = DMTF
        char *reg_name;
        char *reg_version;
        int ad_types;
        char *other_reg_org;
        char *ad_type_descriptions;
        char *provider_name;
};

struct reg_prof Processor = {
        .reg_org = 2,
        .reg_name = "Processor Profile",
        .reg_version = "2.15",
        .provider_name = "Processor"
};

struct reg_prof LogicalDisk = {
        .reg_org = 2,
        .reg_name = "Logical Disk Profile",
        .reg_version = "2.15",
        .provider_name = "LogicalDisk"
};

struct reg_prof NetworkPort = {
        .reg_org = 2,
        .reg_name = "Network Port Profile",
        .reg_version = "2.15",
        .provider_name = "NetworkPort"
};

struct reg_prof ComputerSystem = {
        .reg_org = 2,
        .reg_name = "Computer System Profile",
        .reg_version = "2.15",
        .provider_name = "ComputerSystem"
};

struct reg_prof SVP = {
        .reg_org = 2,
        .reg_name = "System Virtualization Profile",
        .reg_version = "2.15",
        .provider_name = "VirtualSystemManagementService"
};


// Make sure to add pointer to your reg_prof struct here.
struct reg_prof *profiles[] = {
        &Processor,
        &LogicalDisk,
        &NetworkPort,
        &ComputerSystem,
        &SVP,
        NULL
};

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

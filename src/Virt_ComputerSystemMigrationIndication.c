/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 * Jay Gagnon <grendel@linux.vnet.ibm.com>
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
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include <cmpidt.h>
#include <cmpift.h>
#include <cmpimacs.h>

#include <libvirt/libvirt.h>

#include <libcmpiutil/libcmpiutil.h>
#include <misc_util.h>
#include <libcmpiutil/std_indication.h>
#include <cs_util.h>

#include "config.h"

#include "Virt_ComputerSystem.h"

static const CMPIBroker *_BROKER;

DECLARE_FILTER(xen_migrate, "Xen_ComputerSystemMigrationIndication");

static struct std_ind_filter *filters[] = {
        &xen_migrate,
        NULL,
};

DEFAULT_IND_CLEANUP();
DEFAULT_AF();
DEFAULT_MP();

STDI_IndicationMIStub(, 
                      Virt_ComputerSystemMigrationIndicationProvider,
                      _BROKER,
                      libvirt_cim_init(), 
                      NULL,
                      filters);

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *  Guolian Yun <yunguol@cn.ibm.com>
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
#ifndef __SVPC_TYPES_H
#define __SVPC_TYPES_H

#define CIM_RASD_TYPE_PROC  3
#define CIM_RASD_TYPE_MEM   4
#define CIM_RASD_TYPE_NET  10
#define CIM_RASD_TYPE_DISK 17

#define CIM_VSSD_RECOVERY_NONE       2
#define CIM_VSSD_RECOVERY_RESTART    3
/* Vendor-specific extension; should be documented somewhere */
#define CIM_VSSD_RECOVERY_PRESERVE 123

#include <libcmpiutil.h>
#include <string.h>

static inline char *vssd_recovery_action_str(int action)
{
	switch (action) {
	case CIM_VSSD_RECOVERY_NONE:
		return "destroy";
	case CIM_VSSD_RECOVERY_RESTART:
		return "restart";
	case CIM_VSSD_RECOVERY_PRESERVE:
		return "preserve";
	default:
		return "destroy";
	}
}

static inline uint16_t vssd_recovery_action_int(const char *action)
{
	if (STREQ(action, "destroy"))
		return CIM_VSSD_RECOVERY_NONE;
	else if (STREQ(action, "restart"))
		return CIM_VSSD_RECOVERY_RESTART;
	else if (STREQ(action, "preserve"))
		return CIM_VSSD_RECOVERY_PRESERVE;
	else
		return CIM_VSSD_RECOVERY_NONE;
}

#endif

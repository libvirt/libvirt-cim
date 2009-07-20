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

#define CIM_OPERATIONAL_STATUS 2

#define CIM_RES_TYPE_ALL        0
#define CIM_RES_TYPE_PROC       3
#define CIM_RES_TYPE_MEM        4
#define CIM_RES_TYPE_NET        10
#define CIM_RES_TYPE_DISK       17
#define CIM_RES_TYPE_EMU        1
#define CIM_RES_TYPE_GRAPHICS   24
#define CIM_RES_TYPE_INPUT      13 
#define CIM_RES_TYPE_UNKNOWN    1000
#define CIM_RES_TYPE_IMAGE      32768 

#define CIM_RES_TYPE_COUNT 6
const static int cim_res_types[CIM_RES_TYPE_COUNT] = 
  {CIM_RES_TYPE_NET,
   CIM_RES_TYPE_DISK,
   CIM_RES_TYPE_MEM,
   CIM_RES_TYPE_PROC,
   CIM_RES_TYPE_GRAPHICS,
   CIM_RES_TYPE_INPUT,
  };

#define CIM_VSSD_RECOVERY_NONE       2
#define CIM_VSSD_RECOVERY_RESTART    3
/* Vendor-specific extension; should be documented somewhere */
#define CIM_VSSD_RECOVERY_PRESERVE 123

#define CIM_SVPC_RETURN_JOB_STARTED   4096
#define CIM_SVPC_RETURN_FAILED           2
#define CIM_SVPC_RETURN_COMPLETED        0

#define CIM_EC_CHAR_DEFAULT 2

/* ConsoleRedirectionService values */
#define CIM_CRS_SERVICE_TYPE  3
#define CIM_CRS_SHARING_MODE  3
#define CIM_CRS_ENABLED_STATE   2
#define CIM_CRS_REQUESTED_STATE 12

#define  CIM_CRS_OTHER 1
#define  CIM_CRS_VNC   4

#define CIM_SAP_ACTIVE_STATE    2
#define CIM_SAP_INACTIVE_STATE  3
#define CIM_SAP_AVAILABLE_STATE 6

#define NETPOOL_FORWARD_NONE 0
#define NETPOOL_FORWARD_NAT 1
#define NETPOOL_FORWARD_ROUTED 2

#include <libcmpiutil/libcmpiutil.h>
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

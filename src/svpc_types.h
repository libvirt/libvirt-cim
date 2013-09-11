/*
 * Copyright IBM Corp. 2007, 2013
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
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */
#ifndef __SVPC_TYPES_H
#define __SVPC_TYPES_H

#define CIM_OPERATIONAL_STATUS 2

#define CIM_RES_TYPE_ALL        0
#define CIM_RES_TYPE_OTHER      1
#define CIM_RES_TYPE_PROC       3
#define CIM_RES_TYPE_MEM        4
#define CIM_RES_TYPE_NET        10
#define CIM_RES_TYPE_DISK       17
#define CIM_RES_TYPE_EMU        1
#define CIM_RES_TYPE_GRAPHICS   24
#define CIM_RES_TYPE_INPUT      13 
#define CIM_RES_TYPE_UNKNOWN    1000
#define CIM_RES_TYPE_IMAGE      32768 
#define CIM_RES_TYPE_CONSOLE    32769

#define CIM_RES_TYPE_COUNT 7
const static int cim_res_types[CIM_RES_TYPE_COUNT] = 
  {CIM_RES_TYPE_NET,
   CIM_RES_TYPE_DISK,
   CIM_RES_TYPE_MEM,
   CIM_RES_TYPE_PROC,
   CIM_RES_TYPE_GRAPHICS,
   CIM_RES_TYPE_INPUT,
   CIM_RES_TYPE_CONSOLE,
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

/* State enums used by ComputerSystem */
enum CIM_state {
        CIM_STATE_UNKNOWN      = 0,
        CIM_STATE_OTHER        = 1,
        CIM_STATE_ENABLED      = 2,
        CIM_STATE_DISABLED     = 3,
        CIM_STATE_SHUTDOWN     = 4,
        CIM_STATE_NOCHANGE     = 5,
        CIM_STATE_SUSPENDED    = 6,
        CIM_STATE_PAUSED       = 9,
        CIM_STATE_REBOOT       = 10,
        CIM_STATE_RESET        = 11,
};

enum CIM_health_state {
        CIM_HEALTH_UNKNOWN = 0,
        CIM_HEALTH_OK = 5,
        CIM_HEALTH_MINOR_FAILURE = 15,
        CIM_HEALTH_MAJOR_FAILURE = 20,
        CIM_HEALTH_CRITICAL_FAILURE = 25,
        CIM_HEALTH_NON_RECOVERABLE = 30,
};

enum CIM_oping_status {
        CIM_OPING_STATUS_UNKNOWN = 0,
        CIM_OPING_STATUS_NOT_AVAILABLE = 1,
        CIM_OPING_STATUS_SERVICING = 2,
        CIM_OPING_STATUS_STARTING = 3,
        CIM_OPING_STATUS_STOPPING = 4,
        CIM_OPING_STATUS_STOPPED = 5,
        CIM_OPING_STATUS_ABORTED = 6,
        CIM_OPING_STATUS_DORMANT = 7,
        CIM_OPING_STATUS_COMPLETED = 8,
        CIM_OPING_STATUS_MIGRATING = 9,
        CIM_OPING_STATUS_EMIGRATING = 10,
        CIM_OPING_STATUS_IMMIGRATING = 11,
        CIM_OPING_STATUS_SNAPSHOTTING = 12,
        CIM_OPING_STATUS_SHUTTING_DOWN = 13,
        CIM_OPING_STATUS_IN_TEST = 14,
        CIM_OPING_STATUS_TRANSITIONING = 15,
        CIM_OPING_STATUS_IN_SERVICE = 16,
        CIM_OPING_STATUS_STARTED = 32768,
};

enum CIM_op_status {
        CIM_OP_STATUS_UNKNOWN = 0,
        CIM_OP_STATUS_OTHER = 1,
        CIM_OP_STATUS_OK = 2,
        CIM_OP_STATUS_DEGRADED = 3,
        CIM_OP_STATUS_STRESSED = 4,
        CIM_OP_STATUS_PREDICTIVE_FAILURE = 5,
        CIM_OP_STATUS_ERROR = 6,
        CIM_OP_STATUS_NON_RECOVERABLE = 7,
        CIM_OP_STATUS_STARTING = 8,
        CIM_OP_STATUS_STOPPING = 9,
        CIM_OP_STATUS_STOPPED = 10,
        CIM_OP_STATUS_IN_SERVICE = 11,
        CIM_OP_STATUS_NO_CONTACT = 12,
        CIM_OP_STATUS_LOST_COMMS = 13,
        CIM_OP_STATUS_ABORTED = 14,
        CIM_OP_STATUS_DORMANT = 15,
        CIM_OP_STATUS_COMPLETED = 17,
        CIM_OP_STATUS_POWER_MODE = 18,
};

/* emum for the Character device Source resource types */
enum CIM_chardev_source_type {
        CIM_CHARDEV_SOURCE_TYPE_NULL = 0,
        CIM_CHARDEV_SOURCE_TYPE_VC = 1,
        CIM_CHARDEV_SOURCE_TYPE_PTY = 2,
        CIM_CHARDEV_SOURCE_TYPE_DEV = 3,
        CIM_CHARDEV_SOURCE_TYPE_FILE = 4,
        CIM_CHARDEV_SOURCE_TYPE_PIPE = 5,
        CIM_CHARDEV_SOURCE_TYPE_STDIO = 6,
        CIM_CHARDEV_SOURCE_TYPE_UDP = 7,
        CIM_CHARDEV_SOURCE_TYPE_TCP = 8,
        CIM_CHARDEV_SOURCE_TYPE_UNIXSOCK = 9,
        CIM_CHARDEV_SOURCE_TYPE_SPICEVMC = 10,
        /* please insert new source types above */
        CIM_CHARDEV_SOURCE_TYPE_INVALIDTYPE,
        CIM_CHARDEV_SOURCE_TYPE_UNKNOWN = 32768,
};

static inline int chardev_source_type_StrToID(const char *type_str)
{
        int rc = CIM_CHARDEV_SOURCE_TYPE_UNKNOWN;

        if (type_str == NULL)
                return rc;

        if (STREQC(type_str, "null"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_NULL;
        else if (STREQC(type_str, "vc"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_VC;
        else if (STREQC(type_str, "pty"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_PTY;
        else if (STREQC(type_str, "dev"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_DEV;
        else if (STREQC(type_str, "file"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_FILE;
        else if (STREQC(type_str, "pipe"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_PIPE;
        else if (STREQC(type_str, "stdio"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_STDIO;
        else if (STREQC(type_str, "udp"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_UDP;
        else if (STREQC(type_str, "tcp"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_TCP;
        else if (STREQC(type_str, "unix"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_UNIXSOCK;
        else if (STREQC(type_str, "spicevmc"))
                 rc = CIM_CHARDEV_SOURCE_TYPE_SPICEVMC;

        return rc;
}

static inline const char* chardev_source_type_IDToStr(int type)
{
        char *type_str = NULL;

        switch (type)
        {
        case CIM_CHARDEV_SOURCE_TYPE_NULL:
                type_str = "null";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_VC:
                type_str = "vc";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_PTY:
                type_str = "pty";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_DEV:
                type_str = "dev";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_FILE:
                type_str = "file";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_PIPE:
                type_str = "pipe";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_STDIO:
                type_str = "stdio";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_UDP:
                type_str = "udp";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_TCP:
                type_str = "tcp";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_UNIXSOCK:
                type_str = "unix";
                break;
        case CIM_CHARDEV_SOURCE_TYPE_SPICEVMC:
                type_str = "spicevmc";
                break;
        default:
                break;
        }
        return type_str;
}

#endif

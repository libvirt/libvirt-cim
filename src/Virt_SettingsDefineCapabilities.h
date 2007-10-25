/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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
#define PROP_END {NULL, NULL, CMPI_chars}

typedef enum {SDC_RASD_MIN,
              SDC_RASD_MAX,
              SDC_RASD_DEF,
              SDC_RASD_INC
} sdc_rasd_type;

enum {SDC_POLICY_NONE = -1,
      SDC_POLICY_INDEPENDENT = 0,
      SDC_POLICY_CORRELATED = 1,
} policy;

enum {SDC_ROLE_NONE = -1,
      SDC_ROLE_DEFAULT = 0,
      SDC_ROLE_SUPPORTED = 3,
} role;

enum {SDC_RANGE_NONE = -1,
      SDC_RANGE_POINT = 0,
      SDC_RANGE_MIN = 1,
      SDC_RANGE_MAX = 2,
      SDC_RANGE_INC = 3,
} range;

struct sdc_rasd_prop {
        char *field;
        CMPIValue *value;
        CMPIType type;
};

typedef struct sdc_rasd_prop *(*rasd_prop_func_t)(void);

struct sdc_rasd {
        uint16_t resource_type;
        rasd_prop_func_t min;
        rasd_prop_func_t max;
        rasd_prop_func_t def;
        rasd_prop_func_t inc;
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

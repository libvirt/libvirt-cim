/*
 * Copyright IBM Corp. 2007
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
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

/* Returns a malloc()'d string; caller must free() */
char *vsss_get_save_path(const char *domname);
bool vsss_has_save_image(const char *domname);
CMPIStatus vsss_delete_snapshot(const char *domname);

CMPIStatus get_vsss(const CMPIBroker *broker,
                    const CMPIContext *context,
                    const CMPIObjectPath *ref,
                    CMPIInstance **_inst,
                    bool is_get_inst);

#define CIM_VSSS_SNAPSHOT_FULL 2
#define CIM_VSSS_SNAPSHOT_DISK 3

/* VIR_VSSS_SNAPSHOT_MEM  - Attempt to save/restore to create a running snap
 * VIR_VSSS_SNAPSHOT_MEMT - Just save and let the domain be "off"
 */
#define VIR_VSSS_SNAPSHOT_MEM  32768
#define VIR_VSSS_SNAPSHOT_MEMT 32769

#define VIR_VSSS_ERR_SAVE_FAILED    1
#define VIR_VSSS_ERR_REST_FAILED    2
#define VIR_VSSS_ERR_CONN_FAILED    3
#define VIR_VSSS_ERR_NO_SUCH_DOMAIN 4

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */

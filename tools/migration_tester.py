#!/usr/bin/python
#
# Copyright IBM Corp. 2009
#
# Authors:
#    Kaitlin Rupert <karupert@us.ibm.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
#
# Usage:  python tools/migration_tester.py -u root -p pass 
#          -s source.myhost.com:5988 -t target.myhost.com -v Xen guest 
#          --migration-type live 
#
# Note:  Be sure you can authenticate via ssh into the target machine prior 
#        to running the test.
#
#        You can set up ssh keys, or you will be prompted for the password 
#        of the target machine

import sys
import time
from optparse import OptionParser
from pywbem import WBEMConnection, CIMInstanceName, CIMInstance, CIMError

CIM_MIGRATE_OTHER=1
CIM_MIGRATE_LIVE=2
CIM_MIGRATE_RESUME=3
CIM_MIGRATE_RESTART=4

CIM_MIGRATE_URI_OTHER=1
CIM_MIGRATE_URI_SSH=2
CIM_MIGRATE_URI_TLS=3
CIM_MIGRATE_URI_TLS_STRICT=4
CIM_MIGRATE_URI_TCP=5
CIM_MIGRATE_URI_UNIX=6

CIM_JOBSTATE_COMPLETE=7

class CIMClassMOF:

    __supported_types = [int, str, bool]

    def __init__(self, attrs = None):
        """attrs should be dict
        """

        if attrs != None:
            self.__dict__.update(attrs)

    def mof(self):
        """mof()

        Return value is a string, containing the mof representation of the 
        object.

        Attribute types supported are : int, str, bool.

        Attributes with unsupported types will be silently ignored when 
        converting to mof representation.
        """

        mof_str = "instance of " + self.__class__.__name__ + " {\n"
        for key, value in self.__dict__.items():
            value_type = type(value)
            if value_type not in self.__supported_types:
                continue

            mof_str += "%s = " % key
            if value_type == int:
                mof_str += "%d" % value
            elif value_type == bool:
                mof_str += str(value).lower()
            else:
                mof_str += '"%s"' % value
            mof_str += ";\n"

        mof_str += "};"
        return mof_str

    def __str__(self):
        return self.mof()

class CIM_VirtualSystemMigrationSettingData(CIMClassMOF):
    def __init__(self, type, priority):
        self.InstanceID = 'MigrationSettingData'
        self.CreationClassName = self.__class__.__name__
        self.MigrationType = type
        self.Priority = priority

class Xen_VirtualSystemMigrationSettingData(CIM_VirtualSystemMigrationSettingData):
    def __init__(self, type, priority):
        CIM_VirtualSystemMigrationSettingData.__init__(self, type, priority)

class KVM_VirtualSystemMigrationSettingData(CIM_VirtualSystemMigrationSettingData):
    def __init__(self, type, priority):
        CIM_VirtualSystemMigrationSettingData.__init__(self, type, priority)

def get_guest_ref(guest, virt):
    guest_cn = "%s_ComputerSystem" % virt

    keys = { 'Name' : guest,
             'CreationClassName' : guest_cn
           } 

    try:
        cs_ref = CIMInstanceName(guest_cn, keybindings=keys) 

    except CIMError, (err_no, desc):
        print err_no, desc
        return None

    return cs_ref

def get_msd(mtype, virt):
    if mtype == "live":
        mtype = CIM_MIGRATE_LIVE
    elif mtype == "resume":
        mtype = CIM_MIGRATE_RESUME
    elif mtype == "restart":
        mtype = CIM_MIGRATE_RESTART
    else:
        mtype = CIM_MIGRATE_OTHER

    try:
        vsmsd_cn_base = "_VirtualSystemMigrationSettingData"
        msd = eval(virt + vsmsd_cn_base)(type=mtype, priority=0)

    except CIMError, (err_no, desc):
        print err_no, desc
        return None

    return msd.mof()

def check_migrate(s_conn, cs_ref, ip, msd, virt):
    vsms_cn = "%s_VirtualSystemMigrationService" % virt
    try:
        if msd == None:
            res = s_conn.InvokeMethod("CheckVirtualSystemIsMigratableToHost",
                                      vsms_cn,
                                      ComputerSystem=cs_ref,
                                      DestinationHost=ip)
        else:
            res = s_conn.InvokeMethod("CheckVirtualSystemIsMigratableToHost",
                                      vsms_cn,
                                      ComputerSystem=cs_ref,
                                      DestinationHost=ip,
                                      MigrationSettingData=msd)

        if res == None or res[1]['IsMigratable'] != True:
            print "Migration check failed."
            return 1

    except CIMError, (err_no, desc):
        print err_no, desc
        return 1

    return 0 

def get_job_inst(s_conn, job_ref):
    try:
        inst = s_conn.GetInstance(job_ref) 

    except CIMError, (err_no, desc):
        print err_no, desc
        return None

    return inst

def poll_for_job_status(s_conn, job_ref):

    job_inst = get_job_inst(s_conn, job_ref)
    if not job_inst:
        print "Unable to get job instance" 
        return 1

    try:
        while job_inst['JobState'] != CIM_JOBSTATE_COMPLETE:
            time.sleep(3)
            job_inst = get_job_inst(s_conn, job_ref)
            if not job_inst:
                print "Unable to get job instance" 
                return 1
            
        if job_inst['Status'] != "Completed":
            print "Migrate job failed: %s" % job_inst['Status']
            return 1 
    except KeyboardInterrupt:
        print "Migrate job took too long"
        return 1 

    print "Migrate job succeeded: %s" % job_inst['Status']
    return 0

def migrate_host(s_conn, cs_ref, dest, msd, virt):
    vsms_cn = "%s_VirtualSystemMigrationService" % virt

    try:
        if msd == None:
            job = s_conn.InvokeMethod("MigrateVirtualSystemToHost",
                                      vsms_cn,
                                      ComputerSystem=cs_ref,
                                      DestinationHost=dest)
        else:
            job = s_conn.InvokeMethod("MigrateVirtualSystemToHost",
                                      vsms_cn,
                                      ComputerSystem=cs_ref,
                                      DestinationHost=dest,
                                      MigrationSettingData=msd)

        if len(job) < 1:
            print "No job returned from migrate call" 
            return 1

        status = poll_for_job_status(s_conn, job[1]['Job'])

    except CIMError, (err_no, desc):
        print err_no, desc
        return 1

    return 0 

def main():
    usage = "usage: %prog [options] <target system>\nex: %prog my.target.com"
    parser = OptionParser(usage)

    parser.add_option("-s", "--src-url", dest="s_url", default="localhost:5988",
                      help="URL of CIMOM to connect to (host:port)")
    parser.add_option("-t", "--target-url", dest="t_url", 
                      default="localhost:5988",
                      help="URL of CIMOM to connect to (host:port)")
    parser.add_option("-N", "--ns", dest="ns", default="root/virt",
                      help="Namespace (default is root/virt)")
    parser.add_option("-u", "--user", dest="username", default=None,
                      help="Auth username for CIMOM on source system")
    parser.add_option("-p", "--pass", dest="password", default=None,
                      help="Auth password for CIMOM on source system")
    parser.add_option("-v", "--virt-type", dest="virt", default=None,
                      help="Virtualization type [ Xen | KVM ]")
    parser.add_option("--migration-type", dest="type", default=None,
                      help="Migration type:[ live | resume | restart | other ]")
    parser.add_option("--disable-check", dest="disable_ck", action="store_true",
                      help="Disable migration pre-check")

    (options, args) = parser.parse_args()

    if len(args) == 0:
        print "Fatal: no guest specified."
        sys.exit(1)

    guest_name = args[0]

    if ":" in options.s_url:
        (sysname, port) = options.s_url.split(":")
    else:
        sysname = options.s_url

    if ":" in options.t_url:
        (t_sysname, port) = options.t_url.split(":")
    else:
        t_sysname = options.t_url

    src_conn = WBEMConnection('http://%s' % sysname, 
                              (options.username, options.password), options.ns)

    guest_ref = get_guest_ref(guest_name, options.virt)
    if guest_ref == None:
        return 1

    if options.virt == None:
        print "Must specify virtualization type"
        return 1

    if options.type != None:
        msd = get_msd(options.type, options.virt)
        if msd == None:
            return 1
    else:
        print "Using default MigrationSettingData"
        msd = None

    if not options.disable_ck:
        status = check_migrate(src_conn, guest_ref, t_sysname, msd, 
                               options.virt)
        if status == 1:
            return 1

    print "Migrating %s.. this will take some time." % guest_name
    status = migrate_host(src_conn, guest_ref, t_sysname, msd, options.virt)
    return status

if __name__=="__main__":
    sys.exit(main())



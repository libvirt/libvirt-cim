# Copyright IBM Corp. 2007
SUBDIRS = libxkutil src doc

MOFS = \
	schema/ComputerSystem.mof \
	schema/LogicalDisk.mof \
	schema/NetworkPort.mof \
	schema/Memory.mof \
	schema/Processor.mof \
	schema/SystemDevice.mof \
	schema/VSSD.mof \
	schema/HostSystem.mof \
	schema/HostedDependency.mof \
	schema/VirtualSystemManagementService.mof \
	schema/VirtualSystemManagementCapabilities.mof \
	schema/EnabledLogicalElementCapabilities.mof \
	schema/AllocationCapabilities.mof \
	schema/SettingsDefineCapabilities.mof \
	schema/MemoryPool.mof \
	schema/ElementCapabilities.mof \
	schema/ProcessorPool.mof \
	schema/DiskPool.mof \
	schema/HostedResourcePool.mof \
	schema/ElementConformsToProfile.mof \
	schema/ComputerSystemIndication.mof \
	schema/ResourceAllocationSettingData.mof \
	schema/ResourcePoolConfigurationService.mof \
	schema/ResourcePoolConfigurationCapabilities.mof \
	schema/VSSDComponent.mof \
	schema/SettingsDefineState.mof \
	schema/NetPool.mof \
	schema/ResourceAllocationFromPool.mof
	schema/ElementAllocatedFromPool.mof

INTEROP_MOFS = \
	schema/RegisteredProfile.mof \
	schema/ElementConformsToProfile.mof


REGS = \
	schema/ComputerSystem.registration \
	schema/LogicalDisk.registration \
	schema/NetworkPort.registration \
	schema/Memory.registration \
	schema/Processor.registration \
	schema/SystemDevice.registration \
	schema/VSSD.registration \
	schema/HostSystem.registration \
	schema/HostedDependency.registration \
	schema/VirtualSystemManagementService.registration \
	schema/VirtualSystemManagementCapabilities.registration \
	schema/EnabledLogicalElementCapabilities.registration \
	schema/AllocationCapabilities.registration \
	schema/SettingsDefineCapabilities.registration \
	schema/MemoryPool.registration \
	schema/ElementCapabilities.registration \
	schema/ProcessorPool.registration \
	schema/DiskPool.registration \
	schema/HostedResourcePool.registration \
	schema/ComputerSystemIndication.registration \
	schema/ResourceAllocationSettingData.registration \
	schema/ResourcePoolConfigurationService.registration \
	schema/ResourcePoolConfigurationCapabilities.registration \
	schema/VSSDComponent.registration \
	schema/SettingsDefineState.registration \
	schema/NetPool.registration \
	schema/ResourceAllocationFromPool.registration \
	schema/ElementAllocatedFromPool.registration

INTEROP_REGS = \
	schema/RegisteredProfile.registration \
	schema/ElementConformsToProfile.registration

EXTRA_BASE_MOFS = \
	schema/Xen_ResourceAllocationSettingData.mof \
	schema/KVM_ResourceAllocationSettingData.mof

pkgdata_DATA = $(MOFS) $(REGS)
pkgdata_SCRIPTS = provider-register.sh register_base.sh

EXTRA_DIST = schema $(pkgdata_DATA) $(pkgdata_SCRIPTS)	\
             libvirt-cim.spec.in libvirt-cim.spec	\
	     doc/CodingStyle doc/SubmittingPatches

# Un/Register the providers and class definitions from/to the current CIMOM.
# @CIMSERVER@ is set by the configure script
postinstall:
	sh register_base.sh @CIMSERVER@ $(EXTRA_BASE_MOFS)
	sh provider-register.sh -v -t @CIMSERVER@ -n /root/ibmsd -r $(REGS) -m $(MOFS)
	sh provider-register.sh -v -t @CIMSERVER@ -n /root/interop -r $(INTEROP_REGS) -m $(INTEROP_MOFS)

preuninstall:
	sh provider-register.sh -v -d -t @CIMSERVER@ -n /root/ibmsd -r $(REGS) -m $(MOFS)
	sh provider-register.sh -v -d -t @CIMSERVER@ -n /root/interop -r $(INTEROP_REGS) -m $(INTEROP_MOFS)

rpm: clean
	@(unset CDPATH ; $(MAKE) dist && rpmbuild -ta $(distdir).tar.gz)

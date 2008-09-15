# Copyright IBM Corp. 2007
AUTOMAKE_OPTIONS=dist-bzip2

SUBDIRS = libxkutil src doc base_schema

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
	schema/RegisteredProfile.mof \
	schema/ElementConformsToProfile.mof \
	schema/ComputerSystemIndication.mof \
	schema/ComputerSystemMigrationIndication.mof \
	schema/ResourceAllocationSettingData.mof \
	schema/ResourcePoolConfigurationService.mof \
	schema/ResourcePoolConfigurationCapabilities.mof \
	schema/VSSDComponent.mof \
	schema/SettingsDefineState.mof \
	schema/NetPool.mof \
	schema/ResourceAllocationFromPool.mof \
	schema/ElementAllocatedFromPool.mof \
	schema/HostedService.mof \
	schema/ElementSettingData.mof \
	schema/VSMigrationCapabilities.mof \
	schema/VSMigrationService.mof \
	schema/VSMigrationSettingData.mof \
	schema/VirtualSystemSnapshotService.mof \
	schema/VirtualSystemSnapshotServiceCapabilities.mof \
	schema/ConcreteComponent.mof \
	schema/ConsoleRedirectionService.mof \
        schema/ConsoleRedirectionServiceCapabilities.mof \
        schema/ServiceAffectsElement.mof

INTEROP_MOFS = \
	schema/ComputerSystem.mof \
	schema/HostSystem.mof \
	schema/RegisteredProfile.mof \
	schema/DiskPool.mof \
	schema/MemoryPool.mof \
	schema/NetPool.mof \
	schema/ProcessorPool.mof \
	schema/VSMigrationService.mof \
	schema/ElementConformsToProfile.mof \
	schema/ReferencedProfile.mof


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
	schema/ComputerSystemMigrationIndication.registration \
	schema/ResourceAllocationSettingData.registration \
	schema/ResourcePoolConfigurationService.registration \
	schema/ResourcePoolConfigurationCapabilities.registration \
	schema/VSSDComponent.registration \
	schema/SettingsDefineState.registration \
	schema/NetPool.registration \
	schema/ResourceAllocationFromPool.registration \
	schema/ElementAllocatedFromPool.registration \
	schema/HostedService.registration \
	schema/ElementSettingData.registration \
	schema/VSMigrationCapabilities.registration \
	schema/VSMigrationService.registration \
	schema/ElementConformsToProfile.registration \
	schema/VSMigrationSettingData.registration \
	schema/VirtualSystemSnapshotService.registration \
	schema/VirtualSystemSnapshotServiceCapabilities.registration \
	schema/ConcreteComponent.registration \
	schema/ConsoleRedirectionService.registration \
        schema/ConsoleRedirectionServiceCapabilities.registration \
        schema/ServiceAffectsElement.registration

INTEROP_REGS = \
	schema/RegisteredProfile.registration \
	schema/ElementConformsToProfile.registration \
	schema/ReferencedProfile.registration

pkgdata_DATA = $(MOFS) $(REGS) $(INTEROP_MOFS) $(INTEROP_REGS)
pkgdata_SCRIPTS = provider-register.sh

EXTRA_DIST = schema $(pkgdata_DATA) $(pkgdata_SCRIPTS)	\
             libvirt-cim.spec.in libvirt-cim.spec	\
	     doc/CodingStyle doc/SubmittingPatches      \
             .changeset .revision 			\
	     examples/diskpool.conf

preinstall:
	sh -x base_schema/install_base_schema.sh `pwd`/base_schema

# Un/Register the providers and class definitions from/to the current CIMOM.
# @CIMSERVER@ is set by the configure script
postinstall:
	sh provider-register.sh -v -t @CIMSERVER@ -n @CIM_VIRT_NS@ -r $(REGS) -m $(MOFS)
	sh provider-register.sh -v -t @CIMSERVER@ -n root/interop -r $(INTEROP_REGS) -m $(INTEROP_MOFS)
	virsh -v | grep -q '^0.3' && cp examples/diskpool.conf $(DISK_POOL_CONFIG) || true
	mkdir -p $(INFO_STORE)

preuninstall:
	sh provider-register.sh -v -d -t @CIMSERVER@ -n @CIM_VIRT_NS@ -r $(REGS) -m $(MOFS)
	sh provider-register.sh -v -d -t @CIMSERVER@ -n root/interop -r $(INTEROP_REGS) -m $(INTEROP_MOFS)

rpm: clean
	@(unset CDPATH ; $(MAKE) dist && rpmbuild -ta $(distdir).tar.gz)

clean-local:
	rm -f $(find . -name "*.orig")
	rm -f $(find . -name "*.rej")
	rm -f $(find . -name "*~")


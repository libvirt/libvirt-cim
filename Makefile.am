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
	schema/Virt_VSSD.mof \
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
	schema/ResourceAllocationSettingDataIndication.mof \
	schema/ComputerSystemMigrationIndication.mof \
	schema/Virt_ResourceAllocationSettingData.mof \
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
	schema/ServiceAffectsElement.mof \
	schema/KVMRedirectionSAP.mof \
	schema/DisplayController.mof \
	schema/PointingDevice.mof \
	schema/GraphicsPool.mof \
	schema/InputPool.mof \
	schema/HostedAccessPoint.mof \
	schema/ServiceAccessBySAP.mof \
	schema/SAPAvailableForElement.mof

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
	schema/ReferencedProfile.mof \
	schema/AllocationCapabilities.mof

PGINTEROP_MOFS = \
	schema/RegisteredProfile.mof \
	schema/ElementConformsToProfile.mof \
	schema/ReferencedProfile.mof

CIMV2_MOFS = \
	schema/HostedResourcePool.mof \
	schema/ElementCapabilities.mof \
	schema/HostedService.mof \
	schema/HostedDependency.mof \
	schema/RegisteredProfile.mof \
	schema/ComputerSystem.mof \
	schema/ElementConformsToProfile.mof \
	schema/HostedAccessPoint.mof

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
	schema/ResourceAllocationSettingDataIndication.registration \
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
	schema/ServiceAffectsElement.registration \
	schema/KVMRedirectionSAP.registration \
	schema/DisplayController.registration \
	schema/PointingDevice.registration \
	schema/GraphicsPool.registration \
	schema/InputPool.registration \
	schema/HostedAccessPoint.registration \
	schema/ServiceAccessBySAP.registration \
	schema/SAPAvailableForElement.registration

INTEROP_REGS = \
	schema/RegisteredProfile.registration \
	schema/ElementConformsToProfile.registration \
	schema/ReferencedProfile.registration

PGINTEROP_REGS = \
	schema/RegisteredProfile.registration \
	schema/ElementConformsToProfile.registration \
	schema/ReferencedProfile.registration

CIMV2_REGS = \
	schema/HostedResourcePool.registration \
	schema/ElementCapabilities.registration \
	schema/HostedService.registration \
	schema/HostedDependency.registration \
	schema/ElementConformsToProfile.registration \
	schema/HostedAccessPoint.registration

pkgdata_SCRIPTS = provider-register.sh

EXTRA_DIST = schema $(MOFS) $(REGS) $(INTEROP_MOFS) $(INTEROP_REGS) \
             $(pkgdata_SCRIPTS)	libvirt-cim.spec.in libvirt-cim.spec \
	     doc/CodingStyle doc/SubmittingPatches      \
             .changeset .revision 			\
	     examples/diskpool.conf

# If Pegasus isn't the CIMOM target, then remove the PG_InterOp namespace from the appropriate files
install-data-local:
	$(mkinstalldirs) "$(DESTDIR)$(pkgdatadir)"
	$(install_sh_DATA) -t "$(DESTDIR)$(pkgdatadir)" $(MOFS)
	$(install_sh_DATA) -t "$(DESTDIR)$(pkgdatadir)" $(REGS)
	$(install_sh_DATA) -t "$(DESTDIR)$(pkgdatadir)" $(INTEROP_MOFS)
	$(install_sh_DATA) -t "$(DESTDIR)$(pkgdatadir)" $(INTEROP_REGS)
	if [[ @CIMSERVER@ != pegasus ]]; then \
	sed -i '/^# --/,/^# --!/d' $(subst schema,$(pkgdatadir), $(PGINTEROP_REGS)); \
	fi

uninstall-local:
	@list='$(MOFS) $(REGS) $(INTEROP_MOFS) $(INTEROP_REGS)'; \
	for p in $$list; do \
	f=`echo "$$p" | sed 's|^.*/||;'`; \
	echo " rm -f '$(DESTDIR)$(pkgdatadir)/$$f'"; \
	rm -f "$(DESTDIR)$(pkgdatadir)/$$f"; \
	done

preinstall:
	sh -x base_schema/install_base_schema.sh `pwd`/base_schema

# Un/Register the providers and class definitions from/to the current CIMOM.
# @CIMSERVER@ is set by the configure script
postinstall:
	sh provider-register.sh -v -t @CIMSERVER@ -n @CIM_VIRT_NS@ -r $(subst schema,$(pkgdatadir), $(REGS)) -m $(subst schema,$(pkgdatadir), $(MOFS))
	sh provider-register.sh -v -t @CIMSERVER@ -n root/interop -r $(subst schema,$(pkgdatadir), $(INTEROP_REGS)) -m $(subst schema,$(pkgdatadir), $(INTEROP_MOFS))
	sh provider-register.sh -v -t @CIMSERVER@ -n root/cimv2 -r $(subst schema,$(pkgdatadir), $(CIMV2_REGS)) -m $(subst schema,$(pkgdatadir), $(CIMV2_MOFS))
	if [[ @CIMSERVER@ = pegasus ]]; then \
	sh provider-register.sh -v -t @CIMSERVER@ -n root/PG_InterOp -r $(subst schema,$(pkgdatadir), $(PGINTEROP_REGS)) -m $(subst schema,$(pkgdatadir), $(PGINTEROP_MOFS)); \
	fi
	virsh -v | grep -q '^0.3' && cp examples/diskpool.conf $(DISK_POOL_CONFIG) || true
	mkdir -p $(INFO_STORE)

preuninstall:
	sh provider-register.sh -v -d -t @CIMSERVER@ -n @CIM_VIRT_NS@ -r $(subst schema,$(pkgdatadir), $(REGS)) -m $(subst schema,$(pkgdatadir), $(MOFS))
	sh provider-register.sh -v -d -t @CIMSERVER@ -n root/interop -r $(subst schema,$(pkgdatadir), $(INTEROP_REGS)) -m $(subst schema,$(pkgdatadir), $(INTEROP_MOFS))
	sh provider-register.sh -v -d -t @CIMSERVER@ -n root/cimv2 -r $(subst schema,$(pkgdatadir), $(CIMV2_REGS)) -m $(subst schema,$(pkgdatadir), $(CIMV2_MOFS))
	if [[ @CIMSERVER@ = pegasus ]]; then \
	sh provider-register.sh -v -d -t @CIMSERVER@ -n root/PG_InterOp -r $(subst schema,$(pkgdatadir), $(PGINTEROP_REGS)) -m $(subst schema,$(pkgdatadir), $(PGINTEROP_MOFS)); \
	fi

rpm: clean
	@(unset CDPATH ; $(MAKE) dist && rpmbuild -ta $(distdir).tar.gz)

clean-local:
	rm -f $(find . -name "*.orig")
	rm -f $(find . -name "*.rej")
	rm -f $(find . -name "*~")


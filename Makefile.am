# Copyright IBM Corp. 2007
AUTOMAKE_OPTIONS=dist-bzip2

SUBDIRS = libxkutil src doc base_schema

MOFS = \
	$(top_srcdir)/schema/ComputerSystem.mof \
	$(top_srcdir)/schema/LogicalDisk.mof \
	$(top_srcdir)/schema/NetworkPort.mof \
	$(top_srcdir)/schema/Memory.mof \
	$(top_srcdir)/schema/Processor.mof \
	$(top_srcdir)/schema/SystemDevice.mof \
	$(top_srcdir)/schema/Virt_VSSD.mof \
	$(top_srcdir)/schema/VSSD.mof \
	$(top_srcdir)/schema/HostSystem.mof \
	$(top_srcdir)/schema/HostedDependency.mof \
	$(top_srcdir)/schema/VirtualSystemManagementService.mof \
	$(top_srcdir)/schema/VirtualSystemManagementCapabilities.mof \
	$(top_srcdir)/schema/EnabledLogicalElementCapabilities.mof \
	$(top_srcdir)/schema/AllocationCapabilities.mof \
	$(top_srcdir)/schema/SettingsDefineCapabilities.mof \
	$(top_srcdir)/schema/MemoryPool.mof \
	$(top_srcdir)/schema/ElementCapabilities.mof \
	$(top_srcdir)/schema/ProcessorPool.mof \
	$(top_srcdir)/schema/DiskPool.mof \
	$(top_srcdir)/schema/HostedResourcePool.mof \
	$(top_srcdir)/schema/RegisteredProfile.mof \
	$(top_srcdir)/schema/ElementConformsToProfile.mof \
	$(top_srcdir)/schema/ComputerSystemIndication.mof \
	$(top_srcdir)/schema/ResourceAllocationSettingDataIndication.mof \
	$(top_srcdir)/schema/SwitchService.mof \
	$(top_srcdir)/schema/ComputerSystemMigrationIndication.mof \
	$(top_srcdir)/schema/Virt_ResourceAllocationSettingData.mof \
	$(top_srcdir)/schema/ResourceAllocationSettingData.mof \
	$(top_srcdir)/schema/ResourcePoolConfigurationService.mof \
	$(top_srcdir)/schema/ResourcePoolConfigurationCapabilities.mof \
	$(top_srcdir)/schema/VSSDComponent.mof \
	$(top_srcdir)/schema/SettingsDefineState.mof \
	$(top_srcdir)/schema/NetPool.mof \
	$(top_srcdir)/schema/ResourceAllocationFromPool.mof \
	$(top_srcdir)/schema/ElementAllocatedFromPool.mof \
	$(top_srcdir)/schema/HostedService.mof \
	$(top_srcdir)/schema/ElementSettingData.mof \
	$(top_srcdir)/schema/VSMigrationCapabilities.mof \
	$(top_srcdir)/schema/VSMigrationService.mof \
	$(top_srcdir)/schema/VSMigrationSettingData.mof \
	$(top_srcdir)/schema/VirtualSystemSnapshotService.mof \
	$(top_srcdir)/schema/VirtualSystemSnapshotServiceCapabilities.mof \
	$(top_srcdir)/schema/ConcreteComponent.mof \
	$(top_srcdir)/schema/ConsoleRedirectionService.mof \
	$(top_srcdir)/schema/ConsoleRedirectionServiceCapabilities.mof \
	$(top_srcdir)/schema/ServiceAffectsElement.mof \
	$(top_srcdir)/schema/KVMRedirectionSAP.mof \
	$(top_srcdir)/schema/DisplayController.mof \
	$(top_srcdir)/schema/PointingDevice.mof \
	$(top_srcdir)/schema/GraphicsPool.mof \
	$(top_srcdir)/schema/InputPool.mof \
	$(top_srcdir)/schema/HostedAccessPoint.mof \
	$(top_srcdir)/schema/ServiceAccessBySAP.mof \
	$(top_srcdir)/schema/SAPAvailableForElement.mof \
	$(top_srcdir)/schema/FilterEntry.mof \
	$(top_srcdir)/schema/FilterList.mof \
	$(top_srcdir)/schema/EntriesInFilterList.mof \
	$(top_srcdir)/schema/NestedFilterList.mof \
	$(top_srcdir)/schema/AppliedFilterList.mof \
	$(top_srcdir)/schema/HostedFilterList.mof

INTEROP_MOFS = \
	$(top_srcdir)/schema/ComputerSystem.mof \
	$(top_srcdir)/schema/HostSystem.mof \
	$(top_srcdir)/schema/RegisteredProfile.mof \
	$(top_srcdir)/schema/DiskPool.mof \
	$(top_srcdir)/schema/MemoryPool.mof \
	$(top_srcdir)/schema/NetPool.mof \
	$(top_srcdir)/schema/ProcessorPool.mof \
	$(top_srcdir)/schema/VSMigrationService.mof \
	$(top_srcdir)/schema/ElementConformsToProfile.mof \
	$(top_srcdir)/schema/ReferencedProfile.mof \
	$(top_srcdir)/schema/AllocationCapabilities.mof

PGINTEROP_MOFS = \
	$(top_srcdir)/schema/RegisteredProfile.mof \
	$(top_srcdir)/schema/ElementConformsToProfile.mof \
	$(top_srcdir)/schema/ReferencedProfile.mof

CIMV2_MOFS = \
	$(top_srcdir)/schema/HostedResourcePool.mof \
	$(top_srcdir)/schema/ElementCapabilities.mof \
	$(top_srcdir)/schema/HostedService.mof \
	$(top_srcdir)/schema/HostedDependency.mof \
	$(top_srcdir)/schema/RegisteredProfile.mof \
	$(top_srcdir)/schema/ComputerSystem.mof \
	$(top_srcdir)/schema/ElementConformsToProfile.mof \
	$(top_srcdir)/schema/HostedAccessPoint.mof

REGS = \
	$(top_srcdir)/schema/ComputerSystem.registration \
	$(top_srcdir)/schema/LogicalDisk.registration \
	$(top_srcdir)/schema/NetworkPort.registration \
	$(top_srcdir)/schema/Memory.registration \
	$(top_srcdir)/schema/Processor.registration \
	$(top_srcdir)/schema/SystemDevice.registration \
	$(top_srcdir)/schema/VSSD.registration \
	$(top_srcdir)/schema/HostSystem.registration \
	$(top_srcdir)/schema/HostedDependency.registration \
	$(top_srcdir)/schema/VirtualSystemManagementService.registration \
	$(top_srcdir)/schema/VirtualSystemManagementCapabilities.registration \
	$(top_srcdir)/schema/EnabledLogicalElementCapabilities.registration \
	$(top_srcdir)/schema/AllocationCapabilities.registration \
	$(top_srcdir)/schema/SettingsDefineCapabilities.registration \
	$(top_srcdir)/schema/MemoryPool.registration \
	$(top_srcdir)/schema/ElementCapabilities.registration \
	$(top_srcdir)/schema/ProcessorPool.registration \
	$(top_srcdir)/schema/DiskPool.registration \
	$(top_srcdir)/schema/HostedResourcePool.registration \
	$(top_srcdir)/schema/ComputerSystemIndication.registration \
	$(top_srcdir)/schema/ResourceAllocationSettingDataIndication.registration \
	$(top_srcdir)/schema/SwitchService.registration \
	$(top_srcdir)/schema/ComputerSystemMigrationIndication.registration \
	$(top_srcdir)/schema/ResourceAllocationSettingData.registration \
	$(top_srcdir)/schema/ResourcePoolConfigurationService.registration \
	$(top_srcdir)/schema/ResourcePoolConfigurationCapabilities.registration \
	$(top_srcdir)/schema/VSSDComponent.registration \
	$(top_srcdir)/schema/SettingsDefineState.registration \
	$(top_srcdir)/schema/NetPool.registration \
	$(top_srcdir)/schema/ResourceAllocationFromPool.registration \
	$(top_srcdir)/schema/ElementAllocatedFromPool.registration \
	$(top_srcdir)/schema/HostedService.registration \
	$(top_srcdir)/schema/ElementSettingData.registration \
	$(top_srcdir)/schema/VSMigrationCapabilities.registration \
	$(top_srcdir)/schema/VSMigrationService.registration \
	$(top_srcdir)/schema/ElementConformsToProfile.registration \
	$(top_srcdir)/schema/VSMigrationSettingData.registration \
	$(top_srcdir)/schema/VirtualSystemSnapshotService.registration \
	$(top_srcdir)/schema/VirtualSystemSnapshotServiceCapabilities.registration \
	$(top_srcdir)/schema/ConcreteComponent.registration \
	$(top_srcdir)/schema/ConsoleRedirectionService.registration \
	$(top_srcdir)/schema/ConsoleRedirectionServiceCapabilities.registration \
	$(top_srcdir)/schema/ServiceAffectsElement.registration \
	$(top_srcdir)/schema/KVMRedirectionSAP.registration \
	$(top_srcdir)/schema/DisplayController.registration \
	$(top_srcdir)/schema/PointingDevice.registration \
	$(top_srcdir)/schema/GraphicsPool.registration \
	$(top_srcdir)/schema/InputPool.registration \
	$(top_srcdir)/schema/HostedAccessPoint.registration \
	$(top_srcdir)/schema/ServiceAccessBySAP.registration \
	$(top_srcdir)/schema/SAPAvailableForElement.registration \
	$(top_srcdir)/schema/FilterEntry.registration \
	$(top_srcdir)/schema/FilterList.registration \
	$(top_srcdir)/schema/EntriesInFilterList.registration \
	$(top_srcdir)/schema/NestedFilterList.registration \
	$(top_srcdir)/schema/AppliedFilterList.registration \
	$(top_srcdir)/schema/HostedFilterList.registration

INTEROP_REGS = \
	$(top_srcdir)/schema/RegisteredProfile.registration \
	$(top_srcdir)/schema/ElementConformsToProfile.registration \
	$(top_srcdir)/schema/ReferencedProfile.registration

PGINTEROP_REGS = \
	$(top_srcdir)/schema/RegisteredProfile.registration \
	$(top_srcdir)/schema/ElementConformsToProfile.registration \
	$(top_srcdir)/schema/ReferencedProfile.registration

CIMV2_REGS = \
	$(top_srcdir)/schema/HostedResourcePool.registration \
	$(top_srcdir)/schema/ElementCapabilities.registration \
	$(top_srcdir)/schema/HostedService.registration \
	$(top_srcdir)/schema/HostedDependency.registration \
	$(top_srcdir)/schema/ElementConformsToProfile.registration \
	$(top_srcdir)/schema/HostedAccessPoint.registration

pkgdata_SCRIPTS = provider-register.sh

libvirtcim_confdir = @sysconfdir@
dist_libvirtcim_conf_DATA = @PACKAGE@.conf

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
	sed -i '/^# --/,/^# --!/d' $(subst $(top_srcdir)/schema,$(DESTDIR)$(pkgdatadir), $(PGINTEROP_REGS)); \
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
	sh provider-register.sh -v -t @CIMSERVER@ -n @CIM_VIRT_NS@ -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(MOFS))
	sh provider-register.sh -v -t @CIMSERVER@ -n root/interop -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(INTEROP_REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(INTEROP_MOFS))
	sh provider-register.sh -v -t @CIMSERVER@ -n root/cimv2 -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(CIMV2_REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(CIMV2_MOFS))
	if [[ @CIMSERVER@ = pegasus ]]; then \
	sh provider-register.sh -v -t @CIMSERVER@ -n root/PG_InterOp -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(PGINTEROP_REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(PGINTEROP_MOFS)); \
	fi
	virsh -v | grep -q '^0.3' && cp examples/diskpool.conf $(DISK_POOL_CONFIG) || true
	mkdir -p $(INFO_STORE)

preuninstall:
	sh provider-register.sh -v -d -t @CIMSERVER@ -n @CIM_VIRT_NS@ -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(MOFS))
	sh provider-register.sh -v -d -t @CIMSERVER@ -n root/interop -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(INTEROP_REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(INTEROP_MOFS))
	sh provider-register.sh -v -d -t @CIMSERVER@ -n root/cimv2 -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(CIMV2_REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(CIMV2_MOFS))
	if [[ @CIMSERVER@ = pegasus ]]; then \
	sh provider-register.sh -v -d -t @CIMSERVER@ -n root/PG_InterOp -r $(subst $(top_srcdir)/schema,$(pkgdatadir), $(PGINTEROP_REGS)) -m $(subst $(top_srcdir)/schema,$(pkgdatadir), $(PGINTEROP_MOFS)); \
	fi

rpm: clean
	@(unset CDPATH ; $(MAKE) dist && rpmbuild -ta $(distdir).tar.gz)

clean-local:
	rm -f $(find . -name "*.orig")
	rm -f $(find . -name "*.rej")
	rm -f $(find . -name "*~")


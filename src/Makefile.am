# Copyright IBM Corp. 2007
SUBDIRS = tests

noinst_HEADERS = profiles.h svpc_types.h	\
    Virt_ComputerSystem.h			\
    Virt_ComputerSystemIndication.h		\
    Virt_Device.h				\
    Virt_DevicePool.h				\
    Virt_EnabledLogicalElementCapabilities.h	\
    Virt_HostSystem.h				\
    Virt_RASD.h					\
    Virt_RegisteredProfile.h			\
    Virt_ResourcePoolConfigurationService.h     \
    Virt_SettingsDefineCapabilities.h		\
    Virt_VirtualSystemManagementCapabilities.h	\
    Virt_VirtualSystemManagementService.h       \
    Virt_VSSD.h					\
    Virt_VSMigrationCapabilities.h              \
    Virt_VSMigrationService.h                   \
    Virt_AllocationCapabilities.h               \
    Virt_VirtualSystemSnapshotService.h         \
    Virt_VirtualSystemSnapshotServiceCapabilities.h \
    Virt_VSMigrationSettingData.h \
    Virt_ConsoleRedirectionService.h \
    Virt_ConsoleRedirectionServiceCapabilities.h \
    Virt_KVMRedirectionSAP.h

XKUADD = $(top_builddir)/libxkutil/libxkutil.la

CFLAGS += -I$(top_builddir)/libxkutil $(CFLAGS_STRICT)

AM_LDFLAGS = $(XKUADD)

providerdir = $(PROVIDERDIR)

# When adding new provider libraries, be sure place it in the list 
# after any dependant libraries.
provider_LTLIBRARIES = libVirt_ComputerSystem.la \
                       libVirt_Device.la \
                       libVirt_RASD.la \
                       libVirt_HostSystem.la \
                       libVirt_VSSD.la \
                       libVirt_EnabledLogicalElementCapabilities.la \
                       libVirt_DevicePool.la \
                       libVirt_RegisteredProfile.la \
                       libVirt_VSMigrationCapabilities.la \
                       libVirt_VSMigrationSettingData.la \
                       libVirt_VirtualSystemSnapshotServiceCapabilities.la \
                       libVirt_SystemDevice.la \
                       libVirt_ComputerSystemIndication.la \
                       libVirt_ComputerSystemMigrationIndication.la \
                       libVirt_VirtualSystemManagementCapabilities.la \
                       libVirt_AllocationCapabilities.la \
                       libVirt_ReferencedProfile.la \
                       libVirt_VirtualSystemSnapshotService.la \
                       libVirt_VirtualSystemManagementService.la \
                       libVirt_ResourcePoolConfigurationService.la \
                       libVirt_ResourcePoolConfigurationCapabilities.la \
                       libVirt_VSMigrationService.la \
                       libVirt_ConsoleRedirectionService.la \
                       libVirt_ConsoleRedirectionServiceCapabilities.la \
                       libVirt_KVMRedirectionSAP.la \
                       libVirt_SettingsDefineCapabilities.la \
                       libVirt_HostedDependency.la \
                       libVirt_ElementConformsToProfile.la \
                       libVirt_HostedResourcePool.la \
                       libVirt_ElementCapabilities.la \
                       libVirt_VSSDComponent.la \
                       libVirt_SettingsDefineState.la \
                       libVirt_ResourceAllocationFromPool.la \
                       libVirt_ElementAllocatedFromPool.la \
                       libVirt_HostedService.la \
                       libVirt_ElementSettingData.la \
                       libVirt_ConcreteComponent.la \
                       libVirt_ServiceAffectsElement.la \
                       libVirt_HostedAccessPoint.la \
                       libVirt_ServiceAccessBySAP.la

libVirt_ComputerSystem_la_SOURCES = Virt_ComputerSystem.c
libVirt_ComputerSystem_la_DEPENDENCIES = libVirt_VirtualSystemSnapshotService.la
libVirt_ComputerSystem_la_LIBADD = -lVirt_VirtualSystemSnapshotService
libVirt_Device_la_SOURCES = Virt_Device.c

libVirt_ComputerSystemIndication_la_DEPENDENCIES = libVirt_ComputerSystem.la libVirt_HostSystem.la
libVirt_ComputerSystemIndication_la_SOURCES = Virt_ComputerSystemIndication.c
libVirt_ComputerSystemIndication_la_LIBADD = -lVirt_ComputerSystem -lVirt_HostSystem -lpthread -lrt

libVirt_ComputerSystemMigrationIndication_la_DEPENDENCIES = libVirt_ComputerSystem.la
libVirt_ComputerSystemMigrationIndication_la_SOURCES = Virt_ComputerSystemMigrationIndication.c
libVirt_ComputerSystemMigrationIndication_la_LIBADD = -lVirt_ComputerSystem

libVirt_VirtualSystemManagementService_la_DEPENDENCIES = libVirt_ComputerSystem.la libVirt_ComputerSystemIndication.la libVirt_RASD.la libVirt_HostSystem.la libVirt_DevicePool.la libVirt_Device.la
libVirt_VirtualSystemManagementService_la_SOURCES = Virt_VirtualSystemManagementService.c
libVirt_VirtualSystemManagementService_la_LIBADD = -lVirt_ComputerSystem -lVirt_ComputerSystemIndication -lVirt_RASD -lVirt_HostSystem -lVirt_DevicePool -lVirt_Device

libVirt_VirtualSystemManagementCapabilities_la_DEPENDENCIES = libVirt_HostSystem.la
libVirt_VirtualSystemManagementCapabilities_la_SOURCES = Virt_VirtualSystemManagementCapabilities.c
libVirt_VirtualSystemManagementCapabilities_la_LIBADD = -lVirt_HostSystem

libVirt_SystemDevice_la_DEPENDENCIES = libVirt_ComputerSystem.la libVirt_Device.la
libVirt_SystemDevice_la_SOURCES = Virt_SystemDevice.c
libVirt_SystemDevice_la_LIBADD = -lVirt_ComputerSystem -lVirt_Device

libVirt_VSSD_la_SOURCES = Virt_VSSD.c
libVirt_HostSystem_la_SOURCES = Virt_HostSystem.c

libVirt_HostedDependency_la_DEPENDENCIES = libVirt_ComputerSystem.la libVirt_HostSystem.la
libVirt_HostedDependency_la_SOURCES = Virt_HostedDependency.c
libVirt_HostedDependency_la_LIBADD = -lVirt_ComputerSystem -lVirt_HostSystem

libVirt_DevicePool_la_SOURCES = Virt_DevicePool.c

libVirt_ElementConformsToProfile_la_DEPENDENCIES = libVirt_RegisteredProfile.la libVirt_HostSystem.la
libVirt_ElementConformsToProfile_la_SOURCES = Virt_ElementConformsToProfile.c
libVirt_ElementConformsToProfile_la_LIBADD = -lVirt_RegisteredProfile -lVirt_HostSystem

libVirt_ReferencedProfile_la_DEPENDENCIES = libVirt_RegisteredProfile.la
libVirt_ReferencedProfile_la_SOURCES = Virt_ReferencedProfile.c
libVirt_ReferencedProfile_la_LIBADD = -lVirt_RegisteredProfile

libVirt_EnabledLogicalElementCapabilities_la_SOURCES = Virt_EnabledLogicalElementCapabilities.c

libVirt_AllocationCapabilities_la_DEPENDENCIES = libVirt_DevicePool.la
libVirt_AllocationCapabilities_la_SOURCES = Virt_AllocationCapabilities.c
libVirt_AllocationCapabilities_la_LIBADD = -lVirt_DevicePool

libVirt_ElementCapabilities_la_DEPENDENCIES = libVirt_VirtualSystemManagementCapabilities.la libVirt_EnabledLogicalElementCapabilities.la libVirt_ComputerSystem.la libVirt_HostSystem.la libVirt_VSMigrationCapabilities.la libVirt_VirtualSystemManagementService.la libVirt_VSMigrationService.la libVirt_DevicePool.la libVirt_AllocationCapabilities.la libVirt_ConsoleRedirectionService.la libVirt_ConsoleRedirectionServiceCapabilities.la
libVirt_ElementCapabilities_la_SOURCES = Virt_ElementCapabilities.c
libVirt_ElementCapabilities_la_LIBADD = -lVirt_VirtualSystemManagementCapabilities \
                                        -lVirt_EnabledLogicalElementCapabilities \
                                        -lVirt_ComputerSystem \
                                        -lVirt_HostSystem \
                                        -lVirt_VSMigrationCapabilities \
					-lVirt_AllocationCapabilities \
                                        -lVirt_VirtualSystemManagementService \
                                        -lVirt_VSMigrationService \
                                        -lVirt_DevicePool \
                                        -lVirt_ConsoleRedirectionService \
                                        -lVirt_ConsoleRedirectionServiceCapabilities

libVirt_SettingsDefineCapabilities_la_DEPENDENCIES = libVirt_RASD.la libVirt_DevicePool.la libVirt_VSMigrationCapabilities.la libVirt_VSMigrationSettingData.la libVirt_AllocationCapabilities.la
libVirt_SettingsDefineCapabilities_la_SOURCES = Virt_SettingsDefineCapabilities.c
libVirt_SettingsDefineCapabilities_la_LIBADD = -lVirt_RASD \
                                               -lVirt_DevicePool \
                                               -lVirt_VSMigrationCapabilities \
                                               -lVirt_VSMigrationSettingData \
                                               -lVirt_AllocationCapabilities

libVirt_RegisteredProfile_la_SOURCES = Virt_RegisteredProfile.c

libVirt_HostedResourcePool_la_DEPENDENCIES = libVirt_DevicePool.la libVirt_HostSystem.la
libVirt_HostedResourcePool_la_SOURCES = Virt_HostedResourcePool.c
libVirt_HostedResourcePool_la_LIBADD = -lVirt_DevicePool -lVirt_HostSystem

libVirt_RASD_la_DEPENDENCIES = libVirt_Device.la
libVirt_RASD_la_SOURCES = Virt_RASD.c
libVirt_RASD_la_LIBADD = -lVirt_Device

libVirt_ResourcePoolConfigurationService_la_DEPENDENCIES = libVirt_HostSystem.la
libVirt_ResourcePoolConfigurationService_la_SOURCES = Virt_ResourcePoolConfigurationService.c
libVirt_ResourcePoolConfigurationService_la_LIBADD = -lVirt_HostSystem

libVirt_ResourcePoolConfigurationCapabilities_la_SOURCES = Virt_ResourcePoolConfigurationCapabilities.c

libVirt_VSSDComponent_la_DEPENDENCIES = libVirt_VSSD.la libVirt_RASD.la
libVirt_VSSDComponent_la_SOURCES = Virt_VSSDComponent.c
libVirt_VSSDComponent_la_LIBADD = -lVirt_VSSD -lVirt_RASD

libVirt_SettingsDefineState_la_DEPENDENCIES = libVirt_Device.la libVirt_RASD.la libVirt_VSSD.la libVirt_ComputerSystem.la
libVirt_SettingsDefineState_la_SOURCES = Virt_SettingsDefineState.c
libVirt_SettingsDefineState_la_LIBADD = -lVirt_Device -lVirt_RASD -lVirt_VSSD -lVirt_ComputerSystem

libVirt_ResourceAllocationFromPool_la_DEPENDENCIES = libVirt_DevicePool.la libVirt_RASD.la
libVirt_ResourceAllocationFromPool_la_SOURCES = Virt_ResourceAllocationFromPool.c
libVirt_ResourceAllocationFromPool_la_LIBADD = -lVirt_DevicePool -lVirt_RASD

libVirt_ElementAllocatedFromPool_la_DEPENDENCIES = libVirt_DevicePool.la libVirt_Device.la
libVirt_ElementAllocatedFromPool_la_SOURCES = Virt_ElementAllocatedFromPool.c
libVirt_ElementAllocatedFromPool_la_LIBADD = -lVirt_DevicePool -lVirt_Device

libVirt_HostedService_la_DEPENDENCIES = libVirt_VirtualSystemManagementService.la libVirt_ResourcePoolConfigurationService.la libVirt_VSMigrationService.la libVirt_HostSystem.la libVirt_ConsoleRedirectionService.la
libVirt_HostedService_la_SOURCES = Virt_HostedService.c
libVirt_HostedService_la_LIBADD = -lVirt_VirtualSystemManagementService -lVirt_ResourcePoolConfigurationService -lVirt_VSMigrationService -lVirt_HostSystem -lVirt_ConsoleRedirectionService

libVirt_ElementSettingData_la_DEPENDENCIES = libVirt_VSSD.la libVirt_RASD.la
libVirt_ElementSettingData_la_SOURCES = Virt_ElementSettingData.c
libVirt_ElementSettingData_la_LIBADD = -lVirt_VSSD -lVirt_RASD

libVirt_VSMigrationCapabilities_la_SOURCES = Virt_VSMigrationCapabilities.c

libVirt_VSMigrationService_la_DEPENDENCIES = libVirt_HostSystem.la libVirt_VSMigrationSettingData.la libVirt_ComputerSystem.la
libVirt_VSMigrationService_la_SOURCES = Virt_VSMigrationService.c
libVirt_VSMigrationService_la_LIBADD = -lVirt_HostSystem -lVirt_VSMigrationSettingData -lVirt_ComputerSystem

libVirt_VSMigrationSettingData_la_SOURCES = Virt_VSMigrationSettingData.c

libVirt_VirtualSystemSnapshotService_la_DEPENDENCIES = libVirt_HostSystem.la
libVirt_VirtualSystemSnapshotService_la_SOURCES = Virt_VirtualSystemSnapshotService.c
libVirt_VirtualSystemSnapshotService_la_LIBADD = -lVirt_HostSystem

libVirt_VirtualSystemSnapshotServiceCapabilities_la_DEPENDENCIES = 
libVirt_VirtualSystemSnapshotServiceCapabilities_la_SOURCES = Virt_VirtualSystemSnapshotServiceCapabilities.c
libVirt_VirtualSystemSnapshotServiceCapabilities_la_LIBADD =

libVirt_ConcreteComponent_la_DEPENDENCIES = libVirt_HostSystem.la libVirt_DevicePool.la
libVirt_ConcreteComponent_la_SOURCES = Virt_ConcreteComponent.c
libVirt_ConcreteComponent_la_LIBADD = -lVirt_HostSystem -lVirt_DevicePool

libVirt_ConsoleRedirectionService_la_DEPENDENCIES = libVirt_HostSystem.la
libVirt_ConsoleRedirectionService_la_SOURCES = Virt_ConsoleRedirectionService.c
libVirt_ConsoleRedirectionService_la_LIBADD = -lVirt_HostSystem

libVirt_ConsoleRedirectionServiceCapabilities_la_SOURCES = Virt_ConsoleRedirectionServiceCapabilities.c

libVirt_ServiceAffectsElement_la_DEPENDENCIES = libVirt_ComputerSystem.la libVirt_ConsoleRedirectionService.la
libVirt_ServiceAffectsElement_la_SOURCES = Virt_ServiceAffectsElement.c
libVirt_ServiceAffectsElement_la_LIBADD = -lVirt_ComputerSystem -lVirt_ConsoleRedirectionService

libVirt_KVMRedirectionSAP_la_SOURCES = Virt_KVMRedirectionSAP.c

libVirt_ServiceAccessBySAP_la_DEPENDENCIES = libVirt_ConsoleRedirectionService.la libVirt_KVMRedirectionSAP.la
libVirt_ServiceAccessBySAP_la_SOURCES = Virt_ServiceAccessBySAP.c
libVirt_ServiceAccessBySAP_la_LIBADD = -lVirt_ConsoleRedirectionService -lVirt_KVMRedirectionSAP

libVirt_HostedAccessPoint_la_DEPENDENCIES = libVirt_HostSystem.la libVirt_KVMRedirectionSAP.la
libVirt_HostedAccessPoint_la_SOURCES = Virt_HostedAccessPoint.c
libVirt_HostedAccessPoint_la_LIBADD = -lVirt_HostSystem -lVirt_KVMRedirectionSAP


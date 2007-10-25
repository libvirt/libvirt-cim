# Copyright IBM Corp. 2007
SUBDIRS = tests

noinst_HEADERS = Virt_ElementConformsToProfile.h profiles.h

XKUADD = $(top_builddir)/libxkutil/libxkutil.la

CFLAGS += -I$(top_builddir)/libxkutil $(CFLAGS_STRICT)

AM_LDFLAGS = $(XKUADD)

providerdir = @PROVIDERDIR@

provider_LTLIBRARIES = libVirt_ComputerSystem.la \
                       libVirt_Device.la \
                       libVirt_SystemDevice.la \
                       libVirt_ComputerSystemIndication.la \
                       libVirt_RASD.la \
                       libVirt_VirtualSystemManagementService.la \
                       libVirt_VirtualSystemManagementCapabilities.la \
                       libVirt_EnabledLogicalElementCapabilities.la \
                       libVirt_AllocationCapabilities.la \
                       libVirt_SettingsDefineCapabilities.la \
                       libVirt_VSSD.la \
                       libVirt_HostSystem.la \
                       libVirt_HostedDependency.la \
                       libVirt_RegisteredProfile.la \
                       libVirt_ElementConformsToProfile.la \
                       libVirt_DevicePool.la \
                       libVirt_HostedResourcePool.la \
                       libVirt_ElementCapabilities.la \
                       libVirt_ResourcePoolConfigurationService.la \
                       libVirt_ResourcePoolConfigurationCapabilities.la \
                       libVirt_VSSDComponent.la \
                       libVirt_SettingsDefineState.la \
                       libVirt_ResourceAllocationFromPool.la \
                       libVirt_ElementAllocatedFromPool.la

libVirt_ComputerSystem_la_SOURCES = Virt_ComputerSystem.c
libVirt_Device_la_SOURCES = Virt_Device.c

libVirt_ComputerSystemIndication_la_SOURCES = Virt_ComputerSystemIndication.c
libVirt_ComputerSystemIndication_la_LIBADD = -lVirt_ComputerSystem -lpthread -lrt

libVirt_VirtualSystemManagementService_la_SOURCES = Virt_VirtualSystemManagementService.c
libVirt_VirtualSystemManagementService_la_LIBADD = -lVirt_ComputerSystem -lVirt_ComputerSystemIndication -lVirt_RASD

libVirt_VirtualSystemManagementCapabilities_la_SOURCES = Virt_VirtualSystemManagementCapabilities.c

libVirt_SystemDevice_la_SOURCES = Virt_SystemDevice.c
libVirt_SystemDevice_la_LIBADD = -lVirt_ComputerSystem -lVirt_Device

libVirt_VSSD_la_SOURCES = Virt_VSSD.c
libVirt_HostSystem_la_SOURCES = Virt_HostSystem.c

libVirt_HostedDependency_la_SOURCES = Virt_HostedDependency.c
libVirt_HostedDependency_la_LIBADD = -lVirt_ComputerSystem -lVirt_HostSystem

libVirt_DevicePool_la_SOURCES = Virt_DevicePool.c

libVirt_ElementConformsToProfile_la_SOURCES = Virt_ElementConformsToProfile.c
libVirt_ElementConformsToProfile_la_LIBADD = -lVirt_RegisteredProfile

libVirt_EnabledLogicalElementCapabilities_la_SOURCES = Virt_EnabledLogicalElementCapabilities.c

libVirt_ElementCapabilities_la_SOURCES = Virt_ElementCapabilities.c
libVirt_ElementCapabilities_la_LIBADD = -lVirt_VirtualSystemManagementCapabilities \
                                        -lVirt_EnabledLogicalElementCapabilities \
                                        -lVirt_ComputerSystem \
                                        -lVirt_HostSystem

libVirt_AllocationCapabilities_la_SOURCES = Virt_AllocationCapabilities.c

libVirt_SettingsDefineCapabilities_la_SOURCES = Virt_SettingsDefineCapabilities.c

libVirt_RegisteredProfile_la_SOURCES = Virt_RegisteredProfile.c

libVirt_HostedResourcePool_la_SOURCES = Virt_HostedResourcePool.c
libVirt_HostedResourcePool_la_LIBADD = -lVirt_DevicePool -lVirt_HostSystem

libVirt_RASD_la_SOURCES = Virt_RASD.c

libVirt_ResourcePoolConfigurationService_la_SOURCES = Virt_ResourcePoolConfigurationService.c
libVirt_ResourcePoolConfigurationService_la_LIBADD = -lVirt_HostSystem

libVirt_ResourcePoolConfigurationCapabilities_la_SOURCES = Virt_ResourcePoolConfigurationCapabilities.c

libVirt_VSSDComponent_la_SOURCES = Virt_VSSDComponent.c
libVirt_VSSDComponent_la_LIBADD = -lVirt_VSSD -lVirt_RASD

libVirt_SettingsDefineState_la_SOURCES = Virt_SettingsDefineState.c
libVirt_SettingsDefineState_la_LIBADD = -lVirt_Device -lVirt_RASD -lVirt_VSSD -lVirt_ComputerSystem

libVirt_ResourceAllocationFromPool_la_SOURCES = Virt_ResourceAllocationFromPool.c
libVirt_ResourceAllocationFromPool_la_LIBADD = -lVirt_DevicePool -lVirt_RASD

libVirt_ElementAllocatedFromPool_la_SOURCES = Virt_ElementAllocatedFromPool.c
libVirt_ElementAllocatedFromPool_la_LIBADD = -lVirt_DevicePool -lVirt_Device

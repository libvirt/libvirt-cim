CIM_SCHEMA_VER = 2.21.0
CIM_SCHEMA_DIR = 2210
CIM_SCHEMA_REL = $(CIM_SCHEMA_VER)Experimental
CIM_SCHEMA_ZIP = cim_schema_$(CIM_SCHEMA_REL)-MOFs.zip

dist_pkgdata_DATA = $(CIM_SCHEMA_ZIP) cimv2.21.0-interop_mof \
                    cimv2.21.0-cimv2_mof 
dist_pkgdata_SCRIPTS = install_base_schema.sh

$(CIM_SCHEMA_ZIP):
	wget http://www.dmtf.org/standards/cim/cim_schema_v$(CIM_SCHEMA_DIR)/$(CIM_SCHEMA_ZIP)

EXTRA_DIST = README.DMTF

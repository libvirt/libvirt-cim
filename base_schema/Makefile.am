CIM_SCHEMA_VER = 216
CIM_SCHEMA_REL = $(CIM_SCHEMA_VER)Experimental
CIM_SCHEMA_ZIP = cimv$(CIM_SCHEMA_REL)-MOFs.zip

dist_pkgdata_DATA = $(CIM_SCHEMA_ZIP) fix_schema.patch cimv216-interop_mof \
                    cimv216-cimv2_mof
dist_pkgdata_SCRIPTS = install_base_schema.sh

$(CIM_SCHEMA_ZIP):
	wget http://www.dmtf.org/standards/cim/cim_schema_v$(CIM_SCHEMA_VER)/$(CIM_SCHEMA_ZIP)

EXTRA_DIST = README.DMTF

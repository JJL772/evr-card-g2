# Load RUCKUS library
source -quiet $::env(RUCKUS_DIR)/vivado_proc.tcl

# Load Source Code
loadSource -dir  "$::DIR_PATH/rtl"
# loadIpCore -path "$::DIR_PATH/coregen/TenGigEthGth7Core.xci"
loadSource -path "$::DIR_PATH/coregen/TenGigEthGth7Core.dcp"

# Makefile
# See https://docs.cocotb.org/en/stable/quickstart.html for more info

# defaults
SIM ?= icarus
FST ?= -fst # Use more efficient FST format
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(PWD)/../src
PROJECT_SOURCES = project.v cpu.v

ifneq ($(GATES),yes)

# RTL simulation:
SIM_BUILD				= sim_build/rtl
# make MISO_RISE=yes: strap MISO_RISE = 1 and a RAM with stock spi-ram-emu timing
ifeq ($(MISO_RISE),yes)
SIM_BUILD				= sim_build/rtl_rise
COMPILE_ARGS    += -DMISO_RISE
endif
VERILOG_SOURCES += $(addprefix $(SRC_DIR)/,$(PROJECT_SOURCES))

else

# Gate level simulation:
SIM_BUILD				= sim_build/gl
ifeq ($(MISO_RISE),yes)
SIM_BUILD				= sim_build/gl_rise
COMPILE_ARGS    += -DMISO_RISE
endif
COMPILE_ARGS    += -DGL_TEST
ifeq ($(SDF),)
COMPILE_ARGS    += -DFUNCTIONAL
else
# Timing simulation: make GATES=yes SDF=path/to/corner.sdf [CLK_NS=40]
# The cells use their delay (specify) blocks, filled in from the SDF file
# that hardening writes for each corner (runs/wokwi/final/sdf/<corner>/).
# The SDF file must come from the same run as gate_level_netlist.v.
SIM_BUILD       := $(SIM_BUILD)_sdf
COMPILE_ARGS    += -gspecify -ginterconnect
COMPILE_ARGS    += -DSDF_FILE=\"$(abspath $(SDF))\"
endif
COMPILE_ARGS    += -DUSE_POWER_PINS
COMPILE_ARGS    += -DSIM
COMPILE_ARGS    += -DUNIT_DELAY=\#1
VERILOG_SOURCES += $(PDK_ROOT)/gf180mcuD/libs.ref/gf180mcu_fd_sc_mcu7t5v0/verilog/primitives.v
VERILOG_SOURCES += $(PDK_ROOT)/gf180mcuD/libs.ref/gf180mcu_fd_sc_mcu7t5v0/verilog/gf180mcu_fd_sc_mcu7t5v0.v

# this gets copied in by the GDS action workflow
VERILOG_SOURCES += $(PWD)/gate_level_netlist.v

# empty module for the silicon-art macro instance in the netlist
VERILOG_SOURCES += $(PWD)/../macros/unisc_logo_stub.v

endif

# Allow sharing configuration between design and testbench via `include`:
COMPILE_ARGS 		+= -I$(SRC_DIR)

# Include the testbench sources (SPI RAM model is sim/FPGA only, not part of the chip):
VERILOG_SOURCES += $(PWD)/../model/spi_ram_model.v
VERILOG_SOURCES += $(PWD)/tb.v
TOPLEVEL = tb

# List test modules to run, separated by commas and without the .py suffix.
# test_programs runs every programs/*.asm and compares it with the C
# simulator. It only looks at the chip's pins and the testbench RAM, so it
# also runs on the gate-level netlist (make GATES=yes), where it takes a
# few minutes. MODULES=test runs only the short directed tests.
MODULES ?= test,test_programs
COCOTB_TEST_MODULES = $(MODULES)

# The tests call the C assembler and simulator
$(shell $(MAKE) -s -C $(PWD)/../tools cpu8asm cpu8sim >&2)

# include cocotb's make rules to take care of the simulator setup
include $(shell cocotb-config --makefiles)/Makefile.sim

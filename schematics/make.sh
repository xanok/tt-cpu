#!/bin/sh
# Make the schematic pages again, after a change to the Verilog.
# Needs vlogview on the PATH, built with --minify support. Run from anywhere:
#   schematics/make.sh
# --editor none: the pages carry no links to a local editor and no local paths.
# --minify: the viewer code in the pages is compressed.
set -e
cd "$(dirname "$0")/.."
V="vlogview -q --editor none --minify"
CORE="src/project.v src/cpu.v"
$V -o schematics/chip.html $CORE
$V -o schematics/tangnano20k.html fpga/tangnano20k.v fpga/uart_in.v fpga/uart_out.v $CORE model/spi_ram_model.v
$V -o schematics/tangnano20k_chip.html fpga/tangnano20k_chip.v fpga/uart_in.v fpga/uart_out.v $CORE model/spi_ram_model.v
$V -o schematics/testbench.html test/tb.v $CORE model/spi_ram_model.v
if [ -f test/gate_level_netlist.v ]; then
    $V -o schematics/gate_level.html test/gate_level_netlist.v
else
    echo "test/gate_level_netlist.v is not here (it comes from a hardening run): gate_level.html is left as it is"
fi
ls -l schematics/*.html
python3 schematics/pictures.py || true        # the SVG pictures in docs/schematics/ (needs Chrome)

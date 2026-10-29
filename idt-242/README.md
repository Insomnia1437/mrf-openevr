Use IDT Timing Commander to generate program data for IDT 8T49N242
clock synthesizer.

Then create a zynq PS platform with UART and AXI IIC slave enabled.

Use `main.c` in vitis to read/write IDT-242 program data from/to EEPROM.

- 8T49N242 Ioutput Q2 -> MGTREFCLK1, 114.24 MHz
- 8T49N242 Ioutput Q3 -> PL_CLK, 35.7 MHz


Verify Q3 output by conncting oscilloscope to TP31 on FMC Carrier
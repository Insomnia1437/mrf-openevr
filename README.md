# mrf-openevr

- Support Vivado 2025.2.1
- Support configurable size data buffer

### How to use 

Download Avnet board files from https://github.com/Avnet/bdf

Add Avnet board files to  `C:\Users\USER\AppData\Roaming\Xilinx\Vivado\2025.2.1\xhub\board_store\xilinx_board_store\XilinxBoardStore\Vivado\2025.2.1\boards`

For linux, path is: `/opt/Xilinx/2026.1/Vivado/data/boards/board_files`


--------------Original README----------------

Open source Event Receiver implementation

Prerequisites

Hardware

Avnet PicoZed 7Z030 Module P/N AES-Z7PZ-7Z030-SOM-G

Avnet PicoZed FMC Carrier Card V2 P/N AES-PZCC-FMC-V2-G

Software Tools

Xilinx Vivado 2017.4 (Free WebPack version is sufficient to build to project)

Reference clock programming on FMC carrier

http://picozed.org/support/design/13076/106

Building the exmaple design

git clone https://github.com/jpietari/mrf-openevr

vivado -mode tcl

%vivado source ./openevr.tcl

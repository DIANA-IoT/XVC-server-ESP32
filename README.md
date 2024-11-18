# ESP32 Bitbang Debugger

## About
This repository contains a self-sufficient ESP-IDF project to implement an
XVC TCP bitbang driver. <br>
The XVC or [Xilinx Virtual Cable](https://www.xilinx.com/content/dam/xilinx/support/documents/application_notes/xapp1251-xvc-zynq-petalinux.pdf), uses a quite simple message-driven protocol to bitbang JTAG targets.

Related work: <br>
Cano García J.M, Castillo Sánchez J.B, González Parada E. *[Low-cost JTAG debugger with Wi-Fi interface](https://ieeexplore.ieee.org/document/9840601)* -2022

### GPIO Pins Used
| Pin 	| JTAG_PIN |
| ---- 	| -------- |
| GND	| GND	   |
| 12 	| TDO	   |
| 13 	| TCK	   |
| 14 	| TMS	   |
| 15 	| TDI	   |

The code is tested on an ESP32 board (ESP32-DevKitC-V4) using ESP-IDF v5.3.

## Authors
- J. Borja Castillo <joscassan@uma.es> Universidad de Malaga, Spain.<br>
- Jose M. Cano <jcgarcia@uma.es> Universidad de Malaga, Spain.<br>
- Eva Gonzalez <gonzalez@uma.es> Universidad de Malaga, Spain. <br>

All members are affiliated at [University of Malaga](https://www.uma.es/), department of Electronic Techonology and [Malaga Telecommunications Research Institute](https://www.telma.uma.es/).

## Copyright and License

This software is distributed under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

This package is at Copyright (c) University of Malaga. This copyright information is specified in the headers of the corresponding files.

## Acknowledgements

This work has been supported by project P18-RT-1652, funded by Junta de Andalucía. Addiontal funding was provided <br> by project TED2021- 130456B-I00, funded by MCIN/AEI/10.13039/501100011033 and EU ”NextGenerationEU/PRTR” <br> program and the Malaga University project B4-2023-12.

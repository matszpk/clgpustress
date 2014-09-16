clgpustress
===========

Heavy OpenCL GPU stress tester

### IMPORTANT CAUTION!!!!!

THIS PROGRAM IS VERY DANGEROUS FOR GRAPHICS CARD AND CAN OVERHEAT OR DAMAGE YOUR GRAPHICS CARD! PLEASE USE CAREFULLY
THIS PROGRAM. I RECOMMENDED TO RUN THIS PROGRAM ON ALL STOCK PARAMETERS (CLOCKS, VOLTAGES, ESPECIALLY GPU MEMORY CLOCK).

THIS PROGRAM IS INFLUENCED BY PRIMEGRID GENEFER AND ALSO VERY SENSITIVE FOR ANY OVERCLOCKING, BUT MUCH BETTER LOADS GPU CORE. MOREOVER MUCH BETTER BURNS GRAPHICS CARD THAN FURMARK!

THIS PROGRAM WAS TESTED ONLY IN RADEON HD 7850 AND CAN BEHAVES INCORRECTLY ON OTHER GRAPHICS CARDS.

YOU ARE USING THIS SOFTWARE ONLY ON OWN RISK!

### Software requirements:

- popt library
- OpenCL support
- cl.hpp (OpenCL C++ support)
- compiler with C++11 support
- Linux (currently this system is supported).

### Building program

Enter command:

make

To clean project enter command:

make clean

AMDAPP variable defined in Makefile may be changed for successful compilation. If you have AMDAPP or OPENCL directory
in other place than /opt/AMDAPP you must change AMDAPP variable in Makefile file.

### Memory requirements

Program prints size of memory required in GPU (device) memory.
Option '-I' choose standard method with decoupled input and output which requires
double size of memory on device.
Program needs also host memory: 192*workSize bytes for buffers.

### Usage

- print help: ./gpustress -?

- run stress: ./gpustress -G -W512 -S32 -B2 -T0

If option '-j' is not specified then program automatically calibrate test for device.

## supported tests

Now, tests has been built in program.

Tests list:

- 0 - standard with local memory checking
- 1 - standard without local memory checking
- 2 - polynomial walking

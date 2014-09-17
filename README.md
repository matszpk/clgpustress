clgpustress
===========

Heavy OpenCL GPU stress tester

### IMPORTANT CAUTION!!!!!

THIS PROGRAM IS VERY DANGEROUS FOR GRAPHICS CARD AND CAN OVERHEAT OR DAMAGE YOUR GRAPHICS CARD! PLEASE USE CAREFULLY
THIS PROGRAM. I RECOMMEND TO RUN THIS PROGRAM ON ALL STOCK PARAMETERS (CLOCKS, VOLTAGES, ESPECIALLY GPU MEMORY CLOCK).

THIS PROGRAM IS INFLUENCED BY PRIMEGRID GENEFER AND ALSO IS VERY SENSITIVE FOR ANY OVERCLOCKING,
BUT MUCH BETTER LOADS GPU CORE. MOREOVER MUCH BETTER BURNS GRAPHICS CARD THAN FURMARK!

THIS PROGRAM WAS TESTED ONLY ON RADEON HD 7850 AND CAN BEHAVES INCORRECTLY ON OTHER GRAPHICS CARDS.

YOU ARE USING THIS SOFTWARE ONLY ON YOUR OWN RISK!

### Program behaviour

By default program find ALL GPU devices and run stress for them. You can choose particular
device with using '-L' option. By default program calibrate test for performance and memory
bandwidth. While running tests program checks result with previously computed results on the device.
If results mismatches program terminates stress test for failed device.
By default program terminates stress testing when any device fails. You can adds '-f' option
to force continue stress testing of other devices.

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
Option '-I' chooses standard method with decoupled input and output which requires
double size of memory on the device.

Program needs also host memory: 192 * blocksNum * workSize bytes for buffers.

### Usage

- print help: ./gpustress -?

- run stress: ./gpustress -G -W512 -S32 -B2 -T0

If option '-j' is not specified then program automatically calibrates test for device for performance and memory bandwidth.

## supported tests

Now, tests has been built in program.

Tests list:

- 0 - standard with local memory checking
- 1 - standard without local memory checking
- 2 - polynomial walking

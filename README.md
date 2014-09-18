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

By default program finds ALL GPU devices from all platforms and runs stress for them.
You can choose particular device with using '-L' option. Also you can select OpenCL platform
by using '-A', '-N' or '-E' options (also, you can combine these options to select many platforms).
By default program calibrates test for performance and memory
bandwidth. While running tests program checks result with previously computed results on the device.
If results mismatches program terminates stress test for failed device.
By default program terminates stress testing when any device fails. You can adds '-f' option
to force continue stress testing for other devices.

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

Examples of usage:

- print help: ./gpustress -?

- simplest usage: ./gpustress

- run stress: ./gpustress -G -W512 -S32 -B2 -T0

- run stress only on AMD devices: ./gpustress -A

- run stress only on NVIDIA devices: ./gpustress -N

- run stress only on Intel devices: ./gpustress -E

- run stress only on first device from first platform: ./gpustress -L 0:0

- run stress only on second device from second platform: ./gpustress -L 1:1

If option '-j' is not specified then program automatically calibrates test for device for performance and memory bandwidth.

#### Specifiyng devices to testing:

GPUStress provides simple method to select devices. To print all available devices you can
use '-l' option:

./gpustress -l

gpustress prints all OpenCL devices, also prints their the platform id and the device id.

GPUStress allows to select devices from specified the OpenCL platform, by using following options:

- '-A' - choose only devices from AMD (AMDAPP) platform

- '-N' - choose only devices from NVIDIA (NVIDIA CUDA) platform

- '-E' - choose only devices from Intel platform

You can combime these options to choose many platforms.

Moreover gpustress allows to choose devices of particular type:

- '-C' - choose only CPU devices

- '-G' - choose only GPU devices

- '-a' - choose only accelerators

You can combine these option to choose devices of many types.

The custom devices are not supported, because doesn't supports for the OpenCL compiler.

Moreover, you can choose a particular devices from a particular platforms with using option '-L'.
Parameter of this option is comma-separated list of the platform id and the device id
separated by using colon. Following example:

./gpustress -L 0:0,1:1,1:2,1:3

choose first device from first platform; second,third,fourth device from second platform.

### supported tests

Now, tests has been built in program.

Tests list:

- 0 - standard with local memory checking
- 1 - standard without local memory checking
- 2 - polynomial walking

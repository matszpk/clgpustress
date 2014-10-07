clgpustress
===========

Heavy OpenCL GPU stress tester (version 0.0.8.7)

### IMPORTANT CAUTION!!!!!

**THIS PROGRAM IS VERY DANGEROUS FOR GRAPHICS CARD AND CAN OVERHEAT OR DAMAGE YOUR GRAPHICS CARD!
PLEASE USE CAREFULLY THIS PROGRAM. I RECOMMEND TO RUN THIS PROGRAM ON ALL STOCK PARAMETERS
OF THE DEVICES (CLOCKS, VOLTAGES, ESPECIALLY GPU MEMORY CLOCK).**

**THIS PROGRAM IS INFLUENCED BY PRIMEGRID GENEFER AND ALSO IS VERY SENSITIVE FOR ANY OVERCLOCKING,
BUT MUCH BETTER LOADS GPU CORE. MOREOVER MUCH BETTER BURNS GRAPHICS CARD THAN FURMARK!**

**THIS PROGRAM WAS TESTED ONLY ON RADEON HD 7850 AND CAN BEHAVES INCORRECTLY ON OTHER GRAPHICS CARDS.**

**YOU ARE USING THIS SOFTWARE ONLY AT YOUR OWN RISK!**

### Binaries, sources and website

Project website: [http://clgpustress.nativeboinc.org](http://clgpustress.nativeboinc.org).

Binaries are at [http://files.nativeboinc.org/offtopic/clgpustress/](http://files.nativeboinc.org/offtopic/clgpustress/)
and at
[http://clgpustress.nativeboinc.org/downloads.html](http://clgpustress.nativeboinc.org/downloads.html).

Source code packages are at [http://clgpustress.nativeboinc.org/downloads.html](http://clgpustress.nativeboinc.org/downloads.html) and
at [https://github.com/matszpk/clgpustress](https://github.com/matszpk/clgpustress).

Source codes are at [https://github.com/matszpk/clgpustress](https://github.com/matszpk/clgpustress).

### License

Program is distributed under the GPLv2 license.
For more information please read LICENSE file inside the program package.

### Program behaviour

By default program finds ALL GPU devices from all platforms and runs stress for them.
You can choose particular device with using '-L' or '--devicesList' option.
Also you can select OpenCL platform by using '-A', '-N' or '-E' options
(also, you can combine these options to select many platforms).
By default program calibrates test for performance and memory
bandwidth. While running tests program checks result with previously computed results on the device.
If results mismatches program terminates stress test for failed device.
By default program terminates stress testing when any device will fail. You can add
'-f' or '--exitIfAllFails' option to force continue stress testing for other devices.

### Program version

- Version with command-line interface is named as 'gpustress-cli'.
- Version with GUI is named as 'gpustress-gui'.

### Software requirements:

- Windows or Linux operating system in x86 or x86-64 version (for binaries)
- Windows 7 or later (recommended for binaries)
- Linux with libstdc++ from GCC 4.8 or later (recommended for binaries)
- OpenCL 1.1 or later (from GPU vendor drivers or other compatible)
- OpenCL 1.1 C++ binding later (for compilation)
- popt library (only for Linux version or for compilation)
- C++11 compliant compiler or Microsoft Visual Studio Express 2012 (for compilation)
- C++11 compliant C++ libraries (only for Linux)
- FLTK 1.3 for GUI version (for compilation, binaries are statically linked with FLTK)

### Building program

Before any building you must specify the OpenCL root path
(path where are libraries and includes of the OpenCL).
An OpenCL root path is defined as 'OPENCLDIR' variable in the Makefile.

NOTE: If you have 64-bit Linux system you must also correct 'LIBDIRS' in the Makefile to
'-L$(OPENCLDIR)/lib64'.

Enter command:

make

To clean project enter command:

make clean

### Memory requirements

Program prints size of memory required in the device memory.
Standard tests requires 64 * blocksNum * workFactor * maxComputeUnits * workGroupSize bytes in
device memory. By default program choose workGroupSize = maxWorkGroupSize.

You can get maxComputeUnits and maxWorkGroupSize from 'clinfo' or from other
OpenCL diagnostics utility. 

The '-I' (or '--inAndOut') option chooses standard method with decoupled input and output
which requires double size of memory on the device.
By default program uses single buffer for input and output.

Program needs also host memory: 192 * blocksNum * workSize bytes for buffers.

### Usage

Examples of usage:

- print help: ./gpustress -?
- simplest usage: ./gpustress
- run stress: ./gpustress -G -W512 -S32 -B2 -T0
- run stress only on AMD devices: ./gpustress -A
- run stress only on NVIDIA devices: ./gpustress-cli -N
- run stress only on Intel devices: ./gpustress-cli -E
- run stress only on first device from first platform: ./gpustress-cli -L 0:0
- run stress only on second device from second platform: ./gpustress-cli -L 1:1

If option '-j' is not specified then program automatically calibrates
test for device for performance and memory bandwidth.

#### Supported tests

Currently gpustress has 3 tests:

- 0 - standard with local memory checking (for Radeon HD 7850 the most effective test)
- 1 - standard without local memory checking
- 2 - polynomial walking (for Radeon HD 7850 the less effective)
- 3 - polynomial walking with local memory (for Radeon HD 7850 the less effective)

#### Parameters for the tests

Now you can specify following parameters for tests:

- workFactor - controls work size: (workitems number: workFactor * maxComputeUnits * workGroupSize)
- blocksNum - number of blocks processed by single workitem (can be in 1-16)
- passIters - number of iterations of the execution kernel in single pass
- kitersNum - number of iteration of core computation within single memory access
- inputAndOutput - enables input/output mode
- testType - test (builtin kernel) (0-2). tests are described in supported tests section
- groupSize - work group size (by default or if zero, program chooses maxWorkGroupSize)

You can choose these parameter by using following options:

- '-W' or '--workFactor' - workFactor
- '-B' or '--blocksNum' - blocksNum
- '-S' or '--passIters' - passIters
- '-j' or '--kitersNum' - kitersNum
- '-T' or '--testType' - test type (builtin kernel)
- '-g' or '--groupSize' - groupSize

For groupSize, if value is zero or is not specified then program
chooses maxWorkGroupSize for device.
For kitersNum, if value is zero of is not specified then program
calibates kernel for a memory bandwidth and a performance.

#### Specifiyng devices to testing:

GPUStress provides simple method to select devices. To print all available devices you can
use '-l' option:

./gpustress-cli -l

GPUstress prints all OpenCL devices, also prints their the platform id and the device id.

GPUStress allows to select devices from specified the OpenCL platform, by using following options:

- '-A' or '--useAMD' - choose devices from AMD (AMDAPP) platform
- '-N' or '--useNVIDIA' - choose devices from NVIDIA (NVIDIA CUDA) platform
- '-E' or '--useIntel' - choose devices from Intel platform

You can combime these options to choose many platforms.
By default gpustress chooses devices from all  platforms.

Moreover gpustress allows to choose devices of particular type:

- '-C' or '--useCPUs' - choose CPU devices
- '-G' or '--useGPUs' - choose GPU devices
- '-a' or '--useAccs' - choose accelerators

You can combine these options to choose devices of many types.
By default gpustresss chooses only GPU devices.

The custom devices are not supported, because doesn't supports for the OpenCL compiler.

Moreover, you can choose a particular devices from a particular platforms with using
'-L' or '--devicesList' option. Parameter of this option is comma-separated list of
the platform id and the device id separated by using colon. Following example:

./gpustress-cli -L 0:0,1:1,1:2,1:3

chooses first device from first platform; second,third,fourth device from second platform.

#### Specifying configuration for particular devices

In easiest way, you can choose one value for all devices by providing a single value.

You can choose different values for particular devices for following parameters:
workFactor, blocksNum, passItersNum, kitersNum, testType, inputAndOutput.
Values are in list that is comma separated, excepts inputAndOutput where is sequence of
the characters ('1','Y','T' - enables; '0','N','F' - disables). Moreover, parameter of '-I' option
is optional (if not specified program assumes that inputAndOutput modes will be
applied for all devices).

Examples:

./gpustress-cli -L 0:0,0:1 -W 512,4 -B 2 -T 1 -I YN

chooses for all devices blocksNum=2, testType=1, for first device: workFactor=512, inAndOut=yes
; for second device: workFactor=4, inAndOut=no.

If value's list will be shorter than list of the choosen devices then
last provided value from list will be choosen for remaining devices.

For a determining the order of the choosen devices, you can use '-c' or '--choosenDevices'
option to get that order.

### GUI documentation

gpustress-gui accepts similar set of command line options like gpustress.

The GUI of the program contains three tabs. First allow to select devices whose will be tested.
You may do to this by using filtering ('Choose by filtering') or by choosing devices from list
('Choose from list').

After choosing set of a devices you can set test parameters for devices in 'Test configs' tab.
The top choice widget (combo box) allow to choose device for which test configuration will be set.
'Copy to all devices' copies currently choosen test configuration to
test configuration for all devices. 'Copy to these same devices' copies configuration only for
devices that have this same name and belongs to this same platform.

'Test logs' tab displays test logs for running stress test.
The top choice widget (combobox) allow to choose device for which a test log will be displayed.
You can save choosen log to file with using 'Save log' option or clear log with using
'Clear log' option.
'START' button runs stress test. If any failure will be happened program will display
alert message box and will choose the test log for failed device.

Option 'Stop test only when all device will fail'
causes stop test only when all devices will fail. 

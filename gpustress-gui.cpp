/*
 *  GPUStress
 *  Copyright (C) 2014 Mateusz Szpakowski
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define __CL_ENABLE_EXCEPTIONS 1

#ifdef _MSC_VER
#  define NOMINMAX 1
#endif

#include <iostream>
#include <ostream>
#include <string>
#include <set>
#include <map>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <popt.h>
#include <CL/cl.hpp>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Round_Button.H>
#include <FL/Fl_Spinner.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Tree.H>
#include <FL/Fl_Window.H>

#include "gpustress-core.h"

#ifdef _MSC_VER
#  define snprintf _snprintf
#  define SIZE_T_SPEC "%Iu"
#else
#  define SIZE_T_SPEC "%zu"
#endif

#define PROGRAM_VERSION "0.0.5.3"

extern const char* testDescsTable[];

static const char* devicesListString = nullptr;
static const char* builtinKernelsString = nullptr;
static const char* inputAndOutputsString = nullptr;
static const char* groupSizesString = nullptr;
static const char* workFactorsString = nullptr;
static const char* blocksNumsString = nullptr;
static const char* passItersNumsString = nullptr;
static const char* kitersNumsString = nullptr;
static int dontWait = 0;
static int printHelp = 0;
static int printUsage = 0;
static int printVersion = 0;

static const poptOption optionsTable[] =
{
    { "devicesList", 'L', POPT_ARG_STRING, &devicesListString, 'L',
        "Specify list of devices in form: 'platformId:deviceId,....'", "DEVICELIST" },
    { "useCPUs", 'C', POPT_ARG_VAL, &useCPUs, 'C', "Use all CPU devices", nullptr },
    { "useGPUs", 'G', POPT_ARG_VAL, &useGPUs, 'G', "Use all GPU devices", nullptr },
    { "useAccs", 'a', POPT_ARG_VAL, &useAccelerators, 'a',
        "Use all accelerator devices", nullptr },
    { "useAMD", 'A', POPT_ARG_VAL, &useAMDPlatform, 'A', "Use AMD platform", nullptr },
    { "useNVIDIA", 'N', POPT_ARG_VAL, &useNVIDIAPlatform, 'N',
        "Use NVIDIA platform", nullptr },
    { "useIntel", 'E', POPT_ARG_VAL, &useIntelPlatform, 'L', "Use Intel platform", nullptr },
    { "testType", 'T', POPT_ARG_STRING, &builtinKernelsString, 'T',
        "Choose test type (kernel) (range 0-3)", "NUMLIST" },
    { "inAndOut", 'I', POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL, &inputAndOutputsString, 'I',
        "Use input and output buffers (doubles memory reqs.)", "BOOLLIST" },
    { "workFactor", 'W', POPT_ARG_STRING, &workFactorsString, 'W',
        "Set workSize=factor*compUnits*grpSize", "FACTORLIST" },
    { "groupSize", 'g', POPT_ARG_STRING, &groupSizesString, 'g',
        "Set group size", "GROUPSIZELIST" },
    { "blocksNum", 'B', POPT_ARG_STRING, &blocksNumsString, 'B',
        "Set blocks number (range 1-16)", "BLOCKSLIST" },
    { "passIters", 'S', POPT_ARG_STRING, &passItersNumsString, 'S',
        "Set pass iterations num", "ITERSLIST" },
    { "kitersNum", 'j', POPT_ARG_STRING, &kitersNumsString, 'j',
        "Set kernel iterations number (range 1-100)", "ITERSLIST" },
    { "dontWait", 'w', POPT_ARG_VAL, &dontWait, 'w', "Dont wait few seconds", nullptr },
    { "exitIfAllFails", 'f', POPT_ARG_VAL, &exitIfAllFails, 'f',
        "Exit only if all devices fails at computation", nullptr },
    { "version", 'V', POPT_ARG_VAL, &printVersion, 'V', "Print program version", nullptr },
    { "help", '?', POPT_ARG_VAL, &printHelp, '?', "Show this help message", nullptr },
    { "usage", 0, POPT_ARG_VAL, &printUsage, 'u', "Display brief usage message", nullptr },
    { nullptr, 0, 0, nullptr, 0 }
};

static std::string escapeForFlTree(const std::string& s)
{
    std::string out;
    for (const char c: s)
        if (c == '/')
            out += "\\/";
        else if (c == '\\')
            out += "\\\\";
        else
            out.push_back(c);
    return out;
}

static std::string escapeForFlMenu(const std::string& s)
{
    std::string out;
    for (const char c: s)
        if (c == '/')
            out += "\\/";
        else if (c == '\\')
            out += "\\\\";
        else if (c == '&')
            out += "\\&";
        else if (c == '_')
            out += "\\_";
        else
            out.push_back(c);
    return out;
}

static std::string escapeForFlLabel(const std::string& s)
{
    std::string out;
    for (const char c: s)
        if (c == '&')
            out += "&&";
        else
            out.push_back(c);
    return out;
}

class DeviceChoiceGroup;
class TestConfigsGroup;
class TestLogsGroup;

/*
 * main GUI app class
 */

class GUIApp
{
private:
    Fl_Window* mainWin;
    Fl_Tabs* mainTabs;
    
    DeviceChoiceGroup* deviceChoiceGrp;
    TestConfigsGroup* testConfigsGrp;
    TestLogsGroup* testLogsGrp;
    
    Fl_Button* startStopButton;
    
public:
    GUIApp(const std::vector<cl::Device>& clDevices,
           const std::vector<GPUStressConfig>& configs);
    ~GUIApp();
    
    GUIApp(const GUIApp& g) = delete;
    GUIApp(GUIApp&& g) = delete;
    GUIApp& operator=(const GUIApp& g) = delete;
    GUIApp& operator=(GUIApp&& g) = delete;
    
    bool run();
    
    void updateGlobal();
    
    const DeviceChoiceGroup* getDeviceChoiceGroup() const
    { return deviceChoiceGrp; }
};

/*
 * Device choice group
 */

class DeviceChoiceGroup: public Fl_Group
{
private:
    struct DeviceEntry
    {
        cl::Device device;
        Fl_Check_Button* checkButton;
        
        DeviceEntry() : device(nullptr), checkButton(nullptr) { }
        DeviceEntry(cl::Device& _d, Fl_Check_Button* _w) : device(_d), checkButton(_w) { }
    };
    
    Fl_Round_Button* chooseByFilterRadio;
    Fl_Round_Button* chooseFromListRadio;
    
    Fl_Group* byFilterGroup;
    
    Fl_Check_Button* useAllPlatformsButton;
    Fl_Check_Button* useAMDButton;
    Fl_Check_Button* useNVIDIAButton;
    Fl_Check_Button* useIntelButton;
    
    Fl_Check_Button* useAllDevicesButton;
    Fl_Check_Button* useCPUsButton;
    Fl_Check_Button* useGPUsButton;
    Fl_Check_Button* useAccsButton;
    
    std::vector<DeviceEntry> allClDevices;
    size_t enabledClDevicesCount;
    
    static void choiceTypeIsSet(Fl_Widget* widget, void* data);
    static void byFilterChanged(Fl_Widget* widget, void* data);
    
    static void changeClDeviceEnable(Fl_Widget* widget, void* data);
    
    Fl_Tree* devicesTree;
    
    GUIApp& guiapp;
public:
    DeviceChoiceGroup(const std::vector<cl::Device>& clDevices, GUIApp& guiapp);
    
    size_t getClDevicesNum() const
    { return allClDevices.size(); }
    
    bool isClDeviceEnabled(size_t index) const
    { return allClDevices[index].checkButton->value(); }
    
    const cl::Device& getClDevice(size_t index) const
    { return allClDevices[index].device; }
    
    size_t getEnabledDevicesCount() const
    { return enabledClDevicesCount; }
};

DeviceChoiceGroup::DeviceChoiceGroup(const std::vector<cl::Device>& inClDevices,
        GUIApp& _guiapp) : Fl_Group(0, 20, 600, 380, "Device choice"), guiapp(_guiapp)
{
    enabledClDevicesCount = 0;
    align(FL_ALIGN_TOP_LEFT);
    Fl_Group* chGrp = new Fl_Group(10, 30, 580, 25);
    chooseByFilterRadio = new Fl_Round_Button(10, 30, 200, 25, "Choose by &filtering");
    chooseByFilterRadio->tooltip("Choose devices by filtering");
    chooseByFilterRadio->type(FL_RADIO_BUTTON);
    chooseByFilterRadio->callback(&DeviceChoiceGroup::choiceTypeIsSet, this);
    chooseFromListRadio = new Fl_Round_Button(220, 30, 370, 25, "Choose from &list");
    chooseFromListRadio->tooltip("Choose devices from list");
    chooseFromListRadio->type(FL_RADIO_BUTTON);
    chooseFromListRadio->callback(&DeviceChoiceGroup::choiceTypeIsSet, this);
    chGrp->resizable(chooseFromListRadio);
    chGrp->end();
    
    if (devicesListString == nullptr)
        chooseByFilterRadio->setonly();
    else
        chooseFromListRadio->setonly();
    
    byFilterGroup = new Fl_Group(10, 55, 200, 360);
    Fl_Group* grp = new Fl_Group(10, 75, 200, 110, "Platform select:");
    grp->box(FL_THIN_UP_FRAME);
    grp->align(FL_ALIGN_TOP_LEFT);
    useAllPlatformsButton = new Fl_Check_Button(15, 80, 190, 25, "A&ll platforms");
    useAllPlatformsButton->tooltip("Choose all OpenCL platforms");
    if (useAllPlatforms != 0)
        useAllPlatformsButton->set();
    useAllPlatformsButton->callback(&DeviceChoiceGroup::byFilterChanged, this);
    
    useAMDButton = new Fl_Check_Button(15, 105, 190, 25, "&AMD Platforms");
    useAMDButton->tooltip("Choose AMD APP Platforms");
    useAMDButton->callback(&DeviceChoiceGroup::byFilterChanged, this);
    if (useAMDPlatform != 0)
        useAMDButton->set();
    
    useNVIDIAButton = new Fl_Check_Button(15, 130, 190, 25, "&NVIDIA Platforms");
    useNVIDIAButton->tooltip("Choose NVIDIA OpenCL Platforms");    
    useNVIDIAButton->callback(&DeviceChoiceGroup::byFilterChanged, this);
    if (useNVIDIAPlatform != 0)
        useNVIDIAButton->set();
    
    useIntelButton = new Fl_Check_Button(15, 155, 190, 25, "&Intel Platforms");
    useIntelButton->tooltip("Choose Intel OpenCL Platforms");    
    useIntelButton->callback(&DeviceChoiceGroup::byFilterChanged, this);
    if (useIntelPlatform != 0)
        useIntelButton->set();    
    grp->end();
        
    grp = new Fl_Group(10, 205, 200, 85, "Device type select:");
    grp->box(FL_THIN_UP_FRAME);
    grp->align(FL_ALIGN_TOP_LEFT);
    useCPUsButton = new Fl_Check_Button(15, 210, 190, 25, "&CPU devices");
    useCPUsButton->tooltip("Choose all processors");
    useCPUsButton->callback(&DeviceChoiceGroup::byFilterChanged, this);
    if (useCPUs != 0)
        useCPUsButton->set();
    
    useGPUsButton = new Fl_Check_Button(15, 235, 190, 25, "&GPU devices");
    useGPUsButton->tooltip("Choose all GPUs");
    useGPUsButton->callback(&DeviceChoiceGroup::byFilterChanged, this);
    if (useGPUs != 0)
        useGPUsButton->set();
    
    useAccsButton = new Fl_Check_Button(15, 260, 190, 25, "Acc&elerator devices");
    useAccsButton->tooltip("Choose all Accelerators");
    useAccsButton->callback(&DeviceChoiceGroup::byFilterChanged, this);
    if (useAccelerators != 0)
        useAccsButton->set();
    grp->end();
    
    Fl_Box* box = new Fl_Box(10, 295, 200, 65);
    byFilterGroup->resizable(box);
    byFilterGroup->end();
    
    if (devicesListString != nullptr)
        byFilterGroup->deactivate();
    
    devicesTree = new Fl_Tree(220, 75, 370, 315, "Devices list");
    devicesTree->align(FL_ALIGN_TOP_LEFT);
    devicesTree->selectmode(FL_TREE_SELECT_NONE);
    devicesTree->showroot(0);
    devicesTree->end();
    {
        std::vector<cl::Platform> clPlatforms;
        cl::Platform::get(&clPlatforms);
        
        for (cxuint i = 0; i < clPlatforms.size(); i++)
        {
            char numBuf[64];
            snprintf(numBuf, 64, "%u: ", i);
            
            cl::Platform& clPlatform = clPlatforms[i];
            std::vector<cl::Device> clDevices;
            clPlatform.getDevices(CL_DEVICE_TYPE_ALL, &clDevices);
            
            std::string platformName;
            clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
            
            std::string platformPath(numBuf);
            platformPath += escapeForFlTree(platformName);
            devicesTree->add(::strdup(platformPath.c_str()));
            
            for (cxuint j = 0; j < clDevices.size(); j++)
            {
                snprintf(numBuf, 64, "%u: ", j);
                std::string devicePath(platformPath);
                devicePath += "/";
                devicePath += numBuf;
                cl::Device& clDevice = clDevices[j];
                std::string deviceName;
                clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
                devicePath += escapeForFlTree(deviceName);
                
                Fl_Tree_Item* item = devicesTree->add(::strdup(devicePath.c_str()));
                devicesTree->begin();
                Fl_Check_Button* checkButton = new Fl_Check_Button(0, 0, 320, 20,
                            ::strdup(escapeForFlLabel(
                                devicePath.c_str() + platformPath.size() + 1).c_str()));
                checkButton->callback(&DeviceChoiceGroup::changeClDeviceEnable, this);
                if (devicesListString == nullptr)
                    checkButton->deactivate();
                item->widget(checkButton);
                
                cl_uint deviceClock;
                cl_ulong memSize;
                cl_uint maxComputeUnits;
                size_t maxWorkGroupSize;
                clDevice.getInfo(CL_DEVICE_MAX_CLOCK_FREQUENCY, &deviceClock);
                clDevice.getInfo(CL_DEVICE_GLOBAL_MEM_SIZE, &memSize);
                clDevice.getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &maxComputeUnits);
                clDevice.getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &maxWorkGroupSize);
                
                char buf[128];
                snprintf(buf, 128, "Clock: %u MHz, Memory: %u MB, CompUnits: %u,"
                        " MaxGroupSize: " SIZE_T_SPEC, deviceClock,
                         cxuint(memSize>>20), maxComputeUnits,
                         maxWorkGroupSize);
                checkButton->tooltip(::strdup(buf));
                
                allClDevices.push_back(DeviceEntry(clDevice, checkButton));
                devicesTree->end();
            }
        }
    }
    
    resizable(devicesTree);
    end();
    
    if (devicesListString != nullptr)
    {   /* enable choosen devices from command line */
        std::set<cl_device_id> inClDeviceIDsSet;
        for (const cl::Device& device: inClDevices)
            inClDeviceIDsSet.insert(device());
        
        for (DeviceEntry& entry: allClDevices)
            if (inClDeviceIDsSet.find(entry.device()) != inClDeviceIDsSet.end())
            {
                entry.checkButton->set();
                enabledClDevicesCount++;
            }
    }
    else
        byFilterChanged(useAllPlatformsButton, this);
    guiapp.updateGlobal();
}

void DeviceChoiceGroup::choiceTypeIsSet(Fl_Widget* widget, void* data)
{
    Fl_Round_Button* button = (Fl_Round_Button*)widget;
    DeviceChoiceGroup* t = reinterpret_cast<DeviceChoiceGroup*>(data);
    
    if (t->chooseByFilterRadio == button && button->value())
    {
        t->byFilterGroup->activate();
        for (DeviceEntry& entry: t->allClDevices)
            entry.checkButton->deactivate();
        byFilterChanged(t->useAllPlatformsButton, data);
    }
    else if (t->chooseFromListRadio == button && button->value())
    {
        t->byFilterGroup->deactivate();
        for (DeviceEntry& entry: t->allClDevices)
            entry.checkButton->activate();
    }
}

void DeviceChoiceGroup::byFilterChanged(Fl_Widget* widget, void* data)
{
    DeviceChoiceGroup* t = reinterpret_cast<DeviceChoiceGroup*>(data);
    
    if (!t->chooseByFilterRadio->value())
        return; // do nothing
    
    bool thisUseAllPlatforms = t->useAllPlatformsButton->value();
    if (thisUseAllPlatforms)
    {
        t->useAMDButton->deactivate();
        t->useNVIDIAButton->deactivate();
        t->useIntelButton->deactivate();
    }
    else
    {
        t->useAMDButton->activate();
        t->useNVIDIAButton->activate();
        t->useIntelButton->activate();
    }
    
    bool thisUseAMD = t->useAMDButton->value();
    bool thisUseNVIDIA = t->useNVIDIAButton->value();
    bool thisUseIntel = t->useIntelButton->value();
    
    bool thisUseCPUs = t->useCPUsButton->value();
    bool thisUseGPUs = t->useGPUsButton->value();
    bool thisUseAccs = t->useAccsButton->value();
    
    t->enabledClDevicesCount = 0;
    for (DeviceEntry& entry: t->allClDevices)
    {   /* select from list */
        cl::Platform clPlatform;
        entry.device.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
        std::string platformName;
        clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
        
        if (!thisUseAllPlatforms)
        {
            if ((thisUseIntel==0 || platformName.find("Intel") == std::string::npos) &&
                (thisUseAMD==0 || platformName.find("AMD") == std::string::npos) &&
                (thisUseNVIDIA==0 || platformName.find("NVIDIA") == std::string::npos))
            {
                entry.checkButton->value(0);
                continue;
            }
        }
        
        cl_device_type deviceType;
        entry.device.getInfo(CL_DEVICE_TYPE, & deviceType);
        if ((thisUseCPUs && (deviceType & CL_DEVICE_TYPE_CPU) != 0) ||
            (thisUseGPUs && (deviceType & CL_DEVICE_TYPE_GPU) != 0) ||
            (thisUseAccs && (deviceType & CL_DEVICE_TYPE_ACCELERATOR) != 0))
        {
            entry.checkButton->value(1);
            t->enabledClDevicesCount++;
        }
        else
            entry.checkButton->value(0);
    }
    t->devicesTree->redraw();
    t->guiapp.updateGlobal();
}

void DeviceChoiceGroup::changeClDeviceEnable(Fl_Widget* widget, void* data)
{
    Fl_Check_Button* button = (Fl_Check_Button*)widget;
    DeviceChoiceGroup* t = reinterpret_cast<DeviceChoiceGroup*>(data);
    if (button->value())
        t->enabledClDevicesCount++;
    else
        t->enabledClDevicesCount--;
    t->guiapp.updateGlobal();
}

/*
 * SingleTestConfigGroup
 */

class SingleTestConfigGroup: public Fl_Group
{
private:
    cl::Device clDevice;
    char memoryReqsBuffer[64];
    char deviceInfoBuffer[96];
    Fl_Box* deviceInfoBox;
    Fl_Box* memoryReqsBox;
    Fl_Spinner* passItersSpinner;
    Fl_Spinner* groupSizeSpinner;
    Fl_Spinner* workFactorSpinner;
    Fl_Spinner* blocksNumSpinner;
    Fl_Spinner* kitersNumSpinner;
    Fl_Choice* builtinKernelChoice;
    Fl_Check_Button* inputAndOutputButton;
public:
    SingleTestConfigGroup(const cl::Device& clDevice, const GPUStressConfig* config);
    GPUStressConfig getConfig() const;
    void setConfig(const cl::Device& clDevice, const GPUStressConfig& config);
    
    void recomputeMemoryRequirements();
    
    void installCallback(void (*cb)(Fl_Widget*, void*), void* data);
};

SingleTestConfigGroup::SingleTestConfigGroup(const cl::Device& clDevice,
        const GPUStressConfig* config) : Fl_Group(10, 60, 580, 300)
{
    box(FL_THIN_UP_FRAME);
    deviceInfoBox =new Fl_Box(20, 60, 0, 20, "Required memory: MB");
    deviceInfoBox->align(FL_ALIGN_RIGHT);
    memoryReqsBox =new Fl_Box(20, 80, 0, 20, "Required memory: MB");
    memoryReqsBox->align(FL_ALIGN_RIGHT);
    passItersSpinner = new Fl_Spinner(150, 107, 150, 20, "Pass iterations");
    passItersSpinner->tooltip("Set number of kernel execution per single pass");
    passItersSpinner->range(1., INT32_MAX);
    passItersSpinner->step(1.0);
    groupSizeSpinner = new Fl_Spinner(150, 132, 150, 20, "Group size");
    groupSizeSpinner->tooltip("Set the OpenCL work group size");
    groupSizeSpinner->range(0., INT32_MAX);
    groupSizeSpinner->step(1.0);
    workFactorSpinner = new Fl_Spinner(150, 157, 150, 20, "Work factor");
    workFactorSpinner->tooltip("Set work factor (multiplies work size)");
    workFactorSpinner->range(1., INT32_MAX);
    workFactorSpinner->step(1.0);
    blocksNumSpinner = new Fl_Spinner(150, 182, 150, 20, "Blocks number");
    blocksNumSpinner->tooltip("Set number of blocks loaded/stored in kernel execution");
    blocksNumSpinner->range(1., 16);
    blocksNumSpinner->step(1.0);
    kitersNumSpinner = new Fl_Spinner(150, 207, 150, 20, "Kernel iterations");
    kitersNumSpinner->tooltip("Set number of operations between load and store");
    kitersNumSpinner->range(0., 100);
    kitersNumSpinner->step(1.0);
    builtinKernelChoice = new Fl_Choice(150, 232, 340, 20, "T&est type");
    builtinKernelChoice->tooltip("Set test type (builtin kernel)");
    for (const char** p = testDescsTable; *p != nullptr; p++)
        builtinKernelChoice->add(*p);
    inputAndOutputButton = new Fl_Check_Button(130, 257, 200, 25, "&Input and output");
    inputAndOutputButton->tooltip("Enable an using separate input buffer and output buffer");
    end();
    
    if (config != nullptr)
        setConfig(clDevice, *config);
    else
        deactivate();
}

GPUStressConfig SingleTestConfigGroup::getConfig() const
{
    GPUStressConfig config;
    config.passItersNum = passItersSpinner->value();
    config.groupSize = groupSizeSpinner->value();
    config.workFactor = workFactorSpinner->value();
    config.blocksNum = blocksNumSpinner->value();
    config.kitersNum = kitersNumSpinner->value();
    config.builtinKernel = builtinKernelChoice->value();
    config.inputAndOutput = inputAndOutputButton->value();
    return config;
}

void SingleTestConfigGroup::recomputeMemoryRequirements()
{
    cl_uint maxComputeUnits;
    clDevice.getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &maxComputeUnits);
    const size_t workFactor = workFactorSpinner->value();
    size_t groupSize = groupSizeSpinner->value();
    const size_t blocksNum = blocksNumSpinner->value();
    const bool inputAndOutput = inputAndOutputButton->value();
    
    if (groupSize == 0)
        clDevice.getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &groupSize);
    
    const size_t bufItemsNum = ((size_t(workFactor)*
                groupSize*maxComputeUnits)<<4)*blocksNum;
    double devMemReqs = 0.0;
    if (inputAndOutput)
        devMemReqs = (bufItemsNum<<4)/(1048576.0);
    else
        devMemReqs = (bufItemsNum<<3)/(1048576.0);
    snprintf(memoryReqsBuffer, 128, "Required memory: %g MB", devMemReqs);
    memoryReqsBox->label(memoryReqsBuffer);
    redraw();
}

void SingleTestConfigGroup::setConfig(const cl::Device& _clDevice,
                const GPUStressConfig& config)
{
    this->clDevice = _clDevice;
    
    cl_uint deviceClock;
    cl_ulong memSize;
    cl_uint maxComputeUnits;
    size_t maxWorkGroupSize;
    clDevice.getInfo(CL_DEVICE_MAX_CLOCK_FREQUENCY, &deviceClock);
    clDevice.getInfo(CL_DEVICE_GLOBAL_MEM_SIZE, &memSize);
    clDevice.getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &maxComputeUnits);
    clDevice.getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &maxWorkGroupSize);
    
    snprintf(deviceInfoBuffer, 96, "Clock: %u MHz, Memory: %u MB, CompUnits: %u,"
            " MaxGroupSize: " SIZE_T_SPEC, deviceClock, cxuint(memSize>>20),
             maxComputeUnits, maxWorkGroupSize);
    deviceInfoBox->label(deviceInfoBuffer);
    
    passItersSpinner->value(config.passItersNum);
    groupSizeSpinner->value(config.groupSize);
    workFactorSpinner->value(config.workFactor);
    blocksNumSpinner->value(config.blocksNum);
    kitersNumSpinner->value(config.kitersNum);
    builtinKernelChoice->value(config.builtinKernel);
    inputAndOutputButton->value(config.inputAndOutput);
    
    recomputeMemoryRequirements();
}

void SingleTestConfigGroup::installCallback(void (*cb)(Fl_Widget*, void*), void* data)
{
    passItersSpinner->callback(cb, data);
    groupSizeSpinner->callback(cb, data);
    workFactorSpinner->callback(cb, data);
    blocksNumSpinner->callback(cb, data);
    kitersNumSpinner->callback(cb, data);
    builtinKernelChoice->callback(cb, data);
    inputAndOutputButton->callback(cb, data);
}

/*
 * Test configs class
 */

class TestConfigsGroup: public Fl_Group
{
private:
    cl_device_id curClDeviceID;
    std::vector<cl_device_id> choosenClDeviceIDs;
    std::map<cl_device_id, GPUStressConfig> allConfigsMap;
    Fl_Choice* deviceChoice;
    SingleTestConfigGroup* singleConfigGroup;
    
    std::vector<std::string> choiceLabels;
    Fl_Button* toAllDevicesButton;
    Fl_Button* toTheseSameDevsButton;
    
    GUIApp& guiapp;
    
    static void selectedDeviceChanged(Fl_Widget* widget, void* data);
    static void singleConfigChanged(Fl_Widget* widget, void* data);
    
    static void copyToAllDevicesCalled(Fl_Widget* widget, void* data);
    static void copyToTheseSameCalled(Fl_Widget* widget, void* data);
    
    void applyBestTestConfig(cl_device_id clDeviceId);
public:
    TestConfigsGroup(const std::vector<cl::Device>& clDevices,
            const std::vector<GPUStressConfig>& configs, GUIApp& _guiapp);
    
    void updateDeviceList();
};

TestConfigsGroup::TestConfigsGroup(const std::vector<cl::Device>& clDevices,
            const std::vector<GPUStressConfig>& configs, GUIApp& _guiapp)
        : Fl_Group(0, 20, 600, 380, "Test configs"), guiapp(_guiapp)
{   /* fill up configurations */
    for (size_t i = 0; i < configs.size(); i++)
        allConfigsMap.insert(std::make_pair(clDevices[i](), configs[i]));
    
    const DeviceChoiceGroup* devChoiceGroup = guiapp.getDeviceChoiceGroup();
    curClDeviceID = nullptr;
    deviceChoice = new Fl_Choice(70, 32, 520, 20, "Device:");
    deviceChoice->tooltip("Choose device for which test will be configured");
    
    for (size_t i = 0; i < devChoiceGroup->getClDevicesNum(); i++)
        if (devChoiceGroup->isClDeviceEnabled(i))
        {
            const cl::Device& clDevice = devChoiceGroup->getClDevice(i);
            cl::Platform clPlatform;
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
            std::string platformName;
            clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
            std::string deviceName;
            clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
            
            choiceLabels.push_back(escapeForFlMenu(platformName + ":" + deviceName));
            deviceChoice->add(choiceLabels.back().c_str());
            
            if (curClDeviceID == nullptr)
                curClDeviceID = clDevice();
            choosenClDeviceIDs.push_back(clDevice());
        }
    
    if (!configs.empty())
    {
        singleConfigGroup = new SingleTestConfigGroup(
                    cl::Device(curClDeviceID), &allConfigsMap[curClDeviceID]);
        deviceChoice->value(0);
    }
    else // if empty
        singleConfigGroup = new SingleTestConfigGroup(cl::Device(), nullptr);
    
    deviceChoice->callback(&TestConfigsGroup::selectedDeviceChanged, this);
    singleConfigGroup->installCallback(&TestConfigsGroup::singleConfigChanged, this);
    
    toAllDevicesButton = new Fl_Button(10, 365, 285, 25, "Copy to &all devices");
    toAllDevicesButton->tooltip("Copy this test configuration to all devices");
    toAllDevicesButton->callback(&TestConfigsGroup::copyToAllDevicesCalled, this);
    
    toTheseSameDevsButton = new Fl_Button(305, 365, 285, 25, "Copy to the&se same devices");
    toTheseSameDevsButton->tooltip(
            "Copy this test configuration to all devices with this same configuration");
    toTheseSameDevsButton->callback(&TestConfigsGroup::copyToTheseSameCalled, this);
    
    if (configs.empty())
    {
        toAllDevicesButton->deactivate();
        toTheseSameDevsButton->deactivate();
    }
    end();
}

void TestConfigsGroup::selectedDeviceChanged(Fl_Widget* widget, void* data)
{
    Fl_Choice* choice = reinterpret_cast<Fl_Choice*>(widget);
    TestConfigsGroup* t = reinterpret_cast<TestConfigsGroup*>(data);
    //
    size_t index = choice->value();
    if (index >= t->choosenClDeviceIDs.size())
        return;
    t->singleConfigGroup->setConfig(t->choosenClDeviceIDs[index],
            t->allConfigsMap.find(t->choosenClDeviceIDs[index])->second);
    t->curClDeviceID = t->choosenClDeviceIDs[index];
}

void TestConfigsGroup::singleConfigChanged(Fl_Widget* widget, void* data)
{
    TestConfigsGroup* t = reinterpret_cast<TestConfigsGroup*>(data);
    if (t->curClDeviceID != nullptr)
    {
        t->allConfigsMap.find(t->curClDeviceID)->second = t->singleConfigGroup->getConfig();
        t->singleConfigGroup->recomputeMemoryRequirements();
    }
}

void TestConfigsGroup::copyToAllDevicesCalled(Fl_Widget* widget, void* data)
{
    TestConfigsGroup* t = reinterpret_cast<TestConfigsGroup*>(data);
    
    const GPUStressConfig& src = t->allConfigsMap[t->curClDeviceID];
    for (cl_device_id devId: t->choosenClDeviceIDs)
        if (devId != t->curClDeviceID)
            t->allConfigsMap.find(devId)->second = src;
}

void TestConfigsGroup::copyToTheseSameCalled(Fl_Widget* widget, void* data)
{
    TestConfigsGroup* t = reinterpret_cast<TestConfigsGroup*>(data);
    
    const GPUStressConfig& src = t->allConfigsMap[t->curClDeviceID];
    cl::Device curClDevice(t->curClDeviceID);
    std::string targetDevName;
    cl::Platform targetPlatform;
    curClDevice.getInfo(CL_DEVICE_PLATFORM, &targetPlatform);
    curClDevice.getInfo(CL_DEVICE_NAME, &targetDevName);
    
    for (cl_device_id devId: t->choosenClDeviceIDs)
        if (devId != t->curClDeviceID)
        {
            cl::Device clDevice(devId);
            cl::Platform clPlatform;
            std::string clDevName;
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clDevName);
            if (clPlatform() == targetPlatform() && clDevName == targetDevName)
                t->allConfigsMap.find(devId)->second = src;
        }
}

void TestConfigsGroup::applyBestTestConfig(cl_device_id inClDeviceId)
{
    bool found = false;
    cl::Device inClDevice(inClDeviceId);
    std::string targetDevName;
    cl::Platform targetPlatform;
    
    inClDevice.getInfo(CL_DEVICE_PLATFORM, &targetPlatform);
    inClDevice.getInfo(CL_DEVICE_NAME, &targetDevName);
    
    for (cl_device_id devId: choosenClDeviceIDs)
    {   /* first, we search only enabled devices */
        if (devId == curClDeviceID)
            continue;
        cl::Device clDevice(devId);
        cl::Platform clPlatform;
        std::string clDevName;
        clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
        clDevice.getInfo(CL_DEVICE_PLATFORM, &clDevName);
        if (clPlatform() == targetPlatform() && clDevName == targetDevName)
        {   /* if found proper device */
            allConfigsMap.insert(std::make_pair(inClDeviceId,
                    allConfigsMap.find(devId)->second));
            found = true;
            break;
        }
    }
    
    if (!found)
        for (const auto& entry: allConfigsMap)
        {
            cl::Device clDevice(entry.first);
            cl::Platform clPlatform;
            std::string clDevName;
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clDevName);
            if (clPlatform() == targetPlatform() && clDevName == targetDevName)
            {   /* if found proper device */
                allConfigsMap.insert(std::make_pair(inClDeviceId, entry.second));
                found = true;
                break;
            }
        }
    if (!found)
    {   // add default
        GPUStressConfig config;
        config.passItersNum = 32;
        config.groupSize = 0;
        config.workFactor = 256;
        config.blocksNum = 2;
        config.kitersNum = 0;
        config.builtinKernel = 0;
        config.inputAndOutput = false;
        allConfigsMap.insert(std::make_pair(inClDeviceId, config));
    }
}

void TestConfigsGroup::updateDeviceList()
{
    choosenClDeviceIDs.clear();
    const DeviceChoiceGroup* devChoiceGroup = guiapp.getDeviceChoiceGroup();
    
    bool isPrevCLDevice = false;
    size_t prevChoiceIndex = deviceChoice->value();
    cl_device_id prevCLDeviceID = curClDeviceID;
    curClDeviceID = nullptr;
    
    deviceChoice->clear();
    choiceLabels.clear();
    
    size_t choiceIndex = 0;
    for (size_t i = 0; i < devChoiceGroup->getClDevicesNum(); i++)
        if (devChoiceGroup->isClDeviceEnabled(i))
            choosenClDeviceIDs.push_back(devChoiceGroup->getClDevice(i)());
    
    for (size_t i = 0; i < devChoiceGroup->getClDevicesNum(); i++)
        if (devChoiceGroup->isClDeviceEnabled(i))
        {
            const cl::Device& clDevice = devChoiceGroup->getClDevice(i);
            cl::Platform clPlatform;
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
            std::string platformName;
            clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
            std::string deviceName;
            clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
            choiceLabels.push_back(escapeForFlMenu(platformName + ":" + deviceName));
            deviceChoice->add(choiceLabels.back().c_str());
            
            if (curClDeviceID == nullptr)
                curClDeviceID = clDevice();
            if (prevCLDeviceID == clDevice())
            {
                prevChoiceIndex = choiceIndex;
                isPrevCLDevice = true;
            }
            
            if (allConfigsMap.find(clDevice()) == allConfigsMap.end())
                applyBestTestConfig(clDevice());
            choiceIndex++;
        }
    
    if (devChoiceGroup->getEnabledDevicesCount() != 0)
    {
        if (isPrevCLDevice)
        {
            curClDeviceID = prevCLDeviceID;
            deviceChoice->value(prevChoiceIndex);
        }
        else
            deviceChoice->value(0);
        singleConfigGroup->setConfig(cl::Device(curClDeviceID),
                    allConfigsMap[curClDeviceID]);
        singleConfigGroup->activate();
    }
    else // empty
        singleConfigGroup->deactivate();
    
    if (devChoiceGroup->getEnabledDevicesCount() > 1)
    {
        toAllDevicesButton->activate();
        toTheseSameDevsButton->activate();
    }
    else
    {
        toAllDevicesButton->deactivate();
        toTheseSameDevsButton->deactivate();
    }
}

/*
 * Test logs group class
 */

class TestLogsGroup: public Fl_Group
{
private:
    Fl_Choice* deviceChoice;
    Fl_Choice* logTypeChoice;
    
    struct LogLine
    { 
        cxuint type, pos, length;
        
        LogLine() : type(0), pos(0), length() { }
        LogLine(cxuint _type, cxuint _pos, cxuint _length)
            : type(_type), pos(_pos), length(_length)
        { }
    };
    
    std::vector<LogLine> logsLines;
    std::string logsString;
    Fl_Text_Display* logOutput;
    
    Fl_Button* saveLogButton;
    Fl_Button* clearLogButton;
    
    std::vector<std::string> choiceLabels;
    
    GUIApp& guiapp;
public:
    TestLogsGroup(const std::vector<cl::Device>& clDevices, GUIApp& _guiapp);
    
    void updateDeviceList();
    
    void clearLogs();
};

TestLogsGroup::TestLogsGroup(const std::vector<cl::Device>& clDevices, GUIApp& _guiapp)
        : Fl_Group(0, 20, 600, 380, "Test logs"), guiapp(_guiapp)
{
    const DeviceChoiceGroup* devChoiceGroup = guiapp.getDeviceChoiceGroup();
    //curClDeviceID = nullptr;
    deviceChoice = new Fl_Choice(70, 32, 410, 20, "Device:");
    deviceChoice->tooltip("Choose device for which log messages will be displayed");
    
    if (clDevices.size() > 1)
        deviceChoice->add("All devices");
    
    for (size_t i = 0; i < devChoiceGroup->getClDevicesNum(); i++)
        if (devChoiceGroup->isClDeviceEnabled(i))
        {
            const cl::Device& clDevice = devChoiceGroup->getClDevice(i);
            cl::Platform clPlatform;
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
            std::string platformName;
            clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
            std::string deviceName;
            clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
            
            choiceLabels.push_back(escapeForFlMenu(platformName + ":" + deviceName));
            deviceChoice->add(choiceLabels.back().c_str());
        }
    
    if (!clDevices.empty())
        deviceChoice->value(0);
    
    logTypeChoice = new Fl_Choice(525, 32, 65, 20, "Logs:");
    logTypeChoice->tooltip("Choose type of log messages which will be displayed");
    logTypeChoice->add("Both");
    logTypeChoice->add("Out");
    logTypeChoice->add("Error");
    logTypeChoice->value(0);
    
    logOutput = new Fl_Text_Display(10, 60, 580, 300);
    
    saveLogButton = new Fl_Button(10, 365, 285, 25, "&Save log");
    saveLogButton->tooltip("Save choosen log to file");
    clearLogButton = new Fl_Button(305, 365, 285, 25, "&Clear all logs");
    clearLogButton->tooltip("Clear all received logs");
    end();
}

void TestLogsGroup::updateDeviceList()
{
    deviceChoice->clear();
    
    const DeviceChoiceGroup* devChoiceGroup = guiapp.getDeviceChoiceGroup();
    
    if (devChoiceGroup->getEnabledDevicesCount() > 1)
        deviceChoice->add("All devices");
    
    for (size_t i = 0; i < devChoiceGroup->getClDevicesNum(); i++)
        if (devChoiceGroup->isClDeviceEnabled(i))
        {
            const cl::Device& clDevice = devChoiceGroup->getClDevice(i);
            cl::Platform clPlatform;
            clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
            std::string platformName;
            clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
            std::string deviceName;
            clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
            
            choiceLabels.push_back(escapeForFlMenu(platformName + ":" + deviceName));
            deviceChoice->add(choiceLabels.back().c_str());
        }
    
    if (devChoiceGroup->getEnabledDevicesCount() != 0)
        deviceChoice->value(0);
}

/*
 * main GUI app class
 */

GUIApp::GUIApp(const std::vector<cl::Device>& clDevices,
           const std::vector<GPUStressConfig>& configs)
try
        : mainWin(nullptr), mainTabs(nullptr), deviceChoiceGrp(nullptr)
{
    mainWin = new Fl_Window(600, 440, "CLGPUStress");
    mainTabs = new Fl_Tabs(0, 0, 600, 400);
    deviceChoiceGrp = new DeviceChoiceGroup(clDevices, *this);
    testConfigsGrp = new TestConfigsGroup(clDevices, configs, *this);
    testLogsGrp = new TestLogsGroup(clDevices, *this);
    mainTabs->end();
    startStopButton = new Fl_Button(0, 400, 600, 40, "START");
    startStopButton->labelfont(FL_HELVETICA_BOLD);
    startStopButton->labelsize(20);
    mainWin->resizable(mainTabs);
    mainWin->end();
    
    updateGlobal();
}
catch(...)
{
    delete mainWin;
    throw;
}

GUIApp::~GUIApp()
{
    delete mainWin;
}

void GUIApp::updateGlobal()
{
    if (deviceChoiceGrp == nullptr || startStopButton == nullptr)
        return;
    if (deviceChoiceGrp->getEnabledDevicesCount() != 0)
        startStopButton->activate();
    else
        startStopButton->deactivate();
    testConfigsGrp->updateDeviceList();
    testLogsGrp->updateDeviceList();
}

bool GUIApp::run()
{
    mainWin->show();
    int ret = Fl::run();
    if (ret != 0)
    {
        std::cerr << "GUI App returns abnormally with code:" << ret << std::endl;
        return false;
    }
    return true;
}

int main(int argc, const char** argv)
{
    int cmd;
    poptContext optsContext;
    
    outStream = &std::cout;
    errStream = &std::cerr;
    
    optsContext = poptGetContext("gpustress", argc, argv, optionsTable, 0);
    
    bool globalInputAndOutput = false;
    /* parse options */
    while((cmd = poptGetNextOpt(optsContext)) >= 0)
    {
        if (cmd == 'I')
            globalInputAndOutput = true;
    }
    
    if (cmd < -1)
    {
        std::cerr << poptBadOption(optsContext, POPT_BADOPTION_NOALIAS) << ": " <<
            poptStrerror(cmd) << std::endl;
        poptFreeContext(optsContext);
        return 1;
    }
    
    std::cout << "CLGPUStress " PROGRAM_VERSION " by Mateusz Szpakowski. "
        "Program is distributed under terms of the GPLv2." << std::endl;
    if (printVersion)
    {
        poptFreeContext(optsContext);
        return 0; // that's all
    }
    if (printHelp)
    {
        std::cout << std::endl;
        poptPrintHelp(optsContext, stdout, 0);
        poptFreeContext(optsContext);
        fflush(stdout);
        
        std::cout << "\nList of the supported test types "
                "(test can be set by using '-T' option):" << std::endl;
        for(cxuint i = 0; testDescsTable[i] != nullptr; i++)
            std::cout << "  " << i << ": " << testDescsTable[i] << std::endl;
        return 0;
    }
    if (printUsage)
    {
        std::cout << std::endl;
        poptPrintUsage(optsContext, stdout, 0);
        poptFreeContext(optsContext);
        return 0;
    }
    
    if (!useGPUs && !useCPUs)
        useGPUs = 1;
    if (!useAMDPlatform && !useNVIDIAPlatform && !useIntelPlatform)
        useAllPlatforms = true;
    
    int retVal = 0;
    
    std::vector<GPUStressConfig> gpuStressConfigs;
    try
    {
        std::vector<cl::Device> choosenClDevices;
        if (devicesListString == nullptr)
            choosenClDevices = getChoosenCLDevices();
        else
            choosenClDevices = getChoosenCLDevicesFromList(devicesListString);
        
        {
            if (inputAndOutputsString == nullptr && globalInputAndOutput)
                inputAndOutputsString = "1";
            std::vector<cxuint> workFactors =
                    parseCmdUIntList(workFactorsString, "work factors");
            std::vector<cxuint> groupSizes =
                    parseCmdUIntList(groupSizesString, "group sizes");
            std::vector<cxuint> passItersNums =
                    parseCmdUIntList(passItersNumsString, "passIters numbers");
            std::vector<cxuint> blocksNums =
                    parseCmdUIntList(blocksNumsString, "blocks numbers");
            std::vector<cxuint> kitersNums =
                    parseCmdUIntList(kitersNumsString, "kiters numbers");
            std::vector<cxuint> builtinKernels =
                    parseCmdUIntList(builtinKernelsString, "testTypes");
            std::vector<bool> inputAndOutputs =
                    parseCmdBoolList(inputAndOutputsString, "inputAndOutputs");
            
            gpuStressConfigs = collectGPUStressConfigs(choosenClDevices.size(),
                    passItersNums, groupSizes, workFactors, blocksNums, kitersNums,
                    builtinKernels, inputAndOutputs);
        }
        
        /* run window */
        GUIApp guiapp(choosenClDevices, gpuStressConfigs);
        if (!guiapp.run())
            retVal = 1;
    }
    catch(const cl::Error& error)
    {
        std::cerr << "OpenCL error happened: " << error.what() <<
                ", Code: " << error.err() << std::endl;
        retVal = 1;
    }
    catch(const std::exception& ex)
    {
        std::cerr << "Exception happened: " << ex.what() << std::endl;
        retVal = 1;
    }
    
    poptFreeContext(optsContext);
    return retVal;
}

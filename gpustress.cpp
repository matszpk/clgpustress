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
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <string>
#include <cstring>
#include <vector>
#include <utility>
#include <set>
#include <thread>
#include <mutex>
#include <random>
#include <chrono>
#include <atomic>
#include <popt.h>
#include <CL/cl.hpp>

#ifdef _MSC_VER
#  define snprintf _snprintf
#  define SIZE_T_SPEC "%Iu"
#else
#  define SIZE_T_SPEC "%zu"
#endif

typedef unsigned short cxushort;
typedef signed short cxshort;
typedef unsigned int cxuint;
typedef signed int cxint;
typedef unsigned long cxulong;
typedef signed long cxlong;

class MyException: public std::exception
{
private:
    std::string message;
public:
    MyException() { }
    MyException(const std::string& msg)
    {
        this->message = msg;
    }
    virtual ~MyException() throw() { }
    
    const char* what() const throw()
    {
        return message.c_str();
    }
};

extern const char* clKernel1Source;
extern const char* clKernel2Source;
extern const char* clKernelPWSource;

static int useCPUs = 0;
static int useGPUs = 0;
static int useAccelerators = 0;
static int useAMDPlatform = 0;
static int useNVIDIAPlatform = 0;
static int useIntelPlatform = 0;
static bool useAllPlatforms = false;
static int listAllDevices = 0;
static int listChoosenDevices = 0;
static const char* devicesListString = nullptr;
static const char* builtinKernelsString = nullptr;
static const char* inputAndOutputsString = nullptr;
static const char* workFactorsString = nullptr;
static const char* blocksNumsString = nullptr;
static const char* passItersNumsString = nullptr;
static const char* kitersNumsString = nullptr;
static int dontWait = 0;
static int exitIfAllFails = 0;

static std::mutex stdOutputMutex;

static std::atomic<bool> stopAllStressTesters(false);

static const poptOption optionsTable[] =
{
    { "listDevices", 'l', POPT_ARG_VAL, &listAllDevices, 'l', "list all OpenCL devices", nullptr },
    { "devicesList", 'L', POPT_ARG_STRING, &devicesListString, 'L',
        "specify list of devices in form: 'platformId:deviceId,....'", "DEVICELIST" },
    { "choosenDevices", 'c', POPT_ARG_VAL, &listChoosenDevices, 'c',
        "list choosen OpenCL devices", nullptr },
    { "useCPUs", 'C', POPT_ARG_VAL, &useCPUs, 'C', "use all CPU devices", nullptr },
    { "useGPUs", 'G', POPT_ARG_VAL, &useGPUs, 'G', "use all GPU devices", nullptr },
    { "useAccs", 'a', POPT_ARG_VAL, &useAccelerators, 'a',
        "use all accelerator devices", nullptr },
    { "useAMD", 'A', POPT_ARG_VAL, &useAMDPlatform, 'A', "use AMD platform", nullptr },
    { "useNVIDIA", 'N', POPT_ARG_VAL, &useNVIDIAPlatform, 'N',
        "use NVIDIA platform", nullptr },
    { "useIntel", 'E', POPT_ARG_VAL, &useIntelPlatform, 'L', "use Intel platform", nullptr },
    { "builtin", 'T', POPT_ARG_STRING, &builtinKernelsString, 'T',
        "choose OpenCL builtin kernel", "NUMLIST [0-2]" },
    { "inAndOut", 'I', POPT_ARG_STRING|POPT_ARGFLAG_OPTIONAL, &inputAndOutputsString, 'I',
      "use input and output buffers (doubles memory reqs.)", "BOOLLIST" },
    { "workFactor", 'W', POPT_ARG_STRING, &workFactorsString, 'W',
        "set workSize=factor*compUnits*grpSize", "FACTORLIST" },
    { "blocksNum", 'B', POPT_ARG_STRING, &blocksNumsString, 'B', "blocks number", "BLOCKSLIST" },
    { "passIters", 'S', POPT_ARG_STRING, &passItersNumsString, 'S', "pass iterations num",
        "ITERATIONSLIST" },
    { "kitersNum", 'j', POPT_ARG_STRING, &kitersNumsString, 'j', "kitersNum", "ITERATIONSLIST" },
    { "dontWait", 'w', POPT_ARG_VAL, &dontWait, 'w', "dont wait few seconds", nullptr },
    { "exitIfAllFails", 'f', POPT_ARG_VAL, &exitIfAllFails, 'f',
        "exit only if all devices fails at computation", nullptr },
    POPT_AUTOHELP
    { nullptr, 0, 0, nullptr, 0 }
};

static std::vector<cxuint> parseCmdUIntList(const char* str, const char* name)
{
    std::vector<cxuint> outVector;
    if (str == nullptr)
        return outVector;
    
    const char* p = str;
    while (*p != 0)
    {
        cxuint val;
        if (sscanf(p, "%u", &val) != 1)
            throw MyException(std::string("Cant parse ")+name);
        outVector.push_back(val);
        
        while (*p != 0 && *p != ',') p++;
        if (*p == ',') p++; // next elem in list
    }
    return outVector;
}

static std::vector<bool> parseCmdBoolList(const char* str, const char* name)
{
    std::vector<bool> outVector;
    if (str == nullptr)
        return outVector;
    
    for (const char* p = str; *p != 0; p++)
    {
        if (*p == 'Y' || *p == 'y' || *p == '1' || *p == 'T' || *p == 't' || *p == '+')
            outVector.push_back(true);
        else if (*p == 'N' || *p == 'n' || *p == '0' || *p == 'F' || *p == 'f' || *p == '-')
            outVector.push_back(false);
        else
            throw MyException(std::string("Cant parse ")+name);
    }
    return outVector;
}

static std::vector<std::pair<cl::Platform, cl::Device> > getChoosenCLDevices()
{
    std::vector<std::pair<cl::Platform, cl::Device> > outDevices;
    std::vector<cl::Platform> clPlatforms;
    cl::Platform::get(&clPlatforms);
    
    for (const cl::Platform& clPlatform: clPlatforms)
    {
        std::string platformName;
        clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
        
        if (!useAllPlatforms)
        {
            if ((useIntelPlatform==0 || platformName.find("Intel") == std::string::npos) &&
                (useAMDPlatform==0 || platformName.find("AMD") == std::string::npos) &&
                (useNVIDIAPlatform==0 || platformName.find("NVIDIA") == std::string::npos))
                continue;
        }
        
        std::vector<cl::Device> clDevices;
        cl_device_type deviceType = 0;
        if (useGPUs)
            deviceType |= CL_DEVICE_TYPE_GPU;
        if (useCPUs)
            deviceType |= CL_DEVICE_TYPE_CPU;
        if (useAccelerators)
            deviceType |= CL_DEVICE_TYPE_ACCELERATOR;
            
        clPlatform.getDevices(deviceType, &clDevices);
        for (const cl::Device& clDevice: clDevices)
            outDevices.push_back(std::make_pair(clPlatform, clDevice));
    }
    return outDevices;
}

static std::vector<std::pair<cl::Platform, cl::Device> >
getChoosenCLDevicesFromList(const char* str)
{
    std::vector<std::pair<cl::Platform, cl::Device> > outDevices;
    std::vector<cl::Platform> clPlatforms;
    cl::Platform::get(&clPlatforms);
    
    std::set<std::pair<cxuint,cxuint> > deviceIdsSet;
    const char* p = str;
    while (*p != 0)
    {
        cxuint platformId, deviceId;
        if (sscanf(p, "%u:%u", &platformId, &deviceId) != 2)
            throw MyException("Cant parse device list");
        
        if (platformId >= clPlatforms.size())
            throw MyException("PlatformID out of range");
        cl::Platform clPlatform = clPlatforms[platformId];
        
        std::vector<cl::Device> clDevices;
        clPlatform.getDevices(CL_DEVICE_TYPE_ALL, &clDevices);
        if (deviceId >= clDevices.size())
            throw MyException("DeviceID out of range");
        
        if (!deviceIdsSet.insert(std::make_pair(platformId, deviceId)).second)
            throw MyException("Duplicated devices in device list!");
        
        cl::Device clDevice = clDevices[deviceId];
        outDevices.push_back(std::make_pair(clPlatform, clDevice));
        
        while (*p != 0 && *p != ',') p++;
        if (*p == ',') p++; // next elem in list
    }
    
    return outDevices;
}

static void listCLDevices()
{
    std::vector<std::pair<cl::Platform, cl::Device> > outDevices;
    std::vector<cl::Platform> clPlatforms;
    cl::Platform::get(&clPlatforms);
    
    for (cl_uint i = 0; i < clPlatforms.size(); i++)
    {
        cl::Platform& clPlatform = clPlatforms[i];
        std::string platformName;
        clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
        
        std::vector<cl::Device> clDevices;
        clPlatform.getDevices(CL_DEVICE_TYPE_ALL, &clDevices);
        
        for (cl_uint j = 0; j < clDevices.size(); j++)
        {
            cl::Device& clDevice = clDevices[j];
            std::string deviceName;
            clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
            
            std::cout << i << ":" << j << "  " << platformName << ":" << deviceName << "\n";
        }
    }
    std::cout.flush();
}

static void listChoosenCLDevices(const std::vector<std::pair<cl::Platform, cl::Device> >& list)
{
    for (size_t i = 0; i < list.size(); i++)
    {
        const auto& elem = list[i];
        std::string platformName;
        elem.first.getInfo(CL_PLATFORM_NAME, &platformName);
        std::string deviceName;
        elem.second.getInfo(CL_DEVICE_NAME, &deviceName);
        
        std::cout << i << "  " << platformName << ":" << deviceName << "\n";
    }
    std::cout.flush();
}

static const float examplePoly[5] = 
{ 4.43859953e+05,   1.13454169e+00,  -4.50175916e-06, -1.43865531e-12,   4.42133541e-18 };

struct GPUStressConfig
{
    cxuint passItersNum;
    cxuint workFactor;
    cxuint blocksNum;
    cxuint kitersNum;
    cxuint builtinKernel;
    bool inputAndOutput;
};

static std::vector<GPUStressConfig> collectGPUStressConfigs(cxuint devicesNum,
        const std::vector<cxuint>& passItersNumVec, const std::vector<cxuint>& workFactorVec,
        const std::vector<cxuint>& blocksNumVec, const std::vector<cxuint>& kitersNumVec,
        const std::vector<cxuint>& builtinKernelVec, const std::vector<bool>& inAndOutVec)
{
    if (passItersNumVec.size() > devicesNum)
        throw MyException("PassItersNum list is too long");
    if (workFactorVec.size() > devicesNum)
        throw MyException("WorkFactor list is too long");
    if (blocksNumVec.size() > devicesNum)
        throw MyException("BlocksNum list is too long");
    if (kitersNumVec.size() > devicesNum)
        throw MyException("kitersNum list is too long");
    if (builtinKernelVec.size() > devicesNum)
        throw MyException("BuiltinKernel list is too long");
    if (inAndOutVec.size() > devicesNum)
        throw MyException("InputAndOutput list is too long");
    
    std::vector<GPUStressConfig> outConfigs(devicesNum);
    
    for (size_t i = 0; i < devicesNum; i++)
    {
        GPUStressConfig config;
        if (!passItersNumVec.empty())
            config.passItersNum = (passItersNumVec.size() > i) ? passItersNumVec[i] : 
                    passItersNumVec.back();
        else // default
            config.passItersNum = 32;
        
        if (!workFactorVec.empty())
            config.workFactor = (workFactorVec.size() > i) ? workFactorVec[i] : 
                    workFactorVec.back();
        else // default
            config.workFactor = 256;
        
        if (!blocksNumVec.empty())
            config.blocksNum = (blocksNumVec.size() > i) ? blocksNumVec[i] : 
                    blocksNumVec.back();
        else // default
            config.blocksNum = 2;
        
        if (!kitersNumVec.empty())
            config.kitersNum = (kitersNumVec.size() > i) ? kitersNumVec[i] : 
                    kitersNumVec.back();
        else // default
            config.kitersNum = 0;
        
        if (!builtinKernelVec.empty())
            config.builtinKernel = (builtinKernelVec.size() > i) ? builtinKernelVec[i] :
                    builtinKernelVec.back();
        else // default
            config.builtinKernel = 0;
        
        if (!inAndOutVec.empty())
            config.inputAndOutput = (inAndOutVec.size() > i) ? inAndOutVec[i] :
                    inAndOutVec.back();
        else // default
            config.inputAndOutput = false;
        
        if (config.passItersNum == 0)
            throw MyException("PassItersNum is zero");
        if (config.blocksNum == 0 || config.blocksNum > 16)
            throw MyException("BlocksNum is zero or out of range");
        if (config.workFactor == 0)
            throw MyException("WorkFactor is zero");
        if (config.builtinKernel > 2)
            throw MyException("BuiltinKernel out of range");
        if (config.kitersNum > 30)
            throw MyException("KitersNum out of range");
        outConfigs[i] = config;
    }
    
    return outConfigs;
}

class GPUStressTester
{
private:
    cxuint id;
    cl::Device clDevice;
    cl::Context clContext;
    
    std::string platformName;
    std::string deviceName;
    
    typedef std::chrono::time_point<std::chrono::high_resolution_clock> time_point;
    
    time_point startTime;
    time_point lastTime;
    
    cl::CommandQueue clCmdQueue1, clCmdQueue2;
    
    cl::Buffer clBuffer1, clBuffer2;
    cl::Buffer clBuffer3, clBuffer4;
    
    cxuint workFactor;
    cxuint blocksNum;
    cxuint passItersNum;
    cxuint kitersNum;
    bool useInputAndOutput;
    
    size_t bufItemsNum;
    
    float* initialValues;
    float* toCompare;
    float* results;
    
    size_t clKernelSourceSize;
    const char* clKernelSource;
    
    bool usePolyWalker;
    
    cl::Program clProgram;
    cl::Kernel clKernel;
    
    size_t groupSize;
    size_t workSize;
    
    bool failed;
    std::string failMessage;
    
    void printBuildLog();
    void printStatus(cxuint passNum);
    void throwFailedComputations(cxuint passNum);
    
    bool failedWithOptOptions;
    
    void buildKernel(cxuint kitersNum, cxuint blocksNum, bool alwaysPrintBuildLog);
    void calibrateKernel();
public:
    GPUStressTester(cxuint id, cl::Platform& clPlatform, cl::Device& clDevice,
                    const GPUStressConfig& config);
    ~GPUStressTester();
    
    void runTest();
    
    bool isFailed() const
    { return failed; }
    const std::string& getFailMessage() const
    { return failMessage; }
};

GPUStressTester::GPUStressTester(cxuint _id, cl::Platform& clPlatform, cl::Device& _clDevice,
        const GPUStressConfig& config) :
        id(_id), clDevice(_clDevice), workFactor(config.workFactor),
        blocksNum(config.blocksNum), passItersNum(config.passItersNum),
        kitersNum(config.kitersNum), useInputAndOutput(config.inputAndOutput),
        initialValues(nullptr), toCompare(nullptr)
{
    failed = false;
    failedWithOptOptions = false;
    usePolyWalker = false;
    
    clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
    clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
    
    cl_uint maxComputeUnits;
    clDevice.getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &groupSize);
    clDevice.getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &maxComputeUnits);
    
    workSize = size_t(maxComputeUnits)*groupSize*workFactor;
    bufItemsNum = (workSize<<4)*blocksNum;
    
    {
        double devMemReqs = 0.0;
        if (useInputAndOutput!=0)
            devMemReqs = (bufItemsNum<<4)/(1048576.0);
        else
            devMemReqs = (bufItemsNum<<3)/(1048576.0);
        
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cout << "Preparing StressTester for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName <<
                "\n    SetUp: workSize=" << workSize <<
                ", memory=" << devMemReqs << " MB"
                ", workFactor=" << workFactor <<
                ", blocksNum=" << blocksNum <<
                ",\n      computeUnits=" << maxComputeUnits <<
                ", groupSize=" << groupSize <<
                ", passIters=" << passItersNum << 
                ", builtinKernel=" << config.builtinKernel <<
                ",\n      inputAndOutput=" << (useInputAndOutput?"yes":"no") << std::endl;
    }
    
    switch(config.builtinKernel)
    {
        case 0:
            clKernelSource = clKernel1Source;
            break;
        case 1:
            clKernelSource = clKernel2Source;
            break;
        case 2:
            clKernelSource = clKernelPWSource;
            usePolyWalker = true;
            break;
        default:
            throw MyException("Unsupported builtin kernel!");
            break;
    }
    clKernelSourceSize = ::strlen(clKernelSource);
    
    cl_context_properties clContextProps[5];
    clContextProps[0] = CL_CONTEXT_PLATFORM;
    clContextProps[1] = reinterpret_cast<cl_context_properties>(clPlatform());
    clContextProps[2] = 0;
    clContext = cl::Context(clDevice, clContextProps);
    
    clCmdQueue1 = cl::CommandQueue(clContext, clDevice);
    clCmdQueue2 = cl::CommandQueue(clContext, clDevice);
    
    clBuffer1 = cl::Buffer(clContext, CL_MEM_READ_WRITE, bufItemsNum<<2);
    if (useInputAndOutput!=0)
        clBuffer2 = cl::Buffer(clContext, CL_MEM_READ_WRITE, bufItemsNum<<2);
    clBuffer3 = cl::Buffer(clContext, CL_MEM_READ_WRITE, bufItemsNum<<2);
    if (useInputAndOutput!=0)
        clBuffer4 = cl::Buffer(clContext, CL_MEM_READ_WRITE, bufItemsNum<<2);
    
    initialValues = new float[bufItemsNum];
    toCompare = new float[bufItemsNum];
    results = new float[bufItemsNum];
    
    std::mt19937_64 random;
    if (!usePolyWalker)
    {
        for (size_t i = 0; i < bufItemsNum; i++)
            initialValues[i] = (float(random())/float(
                        std::mt19937_64::max()-std::mt19937_64::min())-0.5f)*0.04f;
    }
    else
    {   /* data for polywalker */
        for (size_t i = 0; i < bufItemsNum; i++)
            initialValues[i] = (float(random())/float(
                        std::mt19937_64::max()-std::mt19937_64::min()))*2e6 - 1e6;
    }
    /*for (size_t i = 0; i < workSize<<5; i++)
        std::cout << "in=" << i << ":" << initialValues[i] << '\n';
    std::cout.flush();*/
    
    calibrateKernel();
    
    clCmdQueue1.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
            initialValues);
    
    clKernel.setArg(0, cl_uint(workSize));
    if (usePolyWalker)
    {
        clKernel.setArg(3, examplePoly[0]);
        clKernel.setArg(4, examplePoly[1]);
        clKernel.setArg(5, examplePoly[2]);
        clKernel.setArg(6, examplePoly[3]);
        clKernel.setArg(7, examplePoly[4]);
    }
    /* generate values to compare */
    if (useInputAndOutput == 0)
    {
        clKernel.setArg(1, clBuffer1);
        clKernel.setArg(2, clBuffer1);
    }
    
    for (cxuint i = 0; i < passItersNum; i++)
    {
        if (useInputAndOutput != 0)
        {
            if ((i&1) == 0)
            {
                clKernel.setArg(1, clBuffer1);
                clKernel.setArg(2, clBuffer2);
            }
            else
            {
                clKernel.setArg(1, clBuffer2);
                clKernel.setArg(2, clBuffer1);
            }
        }
        cl::Event clEvent;
        clCmdQueue1.enqueueNDRangeKernel(clKernel, cl::NDRange(0),
                cl::NDRange(workSize), cl::NDRange(groupSize), nullptr, &clEvent);
        try
        { clEvent.wait(); }
        catch(const cl::Error& err)
        {
            if (err.err() != CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
                throw; // if other error
            int eventStatus;
            clEvent.getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
            char strBuf[64];
            snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
            throw MyException(strBuf);
        }
    }
    
    // get results
    if (useInputAndOutput == 0 || (passItersNum&1) == 0)
        clCmdQueue1.enqueueReadBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                    toCompare);
    else //
        clCmdQueue1.enqueueReadBuffer(clBuffer2, CL_TRUE, size_t(0), bufItemsNum<<2,
                    toCompare);
    
    // print results
    /*for (size_t i = 0; i < bufItemsNum; i++)
        std::cout << "out=" << i << ":" << toCompare[i] << '\n';
    std::cout.flush();*/
}

GPUStressTester::~GPUStressTester()
{
    delete[] toCompare;
    delete[] initialValues;
    delete[] results;
}

void GPUStressTester::buildKernel(cxuint thisKitersNum, cxuint thisBlocksNum,
                bool alwaysPrintBuildLog)
{   // freeing resources
    clKernel = cl::Kernel();
    clProgram = cl::Program();
    
    cl::Program::Sources clSources;
    clSources.push_back(std::make_pair(clKernelSource, clKernelSourceSize));
    clProgram = cl::Program(clContext, clSources);
    
    char buildOptions[128];
    try
    {
        if (!failedWithOptOptions)
            snprintf(buildOptions, 128, "-O3 -DGROUPSIZE=" SIZE_T_SPEC
                "U -DKITERSNUM=%uU -DBLOCKSNUM=%uU",
                groupSize, thisKitersNum, thisBlocksNum);
        else //
            snprintf(buildOptions, 128, "-DGROUPSIZE=" SIZE_T_SPEC
                "U -DKITERSNUM=%uU -DBLOCKSNUM=%uU",
                groupSize, thisKitersNum, thisBlocksNum);
        clProgram.build(buildOptions);
    }
    catch(const cl::Error& error)
    {
        if (!failedWithOptOptions && (error.err() == CL_INVALID_BUILD_OPTIONS ||
            error.err() == CL_BUILD_PROGRAM_FAILURE))
        {   // try with no opt options
            {   /* fix for POCL (its needed???) */
                std::lock_guard<std::mutex> l(stdOutputMutex);
                std::cout <<
                    "Trying to compile without optimizing compiler flags..." << std::endl;
            }
            failedWithOptOptions = true;
            snprintf(buildOptions, 128, "-DGROUPSIZE=" SIZE_T_SPEC
                    "U -DKITERSNUM=%uU -DBLOCKSNUM=%uU",
                    groupSize, thisKitersNum, thisBlocksNum);
            try
            { clProgram.build(buildOptions); }
            catch(const cl::Error& error)
            {
                printBuildLog();
                throw;
            }
        }
        else
        {
            printBuildLog();
            throw;
        }
    }
    if (alwaysPrintBuildLog)
        printBuildLog();
    clKernel = cl::Kernel(clProgram, "gpuStress");
    
    // fixing groupSize and workSize if needed and if possible
    size_t newGroupSize;
    clKernel.getWorkGroupInfo(clDevice, CL_KERNEL_WORK_GROUP_SIZE, &newGroupSize);
    if (groupSize > newGroupSize)
    {   // fix it
        cxuint shifts = 0;
        size_t v;
        for (shifts = 0, v = groupSize; v > newGroupSize; v>>=1, shifts++);
        
        if ((groupSize&((1ULL<<shifts)-1ULL)) != 0)
            throw MyException("Cant determine new group size!");
        
        groupSize = groupSize>>shifts;
        workFactor <<= shifts;
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            std::cout << "Fixed groupSize for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName <<
                "\n    SetUp: workFactor=" << workFactor <<
                ", groupSize=" << groupSize << std::endl;
        }
        buildKernel(thisKitersNum, thisBlocksNum, alwaysPrintBuildLog);
    }
}

void GPUStressTester::calibrateKernel()
{
    cxuint bestKitersNum = 1;
    double bestBandwidth = 0.0;
    double bestPerf = 0.0;
    
    clCmdQueue1.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
            initialValues);
    
    if (kitersNum == 0)
    {
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            std::cout << "Calibrating Kernel for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName << "..." << std::endl;
        }
        
        cl::CommandQueue profCmdQueue(clContext, clDevice, CL_QUEUE_PROFILING_ENABLE);
        
        for (cxuint curKitersNum = 1; curKitersNum <= 30; curKitersNum++)
        {
            buildKernel(curKitersNum, blocksNum, false);
            
            clKernel.setArg(0, cl_uint(workSize));
            clKernel.setArg(1, clBuffer1);
            if (useInputAndOutput != 0)
                clKernel.setArg(2, clBuffer2);
            else
                clKernel.setArg(2, clBuffer1);
            
            if (usePolyWalker)
            {
                clKernel.setArg(3, examplePoly[0]);
                clKernel.setArg(4, examplePoly[1]);
                clKernel.setArg(5, examplePoly[2]);
                clKernel.setArg(6, examplePoly[3]);
                clKernel.setArg(7, examplePoly[4]);
            }
            
            cl_ulong kernelTimes[5];
            for (cxuint k = 0; k < 5; k++)
            {
                cl::Event profEvent;
                profCmdQueue.enqueueNDRangeKernel(clKernel, cl::NDRange(0),
                        cl::NDRange(workSize), cl::NDRange(groupSize), nullptr, &profEvent);
                try
                { profEvent.wait(); }
                catch(const cl::Error& err)
                {
                    if (err.err() != CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
                        throw; // if other error
                    int eventStatus;
                    profEvent.getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
                    char strBuf[64];
                    snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
                    throw MyException(strBuf);
                }
                
                cl_ulong eventStartTime, eventEndTime;
                profEvent.getProfilingInfo(CL_PROFILING_COMMAND_START, &eventStartTime);
                profEvent.getProfilingInfo(CL_PROFILING_COMMAND_END, &eventEndTime);
                kernelTimes[k] = eventEndTime-eventStartTime;
            }
            
            // sort kernels times
            for (cxuint k = 0; k < 5; k++)
            {
                for (cxuint l = k+1; l < 5; l++)
                    if (kernelTimes[k]>kernelTimes[l])
                        std::swap(kernelTimes[k], kernelTimes[l]);
                //std::cout << "SortedTime: " << kernelTimes[k] << std::endl;
            }
            
            cxuint acceptedToAvg = 1;
            for (; acceptedToAvg < 5; acceptedToAvg++)
                if (double(kernelTimes[acceptedToAvg]-kernelTimes[0]) >
                            double(kernelTimes[0])*0.07)
                    break;
            //std::cout << "acceptedToAvg: " << acceptedToAvg << std::endl;
            const cl_ulong currentTime =
                std::accumulate(kernelTimes, kernelTimes+acceptedToAvg, 0ULL)/acceptedToAvg;
            /*std::cout << "avg is: " << currentTime << std::endl;
            std::cout << "..." << std::endl;*/
            
            double currentBandwidth;
            currentBandwidth = 2.0*4.0*double(bufItemsNum) / double(currentTime);
            double currentPerf;
            if (!usePolyWalker)
                currentPerf = 2.0*3.0*double(curKitersNum)*double(bufItemsNum) /
                        double(currentTime);
            else
                currentPerf = 8.0*double(curKitersNum)*double(bufItemsNum) /
                        double(currentTime);
            
            if (currentBandwidth*currentPerf > bestBandwidth*bestPerf)
            {
                bestKitersNum = curKitersNum;
                bestPerf = currentPerf;
                bestBandwidth = currentBandwidth;
            }
            /*{
                std::lock_guard<std::mutex> l(stdOutputMutex);
                std::cout << "Choose for kernel \n  " <<
                    "#" << id << " " << platformName << ":" << deviceName << "\n"
                    "  BestKitersNum: " << curKitersNum << ", Bandwidth: " << currentBandwidth <<
                    " GB/s, Performance: " << currentPerf << " GFLOPS" << std::endl;
            }*/
        }
        /* if choosen we compile real code */
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            std::cout << "Kernel Calibrated for\n  " <<
                    "#" << id << " " << platformName << ":" << deviceName << "\n"
                    "  BestKitersNum: " << bestKitersNum << ", Bandwidth: " << bestBandwidth <<
                    " GB/s, Performance: " << bestPerf << " GFLOPS" << std::endl;
        }
    }
    else
    {
        bestKitersNum = kitersNum;
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cout << "Kernel KitersNum: " << bestKitersNum << std::endl;
    }
    
    kitersNum = bestKitersNum;
    
    buildKernel(kitersNum, blocksNum, true);
}

void GPUStressTester::printBuildLog()
{
    std::string buildLog;
    clProgram.getBuildInfo(clDevice, CL_PROGRAM_BUILD_LOG, &buildLog);
    std::lock_guard<std::mutex> l(stdOutputMutex);
    std::cout << "Program build log:\n  " <<
            platformName << ":" << deviceName << "\n:--------------------\n" <<
            buildLog << std::endl;
}

void GPUStressTester::printStatus(cxuint passNum)
{
    if ((passNum%10) != 0)
        return;
    const time_point currentTime = std::chrono::high_resolution_clock::now();
    const int64_t nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                currentTime-lastTime).count();
    lastTime = currentTime;
    
    double bandwidth, perf;
    bandwidth = 2.0*10.0*4.0*double(passItersNum)*double(bufItemsNum) / double(nanos);
    if (!usePolyWalker)
        perf = 2.0*10.0*3.0*double(kitersNum)*double(passItersNum)*double(bufItemsNum)
                / double(nanos);
    else
        perf = 10.0*8.0*double(kitersNum)*double(passItersNum)*double(bufItemsNum)
                / double(nanos);
    
    const int64_t startMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime-startTime).count();
    char timeStrBuf[128];
    snprintf(timeStrBuf, 128, "%02u:%02u:%02u.%03u", cxuint(startMillis/3600000),
             cxuint((startMillis/60000)%60), cxuint((startMillis/1000)%60),
             cxuint(startMillis%1000));
    
    std::lock_guard<std::mutex> l(stdOutputMutex);
    std::cout << "#" << id << " " << platformName << ":" << deviceName <<
            " passed PASS #" << passNum << "\n"
            "Approx. bandwidth: " << bandwidth << " GB/s, "
            "Approx. perf: " << perf << " GFLOPS, elapsed: " << timeStrBuf << std::endl;
}

void GPUStressTester::throwFailedComputations(cxuint passNum)
{
    const time_point currentTime = std::chrono::high_resolution_clock::now();
    const int64_t startMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime-startTime).count();
    char strBuf[128];
    snprintf(strBuf, 128,
             "FAILED COMPUTATIONS!!!! PASS #%u, Elapsed time: %02u:%02u:%02u.%03u",
             passNum, cxuint(startMillis/3600000), cxuint((startMillis/60000)%60),
             cxuint((startMillis/1000)%60), cxuint(startMillis%1000));
    if (!exitIfAllFails)
        stopAllStressTesters.store(true);
    throw MyException(strBuf);
}

void GPUStressTester::runTest()
try
{
    bool run1Exec = false;
    bool run2Exec = false;
    std::vector<cl::Event> exec1Events(passItersNum);
    std::vector<cl::Event> exec2Events(passItersNum);
    clKernel.setArg(0, cl_uint(workSize));
    if (usePolyWalker)
    {
        clKernel.setArg(3, examplePoly[0]);
        clKernel.setArg(4, examplePoly[1]);
        clKernel.setArg(5, examplePoly[2]);
        clKernel.setArg(6, examplePoly[3]);
        clKernel.setArg(7, examplePoly[4]);
    }
    
    try
    {
    cxuint pass1Num = 1;
    cxuint pass2Num = 2;
    
    startTime = lastTime = std::chrono::high_resolution_clock::now();
    
    do {
        if (stopAllStressTesters.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            std::cout << "Exiting, because some device failed." << std::endl;
            break;
        }
        
        clCmdQueue2.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                initialValues);
        /* run execution 1 */
        if (useInputAndOutput == 0)
        {
            clKernel.setArg(1, clBuffer1);
            clKernel.setArg(2, clBuffer1);
        }
        
        for (cxuint i = 0; i < passItersNum; i++)
        {
            if (useInputAndOutput != 0)
            {
                if ((i&1) == 0)
                {
                    clKernel.setArg(1, clBuffer1);
                    clKernel.setArg(2, clBuffer2);
                }
                else
                {
                    clKernel.setArg(1, clBuffer2);
                    clKernel.setArg(2, clBuffer1);
                }
            }
            clCmdQueue1.enqueueNDRangeKernel(clKernel, cl::NDRange(0),
                    cl::NDRange(workSize), cl::NDRange(groupSize), nullptr, &exec1Events[i]);
        }
        run1Exec = true;
        
        if (run2Exec)
        {   /* after exec2 */
            try
            { exec2Events[passItersNum-1].wait(); }
            catch(const cl::Error& err)
            {
                if (err.err() != CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
                    throw; // if other error
            }
            run2Exec = false;
            for (cxuint i = 0; i < passItersNum; i++)
            {   // check kernel event status
                int eventStatus;
                exec2Events[i].getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
                if (eventStatus < 0)
                {
                    char strBuf[64];
                    snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
                    throw MyException(strBuf);
                }
                exec2Events[i] = cl::Event(); // release event
            }
            // get results
            if (useInputAndOutput == 0 || (passItersNum&1) == 0)
                clCmdQueue2.enqueueReadBuffer(clBuffer3, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            else //
                clCmdQueue2.enqueueReadBuffer(clBuffer4, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            if (::memcmp(toCompare, results, bufItemsNum<<2))
                throwFailedComputations(pass2Num);
            printStatus(pass2Num);
            pass2Num += 2;
        }
        
        if (stopAllStressTesters.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            std::cout << "Exiting, because some device failed." << std::endl;
            break;
        }
        
        clCmdQueue2.enqueueWriteBuffer(clBuffer3, CL_TRUE, size_t(0), bufItemsNum<<2,
                initialValues);
        /* run execution 2 */
        if (useInputAndOutput == 0)
        {
            clKernel.setArg(1, clBuffer3);
            clKernel.setArg(2, clBuffer3);
        }
        
        for (cxuint i = 0; i < passItersNum; i++)
        {
            if (useInputAndOutput != 0)
            {
                if ((i&1) == 0)
                {
                    clKernel.setArg(1, clBuffer3);
                    clKernel.setArg(2, clBuffer4);
                }
                else
                {
                    clKernel.setArg(1, clBuffer4);
                    clKernel.setArg(2, clBuffer3);
                }
            }
            clCmdQueue1.enqueueNDRangeKernel(clKernel, cl::NDRange(0),
                    cl::NDRange(workSize), cl::NDRange(groupSize), nullptr, &exec2Events[i]);
        }
        run2Exec = true;
        
        if (run1Exec)
        {   /* after exec1 */
            try
            { exec1Events[passItersNum-1].wait(); }
            catch(const cl::Error& err)
            {
                if (err.err() != CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
                    throw; // if other error
            }
            run1Exec = false;
            for (cxuint i = 0; i < passItersNum; i++)
            {   // check kernel event status
                int eventStatus;
                exec1Events[i].getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
                if (eventStatus < 0)
                {
                    char strBuf[64];
                    snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
                    throw MyException(strBuf);
                }
                exec1Events[i] = cl::Event(); // release event
            }
            // get results
            if (useInputAndOutput == 0 || (passItersNum&1) == 0)
                clCmdQueue2.enqueueReadBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            else //
                clCmdQueue2.enqueueReadBuffer(clBuffer2, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            if (::memcmp(toCompare, results, bufItemsNum<<2))
                throwFailedComputations(pass1Num);
            printStatus(pass1Num);
            pass1Num += 2;
        }
    } while (run1Exec || run2Exec);
    }
    catch(...)
    {   /* wait for finish kernels */
        try
        { clCmdQueue1.finish(); }
        catch(...)
        { }
        try
        { clCmdQueue2.finish(); }
        catch(...)
        { }
        throw;
    }
    try
    { clCmdQueue1.finish(); }
    catch(...)
    { }
    try
    { clCmdQueue2.finish(); }
    catch(...)
    { }
}
catch(const cl::Error& error)
{
    failed = true;
    try
    {
        char codeBuf[64];
        snprintf(codeBuf, 64, ", Code: %d", error.err());
        failMessage = "OpenCL error happened: ";
        failMessage += error.what();
        failMessage += codeBuf;
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cerr << "Failed StressTester for\n  " <<
                "#" << id  << " " << platformName << ":" << deviceName << ": " <<
                failMessage << std::endl;
    }
    catch(...)
    { } // fatal exception!!!
}
catch(const std::exception& ex)
{
    failed = true;
    try
    {
        failMessage = "Exception happened: ";
        failMessage += ex.what();
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cerr << "Failed StressTester for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName << ":\n    " <<
                failMessage << std::endl;
    }
    catch(...)
    { } // fatal exception!!!
}
catch(...)
{
    failed = true;
    try
    {
        failMessage = "Unknown exception happened";
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cerr << "Failed StressTester for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName << ":\n    " <<
                failMessage << std::endl;
    }
    catch(...)
    { } // fatal exception!!!
}

int main(int argc, const char** argv)
{
    int cmd;
    poptContext optsContext;
    
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
        return 1;
    }
    
    std::cout << "CLGPUStress 0.0.4.2 by Mateusz Szpakowski. "
        "Program is distributed under terms of the GPLv2." << std::endl;
    
    if (listAllDevices)
    {
        listCLDevices();
        return 0;
    }
        
    if (!useGPUs && !useCPUs)
        useGPUs = 1;
    if (!useAMDPlatform && !useNVIDIAPlatform && !useIntelPlatform)
        useAllPlatforms = true;
    
    int retVal = 0;
    
    std::vector<GPUStressConfig> gpuStressConfigs;
    std::vector<GPUStressTester*> gpuStressTesters;
    std::vector<std::thread*> testerThreads;
    try
    {
        std::vector<std::pair<cl::Platform, cl::Device> > choosenCLDevices;
        if (devicesListString == nullptr)
            choosenCLDevices = getChoosenCLDevices();
        else
            choosenCLDevices = getChoosenCLDevicesFromList(devicesListString);
        if (choosenCLDevices.empty())
            throw MyException("OpenCL devices not found!");
        
        if (listChoosenDevices != 0)
        {
            listChoosenCLDevices(choosenCLDevices);
            return 0;
        }
        
        {
            if (inputAndOutputsString == nullptr && globalInputAndOutput)
                inputAndOutputsString = "1";
            std::vector<cxuint> workFactors =
                    parseCmdUIntList(workFactorsString, "work factors");
            std::vector<cxuint> passItersNums =
                    parseCmdUIntList(passItersNumsString, "passIters numbers");
            std::vector<cxuint> blocksNums =
                    parseCmdUIntList(blocksNumsString, "blocks numbers");
            std::vector<cxuint> kitersNums =
                    parseCmdUIntList(kitersNumsString, "kiters numbers");
            std::vector<cxuint> builtinKernels =
                    parseCmdUIntList(builtinKernelsString, "builtin kernels");
            std::vector<bool> inputAndOutputs =
                    parseCmdBoolList(inputAndOutputsString, "inputAndOutputs");
            
            gpuStressConfigs = collectGPUStressConfigs(choosenCLDevices.size(),
                    passItersNums, workFactors, blocksNums, kitersNums,
                    builtinKernels, inputAndOutputs);
        }
        
        std::cout <<
            "\nWARNING: THIS PROGRAM CAN OVERHEAT OR DAMAGE "
            "YOUR GRAPHICS CARD FASTER (AND BETTER)\n"
            "THAN ANY FURMARK STRESS. PLEASE USE THIS PROGRAM VERY CAREFULLY!!!\n"
            "RECOMMEND TO RUN THIS PROGRAM ON STOCK PARAMETERS "
            "(CLOCKS, VOLTAGES,\nESPECIALLY MEMORY CLOCK).\n"
            "TO TERMINATE THIS PROGRAM PLEASE USE STANDARD CTRL-C.\n" << std::endl;
        if (exitIfAllFails && choosenCLDevices.size() > 1)
            std::cout << "PROGRAM EXITS ONLY WHEN ALL DEVICES FAILS.\n"
                "PLEASE TRACE OUTPUT TO FIND FAILED DEVICE AND REACT!\n" << std::endl;
        if (dontWait==0)
            std::this_thread::sleep_for(std::chrono::milliseconds(8000));
        
        for (size_t i = 0; i < choosenCLDevices.size(); i++)
        {
            auto& p = choosenCLDevices[i];
            gpuStressTesters.push_back(new GPUStressTester(i, p.first, p.second,
                        gpuStressConfigs[i]));
        }
        
        for (size_t i = 0; i < choosenCLDevices.size(); i++)
            testerThreads.push_back(new std::thread(
                    &GPUStressTester::runTest, gpuStressTesters[i]));
    }
    catch(const cl::Error& error)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cerr << "OpenCL error happened: " << error.what() <<
                ", Code: " << error.err() << std::endl;
        retVal = 1;
    }
    catch(const std::exception& ex)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cerr << "Exception happened: " << ex.what() << std::endl;
        retVal = 1;
    }
    // clean up
    for (size_t i = 0; i < testerThreads.size(); i++)
        if (testerThreads[i] != nullptr)
        {
            try
            { testerThreads[i]->join(); }
            catch(const std::exception& ex)
            {
                std::lock_guard<std::mutex> l(stdOutputMutex);
                std::cerr << "Failed join for stress thread #" << i << "!!!" << std::endl;
                retVal = 1;
            }
            delete testerThreads[i];
            testerThreads[i] = nullptr;
            std::lock_guard<std::mutex> l(stdOutputMutex);
            if (gpuStressTesters.size() > i &&
                gpuStressTesters[i] != nullptr && !gpuStressTesters[i]->isFailed())
                std::cout << "Finished #" << i << std::endl;
        }
    for (size_t i = 0; i < gpuStressTesters.size(); i++)
    {
        if (gpuStressTesters[i]->isFailed())
        {
            retVal = 1;
            std::lock_guard<std::mutex> l(stdOutputMutex);
            std::cerr << "Failed #" << i << std::endl;
        }
        delete gpuStressTesters[i];
    }
    
    poptFreeContext(optsContext);
    return retVal;
}

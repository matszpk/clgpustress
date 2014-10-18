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

#include <cstdio>
#include <cmath>
#include <cstring>
#include <utility>
#include <set>
#include <cmath>
#ifdef _WINDOWS
#include <thread>
#include <windows.h>
#endif
#include "gpustress-core.h"

#ifdef _MSC_VER
#  define snprintf _snprintf
#  define SIZE_T_SPEC "%Iu"
#else
#  define SIZE_T_SPEC "%zu"
#endif

MyException::MyException()
{ }

MyException::MyException(const std::string& msg)
{
    this->message = msg;
}

MyException::~MyException() throw()
{ }

const char* MyException::what() const throw()
{
    return message.c_str();
}

#ifdef _WINDOWS
namespace
{
    static int64_t initGFrequency()
    {
        LARGE_INTEGER w;
        OSVERSIONINFO verInfo;
        GetVersionEx(&verInfo);
        if (!QueryPerformanceFrequency(&w) || w.QuadPart==0 ||
            /* trust QPC from Vista or later Windows,
             * otherwise check whether based on TSC (if yes we do not use) */
            (verInfo.dwMajorVersion < 6 && w.QuadPart >= 100000000ULL))
            return 0;
        return w.QuadPart;
    }
    static int64_t g_Frequency = initGFrequency();
};

SteadyClock::time_point SteadyClock::now()
{
    if (g_Frequency == 0)
        return time_point(std::chrono::duration_cast<duration>(
            std::chrono::system_clock::now().time_since_epoch()));
    
    LARGE_INTEGER w;
    if (!QueryPerformanceCounter(&w))
        throw MyException("Can't get QPC clock time!");
    
    const int64_t lx = int64_t(w.LowPart) * int64_t(period::den);
    const int64_t lp = lx / g_Frequency;
    const int64_t lmod = lx - lp*g_Frequency;
    const int64_t x = int64_t(period::den)<<32;
    const int64_t ih = x/g_Frequency;
    const int64_t ihmod = x - ih*g_Frequency;
    const int64_t f = lp + int64_t(w.HighPart)*ih +
            (int64_t(w.HighPart)*ihmod + lmod)/g_Frequency;
    return time_point(duration(f));
}

bool isQPCClockChoosen()
{
    return g_Frequency!=0;
}

bool verifyQPCClock()
{
    const std::chrono::time_point<SteadyClock> start = SteadyClock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    const std::chrono::time_point<SteadyClock> end = SteadyClock::now();
    double nanos = double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                end-start).count())*1.0e-9;
    
    double diff = ::fabs(nanos-2.0)*0.5;
    if (diff > 0.015)
    {
        g_Frequency = 0;
        return false;
    }
    return true;
}
#endif

std::string trimSpaces(const std::string& s)
{
    std::string::size_type pos = s.find_first_not_of(" \n\t\r\v\f");
    if (pos == std::string::npos)
        return "";
    std::string::size_type endPos = s.find_last_not_of(" \n\t\r\v\f");
    return s.substr(pos, endPos+1-pos);
}

int useCPUs = 0;
int useGPUs = 0;
int useAccelerators = 0;
int useAMDPlatform = 0;
int useNVIDIAPlatform = 0;
int useIntelPlatform = 0;
bool useAllPlatforms = false;

std::vector<cxuint> parseCmdUIntList(const char* str, const char* name)
{
    std::vector<cxuint> outVector;
    if (str == nullptr)
        return outVector;
    
    const char* p = str;
    while (*p != 0)
    {
        cxuint val;
        if (sscanf(p, "%u", &val) != 1)
            throw MyException(std::string("Can't parse ")+name);
        outVector.push_back(val);
        
        while (*p != 0 && *p != ',') p++;
        if (*p == ',') // next elem in list
        {
            p++;
            if (*p == 0)
                throw MyException(std::string("Can't parse ")+name);
        }
    }
    return outVector;
}

std::vector<bool> parseCmdBoolList(const char* str, const char* name)
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
            throw MyException(std::string("Can't parse ")+name);
    }
    return outVector;
}

std::vector<cl::Device> getChoosenCLDevices()
{
    std::vector<cl::Device> outDevices;
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
        
        try
        { clPlatform.getDevices(deviceType, &clDevices); }
        catch(const cl::Error& err)
        {
            if (err.err() != CL_DEVICE_NOT_FOUND && err.err() != CL_INVALID_VALUE &&
                err.err() != CL_INVALID_DEVICE_TYPE)
                throw;
        }
        outDevices.insert(outDevices.end(), clDevices.begin(), clDevices.end());
    }
    return outDevices;
}

std::vector<cl::Device> getChoosenCLDevicesFromList(const char* str)
{
    std::vector<cl::Device> outDevices;
    std::vector<cl::Platform> clPlatforms;
    cl::Platform::get(&clPlatforms);
    std::vector<std::vector<cl::Device> > clDevices(clPlatforms.size());
    
    std::set<std::pair<cxuint,cxuint> > deviceIdsSet;
    const char* p = str;
    while (*p != 0)
    {
        cxuint platformId, deviceId;
        if (sscanf(p, "%u:%u", &platformId, &deviceId) != 2)
            throw MyException("Can't parse device list");
        
        if (platformId >= clPlatforms.size())
            throw MyException("PlatformID out of range");
        cl::Platform clPlatform = clPlatforms[platformId];
        
        if (clDevices[platformId].empty())
            clPlatform.getDevices(CL_DEVICE_TYPE_ALL, &clDevices[platformId]);
        if (deviceId >= clDevices[platformId].size())
            throw MyException("DeviceID out of range");
        
        if (!deviceIdsSet.insert(std::make_pair(platformId, deviceId)).second)
            throw MyException("Duplicated devices in device list!");
        
        cl::Device clDevice = clDevices[platformId][deviceId];
        outDevices.push_back(clDevice);
        
        while (*p != 0 && *p != ',') p++;
        if (*p == ',') // next elem in list
        {
            p++;
            if (*p == 0)
                throw MyException("Can't parse device list");
        }
    }
    
    return outDevices;
}

std::vector<GPUStressConfig> collectGPUStressConfigs(cxuint devicesNum,
        const std::vector<cxuint>& passItersNumVec, const std::vector<cxuint>& groupSizeVec,
        const std::vector<cxuint>& workFactorVec,
        const std::vector<cxuint>& blocksNumVec, const std::vector<cxuint>& kitersNumVec,
        const std::vector<cxuint>& builtinKernelVec, const std::vector<bool>& inAndOutVec)
{
    if (passItersNumVec.size() > devicesNum)
        throw MyException("PassItersNum list is too long");
    if (groupSizeVec.size() > devicesNum)
        throw MyException("GroupSize list is too long");
    if (workFactorVec.size() > devicesNum)
        throw MyException("WorkFactor list is too long");
    if (blocksNumVec.size() > devicesNum)
        throw MyException("BlocksNum list is too long");
    if (kitersNumVec.size() > devicesNum)
        throw MyException("KitersNum list is too long");
    if (builtinKernelVec.size() > devicesNum)
        throw MyException("TestType list is too long");
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
        
        if (!groupSizeVec.empty())
            config.groupSize = (groupSizeVec.size() > i) ? groupSizeVec[i] :
                    groupSizeVec.back();
        else // default
            config.groupSize = 0;
        
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
        if (config.builtinKernel > 3)
            throw MyException("BuiltinKernel out of range");
        if (config.kitersNum > 100)
            throw MyException("KitersNum out of range");
        outConfigs[i] = config;
    }
    
    return outConfigs;
}

extern const char* clKernel1Source;
extern const char* clKernel2Source;
extern const char* clKernelPWSource;
extern const char* clKernelPW2Source;

int exitIfAllFails = 0;

std::mutex stdOutputMutex;
std::ostream* outStream = nullptr;
std::ostream* errStream = nullptr;

std::atomic<bool> stopAllStressTestersIfFail(false);
std::atomic<bool> stopAllStressTestersByUser(false);

OutputHandler outputHandler = nullptr;
void* outputHandlerData = nullptr;

void installOutputHandler(std::ostream* out, std::ostream* err,
                OutputHandler handler, void* data)
{
    outStream = out;
    errStream = err;
    outputHandler = handler;
    outputHandlerData = data;
}

static const float examplePoly[5] = 
{ 4.43859953e+05,   1.13454169e+00,  -4.50175916e-06, -1.43865531e-12,   4.42133541e-18 };

GPUStressTester::GPUStressTester(cxuint _id, cl::Device& _clDevice,
        const GPUStressConfig& config)
try :
        id(_id), workFactor(config.workFactor),
        blocksNum(config.blocksNum), passItersNum(config.passItersNum),
        kitersNum(config.kitersNum), useInputAndOutput(config.inputAndOutput),
        initialValues(nullptr), toCompare(nullptr), results(nullptr)
{
    initialized = false;
    failed = false;
    usePolyWalker = false;
    // set clDevice, after because can fails and pointers to free must be set
    clDevice = _clDevice;
    
    cl::Platform clPlatform;
    clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
    
    clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
    platformName = trimSpaces(platformName);
    clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
    deviceName = trimSpaces(deviceName);
    
    cl_uint maxComputeUnits;
    if (config.groupSize == 0)
        clDevice.getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &groupSize);
    else // from config
        groupSize = config.groupSize;
    clDevice.getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &maxComputeUnits);
    
    workSize = size_t(maxComputeUnits)*groupSize*workFactor;
    bufItemsNum = (workSize<<4)*blocksNum;
    
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
        case 3:
            clKernelSource = clKernelPW2Source;
            usePolyWalker = true;
            break;
        default:
            throw MyException("Unsupported builtin kernel!");
            break;
    }
    clKernelSourceSize = ::strlen(clKernelSource);
    
    {
        double devMemReqs = 0.0;
        if (useInputAndOutput)
            devMemReqs = (bufItemsNum<<4)/(1048576.0);
        else
            devMemReqs = (bufItemsNum<<3)/(1048576.0);
        
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *outStream << "Preparing StressTester for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName <<
                "\n    SetUp: workSize=" << workSize <<
                ", memory=" << devMemReqs << " MB"
                ", workFactor=" << workFactor <<
                ", blocksNum=" << blocksNum <<
                ",\n    computeUnits=" << maxComputeUnits <<
                ", groupSize=" << groupSize <<
                ", passIters=" << passItersNum <<
                ", testType=" << config.builtinKernel <<
                ",\n    inputAndOutput=" << (useInputAndOutput?"yes":"no") << std::endl;
        handleOutput(id);
    }
    
    if (stopAllStressTestersByUser.load())
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *outStream << "#" << id << " Exiting, because user stopped test." << std::endl;
        handleOutput(id);
        return;
    }
    
    cl_context_properties clContextProps[3];
    clContextProps[0] = CL_CONTEXT_PLATFORM;
    clContextProps[1] = reinterpret_cast<cl_context_properties>(clPlatform());
    clContextProps[2] = 0;
    clContext = cl::Context(clDevice, clContextProps);
    
    clCmdQueue1 = cl::CommandQueue(clContext, clDevice);
    clCmdQueue2 = cl::CommandQueue(clContext, clDevice);
    
    clBuffer1 = cl::Buffer(clContext, CL_MEM_READ_WRITE, bufItemsNum<<2);
    if (useInputAndOutput)
        clBuffer2 = cl::Buffer(clContext, CL_MEM_READ_WRITE, bufItemsNum<<2);
    clBuffer3 = cl::Buffer(clContext, CL_MEM_READ_WRITE, bufItemsNum<<2);
    if (useInputAndOutput)
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
    
    calibrateKernel();
    if (stopAllStressTestersByUser.load())
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *outStream << "#" << id << " Exiting, because user stopped test." << std::endl;
        handleOutput(id);
        return;
    }
    
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
    if (!useInputAndOutput)
    {
        clKernel.setArg(1, clBuffer1);
        clKernel.setArg(2, clBuffer1);
    }
    
    for (cxuint i = 0; i < passItersNum; i++)
    {
        if (stopAllStressTestersByUser.load())
            break; // skip this
        
        if (useInputAndOutput)
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
    
    if (stopAllStressTestersByUser.load())
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *outStream << "#" << id << " Exiting, because user stopped test." << std::endl;
        handleOutput(id);
        return;
    }
    
    // get results
    if (!useInputAndOutput || (passItersNum&1) == 0)
        clCmdQueue1.enqueueReadBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                    toCompare);
    else //
        clCmdQueue1.enqueueReadBuffer(clBuffer2, CL_TRUE, size_t(0), bufItemsNum<<2,
                    toCompare);
    
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *outStream << "#" << id << " Results for comparison has been generated." << std::endl;
        handleOutput(id);
    }
    
    // print results
    /*for (size_t i = 0; i < bufItemsNum; i++)
        *outStream << "out=" << i << ":" << toCompare[i] << '\n';
    outStream->flush();*/
    
    initialized = true;
}
catch(...)
{
    delete[] toCompare;
    delete[] initialValues;
    delete[] results;
    throw;
}

GPUStressTester::~GPUStressTester()
{
    delete[] toCompare;
    delete[] initialValues;
    delete[] results;
}

void GPUStressTester::buildKernel(cxuint thisKitersNum, cxuint thisBlocksNum,
                bool alwaysPrintBuildLog, bool whenCalibrates)
{   // freeing resources
    clKernel = cl::Kernel();
    clProgram = cl::Program();
    
    cl::Program::Sources clSources;
    clSources.push_back(std::make_pair(clKernelSource, clKernelSourceSize));
    clProgram = cl::Program(clContext, clSources);
    
    char buildOptions[128];
    try
    {
        snprintf(buildOptions, 128, "-DGROUPSIZE=" SIZE_T_SPEC
                "U -DKITERSNUM=%uU -DBLOCKSNUM=%uU",
                groupSize, thisKitersNum, thisBlocksNum);
        clProgram.build(buildOptions);
    }
    catch(const cl::Error& error)
    {
        printBuildLog();
        throw;
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
            throw MyException("Can't determine new group size!");
        
        groupSize >>= shifts;
        workFactor <<= shifts;
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            if (whenCalibrates)
                *outStream << std::endl;
            *outStream << "Fixed groupSize for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName <<
                "\n    SetUp: workFactor=" << workFactor <<
                ", groupSize=" << groupSize << std::endl;
            if (whenCalibrates)
            {
                *outStream << "  Calibration progress:";
                outStream->flush();
            }
            handleOutput(id);
        }
        buildKernel(thisKitersNum, thisBlocksNum, alwaysPrintBuildLog, whenCalibrates);
    }
}

void GPUStressTester::calibrateKernel()
{
    cxuint bestKitersNum = 1;
    double bestBandwidth = 0.0;
    double bestPerf = 0.0;
    cl_ulong bestKernelTime = CL_ULONG_MAX;
    cl_ulong kernelTime = 0;
    cl::CommandQueue profCmdQueue(clContext, clDevice, CL_QUEUE_PROFILING_ENABLE);
    
    const bool profileKernelAfterBuilt = (kitersNum != 0);
    if (kitersNum == 0)
    {
        if (useInputAndOutput)
            clCmdQueue1.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                    initialValues);
        
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "Calibrating Kernel for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName << "..." << std::endl;
            *outStream << "  Calibration progress:";
            outStream->flush();
            handleOutput(id);
        }
        
        try
        {
        for (cxuint curKitersNum = 1; curKitersNum <= 40; curKitersNum++)
        {
            if (stopAllStressTestersByUser.load())
            {
                std::lock_guard<std::mutex> l(stdOutputMutex);
                *outStream << std::endl;
                handleOutput(id);
                return;
            }
            
            if (((curKitersNum-1)%5) == 0)
            {   /* print progress of calibration */
                std::lock_guard<std::mutex> l(stdOutputMutex);
                *outStream << " " << ((curKitersNum-1)*100/40) << "%";
                outStream->flush();
                handleOutput(id);
            }
            buildKernel(curKitersNum, blocksNum, false, true);
            
            clKernel.setArg(0, cl_uint(workSize));
            clKernel.setArg(1, clBuffer1);
            if (useInputAndOutput)
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
                if (stopAllStressTestersByUser.load())
                {
                    std::lock_guard<std::mutex> l(stdOutputMutex);
                    *outStream << std::endl;
                    handleOutput(id);
                    return; // if stopped by user
                }
                
                if (!useInputAndOutput) // ensure always this same input data for kernel
                    clCmdQueue1.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0),
                            bufItemsNum<<2, initialValues);
                
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
                //*outStream << "SortedTime: " << kernelTimes[k] << std::endl;
            }
            
            cxuint acceptedToAvg = 1;
            for (; acceptedToAvg < 5; acceptedToAvg++)
                if (double(kernelTimes[acceptedToAvg]-kernelTimes[0]) >
                            double(kernelTimes[0])*0.07)
                    break;
            //*outStream << "acceptedToAvg: " << acceptedToAvg << std::endl;
            const cl_ulong currentTime =
                std::accumulate(kernelTimes, kernelTimes+acceptedToAvg, 0ULL)/acceptedToAvg;
            /* *outStream << "avg is: " << currentTime << std::endl;
            *outStream << "..." << std::endl;*/
            
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
                bestKernelTime = currentTime;
            }
        }
        } // try/catch
        catch(...)
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << std::endl;
            handleOutput(id);
            throw;
        }
        
        {   /* if choosen we compile real code */
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << " 100%" << std::endl;
            *outStream << "Kernel calibrated for\n  " <<
                    "#" << id << " " << platformName << ":" << deviceName << "\n"
                    "  BestKitersNum: " << bestKitersNum << ", Bandwidth: " << bestBandwidth <<
                    " GB/s, Performance: " << bestPerf << " GFLOPS" << std::endl;
            handleOutput(id);
        }
        
        kernelTime = bestKernelTime;
    }
    else
    {
        bestKitersNum = kitersNum;
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *outStream << "Kernel KitersNum: " << bestKitersNum << std::endl;
        handleOutput(id);
    }
    
    if (stopAllStressTestersByUser.load())
        return;
    kitersNum = bestKitersNum;
    buildKernel(kitersNum, blocksNum, true, false);
    
    if (profileKernelAfterBuilt)
    {
        if (useInputAndOutput)
            clCmdQueue1.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                    initialValues);
        
        clKernel.setArg(0, cl_uint(workSize));
        clKernel.setArg(1, clBuffer1);
        if (useInputAndOutput)
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
            if (stopAllStressTestersByUser.load())
                return; // if stopped by user
            
            if (!useInputAndOutput) // ensure always this same input data for kernel
                clCmdQueue1.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0),
                        bufItemsNum<<2, initialValues);
            
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
            //*outStream << "SortedTime: " << kernelTimes[k] << std::endl;
        }
        
        cxuint acceptedToAvg = 1;
        for (; acceptedToAvg < 5; acceptedToAvg++)
            if (double(kernelTimes[acceptedToAvg]-kernelTimes[0]) >
                        double(kernelTimes[0])*0.07)
                break;
        //*outStream << "acceptedToAvg: " << acceptedToAvg << std::endl;
        kernelTime = std::accumulate(kernelTimes, kernelTimes+acceptedToAvg, 0ULL)/acceptedToAvg;
        
        double currentBandwidth;
        currentBandwidth = 2.0*4.0*double(bufItemsNum) / double(kernelTime);
        double currentPerf;
        if (!usePolyWalker)
            currentPerf = 2.0*3.0*double(kitersNum)*double(bufItemsNum) /
                    double(kernelTime);
        else
            currentPerf = 8.0*double(kitersNum)*double(bufItemsNum) /
                    double(kernelTime);
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "Kernel performance for\n  " <<
                    "#" << id << " " << platformName << ":" << deviceName << "\n"
                    "  KitersNum: " << kitersNum << ", Bandwidth: " << currentBandwidth <<
                    " GB/s, Performance: " << currentPerf << " GFLOPS" << std::endl;
            handleOutput(id);
        }
    }
    
    // determine how many iterations can be queued at same time
    if (kernelTime != 0)
        stepsPerWait = ::ceil(3e8 / double(kernelTime));
    else // force 1000 if kernelTime is zero
        stepsPerWait = 1000;
    
    if (stepsPerWait < 2)
        stepsPerWait = 2;
    {
        cl_device_type devType;
        if (kernelTime >= 4000000000ULL)
            clDevice.getInfo(CL_DEVICE_TYPE, &devType);
        
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *outStream << "KernelTime: " << (double(kernelTime)*1e-9) <<
                "s, itersPerWait: " << stepsPerWait << "\n" << std::endl;
        if (kernelTime >= 4000000000ULL)
        {
            if ((devType & CL_DEVICE_TYPE_CPU) == 0)
                *outStream <<
                    "WARNING! KERNEL TIME FOR NON-CPU DEVICE IS VERY LONG!\n"
                    "YOU MAY HAVE PROBLEMS WITH EXITING FROM APPLICATION!\n" << std::endl;
            else
                *outStream <<
                    "Warning: Kernel time for CPU is long! You may have problems with\n"
                    "stopping test. You can exit from application immediately when test\n"
                    "can't be stopped\n" << std::endl;
        }
        handleOutput(id);
    }
}

void GPUStressTester::printBuildLog()
{
    std::string buildLog;
    clProgram.getBuildInfo(clDevice, CL_PROGRAM_BUILD_LOG, &buildLog);
    std::lock_guard<std::mutex> l(stdOutputMutex);
    *outStream << "Program build log:\n  " <<
            platformName << ":" << deviceName << "\n:--------------------\n" <<
            buildLog << "\n:--------------------" << std::endl;
    handleOutput(id);
}

void GPUStressTester::printStatus(cxuint passNum)
{
    if ((passNum%10) != 0)
        return;
    const rt_time_point rtCurrentTime = RealtimeClock::now();
    const std_time_point stdCurrentTime = SteadyClock::now();
    const int64_t nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
                stdCurrentTime-lastTime).count();
    lastTime = stdCurrentTime;
    
    double bandwidth, perf;
    bandwidth = 2.0*10.0*4.0*double(passItersNum)*double(bufItemsNum) / double(nanos);
    if (!usePolyWalker)
        perf = 2.0*10.0*3.0*double(kitersNum)*double(passItersNum)*double(bufItemsNum)
                / double(nanos);
    else
        perf = 10.0*8.0*double(kitersNum)*double(passItersNum)*double(bufItemsNum)
                / double(nanos);
    
    const int64_t startMillis = std::max(int64_t(0),
        std::chrono::duration_cast<std::chrono::milliseconds>(
                rtCurrentTime-startTime).count());
    char timeStrBuf[128];
    snprintf(timeStrBuf, 128, "%u:%02u:%02u.%03u", cxuint(startMillis/3600000),
             cxuint((startMillis/60000)%60), cxuint((startMillis/1000)%60),
             cxuint(startMillis%1000));
    
    std::lock_guard<std::mutex> l(stdOutputMutex);
    *outStream << "#" << id << " " << platformName << ":" << deviceName <<
            " passed PASS #" << passNum << "\n"
            "Approx. bandwidth: " << bandwidth << " GB/s, "
            "Approx. perf: " << perf << " GFLOPS, elapsed: " << timeStrBuf << std::endl;
    handleOutput(id);
}

void GPUStressTester::throwFailedComputations(cxuint passNum)
{
    const rt_time_point currentTime = RealtimeClock::now();
    const int64_t startMillis = std::max(int64_t(0),
            std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime-startTime).count());
    char strBuf[128];
    snprintf(strBuf, 128,
             "FAILED COMPUTATIONS!!!! PASS #%u, Elapsed time: %u:%02u:%02u.%03u",
             passNum, cxuint(startMillis/3600000), cxuint((startMillis/60000)%60),
             cxuint((startMillis/1000)%60), cxuint(startMillis%1000));
    if (!exitIfAllFails)
        stopAllStressTestersIfFail.store(true);
    throw MyException(strBuf);
}

void GPUStressTester::runTest()
try
{
    bool run1Exec = false;
    bool run2Exec = false;
    bool result1Checked = false;
    bool result2Checked = false;
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
    
    cxuint pass1Num = 1;
    cxuint pass2Num = 2;
    try
    {
    startTime = RealtimeClock::now();
    lastTime = SteadyClock::now();
    
    while (true)
    {
        if (stopAllStressTestersIfFail.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because some device failed." << std::endl;
            handleOutput(id);
            break;
        }
        if (stopAllStressTestersByUser.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because user stopped test." << std::endl;
            handleOutput(id);
            break;
        }
        
        clCmdQueue2.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                initialValues);
        /* run execution 1 */
        if (!useInputAndOutput)
        {
            clKernel.setArg(1, clBuffer1);
            clKernel.setArg(2, clBuffer1);
        }
        
        cxuint stepsAfterWait = 0;
        bool allIsExecuted = true;
        for (cxuint i = 0; i < passItersNum; i++)
        {
            if (stopAllStressTestersIfFail.load() || stopAllStressTestersByUser.load())
            {
                allIsExecuted = false;
                break;
            }
            if (useInputAndOutput)
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
            stepsAfterWait++;
            if (stepsAfterWait >= stepsPerWait && i+((stepsPerWait+1)>>1) < passItersNum)
            {   /* wait for ndrange kernel and ensure fluent working */
                stepsAfterWait = 0;
                try
                { exec1Events[i-1].wait(); }
                catch(const cl::Error& err)
                {
                    if (err.err() != CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
                        throw; // if other error
                    int eventStatus;
                    exec1Events[i-1].getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
                    if (eventStatus < 0)
                    {
                        char strBuf[64];
                        snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
                        throw MyException(strBuf);
                    }
                }
            }
        }
        if (allIsExecuted)
        {
            run1Exec = true;
            result1Checked = false; // not yet checked
        }
        if (stopAllStressTestersIfFail.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because some device failed." << std::endl;
            handleOutput(id);
            break;
        }
        if (stopAllStressTestersByUser.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because user stopped test." << std::endl;
            handleOutput(id);
            break;
        }
        
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
            if (!useInputAndOutput || (passItersNum&1) == 0)
                clCmdQueue2.enqueueReadBuffer(clBuffer3, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            else //
                clCmdQueue2.enqueueReadBuffer(clBuffer4, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            if (::memcmp(toCompare, results, bufItemsNum<<2))
                throwFailedComputations(pass2Num);
            printStatus(pass2Num);
            pass2Num += 2;
            result2Checked = true; // now is checked
        }
        
        if (stopAllStressTestersIfFail.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because some device failed." << std::endl;
            handleOutput(id);
            break;
        }
        if (stopAllStressTestersByUser.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because user stopped test." << std::endl;
            handleOutput(id);
            break;
        }
        
        clCmdQueue2.enqueueWriteBuffer(clBuffer3, CL_TRUE, size_t(0), bufItemsNum<<2,
                initialValues);
        /* run execution 2 */
        if (!useInputAndOutput)
        {
            clKernel.setArg(1, clBuffer3);
            clKernel.setArg(2, clBuffer3);
        }
        
        stepsAfterWait = 0;
        allIsExecuted = true;
        for (cxuint i = 0; i < passItersNum; i++)
        {
            if (stopAllStressTestersIfFail.load() || stopAllStressTestersByUser.load())
            {
                allIsExecuted = false;
                break;
            }
            if (useInputAndOutput)
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
            stepsAfterWait++;
            if (stepsAfterWait >= stepsPerWait && i+((stepsPerWait+1)>>1) < passItersNum)
            {   /* wait for ndrange kernel and ensure fluent working */
                stepsAfterWait = 0;
                try
                { exec2Events[i-1].wait(); }
                catch(const cl::Error& err)
                {
                    if (err.err() != CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
                        throw; // if other error
                    int eventStatus;
                    exec2Events[i-1].getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
                    if (eventStatus < 0)
                    {
                        char strBuf[64];
                        snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
                        throw MyException(strBuf);
                    }
                }
            }
        }
        if (allIsExecuted)
        {
            run2Exec = true;
            result2Checked = false; // not yet checked
        }
        if (stopAllStressTestersIfFail.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because some device failed." << std::endl;
            handleOutput(id);
            break;
        }
        if (stopAllStressTestersByUser.load())
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *outStream << "#" << id << " Exiting, because user stopped test." << std::endl;
            handleOutput(id);
            break;
        }
        
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
            if (!useInputAndOutput || (passItersNum&1) == 0)
                clCmdQueue2.enqueueReadBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            else //
                clCmdQueue2.enqueueReadBuffer(clBuffer2, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            if (::memcmp(toCompare, results, bufItemsNum<<2))
                throwFailedComputations(pass1Num);
            printStatus(pass1Num);
            pass1Num += 2;
            result1Checked = true; // now is checked
        }
    }
    }
    catch(...)
    {   /* wait for finish kernels */
        try
        { clCmdQueue1.finish(); }
        catch(...)
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *errStream << "Failed on CommandQueue1 finish" << std::endl;
            handleOutput(id);
        }
        try
        { clCmdQueue2.finish(); }
        catch(...)
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *errStream << "Failed on CommandQueue2 finish" << std::endl;
            handleOutput(id);
        }
        throw;
    }
    
    bool queuesFinished = true;
    /* finish all queues */
    try
    { clCmdQueue1.finish(); }
    catch(...)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Failed on CommandQueue1 finish" << std::endl;
        handleOutput(id);
        queuesFinished = false;
    }
    try
    { clCmdQueue2.finish(); }
    catch(...)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Failed on CommandQueue2 finish" << std::endl;
        handleOutput(id);
        queuesFinished = false;
    }
    
    if (!queuesFinished)
        return; // if queues failed do not check (only returns)
    
    /* after break check kernel events and results */
    {
        cxuint i;
        for (i = 0; i < passItersNum; i++)
        {   // check kernel event status
            int eventStatus;
            if (exec1Events[i]() == nullptr)
                break; // no other events
            exec1Events[i].getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
            if (eventStatus < 0)
            {
                char strBuf[64];
                snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
                throw MyException(strBuf);
            }
        }
        if (i == passItersNum && !result1Checked)
        {   // get results
            if (!useInputAndOutput || (passItersNum&1) == 0)
                clCmdQueue2.enqueueReadBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            else //
                clCmdQueue2.enqueueReadBuffer(clBuffer2, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            if (::memcmp(toCompare, results, bufItemsNum<<2))
                throwFailedComputations(pass1Num);
            printStatus(pass1Num);
        }
        
        for (i = 0; i < passItersNum; i++)
        {   // check kernel event status
            int eventStatus;
            if (exec2Events[i]() == nullptr)
                break; // no other events
            exec2Events[i].getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
            if (eventStatus < 0)
            {
                char strBuf[64];
                snprintf(strBuf, 64, "Failed NDRangeKernel with code: %d", eventStatus);
                throw MyException(strBuf);
            }
        }
        if (i == passItersNum && !result2Checked)
        {   // get results
            if (!useInputAndOutput || (passItersNum&1) == 0)
                clCmdQueue2.enqueueReadBuffer(clBuffer3, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            else //
                clCmdQueue2.enqueueReadBuffer(clBuffer4, CL_TRUE, size_t(0), bufItemsNum<<2,
                            results);
            if (::memcmp(toCompare, results, bufItemsNum<<2))
                throwFailedComputations(pass2Num);
            printStatus(pass2Num);
        }
    }
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
        *errStream << "Failed StressTester for\n  " <<
                "#" << id  << " " << platformName << ":" << deviceName << ": " <<
                failMessage << std::endl;
        handleOutput(id);
    }
    catch(...)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Can't print fatal error!!!" << std::endl;
        handleOutput(id);
    } // fatal exception!!!
}
catch(const std::exception& ex)
{
    failed = true;
    try
    {
        failMessage = "Exception happened: ";
        failMessage += ex.what();
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Failed StressTester for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName << ":\n    " <<
                failMessage << std::endl;
        handleOutput(id);
    }
    catch(...)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Can't print fatal error!!!" << std::endl;
        handleOutput(id);
    } // fatal exception!!!
}
catch(...)
{
    failed = true;
    try
    {
        failMessage = "Unknown exception happened";
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Failed StressTester for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName << ":\n    " <<
                failMessage << std::endl;
        handleOutput(id);
    }
    catch(...)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Can't print fatal error!!!" << std::endl;
        handleOutput(id);
    } // fatal exception!!!
}

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

#include <iostream>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>
#include <sstream>
#include <vector>
#include <utility>
#include <set>
#include <thread>
#include <mutex>
#include <random>
#include <chrono>
#include <atomic>
#include <sys/stat.h>
#include <popt.h>
#include <CL/cl.hpp>

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
static int workFactor = 256;
static int blocksNum = 2;
static int passItersNum = 32;
static int choosenKitersNum = 0;
static int useInputAndOutput = 0;
static int useAMDPlatform = 0;
static int useNVIDIAPlatform = 0;
static int useIntelPlatform = 0;
static bool useAllPlatforms = false;
static int listDevices = 0;
static const char* devicesListString = nullptr;
static int dontWait = 0;
static int exitIfAllFails = 0;

static const char* programName = nullptr;
static bool usePolyWalker = false;
static int builtinKernel = 0; // default

static std::mutex stdOutputMutex;

static size_t clKernelSourceSize = 0;
static const char* clKernelSource = nullptr;

static std::atomic<bool> stopAllStressTesters(false);

static const poptOption optionsTable[] =
{
    { "listDevices", 'l', POPT_ARG_VAL, &listDevices, 'l', "list OpenCL devices", nullptr },
    { "devicesList", 'L', POPT_ARG_STRING, &devicesListString, 'L',
        "specify list of devices in form: 'platformId:deviceId,....'", "DEVICELIST" },
    { "useCPUs", 'C', POPT_ARG_VAL, &useCPUs, 'C', "use all CPU devices", nullptr },
    { "useGPUs", 'G', POPT_ARG_VAL, &useGPUs, 'G', "use all GPU devices", nullptr },
    { "useAccs", 'a', POPT_ARG_VAL, &useAccelerators, 'a',
        "use all accelerator devices", nullptr },
    { "useAMD", 'A', POPT_ARG_VAL, &useAMDPlatform, 'A', "use AMD platform", nullptr },
    { "useNVIDIA", 'N', POPT_ARG_VAL, &useNVIDIAPlatform, 'N',
        "use NVIDIA platform", nullptr },
    { "useIntel", 'E', POPT_ARG_VAL, &useIntelPlatform, 'L', "use Intel platform", nullptr },
    { "program", 'P', POPT_ARG_STRING, &programName, 'P',
        "choose OpenCL program name", "NAME" },
    { "builtin", 'T', POPT_ARG_INT, &builtinKernel, 'T',
        "choose OpenCL builtin kernel", "[0-2]" },
    { "inAndOut", 'I', POPT_ARG_VAL, &useInputAndOutput, 'I',
      "use input and output buffers (doubles memory reqs.)" },
    { "workFactor", 'W', POPT_ARG_INT, &workFactor, 'W',
        "set workSize=factor*compUnits*grpSize", "FACTOR" },
    { "blocksNum", 'B', POPT_ARG_INT, &blocksNum, 'B', "blocks number", "BLOCKS" },
    { "passIters", 'S', POPT_ARG_INT, &passItersNum, 'S', "pass iterations num",
        "ITERATION" },
    { "kiters", 'j', POPT_ARG_INT, &choosenKitersNum, 'j', "kitersNum", "ITERATION" },
    { "dontWait", 'w', POPT_ARG_VAL, &dontWait, 'w', "dont wait few seconds", nullptr },
    { "exitIfAllFails", 'f', POPT_ARG_VAL, &exitIfAllFails, 'f',
        "exit only if all devices fails at computation", nullptr },
    POPT_AUTOHELP
    { nullptr, 0, 0, nullptr, 0 }
};

static char* loadFromFile(const char* filename, size_t& size)
{
    struct stat stbuf;
    if (stat(filename, &stbuf)<0)
        throw MyException("No permissions or file doesnt exists");
    if (!S_ISREG(stbuf.st_mode))
        throw MyException("This is not regular file");
    
    std::ifstream ifs(filename, std::ios::binary);
    if (!ifs)
        throw MyException("Cant open file");
    ifs.exceptions(std::ifstream::badbit | std::ifstream::failbit);
    ifs.seekg(0, std::ios::end); // to end of file
    size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    char* buf = nullptr;
    try
    {
        buf = new char[size];
        ifs.read(buf, size);
        if (ifs.gcount() != std::streamsize(size))
            throw MyException("Cant read whole file");
    }
    catch(...)
    {
        delete[] buf;
        throw;
    }
    return buf;
}

std::vector<std::pair<cl::Platform, cl::Device> > getChoosenCLDevices()
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

std::vector<std::pair<cl::Platform, cl::Device> > getChoosenCLDevicesFromList(const char* str)
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

static const float  examplePoly[5] = 
{ 4.43859953e+05,   1.13454169e+00,  -4.50175916e-06, -1.43865531e-12,   4.42133541e-18 };

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
    
    cxuint blocksNum;
    cxuint passItersNum;
    cxuint kitersNum;
    
    size_t bufItemsNum;
    
    float* initialValues;
    float* toCompare;
    float* results;
    
    cl::Program clProgram;
    cl::Kernel clKernel;
    
    size_t groupSize;
    size_t workSize;
    
    bool failed;
    std::string failMessage;
    
    void printBuildLog();
    void printStatus(cxuint passNum);
    void throwFailedComputations(cxuint passNum);
    
    void buildKernel(cxuint kitersNum, cxuint blocksNum, bool alwaysPrintBuildLog);
    void calibrateKernel();
public:
    GPUStressTester(cxuint id, cl::Platform& clPlatform, cl::Device& clDevice,
                    size_t workFactor, cxuint blocksNum, cxuint passItersNum);
    ~GPUStressTester();
    
    void runTest();
    
    bool isFailed() const
    { return failed; }
    const std::string& getFailMessage() const
    { return failMessage; }
};

GPUStressTester::GPUStressTester(cxuint _id, cl::Platform& clPlatform, cl::Device& _clDevice,
        size_t workFactor, cxuint _blocksNum, cxuint _passItersNum) :
        id(_id), clDevice(_clDevice), blocksNum(_blocksNum), passItersNum(_passItersNum),
        initialValues(nullptr), toCompare(nullptr)
{
    failed = false;
    
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
                ", passIters=" << passItersNum << std::endl;
    }
    
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
                cl::NDRange(workSize), cl::NDRange(groupSize), NULL, &clEvent);
        clEvent.wait();
        int eventStatus;
        clEvent.getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
        if (eventStatus < 0)
        {
            std::ostringstream oss;
            oss << "Failed NDRangeKernel with code: " << eventStatus << std::endl;
            throw MyException(oss.str());
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

void GPUStressTester::buildKernel(cxuint kitersNum, cxuint blocksNum, bool alwaysPrintBuildLog)
{
    cl::Program::Sources clSources;
    clSources.push_back(std::make_pair(clKernelSource, clKernelSourceSize));
    clProgram = cl::Program(clContext, clSources);
    
    try
    {
        char buildOptions[128];
        snprintf(buildOptions, 128, "-O5 -DGROUPSIZE=%zu -DKITERSNUM=%u -DBLOCKSNUM=%u",
                groupSize, kitersNum, blocksNum);
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
        buildKernel(kitersNum, blocksNum, alwaysPrintBuildLog);
    }
}

void GPUStressTester::calibrateKernel()
{
    cxuint bestKitersNum = 1;
    double bestBandwidth = 0.0;
    double bestPerf = 0.0;
    
    clCmdQueue1.enqueueWriteBuffer(clBuffer1, CL_TRUE, size_t(0), bufItemsNum<<2,
            initialValues);
    
    if (choosenKitersNum == 0)
    {
        {
            std::lock_guard<std::mutex> l(stdOutputMutex);
            std::cout << "Calibrating Kernel for\n  " <<
                "#" << id << " " << platformName << ":" << deviceName << "..." << std::endl;
        }
        
        cl::CommandQueue profCmdQueue(clContext, clDevice, CL_QUEUE_PROFILING_ENABLE);
        
        for (cxuint kitersNum = 1; kitersNum <= 30; kitersNum++)
        {
            buildKernel(kitersNum, blocksNum, false);
            
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
                        cl::NDRange(workSize), cl::NDRange(groupSize), NULL, &profEvent);
                profEvent.wait();
                int eventStatus;
                profEvent.getInfo(CL_EVENT_COMMAND_EXECUTION_STATUS, &eventStatus);
                if (eventStatus < 0)
                {
                    std::ostringstream oss;
                    oss << "Failed NDRangeKernel with code: " << eventStatus << std::endl;
                    throw MyException(oss.str());
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
                currentPerf = 2.0*3.0*double(kitersNum)*double(bufItemsNum) /
                        double(currentTime);
            else
                currentPerf = 8.0*double(kitersNum)*double(bufItemsNum) /
                        double(currentTime);
            
            if (currentBandwidth*currentPerf > bestBandwidth*bestPerf)
            {
                bestKitersNum = kitersNum;
                bestPerf = currentPerf;
                bestBandwidth = currentBandwidth;
            }
            /*{
                std::lock_guard<std::mutex> l(stdOutputMutex);
                std::cout << "Choose for kernel \n  " <<
                    "#" << id << " " << platformName << ":" << deviceName << "\n"
                    "  BestKitersNum: " << kitersNum << ", Bandwidth: " << currentBandwidth <<
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
        bestKitersNum = choosenKitersNum;
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
    
    const int32_t startMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime-startTime).count();
    
    char timeStrBuf[128];
    snprintf(timeStrBuf, 128, "%02u:%02u:%02u.%03u", (startMillis/3600000),
             (startMillis/60000)%60, (startMillis/1000)%60, (startMillis%1000));
    
    std::lock_guard<std::mutex> l(stdOutputMutex);
    std::cout << "#" << id << " " << platformName << ":" << deviceName <<
            " was passed PASS #" << passNum << "\n"
            "Approx. bandwidth: " << bandwidth << " GB/s, "
            "Approx. perf: " << perf << " GFLOPS, elapsed: " << timeStrBuf << std::endl;
}

void GPUStressTester::throwFailedComputations(cxuint passNum)
{
    const time_point currentTime = std::chrono::high_resolution_clock::now();
    const int32_t startMillis = std::chrono::duration_cast<std::chrono::milliseconds>(
                currentTime-startTime).count();
    char strBuf[128];
    snprintf(strBuf, 128,
             "FAILED COMPUTATIONS!!!! PASS #%u, Elapsed time: %02u:%02u:%02u.%03u",
             passNum, (startMillis/3600000), (startMillis/60000)%60, (startMillis/1000)%60,
             (startMillis%1000));
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
                    cl::NDRange(workSize), cl::NDRange(groupSize), NULL, &exec1Events[i]);
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
                    snprintf(strBuf, 64, "Failed NDRangeKernel with code: %u", eventStatus);
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
                    cl::NDRange(workSize), cl::NDRange(groupSize), NULL, &exec2Events[i]);
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
                    snprintf(strBuf, 64, "Failed NDRangeKernel with code: %u", eventStatus);
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
        if (exec1Events[passItersNum-1]() != nullptr)
        {
            try
            { exec1Events[passItersNum-1].wait(); }
            catch(...)
            { }
        }
        if (exec2Events[passItersNum-1]() != nullptr)
        {
            try
            { exec2Events[passItersNum-1].wait(); }
            catch(...)
            { }
        }
        throw;
    }
    if (exec1Events[passItersNum-1]() != nullptr)
    {
        try
        { exec1Events[passItersNum-1].wait(); }
        catch(...)
        { }
    }
    if (exec2Events[passItersNum-1]() != nullptr)
    {
        try
        { exec2Events[passItersNum-1].wait(); }
        catch(...)
        { }
    }
}
catch(const cl::Error& error)
{
    failed = true;
    try
    {
        std::ostringstream oss;
        oss << "OpenCL error happened: " << error.what() << ", Code: " << error.err();
        oss.flush();
        failMessage = oss.str();
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
        std::ostringstream oss;
        oss << "Exception happened: " << ex.what();
        oss.flush();
        failMessage = oss.str();
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
    
    /* parse options */
    while((cmd = poptGetNextOpt(optsContext)) >= 0);
    
    if (cmd < -1)
    {
        std::cerr << poptBadOption(optsContext, POPT_BADOPTION_NOALIAS) << ": " <<
            poptStrerror(cmd) << std::endl;
        return 1;
    }
    
    if (workFactor <= 0)
    {
        std::cerr << "WorkFactor is not positive!" << std::endl;
        return 1;
    }
    if (passItersNum <= 0)
    {
        std::cerr << "PassIters is not positive!" << std::endl;
        return 1;
    }
    if (blocksNum <= 0)
    {
        std::cerr << "BlocksNum is not positive!" << std::endl;
        return 1;
    }
    if (builtinKernel < 0 || builtinKernel > 2)
    {
        std::cerr << "Builtin kernel number out of range!" << std::endl;
        return 1;
    }
    if (choosenKitersNum < 0 || choosenKitersNum > 30)
    {
        std::cerr << "KitersNum out of range" << std::endl;
        return 1;
    }
    
    std::cout << "CLGPUStress 0.0.2 by Mateusz Szpakowski. "
        "Program is delivered under GPLv2 License" << std::endl;
    
    if (listDevices)
    {
        listCLDevices();
        return 0;
    }
        
    if (!useGPUs && !useCPUs)
        useGPUs = 1;
    if (!useAMDPlatform && !useNVIDIAPlatform && !useIntelPlatform)
        useAllPlatforms = true;
    
    if (programName != nullptr)
    {
        if (::strcmp(programName, "gpustressPW.cl") == 0)
            usePolyWalker = true;
    }
    
    int retVal = 0;
    
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
        
        std::cout <<
            "\nWARNING: THIS PROGRAM CAN OVERHEAT OR DAMAGE YOUR GRAPHICS CARD FASTER (AND BETTER)\n"
            "THAN ANY FURMARK STRESS. PLEASE USE THIS PROGRAM VERY CAREFULLY!!!\n"
            "RECOMMEND TO RUN THIS PROGRAM ON STOCK PARAMETERS "
            "(CLOCKS, VOLTAGES,\nESPECIALLY MEMORY CLOCK).\n"
            "TO TERMINATE THIS PROGRAM PLEASE USE STANDARD CTRL-C.\n" << std::endl;
        if (exitIfAllFails)
            std::cout << "Program exits only when all devices fails.\n"
                "Please trace output to find failed device and react!\n" << std::endl;
        if (dontWait==0)
            std::this_thread::sleep_for(std::chrono::milliseconds(8000));
        
        if (programName != nullptr)
        {
            std::cout << "Load kernel code from file " << programName << std::endl;
            clKernelSource = loadFromFile(programName, clKernelSourceSize);
        }
        else
        {
            std::cout << "Choosing builtin kernel: " << builtinKernel << std::endl;
            switch(builtinKernel)
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
        }
        
        for (size_t i = 0; i < choosenCLDevices.size(); i++)
        {
            auto& p = choosenCLDevices[i];
            gpuStressTesters.push_back(new GPUStressTester(i, p.first, p.second, workFactor,
                            blocksNum, passItersNum));
        }
        
        if (programName != nullptr)
            delete[] clKernelSource;
        clKernelSource = nullptr;
        
        for (size_t i = 0; i < choosenCLDevices.size(); i++)
            testerThreads.push_back(new std::thread(
                    &GPUStressTester::runTest, gpuStressTesters[i]));
    }
    catch(const cl::Error& error)
    {
        if (programName != nullptr)
            delete[] clKernelSource;
        std::lock_guard<std::mutex> l(stdOutputMutex);
        std::cerr << "OpenCL error happened: " << error.what() <<
                ", Code: " << error.err() << std::endl;
        retVal = 1;
    }
    catch(const std::exception& ex)
    {
        if (programName != nullptr)
            delete[] clKernelSource;
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
            if (!gpuStressTesters[i]->isFailed())
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

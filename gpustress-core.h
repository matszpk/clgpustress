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

#ifndef __GPUSTRESS_CORE_H__
#define __GPUSTRESS_CORE_H__

#include <ostream>
#include <algorithm>
#include <exception>
#include <numeric>
#include <string>
#include <vector>
#include <mutex>
#include <random>
#include <chrono>
#include <atomic>
#include <CL/cl.hpp>

#if defined(_WINDOWS) && defined(_MSC_VER)
struct SteadyClock
{
    typedef int64_t rep;
    typedef std::nano period;
    typedef std::chrono::duration<rep,period> duration;
    typedef std::chrono::time_point<SteadyClock> time_point;
    static const bool is_steady = true;
    static time_point now();
};

extern bool isQPCClockChoosen();
extern bool verifyQPCClock();

typedef std::chrono::system_clock RealtimeClock;
#else
typedef std::chrono::steady_clock SteadyClock;
typedef std::chrono::system_clock RealtimeClock;
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
    MyException();
    MyException(const std::string& msg);
    virtual ~MyException() throw();
    
    const char* what() const throw();
};

struct GPUStressConfig
{
    cxuint passItersNum;
    cxuint groupSize;
    cxuint workFactor;
    cxuint blocksNum;
    cxuint kitersNum;
    cxuint builtinKernel;
    bool inputAndOutput;
};

typedef void (*OutputHandler)(void* data, cxuint id);

extern int useCPUs;
extern int useGPUs;
extern int useAccelerators;
extern int useAMDPlatform;
extern int useNVIDIAPlatform;
extern int useIntelPlatform;
extern bool useAllPlatforms;

extern int exitIfAllFails;

extern std::mutex stdOutputMutex;
extern std::ostream* outStream;
extern std::ostream* errStream;
extern std::atomic<bool> stopAllStressTestersIfFail;
extern std::atomic<bool> stopAllStressTestersByUser;

extern OutputHandler outputHandler;
extern void* outputHandlerData;

extern std::string trimSpaces(const std::string& s);

extern std::vector<cxuint> parseCmdUIntList(const char* str, const char* name);

extern std::vector<bool> parseCmdBoolList(const char* str, const char* name);

extern std::vector<cl::Device> getChoosenCLDevices();

extern std::vector<cl::Device> getChoosenCLDevicesFromList(const char* str);

extern std::vector<GPUStressConfig> collectGPUStressConfigs(cxuint devicesNum,
        const std::vector<cxuint>& passItersNumVec, const std::vector<cxuint>& groupSizeVec,
        const std::vector<cxuint>& workFactorVec,
        const std::vector<cxuint>& blocksNumVec, const std::vector<cxuint>& kitersNumVec,
        const std::vector<cxuint>& builtinKernelVec, const std::vector<bool>& inAndOutVec);

extern void installOutputHandler(std::ostream* out, std::ostream* err,
                OutputHandler handler = nullptr, void* data = nullptr);

inline void handleOutput(cxuint id)
{
    if (outputHandler == nullptr)
        return;
    outputHandler(outputHandlerData, id);
}

class GPUStressTester
{
private:
    cxuint id;
    cl::Device clDevice;
    cl::Context clContext;
    
    std::string platformName;
    std::string deviceName;
    
    typedef std::chrono::time_point<RealtimeClock> rt_time_point;
    typedef std::chrono::time_point<SteadyClock> std_time_point;
    
    cxuint stepsPerWait;
    
    rt_time_point startTime;
    std_time_point lastTime;
    
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
    
    bool initialized;
    
    void printBuildLog();
    void printStatus(cxuint passNum);
    void throwFailedComputations(cxuint passNum);
    
    void buildKernel(cxuint kitersNum, cxuint blocksNum, bool alwaysPrintBuildLog,
         bool whenCalibrates);
    void calibrateKernel();
public:
    GPUStressTester(cxuint id, cl::Device& clDevice, const GPUStressConfig& config);
    ~GPUStressTester();
    
    void runTest();
    
    bool isInitialized() const
    { return initialized; }
    
    bool isFailed() const
    { return failed; }
    const std::string& getFailMessage() const
    { return failMessage; }
};

#endif

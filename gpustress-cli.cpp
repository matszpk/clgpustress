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
#include <vector>
#include <thread>
#include <chrono>
#ifndef _WINDOWS
#include <unistd.h>
#else
#include <windows.h>
#include <io.h>
#endif
#include <csignal>
#include <mutex>
#include <popt.h>
#include <CL/cl.hpp>
#include "gpustress-core.h"

#define PROGRAM_VERSION "0.0.8.6"

extern const char* testDescsTable[];

static int listAllDevices = 0;
static int listChoosenDevices = 0;
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
    { "listDevices", 'l', POPT_ARG_VAL, &listAllDevices, 'l',
        "List all OpenCL devices", nullptr },
    { "devicesList", 'L', POPT_ARG_STRING, &devicesListString, 'L',
        "Specify list of devices in form: 'platformId:deviceId,....'", "DEVICELIST" },
    { "choosenDevices", 'c', POPT_ARG_VAL, &listChoosenDevices, 'c',
        "List choosen OpenCL devices", nullptr },
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
        "Exit only when all devices will fail at computation", nullptr },
    { "version", 'V', POPT_ARG_VAL, &printVersion, 'V', "Print program version", nullptr },
    { "help", '?', POPT_ARG_VAL, &printHelp, '?', "Show this help message", nullptr },
    { "usage", 0, POPT_ARG_VAL, &printUsage, 'u', "Display brief usage message", nullptr },
    { nullptr, 0, 0, nullptr, 0 }
};

static void printCLDeviceInfo(const cl::Device& clDevice)
{
    cl_uint deviceClock;
    cl_ulong memSize;
    cl_uint maxComputeUnits;
    size_t maxWorkGroupSize;
    cl_device_type deviceType;
    clDevice.getInfo(CL_DEVICE_TYPE, &deviceType);
    clDevice.getInfo(CL_DEVICE_MAX_CLOCK_FREQUENCY, &deviceClock);
    clDevice.getInfo(CL_DEVICE_GLOBAL_MEM_SIZE, &memSize);
    clDevice.getInfo(CL_DEVICE_MAX_COMPUTE_UNITS, &maxComputeUnits);
    clDevice.getInfo(CL_DEVICE_MAX_WORK_GROUP_SIZE, &maxWorkGroupSize);
    
    std::cout << "  Type:" << ((deviceType & CL_DEVICE_TYPE_CPU)!=0?" CPU":"") <<
            ((deviceType & CL_DEVICE_TYPE_GPU)!=0?" GPU":"") <<
            ((deviceType & CL_DEVICE_TYPE_ACCELERATOR)!=0?" ACC":"") <<
            ", Clock: " << deviceClock << " MHz, Memory: " <<
            (memSize>>20) << " MB, CompUnits: " <<
            maxComputeUnits << ", MaxGroupSize: " << maxWorkGroupSize << "\n";
}

static void listCLDevices()
{
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
            
            printCLDeviceInfo(clDevice);
        }
    }
    std::cout.flush();
}

static void listChoosenCLDevices(const std::vector<cl::Device>& list)
{
    for (size_t i = 0; i < list.size(); i++)
    {
        const cl::Device& clDevice = list[i];
        cl::Platform clPlatform;
        clDevice.getInfo(CL_DEVICE_PLATFORM, &clPlatform);
        std::string platformName;
        clPlatform.getInfo(CL_PLATFORM_NAME, &platformName);
        std::string deviceName;
        clDevice.getInfo(CL_DEVICE_NAME, &deviceName);
        
        std::cout << i << "  " << platformName << ":" << deviceName << "\n";
        
        printCLDeviceInfo(clDevice);
    }
    std::cout.flush();
}

#ifdef _POSIX_SOURCE
static struct sigaction oldINTHandler;
static struct sigaction oldTERMHandler;
#else
#  ifdef _WINDOWS
typedef void (*sighandler_t)(int);
static sighandler_t oldINTHandler;
#  endif
static sighandler_t oldTERMHandler;
#endif

#ifdef _POSIX_SOURCE
static void installTerminate(int signo, struct sigaction* old, sighandler_t f)
{
    struct sigaction sanew;
    sanew.sa_handler = f;
    sanew.sa_flags = 0;
    sigemptyset(&sanew.sa_mask);
    sigaction(signo, &sanew, old);
#else
static void installTerminate(int signo, sighandler_t* old, sighandler_t f)
{
    if (old != nullptr)
        *old = signal(signo, f);
    else // if not specified
        signal(signo, f);
#endif
}

#ifdef _POSIX_SOURCE
static void uninstallTerminate(int signo, const struct sigaction& old)
{
    sigaction(signo, &old, nullptr);
#else
static void uninstallTerminate(int signo, sighandler_t old)
{
    signal(signo, old);
#endif
}

static void abnormalTerminate(int signo)
{
    ::write(2, "Abnormal exiting...\n", 20);
#ifdef _WINDOWS
    raise(SIGABRT);
#else
    raise(SIGKILL);
#endif
}

static const cxuint secondsToTerminate = 2;

static void abnormalTerminateThreadFunc(int signo)
{
    try
    { std::this_thread::sleep_for(std::chrono::seconds(secondsToTerminate)); }
    catch(...)
    { }
    abnormalTerminate(SIGTERM);
}

#ifdef _WINDOWS
static BOOL queueTimerCreated = FALSE;
static HANDLE queueTimer;

static VOID CALLBACK abnormalTimerCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired)
{
    if (queueTimerCreated)
        DeleteTimerQueueTimer(nullptr, queueTimer, nullptr);
    abnormalTerminate(SIGTERM);
}
#endif

static void normalTerminate(int signo)
{
#ifndef _WINDOWS
    uninstallTerminate(SIGINT, oldINTHandler);
#endif
    uninstallTerminate(SIGTERM, oldTERMHandler);
    ::write(2, "Normal exiting...\n", 18);
    std::thread* th1 = nullptr;
    try
    { th1 = new std::thread(abnormalTerminateThreadFunc, SIGTERM); }
    catch(...)
    { }
    if (th1 != nullptr)
    {
        try
        { th1->detach(); }
        catch(...)
        { }
    }
    else
    {   /* otherwise we try alarm method */
#ifdef _WINDOWS
        queueTimerCreated = CreateTimerQueueTimer(&queueTimer, nullptr,
                abnormalTimerCallback, nullptr, secondsToTerminate*1000U,
                0, WT_EXECUTEDEFAULT);
#else
        installTerminate(SIGALRM, nullptr, abnormalTerminate);
        alarm(secondsToTerminate);
#endif
    }
    stopAllStressTestersByUser.store(true);
}

#ifdef _WINDOWS
static BOOL WINAPI normalTerminateWin(DWORD ctrltype)
{
    if (ctrltype == CTRL_C_EVENT || ctrltype == CTRL_BREAK_EVENT)
    {
        normalTerminate(SIGINT);
        SetConsoleCtrlHandler(normalTerminateWin, FALSE);
        return TRUE;
    }
    return FALSE;
}
#endif

int main(int argc, const char** argv)
{
    int cmd;
    poptContext optsContext;
    installOutputHandler(&std::cout, &std::cerr);
    optsContext = poptGetContext("gpustress-cli", argc, argv, optionsTable, 0);
    
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
    
    std::cout << "CLGPUStress CLI " PROGRAM_VERSION " by Mateusz Szpakowski. "
        "Program is distributed under terms of the GPLv2.\n"
        "Website: http://clgpustress.nativeboinc.org.\n"
        "Sources available at https://github.com/matszpk/clgpustress.\n"
        "Binaries available at http://files.nativeboinc.org/offtopic/clgpustress/."
        << std::endl;
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
    
    if (listAllDevices)
    {
        listCLDevices();
        poptFreeContext(optsContext);
        return 0;
    }
    
    if (!useGPUs && !useCPUs && !useAccelerators)
        useGPUs = 1;
    if (!useAMDPlatform && !useNVIDIAPlatform && !useIntelPlatform)
        useAllPlatforms = true;
    
    int retVal = 0;
    
    std::vector<GPUStressConfig> gpuStressConfigs;
    std::vector<GPUStressTester*> gpuStressTesters;
    std::vector<std::thread*> testerThreads;
    try
    {
        std::vector<cl::Device> choosenCLDevices;
        if (devicesListString == nullptr)
            choosenCLDevices = getChoosenCLDevices();
        else
            choosenCLDevices = getChoosenCLDevicesFromList(devicesListString);
        if (choosenCLDevices.empty())
            throw MyException("OpenCL devices not found!");
        
        if (listChoosenDevices != 0)
        {
            listChoosenCLDevices(choosenCLDevices);
            poptFreeContext(optsContext);
            return 0;
        }
        
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
            
            gpuStressConfigs = collectGPUStressConfigs(choosenCLDevices.size(),
                    passItersNums, groupSizes, workFactors, blocksNums, kitersNums,
                    builtinKernels, inputAndOutputs);
        }
        
        std::cout <<
            "\nWARNING: THIS PROGRAM CAN OVERHEAT OR DAMAGE "
            "YOUR GRAPHICS CARD FASTER (AND BETTER)\n"
            "THAN ANY FURMARK STRESS TEST. PLEASE USE THIS PROGRAM VERY CAREFULLY!!!\n"
            "WE RECOMMEND TO RUN THIS PROGRAM ON THE STOCK PARAMETERS OF THE DEVICES "
            "(CLOCKS,\nVOLTAGES, ESPECIALLY MEMORY CLOCK).\n"
            "TO TERMINATE THIS PROGRAM PLEASE USE STANDARD 'CTRL-C' KEY COMBINATION.\n"
            << std::endl;
        if (exitIfAllFails && choosenCLDevices.size() > 1)
            std::cout << "PROGRAM EXITS ONLY WHEN ALL DEVICES WILL FAIL.\n"
                "PLEASE TRACE OUTPUT TO FIND FAILED DEVICE AND REACT!\n" << std::endl;
        if (dontWait==0)
            std::this_thread::sleep_for(std::chrono::milliseconds(8000));
        
#ifdef _WINDOWS
        SetConsoleCtrlHandler(normalTerminateWin, TRUE);
#else
        installTerminate(SIGINT, &oldINTHandler, normalTerminate);
#endif
        installTerminate(SIGTERM, &oldTERMHandler, normalTerminate);
        
        bool ifExitingAtInit = false;
        for (size_t i = 0; i < choosenCLDevices.size(); i++)
        {
            cl::Device& clDevice = choosenCLDevices[i];
            GPUStressTester* stressTester = new GPUStressTester(i, clDevice,
                        gpuStressConfigs[i]);
            if (!stressTester->isInitialized())
            {
                ifExitingAtInit = true;
                delete stressTester;
                break;
            }
            gpuStressTesters.push_back(stressTester);
        }
        if (!ifExitingAtInit)
            for (size_t i = 0; i < choosenCLDevices.size(); i++)
                testerThreads.push_back(new std::thread(
                        &GPUStressTester::runTest, gpuStressTesters[i]));
    }
    catch(const cl::Error& error)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "OpenCL error happened: " << error.what() <<
                ", Code: " << error.err() << std::endl;
        retVal = 1;
    }
    catch(const std::exception& ex)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Exception happened: " << ex.what() << std::endl;
        retVal = 1;
    }
    catch(...)
    {
        std::lock_guard<std::mutex> l(stdOutputMutex);
        *errStream << "Unknown exception happened" << std::endl;
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
                *errStream << "Failed join for stress thread #" << i << "!!!" << std::endl;
                retVal = 1;
            }
            delete testerThreads[i];
            testerThreads[i] = nullptr;
            if (gpuStressTesters.size() > i &&
                gpuStressTesters[i] != nullptr && !gpuStressTesters[i]->isFailed())
            {
                std::lock_guard<std::mutex> l(stdOutputMutex);
                *outStream << "Finished #" << i << std::endl;
            }
        }
    for (size_t i = 0; i < gpuStressTesters.size(); i++)
    {
        if (gpuStressTesters[i]->isFailed())
        {
            retVal = 1;
            std::lock_guard<std::mutex> l(stdOutputMutex);
            *errStream << "Failed #" << i << std::endl;
        }
        delete gpuStressTesters[i];
    }
    
    poptFreeContext(optsContext);
    return retVal;
}

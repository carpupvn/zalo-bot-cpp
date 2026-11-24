#pragma once
#include <windows.h>
#include <psapi.h>
#include <iphlpapi.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <intrin.h>
#include <algorithm>
#include <cstring>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "iphlpapi.lib")

struct SystemDetails {
    std::string os_name;
    std::string uptime_os;
    std::string cpu_model;
    std::string cpu_usage;
    int cpu_percent = 0;
    std::string processes;
    std::string ram_total, ram_used, ram_free;
    int ram_percent = 0;
    std::string disk_total, disk_used, disk_free;
    int disk_percent = 0;
    std::string net_sent, net_recv;
    std::string net_name;
    std::string net_desc;

    std::string bot_uptime;
    std::string bot_ram;
};

inline std::string fmt_bytes(ULONGLONG bytes) {
    const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
    int i = 0;
    double d = (double)bytes;
    while (d >= 1024.0 && i < 4) { d /= 1024.0; i++; }
    char buf[64]; snprintf(buf, sizeof(buf), "%.1f %s", d, suffixes[i]); return buf;
}

inline std::string fmt_time(ULONGLONG ms) {
    ULONGLONG s = ms / 1000;
    ULONGLONG d = s / 86400; s %= 86400;
    ULONGLONG h = s / 3600; s %= 3600;
    ULONGLONG m = s / 60; s %= 60;
    char buf[128]; snprintf(buf, sizeof(buf), "%llu ngày, %llu giờ, %llu phút, %llu giây", (unsigned long long)d, (unsigned long long)h, (unsigned long long)m, (unsigned long long)s); return buf;
}

inline SystemDetails get_sys_details(DWORD bot_pid) {
    SystemDetails info;

    info.os_name = "Windows";
    info.uptime_os = fmt_time(GetTickCount64());

    int CPUInfo[4] = {-1};
    char CPUBrandString[0x40] = {0};
    __cpuid(CPUInfo, 0x80000000);
    if ((unsigned int)CPUInfo[0] >= 0x80000004) {
        __cpuid(CPUInfo, 0x80000002); memcpy(CPUBrandString, CPUInfo, 16);
        __cpuid(CPUInfo, 0x80000003); memcpy(CPUBrandString + 16, CPUInfo, 16);
        __cpuid(CPUInfo, 0x80000004); memcpy(CPUBrandString + 32, CPUInfo, 16);
        info.cpu_model = std::string(CPUBrandString);
        auto f = info.cpu_model.find_first_not_of(" ");
        auto l = info.cpu_model.find_last_not_of(" ");
        if (f != std::string::npos && l != std::string::npos)
            info.cpu_model = info.cpu_model.substr(f, l - f + 1);
    } else info.cpu_model = "Unknown CPU";

    SYSTEM_INFO si; GetSystemInfo(&si);

    static FILETIME prevIdleTime = {}, prevKernelTime = {}, prevUserTime = {};
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        auto to_ull = [](const FILETIME& ft) { return ((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime; };
        ULONGLONG idle = to_ull(idleTime) - to_ull(prevIdleTime);
        ULONGLONG kernel = to_ull(kernelTime) - to_ull(prevKernelTime);
        ULONGLONG user = to_ull(userTime) - to_ull(prevUserTime);

        prevIdleTime = idleTime; prevKernelTime = kernelTime; prevUserTime = userTime;

        ULONGLONG total = kernel + user;
        if (total > 0) {
            int usage = (int)(100.0 - (idle * 100.0 / total));
            if (usage < 0) usage = 0;
            if (usage > 100) usage = 100;
            info.cpu_percent = usage;
            info.cpu_usage = std::to_string(si.dwNumberOfProcessors) + " Cores - " + std::to_string(usage) + "%";
        } else {
            info.cpu_percent = 0;
            info.cpu_usage = std::to_string(si.dwNumberOfProcessors) + " Cores - 0%";
        }
    }

    PERFORMANCE_INFORMATION pi;
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        info.processes = std::to_string(pi.ProcessCount) + " Processes | " +
                         std::to_string(pi.ThreadCount) + " Threads | " +
                         std::to_string(pi.HandleCount) + " Handles";
    }

    MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms); GlobalMemoryStatusEx(&ms);
    info.ram_total = fmt_bytes(ms.ullTotalPhys);
    info.ram_free = fmt_bytes(ms.ullAvailPhys);
    info.ram_used = fmt_bytes(ms.ullTotalPhys - ms.ullAvailPhys);
    info.ram_percent = (int)ms.dwMemoryLoad;

    ULARGE_INTEGER freeB, totalB, totalFreeB;
    if (GetDiskFreeSpaceExA("C:\\", &freeB, &totalB, &totalFreeB)) {
        info.disk_total = fmt_bytes(totalB.QuadPart);
        info.disk_free = fmt_bytes(totalFreeB.QuadPart);
        info.disk_used = fmt_bytes(totalB.QuadPart - totalFreeB.QuadPart);
        info.disk_percent = totalB.QuadPart > 0
            ? (int)(((double)(totalB.QuadPart - totalFreeB.QuadPart) / totalB.QuadPart) * 100)
            : 0;
    }

    info.net_name = "Default Adapter";
    info.net_desc = "TCP/IPv4 Ethernet/Wi-Fi";
    PMIB_IFTABLE pIfTable = nullptr; DWORD dwSize = 0;
    if (GetIfTable(NULL, &dwSize, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
        pIfTable = (PMIB_IFTABLE)malloc(dwSize);
        if (pIfTable && GetIfTable(pIfTable, &dwSize, FALSE) == NO_ERROR) {
            ULONGLONG sent = 0, recv = 0;
            ULONGLONG best_traffic = 0;
            for (DWORD i = 0; i < pIfTable->dwNumEntries; i++) {
                const MIB_IFROW& row = pIfTable->table[i];
                if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                sent += row.dwOutOctets;
                recv += row.dwInOctets;
                ULONGLONG traffic = (ULONGLONG)row.dwOutOctets + row.dwInOctets;
                if (row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL && traffic >= best_traffic && row.dwDescrLen > 0) {
                    best_traffic = traffic;
                    std::string desc((const char*)row.bDescr, row.dwDescrLen);
                    while (!desc.empty() && desc.back() == '\0') desc.pop_back();
                    if (!desc.empty()) info.net_desc = desc;
                    info.net_name = (row.dwType == 71) ? "Wi-Fi (802.11)" : "Ethernet";
                }
            }
            info.net_sent = fmt_bytes(sent);
            info.net_recv = fmt_bytes(recv);
        }
        if (pIfTable) free(pIfTable);
    }

    HANDLE hP = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, bot_pid);
    if (hP) {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(hP, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) info.bot_ram = fmt_bytes(pmc.WorkingSetSize);
        FILETIME ft, fs, fu, fc; GetProcessTimes(hP, &ft, &fs, &fu, &fc);
        ULARGE_INTEGER ui; ui.LowPart = ft.dwLowDateTime; ui.HighPart = ft.dwHighDateTime;
        GetSystemTimeAsFileTime(&ft); ULARGE_INTEGER now; now.LowPart = ft.dwLowDateTime; now.HighPart = ft.dwHighDateTime;
        info.bot_uptime = fmt_time((now.QuadPart - ui.QuadPart) / 10000);
        CloseHandle(hP);
    } else {
        info.bot_ram = "N/A"; info.bot_uptime = "N/A";
    }
    return info;
}

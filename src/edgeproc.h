#pragma once
// 进程看门狗数据层（参考 Edge-Monitor 的思路）：
// 枚举指定进程的全部实例，标注是否拥有可见窗口，读取 CPU 累计时间与工作集内存，
// 并提供终止原语。CPU 占用率由调用方对两次采样的差分计算。
#include <QString>
#include <QStringList>
#include <QSet>
#include <QList>
#include <windows.h>
#include <tlhelp32.h>
#define PSAPI_VERSION 1
#include <psapi.h>

namespace edgeproc {

struct ProcInfo {
    quint32 pid = 0;
    QString name;
    quint64 memWS = 0;   // 工作集，字节
    quint64 cpuTime = 0; // 内核+用户时间，100ns 单位（自进程启动累计）
    bool hasWindow = false;
};

// 当前拥有可见顶层窗口的进程 PID 集合
inline QSet<quint32> visibleWindowPids() {
    QSet<quint32> out;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        if (IsWindowVisible(h)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (pid) reinterpret_cast<QSet<quint32>*>(lp)->insert(pid);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
    return out;
}

// 枚举指定进程名（大小写不敏感）的全部实例
inline QList<ProcInfo> enumerate(const QString& nameLower) {
    QList<ProcInfo> out;
    QSet<quint32> vis = visibleWindowPids();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            QString pname = QString::fromWCharArray(pe.szExeFile);
            if (pname.compare(nameLower, Qt::CaseInsensitive) != 0) continue;
            ProcInfo pi;
            pi.pid = pe.th32ProcessID;
            pi.name = pname;
            pi.hasWindow = vis.contains(pi.pid);
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pi.pid);
            if (h) {
                FILETIME c{}, e{}, k{}, u{};
                if (GetProcessTimes(h, &c, &e, &k, &u)) {
                    auto to64 = [](const FILETIME& ft) {
                        return (quint64(ft.dwHighDateTime) << 32) | quint64(ft.dwLowDateTime);
                    };
                    pi.cpuTime = to64(k) + to64(u);
                }
                PROCESS_MEMORY_COUNTERS pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(h, &pmc, sizeof(pmc)))
                    pi.memWS = pmc.WorkingSetSize;
                CloseHandle(h);
            }
            out << pi;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

inline bool killPid(quint32 pid) {
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (!h) return false;
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok != FALSE;
}

} // namespace edgeproc

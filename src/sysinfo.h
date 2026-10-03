#pragma once
// 系统硬件信息采集（Win32 / 注册表 / QSysInfo），无第三方依赖。
// CpuUsage 为增量采样器：两次 sample() 之间按 GetSystemTimes 计算占用率。
#include <QString>
#include <QStringList>
#include <QSysInfo>
#include <QThread>
#include <QScreen>
#include <QGuiApplication>
#include <QSettings>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace sysinfo {

inline QString cpuName() {
    QSettings st(R"(HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0)",
                 QSettings::NativeFormat);
    QString n = st.value("ProcessorNameString").toString();
    return n.isEmpty() ? QString("未知处理器") : n.trimmed();
}

inline int cpuCores() { return QThread::idealThreadCount(); }

// CPU 占用率（增量采样，间隔越准越稳）
struct CpuUsage {
    ULONGLONG prevIdle = 0, prevTotal = 0;
    int sample() {
        FILETIME idleF, kernelF, userF;
        if (!GetSystemTimes(&idleF, &kernelF, &userF)) return -1;
        auto toU64 = [](const FILETIME& ft) {
            return (ULONGLONG(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
        };
        ULONGLONG idle = toU64(idleF);
        ULONGLONG total = toU64(kernelF) + toU64(userF);
        if (prevTotal == 0) { prevIdle = idle; prevTotal = total; return -1; }
        ULONGLONG dIdle = idle - prevIdle, dTotal = total - prevTotal;
        prevIdle = idle;
        prevTotal = total;
        if (dTotal == 0) return -1;
        return int(100 - 100 * dIdle / dTotal);
    }
};

struct MemInfo {
    double totalGB = 0, freeGB = 0;
    int usedPct() const { return totalGB > 0 ? int(100 * (totalGB - freeGB) / totalGB) : 0; }
};
inline MemInfo memory() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    GlobalMemoryStatusEx(&ms);
    MemInfo m;
    m.totalGB = ms.ullTotalPhys / (1024.0 * 1024 * 1024);
    m.freeGB = ms.ullAvailPhys / (1024.0 * 1024 * 1024);
    return m;
}

// 显卡：EnumDisplayDevices 取活动显示适配器描述
inline QStringList gpus() {
    QStringList out;
    for (DWORD i = 0;; ++i) {
        DISPLAY_DEVICEW dd{};
        dd.cb = sizeof(dd);
        if (!EnumDisplayDevicesW(nullptr, i, &dd, 0)) break;
        if (!(dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) &&
            dd.DeviceString[0])
            out << QString::fromWCharArray(dd.DeviceString).trimmed();
    }
    out.removeDuplicates();
    return out;
}

struct Disk {
    QString drive;
    double totalGB = 0, freeGB = 0;
};
inline QList<Disk> disks() {
    QList<Disk> out;
    wchar_t buf[512] = {};
    DWORD len = GetLogicalDriveStringsW(511, buf);
    for (wchar_t* d = buf; *d && len > 0; d += wcslen(d) + 1) {
        ULARGE_INTEGER total{}, freeQ{};
        if (GetDiskFreeSpaceExW(d, nullptr, &total, &freeQ) && total.QuadPart > 0) {
            Disk dk;
            dk.drive = QString::fromWCharArray(d);
            dk.totalGB = total.QuadPart / (1024.0 * 1024 * 1024);
            dk.freeGB = freeQ.QuadPart / (1024.0 * 1024 * 1024);
            out << dk;
        }
    }
    return out;
}

inline QString osInfo() { return QSysInfo::prettyProductName(); }
inline QString hostName() { return QSysInfo::machineHostName(); }

// 主板 / BIOS
inline QString boardInfo() {
    QSettings st(R"(HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\BIOS)",
                 QSettings::NativeFormat);
    QString maker = st.value("SystemManufacturer").toString();
    QString product = st.value("SystemProductName").toString();
    QString b = (maker + " " + product).trimmed();
    return b.isEmpty() ? "未知" : b;
}

inline QString uptime() {
    ULONGLONG ms = GetTickCount64();
    ULONGLONG m = ms / 60000;
    if (m < 60) return QString("%1 分钟").arg(m);
    ULONGLONG h = m / 60;
    if (h < 48) return QString("%1 小时 %2 分").arg(h).arg(m % 60);
    return QString("%1 天 %2 小时").arg(h / 24).arg(h % 24);
}

inline QString screenInfo() {
    QScreen* s = QGuiApplication::primaryScreen();
    if (!s) return "-";
    return QString("%1 × %2 @ %3% 缩放")
        .arg(s->size().width())
        .arg(s->size().height())
        .arg(int(s->devicePixelRatio() * 100));
}

struct BatteryInfo {
    bool present = false;
    int percent = 0;
    bool charging = false;
};
inline BatteryInfo battery() {
    SYSTEM_POWER_STATUS st;
    BatteryInfo bi;
    if (GetSystemPowerStatus(&st)) {
        bi.present = st.BatteryFlag != 128; // 128 = 无电池
        bi.percent = st.BatteryLifePercent <= 100 ? st.BatteryLifePercent : 0;
        bi.charging = (st.BatteryFlag & 8) != 0;
    }
    return bi;
}

} // namespace sysinfo

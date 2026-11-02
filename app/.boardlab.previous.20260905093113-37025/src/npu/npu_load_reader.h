#pragma once

#include <QString>
#include <QVariantList>

// Parsed RKNPU core utilization. Callers never receive raw debugfs text —
// only validated one-, two-, or three-core percentages (or unavailable).
struct NpuLoadReading {
    bool available = false;
    QVariantList cores;
    double averagePercent = -1.0;
    QString source;
    QString message;
};

class NpuLoadReader final
{
public:
    static QVariantList unavailableCores(int expectedCoreCount = 3);

    // Multi-path RKNPU load read for Buildroot root and Ubuntu/Debian non-root:
    // 1) direct debugfs when readable, 2) installed helper without sudo,
    // 3) sudo -n helper. Intended for worker threads (blocking bounded I/O).
    static NpuLoadReading readPrivileged(int expectedCoreCount = 3);

    // Test and restricted-mount-namespace support. Parsed values retain the
    // same contract as the privileged reader and raw debugfs text is not
    // returned in the result.
    static NpuLoadReading readDebugfsFile(const QString &path, int expectedCoreCount = 3);
};

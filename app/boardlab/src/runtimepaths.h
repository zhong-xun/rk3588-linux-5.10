#pragma once

#include <QDir>
#include <QString>
#include <QtGlobal>

namespace boardlab {

inline bool testModeEnabled()
{
    return qgetenv("BOARDLAB_TEST_MODE").trimmed() == QByteArrayLiteral("1");
}

inline QString fixtureRoot(const char *environmentName, const QString &productionRoot)
{
    if (!testModeEnabled()) {
        return productionRoot;
    }
    const QString configured = QString::fromLocal8Bit(qgetenv(environmentName)).trimmed();
    return configured.isEmpty() ? productionRoot : QDir::cleanPath(configured);
}

inline bool fixtureRootConfigured(const char *environmentName)
{
    return testModeEnabled() && !qgetenv(environmentName).trimmed().isEmpty();
}

inline QString deviceTreeRoot()
{
    return fixtureRoot("BOARDLAB_DEVICE_TREE_ROOT", QStringLiteral("/proc/device-tree"));
}

inline QString sysRoot()
{
    return fixtureRoot("BOARDLAB_SYS_ROOT", QStringLiteral("/sys"));
}

inline QString devRoot()
{
    return fixtureRoot("BOARDLAB_DEV_ROOT", QStringLiteral("/dev"));
}

} // namespace boardlab

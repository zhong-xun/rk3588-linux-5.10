#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QString>

namespace boardlab {

inline QString cameraSensorModelFromEvidence(const QString &evidence)
{
    const QString normalized = evidence.trimmed().toLower();
    if (normalized.contains(QStringLiteral("ov5695"))) {
        return QStringLiteral("ov5695");
    }
    if (normalized.contains(QStringLiteral("ov13850"))) {
        return QStringLiteral("ov13850");
    }
    if (normalized.contains(QStringLiteral("imx415"))) {
        return QStringLiteral("imx415");
    }
    return QString();
}

inline QString cameraPreviewRotateMethod(const QString &sensorModel)
{
    return cameraSensorModelFromEvidence(sensorModel) == QStringLiteral("ov5695")
               ? QStringLiteral("90l")
               : QString();
}

// `videoflip::method` is available across the GStreamer 1.x releases shipped
// by Ubuntu 20.04/22.04/24.04, Debian 11/12 and the supported Buildroot
// images.  Newer GStreamer also exposes `video-direction`, but relying on that
// newer alias would make the same pipeline fail on older desktop images.
inline QString cameraPreviewVideoflipPropertyName()
{
    return QStringLiteral("method");
}

inline QString cameraPreviewVideoflipValue(const QString &rotateMethod)
{
    return rotateMethod.trimmed().compare(QStringLiteral("90l"), Qt::CaseInsensitive) == 0
               ? QStringLiteral("counterclockwise")
               : QString();
}

inline QString readCameraSysfsEvidence(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

inline QString cameraVideo4LinuxSysfsRoot()
{
    const QString configured = qEnvironmentVariable("BOARDLAB_VIDEO4LINUX_SYSFS_ROOT").trimmed();
    return configured.isEmpty() ? QStringLiteral("/sys/class/video4linux") : configured;
}

// Rockchip capture-node names identify rkisp/rkcif rather than the attached
// sensor. A successfully probed sensor is exposed as a V4L2 subdevice, so use
// that live kernel evidence instead of a probe-order-dependent /dev/videoN.
inline QString cameraSensorModelForDevice(const QString &devicePath,
                                          const QString &sysfsRoot = QString())
{
    static const QRegularExpression videoNodePattern(QStringLiteral("^video[0-9]+$"));
    const QString nodeName = QFileInfo(devicePath.trimmed()).fileName();
    if (!videoNodePattern.match(nodeName).hasMatch()) {
        return QString();
    }

    const QString classRoot = sysfsRoot.trimmed().isEmpty()
                                  ? cameraVideo4LinuxSysfsRoot()
                                  : sysfsRoot.trimmed();
    const QString nodeRoot = QDir(classRoot).filePath(nodeName);
    const QString selectedEvidence = (QStringList()
                                          << readCameraSysfsEvidence(QDir(nodeRoot).filePath(QStringLiteral("name")))
                                          << readCameraSysfsEvidence(QDir(nodeRoot).filePath(QStringLiteral("device/uevent")))
                                          << readCameraSysfsEvidence(QDir(nodeRoot).filePath(QStringLiteral("device/modalias")))
                                          << readCameraSysfsEvidence(QDir(nodeRoot).filePath(QStringLiteral("device/of_node/compatible"))))
                                         .join(QLatin1Char('\n'));

    const QString directModel = cameraSensorModelFromEvidence(selectedEvidence);
    if (!directModel.isEmpty()) {
        return directModel;
    }

    const QString selectedKind = selectedEvidence.toLower();
    if (selectedKind.contains(QStringLiteral("hdmirx"))
        || selectedKind.contains(QStringLiteral("rk_hdmirx"))
        || selectedKind.contains(QStringLiteral("uvc"))
        || selectedKind.contains(QStringLiteral("usb"))
        || selectedKind.contains(QStringLiteral("webcam"))) {
        return QString();
    }
    if (!selectedKind.contains(QStringLiteral("rkisp"))
        && !selectedKind.contains(QStringLiteral("mainpath"))
        && !selectedKind.contains(QStringLiteral("rkcif"))
        && !selectedKind.contains(QStringLiteral("mipi"))
        && !selectedKind.contains(QStringLiteral("cif"))) {
        return QString();
    }

    QSet<QString> detectedModels;
    const QFileInfoList subdevices = QDir(classRoot).entryInfoList(
        QStringList() << QStringLiteral("v4l-subdev*"),
        QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name);
    for (const QFileInfo &subdevice : subdevices) {
        const QString subdeviceRoot = subdevice.absoluteFilePath();
        const QString evidence = (QStringList()
                                      << readCameraSysfsEvidence(QDir(subdeviceRoot).filePath(QStringLiteral("name")))
                                      << readCameraSysfsEvidence(QDir(subdeviceRoot).filePath(QStringLiteral("device/uevent")))
                                      << readCameraSysfsEvidence(QDir(subdeviceRoot).filePath(QStringLiteral("device/modalias")))
                                      << readCameraSysfsEvidence(QDir(subdeviceRoot).filePath(QStringLiteral("device/of_node/compatible"))))
                                     .join(QLatin1Char('\n'));
        const QString model = cameraSensorModelFromEvidence(evidence);
        if (!model.isEmpty()) {
            detectedModels.insert(model);
        }
    }

    // A single Topeet camera connector exposes one successfully probed sensor.
    // If several sensor models are active, do not guess which media graph owns
    // this node; leaving rotation unchanged is safer than rotating another feed.
    return detectedModels.size() == 1 ? *detectedModels.constBegin() : QString();
}

} // namespace boardlab

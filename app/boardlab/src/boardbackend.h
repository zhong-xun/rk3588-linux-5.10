#pragma once

#include <atomic>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <memory>
#include <mutex>
#include <termios.h>

class BoardProfile;
class GpuBenchmark;
class KeyMonitor;
class QTimer;
class WebCameraPreview;
class WifiManager;

class BoardBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList serialDevices READ serialDevices NOTIFY changed)
    Q_PROPERTY(QVariantList ethernetInterfaces READ ethernetInterfaces NOTIFY changed)
    Q_PROPERTY(QStringList canInterfaces READ canInterfaces NOTIFY changed)
    Q_PROPERTY(QStringList cellularDevices READ cellularDevices NOTIFY changed)
    // Server-derived USB modem identity and the exact, allowlisted Quectel
    // program selected for this board profile.  Clients can inspect it but
    // never provide a module name or executable path.
    Q_PROPERTY(QVariantMap cellularModemStatus READ cellularModemStatus NOTIFY changed)
    Q_PROPERTY(QStringList videoDevices READ videoDevices NOTIFY changed)
    Q_PROPERTY(QStringList cameraVideoDevices READ cameraVideoDevices NOTIFY changed)
    Q_PROPERTY(QStringList hdmiInVideoDevices READ hdmiInVideoDevices NOTIFY changed)
    Q_PROPERTY(QVariantList cameraCaptureDevices READ cameraCaptureDevices NOTIFY changed)
    Q_PROPERTY(QVariantList hdmiInCaptureDevices READ hdmiInCaptureDevices NOTIFY changed)
    Q_PROPERTY(QString cameraVideoDevice READ cameraVideoDevice NOTIFY changed)
    Q_PROPERTY(QString hdmiInVideoDevice READ hdmiInVideoDevice NOTIFY changed)
    Q_PROPERTY(QStringList watchdogDevices READ watchdogDevices NOTIFY changed)
    Q_PROPERTY(QVariantList fileEntries READ fileEntries NOTIFY changed)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY changed)
    Q_PROPERTY(bool backlightAvailable READ backlightAvailable NOTIFY changed)
    Q_PROPERTY(int backlightPercent READ backlightPercent NOTIFY changed)
    Q_PROPERTY(int audioVolumePercent READ audioVolumePercent NOTIFY changed)
    Q_PROPERTY(QVariantList audioPlaybackDevices READ audioPlaybackDevices NOTIFY changed)
    Q_PROPERTY(QVariantList audioCaptureDevices READ audioCaptureDevices NOTIFY changed)
    Q_PROPERTY(bool buzzerAvailable READ buzzerAvailable NOTIFY changed)
    Q_PROPERTY(QString buzzerAvailabilityStatus READ buzzerAvailabilityStatus NOTIFY changed)
    Q_PROPERTY(QString buzzerControlMethod READ buzzerControlMethod NOTIFY changed)
    Q_PROPERTY(QVariantMap fanStatus READ fanStatus NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(bool backendBusy READ backendBusy NOTIFY operationStateChanged)
    Q_PROPERTY(QString backendBusyOperation READ backendBusyOperation NOTIFY operationStateChanged)
    Q_PROPERTY(bool audioOperationBusy READ audioOperationBusy NOTIFY operationStateChanged)
    Q_PROPERTY(QVariantList storageDevices READ storageDevices NOTIFY changed)
    Q_PROPERTY(QVariantList ledDevices READ ledDevices NOTIFY changed)
    Q_PROPERTY(bool ledAvailable READ ledAvailable NOTIFY changed)
    Q_PROPERTY(QVariantMap rtcStatus READ rtcStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap npuStatus READ npuStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap wifiStatus READ wifiStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap bluetoothStatus READ bluetoothStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap gpuBenchmarkStatus READ gpuBenchmarkStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap webCameraPreviewStatus READ webCameraPreviewStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap wifiManagerStatus READ wifiManagerStatus NOTIFY changed)
    Q_PROPERTY(QVariantList wifiAccessPoints READ wifiAccessPoints NOTIFY changed)
    Q_PROPERTY(bool wifiManagerBusy READ wifiManagerBusy NOTIFY changed)
    Q_PROPERTY(QVariantList inputDevices READ inputDevices NOTIFY changed)
    Q_PROPERTY(QVariantList displayOutputs READ displayOutputs NOTIFY changed)
    Q_PROPERTY(QVariantMap environmentStatus READ environmentStatus NOTIFY changed)
    // Kept as an alias while Qt and Web clients move to environmentStatus.
    Q_PROPERTY(QVariantMap environmentState READ environmentState NOTIFY changed)
    Q_PROPERTY(bool environmentRefreshInFlight READ environmentRefreshInFlight NOTIFY changed)
    Q_PROPERTY(QVariantMap keyTestStatus READ keyTestStatus NOTIFY changed)
    Q_PROPERTY(QVariantMap hdmiInStatus READ hdmiInStatus NOTIFY changed)

public:
    explicit BoardBackend(BoardProfile *boardProfile, QObject *parent = nullptr);
    ~BoardBackend() override;

    QStringList serialDevices() const;
    QVariantList ethernetInterfaces() const;
    QStringList canInterfaces() const;
    QStringList cellularDevices() const;
    QVariantMap cellularModemStatus() const;
    QStringList videoDevices() const;
    QStringList cameraVideoDevices() const;
    QStringList hdmiInVideoDevices() const;
    QVariantList cameraCaptureDevices() const;
    QVariantList hdmiInCaptureDevices() const;
    QString cameraVideoDevice() const;
    QString hdmiInVideoDevice() const;
    QStringList watchdogDevices() const;
    QVariantList fileEntries() const;
    QString currentPath() const;
    bool backlightAvailable() const;
    int backlightPercent() const;
    int audioVolumePercent() const;
    QVariantList audioPlaybackDevices() const;
    QVariantList audioCaptureDevices() const;
    bool buzzerAvailable() const;
    QString buzzerAvailabilityStatus() const;
    QString buzzerControlMethod() const;
    QVariantMap fanStatus() const;
    QString requiredCapabilityForOperation(const QString &operation) const;
    bool operationAllowed(const QString &operation) const;
    QString operationUnavailableReason(const QString &operation) const;
    QString statusText() const;
    bool backendBusy() const;
    QString backendBusyOperation() const;
    bool audioOperationBusy() const;
    QVariantList storageDevices() const;
    QVariantList ledDevices() const;
    bool ledAvailable() const;
    QVariantMap rtcStatus() const;
    QVariantMap npuStatus() const;
    QVariantMap wifiStatus() const;
    QVariantMap bluetoothStatus() const;
    QVariantMap gpuBenchmarkStatus() const;
    QVariantMap webCameraPreviewStatus() const;
    QVariantMap wifiManagerStatus() const;
    QVariantList wifiAccessPoints() const;
    bool wifiManagerBusy() const;
    QVariantList inputDevices() const;
    QVariantList displayOutputs() const;
    QVariantMap environmentStatus() const;
    QVariantMap environmentState() const;
    bool environmentRefreshInFlight() const;
    QVariantMap keyTestStatus() const;
    QVariantMap hdmiInStatus() const;

    Q_INVOKABLE void refresh();
    // Performs the DHT-safe read-only sequence in a worker thread.  The
    // result is published through environmentStatus/environmentState.
    Q_INVOKABLE bool refreshEnvironmentAsync();
    // The input monitor accepts only a stable device-tree ID from the
    // backend's current discovery snapshot.  It never accepts /dev/input
    // paths from QML or the Web API.
    Q_INVOKABLE bool startKeyMonitoring(const QString &deviceId);
    Q_INVOKABLE void stopKeyMonitoring();
    Q_INVOKABLE void clearKeyTestEvents();
    // GPU and Wi-Fi are independent applications. Their helpers own the
    // long-running QProcess work; BoardBackend only serializes the request
    // with other hardware actions and exposes an immutable status snapshot.
    Q_INVOKABLE bool discoverGpuBenchmark();
    Q_INVOKABLE bool startGpuBenchmark();
    Q_INVOKABLE bool stopGpuBenchmark();
    // The visible preview drives the fixed Qt Quick ShaderEffect embedded in
    // the GPU test page.  It never creates a separate Weston/X11 window;
    // callers cannot supply rendering parameters or command-line arguments.
    Q_INVOKABLE bool startGpuPreview();
    Q_INVOKABLE bool stopGpuPreview();
    // Browser preview is intentionally a separate, authenticated JPEG path.
    // The device and capture mode are resolved from the current discovery
    // snapshot; Web callers cannot pass arbitrary GStreamer parameters.
    bool startWebCameraPreview(const QString &device);
    bool stopWebCameraPreview();
    QByteArray webCameraPreviewJpeg() const;
    Q_INVOKABLE bool refreshWifiAsync();
    Q_INVOKABLE bool scanWifiAsync(const QString &interfaceName = QString());
    Q_INVOKABLE bool connectWifiAsync(const QString &interfaceName,
                                      const QString &networkId,
                                      const QString &password = QString());
    Q_INVOKABLE bool disconnectWifiAsync(const QString &interfaceName);
    Q_INVOKABLE bool setBacklightPercent(int percent);
    Q_INVOKABLE bool setGpioValue(const QString &pinName, bool high);
    Q_INVOKABLE bool setGpioMode(const QString &pinName, const QString &mode, bool high);
    Q_INVOKABLE QString readGpioValue(const QString &pinName);
    Q_INVOKABLE bool sendSerialTestFrame(const QString &device, int baudRate);
    Q_INVOKABLE QString readSerialSample(const QString &device, int baudRate);
    Q_INVOKABLE bool sendSerialTestFrameConfigured(const QString &device, int baudRate, int dataBits, const QString &parity, int stopBits, const QString &flowControl);
    Q_INVOKABLE QString readSerialSampleConfigured(const QString &device, int baudRate, int dataBits, const QString &parity, int stopBits, const QString &flowControl);
    bool playAudioTest();
    bool recordAudioSample();
    bool playRecordedAudio();
    bool stopAudioPlayback();
    bool setAudioVolumePercent(int percent);
    bool setBeepPercent(int percent);
    bool startBeepHeartbeat();
    bool stopBeep();
    // Return whether the operation was accepted synchronously.  The actual
    // audio result remains asynchronous and is reported through
    // operationFinished().  HTTP callers use this to avoid claiming that a
    // rejected request was submitted successfully.
    Q_INVOKABLE bool runAudioOperationAsync(const QString &operation, int value = -1,
                                            const QString &playbackDevice = QString(),
                                            const QString &captureDevice = QString());
    Q_INVOKABLE bool runHardwareOperationAsync(const QString &operation, const QVariantMap &parameters = QVariantMap());
    Q_INVOKABLE bool setFanAuto();
    Q_INVOKABLE bool setFanManualPercent(int percent);
    bool openPath(const QString &path);
    bool goUp();
    Q_INVOKABLE void openPathAsync(const QString &path);
    Q_INVOKABLE void goUpAsync();
    Q_INVOKABLE QString gpioPreview(const QString &pinName, bool high) const;
    // Legacy synchronous CAN helpers are intentionally not Q_INVOKABLE.
    // Every UI/API CAN operation must go through runHardwareOperationAsync(),
    // which enforces profile discovery, user acknowledgement and busy-state
    // serialization before it can reach a real bus.
    bool configureCanInterface(const QString &interfaceName, int bitrate, int dataBitrate, bool fdMode);
    bool closeCanInterface(const QString &interfaceName);
    bool sendCanTestFrame(const QString &interfaceName);
    QString readCanSample(const QString &interfaceName);
    bool startCanReferenceSender(const QString &interfaceName, int bitrate);
    bool stopCanReferenceSender();
    Q_INVOKABLE bool renewDhcp(const QString &interfaceName);
    Q_INVOKABLE bool setStaticIp(const QString &interfaceName, const QString &address, const QString &netmask, const QString &gateway);
    Q_INVOKABLE QString pingInterface(const QString &interfaceName, const QString &target);
    Q_INVOKABLE bool pingInterfaceAsync(const QString &interfaceName, const QString &target);
    Q_INVOKABLE bool startCellularConnection();
    Q_INVOKABLE bool startCellularConnectionWithOptions(const QString &apn, const QString &interfaceName);
    Q_INVOKABLE QString preferredCellularInterface() const;
    Q_INVOKABLE QString cellularStatusReport() const;
    Q_INVOKABLE bool stopCellularConnection();
    bool startCameraTest(const QString &mode, const QString &device);
    bool stopCameraTest();
    Q_INVOKABLE QString watchdogStatusReport() const;
    Q_INVOKABLE bool startWatchdogReferenceTest(const QString &device, int timeoutSeconds, bool confirmed = false);
    // These return false when another backend task owns the operation lock.
    // QML callers may ignore the return value, while HTTP callers can turn it
    // into a useful 409 response instead of reporting a request as accepted.
    Q_INVOKABLE bool scanStorageDevicesAsync();
    Q_INVOKABLE bool mountStorageDeviceAsync(const QString &device, const QString &mountPoint);
    Q_INVOKABLE bool unmountStorageDeviceAsync(const QString &mountPoint);
    void scanStorageDevices();
    bool mountStorageDevice(const QString &device, const QString &mountPoint);
    bool unmountStorageDevice(const QString &mountPoint);
    Q_INVOKABLE bool testStorageSpeed(const QString &mountPoint);

signals:
    void changed();
    void operationStateChanged();
    void operationFinished(const QString &operation, bool success, const QString &result, const QString &status);

private:
    struct BacklightDevice {
        QString name;
        QString path;
        int maxBrightness = 0;
        int brightness = 0;
    };

    struct FanDevice {
        QString name;
        QString path;
        QString pwmPath;
        QString pwmMaxPath;
        QString tempControlPath;
        int pwm = 0;
        int pwmMax = 255;
        int tempControl = -1;
        bool ambiguous = false;
    };

    struct VideoDeviceInfo {
        QString path;
        QString groupName;
        QString sysfsName;
        QString detailText;
        QString mode;
        // Higher values are preferred for the interactive preview.  On
        // Rockchip ISP pipelines, rkcif exposes raw Bayer ingress nodes while
        // rkisp_mainpath exposes the processed YUV stream an operator can
        // actually view.  Keep the ranking with the discovery evidence rather
        // than inferring it from a probe-order-dependent video number.
        int cameraPriority = 0;
        bool camera = false;
        bool hdmiIn = false;
    };

    struct VideoDeviceScanResult {
        QStringList videoDevices;
        QStringList cameraVideoDevices;
        QStringList hdmiInVideoDevices;
        QVariantList cameraCaptureDevices;
        QVariantList hdmiInCaptureDevices;
        QString cameraVideoDevice;
        QString hdmiInVideoDevice;
    };

    static int gpioNumberFromName(const QString &pinName);
    static speed_t serialSpeed(int baudRate);
    static bool configureSerialPort(int fd, int baudRate, int dataBits, const QString &parity, int stopBits, const QString &flowControl, QString *errorText);
    static QString commandPath(const QString &command);
    static QString runCommand(const QString &program, const QStringList &arguments, int timeoutMs, int *exitCode);
    static bool startDetachedWithTimeout(const QString &program, const QStringList &arguments, int timeoutSeconds, qint64 *pid);
    static QString readTextFile(const QString &path);
    static bool writeTextFile(const QString &path, const QString &text, QString *errorText);
    static QString interfaceIpv4Address(const QString &interfaceName);
    static QString nmcliConnectionNameForDevice(const QString &interfaceName);
    static int netmaskPrefixLength(const QString &netmask);
    static VideoDeviceInfo inspectVideoDevice(const QString &path, const QString &groupName);
    static QVariantMap videoDeviceInfoToMap(const VideoDeviceInfo &info, const QString &kind, bool preferred);
    static VideoDeviceScanResult scanVideoDevices();
    static void appendDetectedVideoDeviceToResult(const VideoDeviceInfo &info, VideoDeviceScanResult *result);

    void refreshSerialDevices();
    void refreshNetworkDevices();
    void refreshVideoDevices();
    void refreshVideoDevicesAsync();
    void refreshVideoDevicesFromV4l2(QStringList *detectedDevices);
    void appendDetectedVideoDevice(const VideoDeviceInfo &info, QStringList *detectedDevices);
    bool applyVideoDeviceScan(const VideoDeviceScanResult &result);
    void refreshWatchdogDevices();
    void refreshBacklight();
    void refreshLedDevices();
    void refreshBuzzerAvailability();
    void refreshFan();
    void refreshAudioVolume();
    void refreshAudioDevices();
    void refreshReadOnlyDiagnostics();
    void refreshKeyTestDevices();
    void refreshFileEntries();
    void setStatus(const QString &status);
    bool beginAsyncOperation(const QString &operation);
    void finishAsyncOperation(const QString &operation, bool success, const QString &result, const QString &status);
    void startAudioVolumeOperation(int percent, const QString &playbackDevice = QString());
    void stopAudioProcessesAsync();
    void startFileBrowseAsync(const QString &operation, const QString &path);

    QStringList m_serialDevices;
    QVariantList m_ethernetInterfaces;
    QStringList m_canInterfaces;
    QStringList m_cellularDevices;
    QVariantMap m_cellularModemStatus;
    qint64 m_cellularDialerPid = 0;
    QString m_cellularDialerPath;
    QStringList m_videoDevices;
    QStringList m_cameraVideoDevices;
    QStringList m_hdmiInVideoDevices;
    QVariantList m_cameraCaptureDevices;
    QVariantList m_hdmiInCaptureDevices;
    QString m_cameraVideoDevice;
    QString m_hdmiInVideoDevice;
    QStringList m_watchdogDevices;
    QVariantList m_fileEntries;
    QString m_currentPath;
    BacklightDevice m_backlight;
    int m_audioVolumePercent = 50;
    QVariantList m_audioPlaybackDevices;
    QVariantList m_audioCaptureDevices;
    bool m_buzzerAvailable = false;
    QString m_buzzerAvailabilityStatus = QStringLiteral("蜂鸣器尚未检测");
    QString m_buzzerControlMethod = QStringLiteral("unavailable");
    QString m_buzzerInputEventDevice;
    QString m_statusText;
    bool m_backendBusy = false;
    bool m_audioOperationBusy = false;
    bool m_audioVolumeBusy = false;
    bool m_audioStopPending = false;
    bool m_pendingAudioStopAll = false;
    QString m_backendBusyOperation;
    int m_pendingAudioVolumePercent = -1;
    QString m_pendingAudioVolumePlaybackDevice;
    qint64 m_lastRefreshMs = 0;
    bool m_refreshInFlight = false;
    bool m_videoRefreshInFlight = false;
    QVector<qint64> m_audioProcessPids;
    QVector<qint64> m_beepProcessPids;
    // The heartbeat worker and stop requests run on different threads. The
    // atomic records cancellation while this mutex serializes the check and
    // every evdev write, so a cancelled heartbeat cannot emit another tone.
    std::shared_ptr<std::atomic_bool> m_buzzerHeartbeatCancelled =
        std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<std::mutex> m_buzzerToneWriteMutex = std::make_shared<std::mutex>();
    FanDevice m_fan;
    QVariantList m_storageDevices;
    QVariantList m_ledDevices;
    QVariantMap m_rtcStatus;
    QVariantMap m_npuStatus;
    QVariantMap m_wifiStatus;
    QVariantMap m_bluetoothStatus;
    QVariantList m_inputDevices;
    QVariantList m_displayOutputs;
    QVariantMap m_environmentRawState;
    QVariantMap m_environmentState;
    bool m_environmentRefreshInFlight = false;
    QVariantList m_keyTestDevices;
    KeyMonitor *m_keyMonitor = nullptr;
    QTimer *m_keyMappingVerificationTimer = nullptr;
    GpuBenchmark *m_gpuBenchmark = nullptr;
    WebCameraPreview *m_webCameraPreview = nullptr;
    WifiManager *m_wifiManager = nullptr;
    QVariantMap m_hdmiInStatus;
    BoardProfile *m_boardProfile = nullptr;
};

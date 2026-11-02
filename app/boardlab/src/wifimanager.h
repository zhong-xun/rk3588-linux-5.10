#pragma once

#include <QByteArray>
#include <QObject>
#include <QDateTime>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QTimer;

// Explicit NetworkManager Wi-Fi operations for BoardLab.
//
// Passive BoardLab startup must not use this class: some Rockchip images ask
// for a polkit Wi-Fi-scan permission when nmcli is queried.  Construct this
// service for the dedicated Wi-Fi application and invoke its operations only
// after a user action.  Every nmcli invocation uses QProcess program and
// argument APIs; no input is passed through a shell.
class WifiManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(QVariantList accessPoints READ accessPoints NOTIFY accessPointsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit WifiManager(QObject *parent = nullptr);
    // The explicit program constructor is useful for an integration test
    // fixture.  Production callers normally use the default constructor.
    explicit WifiManager(const QString &nmcliProgram, QObject *parent = nullptr,
                         int scanCacheMaximumAgeMs = 300000);
    ~WifiManager() override;

    QVariantMap state() const;
    QVariantList accessPoints() const;
    bool busy() const;

    // Pure helpers for the `nmcli -t -e yes` output used by this service.
    // They intentionally do not invoke processes and can be tested without
    // NetworkManager or wireless hardware.
    static QStringList splitTerseLine(const QString &line);
    static QVariantList parseWifiDeviceStatus(const QString &output);
    static QVariantList parseAccessPoints(const QString &output);
    // Parses the read-only `IP4.ADDRESS` field emitted by nmcli device show.
    // Each returned row has address, prefix and cidr fields.  This is kept
    // public for the fixture tests and does not invoke any system command.
    static QVariantList parseIpv4Addresses(const QString &output);
    static QVariantMap classifyNmcliError(const QString &output, bool timedOut = false);
    // Classifies a NetworkManager device STATE string into a bilingual diagnostic.
    // Optional interfaceName enables non-destructive local hints (ifupdown/dhcpcd).
    static QVariantMap diagnoseWifiDeviceState(const QString &state,
                                               const QString &interfaceName = QString());
    static bool isSafeInterfaceName(const QString &interfaceName);
    static bool isValidSsid(const QString &ssid);
    static bool isValidPassword(const QString &password);
    static bool isScanCacheFresh(const QDateTime &scannedAt,
                                 const QDateTime &currentTime,
                                 int maximumAgeMs);

    // All operations are asynchronous.  A false return means that the
    // request was rejected synchronously (bad input, nmcli missing, or an
    // operation already running); a true return means completion will be
    // reported by operationFinished().  `refresh()` is an explicit UI action
    // only; callers must never invoke it from passive application refresh.
    Q_INVOKABLE bool refresh();
    Q_INVOKABLE bool scan(const QString &interfaceName = QString());
    // networkId must be an unexpired ID returned by accessPoints() for this
    // interface.  BoardLab currently uses the scan result's BSSID as that ID,
    // so untrusted callers cannot ask nmcli to join an arbitrary SSID.
    Q_INVOKABLE bool connectToNetwork(const QString &interfaceName,
                                      const QString &networkId,
                                      const QString &password = QString());
    Q_INVOKABLE bool disconnect(const QString &interfaceName);

signals:
    void stateChanged();
    void accessPointsChanged();
    void busyChanged();
    // `result` deliberately never contains the supplied Wi-Fi password.
    void operationFinished(const QString &operation, bool success, const QVariantMap &result);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onCommandTimeout();

private:
    enum class Operation {
        None,
        Refresh,
        Scan,
        Connect,
        Disconnect
    };

    enum class Stage {
        Idle,
        ReadRadio,
        ReadDevices,
        ReadIpv4,
        Rescan,
        ListAccessPoints,
        Connect,
        Disconnect
    };

    bool begin(Operation operation, const QString &interfaceName = QString());
    bool reject(const QString &operation, const QString &code,
                const QString &message, const QString &messageEn);
    void startStage(Stage stage);
    // The installed application uses a root-owned, argument-whitelisting
    // helper through sudo. `standardInput` is used only for a Wi-Fi password
    // so it never appears in either sudo or nmcli arguments.
    void startNmcli(const QStringList &arguments, int timeoutMs,
                    const QByteArray &standardInput = QByteArray());
    void handleStageResult(int exitCode, QProcess::ExitStatus exitStatus,
                           const QString &standardOutput, const QString &standardError,
                           bool timedOut);
    void handleDeviceStatus(const QString &output);
    void handleIpv4Addresses(const QString &output, const QString &error = QString());
    void continueAfterDeviceStatus();
    void finish(bool success, const QString &code,
                const QString &message, const QString &messageEn);
    void updateState(const QVariantMap &changes);
    QString resolvedNmcliProgram() const;
    QString resolvedSudoProgram() const;
    bool usesRestrictedWifiHelper() const;
    // root-nmcli | restricted-sudo-helper | networkmanager-polkit
    QString authorizationModeName() const;
    QString selectedWifiDevice() const;
    QVariantMap selectedDeviceState() const;
    QVariantMap cachedNetwork(const QString &interfaceName, const QString &networkId) const;
    QString operationName() const;
    static QString operationNameFor(Operation operation);
    static QString statusForOperation(Operation operation);
    static QString messageForOperation(Operation operation);
    static QString messageEnForOperation(Operation operation);

    QString m_nmcliProgram;
    QProcess *m_process = nullptr;
    QTimer *m_timeoutTimer = nullptr;
    QVariantMap m_state;
    QVariantList m_devices;
    QVariantList m_accessPoints;
    QString m_requestedInterface;
    QString m_requestedNetworkId;
    QString m_requestedSsid;
    QString m_requestedBssid;
    // Kept only until the nmcli connection request finishes.  It is never
    // placed in a state map, result, signal payload, or log message.
    QString m_pendingPassword;
    QString m_scanCacheInterface;
    QDateTime m_scanCacheTimestamp;
    int m_scanCacheMaximumAgeMs = 300000;
    Operation m_operation = Operation::None;
    Stage m_stage = Stage::Idle;
    bool m_busy = false;
    bool m_commandActive = false;
    bool m_commandTimedOut = false;
};

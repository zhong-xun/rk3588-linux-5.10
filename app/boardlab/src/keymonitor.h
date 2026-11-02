#pragma once

#include <QObject>
#include <QByteArray>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class QTimer;

// A read-only monitor for device-tree-backed board user keys.
// The caller owns discovery and passes device-tree-backed candidate rows.  A
// deviceId, never a client-provided event path, selects a candidate to open.
class KeyMonitor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString activeDeviceId READ activeDeviceId NOTIFY activeChanged)
    Q_PROPERTY(QString activeEventPath READ activeEventPath NOTIFY activeChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)

public:
    explicit KeyMonitor(QObject *parent = nullptr);
    ~KeyMonitor() override;

    bool active() const;
    QString activeDeviceId() const;
    // Runtime evidence for the active monitor.  This is never a caller
    // selector: callers must continue to use activeDeviceId/device-tree ID.
    QString activeEventPath() const;
    QVariantList events() const;
    QVariantMap state() const;
    QString lastError() const;

    // A candidate must include deviceId, dtPath, source=device-tree,
    // compatible/dtCompatible (adc-keys or gpio-keys), and eventPath.  event
    // is accepted as a compatibility alias for eventPath, but never as an ID.
    static bool isSafeCandidate(const QVariantMap &candidate, QString *reason = nullptr);
    static QVariantList filterSafeCandidates(const QVariantList &candidates);
    static bool isSafeKeyCode(int keyCode);
    static QString friendlyKeyName(int keyCode);

    // Resolves deviceId only from the supplied discovery snapshot.  It never
    // accepts a raw /dev/input path from callers.
    bool start(const QString &deviceId, const QVariantList &discoveryCandidates);
    // An optional status is used when discovery revokes an active mapping.
    // Ordinary callers continue to receive the existing stopped/passed text.
    void stop(const QString &statusText = QString());
    void clear();

signals:
    void activeChanged();
    void eventsChanged();
    void stateChanged();
    void eventCaptured(const QVariantMap &event);

private slots:
    void pollInput();

private:
    static QString candidateDeviceId(const QVariantMap &candidate);
    static QString candidateEventPath(const QVariantMap &candidate);
    static QSet<int> candidateKeyCodes(const QVariantMap &candidate);
    static bool hasUnsafeKeyCode(const QSet<int> &codes);

    bool validateRuntimeInput(QString *errorText) const;
    bool queryEvKeyCapabilities(QSet<int> *codes, QString *errorText) const;
    void processInputEvent(int keyCode, int value);
    void appendEvent(int keyCode, int value, bool counted);
    void resetProgress();
    void setError(const QString &errorText);
    void setStatus(const QString &statusText);
    bool testPassed() const;

    int m_fd = -1;
    QTimer *m_pollTimer = nullptr;
    QByteArray m_pendingBytes;
    QVariantMap m_candidate;
    QString m_activeDeviceId;
    QString m_status;
    QString m_lastError;
    QVariantList m_events;
    QSet<int> m_expectedCodes;
    QSet<int> m_pressedCodes;
    QSet<int> m_seenPresses;
    QSet<int> m_seenReleases;
    bool m_fifoTestDevice = false;
};

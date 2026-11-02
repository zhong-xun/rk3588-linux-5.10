#pragma once

#include <QObject>
#include <QTcpServer>
#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QVariantMap>

class BoardBackend;
class BoardProfile;
class NpuYoloDemo;
class QTcpSocket;
class ResourceMonitor;
class SystemInfo;
class QUrl;

class LocalWebServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(QString bindAddress READ bindAddress NOTIFY changed)
    Q_PROPERTY(quint16 port READ port NOTIFY changed)
    Q_PROPERTY(QString url READ url NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
    explicit LocalWebServer(SystemInfo *systemInfo, BoardBackend *boardBackend, BoardProfile *boardProfile,
                            ResourceMonitor *resourceMonitor, NpuYoloDemo *npuYoloDemo,
                            QObject *parent = nullptr);
    ~LocalWebServer() override;

    bool running() const;
    QString bindAddress() const;
    quint16 port() const;
    QString url() const;
    QString lanHostAddress() const;
    QString statusText() const;

    bool start(const QHostAddress &address, quint16 port, const QByteArray &accessToken);
    void stop();

    Q_INVOKABLE QVariantMap readinessData() const;

signals:
    void changed();

private:
    void closeClientSockets();
    void handleConnection();
    void handleReadyRead(QTcpSocket *socket);
    void sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType, const QByteArray &body,
                      const QByteArray &extraHeaders = QByteArray()) const;
    void sendUnauthorized(QTcpSocket *socket) const;
    void sendNotFound(QTcpSocket *socket) const;
    bool isAuthorized(const QByteArray &headerBlock) const;
    QByteArray htmlDashboard() const;
    QByteArray legacyHtmlDashboard() const;
    QByteArray controlJson(const QByteArray &body, int *statusCode);
    QByteArray systemJson() const;
    QByteArray boardJson() const;
    QByteArray environmentJson() const;
    QByteArray keyTestJson() const;
    QByteArray gpuJson() const;
    QByteArray cameraJson() const;
    QByteArray wifiJson() const;
    QByteArray resourcesJson();
    QJsonObject npuYoloStatusObject() const;
    QByteArray npuYoloJson() const;
    bool npuYoloPreviewJpeg(QByteArray *jpeg) const;
    bool webCameraPreviewJpeg(QByteArray *jpeg) const;
    QByteArray uploadFileJson(const QUrl &requestUrl, const QByteArray &body, int *statusCode);
    QJsonObject readinessObject() const;
    QJsonObject cachedReadinessObject() const;
    QByteArray readinessJson() const;
    QByteArray manifestJson() const;
    QByteArray handoffJson() const;
    QByteArray healthJson() const;
    QByteArray jsonEscape(const QString &value) const;
    QByteArray jsonStringList(const QStringList &items) const;

    QTcpServer m_server;
    SystemInfo *m_systemInfo = nullptr;
    BoardBackend *m_boardBackend = nullptr;
    BoardProfile *m_boardProfile = nullptr;
    ResourceMonitor *m_resourceMonitor = nullptr;
    NpuYoloDemo *m_npuYoloDemo = nullptr;
    QHostAddress m_bindAddress = QHostAddress::AnyIPv4;
    quint16 m_port = 0;
    QByteArray m_accessToken;
    QString m_statusText;
    // The HTTP control endpoint accepts work asynchronously. Keep its final
    // result so a browser never mistakes acceptance for hardware success.
    QJsonObject m_lastOperation;
    QHash<QTcpSocket *, QByteArray> m_requestBuffers;
    mutable qint64 m_readinessCacheMs = 0;
    mutable QJsonObject m_readinessCache;
};

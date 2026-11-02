#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <memory>

class SingleInstance final : public QObject
{
    Q_OBJECT
public:
    enum class Result { Primary, Activated, Error };
    explicit SingleInstance(QObject *parent = nullptr);
    Result start(const QString &sessionIdentity);
    QString errorString() const;

signals:
    void activationRequested();

private:
    void acceptConnections();
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
    QString m_error;
};

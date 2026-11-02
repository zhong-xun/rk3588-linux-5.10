#include "singleinstance.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::acceptConnections);
}

SingleInstance::Result SingleInstance::start(const QString &sessionIdentity)
{
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty() || !QDir().mkpath(runtime)) {
        m_error = QStringLiteral("Cannot create the session runtime directory");
        return Result::Error;
    }
    const QByteArray digest = QCryptographicHash::hash(sessionIdentity.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    const QString endpoint = QDir(runtime).filePath(QStringLiteral("boardlab-") + QString::fromLatin1(digest));
    m_lock.reset(new QLockFile(endpoint + QStringLiteral(".lock")));
    m_lock->setStaleLockTime(0);

    QElapsedTimer deadline;
    deadline.start();
    while (!m_lock->tryLock()) {
        if (m_lock->error() != QLockFile::LockFailedError || deadline.elapsed() >= 3000) {
            m_error = QStringLiteral("Another BoardLab instance is starting or the session lock is unavailable");
            return Result::Error;
        }
        QLocalSocket existing;
        existing.connectToServer(endpoint);
        if (existing.waitForConnected(150)) {
            existing.write("activate\n");
            existing.flush();
            if (existing.waitForReadyRead(500) && existing.readLine() == QByteArrayLiteral("ok\n"))
                return Result::Activated;
        }
        QThread::msleep(25);
    }

    // Only the lock owner may remove a socket left by a crashed instance.
    QLocalServer::removeServer(endpoint);
    if (!m_server.listen(endpoint)) {
        m_error = m_server.errorString();
        return Result::Error;
    }
    return Result::Primary;
}

QString SingleInstance::errorString() const
{
    return m_error;
}

void SingleInstance::acceptConnections()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        socket->setReadBufferSize(64);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        const auto receive = [this, socket]() {
            if (!socket->canReadLine() && socket->bytesAvailable() < 32)
                return;
            if (socket->readLine(32) == QByteArrayLiteral("activate\n")) {
                emit activationRequested();
                socket->write("ok\n");
            }
            socket->disconnectFromServer();
        };
        connect(socket, &QLocalSocket::readyRead, socket, receive);
        QTimer::singleShot(1000, socket, [socket]() { socket->abort(); socket->deleteLater(); });
        receive();
    }
}

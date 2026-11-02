#pragma once

#include <QQuickImageProvider>

class QrImageProvider : public QQuickImageProvider
{
public:
    QrImageProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};

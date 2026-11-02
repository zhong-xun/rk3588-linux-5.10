#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class BoardProfile : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id READ id CONSTANT)
    Q_PROPERTY(QString displayName READ displayName CONSTANT)
    Q_PROPERTY(QString socFamily READ socFamily CONSTANT)
    Q_PROPERTY(QString deviceTreeModel READ deviceTreeModel CONSTANT)
    Q_PROPERTY(QString deviceTreeCompatible READ deviceTreeCompatible CONSTANT)
    Q_PROPERTY(QString detectionSource READ detectionSource CONSTANT)
    Q_PROPERTY(QVariantMap capabilities READ capabilities CONSTANT)
    Q_PROPERTY(QStringList ledPaths READ ledPaths CONSTANT)

public:
    explicit BoardProfile(QObject *parent = nullptr);

    QString id() const;
    QString displayName() const;
    QString socFamily() const;
    QString deviceTreeModel() const;
    QString deviceTreeCompatible() const;
    QString detectionSource() const;
    QVariantMap capabilities() const;
    QStringList ledPaths() const;

    Q_INVOKABLE bool hasCapability(const QString &name) const;
    Q_INVOKABLE QVariantMap summary() const;
    QJsonObject toJson() const;

private:
    QString m_id;
    QString m_displayName;
    QString m_socFamily;
    QString m_deviceTreeModel;
    QString m_deviceTreeCompatible;
    QString m_detectionSource;
    QVariantMap m_capabilities;
    QStringList m_ledPaths;
};

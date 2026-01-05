/*
 * SPDX-FileCopyrightText: 2013 - 2020 Jolla Ltd.
 * SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DECLARATIVESETTINGS_H
#define DECLARATIVESETTINGS_H

#include <QObject>
#include <QDateTime>

#include <QUrl>
#include <MGConfItem>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

class PartitionManager;

class DeclarativeSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString photoDirectory READ photoDirectory NOTIFY photoDirectoryChanged)
    Q_PROPERTY(QString videoDirectory READ videoDirectory NOTIFY videoDirectoryChanged)
    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath NOTIFY storagePathChanged)
    Q_PROPERTY(qint64 storageMaxFileSize READ storageMaxFileSize NOTIFY storageMaxFileSizeChanged)
    Q_PROPERTY(StoragePathStatus storagePathStatus READ storagePathStatus NOTIFY storagePathStatusChanged)
    Q_ENUMS(StoragePathStatus)

public:
    DeclarativeSettings(QObject *parent = nullptr);
    ~DeclarativeSettings();

    static QObject *factory(QQmlEngine *, QJSEngine *);

    QString photoDirectory() const;
    QString videoDirectory() const;

    enum StoragePathStatus {
        NotSet,
        Unavailable,
        Mounting,
        Available
    };

    QString storagePath() const;
    void setStoragePath(const QString &path);
    StoragePathStatus storagePathStatus() const;
    QString storagePathFilesystem() const;
    qint64 storageMaxFileSize() const;
    Q_INVOKABLE void refreshMaxFileSize();

    Q_INVOKABLE QString photoCapturePath(const QString &extension);
    Q_INVOKABLE QString videoCapturePath(const QString &extension);

    Q_INVOKABLE QUrl completeCapture(const QUrl &file);
    Q_INVOKABLE void completePhoto(const QUrl &file);

signals:
    void photoDirectoryChanged();
    void videoDirectoryChanged();
    void storagePathChanged();
    void storagePathStatusChanged();
    void storageMaxFileSizeChanged();

private slots:
    void verifyStoragePath();

private:
    void fixupPermissions(const QString &targetPath);
    bool verifyWritable(const QString &path);
    void verifyCapturePrefix();
    QString capturePath(const QString &format);

    PartitionManager *m_partitionManager;
    MGConfItem m_storagePath;
    MGConfItem m_minSpaceForRecording;

    QString m_prefix;
    QString m_photoDirectory;
    QString m_videoDirectory;
    QDateTime m_prefixDate;

    StoragePathStatus m_storagePathStatus;
    qint64 m_storageMaxFileSize;
};

#endif

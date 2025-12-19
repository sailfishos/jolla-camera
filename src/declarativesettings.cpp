// SPDX-FileCopyrightText: 2013 - 2024 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

#include "declarativesettings.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QStandardPaths>
#include <QLocale>
#include <QTemporaryFile>
#include <partitionmanager.h>

#include <unistd.h>
#include <sys/types.h>
#include <limits.h>

DeclarativeSettings::DeclarativeSettings(QObject *parent)
    : QObject(parent)
    , m_partitionManager(new PartitionManager(this))
    , m_storagePath(QStringLiteral("/apps/jolla-camera/storagePath"))
    , m_minSpaceForRecording(QStringLiteral("/apps/jolla-camera/minSpaceForRecording"))
    , m_storagePathStatus(NotSet)
    , m_storageMaxFileSize(0)
{
    connect(&m_storagePath, SIGNAL(valueChanged()), this, SLOT(verifyStoragePath()));
    connect(m_partitionManager, SIGNAL(partitionRemoved(const Partition&)), this, SLOT(verifyStoragePath()));
    connect(m_partitionManager, SIGNAL(partitionAdded(const Partition&)), this, SLOT(verifyStoragePath()));
    connect(m_partitionManager, SIGNAL(partitionChanged(const Partition&)), this, SLOT(verifyStoragePath()));

    verifyStoragePath();

    // protect against camera crashes leaving files in the hidden directory
    for (const QFileInfo &info : QDir(videoDirectory() + QLatin1String("/.recording")).entryInfoList(QDir::Files)) {
        QString targetPath = videoDirectory() + "/" + info.fileName();
        QFile(info.absoluteFilePath()).rename(targetPath);
        fixupPermissions(targetPath);
    }
}

DeclarativeSettings::~DeclarativeSettings()
{
}

QObject *DeclarativeSettings::factory(QQmlEngine *engine, QJSEngine *)
{
    const QUrl source = QUrl::fromLocalFile(QStringLiteral(DEPLOYMENT_PATH "settings.qml"));
    QQmlComponent component(engine, source);
    if (component.isReady()) {
        return component.create();
    } else {
        qWarning() << "Failed to instantiate Settings";
        qWarning() << component.errors();

        return nullptr;
    }
}

QString DeclarativeSettings::photoDirectory() const
{
    return m_photoDirectory;
}

QString DeclarativeSettings::videoDirectory() const
{
    return m_videoDirectory;
}

QString DeclarativeSettings::storagePath() const
{
    return m_storagePath.value().toString();
}

void DeclarativeSettings::setStoragePath(const QString &path)
{
    if (path == storagePath())
        return;

    if (path.isEmpty()) {
        m_storagePath.unset();
        m_storagePathStatus = NotSet;
    } else {
        m_storagePath.set(path);
    }

    // notifiers will be handled by the MGConfItem change signal connection
}

DeclarativeSettings::StoragePathStatus DeclarativeSettings::storagePathStatus() const
{
    return m_storagePathStatus;
}

qint64 DeclarativeSettings::storageMaxFileSize() const
{
    return m_storageMaxFileSize;
}

void DeclarativeSettings::fixupPermissions(const QString &targetPath)
{
    const QByteArray path = targetPath.toUtf8();
    if (chown(path.constData(), getuid(), getgid()) != 0)
         qWarning() << "Could not change owner/group of resulting photo capture file:" << targetPath << strerror(errno);
}

bool DeclarativeSettings::verifyWritable(const QString &path)
{
    QTemporaryFile file(path + QStringLiteral("/XXXXXX.tmp"));
    file.setAutoRemove(true);
    return file.open();
}

static qint64 getMaxBytes(Partition partition)
{
    qint64 realMaxBytes = partition.bytesAvailable();
    if (partition.filesystemType() == "vfat" && partition.bytesAvailable() > static_cast<qint64>(ULONG_MAX))
        realMaxBytes = ULONG_MAX;

    return realMaxBytes;
}

void DeclarativeSettings::verifyStoragePath()
{
    const QString prevPhotoPath = m_photoDirectory;
    const QString prevVideoPath = m_videoDirectory;

    QString path = storagePath();
    StoragePathStatus oldStatus = m_storagePathStatus;
    qint64 oldMaxBytes = m_storageMaxFileSize;

    m_storagePathStatus = path.isEmpty() ? NotSet : Unavailable;

    if (!path.isEmpty()) {
        QVector<Partition> partitions = m_partitionManager->partitions(Partition::External | Partition::ExcludeParents);
        auto it = std::find_if(partitions.begin(), partitions.end(),
                               [path](const Partition &partition) { return partition.mountPath() == path; });
        if (it != partitions.end()) {
            const Partition &partition = *it;
            if (partition.status() == Partition::Mounted) {
                if (verifyWritable(storagePath())) {
                    m_storagePathStatus = Available;
                    m_storageMaxFileSize = getMaxBytes(partition);
                } else {
                    m_storagePathStatus = Unavailable;
                    m_storageMaxFileSize = 0;
                }
            } else if(partition.status() == Partition::Mounting) {
                m_storagePathStatus = Mounting;
                m_storageMaxFileSize = 0;
            }
        }
    }

    if (m_storagePathStatus == Available && !path.isEmpty()) {
        m_photoDirectory = path + QStringLiteral("/Pictures/Camera");
        m_videoDirectory = path + QStringLiteral("/Videos/Camera");
    } else {
        m_photoDirectory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QLatin1String("/Camera");
        m_videoDirectory = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QLatin1String("/Camera");
        QVector<Partition> partitions = m_partitionManager->partitions(Partition::User | Partition::Mass);
        auto it = std::find_if(partitions.begin(), partitions.end(), [path](const Partition &partition) {
            return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation).startsWith(partition.mountPath()); });
        if (it != partitions.end()) {
            const Partition &partition = *it;
            m_storageMaxFileSize = qMax((qint64)0,
                                        getMaxBytes(partition) - (m_minSpaceForRecording.value(100).toLongLong() << 20));
        } else {
            m_storageMaxFileSize = 0;
        }
    }

    QDir(m_photoDirectory).mkpath(QLatin1String("."));
    QDir(m_videoDirectory).mkpath(QLatin1String(".recording"));

    if (prevPhotoPath != m_photoDirectory) {
        emit photoDirectoryChanged();
    }
    if (prevVideoPath != m_videoDirectory) {
        emit videoDirectoryChanged();
    }

    if (oldStatus != m_storagePathStatus) {
        emit storagePathStatusChanged();
    }

    if (oldMaxBytes != m_storageMaxFileSize) {
        emit storageMaxFileSizeChanged();
    }
}

QString DeclarativeSettings::photoCapturePath(const QString &extension)
{
    verifyCapturePrefix();

    QString fileFormat(photoDirectory() + QLatin1Char('/') + m_prefix + QLatin1String("%1.") + extension);
    return capturePath(fileFormat);
}

QString DeclarativeSettings::videoCapturePath(const QString &extension)
{
    verifyCapturePrefix();

    QString fileFormat(videoDirectory() + QLatin1String("/.recording/") + m_prefix + QLatin1String("%1.") + extension);
    return capturePath(fileFormat);
}

void DeclarativeSettings::completePhoto(const QUrl &file)
{
    fixupPermissions(file.path());
}

void DeclarativeSettings::refreshMaxFileSize()
{
    m_partitionManager->refresh();
}

QUrl DeclarativeSettings::completeCapture(const QUrl &file)
{
    const QString recordingDir = QStringLiteral("/.recording/");
    const QString absolutePath = file.toLocalFile();
    const int index = absolutePath.lastIndexOf(recordingDir) + 1;
    if (index == -1) {
        return file;
    }

    QString targetPath = absolutePath;
    targetPath.remove(index, recordingDir.length() - 1);

    if (QFile::rename(absolutePath, targetPath)) {
        fixupPermissions(targetPath);
        refreshMaxFileSize();
        return QUrl::fromLocalFile(targetPath);
    } else {
        QFile::remove(absolutePath);
        return QUrl();
    }
}

void DeclarativeSettings::verifyCapturePrefix()
{
    const QDateTime currentDate = QDateTime::currentDateTime();
    if (m_prefixDate != currentDate) {
        m_prefixDate = currentDate;
        m_prefix = QLocale::c().toString(currentDate, QLatin1String("yyyyMMdd_HHmmss"));
    }
}

QString DeclarativeSettings::capturePath(const QString &format)
{
    QString path = format.arg(QString(""));
    if (!QFile::exists(path)) {
        return path;
    }

    int counter = 1;
    for (;;) {
        path = format.arg(QString(QStringLiteral("_%1")).arg(counter, 3, 10, QLatin1Char('0')));
        if (!QFile::exists(path))
            return path;
        ++counter;
    }
}

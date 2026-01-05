/*
 * SPDX-FileCopyrightText: 2013 - 2024 Jolla Ltd.
 * SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CAPTUREMODEL_H
#define CAPTUREMODEL_H

#include <QAbstractItemModel>
#include <QMimeDatabase>
#include <QMutex>
#include <QQmlParserStatus>
#include <QSocketNotifier>
#include <QUrl>
#include <QWaitCondition>

#include <sys/inotify.h>

class CaptureModel : public QAbstractListModel, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(bool populated READ isPopulated NOTIFY populatedChanged)
    Q_PROPERTY(QStringList directories READ directories WRITE setDirectories NOTIFY directoriesChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum {
        Url,
        MimeType
    };

    CaptureModel(QObject *parent = nullptr);
    ~CaptureModel() override;

    bool isPopulated() const;

    QStringList directories() const;
    void setDirectories(const QStringList &directories);

    Q_INVOKABLE void appendCapture(const QUrl &url, const QString &mimeType);
    Q_INVOKABLE void deleteFile(int index);

    QHash<int, QByteArray> roleNames() const override;
    QModelIndex index(int row, int column, const QModelIndex &parent) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    bool event(QEvent *event) override;

    void classBegin() override;
    void componentComplete() override;

signals:
    void populatedChanged();
    void directoriesChanged();
    void countChanged();

private:
    struct Capture
    {
        bool operator ==(const Capture &other) const {
            return directory == other.directory && fileName == other.fileName;
        }
        bool operator !=(const Capture &other) const {
            return directory != other.directory || fileName != other.fileName;
        }

        QString filePath() const;

        QByteArray directory;
        QByteArray fileName;
        QString mimeType;

    };

    struct WatchedDirectory
    {
        QByteArray path;
        qint64 id;
    };

    inline int count() const;
    inline const Capture &captureAt(int index) const;

    inline void updateWatchedDirectories();
    inline void scanFiles(
            const QVector<Capture> &originalCaptures,
            const QVector<QByteArray> &addDirectories,
            const QVector<QByteArray> &removeDirectories);
    inline void diffFiles(
            const QVector<Capture> &captures, const QVector<Capture> &expired, bool commonFiles);
    inline void filesChanged();

    inline void insertCapture(
            const WatchedDirectory &directory, const QByteArray &fileName, const QString &mimeType);
    inline static bool compare(const Capture &left, const Capture &right);
    inline bool isCameraFile(const QByteArray &fileName) const;

    template <class Function> inline void post(const Function &function);

    QSocketNotifier m_notifier { inotify_init(), QSocketNotifier::Read };
    QVector<Capture> m_captures;
    QVector<Capture> m_expiredCaptures;
    QVector<WatchedDirectory> m_watchedDirectories;
    QStringList m_directories;
    QWaitCondition m_exitCondition;
    QMutex m_exitMutex;
    QMimeDatabase m_mimeDatabase;
    const QUrl m_fileUrl = QUrl::fromLocalFile(QLatin1String("/"));
    const int m_inotifyFd = m_notifier.socket();
    int m_maximumCaptureIndex = 0;
    int m_minimumExpiredIndex = 0;
    bool m_complete = true;
    bool m_scanning = false;
    bool m_populated = false;
};

#endif

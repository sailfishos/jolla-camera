// SPDX-FileCopyrightText: 2013 - 2014 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

#include "declarativecameraextensions.h"
#include "dnglensshading.h"
#include "imageadjustments.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlInfo>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QTransform>
#include <QUrl>
#include <QVector>

#include <QtDebug>
#include <QtConcurrent/QtConcurrentRun>

#include <QQuickWindow>
#include <qpa/qplatformnativeinterface.h>

#include <tiffio.h>

#ifndef TIFFTAG_NOISEPROFILE
#define TIFFTAG_NOISEPROFILE 51041
#endif

#ifndef TIFFTAG_OPCODELIST2
#define TIFFTAG_OPCODELIST2 51009
#endif

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdint.h>
#include <utime.h>

namespace {
// TIFFTAG_NOISEPROFILE and TIFFTAG_OPCODELIST2 are DNG-private tags that this
// build of libtiff does not register internally (unlike e.g. COLORMATRIX1 or
// ASSHOTNEUTRAL, which libtiff already knows about). Without registering
// them first, TIFFSetField() silently fails ("Unknown tag") and the tag is
// never written -- confirmed both by TIFFSetField's return value and by
// inspecting the resulting file's IFD directly.
const TIFFFieldInfo dngPrivateFields[] = {
    { TIFFTAG_NOISEPROFILE, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_DOUBLE, FIELD_CUSTOM, 1, 1, const_cast<char *>("DNGNoiseProfile") },
    { TIFFTAG_OPCODELIST2, TIFF_VARIABLE2, TIFF_VARIABLE2, TIFF_UNDEFINED, FIELD_CUSTOM, 1, 1, const_cast<char *>("DNGOpcodeList2") },
};
}

DeclarativeCameraExtensions::DeclarativeCameraExtensions(QObject *parent)
    : QObject(parent)
{
}

DeclarativeCameraExtensions::~DeclarativeCameraExtensions()
{
    if (m_rawRenderWatcher) {
        m_rawRenderWatcher->disconnect(this);
        if (m_rawRenderWatcher->isRunning()) {
            m_rawRenderWatcher->waitForFinished();
        }
    }
    if (m_rawCaptureProcess) {
        m_rawCaptureProcess->disconnect(this);
        if (m_rawCaptureProcess->state() != QProcess::NotRunning) {
            m_rawCaptureProcess->terminate();
            if (!m_rawCaptureProcess->waitForFinished(1000)) {
                m_rawCaptureProcess->kill();
                m_rawCaptureProcess->waitForFinished(1000);
            }
        }
    }
}

static QString rawCaptureProbePath()
{
    const QString override = QString::fromLocal8Bit(qgetenv("SFOS_CAMERA2_PROBE"));
    return override.isEmpty()
            ? QStringLiteral("/usr/libexec/rawfish/sfos-camera2-probe")
            : override;
}

static QString rawFastJpegConverterPath()
{
    const QString override = QString::fromLocal8Bit(qgetenv("SFOS_RAW16_JPEG_CONVERTER"));
    return override.isEmpty()
            ? QStringLiteral("/usr/libexec/rawfish/sfos-raw16-to-jpeg")
            : override;
}

static bool rawRenderEngineSupported(const QString &engine)
{
    return engine == QLatin1String("internal")
            || engine == QLatin1String("fastjpeg");
}

static bool executableExists(const QString &path)
{
    const QFileInfo file(path);
    return file.exists() && file.isExecutable();
}

static bool readableImageExists(const QString &path, QString *error)
{
    const QFileInfo file(path);
    if (!file.isFile() || file.size() <= 0) {
        *error = QStringLiteral("RAW JPEG output is missing or empty: %1").arg(path);
        return false;
    }

    QImageReader reader(path);
    if (!reader.canRead() || !reader.size().isValid()) {
        *error = QStringLiteral("RAW JPEG output is not readable: %1 size=%2 error=%3")
                .arg(path)
                .arg(file.size())
                .arg(reader.errorString());
        return false;
    }
    return true;
}

static QString jsonSidecarPath(const QString &targetPath)
{
    const QFileInfo targetInfo(targetPath);
    return targetInfo.absolutePath() + QLatin1Char('/')
            + targetInfo.completeBaseName() + QLatin1String(".json");
}

static bool writeMetadataWithRawPath(const QString &sourcePath,
                                     const QString &destinationPath,
                                     const QString &rawPath,
                                     QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read metadata: %1").arg(sourcePath);
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument document = QJsonDocument::fromJson(source.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("RAW metadata is not valid JSON");
        return false;
    }

    QJsonObject object = document.object();
    object.insert(QStringLiteral("raw_path"), rawPath);
    document.setObject(object);

    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    QFile::remove(destinationPath);
    QFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = QStringLiteral("Cannot write metadata: %1").arg(destinationPath);
        return false;
    }
    destination.write(document.toJson(QJsonDocument::Indented));
    return true;
}

static void appendRawCaptureLog(const QString &message)
{
    QString picturesPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (picturesPath.isEmpty()) {
        picturesPath = QDir::homePath() + QLatin1String("/Pictures");
    }
    const QString logDirectory = picturesPath + QLatin1String("/RAWfish");
    QDir().mkpath(logDirectory);
    QFile file(logDirectory + QLatin1String("/rawfish-capture.log"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    file.write(QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8());
    file.write(" ");
    file.write(message.toUtf8());
    file.write("\n");
}

static QString rawPathFromMetadata(const QString &metadataPath, QString *error)
{
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read metadata: %1").arg(metadataPath);
        return QString();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = QStringLiteral("RAW metadata is not valid JSON: %1").arg(metadataPath);
        return QString();
    }

    const QString rawPath = document.object().value(QStringLiteral("raw_path")).toString();
    if (rawPath.isEmpty()) {
        *error = QStringLiteral("RAW metadata has no raw_path: %1").arg(metadataPath);
    }
    return rawPath;
}

bool DeclarativeCameraExtensions::rawImageCaptureAvailable() const
{
    return executableExists(rawCaptureProbePath());
}

void DeclarativeCameraExtensions::disableNotifications(QQuickItem *item, bool disable)
{
    if (QWindow *window = item ? item->window() : 0) {
        QGuiApplication::platformNativeInterface()->setWindowProperty(
                    window->handle(), QLatin1String("NOTIFICATION_PREVIEWS_DISABLED"),
                    QVariant(disable ? 3 : 0));
    }
}

bool DeclarativeCameraExtensions::captureRawImage(const QString &targetPath, const QString &cameraId,
                                                  const QString &rawSize, int timeoutSeconds,
                                                  const QString &focusMode, const QString &focusDistance,
                                     int focusTimeoutSeconds, const QString &focusFailure,
                                     const QString &exposure, int jpegQuality,
                                     int rotationDegrees, const QString &rawSaveFormat,
                                     const QString &rawRenderEngine,
                                     const QString &sceneMode, int colorTemperature,
                                     int colorTint, bool progressiveJpeg,
                                     int sensorSensitivity,
                                     const QString &exposureTime,
                                     int aperture,
                                     int noiseReduction,
                                     qreal zoom)
{
    if (m_rawCaptureProcess || m_rawRenderWatcher) {
        emit rawImageCaptureFailed(QStringLiteral("RAW image capture is already running"));
        return false;
    }

    QString localTargetPath = targetPath;
    const QUrl targetUrl(targetPath);
    if (targetUrl.isLocalFile()) {
        localTargetPath = targetUrl.toLocalFile();
    }
    if (localTargetPath.isEmpty()) {
        emit rawImageCaptureFailed(QStringLiteral("RAW image capture target is empty"));
        return false;
    }
    if (!rawImageCaptureAvailable()) {
        emit rawImageCaptureFailed(QStringLiteral("RAW image capture helper is not installed"));
        return false;
    }

    m_rawCaptureDirectory.reset(new QTemporaryDir(
                QDir::tempPath() + QLatin1String("/rawfish-raw-XXXXXX")));
    if (!m_rawCaptureDirectory->isValid()) {
        clearRawImageCapture();
        emit rawImageCaptureFailed(QStringLiteral("Could not create temporary RAW capture directory"));
        return false;
    }

    m_rawCaptureTargetPath = localTargetPath;
    m_rawCapturePrefix = m_rawCaptureDirectory->path() + QLatin1String("/capture");
    const QFileInfo targetInfo(localTargetPath);
    m_rawCaptureArchivePrefix = targetInfo.absolutePath()
            + QLatin1Char('/') + targetInfo.completeBaseName();
    m_rawCaptureErrors.clear();
    m_rawCaptureStandardOutput.clear();
    m_rawCaptureExposure = exposure.isEmpty() ? QStringLiteral("1.0") : exposure;
    m_rawCaptureSaveFormat = rawSaveFormat.isEmpty()
            ? QStringLiteral("none") : rawSaveFormat;
    m_rawRenderEngine = rawRenderEngineSupported(rawRenderEngine)
            ? rawRenderEngine : QStringLiteral("internal");
    m_rawCaptureProgressiveJpeg = progressiveJpeg;
    m_rawCaptureJpegQuality = qBound(1, jpegQuality, 100);
    m_rawCaptureRotationDegrees = ((rotationDegrees % 360) + 360) % 360;
    m_rawCaptureColorTemperature = qBound(0, colorTemperature, 50000);
    m_rawCaptureColorTint = qBound(-1000, colorTint, 1000);
    m_rawCaptureTimer.restart();
    qDebug() << "capture-timing app raw request"
             << "t" << m_rawCaptureTimer.elapsed()
             << "size" << rawSize
             << "focus" << focusMode
             << "save" << rawSaveFormat;

    QStringList arguments;
    arguments << QStringLiteral("--capture")
              << QStringLiteral("--camera") << (cameraId.isEmpty() ? QStringLiteral("0") : cameraId)
              << QStringLiteral("--size") << (rawSize.isEmpty() ? QStringLiteral("4096x3072") : rawSize)
              << QStringLiteral("--timeout") << QString::number(qBound(1, timeoutSeconds, 3600))
              << QStringLiteral("--focus") << (focusMode.isEmpty() ? QStringLiteral("auto") : focusMode)
              << QStringLiteral("--focus-distance") << (focusDistance.isEmpty() ? QStringLiteral("0") : focusDistance)
              << QStringLiteral("--focus-timeout") << QString::number(qBound(1, focusTimeoutSeconds, 3600))
              << QStringLiteral("--focus-failure") << (focusFailure.isEmpty() ? QStringLiteral("capture") : focusFailure);

    const QString effectiveSceneMode = sceneMode.isEmpty()
            ? QStringLiteral("manual") : sceneMode;
    if (effectiveSceneMode != QLatin1String("none") &&
            effectiveSceneMode != QLatin1String("manual")) {
        arguments << QStringLiteral("--scene") << effectiveSceneMode;
    }
    if (colorTemperature > 0) {
        arguments << QStringLiteral("--color-temperature") << QString::number(qBound(0, colorTemperature, 50000));
    }
    if (colorTint != 0) {
        arguments << QStringLiteral("--color-tint") << QString::number(qBound(-1000, colorTint, 1000));
    }
    if (sensorSensitivity > 0) {
        arguments << QStringLiteral("--iso")
                  << QString::number(qBound(0, sensorSensitivity, 102400));
    }
    if (!exposureTime.isEmpty() && exposureTime != QLatin1String("0")) {
        arguments << QStringLiteral("--shutter-ns") << exposureTime;
    }
    if (aperture > 0) {
        arguments << QStringLiteral("--aperture")
                  << QString::number(qBound(0, aperture, 255));
    }
    if (noiseReduction > 0) {
        arguments << QStringLiteral("--noise-reduction")
                  << QString::number(noiseReduction);
    }
    arguments << QStringLiteral("--zoom") << QString::number(qMax<qreal>(1.0, zoom), 'f', 4);

    arguments << QStringLiteral("--output") << m_rawCapturePrefix
              << QStringLiteral("--force");

    m_rawCaptureStage = RawCaptureCapturing;
    return startRawImageProcess(rawCaptureProbePath(), arguments,
                                QStringLiteral("Camera2 probe"));
}

bool DeclarativeCameraExtensions::captureJpegImage(const QString &targetPath, const QString &cameraId,
                                                   const QString &jpegSize, int timeoutSeconds,
                                                   int jpegQuality, int rotationDegrees,
                                                   const QString &exposure,
                                                   const QString &sceneMode,
                                                   int sensorSensitivity,
                                                   const QString &exposureTime,
                                                   int aperture,
                                                   int noiseReduction,
                                                   qreal zoom)
{
    if (m_rawCaptureProcess || m_rawRenderWatcher) {
        emit rawImageCaptureFailed(QStringLiteral("Camera2 image capture is already running"));
        return false;
    }

    QString localTargetPath = targetPath;
    const QUrl targetUrl(targetPath);
    if (targetUrl.isLocalFile()) {
        localTargetPath = targetUrl.toLocalFile();
    }
    if (localTargetPath.isEmpty()) {
        emit rawImageCaptureFailed(QStringLiteral("Camera2 image capture target is empty"));
        return false;
    }
    if (!executableExists(rawCaptureProbePath())) {
        emit rawImageCaptureFailed(QStringLiteral("Camera2 capture helper is not installed"));
        return false;
    }

    const QFileInfo targetInfo(localTargetPath);
    QDir().mkpath(targetInfo.absolutePath());
    QFile::remove(localTargetPath);

    m_rawCaptureTargetPath = localTargetPath;
    m_rawCaptureErrors.clear();
    m_rawCaptureStandardOutput.clear();
    m_rawCaptureStage = RawCaptureJpegCapturing;
    m_rawCaptureExposure = exposure.isEmpty() ? QStringLiteral("1.0") : exposure;
    m_rawCaptureJpegQuality = qBound(1, jpegQuality, 100);
    m_rawCaptureColorTemperature = 0;
    m_rawCaptureColorTint = 0;
    m_rawCaptureTimer.restart();
    qDebug() << "capture-timing app jpeg request"
             << "t" << m_rawCaptureTimer.elapsed()
             << "size" << jpegSize;
    QStringList arguments;
    arguments << QStringLiteral("--capture-jpeg")
              << QStringLiteral("--camera") << (cameraId.isEmpty() ? QStringLiteral("0") : cameraId)
              << QStringLiteral("--size") << (jpegSize.isEmpty() ? QStringLiteral("4096x3072") : jpegSize)
              << QStringLiteral("--timeout") << QString::number(qBound(1, timeoutSeconds, 3600))
              << QStringLiteral("--quality") << QString::number(m_rawCaptureJpegQuality)
              << QStringLiteral("--orientation") << QString::number(((rotationDegrees % 360) + 360) % 360)
              << QStringLiteral("--zoom") << QString::number(qMax<qreal>(1.0, zoom), 'f', 4)
              << QStringLiteral("--output") << localTargetPath
              << QStringLiteral("--force");
    const QString effectiveSceneMode = sceneMode.isEmpty()
            ? QStringLiteral("manual") : sceneMode;
    if (effectiveSceneMode != QLatin1String("none") &&
            effectiveSceneMode != QLatin1String("manual")) {
        arguments << QStringLiteral("--scene") << effectiveSceneMode;
    }
    if (sensorSensitivity > 0) {
        arguments << QStringLiteral("--iso")
                  << QString::number(qBound(0, sensorSensitivity, 102400));
    }
    if (!exposureTime.isEmpty() && exposureTime != QLatin1String("0")) {
        arguments << QStringLiteral("--shutter-ns") << exposureTime;
    }
    if (aperture > 0) {
        arguments << QStringLiteral("--aperture")
                  << QString::number(qBound(0, aperture, 255));
    }
    if (noiseReduction > 0) {
        arguments << QStringLiteral("--noise-reduction")
                  << QString::number(noiseReduction);
    }

    return startRawImageProcess(rawCaptureProbePath(), arguments,
                                QStringLiteral("Camera2 JPEG probe"));
}

bool DeclarativeCameraExtensions::processRawImage(
        const QString &targetPath, const QString &rawPath,
        const QString &metadataPath, const QString &exposure, int jpegQuality,
        int rotationDegrees, const QString &rawSaveFormat,
        const QString &rawRenderEngine,
        int colorTemperature, int colorTint, bool progressiveJpeg)
{
    if (m_rawCaptureProcess || m_rawRenderWatcher) {
        emit rawImageCaptureFailed(QStringLiteral("Camera2 image capture is already running"));
        return false;
    }

    QString localTargetPath = targetPath;
    const QUrl targetUrl(targetPath);
    if (targetUrl.isLocalFile()) {
        localTargetPath = targetUrl.toLocalFile();
    }
    if (localTargetPath.isEmpty() || rawPath.isEmpty() ||
            metadataPath.isEmpty()) {
        emit rawImageCaptureFailed(QStringLiteral("RAW image capture target is empty"));
        return false;
    }
    if (!QFileInfo(rawPath).isFile() || !QFileInfo(metadataPath).isFile()) {
        emit rawImageCaptureFailed(QStringLiteral("RAW image capture did not produce usable output"));
        return false;
    }

    const QFileInfo rawInfo(rawPath);
    const QFileInfo targetInfo(localTargetPath);
    m_rawCaptureTargetPath = localTargetPath;
    m_rawCapturePrefix = rawInfo.absolutePath() + QLatin1Char('/')
            + rawInfo.completeBaseName();
    m_rawCaptureArchivePrefix = targetInfo.absolutePath()
            + QLatin1Char('/') + targetInfo.completeBaseName();
    m_rawCaptureErrors.clear();
    m_rawCaptureStandardOutput.clear();
    m_rawCaptureExposure = exposure.isEmpty() ? QStringLiteral("1.0") : exposure;
    m_rawCaptureSaveFormat = rawSaveFormat.isEmpty()
            ? QStringLiteral("none") : rawSaveFormat;
    m_rawRenderEngine = rawRenderEngineSupported(rawRenderEngine)
            ? rawRenderEngine : QStringLiteral("internal");
    m_rawCaptureProgressiveJpeg = progressiveJpeg;
    m_rawCaptureJpegQuality = qBound(1, jpegQuality, 100);
    m_rawCaptureRotationDegrees = ((rotationDegrees % 360) + 360) % 360;
    m_rawCaptureColorTemperature = qBound(0, colorTemperature, 50000);
    m_rawCaptureColorTint = qBound(-1000, colorTint, 1000);
    m_rawCaptureTimer.restart();

    QString metadataError;
    if (!writeMetadataWithRawPath(metadataPath,
                                  m_rawCapturePrefix + QLatin1String(".json"),
                                  rawPath,
                                  &metadataError)) {
        emit rawImageCaptureFailed(metadataError);
        return false;
    }

    preserveRawCaptureFiles();
    if (!(m_rawRenderEngine == QLatin1String("fastjpeg")
            ? renderRawImageWithFastJpegConverter() : renderRawImage())) {
        const QString error = m_rawCaptureErrors.isEmpty()
                ? QStringLiteral("RAW image conversion failed")
                : m_rawCaptureErrors;
        clearRawImageCapture();
        emit rawImageCaptureFailed(error);
        return false;
    }
    QString imageError;
    if (!readableImageExists(localTargetPath, &imageError)) {
        clearRawImageCapture();
        emit rawImageCaptureFailed(imageError);
        return false;
    }
    if (!copyJsonSidecar(m_rawCapturePrefix + QLatin1String(".json"),
                         localTargetPath)) {
        const QString error = m_rawCaptureErrors.isEmpty()
                ? QStringLiteral("Could not save RAW metadata sidecar")
                : m_rawCaptureErrors;
        clearRawImageCapture();
        emit rawImageCaptureFailed(error);
        return false;
    }

    clearRawImageCapture();
    emit rawImageCaptured(localTargetPath, QStringLiteral("image/jpeg"));
    return true;
}

void DeclarativeCameraExtensions::finishRawImageCapture(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_rawCaptureProcess ||
            (!m_rawCaptureDirectory && m_rawCaptureStage != RawCaptureJpegCapturing)) {
        clearRawImageCapture();
        emit rawImageCaptureFailed(QStringLiteral("Camera2 image capture finished without capture state"));
        return;
    }

    const QString errorOutput = QString::fromLocal8Bit(
                m_rawCaptureProcess->readAllStandardError()).trimmed();
    m_rawCaptureStandardOutput = m_rawCaptureProcess->readAllStandardOutput();
    if (!errorOutput.isEmpty()) {
        if (!m_rawCaptureErrors.isEmpty()) {
            m_rawCaptureErrors.append(QLatin1Char('\n'));
        }
        m_rawCaptureErrors.append(errorOutput);
    }

    QScopedPointer<QProcess> finishedProcess(m_rawCaptureProcess.take());
    qDebug() << "capture-timing app helper finished"
             << "t" << m_rawCaptureTimer.elapsed()
             << "stage" << m_rawCaptureStage
             << "exit" << exitCode;

    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString error = m_rawCaptureErrors.isEmpty()
                ? QStringLiteral("Camera2 image capture failed")
                : m_rawCaptureErrors;
        clearRawImageCapture();
        emit rawImageCaptureFailed(error);
        return;
    }

    if (m_rawCaptureStage == RawCaptureJpegCapturing) {
        const QString targetPath = m_rawCaptureTargetPath;
        if (!QFileInfo(targetPath).isFile()) {
            const QString error = m_rawCaptureErrors.isEmpty()
                    ? QStringLiteral("Camera2 JPEG capture did not produce usable output")
                    : m_rawCaptureErrors;
            clearRawImageCapture();
            emit rawImageCaptureFailed(error);
            return;
        }
        if (!saveJsonSidecar(targetPath, m_rawCaptureStandardOutput)) {
            const QString error = m_rawCaptureErrors.isEmpty()
                    ? QStringLiteral("Could not save Camera2 JPEG metadata")
                    : m_rawCaptureErrors;
            clearRawImageCapture();
            emit rawImageCaptureFailed(error);
            return;
        }
        bool exposureOk = false;
        qreal exposure = m_rawCaptureExposure.toDouble(&exposureOk);
        if (!exposureOk) {
            exposure = 1.0;
        }
        if (ImageAdjustments::requested(exposure, m_rawCaptureColorTemperature,
                                        m_rawCaptureColorTint)) {
            QImage image(targetPath);
            if (image.isNull()) {
                clearRawImageCapture();
                emit rawImageCaptureFailed(QStringLiteral(
                    "Could not load Camera2 JPEG for adjustment"));
                return;
            }
            ImageAdjustments::apply(&image, exposure, m_rawCaptureColorTemperature,
                                    m_rawCaptureColorTint);
            QImageWriter writer(targetPath, "JPG");
            writer.setQuality(m_rawCaptureJpegQuality);
            if (!writer.write(image)) {
                clearRawImageCapture();
                emit rawImageCaptureFailed(QStringLiteral(
                    "Could not save adjusted Camera2 JPEG"));
                return;
            }
        }
        qDebug() << "capture-timing app jpeg complete"
                 << "t" << m_rawCaptureTimer.elapsed();
        clearRawImageCapture();
        emit rawImageCaptured(targetPath, QStringLiteral("image/jpeg"));
        return;
    }

    if (m_rawCaptureStage == RawCaptureCapturing) {
        if (!QFileInfo(m_rawCapturePrefix + QLatin1String(".raw16")).isFile()
                || !QFileInfo(m_rawCapturePrefix + QLatin1String(".json")).isFile()) {
            const QString error = m_rawCaptureErrors.isEmpty()
                    ? QStringLiteral("RAW image capture did not produce usable output")
                    : m_rawCaptureErrors;
            clearRawImageCapture();
            emit rawImageCaptureFailed(error);
            return;
        }
        const qint64 preserveStart = m_rawCaptureTimer.elapsed();
        preserveRawCaptureFiles();
        qDebug() << "capture-timing app raw preserve"
                 << "start" << preserveStart
                 << "end" << m_rawCaptureTimer.elapsed();
        m_rawRenderStart = m_rawCaptureTimer.elapsed();
        m_rawRenderWatcher.reset(new QFutureWatcher<bool>);
        connect(m_rawRenderWatcher.data(), &QFutureWatcher<bool>::finished,
                this, &DeclarativeCameraExtensions::finishRawImageRender);
        m_rawRenderWatcher->setFuture(QtConcurrent::run([this]() {
            return m_rawRenderEngine == QLatin1String("fastjpeg")
                    ? renderRawImageWithFastJpegConverter() : renderRawImage();
        }));
        return;
    }

    clearRawImageCapture();
    emit rawImageCaptureFailed(QStringLiteral("RAW image capture finished in an unknown state"));
}

void DeclarativeCameraExtensions::finishRawImageRender()
{
    QFutureWatcher<bool> *watcher = m_rawRenderWatcher.take();
    const bool renderOk = watcher && watcher->result();
    if (watcher) {
        watcher->deleteLater();
    }
    if (!renderOk) {
        const QString error = m_rawCaptureErrors.isEmpty()
                ? QStringLiteral("RAW image conversion failed")
                : m_rawCaptureErrors;
        clearRawImageCapture();
        emit rawImageCaptureFailed(error);
        return;
    }

    qDebug() << "capture-timing app raw render"
             << "start" << m_rawRenderStart
             << "end" << m_rawCaptureTimer.elapsed();
    const QString targetPath = m_rawCaptureTargetPath;
    QString imageError;
    if (!readableImageExists(targetPath, &imageError)) {
        clearRawImageCapture();
        emit rawImageCaptureFailed(imageError);
        return;
    }
    if (!copyJsonSidecar(m_rawCapturePrefix + QLatin1String(".json"),
                         targetPath)) {
        const QString error = m_rawCaptureErrors.isEmpty()
                ? QStringLiteral("Could not save RAW metadata sidecar")
                : m_rawCaptureErrors;
        clearRawImageCapture();
        emit rawImageCaptureFailed(error);
        return;
    }
    qDebug() << "capture-timing app raw complete"
             << "t" << m_rawCaptureTimer.elapsed();
    clearRawImageCapture();
    emit rawImageCaptured(targetPath, QStringLiteral("image/jpeg"));
}

bool DeclarativeCameraExtensions::startRawImageProcess(const QString &program,
                                                       const QStringList &arguments,
                                                       const QString &errorContext,
                                                       const QString &standardOutputPath)
{
    m_rawCaptureProcess.reset(new QProcess);
    m_rawCaptureProcess->setProgram(program);
    m_rawCaptureProcess->setArguments(arguments);
    if (!standardOutputPath.isEmpty()) {
        m_rawCaptureProcess->setStandardOutputFile(standardOutputPath);
    }
    connect(m_rawCaptureProcess.data(),
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            &DeclarativeCameraExtensions::finishRawImageCapture);
    m_rawCaptureProcess->start();
    if (m_rawCaptureProcess->waitForStarted(1000)) {
        qDebug() << "capture-timing app helper started"
                 << "t" << m_rawCaptureTimer.elapsed()
                 << "program" << program;
        return true;
    }

    m_rawCaptureErrors = QStringLiteral("%1 could not start %2: %3")
            .arg(errorContext, program, m_rawCaptureProcess->errorString());
    m_rawCaptureProcess.reset();
    return false;
}

namespace {

enum PixelColor {
    PixelRed,
    PixelGreen,
    PixelBlue
};

struct RawRenderConfig {
    int width = 0;
    int height = 0;
    int rowStride = 0;
    int whiteLevel = 0;
    int iso = 0;
    int blackLevel[4] = {0, 0, 0, 0};
    float gains[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    float matrix[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    double noiseProfile[8] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    int noiseProfileCount = 0;
    float exposure = 1.0f;
    int colorTemperature = 0;
    int cfaMap = 3;
    QString cfa;
    QString rawPath;
};

float jsonRationalFloat(const QJsonValue &value, float fallback = 0.0f)
{
    const QJsonArray rational = value.toArray();
    if (rational.size() < 2) {
        return fallback;
    }
    const double denominator = rational.at(1).toDouble(1.0);
    return denominator == 0.0
            ? fallback
            : float(rational.at(0).toDouble() / denominator);
}

bool readRationalFloats(const QJsonArray &array, float *values, int count)
{
    if (array.size() < count) {
        return false;
    }
    for (int index = 0; index < count; ++index) {
        values[index] = jsonRationalFloat(array.at(index));
    }
    return true;
}

QByteArray cfaPatternBytes(const QString &cfa)
{
    if (cfa == QLatin1String("RGGB")) {
        return QByteArray::fromRawData("\000\001\001\002", 4);
    } else if (cfa == QLatin1String("GRBG")) {
        return QByteArray::fromRawData("\001\000\002\001", 4);
    } else if (cfa == QLatin1String("GBRG")) {
        return QByteArray::fromRawData("\001\002\000\001", 4);
    }
    return QByteArray::fromRawData("\002\001\001\000", 4);
}

void writeExifDirectory(TIFF *tiff, const QJsonObject &metadata)
{
    if (!TIFFWriteDirectory(tiff)) {
        return;
    }

    TIFFCreateEXIFDirectory(tiff);

    const QByteArray dateTime = QDateTime::currentDateTime()
            .toString(QStringLiteral("yyyy:MM:dd hh:mm:ss")).toLatin1();
    TIFFSetField(tiff, EXIFTAG_DATETIMEORIGINAL, dateTime.constData());

    const qint64 exposureTimeNs =
            qint64(metadata.value(QStringLiteral("exposure_time_ns")).toDouble());
    if (exposureTimeNs > 0) {
        const float exposureSeconds = float(double(exposureTimeNs) / 1000000000.0);
        TIFFSetField(tiff, EXIFTAG_EXPOSURETIME, exposureSeconds);
    }

    const int iso = metadata.value(QStringLiteral("iso")).toInt();
    if (iso > 0) {
        const uint16_t isoValue = uint16_t(qMin(iso, 65535));
        TIFFSetField(tiff, EXIFTAG_ISOSPEEDRATINGS, 1, &isoValue);
    }

    float aperture = float(metadata.value(QStringLiteral("lens_aperture")).toDouble());
    if (aperture <= 0.0f) {
        aperture = float(metadata.value(QStringLiteral("aperture_requested")).toInt()) / 10.0f;
    }
    if (aperture > 0.0f) {
        TIFFSetField(tiff, EXIFTAG_FNUMBER, aperture);
    }

    const float focalLength =
            float(metadata.value(QStringLiteral("focal_length_mm")).toDouble());
    if (focalLength > 0.0f) {
        TIFFSetField(tiff, EXIFTAG_FOCALLENGTH, focalLength);
    }

    uint64_t exifOffset = 0;
    if (!TIFFWriteCustomDirectory(tiff, &exifOffset) || exifOffset == 0) {
        return;
    }
    if (TIFFSetDirectory(tiff, 0)) {
        TIFFSetField(tiff, TIFFTAG_EXIFIFD, exifOffset);
        TIFFRewriteDirectory(tiff);
    }
}

bool writeTiffDng(const QString &metadataPath, const QString &dngPath,
                  QString *error)
{
    QFile metadataFile(metadataPath);
    if (!metadataFile.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read RAW metadata: %1").arg(metadataPath);
        return false;
    }
    const QJsonObject metadata =
            QJsonDocument::fromJson(metadataFile.readAll()).object();
    const int width = metadata.value(QStringLiteral("width")).toInt();
    const int height = metadata.value(QStringLiteral("height")).toInt();
    const int rowStride = metadata.value(QStringLiteral("row_stride")).toInt();
    const int whiteLevel = metadata.value(QStringLiteral("white_level")).toInt();
    const QString rawPath = metadata.value(QStringLiteral("raw_path")).toString();
    const QString cfa = metadata.value(QStringLiteral("cfa")).toString();
    if (width <= 0 || height <= 0 || rowStride < width * 2 ||
            whiteLevel <= 0 || rawPath.isEmpty()) {
        *error = QStringLiteral("RAW metadata is incomplete for DNG");
        return false;
    }

    QFile raw(rawPath);
    if (!raw.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read RAW16 file: %1").arg(rawPath);
        return false;
    }

    QDir().mkpath(QFileInfo(dngPath).absolutePath());
    QFile::remove(dngPath);

    const QByteArray dngPathBytes = dngPath.toLocal8Bit();
    TIFF *tiff = TIFFOpen(dngPathBytes.constData(), "w");
    if (!tiff) {
        *error = QStringLiteral("Cannot create DNG: %1").arg(dngPath);
        return false;
    }

    // Register DNG-private tags this libtiff build doesn't know about by
    // default; see the comment on dngPrivateFields above.
    TIFFMergeFieldInfo(tiff, dngPrivateFields,
                        sizeof(dngPrivateFields) / sizeof(dngPrivateFields[0]));

    const QByteArray software = QByteArrayLiteral("RAWfish Camera2");
    const QByteArray uniqueModel = QStringLiteral("Sailfish Camera2 camera %1")
            .arg(metadata.value(QStringLiteral("camera_id")).toString())
            .toUtf8();
    const QByteArray cfaPattern = cfaPatternBytes(cfa);
    const uint8_t dngVersion[4] = { 1, 4, 0, 0 };
    const uint8_t dngBackwardVersion[4] = { 1, 1, 0, 0 };
    const uint8_t cfaPlaneColor[3] = { 0, 1, 2 };
    const uint16_t cfaRepeatPatternDim[2] = { 2, 2 };
    const uint16_t blackLevelRepeatDim[2] = { 2, 2 };
    const uint32_t whiteLevelValue = uint32_t(whiteLevel);
    float blackLevel[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const QJsonArray black = metadata.value(QStringLiteral("black_level_pattern")).toArray();
    for (int index = 0; index < 4; ++index) {
        blackLevel[index] = float(index < black.size() ? black.at(index).toDouble() : 0.0);
    }
    const uint32_t activeArea[4] = { 0, 0, uint32_t(height), uint32_t(width) };
    uint32_t activeAreaFromMetadata[4];
    const QJsonArray active = metadata.value(QStringLiteral("active_array")).toArray();
    const uint32_t *effectiveActiveArea = activeArea;
    if (active.size() >= 4) {
        activeAreaFromMetadata[0] = uint32_t(active.at(1).toInt());
        activeAreaFromMetadata[1] = uint32_t(active.at(0).toInt());
        activeAreaFromMetadata[2] = uint32_t(active.at(3).toInt());
        activeAreaFromMetadata[3] = uint32_t(active.at(2).toInt());
        effectiveActiveArea = activeAreaFromMetadata;
    }
    const float defaultCropOrigin[2] = { 0.0f, 0.0f };
    const float defaultCropSize[2] = { float(width), float(height) };

    TIFFSetField(tiff, TIFFTAG_SUBFILETYPE, 0);
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, uint32_t(width));
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, uint32_t(height));
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tiff, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
    TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tiff, TIFFTAG_ROWSPERSTRIP, TIFFDefaultStripSize(tiff, 0));
    TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tiff, TIFFTAG_SOFTWARE, software.constData());
    TIFFSetField(tiff, TIFFTAG_CFAREPEATPATTERNDIM, cfaRepeatPatternDim);
    TIFFSetField(tiff, TIFFTAG_CFAPATTERN, 4, cfaPattern.constData());
    TIFFSetField(tiff, TIFFTAG_DNGVERSION, dngVersion);
    TIFFSetField(tiff, TIFFTAG_DNGBACKWARDVERSION, dngBackwardVersion);
    TIFFSetField(tiff, TIFFTAG_UNIQUECAMERAMODEL, uniqueModel.constData());
    TIFFSetField(tiff, TIFFTAG_CFAPLANECOLOR, 3, cfaPlaneColor);
    TIFFSetField(tiff, TIFFTAG_CFALAYOUT, 1);
    TIFFSetField(tiff, TIFFTAG_BLACKLEVELREPEATDIM, blackLevelRepeatDim);
    TIFFSetField(tiff, TIFFTAG_BLACKLEVEL, 4, blackLevel);
    TIFFSetField(tiff, TIFFTAG_WHITELEVEL, 1, &whiteLevelValue);
    TIFFSetField(tiff, TIFFTAG_ACTIVEAREA, effectiveActiveArea);
    TIFFSetField(tiff, TIFFTAG_DEFAULTCROPORIGIN, defaultCropOrigin);
    TIFFSetField(tiff, TIFFTAG_DEFAULTCROPSIZE, defaultCropSize);
    TIFFSetField(tiff, TIFFTAG_CALIBRATIONILLUMINANT1, 21);

    QJsonArray matrix = metadata.value(QStringLiteral("color_transform1")).toArray();
    if (matrix.size() < 9) {
        matrix = metadata.value(QStringLiteral("capture_color_transform")).toArray();
    }
    float colorMatrix[9];
    if (matrix.size() >= 9 && readRationalFloats(matrix, colorMatrix, 9)) {
        TIFFSetField(tiff, TIFFTAG_COLORMATRIX1, 9, colorMatrix);
    }
    const QJsonArray neutral = metadata.value(QStringLiteral("neutral_color_point")).toArray();
    float asShotNeutral[3];
    if (neutral.size() >= 3) {
        asShotNeutral[0] = jsonRationalFloat(neutral.at(0), 1.0f);
        asShotNeutral[1] = jsonRationalFloat(neutral.at(1), 1.0f);
        asShotNeutral[2] = jsonRationalFloat(neutral.at(2), 1.0f);
        TIFFSetField(tiff, TIFFTAG_ASSHOTNEUTRAL, 3, asShotNeutral);
    }
    const QJsonArray noiseProfileJson = metadata.value(QStringLiteral("noise_profile")).toArray();
    double noiseProfile[8];
    if (noiseProfileJson.size() >= 8) {
        for (int index = 0; index < 8; ++index) {
            noiseProfile[index] = noiseProfileJson.at(index).toDouble();
        }
        TIFFSetField(tiff, TIFFTAG_NOISEPROFILE, 8, noiseProfile);
    }

    // Per-channel vignetting/color-shading correction, if a calibration was
    // generated for this camera (see tools/calibration/generate_lens_shading.py
    // and calibration/README.md). Silently skipped when absent or when it
    // does not match this capture's camera/resolution/CFA.
    const QString calibrationDir = QStringLiteral(DEPLOYMENT_PATH "calibration");
    const QString cameraId = metadata.value(QStringLiteral("camera_id")).toString();
    QString lensShadingWarning;
    const QByteArray opcodeList2 = DngLensShading::buildOpcodeList2(
            calibrationDir, cameraId, cfa, width, height, &lensShadingWarning);
    if (!opcodeList2.isEmpty()) {
        TIFFSetField(tiff, TIFFTAG_OPCODELIST2, opcodeList2.size(), opcodeList2.constData());
    } else if (!lensShadingWarning.isEmpty()) {
        qWarning() << lensShadingWarning;
    }

    QByteArray row(rowStride, Qt::Uninitialized);
    for (int y = 0; y < height; ++y) {
        if (raw.read(row.data(), row.size()) != row.size()) {
            TIFFClose(tiff);
            QFile::remove(dngPath);
            *error = QStringLiteral("RAW16 file ended at row %1").arg(y);
            return false;
        }
        if (TIFFWriteScanline(tiff, row.data(), uint32_t(y), 0) < 0) {
            TIFFClose(tiff);
            QFile::remove(dngPath);
            *error = QStringLiteral("Cannot write DNG row %1").arg(y);
            return false;
        }
    }

    writeExifDirectory(tiff, metadata);
    TIFFClose(tiff);
    return true;
}

float jsonArrayFloat(const QJsonArray &array, int index, float fallback)
{
    return index >= 0 && index < array.size() ? float(array.at(index).toDouble(fallback)) : fallback;
}

bool readRationalMatrix(const QJsonArray &array, float *matrix)
{
    if (array.size() < 9) {
        return false;
    }
    for (int index = 0; index < 9; ++index) {
        const QJsonArray rational = array.at(index).toArray();
        const double denominator = rational.size() > 1 ? rational.at(1).toDouble() : 0.0;
        if (rational.size() < 2 || denominator == 0.0) {
            return false;
        }
        matrix[index] = float(rational.at(0).toDouble() / denominator);
    }
    return true;
}

}

bool DeclarativeCameraExtensions::saveJsonSidecar(const QString &targetPath,
                                                  const QByteArray &json)
{
    if (json.trimmed().isEmpty()) {
        m_rawCaptureErrors = QStringLiteral("Camera2 capture did not return metadata");
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || document.isNull()) {
        m_rawCaptureErrors = QStringLiteral("Camera2 metadata is not valid JSON");
        return false;
    }

    const QString sidecarPath = jsonSidecarPath(targetPath);
    QDir().mkpath(QFileInfo(sidecarPath).absolutePath());
    QFile::remove(sidecarPath);
    QFile file(sidecarPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_rawCaptureErrors = QStringLiteral("Cannot write metadata: %1")
                .arg(sidecarPath);
        return false;
    }
    file.write(document.toJson(QJsonDocument::Indented));
    return true;
}

bool DeclarativeCameraExtensions::copyJsonSidecar(const QString &sourcePath,
                                                  const QString &targetPath)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        m_rawCaptureErrors = QStringLiteral("Cannot read metadata: %1")
                .arg(sourcePath);
        return false;
    }
    return saveJsonSidecar(targetPath, source.readAll());
}

bool DeclarativeCameraExtensions::writeDngSidecar(const QString &metadataPath,
                                                  const QString &targetPath)
{
    QString error;
    if (!writeTiffDng(metadataPath, targetPath, &error)) {
        m_rawCaptureErrors = error;
        return false;
    }
    return true;
}

namespace {

bool loadRawRenderConfig(const QString &metadataPath, const QString &exposure,
                         RawRenderConfig *config, QString *error)
{
    QFile file(metadataPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read RAW metadata: %1").arg(metadataPath);
        return false;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    const QJsonObject object = document.object();
    if (object.isEmpty()) {
        *error = QStringLiteral("RAW metadata is not valid JSON");
        return false;
    }

    config->width = object.value(QStringLiteral("width")).toInt();
    config->height = object.value(QStringLiteral("height")).toInt();
    config->rowStride = object.value(QStringLiteral("row_stride")).toInt();
    config->whiteLevel = object.value(QStringLiteral("white_level")).toInt();
    config->iso = object.value(QStringLiteral("iso")).toInt();
    config->cfa = object.value(QStringLiteral("cfa")).toString();
    config->rawPath = object.value(QStringLiteral("raw_path")).toString();
    config->colorTemperature = object.value(QStringLiteral("color_temperature_requested_kelvin")).toInt();

    bool exposureOk = false;
    config->exposure = exposure.toFloat(&exposureOk);
    if (!exposureOk || config->exposure <= 0.0f || config->exposure > 32.0f) {
        *error = QStringLiteral("RAW exposure must be greater than 0 and at most 32");
        return false;
    }

    const QJsonArray black = object.value(QStringLiteral("black_level_pattern")).toArray();
    const QJsonArray gains = object.value(QStringLiteral("color_correction_gains")).toArray();
    const QJsonArray noiseProfile = object.value(QStringLiteral("noise_profile")).toArray();
    for (int index = 0; index < 4; ++index) {
        config->blackLevel[index] = int(jsonArrayFloat(black, index, 0.0f));
        config->gains[index] = jsonArrayFloat(gains, index, 1.0f);
    }
    config->noiseProfileCount = qMin(noiseProfile.size(), 8);
    for (int index = 0; index < config->noiseProfileCount; ++index) {
        config->noiseProfile[index] = noiseProfile.at(index).toDouble();
    }
    readRationalMatrix(object.value(QStringLiteral("capture_color_transform")).toArray(),
                       config->matrix);

    config->cfaMap = config->cfa == QLatin1String("RGGB") ? 0 :
                     config->cfa == QLatin1String("GRBG") ? 1 :
                     config->cfa == QLatin1String("GBRG") ? 2 : 3;
    if (config->width <= 1 || config->height <= 1 ||
            config->rowStride < config->width * 2 ||
            config->whiteLevel <= 0 || config->rawPath.isEmpty() ||
            (config->cfa != QLatin1String("RGGB") &&
             config->cfa != QLatin1String("GRBG") &&
             config->cfa != QLatin1String("GBRG") &&
             config->cfa != QLatin1String("BGGR"))) {
        *error = QStringLiteral("RAW metadata is incomplete or unsupported");
        return false;
    }

    return true;
}

PixelColor pixelColorAt(const RawRenderConfig &config, int x, int y)
{
    static const PixelColor maps[4][4] = {
        { PixelRed, PixelGreen, PixelGreen, PixelBlue },
        { PixelGreen, PixelRed, PixelBlue, PixelGreen },
        { PixelGreen, PixelBlue, PixelRed, PixelGreen },
        { PixelBlue, PixelGreen, PixelGreen, PixelRed },
    };
    return maps[config.cfaMap][(y & 1) * 2 + (x & 1)];
}

float correctedSample(const RawRenderConfig &config, const QVector<quint16> &pixels,
                      int x, int y)
{
    const int patternIndex = (y & 1) * 2 + (x & 1);
    const int black = config.blackLevel[patternIndex];
    const int value = pixels.at(y * config.width + x);
    const float normalized = value > black
            ? float(value - black) / float(config.whiteLevel - black)
            : 0.0f;
    const PixelColor color = pixelColorAt(config, x, y);
    const int gainIndex = color == PixelRed ? 0 : color == PixelBlue ? 3 : ((y & 1) ? 2 : 1);
    return normalized * config.gains[gainIndex];
}

float demosaicChannel(const RawRenderConfig &config, const QVector<quint16> &pixels,
                      int x, int y, PixelColor wanted)
{
    if (pixelColorAt(config, x, y) == wanted) {
        return correctedSample(config, pixels, x, y);
    }
    float sum = 0.0f;
    int count = 0;
    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
        const int sampleY = y + offsetY;
        if (sampleY < 0 || sampleY >= config.height) {
            continue;
        }
        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
            const int sampleX = x + offsetX;
            if (sampleX < 0 || sampleX >= config.width ||
                    pixelColorAt(config, sampleX, sampleY) != wanted) {
                continue;
            }
            sum += correctedSample(config, pixels, sampleX, sampleY);
            ++count;
        }
    }
    return count ? sum / float(count) : 0.0f;
}

uchar srgbByte(float linear)
{
    linear = qBound(0.0f, linear, 1.0f);
    const float encoded = linear <= 0.0031308f
            ? 12.92f * linear
            : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return uchar(qBound(0, int(encoded * 255.0f + 0.5f), 255));
}

bool readRawPixels(const RawRenderConfig &config, QVector<quint16> *pixels,
                   QString *error)
{
    QFile file(config.rawPath);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read RAW16 file: %1").arg(config.rawPath);
        return false;
    }
    pixels->resize(config.width * config.height);
    QByteArray row;
    row.resize(config.rowStride);
    for (int y = 0; y < config.height; ++y) {
        if (file.read(row.data(), row.size()) != row.size()) {
            *error = QStringLiteral("RAW16 file ended at row %1").arg(y);
            return false;
        }
        const uchar *bytes = reinterpret_cast<const uchar *>(row.constData());
        for (int x = 0; x < config.width; ++x) {
            (*pixels)[y * config.width + x] =
                    quint16(bytes[x * 2]) | quint16(bytes[x * 2 + 1] << 8);
        }
    }
    return true;
}

bool renderRawToImage(const RawRenderConfig &config, QImage *image,
                      QString *error)
{
    QVector<quint16> pixels;
    if (!readRawPixels(config, &pixels, error)) {
        return false;
    }
    *image = QImage(config.width, config.height, QImage::Format_RGB888);
    if (image->isNull()) {
        *error = QStringLiteral("Not enough memory for RAW image");
        return false;
    }
    for (int y = 0; y < config.height; ++y) {
        uchar *row = image->scanLine(y);
        for (int x = 0; x < config.width; ++x) {
            const float sensor[3] = {
                demosaicChannel(config, pixels, x, y, PixelRed),
                demosaicChannel(config, pixels, x, y, PixelGreen),
                demosaicChannel(config, pixels, x, y, PixelBlue),
            };
            for (int channel = 0; channel < 3; ++channel) {
                const float output = config.exposure *
                        (config.matrix[channel * 3] * sensor[0] +
                         config.matrix[channel * 3 + 1] * sensor[1] +
                         config.matrix[channel * 3 + 2] * sensor[2]);
                row[x * 3 + channel] = srgbByte(output);
            }
        }
    }
    return true;
}

void copyFileTimes(const QString &sourcePath, const QString &targetPath)
{
    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists()) {
        return;
    }

    struct utimbuf times;
    const uint modified = sourceInfo.lastModified().toTime_t();
    times.actime = modified;
    times.modtime = modified;
    utime(QFile::encodeName(targetPath).constData(), &times);
}

}

bool DeclarativeCameraExtensions::renderRawImage()
{
    QString error;
    RawRenderConfig config;
    if (!loadRawRenderConfig(m_rawCapturePrefix + QLatin1String(".json"),
                             m_rawCaptureExposure, &config, &error)) {
        m_rawCaptureErrors = error;
        return false;
    }

    QImage image;
    if (!renderRawToImage(config, &image, &error)) {
        m_rawCaptureErrors = error;
        return false;
    }
    if (m_rawCaptureRotationDegrees != 0) {
        image = image.transformed(QTransform().rotate(m_rawCaptureRotationDegrees));
    }
    ImageAdjustments::apply(&image, 1.0, m_rawCaptureColorTemperature,
                            m_rawCaptureColorTint);

    const QFileInfo targetInfo(m_rawCaptureTargetPath);
    QDir().mkpath(targetInfo.absolutePath());
    QFile::remove(m_rawCaptureTargetPath);
    QImageWriter writer(m_rawCaptureTargetPath, "JPG");
    writer.setQuality(m_rawCaptureJpegQuality);
    if (m_rawCaptureProgressiveJpeg) {
        writer.setProgressiveScanWrite(true);
    }
    if (!writer.write(image)) {
        m_rawCaptureErrors = writer.errorString().isEmpty()
                ? QStringLiteral("Could not save converted RAW image to final path")
                : QStringLiteral("Could not save converted RAW image: %1").arg(writer.errorString());
        return false;
    }
    copyFileTimes(config.rawPath, m_rawCaptureTargetPath);
    return true;
}

bool DeclarativeCameraExtensions::renderRawImageWithFastJpegConverter()
{
    const QString jpegConverter = rawFastJpegConverterPath();
    if (!executableExists(jpegConverter)) {
        m_rawCaptureErrors = QStringLiteral("RAW JPEG converter is not installed: %1")
                .arg(jpegConverter);
        appendRawCaptureLog(m_rawCaptureErrors);
        return false;
    }

    const QString metadataPath = m_rawCapturePrefix + QLatin1String(".json");
    QString metadataError;
    const QString rawPath = rawPathFromMetadata(metadataPath, &metadataError);
    if (!metadataError.isEmpty()) {
        m_rawCaptureErrors = metadataError;
        appendRawCaptureLog(m_rawCaptureErrors);
        return false;
    }
    if (!QFileInfo(rawPath).isFile()) {
        m_rawCaptureErrors = QStringLiteral("RAW16 file is missing: %1").arg(rawPath);
        appendRawCaptureLog(QStringLiteral("%1 metadata=%2 converter=%3")
                            .arg(m_rawCaptureErrors)
                            .arg(metadataPath)
                            .arg(jpegConverter));
        return false;
    }

    const QFileInfo targetInfo(m_rawCaptureTargetPath);
    QDir().mkpath(targetInfo.absolutePath());
    QFile::remove(m_rawCaptureTargetPath);

    const bool helperHandlesRotation = (m_rawCaptureRotationDegrees % 90) == 0;
    QStringList arguments;
    arguments << metadataPath
              << m_rawCaptureTargetPath
              << m_rawCaptureExposure
              << QString::number(m_rawCaptureJpegQuality)
              << (m_rawCaptureProgressiveJpeg ? QStringLiteral("1")
                                              : QStringLiteral("0"))
              << QString::number(helperHandlesRotation
                                  ? m_rawCaptureRotationDegrees : 0);

    QProcess process;
    process.setProgram(jpegConverter);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(3000)) {
        m_rawCaptureErrors = QStringLiteral("Could not start RAW JPEG converter: %1")
                .arg(process.errorString());
        appendRawCaptureLog(QStringLiteral("%1 command=%2 metadata=%3 raw=%4 output=%5")
                            .arg(m_rawCaptureErrors)
                            .arg(jpegConverter)
                            .arg(metadataPath)
                            .arg(rawPath)
                            .arg(m_rawCaptureTargetPath));
        return false;
    }
    if (!process.waitForFinished(-1) ||
            process.exitStatus() != QProcess::NormalExit ||
            process.exitCode() != 0) {
        const QString errors = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        m_rawCaptureErrors = errors.isEmpty()
                ? QStringLiteral("RAW JPEG converter failed: %1").arg(jpegConverter)
                : QStringLiteral("%1 failed:\n%2").arg(jpegConverter, errors);
        appendRawCaptureLog(QStringLiteral("%1 metadata=%2 raw=%3 output=%4")
                            .arg(m_rawCaptureErrors)
                            .arg(metadataPath)
                            .arg(rawPath)
                            .arg(m_rawCaptureTargetPath));
        return false;
    }
    const QString converterOutput = QString::fromLocal8Bit(
                process.readAllStandardError()).trimmed();
    if (!converterOutput.isEmpty()) {
        appendRawCaptureLog(converterOutput);
    }

    if (!QFileInfo(m_rawCaptureTargetPath).isFile() ||
            QFileInfo(m_rawCaptureTargetPath).size() <= 0) {
        m_rawCaptureErrors = QStringLiteral("RAW JPEG converter did not create usable output");
        appendRawCaptureLog(QStringLiteral("%1 output=%2 metadata=%3 raw=%4")
                            .arg(m_rawCaptureErrors)
                            .arg(m_rawCaptureTargetPath)
                            .arg(metadataPath)
                            .arg(rawPath));
        return false;
    }

    if ((!helperHandlesRotation && m_rawCaptureRotationDegrees != 0) ||
            m_rawCaptureColorTint != 0) {
        QImage image(m_rawCaptureTargetPath);
        if (image.isNull()) {
            m_rawCaptureErrors = QStringLiteral("RAW JPEG converter output is not usable");
            appendRawCaptureLog(QStringLiteral("%1 output=%2 metadata=%3 raw=%4")
                                .arg(m_rawCaptureErrors)
                                .arg(m_rawCaptureTargetPath)
                                .arg(metadataPath)
                                .arg(rawPath));
            return false;
        }
        if (!helperHandlesRotation && m_rawCaptureRotationDegrees != 0) {
            image = image.transformed(QTransform().rotate(m_rawCaptureRotationDegrees));
        }
        ImageAdjustments::apply(&image, 1.0, 5500, m_rawCaptureColorTint);

        QImageWriter writer(m_rawCaptureTargetPath, "JPG");
        writer.setQuality(m_rawCaptureJpegQuality);
        if (m_rawCaptureProgressiveJpeg) {
            writer.setProgressiveScanWrite(true);
        }
        if (!writer.write(image)) {
            m_rawCaptureErrors = writer.errorString().isEmpty()
                    ? QStringLiteral("Could not save adjusted RAW JPEG")
                    : QStringLiteral("Could not save adjusted RAW JPEG: %1").arg(writer.errorString());
            return false;
        }
    }

    copyFileTimes(m_rawCapturePrefix + QLatin1String(".raw16"),
                  m_rawCaptureTargetPath);
    return true;
}

void DeclarativeCameraExtensions::preserveRawCaptureFiles()
{
    const bool saveRaw16 = m_rawCaptureSaveFormat == QLatin1String("raw16")
            || m_rawCaptureSaveFormat == QLatin1String("both");
    const bool saveDng = m_rawCaptureSaveFormat == QLatin1String("dng")
            || m_rawCaptureSaveFormat == QLatin1String("both");
    if (!saveRaw16 && !saveDng) {
        return;
    }
    if (m_rawCaptureArchivePrefix.isEmpty()) {
        return;
    }

    const QFileInfo archiveInfo(m_rawCaptureArchivePrefix);
    QDir().mkpath(archiveInfo.absolutePath());

    if (saveRaw16) {
        const QString rawSource = m_rawCapturePrefix + QLatin1String(".raw16");
        const QString rawDestination = m_rawCaptureArchivePrefix + QLatin1String(".raw16");
        if (QFileInfo(rawSource).absoluteFilePath() !=
                QFileInfo(rawDestination).absoluteFilePath()) {
            QFile::remove(rawDestination);
            if (!QFile::copy(rawSource, rawDestination)) {
                qWarning() << "Could not preserve RAW capture sidecar" << rawSource << "to" << rawDestination;
            }
        }

        QString metadataError;
        const QString metadataSource = m_rawCapturePrefix + QLatin1String(".json");
        const QString metadataDestination = m_rawCaptureArchivePrefix + QLatin1String(".json");
        if (!writeMetadataWithRawPath(metadataSource, metadataDestination,
                                      rawDestination, &metadataError)) {
            qWarning() << "Could not preserve RAW metadata sidecar"
                       << metadataSource << "to" << metadataDestination
                       << metadataError;
        }
    }
    if (saveDng) {
        const QString dngPath = m_rawCaptureArchivePrefix + QLatin1String(".dng");
        if (!writeDngSidecar(m_rawCapturePrefix + QLatin1String(".json"),
                             dngPath)) {
            qWarning() << "Could not preserve DNG sidecar" << dngPath
                       << m_rawCaptureErrors;
        }
    }
}

void DeclarativeCameraExtensions::clearRawImageCapture()
{
    QProcess *process = m_rawCaptureProcess.take();
    if (process) {
        process->deleteLater();
    }
    m_rawCaptureDirectory.reset();
    m_rawCaptureStage = RawCaptureIdle;
    m_rawCaptureTargetPath.clear();
    m_rawCapturePrefix.clear();
    m_rawCaptureArchivePrefix.clear();
    m_rawCaptureErrors.clear();
    m_rawCaptureExposure.clear();
    m_rawCaptureStandardOutput.clear();
    m_rawCaptureSaveFormat = QStringLiteral("raw16");
    m_rawRenderEngine = QStringLiteral("internal");
    m_rawCaptureProgressiveJpeg = false;
    m_rawCaptureJpegQuality = 92;
    m_rawCaptureRotationDegrees = 0;
    m_rawCaptureColorTemperature = 0;
    m_rawCaptureColorTint = 0;
    m_rawRenderStart = 0;
}

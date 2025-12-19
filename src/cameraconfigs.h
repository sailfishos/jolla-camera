/*
 * SPDX-FileCopyrightText: 2021 Jolla Ltd.
 * SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CAMERACONFIGS_H
#define CAMERACONFIGS_H

#include <QObject>
#include <QSize>
#include <QVariantList>

QT_BEGIN_NAMESPACE
class QCamera;
class QCameraImageCapture;
class QMediaRecorder;
QT_END_NAMESPACE

class CameraConfigs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject * camera READ camera WRITE setCamera NOTIFY cameraChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

    // TODO: Replace QVariantList here with QList<QSize> on newer Qt
    Q_PROPERTY(QVariantList supportedViewfinderResolutions READ supportedViewfinderResolutions NOTIFY supportedViewfinderResolutionsChanged)
    Q_PROPERTY(QVariantList supportedImageResolutions READ supportedImageResolutions NOTIFY supportedImageResolutionsChanged)
    Q_PROPERTY(QVariantList supportedVideoResolutions READ supportedVideoResolutions NOTIFY supportedVideoResolutionsChanged)
    Q_PROPERTY(QVariantList supportedIsoSensitivities READ supportedIsoSensitivities NOTIFY supportedIsoSensitivitiesChanged)
    Q_PROPERTY(QVariantList supportedWhiteBalanceModes READ supportedWhiteBalanceModes NOTIFY supportedWhiteBalanceModesChanged)
    Q_PROPERTY(QVariantList supportedExposureModes READ supportedExposureModes NOTIFY supportedExposureModesChanged)
    Q_PROPERTY(QVariantList supportedColorFilters READ supportedColorFilters NOTIFY supportedColorFiltersChanged)
    Q_PROPERTY(QVariantList supportedFocusModes READ supportedFocusModes NOTIFY supportedFocusModesChanged)
    Q_PROPERTY(QVariantList supportedFocusPointModes READ supportedFocusPointModes NOTIFY supportedFocusPointModesChanged)
    Q_PROPERTY(QVariantList supportedMeteringModes READ supportedMeteringModes NOTIFY supportedMeteringModesChanged)
    Q_PROPERTY(QVariantList supportedFlashModes READ supportedFlashModes NOTIFY supportedFlashModesChanged)

public:
    enum AspectRatio {
        AspectRatio_4_3,
        AspectRatio_16_9
    };

    Q_ENUM(AspectRatio)

    CameraConfigs(QObject *parent = nullptr);
    ~CameraConfigs();

    QList<QObject *> exposedItems() const;

    QVariantList supportedViewfinderResolutions() const;
    QVariantList supportedImageResolutions() const;
    QVariantList supportedVideoResolutions() const;
    QVariantList supportedIsoSensitivities() const;
    QVariantList supportedWhiteBalanceModes() const;
    QVariantList supportedExposureModes() const;
    QVariantList supportedColorFilters() const;
    QVariantList supportedFocusModes() const;
    QVariantList supportedFocusPointModes() const;
    QVariantList supportedMeteringModes() const;
    QVariantList supportedFlashModes() const;

    void setCamera(QObject *camera);
    QObject *camera() const;

    bool ready() const;

signals:
    void cameraChanged();
    void readyChanged();
    void supportedViewfinderResolutionsChanged();
    void supportedImageResolutionsChanged();
    void supportedVideoResolutionsChanged();
    void supportedIsoSensitivitiesChanged();
    void supportedWhiteBalanceModesChanged();
    void supportedExposureModesChanged();
    void supportedColorFiltersChanged();
    void supportedFocusModesChanged();
    void supportedFocusPointModesChanged();
    void supportedMeteringModesChanged();
    void supportedFlashModesChanged();

private slots:
    void handleStatus();
    void handleState();
    void handleCaptureMode();

private:
    bool m_ready = false;
    QCamera *m_camera = nullptr;
    QObject *m_qmlCamera = nullptr;
    QVariantList m_supportedViewfinderResolutions;
    QVariantList m_supportedImageResolutions;
    QVariantList m_supportedVideoResolutions;
    QVariantList m_supportedIsoSensitivities;
    QVariantList m_supportedWhiteBalanceModes;
    QVariantList m_supportedExposureModes;
    QVariantList m_supportedColorFilters;
    QVariantList m_supportedFocusModes;
    QVariantList m_supportedFocusPointModes;
    QVariantList m_supportedMeteringModes;
    QVariantList m_supportedFlashModes;
};

#endif // CAMERACONFIGS_H

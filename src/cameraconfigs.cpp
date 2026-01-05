// SPDX-FileCopyrightText: 2021 - 2024 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

#include "cameraconfigs.h"

#include <QCamera>
#include <QMediaRecorder>
#include <QCameraImageCapture>
#include <mgconfitem.h>

CameraConfigs::CameraConfigs(QObject *parent)
    : QObject(parent)
{
}

CameraConfigs::~CameraConfigs()
{
}

QObject *CameraConfigs::camera() const
{
    return m_camera;
}

bool CameraConfigs::ready() const
{
    return m_ready;
}

void CameraConfigs::setCamera(QObject * camera)
{
    m_qmlCamera = camera;
    QCamera *qCamera = camera->property("mediaObject").value<QCamera *>();

    if (m_camera != qCamera) {
        m_camera = qCamera;

        connect(m_camera, &QCamera::statusChanged, this, &CameraConfigs::handleStatus);
        connect(m_camera, &QCamera::stateChanged, this, &CameraConfigs::handleState);
        connect(m_camera, &QCamera::captureModeChanged, this, &CameraConfigs::handleCaptureMode);
        m_ready = false;
        handleStatus();

        emit cameraChanged();
    }
}

void CameraConfigs::handleState()
{
    if (m_camera && m_camera->state() == QCamera::UnloadedState) {
        m_ready = false;
    }
}

void CameraConfigs::handleCaptureMode()
{
    m_ready = false;
}

void CameraConfigs::handleStatus()
{
    if (!m_ready) {
        if (m_camera && (m_camera->status() == QCamera::LoadedStatus
                         || m_camera->status() == QCamera::StartingStatus
                         || m_camera->status() == QCamera::ActiveStatus)) {

            m_supportedViewfinderResolutions.clear();
            for (const QSize resolution : m_camera->supportedViewfinderResolutions()) {
                m_supportedViewfinderResolutions.append(resolution);
            }

            QObject *qmlCapture = qvariant_cast<QObject *>(m_qmlCamera->property("imageCapture"));
            QList<QCameraImageCapture *> captures = qmlCapture->findChildren<QCameraImageCapture *>();
            if (captures.count() > 0) {
                QCameraImageCapture *capture = captures[0];
                m_supportedImageResolutions.clear();
                for (const QSize resolution : capture->supportedResolutions()) {
                    m_supportedImageResolutions.append(resolution);
                }
            }

            QObject *qmlRecorder = qvariant_cast<QObject *>(m_qmlCamera->property("videoRecorder"));
            QList<QMediaRecorder *> recorders = qmlRecorder->findChildren<QMediaRecorder *>();
            if (recorders.count() > 0) {
                QMediaRecorder *recorder = recorders[0];
                m_supportedVideoResolutions.clear();

                QSize maxVideoResolution;
                QVariant value(MGConfItem("/apps/jolla-camera/maxVideoResolution").value());
                if (!value.isNull()) {
                    QStringList values = value.toString().split('x');
                    if (values.size() == 2) {
                        maxVideoResolution = QSize(values.at(0).toInt(), values.at(1).toInt());
                    }
                }

                for (const QSize resolution : recorder->supportedResolutions()) {
                    if (!maxVideoResolution.isValid() || (resolution.height() <= maxVideoResolution.height() &&
                                                          resolution.width() <= maxVideoResolution.width())) {
                        m_supportedVideoResolutions.append(resolution);
                    }
                }
            }

            m_supportedIsoSensitivities.clear();
            for (int value : m_camera->exposure()->supportedIsoSensitivities()) {

                // Filter out invalid ISO value
                if (value != 1) {
                    m_supportedIsoSensitivities.append(value);
                }
            }
            std::sort(m_supportedIsoSensitivities.begin(), m_supportedIsoSensitivities.end());

            auto updateSupportedModes = [](QVariantList &modes, QLatin1String modeName,
                                           const QMetaObject &meta, auto isSupported) {
                modes.clear();
                // TODO: Use QMetaEnum::fromType<Class::EnumName>() once Qt Multimedia uses Q_ENUM
                int i = meta.indexOfEnumerator(modeName.data());
                QMetaEnum e = meta.enumerator(i);
                for (int j = 0; j < e.keyCount(); j++) {
                    int mode = e.value(j);
                    if (isSupported(mode)) {
                        modes << mode;
                    }
                }
            };

            auto isWhiteBalanceModeSupported = [this](int mode) {
                return m_camera->imageProcessing()->isWhiteBalanceModeSupported(static_cast<QCameraImageProcessing::WhiteBalanceMode>(mode));
            };
            updateSupportedModes(m_supportedWhiteBalanceModes, QLatin1String("WhiteBalanceMode"),
                                 QCameraImageProcessing::staticMetaObject, isWhiteBalanceModeSupported);

            auto isExposureModeSupported = [this](int mode) {
                return m_camera->exposure()->isExposureModeSupported(static_cast<QCameraExposure::ExposureMode>(mode));
            };
            updateSupportedModes(m_supportedExposureModes, QLatin1String("ExposureMode"),
                                 QCameraExposure::staticMetaObject, isExposureModeSupported);

            auto isColorFilterSupported = [this](int mode) {
                return m_camera->imageProcessing()->isColorFilterSupported(static_cast<QCameraImageProcessing::ColorFilter>(mode));
            };
            updateSupportedModes(m_supportedColorFilters, QLatin1String("ColorFilter"),
                                 QCameraImageProcessing::staticMetaObject, isColorFilterSupported);

            auto isFocusModeSupported = [this](int mode) {
                return m_camera->focus()->isFocusModeSupported(static_cast<QCameraFocus::FocusMode>(mode));
            };
            updateSupportedModes(m_supportedFocusModes, QLatin1String("FocusMode"),
                                 QCameraFocus::staticMetaObject, isFocusModeSupported);

            auto isFocusPointModeSupported = [this](int mode) {
                return m_camera->focus()->isFocusPointModeSupported(static_cast<QCameraFocus::FocusPointMode>(mode));
            };
            updateSupportedModes(m_supportedFocusPointModes, QLatin1String("FocusPointMode"),
                                 QCameraFocus::staticMetaObject, isFocusPointModeSupported);

            auto isMeteringModeSupported = [this](int mode) {
                return m_camera->exposure()->isMeteringModeSupported(static_cast<QCameraExposure::MeteringMode>(mode));
            };
            updateSupportedModes(m_supportedMeteringModes, QLatin1String("MeteringMode"),
                                 QCameraExposure::staticMetaObject, isMeteringModeSupported);

            auto isFlashModeSupported = [this](int mode) {
                return m_camera->exposure()->isFlashModeSupported(static_cast<QCameraExposure::FlashModes>(mode));
            };
            updateSupportedModes(m_supportedFlashModes, QLatin1String("FlashMode"),
                                 QCameraExposure::staticMetaObject, isFlashModeSupported);

            m_ready = true;
        } else if (!m_camera) {
            m_supportedViewfinderResolutions.clear();
            m_supportedImageResolutions.clear();
            m_supportedVideoResolutions.clear();
            m_supportedIsoSensitivities.clear();
            m_supportedWhiteBalanceModes.clear();
            m_supportedExposureModes.clear();
            m_supportedFocusModes.clear();
            m_supportedFocusPointModes.clear();
            m_supportedMeteringModes.clear();
            m_supportedFlashModes.clear();

            m_ready = true;
        }

        if (m_ready) {
            emit supportedViewfinderResolutionsChanged();
            emit supportedImageResolutionsChanged();
            emit supportedVideoResolutionsChanged();
            emit supportedIsoSensitivitiesChanged();
            emit supportedWhiteBalanceModesChanged();
            emit supportedExposureModesChanged();
            emit supportedColorFiltersChanged();
            emit supportedFocusModesChanged();
            emit supportedFocusPointModesChanged();
            emit supportedMeteringModesChanged();
            emit supportedFlashModesChanged();
            emit readyChanged();
        }
    }
}

QVariantList CameraConfigs::supportedViewfinderResolutions() const
{
    return m_supportedViewfinderResolutions;
}

QVariantList CameraConfigs::supportedImageResolutions() const
{
    return m_supportedImageResolutions;
}

QVariantList CameraConfigs::supportedVideoResolutions() const
{
    return m_supportedVideoResolutions;
}

QVariantList CameraConfigs::supportedIsoSensitivities() const
{
    return m_supportedIsoSensitivities;
}

QVariantList CameraConfigs::supportedExposureModes() const
{
    return m_supportedExposureModes;
}

QVariantList CameraConfigs::supportedColorFilters() const
{
    return m_supportedColorFilters;
}

QVariantList CameraConfigs::supportedWhiteBalanceModes() const
{
    return m_supportedWhiteBalanceModes;
}

QVariantList CameraConfigs::supportedFocusModes() const
{
    return m_supportedFocusModes;
}

QVariantList CameraConfigs::supportedFocusPointModes() const
{
    return m_supportedFocusPointModes;
}

QVariantList CameraConfigs::supportedMeteringModes() const
{
    return m_supportedMeteringModes;
}

QVariantList CameraConfigs::supportedFlashModes() const
{
    return m_supportedFlashModes;
}

// SPDX-FileCopyrightText: 2016 - 2024 Jolla Ltd.
// SPDX-FileCopyrightText: 2024 - 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import QtQuick.Window 2.0 as QuickWindow
import QtMultimedia 5.4
import QtPositioning 5.1
import QtSensors 5.0
import Nemo.Time 1.0
import Nemo.Configuration 1.0
import Nemo.Notifications 1.0
import org.nemomobile.systemsettings 1.0
import Sailfish.Silica 1.0
import com.jolla.camera 1.0

import "../settings"

SettingsOverlay {
    id: settingsOverlay

    property bool reallyWideScreen: captureView.reallyWideScreen
    property var captureView
    property var camera
    property Item focusArea

    property int _recordingDuration: clock.enabled ? ((clock.time - _startTime) / 1000) : 0
    property int _recSecsRemaining: {
        var totalBitRate = (camera.videoRecorder.videoBitRate + camera.videoRecorder.audioBitRate) * 1.05
        var maxDuration = Settings.storageMaxFileSize * 8 / totalBitRate
        return maxDuration - _recordingDuration
    }

    property var _startTime: new Date()

    width: captureView.width
    height: captureView.height

    property int _pictureRotation: {
        if (orientationSensor.connectedToBackend) {
            return QuickWindow.Screen.primaryOrientation == Qt.PortraitOrientation ? 0 : 90
        } else {
            switch (QuickWindow.Screen.orientation) {
            case Qt.PortraitOrientation:
                return 0
            case Qt.LandscapeOrientation:
                return 90
            case Qt.InvertedPortraitOrientation:
                return 180
            case Qt.InvertedLandscapeOrientation:
                return 270
            default:
                return 0
            }
        }
    }

    OrientationSensor {
        id: orientationSensor

        active: captureView.effectiveActive
        onReadingChanged: {
            switch (reading.orientation) {
            case OrientationReading.TopUp:
                _pictureRotation = 0; break
            case OrientationReading.TopDown:
                _pictureRotation = 180; break
            case OrientationReading.LeftUp:
                _pictureRotation = 270; break
            case OrientationReading.RightUp:
                _pictureRotation = 90; break
            default:
                // Keep device orientation at previous state
            }
        }
    }

    function writeMetaData() {
        captureView.captureOrientation = camera.position === Camera.FrontFace
                       ? (720 + camera.orientation - _pictureRotation) % 360
                       : (720 + camera.orientation + _pictureRotation) % 360

        // Camera documentation says dateTimeOriginal should be used but at the moment CameraBinMetaData uses only
        // date property (which the documentation doesn't even list)
        camera.metaData.date = new Date()

        if (positionSource.active) {
            var coordinate = positionSource.position.coordinate
            if (coordinate.isValid) {
                camera.metaData.gpsLatitude = coordinate.latitude
                camera.metaData.gpsLongitude = coordinate.longitude
            } else {
                camera.metaData.gpsLatitude = undefined
                camera.metaData.gpsLongitude = undefined
            }
            camera.metaData.gpsAltitude = positionSource.position.altitudeValid
                        ? coordinate.altitude
                        : undefined
        } else {
            camera.metaData.gpsLatitude = undefined
            camera.metaData.gpsLongitude = undefined
            camera.metaData.gpsAltitude = undefined
        }
    }

    readonly property int storagePathStatus: Settings.storagePathStatus
    onStoragePathStatusChanged: checkStorage()

    readonly property bool _applicationActive: Qt.application.state == Qt.ApplicationActive
    on_ApplicationActiveChanged: if (_applicationActive) checkStorage()

    Component.onCompleted: checkStorage()

    function checkStorage() {
        if (Qt.application.state != Qt.ApplicationActive) {
            // We don't want to show notification when we're in the background
            return
        }

        var prevStatus = previousStoragePathStatus.value
        if (Settings.storagePathStatus == Settings.Unavailable) {
            if (prevStatus != Settings.storagePathStatus) {
                //% "The selected storage is unavailable. Device memory will be used instead"
                notification.publishMessage(qsTrId("camera-me-storage-unavailable"))
            }
        } else if (Settings.storagePathStatus == Settings.Available) {
            if (prevStatus == Settings.Unavailable || prevStatus == Settings.Mounting) {
                //% "Using memory card"
                notification.publishMessage(qsTrId("camera-me-using-memory-card"))
            }
        } else if (Settings.storagePathStatus == Settings.Mounting) {
            //% "Busy mounting the memory card. Device memory will be used instead"
            notification.publishMessage(qsTrId("camera-me-storage-mounting"))
        }
        previousStoragePathStatus.value = Settings.storagePathStatus

        // Refresh the remaining storage space, in case it was modified while we weren't active
        // TODO: Do this regularly throughout recording, if it doesn't interfere.
        if (!captureView.recording) {
            Settings.refreshMaxFileSize()
        }
    }

    PositionSource {
        id: positionSource

        // QDeclarativePositionSource::active property is not well behaved.
        // The internal position source may not be initialised until the component completes,
        // and in that instance the active property may be reset.
        // So, initialise the property after component completion to ensure correct behaviour.
        Component.onCompleted: positionSource.active = Qt.binding(function() {
            return captureView.effectiveActive && locationSettings.locationEnabled && Settings.global.saveLocationInfo
        })
    }

    opacity: 0.0

    showCommonControls: !captureView.recording
    isPortrait: captureView.isPortrait
    topButtonRowHeight: Screen.sizeCategory >= Screen.Large ? Theme.itemSizeLarge : Theme.itemSizeSmall
    deviceToggleEnabled: !captureView.captureBusy

    onPinchStarted: {
        // We're not getting notifications when the maximumDigitalZoom changes,
        // so update the value here.
        zoomIndicator.maximumZoom = camera.maximumDigitalZoom
    }

    onPinchUpdated: {
        camera.digitalZoom = Math.max(1, Math.min(
                    camera.digitalZoom + ((camera.maximumDigitalZoom - 1)
                                          * ((pinch.scale / Math.abs(pinch.previousScale) - 1))),
                    camera.maximumDigitalZoom))
        zoomIndicator.show()
    }

    Connections {
        target: captureView
        ignoreUnknownSignals: true
        onEffectiveActiveChanged: {
            if (!captureView.effectiveActive) {
                settingsOverlay.closeMenus()
            }
        }
    }

    onClicked: {
        if (!captureView._captureOnFocus && captureView.touchFocusSupported) {
            // Translate and rotate the touch point into focusArea's space.
            var focusPoint
            switch ((360 - captureView.viewfinderOrientation) % 360) {

            case 90:
                focusPoint = Qt.point(
                            mouse.y - ((height - focusArea.width) / 2) - captureView.viewfinderOffset,
                            width - mouse.x);
                break;
            case 180:
                focusPoint = Qt.point(
                            width - mouse.x - ((width - focusArea.width) / 2) + captureView.viewfinderOffset,
                            height - mouse.y);
                break;
            case 270:
                focusPoint = Qt.point(
                            height - mouse.y - ((height - focusArea.width) / 2) + captureView.viewfinderOffset,
                            mouse.x);
                break;
            default:
                focusPoint = Qt.point(
                            mouse.x - ((width - focusArea.width) / 2) - captureView.viewfinderOffset,
                            mouse.y);
                break;
            }

            // Normalize the focus point.
            focusPoint.x = focusPoint.x / focusArea.width
            focusPoint.y = focusPoint.y / focusArea.height

            // Mirror the point if the viewfinder is mirrored.
            if (captureView._horizontalMirror) {
                focusPoint.x = 1 - focusPoint.x
            }
            if (captureView._verticalMirror) {
                focusPoint.y = 1 - focusPoint.y
            }

            if (focusPoint.x >= 0.0 && focusPoint.x <= 1.0 && focusPoint.y >= 0.0 && focusPoint.y <= 1.0) {
                captureView.setFocusPoint(focusPoint)
            }
        }
    }

    shutter: CameraButton {
        id: captureButton

        property bool canStopVideo: startRecordTimer.running
                                    || camera.videoRecorder.recorderState == CameraRecorder.RecordingState

        z: settingsOverlay.inButtonLayout ? 1 : 0
        size: Theme.iconSizeMedium
        anchors.centerIn: parent
        background.visible: icon.opacity < 1.0
        enabled: captureView._canCapture
                    && !captureView._captureOnFocus

        onPressed: camera.lockAutoFocus()
        onReleased: {
            if (containsMouse) {
                captureView._triggerCapture()
            } else {
                camera.unlockAutoFocus()
            }
        }
        onCanceled: camera.unlockAutoFocus()

        icon {
            opacity: {
                if (captureTimer.running) {
                    return Theme.opacityLow
                } else if (captureButton.pressed) {
                    return Theme.opacityHigh
                } else if (captureView._captureOnFocus) {
                    return Theme.opacityOverlay
                } else {
                    return 1.0
                }
            }

            source: canStopVideo
                    ? "image://theme/icon-camera-video-shutter-off"
                    : (camera.captureMode == Camera.CaptureVideo
                       ? "image://theme/icon-camera-video-shutter-on"
                       : "image://theme/icon-camera-shutter")
        }

        Label {
            anchors {
                centerIn: parent
                // The font is not monospaced so countdown values over 10 seconds will be slightly off center
                // TODO: FontMetrics might make more sense than magic below, but also be quite much more complex codewise
                verticalCenterOffset: -Math.round(font.pixelSize/20)
                horizontalCenterOffset: text.length > 1 ? -Math.round(font.pixelSize/20) : 0
            }
            text: captureTimer.running ? Math.floor(captureView._captureCountdown + 1) : Settings.mode.timer
            visible: Settings.mode.timer != 0 && !captureButton.canStopVideo
            opacity: captureTimer.running ? captureView._captureCountdown % 1 : 1.0
            color: captureTimer.running ? Theme.lightPrimaryColor : Theme.darkPrimaryColor
            font {
                pixelSize: captureTimer.running ? Theme.fontSizeHuge : Theme.fontSizeTiny
                weight: Font.Light
            }
            Behavior on font.pixelSize {
                NumberAnimation { easing.type: Easing.InOutQuad; duration: 150 }
            }
        }
    }

    Label {
        id: timerLabel

        y: Theme.paddingMedium
           + (isPortrait ? Screen.topCutout.height : 0)
        anchors.horizontalCenter: parent.horizontalCenter
        opacity: captureView.recording ? 1.0 : 0.0
        Behavior on opacity { FadeAnimator {} }

        text: Format.formatDuration(_recordingDuration,
                                    _recordingDuration >= 3600 ? Formatter.DurationLong : Formatter.DurationShort)
        font.pixelSize: Theme.fontSizeLarge
        color: Theme.lightPrimaryColor
        style: Text.Outline
        styleColor: "#20000000"
    }

    Label {
        anchors {
            horizontalCenter: parent.horizontalCenter
            top: timerLabel.bottom
        }
        opacity: {
            if (captureView.recording)
                return _recSecsRemaining < 300 || (_recordingDuration > 120 && _recSecsRemaining < 3600) ? 1.0 : 0.0
            else 
                return camera.captureMode == Camera.CaptureVideo && _recSecsRemaining < 300 ? 1.0 : 0.0
        }
        Behavior on opacity { FadeAnimator {} }
        text: {
            if (captureView.recording) {
                if (_recSecsRemaining >= 60) {
                    //% "Recording stops in %n min"
                    return qsTrId("camera-la-rec-remain_min_countdown", Math.floor(_recSecsRemaining/60))
                } else {
                    //% "Recording stops in %n sec"
                    return qsTrId("camera-la-rec-remain_sec_countdown", _recSecsRemaining)
                }
            } else if (_recSecsRemaining == 0) {
                //% "Storage full"
                return qsTrId("camera-la-storage_full")
            } else {
                //% "%n min estimated recording time left"
                return qsTrId("camera-la-video_rec_remain", Math.floor(_recSecsRemaining/60))
            }
        }
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.lightPrimaryColor
        style: Text.Outline
        styleColor: "#20000000"
    }

    WallClock {
        id: clock

        updateFrequency: WallClock.Second
        enabled: camera.videoRecorder.recorderState == CameraRecorder.RecordingState
        onEnabledChanged: {
            if (enabled) {
                _startTime = clock.time
            }
        }
        onTimeChanged: {
            if (enabled && _recSecsRemaining <= 0 && camera) {
                camera.videoRecorder.stop()
            }
        }
    }

    // Viewfinder Grid
    Item {
        id: grid

        property real gridWidth: captureView.viewfinderOrientation % 180 == 0 ? focusArea.width : focusArea.height
        property real gridHeight: captureView.viewfinderOrientation % 180 == 0 ? focusArea.height : focusArea.width

        anchors.centerIn: parent
        anchors.verticalCenterOffset: isPortrait ? captureView.viewfinderOffset : 0
        anchors.horizontalCenterOffset: isPortrait ? 0 : captureView.viewfinderOffset

        visible: Settings.global.viewfinderGrid != "none"
                 && camera.cameraStatus == Camera.ActiveStatus

        width: gridWidth / 3
        height: gridHeight / 3

        GridLine {
            anchors {
                horizontalCenter: grid.horizontalCenter
                verticalCenter: grid.top
            }
            width: grid.gridWidth
        }

        GridLine {
            anchors {
                horizontalCenter: grid.horizontalCenter
                verticalCenter: grid.bottom
            }
            width: grid.gridWidth
        }

        GridLine {
            anchors {
                horizontalCenter: grid.left
                verticalCenter: grid.verticalCenter
            }
            width: grid.gridHeight
            rotation: 90
        }

        GridLine {
            anchors {
                horizontalCenter: grid.right
                verticalCenter: grid.verticalCenter
            }
            width: grid.gridHeight
            rotation: 90
        }
    }

    ZoomIndicator {
        id: zoomIndicator

        anchors {
            top: timerLabel.bottom
            horizontalCenter: parent.horizontalCenter
        }

        zoom: camera.digitalZoom
        maximumZoom: camera.maximumDigitalZoom
    }

    Notification {
        id: notification

        function publishMessage(msg) {
            notification.previewBody = msg
            notification.publish()
        }

        isTransient: true
        urgency: Notification.Critical
    }

    LocationSettings { id: locationSettings }

    ConfigurationValue {
        id: previousStoragePathStatus
        key: "/apps/jolla-camera/previousStoragePathStatus"
    }
}

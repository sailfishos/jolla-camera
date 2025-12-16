// SPDX-FileCopyrightText: 2013 - 2022 Jolla Ltd.
// SPDX-FileCopyrightText: 2020 Open Mobile Platform LLC.
// SPDX-FileCopyrightText: 2024 - 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.4
import QtMultimedia 5.4
import Sailfish.Silica 1.0
import Sailfish.Policy 1.0
import com.jolla.camera 1.0
import Nemo.Policy 1.0
import Nemo.Ngf 1.0
import Nemo.DBus 2.0
import Nemo.Notifications 1.0
import org.nemomobile.systemsettings 1.0

import "../settings"

FocusScope {
    id: captureView

    property bool active
    property int orientation
    property int effectiveIso: Settings.mode.iso
    property bool inButtonLayout: captureOverlay == null || captureOverlay.inButtonLayout
    property QtObject captureModel

    readonly property int viewfinderOrientation: {
        var rotation = 0
        switch (captureView.orientation) {
        case Orientation.Landscape: rotation = 90; break;
        case Orientation.PortraitInverted: rotation = 180; break;
        case Orientation.LandscapeInverted: rotation = 270; break;
        }

        return (720 + camera.orientation + rotation) % 360
    }
    property int captureOrientation
    property int pageRotation
    property bool orientationTransitionRunning

    property alias camera: camera
    property QtObject viewfinder

    readonly property bool recording: active && camera.videoRecorder.recorderState == CameraRecorder.RecordingState

    property bool _unload

    property bool touchFocusSupported: (camera.focus.focusMode == Camera.FocusAuto
                                        || camera.focus.focusMode == Camera.FocusContinuous)
                                       && camera.captureMode != Camera.CaptureVideo

    // not bound to focusTimer.running, restarting timer shouldn't exit tap focus mode temporarily and lose focus state
    property bool tapFocusActive
    property bool _captureOnFocus
    property real _captureCountdown

    property bool reallyWideScreen: (Screen.height / Screen.width) >= 2.0
    // wide screen can move 4:3 viewfinder a little lower and avoid overlap with top&bottom buttons
    readonly property real viewfinderOffset: Math.min(0,
                                                      isPortrait ? (focusArea.width - height) / 2
                                                                 : (focusArea.width - width) / 2)
                                             + ((reallyWideScreen && (focusArea.width / focusArea.height <= 1.4))
                                                ? Theme.itemSizeLarge + (isPortrait ? Screen.topCutout.height : 0)
                                                : 0)

    readonly property bool isPortrait: orientation == Orientation.Portrait
                || orientation == Orientation.PortraitInverted
    readonly property bool effectiveActive: (active || recording) && _applicationActive && pageStack.depth < 2

    readonly property bool _canCapture: {
        switch (camera.captureMode) {
            case Camera.CaptureStillImage: 
                return camera.imageCapture.ready
            case Camera.CaptureVideo:
                return camera.videoRecorder.recorderStatus >= CameraRecorder.LoadedStatus 
                    && captureOverlay != null && captureOverlay._recSecsRemaining > 0
            default: 
                return false
        }
    }

    property bool _captureQueued
    property bool captureBusy
    onCaptureBusyChanged: {
        if (!captureBusy && _captureQueued) {
            _captureQueued = false
            camera.captureImage()
        }
    }

    property bool handleVolumeKeys: camera.imageCapture.ready
                                    && keysResource.acquired
                                    && camera.captureMode == Camera.CaptureStillImage
                                    && !captureView._captureOnFocus
    property bool captureOnVolumeRelease

    onHandleVolumeKeysChanged: {
        if (!handleVolumeKeys)
            captureOnVolumeRelease = false
    }

    readonly property bool _mirrorViewfinder: camera.position === Camera.FrontFace
    readonly property bool _horizontalMirror: _mirrorViewfinder && camera.orientation % 180 == 0
    readonly property bool _verticalMirror: _mirrorViewfinder && camera.orientation % 180 != 0

    readonly property bool _applicationActive: Qt.application.state == Qt.ApplicationActive
    on_ApplicationActiveChanged: if (_applicationActive) flashlightServiceProbe.checkFlashlightServiceStatus()

    readonly property string deviceId: Settings.deviceId

    property var captureOverlay: null

    signal recordingStopped(url url, string mimeType)
    signal loaded
    signal captured

    Item {
        id: captureSnapshot

        property alias sourceItem: captureSnapshotEffect.sourceItem
        visible: false
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width*captureSnapshotEffect.scale
        height: parent.height*captureSnapshotEffect.scale

        ShaderEffectSource {
            id: captureSnapshotEffect

            hideSource: false
            live: false
            scale: 0.4
            anchors.centerIn: parent
            width: isPortrait ? captureView.width : captureView.height
            height: isPortrait ? captureView.height : captureView.width
            rotation: -captureView.pageRotation
        }
    }

    function setFocusPoint(point) {
        focusTimer.restart()
        camera.unlock()
        tapFocusActive = true
        camera.focus.customFocusPoint = point
        camera.searchAndLock()
    }

    function _resetFocus() {
        focusTimer.running = false
        tapFocusActive = false
        camera.unlock()
    }

    function _triggerCapture() {
        // avoid duplicate capture if volume key and some other key trigger (e.g. shutter)
        captureOnVolumeRelease = false

        if (captureTimer.running) {
            captureTimer.reset()
        } else if (startRecordTimer.running) {
            startRecordTimer.running = false
        } else if (camera.videoRecorder.recorderState == CameraRecorder.RecordingState) {
            camera.videoRecorder.stop()
        } else if (_canCapture) {
            if (Settings.mode.timer != 0) {
                microphoneWarningNotification.publishIfNeeded()
                captureTimer.restart()
            } else if (camera.captureMode == Camera.CaptureStillImage) {
                camera.captureImage()
            } else {
                microphoneWarningNotification.publishIfNeeded()
                camera.record()
            }
        }
    }


    function _pickViewfinderResolution(resolutions, aspectRatio) {
        var ratio
        if (aspectRatio === CameraConfigs.AspectRatio_16_9) {
            ratio = 16.0/9.0
        } else { // CameraConfigs.AspectRatio_4_3
            ratio = 4.0/3.0
        }

        if (resolutions && resolutions.length > 0) {
            var selectedPixels = 0
            var selectedIndex = 0
            var targetWidth = Math.round(Screen.width * ratio)
            for (var i = 0; i < resolutions.length; i++) {
                var resolution = resolutions[i]
                if (resolution.height === Screen.width && resolution.width === targetWidth) {
                    return resolution
                }
            }
            return _pickResolution(resolutions, aspectRatio)
        }
        return "-1x-1"
    }

    function aspectRatioToFraction(aspectRatio) {

        var ratio = 4.0/3.0
        if (aspectRatio === CameraConfigs.AspectRatio_16_9) {
            ratio = 16.0/9.0
        } else if (aspectRatio !== CameraConfigs.AspectRatio_4_3) {
            console.warn("Unknown aspect ratio", aspectRatio)
        }
        return ratio
    }

    function _pickResolution(resolutions, aspectRatio) {

        var ratio = aspectRatioToFraction(aspectRatio)

        if (resolutions && resolutions.length > 0) {
            var selectedPixels = 0
            var selectedIndex = -1
            for (var i = 0; i < resolutions.length; i++) {
                var resolution = resolutions[i]
                var pixels = resolution.width * resolution.height
                if (Math.abs(ratio - resolution.width/resolution.height) < 0.05 && pixels > selectedPixels) {
                    selectedPixels = pixels
                    selectedIndex = i
                }
            }

            if (selectedIndex >= 0) {
                return resolutions[selectedIndex]
            }
        }
        return "-1x-1"
    }

    Notification {
        id: microphoneWarningNotification

        function publishIfNeeded() {
            if (camera.captureMode == Camera.CaptureVideo && !AccessPolicy.microphoneEnabled) {
                microphoneWarningNotification.publish()
            }
        }

        urgency: Notification.Critical
        //: %1 is an operating system name without the OS suffix
        //% "Camera audio won't be recorded, microphone disabled by %1 Device Manager"
        body: qsTrId("jolla-camera-la-microphone_disallowed_by_policy")
            .arg(aboutSettings.baseOperatingSystemName)
    }

    onEffectiveIsoChanged: {
        if (effectiveIso == 0) {
            camera.exposure.setAutoIsoSensitivity()
        } else {
            camera.exposure.manualIso = Settings.mode.iso
        }
    }

    on_CanCaptureChanged: {
        if (!_canCapture) {
            startRecordTimer.running = false
        }
    }

    Component.onCompleted: {
        flashlightServiceProbe.checkFlashlightServiceStatus()
        loadOverlay()
    }

    onDeviceIdChanged: {
        _resetFocus()
        captureTimer.reset()
        Settings.global.deviceId = Settings.deviceId
        camera.deviceId = Settings.deviceId
        Settings.global.position = camera.position
        if (camera.position === Camera.BackFace) {
            Settings.global.previousBackFacingDeviceId = camera.deviceId
        }
    }

    onEffectiveActiveChanged: {
        qrFilter.clearResult()

        if (!effectiveActive) {
            _resetFocus()
            captureTimer.reset()
        }
    }

    Timer {
        // prevent video recording continuing forever in the background
        running: recording && !effectiveActive
        interval: 60*1000
        onTriggered: camera.videoRecorder.stop()
    }

    Timer {
        id: reloadTimer

        interval: 1000
        running: captureView._unload && (camera.cameraStatus === Camera.UnloadedStatus
                                         || camera.cameraStatus === Camera.CameraError)
        onTriggered: {
            captureView._unload = false
        }
    }

    Timer {
        id: reactivateTimer

        property int retryCounter
        readonly property bool abort: retryCounter >= 5

        interval: 1000
        running: camera.cameraStatus == Camera.LoadingStatus && !abort
        onTriggered: {
            // Try re-activate when stuck in loading status for 1sec.
            active = false
            active = true
            ++retryCounter
        }
    }

    NonGraphicalFeedback {
        id: shutterEvent
        event: "camera_shutter"
    }

    NonGraphicalFeedback {
        id: recordStartEvent
        event: "video_record_start"
    }

    Timer {
        id: startRecordTimer

        interval: 200
        onTriggered: {
            captureOverlay.writeMetaData()
            camera.videoRecorder.record()
            if (camera.videoRecorder.recorderState == CameraRecorder.RecordingState) {
                camera.videoRecorder.recorderStateChanged.connect(camera._finishRecording)
                extensions.disableNotifications(captureView, true)
            }
        }
    }

    SequentialAnimation {
        id: captureTimer

        property bool resetCameraOnStop

        function reset() {
            if (resetCameraOnStop) {
                _resetFocus()
                resetCameraOnStop = false
            }
            stop()
        }

        NumberAnimation {
            duration: Settings.mode.timer * 1000
            from: Settings.mode.timer
            to: 0
            easing.type: Easing.Linear
            target: captureView
            property: "_captureCountdown"
        }
        ScriptAction {
            script: {
                if (camera.captureMode == Camera.CaptureStillImage) {
                    if (camera.focusPointMode == Camera.FocusPointAuto) {
                        camera.searchAndLock()
                    }
                    camera.captureImage()
                } else {
                    camera.record()
                }

                if (captureTimer.resetCameraOnStop) {
                    _resetFocus()
                    captureTimer.resetCameraOnStop = false
                }
            }
        }
    }

    NonGraphicalFeedback {
        id: recordStopEvent
        event: "video_record_stop"
    }

    onRecordingStopped: {
        if (captureModel) {
            captureModel.appendCapture(url, mimeType)
        }
    }

    Connections {
        target: CameraConfigs
        onReadyChanged: {
            // Reset flash torch mode if it's not supported
            if (camera.captureMode === Camera.CaptureVideo
                    && CameraConfigs.supportedFlashModes.indexOf(Settings.mode.flash) === -1) {
                Settings.mode.flash = Camera.FlashOff
            }
        }
    }

    Camera {
        id: camera

        function lockAutoFocus() {
            captureOverlay.closeMenus()
            // timed capture locks when timer triggers
            if (camera.captureMode == Camera.CaptureStillImage
                    && focus.focusMode != Camera.FocusInfinity
                    && focus.focusMode != Camera.FocusHyperfocal
                    && camera.lockStatus == Camera.Unlocked
                    && focus.focusPointMode == Camera.FocusPointAuto
                    && Settings.mode.timer == 0) {
                camera.searchAndLock()
            }
        }

        function unlockAutoFocus() {
            if (camera.captureMode == Camera.CaptureStillImage
                    && focus.focusMode != Camera.FocusInfinity
                    && focus.focusMode != Camera.FocusHyperfocal
                    && focus.focusPointMode == Camera.FocusPointAuto) {
                camera.unlock()
            }
        }

        function captureImage() {
            if (camera.lockStatus != Camera.Searching) {
                _completeCapture()
            } else {
                captureView._captureOnFocus = true
            }
        }

        function record() {
            videoRecorder.outputLocation = Settings.videoCapturePath("mp4")
            startRecordTimer.running = true
            recordStartEvent.play()
        }

        function _completeCapture() {
            if (captureBusy) {
                _captureQueued = true
                return
            }

            captureBusy = true
            captureOverlay.writeMetaData()

            camera.imageCapture.captureToLocation(Settings.photoCapturePath('jpg'))

            if (focusTimer.running) {
                focusTimer.restart()
            }
        }

        function _finishRecording() {
            if (videoRecorder.recorderState == CameraRecorder.StoppedState) {
                videoRecorder.recorderStateChanged.disconnect(_finishRecording)
                extensions.disableNotifications(captureView, false)
                var finalUrl = Settings.completeCapture(videoRecorder.outputLocation)
                if (finalUrl != "") {
                    captureView.recordingStopped(finalUrl, videoRecorder.mediaContainer)
                }
                recordStopEvent.play()
            }
        }

        property bool hasCameraOnBothSides
        property string frontFacingDeviceId
        property var backFacingCameras

        // On some adaptations media booster makes camera initialization fail
        // and Camera must be reloaded, try to do that once when that happens
        property bool needsReload: camera.errorCode === Camera.CameraError
                                   || (camera.cameraState === Camera.UnloadedState
                                       && camera.cameraStatus === Camera.UnloadedStatus)


        onErrorCodeChanged: {
            if (errorCode == Camera.CameraError) {
                captureView._unload = true
            }
        }

        onNeedsReloadChanged: {
            if (needsReload) {
                captureView._unload = true
            }
        }

        deviceId: Settings.deviceId
        captureMode: Settings.global.captureMode == "image" ? Camera.CaptureStillImage
                                                            : Camera.CaptureVideo

        onCaptureModeChanged: {
            // Reset flash mode when changing to video mode
            if (initialized && captureMode === Camera.CaptureVideo) {
                Settings.mode.flash = Camera.FlashOff
            }
            captureView._resetFocus()
        }

        cameraState: {
            if (captureView.effectiveActive && !captureView._unload) {
                if (CameraConfigs.ready) {
                    return Camera.ActiveState
                } else {
                    return Camera.LoadedState
                }
            } else {
                return Camera.UnloadedState
            }
        }

        onCameraStateChanged: {
            if (cameraState == Camera.ActiveState && captureOverlay) {
                captureView.loaded()
            }
        }
        property bool initialized

        onCameraStatusChanged: {
            if (camera.cameraStatus === Camera.ActiveStatus) {
                reactivateTimer.retryCounter = 0
            } else {
                _captureQueued = false
                captureBusy = false
            }

            var backCameras = []
            if (cameraStatus === Camera.LoadedStatus && !initialized) {
                initialized = true
                var hasFrontFace = false
                var hasBackFace = false

                for (var i = 0; i < QtMultimedia.availableCameras.length; i++) {
                    var device = QtMultimedia.availableCameras[i]
                    if (!hasFrontFace && device.position === Camera.FrontFace) {
                        hasFrontFace = true
                        frontFacingDeviceId = device.deviceId
                    } else if (device.position === Camera.BackFace) {
                        hasBackFace = true
                        backCameras.push(device)
                    }
                }

                backFacingCameras = backCameras

                hasCameraOnBothSides = hasFrontFace && hasBackFace

                if (Settings.global.previousBackFacingDeviceId.length === 0 && backCameras.length > 0) {
                    if (backCameras.indexOf(QtMultimedia.defaultCamera.deviceId) >= 0) {
                        Settings.global.previousBackFacingDeviceId = QtMultimedia.defaultCamera.deviceId
                    } else {
                        Settings.global.previousBackFacingDeviceId = backCameras[0].deviceId
                    }
                }

                // Always disable flash torch at startup
                if (captureMode === Camera.CaptureVideo) {
                    Settings.mode.flash = Camera.FlashOff
                }
            }
        }

        imageCapture {
            resolution: _pickResolution(CameraConfigs.supportedImageResolutions, Settings.aspectRatio)

            onImageSaved: {
                // HDR case emits the exposed already on the first image, delay the feedback so user avoids
                // moving the device until it's safe again.
                if (camera.exposure.exposureMode == Camera.ExposureHDR) {
                    shutterEvent.play()
                    captureAnimation.start()
                }

                camera.unlockAutoFocus()
                captureBusy = false

                if (captureModel) {
                    captureModel.appendCapture(path, "image/jpeg")
                }

                Settings.completePhoto(Qt.resolvedUrl(path))
            }
            onImageExposed: {
                if (camera.exposure.exposureMode != Camera.ExposureHDR) {
                    shutterEvent.play()
                    captureAnimation.start()
                } else {
                    flashAnimation.start()
                }
            }
            onCaptureFailed: {
                camera.unlockAutoFocus()
                captureBusy = false
            }
        }
        videoRecorder {
            resolution: _pickResolution(CameraConfigs.supportedVideoResolutions, CameraConfigs.AspectRatio_16_9)

            audioChannels: 2
            audioSampleRate: Settings.global.audioSampleRate
            audioCodec: Settings.global.audioCodec
            videoCodec: Settings.global.videoCodec
            mediaContainer: Settings.global.mediaContainer

            videoEncodingMode: Settings.global.videoEncodingMode
            videoBitRate: Settings.global.videoBitRate
        }
        focus {
            // could expect that locking focus on auto or continous behaves the same, but
            // continuous doesn't work as well
            focusMode: {
                // The cameraStatus doesn't really matter as a precondition but incorporating
                // it ensures the binding is reevaluated when the status changes and the desired
                // focus mode is assigned. Otherwise QtMultimedia may reject a mode as unsupported
                // and default to auto because the binding was evaluated in the unloaded state and
                // real support was unknown at that time.
                if (camera.cameraStatus == Camera.ActiveStatus && tapFocusActive) {
                    return Camera.FocusAuto
                } else if (CameraConfigs.supportedFocusModes.indexOf(Camera.FocusContinuous) >= 0) {
                    return Camera.FocusContinuous
                } else if (CameraConfigs.supportedFocusModes.length > 0) {
                    return CameraConfigs.supportedFocusModes[0]
                } else {
                    return Camera.FocusAuto
                }
            }
            focusPointMode: tapFocusActive ? Camera.FocusPointCustom : Camera.FocusPointAuto
        }
        flash.mode: Settings.mode.flash
        imageProcessing.whiteBalanceMode: {
            var hasFilter = camera.imageProcessing.colorFilter !== CameraImageProcessing.ColorFilterNone
            return hasFilter ? CameraImageProcessing.WhiteBalanceAuto : Settings.global.whiteBalance
        }

        exposure {
            exposureMode: Settings.mode.exposureMode
            exposureCompensation: Settings.global.exposureCompensation / 2.0
            meteringMode: Settings.mode.meteringMode
        }

        viewfinder {
            resolution: {
                var resolutions = CameraConfigs.supportedViewfinderResolutions
                if (resolutions.length > 0) {
                    return _pickViewfinderResolution(resolutions, Settings.aspectRatio)
                }
                return "-1x-1"
            }

            // Let gst-droid decide the best framerate
        }

        metaData {
            orientation: captureView.captureOrientation
            cameraModel: deviceInfo.prettyName
            cameraManufacturer: deviceInfo.manufacturer
        }

        focus.onFocusModeChanged: camera.unlock()

        onLockStatusChanged: {
            if (lockStatus != Camera.Searching && captureView._captureOnFocus) {
                captureView._captureOnFocus = false
                camera._completeCapture()
            }
        }
    }

    Binding {
        target: CameraConfigs
        property: "camera"
        value: camera
    }

    DeviceInfo {
        id: deviceInfo
    }

    CameraExtensions {
        id: extensions
    }

    Binding {
        target: captureView.viewfinder
        property: "source"
        value: camera
    }

    Rectangle {
        id: flashRectangle

        anchors.fill: parent
        color: "white"
        opacity: 0
    }

    SequentialAnimation {
        id: flashAnimation

        PropertyAction {
            target: flashRectangle
            property: "visible"
            value: true
        }
        OpacityAnimator {
            target: flashRectangle
            from: Theme.opacityHigh
            to: 0
            duration: 250
        }
        PropertyAction {
            target: flashRectangle
            property: "visible"
            value: false
        }
    }

    SequentialAnimation {
        id: captureAnimation

        PropertyAction {
            target: captureSnapshot
            property: "sourceItem"
            value: viewfinder
        }
        ScriptAction {
            script: captureSnapshotEffect.scheduleUpdate()
        }
        PropertyAction {
            target: captureSnapshot
            property: "x"
            value: 0
        }
        PropertyAction {
            target: captureSnapshot
            property: "visible"
            value: true
        }
        PropertyAction {
            target: viewfinder
            property: "opacity"
            value: 0
        }
        PauseAnimation {
            duration: 100
        }
        ParallelAnimation {
            XAnimator {
                target: captureSnapshot
                from: 0
                to: captureView.isPortrait ? -captureView.height : -captureView.width
                duration: 300
                easing.type: Easing.InQuad
            }
            OpacityAnimator {
                target: viewfinder
                to: 1
                duration: 300
            }
        }
        PropertyAction {
            target: captureSnapshot
            property: "visible"
            value: false
        }
        PropertyAction {
            target: captureSnapshot
            property: "sourceItem"
            value: null
        }
        ScriptAction {
            script: captureView.captured()
        }
    }

    property Component overlayComponent
    property var overlayIncubator

    function loadOverlay() {
        overlayComponent = Qt.createComponent("CaptureOverlay.qml", Component.Asynchronous, captureView)
        if (overlayComponent) {
            if (overlayComponent.status === Component.Ready) {
                incubateOverlay()
            } else if (overlayComponent.status === Component.Loading) {
                overlayComponent.statusChanged.connect(
                    function(status) {
                        if (overlayComponent) {
                            if (status == Component.Ready) {
                                incubateOverlay()
                            } else if (status == Component.Error) {
                                console.warn(overlayComponent.errorString())
                            }
                        }
                    })
            } else {
                console.log("Error loading capture overlay", overlayComponent.errorString())
            }
        }
    }

    function incubateOverlay() {
        overlayIncubator = overlayComponent.incubateObject(captureView, {
                                                                      "captureView": captureView,
                                                                      "camera": camera,
                                                                      "focusArea": focusArea
                                                                  }, Qt.Asynchronous)
        overlayIncubator.onStatusChanged = function(status) {
            if (status == Component.Ready) {
                captureOverlay = overlayIncubator.object
                captureOverlay.orientationTransitionRunning = Qt.binding(function () {
                    return captureView.orientationTransitionRunning
                })
                overlayFadeIn.start()
                overlayIncubator = null
                if (camera.cameraState == Camera.ActiveState && captureOverlay) {
                    captureView.loaded()
                }
            } else if (status == Component.Error) {
                console.log("Failed to create capture overlay")
                overlayIncubator = null
            }
        }
    }

    FadeAnimator {
        id: overlayFadeIn

        target: captureOverlay
        to: 1.0
        duration: 100
    }

    Item {
        id: focusArea

        width: Screen.width
               * camera.viewfinder.resolution.width
               / camera.viewfinder.resolution.height
        height: Screen.width

        rotation: -captureView.viewfinderOrientation
        anchors {
            centerIn: parent
            verticalCenterOffset: isPortrait ? viewfinderOffset : 0
            horizontalCenterOffset: isPortrait ? 0 : viewfinderOffset
        }
        opacity: captureOverlay ? 1.0 - captureOverlay.settingsOpacity : 1.0

        Repeater {
            model: camera.focus.focusZones
            delegate: Item {
                x: focusArea.width * (captureView._horizontalMirror
                                      ? 1 - area.x - area.width
                                      : area.x)
                y: focusArea.height * (captureView._verticalMirror
                                      ? 1 - area.y - area.height
                                      : area.y)
                width: focusArea.width * area.width
                height: focusArea.height * area.height

                visible: status != Camera.FocusAreaUnused && camera.focus.focusPointMode == Camera.FocusPointCustom

                Rectangle {
                    id: focusRectangle

                    width: Math.min(parent.width, parent.height)
                    height: width
                    anchors.centerIn: parent
                    radius: width / 2
                    border {
                        width: Math.round(Theme.pixelRatio * 2)
                        color: status == Camera.FocusAreaFocused
                               ? (Theme.colorScheme == Theme.LightOnDark
                                  ? Theme.highlightColor
                                  : Theme.highlightFromColor(Theme.highlightColor, Theme.LightOnDark))
                               : "white"
                    }
                    color: "#00000000"
                }
            }
        }
    }

    Timer {
        id: focusTimer

        interval: 5000
        onTriggered: {
            if (!captureTimer.running) {
                captureView._resetFocus()
            } else {
                captureTimer.resetCameraOnStop = true
            }
        }
    }

    Keys.onVolumeDownPressed: {
        if (handleVolumeKeys && !event.isAutoRepeat) {
            camera.lockAutoFocus()
            captureOnVolumeRelease = true
        }
    }
    Keys.onVolumeUpPressed: {
        if (handleVolumeKeys && !event.isAutoRepeat) {
            camera.lockAutoFocus()
            captureOnVolumeRelease = true
        }
    }

    function supportedKey(key) {
        return key === Qt.Key_CameraFocus
                || key === Qt.Key_Camera
                || key === Qt.Key_VolumeDown
                || key === Qt.Key_VolumeUp
    }

    Keys.onPressed: {
        if (supportedKey(event.key)) {
            event.accepted = true
        }

        if (event.isAutoRepeat) {
            return
        }

        if (event.key == Qt.Key_CameraFocus) {
            camera.lockAutoFocus()
        } else if (event.key == Qt.Key_Camera) {
            captureView._triggerCapture() // key having half-pressed state too so can capture already here
        }
    }

    Keys.onReleased: {
        if (supportedKey(event.key)) {
            event.accepted = true
        }

        if (event.isAutoRepeat) {
            return
        }

        if (event.key == Qt.Key_CameraFocus) {
            // note: forces capture if it was still pending. debatable if that should be allowed to finish.
            camera.unlockAutoFocus()
        } else if ((event.key == Qt.Key_VolumeDown || event.key == Qt.Key_VolumeUp)
                   && captureOnVolumeRelease && handleVolumeKeys) {
            captureView._triggerCapture()
        }
    }

    Permissions {
        enabled: captureView.activeFocus
                    && camera.captureMode == Camera.CaptureStillImage
                    && camera.cameraState == Camera.ActiveState
        autoRelease: true
        applicationClass: "camera"

        Resource {
            id: keysResource

            type: Resource.ScaleButton
            optional: true
        }
    }

    Permissions {
        enabled: Qt.application.state == Qt.ApplicationActive
        autoRelease: true
        applicationClass: "camera"

        Resource {
            type: Resource.SnapButton
            optional: true
        }
    }

    DBusInterface {
        id: flashlightServiceProbe

        service: "org.freedesktop.DBus"
        path: "/org/freedesktop/DBus"
        iface: "org.freedesktop.DBus"
        property bool flashlightServiceActive
        onFlashlightServiceActiveChanged: {
            if (flashlightServiceActive) {
                if (flashlightComponentLoader.sourceComponent == null
                        || flashlightComponentLoader.sourceComponent == undefined) {
                    flashlightComponentLoader.sourceComponent = flashlightComponent
                } else {
                    flashlightComponentLoader.item.toggleFlashlight()
                }
            }
        }
        function checkFlashlightServiceStatus() {
            var probe = flashlightServiceProbe // cache id resolution to avoid context destruction issues
            typedCall('NameHasOwner',
                      { 'type': 's', 'value': 'com.jolla.settings.system.flashlight' },
                      function(result) {
                          // twiddle so that the change-handler is invoked
                          probe.flashlightServiceActive = false
                          probe.flashlightServiceActive = result
                      },
                      function() {
                          // assume true in failed case, to ensure we turn it off
                          probe.flashlightServiceActive = false
                          probe.flashlightServiceActive = true
                      })
        }
    }

    Loader { id: flashlightComponentLoader }

    Component {
        id: flashlightComponent

        DBusInterface {
            id: flashlightDbus

            bus: DBusInterface.SessionBus
            service: "com.jolla.settings.system.flashlight"
            path: "/com/jolla/settings/system/flashlight"
            iface: "com.jolla.settings.system.flashlight"
            Component.onCompleted: toggleFlashlight()
            function toggleFlashlight() {
                var isOn = flashlightDbus.getProperty("flashlightOn")
                if (isOn) flashlightDbus.call("toggleFlashlight")
            }
        }
    }

    AboutSettings {
        id: aboutSettings
    }
}

// SPDX-FileCopyrightText: 2013 - 2024 Jolla Ltd.
// SPDX-FileCopyrightText: 2019 Open Mobile Platform LLC.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import QtMultimedia 5.4
import Nemo.KeepAlive 1.2
import Sailfish.Silica 1.0
import Sailfish.Media 1.0
import Sailfish.Policy 1.0
import com.jolla.camera 1.0
import com.jolla.settings.system 1.0
import "capture"
import "gallery"

Page {
    id: page

    property alias viewfinder: captureView.viewfinder
    property bool galleryActive
    property url galleryView
    readonly property bool captureModeActive: switcherView.currentIndex === 1
    readonly property bool galleryVisible: galleryLoader.visible
    readonly property int galleryIndex: galleryLoader.item ? galleryLoader.item.currentIndex : 0
    readonly property QtObject captureModel: galleryLoader.item ? galleryLoader.item.captureModel : null

    function resetZoom() {
        switcherView.resetZoom()
    }

    function returnToCaptureMode() {
        switcherView.returnToCaptureMode()
    }

    palette.colorScheme: Theme.LightOnDark

    _opaqueBackground: true

    allowedOrientations: captureView.inButtonLayout ? page.orientation : Orientation.All
    cutoutMode: CutoutMode.FullScreen

    softKeys: SoftKeys {
        // Back from the camera roll returns to the viewfinder; Silica's own Back
        // would leave the application from there.
        left: page.galleryActive ? backToViewfinder : null
        center: page.captureModeActive ? captureAction : null
        menu: [
            modeEntry, flashEntry, timerEntry, gridEntry,
            whiteBalanceEntry, isoEntry, cameraRollEntry, resetEntry
        ]
    }

    SoftKeyAction {
        id: backToViewfinder

        actionType: SoftKeyAction.BackAction
        onTriggered: page.returnToCaptureMode()
    }

    SoftKeyAction {
        id: captureAction

        text: {
            if (captureView.recording || captureView.startingRecording) {
                //% "Stop"
                return qsTrId("camera-la-softkey_stop")
            } else if (captureView.camera.captureMode == Camera.CaptureVideo) {
                //% "Record"
                return qsTrId("camera-la-softkey_record")
            }
            //% "Capture"
            return qsTrId("camera-la-softkey_capture")
        }
        enabled: captureView.canCaptureByKey
        onTriggered: captureView.captureByKey()
    }

    // The Options entries. Listed by visible when the menu opens; enabled is read live,
    // so they gray out if a capture starts while the menu is up.
    KeypadMenuEntry {
        id: modeEntry

        text: Settings.global.captureMode === "image"
              //% "Video mode"
              ? qsTrId("camera-me-video_mode")
              //% "Photo mode"
              : qsTrId("camera-me-photo_mode")
        enabled: captureView.idle
        onClicked: Settings.global.captureMode = Settings.global.captureMode === "image" ? "video" : "image"
    }

    KeypadMenuEntry {
        id: flashEntry

        //% "Flash"
        text: qsTrId("camera-me-flash")
        visible: CameraConfigs.supportedFlashModes.length > 1
        enabled: captureView.idle
        onClicked: page._pickValue(text, CameraConfigs.supportedFlashModes, Settings.mode.flash,
                                   Settings.flashText, Settings.flashIcon,
                                   function (value) { Settings.mode.flash = value })
    }

    KeypadMenuEntry {
        id: timerEntry

        //% "Timer"
        text: qsTrId("camera-me-timer")
        enabled: captureView.idle
        onClicked: page._pickValue(text, [0, 3, 10, 15], Settings.mode.timer,
                                   Settings.timerText, Settings.timerIcon,
                                   function (value) { Settings.mode.timer = value })
    }

    KeypadMenuEntry {
        id: gridEntry

        text: Settings.global.viewfinderGrid === "none"
              //% "Show grid"
              ? qsTrId("camera-me-grid_show")
              //% "Hide grid"
              : qsTrId("camera-me-grid_hide")
        enabled: captureView.idle
        onClicked: Settings.global.viewfinderGrid = Settings.global.viewfinderGrid === "none" ? "thirds" : "none"
    }

    KeypadMenuEntry {
        id: whiteBalanceEntry

        //% "White balance"
        text: qsTrId("camera-me-white_balance")
        // With a color filter the camera forces automatic white balance, as the touch menu's disabling says.
        visible: CameraConfigs.supportedWhiteBalanceModes.length > 1
                 && captureView.camera.imageProcessing.colorFilter === CameraImageProcessing.ColorFilterNone
        enabled: captureView.idle
        onClicked: page._pickValue(text, CameraConfigs.supportedWhiteBalanceModes, Settings.global.whiteBalance,
                                   Settings.whiteBalanceText, Settings.whiteBalanceIcon,
                                   function (value) { Settings.global.whiteBalance = value })
    }

    KeypadMenuEntry {
        id: isoEntry

        //% "Light sensitivity"
        text: qsTrId("camera-me-light_sensitivity")
        visible: CameraConfigs.supportedIsoSensitivities.length > 1
        enabled: captureView.idle
        onClicked: page._pickValue(text, CameraConfigs.supportedIsoSensitivities, Settings.mode.iso,
                                   Settings.isoText, null,
                                   function (value) { Settings.mode.iso = value })
    }

    KeypadMenuEntry {
        id: cameraRollEntry

        //% "Camera roll"
        text: qsTrId("camera-me-camera_roll")
        enabled: captureView.idle
        onClicked: switcherView.moveToGallery()
    }

    KeypadMenuEntry {
        id: resetEntry

        //% "Reset settings"
        text: qsTrId("camera-me-reset_settings")
        visible: !Settings.defaultSettings
        enabled: captureView.idle
        onClicked: Settings.reset()
    }

    function _pickValue(title, values, current, textFor, iconFor, apply) {
        pageStack.push(Qt.resolvedUrl("settings/ValuePickerPage.qml"), {
                           "title": title, "values": values, "current": current,
                           "textFor": textFor, "iconFor": iconFor, "apply": apply
                       })
    }

    Item {
        parent: page.parent

        width: page.width
        height: page.height
        rotation: page.rotation

        anchors.centerIn: parent
        z: -1

        Rectangle {
            x: galleryItem.x
            y: galleryItem.y
            width: galleryItem.width
            height: galleryItem.height
            color: "black"
            visible: galleryItem.PagedView.exposed
        }
    }

    PagedView {
        id: switcherView

        readonly property bool transitioning: moving || returnToCaptureModeTimeout.running

        function resetZoom() {
            captureView.camera.digitalZoom = 1.0
        }

        function returnToCaptureMode() {
            if (Qt.application.active) {
                if (pageStack.currentPage === page) {
                    returnToCaptureModeTimeout.restart()
                    switcherView.currentIndex = 1
                }
            } else {
                pageStack.pop(page, PageStackAction.Immediate)
                moveTo(1, PagedView.Immediate)
            }
        }

        // The Camera roll entry: what the touch swipe ends in. The loader is sourced
        // as the transition handler does it, and the timeout marks the move as a
        // transition so galleryActive updates when it settles.
        function moveToGallery() {
            if (galleryLoader.source == "") {
                galleryLoader.setSource(page.galleryView, { page: page })
            }
            returnToCaptureModeTimeout.restart()
            switcherView.currentIndex = 0
        }

        Timer {
            id: returnToCaptureModeTimeout

            interval: 300 //switcherView.highlightMoveDuration
        }

        width: page.width
        height: page.height
        wrapMode: PagedView.NoWrap

        interactive: (!galleryLoader.item || !galleryLoader.item.positionLocked)
                     && !captureView.recording
        currentIndex: 1
        focus: true

        Keys.onPressed: {
            if (!event.isAutoRepeat && event.key == Qt.Key_Camera) {
                switcherView.returnToCaptureMode()
            }
        }

        model: VisualItemModel {
            Item {
                id: galleryItem

                width: page.width
                height: page.height

                Loader {
                    id: galleryLoader

                    anchors.fill: parent

                    asynchronous: true
                    visible: switcherView.moving || page.galleryActive || returnToCaptureModeTimeout.running
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    size: BusyIndicatorSize.Large
                    running: galleryLoader.status == Loader.Loading
                }
            }

            CaptureView {
                id: captureView

                readonly property real _viewfinderPosition: orientation == Orientation.Portrait
                                                            || orientation == Orientation.Landscape
                                                            ? parent.x + x
                                                            : -parent.x - x
                width: page.width
                height: page.height

                active: true

                orientation: page.orientation
                pageRotation: page.rotation
                captureModel: page.captureModel
                orientationTransitionRunning: page.orientationTransitionRunning

                visible: switcherView.moving || captureView.active

                onLoaded: {
                    if (galleryLoader.source == "") {
                        galleryLoader.setSource(galleryView, { page: page })
                    }
                }

                CameraRollHint { z: 2 }
                CameraModeHint { z: 2 }

                Binding {
                    target: captureView.viewfinder
                    property: "x"
                    value: captureView.isPortrait
                           ? captureView._viewfinderPosition
                           : 0
                }

                Binding {
                    target: captureView.viewfinder
                    property: "y"
                    value: !captureView.isPortrait
                           ? captureView._viewfinderPosition
                             + (page.orientation == Orientation.Landscape
                                ? captureView.viewfinderOffset : -captureView.viewfinderOffset)
                           : (page.orientation == Orientation.Portrait ? captureView.viewfinderOffset
                                                                       : -captureView.viewfinderOffset)
                }
            }
        }

        onCurrentItemChanged: {
            if (!transitioning) {
                page.galleryActive = galleryItem.PagedView.isCurrentItem
                captureView.active = captureView.PagedView.isCurrentItem
            }
        }

        onTransitioningChanged: {
            if (!transitioning) {
                page.galleryActive = galleryItem.PagedView.isCurrentItem
                captureView.active = captureView.PagedView.isCurrentItem
            } else if (captureView.active) {
                if (galleryLoader.source == "") {
                    galleryLoader.setSource("gallery/GalleryView.qml", { page: page })
                } else if (galleryLoader.item) {
                    galleryLoader.item._positionViewAtBeginning()
                }
            }
        }
    }

    DisabledByMdmView {
        //% "Camera"
        activity: qsTrId("sailfish_browser-la-camera");
        enabled: !AccessPolicy.cameraEnabled
    }

    DisplayBlanking {
        preventBlanking: (galleryLoader.item && galleryLoader.item.playing)
                         || captureView.camera.videoRecorder.recorderState == CameraRecorder.RecordingState
    }
}

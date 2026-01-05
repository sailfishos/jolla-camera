// SPDX-FileCopyrightText: 2018 - 2021 Jolla Ltd.
// SPDX-FileCopyrightText: 2020 Open Mobile Platform LLC.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import Sailfish.Silica 1.0
import com.jolla.camera 1.0
import Nemo.DBus 2.0

CameraPage {
    id: page

    galleryView: Qt.resolvedUrl("gallery/MainGalleryView.qml")

    Binding {
        target: window
        property: "galleryActive"
        value: page.galleryActive
    }

    Binding {
        target: window
        property: "galleryVisible"
        value: page.galleryVisible
    }

    Binding {
        target: window
        property: "galleryIndex"
        value: page.galleryIndex
    }

    Binding {
        target: window
        property: "captureModel"
        value: page.captureModel
    }

    Timer {
        running: Qt.application.state != Qt.ApplicationActive && !captureModeActive
        interval: 15 * 60 * 1000
        onTriggered: returnToCaptureMode()
    }

    Timer {
        running: Qt.application.state != Qt.ApplicationActive || !captureModeActive
        interval: 5 * 60 * 1000
        onTriggered: resetZoom()
    }

    DBusAdaptor {
        iface: "com.jolla.camera.ui"
        service: "com.jolla.camera"
        path: "/"

        signal showViewfinder(variant args)
        onShowViewfinder: {
            page.returnToCaptureMode()
            window.activate()
        }

        signal showFrontViewfinder(bool switchToImageMode)
        onShowFrontViewfinder: {
            if (switchToImageMode) {
                Settings.global.captureMode = "image"
            }
            if (Settings.global.frontFacingDeviceId !== "") {
                Settings.deviceId = Settings.global.frontFacingDeviceId
            } else {
                console.warn("No front camera detected")
            }

            page.returnToCaptureMode()
            window.activate()
        }
    }
}

// SPDX-FileCopyrightText: 2013 - 2024 Jolla Ltd.
// SPDX-FileCopyrightText: 2020 - 2021 Open Mobile Platform LLC.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.1
import QtMultimedia 5.0
import Amber.QrFilter 1.0
import Sailfish.Silica 1.0
import com.jolla.camera 1.0
import "pages"

ApplicationWindow {
    id: window

    property var captureModel: null
    property bool galleryActive
    property bool galleryVisible
    property int galleryIndex

    allowedOrientations: defaultAllowedOrientations
    _defaultPageOrientations: Orientation.All
    _defaultLabelFormat: Text.PlainText

    cover: Qt.resolvedUrl("cover/CameraCover.qml")

    initialPage: Component {
        MainCameraPage {
            viewfinder: videoOutput
        }
    }

    // viewfinder background
    Rectangle {
        parent: window
        anchors.fill: parent
        z: -1
        color: "black"
        visible: (pageStack.depth < 2 && !pageStack.busy) || !galleryActive
    }

    ViewfinderOutput {
        id: videoOutput

        z: -1
        width: window.width
        height: window.height

        Behavior on y {
            enabled: !galleryVisible
            NumberAnimation { duration: 150; easing.type: Easing.InOutQuad }
        }

        filters: [ qrFilter ]
    }

    QrFilter {
        id: qrFilter

        active: Settings.global.qrFilterEnabled
                && Settings.global.captureMode === "image"
                && Settings.global.position === Camera.BackFace

        onActiveChanged: qrFilter.clearResult()
    }
}

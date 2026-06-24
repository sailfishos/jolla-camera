// SPDX-FileCopyrightText: 2021 - 2024 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0
import com.jolla.camera 1.0

Grid {
    id: root

    property string currentDeviceId
    property var labels: []
    property alias model: repeater.model
    property int orientation: Qt.Vertical
    property color highlightColor: Theme.colorScheme == Theme.LightOnDark
                                   ? Theme.highlightColor
                                   : Theme.highlightFromColor(Theme.highlightColor, Theme.LightOnDark)

    signal selected(string deviceId)

    columns: orientation === Qt.Vertical ? 1 : repeater.count
    rows: orientation === Qt.Vertical ? repeater.count : 1
    spacing: Theme.paddingMedium

    readonly property bool _supportNotEnabled: !!model && model.length > 1 && labels.length === 0
    on_SupportNotEnabledChanged: {
        if (_supportNotEnabled)
            console.warn("Device supports multiple back cameras, please define dconf /apps/jolla-camera/backCameraLabels")
    }

    Repeater {
        id: repeater

        SilicaItem {
            highlighted: mouseArea.pressed && mouseArea.containsMouse || modelData.deviceId === root.currentDeviceId
            width: Theme.itemSizeExtraSmall
            height: Theme.itemSizeExtraSmall
            visible: cameraLabel.text != ''

            MouseArea {
                id: mouseArea

                anchors.fill: parent
                onClicked: {
                    if (modelData.deviceId !== root.currentDeviceId) {
                        root.selected(modelData.deviceId)
                    }
                }
            }

            Rectangle {
                border {
                    width: Theme._lineWidth
                    color: parent.highlighted ? root.highlightColor : Theme.lightPrimaryColor
                }

                radius: width/2
                anchors.fill: parent
                color: "transparent"
            }

            Label {
                id: cameraLabel

                // TODO: Don't hardcode these values
                text: root.labels.length > model.index ? root.labels[model.index] : ""
                color: parent.highlighted ? root.highlightColor : Theme.lightPrimaryColor
                font.pixelSize: Theme.fontSizeMediumBase
                anchors.verticalCenterOffset: -Theme.pixelRatio
                anchors.centerIn: parent
            }
        }
    }
}

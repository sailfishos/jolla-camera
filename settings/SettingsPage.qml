// SPDX-FileCopyrightText: 2013 - 2022 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import QtMultimedia 5.0
import com.jolla.camera 1.0
import Nemo.Configuration 1.0
import org.nemomobile.systemsettings 1.0
import Sailfish.Silica 1.0
import Sailfish.Policy 1.0
import com.jolla.settings 1.0
import com.jolla.settings.system 1.0

ApplicationSettings {
    function aspectRatioName(aspectRatio) {
        if (aspectRatio === CameraConfigs.AspectRatio_16_9) {
            //: Aspect ratio 16:9
            //% "16:9"
            return qsTrId("camera_settings-me-aspect_ratio_16_9")
        } else if (aspectRatio === CameraConfigs.AspectRatio_4_3) {
            //: Aspect ratio 4:3
            //% "4:3"
            return qsTrId("camera_settings-me-aspect_ratio_4_3")
        } else {
            console.warn("Camera Settings: Unsupported aspect ratio")
        }
    }

    ConfigurationValue {
        id: backCameraAspectRatio

        key: "/apps/jolla-camera/back/image/aspectRatio"
        defaultValue: CameraConfigs.AspectRatio_4_3
    }

    ConfigurationValue {
        id: frontCameraAspectRatio

        key: "/apps/jolla-camera/front/image/aspectRatio"
        defaultValue: CameraConfigs.AspectRatio_4_3
    }

    LocationSettings { id: locationSettings }

    DisabledByMdmBanner {
        active: !AccessPolicy.cameraEnabled
    }

    IconTextSwitch {
        automaticCheck: false
        icon.source: "image://theme/icon-m-gps"
        //: Save GPS coordinates in photos.
        //% "Save location"
        text: qsTrId("camera_settings-la-save_location")
        //% "Save current GPS coordinates in captured photos."
        description: qsTrId("camera_settings-la-save_location_description")
        enabled: AccessPolicy.cameraEnabled
        checked: Settings.global.saveLocationInfo
        onClicked: Settings.global.saveLocationInfo = !Settings.global.saveLocationInfo
    }

    IconTextSwitch {
        automaticCheck: false
        icon.source: "image://theme/icon-m-qr"
        //% "Enable QR-code recognition"
        text: qsTrId("camera_settings-la-enable_qr")
        //% "Detect QR-code via camera."
        description: qsTrId("camera_settings-la-detect_qr_description")
        enabled: AccessPolicy.cameraEnabled
        checked: Settings.global.qrFilterEnabled
        onClicked: Settings.global.qrFilterEnabled = !Settings.global.qrFilterEnabled
    }

    Label {
        //% "Positioning is turned off. Enable it in Settings | Connectivity | Location"
        text: qsTrId("camera_settings-la-enable_location")
        wrapMode: Text.Wrap
        x: Theme.horizontalPageMargin
        width: parent.width - 2*x
        color: Theme.highlightColor
        font.pixelSize: Theme.fontSizeSmall
        visible: !locationSettings.locationEnabled
    }

    ComboBox {
        id: storageCombo

        readonly property int storageStatus: Settings.storagePathStatus
        readonly property string storagePath: Settings.storagePath

        function updateCurrentIndex() {
            if (!partitions.externalStoragesPopulated)
                return

            for (var i = 0; i < menu.children.length; ++i) {
                var item = menu.children[i]
                if (item.hasOwnProperty("__silica_menuitem") && item.visible && item.mountPath == Settings.storagePath) {
                    currentIndex = i
                    currentItem = item
                    return
                }
            }
            currentIndex = -1
        }

        onStorageStatusChanged: updateCurrentIndex()
        onStoragePathChanged: updateCurrentIndex()
        Component.onCompleted: updateCurrentIndex()

        //% "Storage"
        label: qsTrId("camera_settings-cb-storage")
        enabled: AccessPolicy.cameraEnabled
        menu: ContextMenu {
            MenuItem {
                property string mountPath: ""
                //% "Device memory"
                text: qsTrId("camera_settings-la-device_memory")
                onClicked: Settings.storagePath = ""
            }
            MenuItem {
                // This is a placeholder for a card that was previously selected, but is no longer inserted
                property string mountPath: Settings.storagePath

                text: qsTrId("camera_settings-la-memory_card_not_inserted")
                visible: partitions.externalStoragesPopulated && partitions.count == 0 && Settings.storagePath !== ""
                onVisibleChanged: storageCombo.updateCurrentIndex()
                opacity: Theme.opacityLow
            }
            Repeater {
                model: partitions
                delegate: MenuItem {
                    property string mountPath: model.mountPath

                    onMountPathChanged: storageCombo.updateCurrentIndex()
                    enabled: model.status === PartitionModel.Mounted && model.devicePath !== ""
                    text: model.status === PartitionModel.Mounted
                          ? //: the parameter is the capacity of the memory card, e.g. "4.2 GB"
                            //% "Memory card %1"
                            qsTrId("camera_settings-la-memory_card").arg(Format.formatFileSize(model.bytesAvailable))
                          : model.devicePath !== ""
                            ? //% "Memory card not mounted"
                              qsTrId("camera_settings-la-unmounted_memory_card")
                            : //% "Memory card not inserted"
                              qsTrId("camera_settings-la-memory_card_not_inserted")
                    onClicked: Settings.storagePath = model.mountPath
                }
            }
        }
    }

    Label {
        //% "The selected storage is not available. Device memory will be used instead."
        text: qsTrId("camera_settings-la-unwritable")
        visible: Settings.storagePathStatus == Settings.Unavailable
        x: Theme.horizontalPageMargin
        width: parent.width - x*2
        color: Theme.secondaryColor
        font.pixelSize: Theme.fontSizeExtraSmall
        wrapMode: Text.Wrap
    }

    SectionHeader {
        //% "Back camera"
        text: qsTrId("camera-ph-back-camera")
        opacity: AccessPolicy.cameraEnabled ? 1.0 : Theme.opacityLow
    }

    ComboBox {
        //% "Aspect ratio"
        label: qsTrId("camera_settings-la-aspect_ratio")
        enabled: AccessPolicy.cameraEnabled
        currentIndex: backCameraAspectRatio.value

        menu: ContextMenu {
            MenuItem {
                text: aspectRatioName(CameraConfigs.AspectRatio_4_3)
                onClicked: backCameraAspectRatio.value = CameraConfigs.AspectRatio_4_3
            }
            MenuItem {
                text: aspectRatioName(CameraConfigs.AspectRatio_16_9)
                onClicked: backCameraAspectRatio.value = CameraConfigs.AspectRatio_16_9
            }
        }
    }

    SectionHeader {
        //% "Front camera"
        text: qsTrId("camera-he-front-camera")
        opacity: AccessPolicy.cameraEnabled ? 1.0 : Theme.opacityLow
    }

    ComboBox {
        //% "Aspect ratio"
        label: qsTrId("camera_settings-la-aspect_ratio")
        enabled: AccessPolicy.cameraEnabled
        currentIndex: frontCameraAspectRatio.value
        menu: ContextMenu {
            MenuItem {
                text: aspectRatioName(CameraConfigs.AspectRatio_4_3)
                onClicked: frontCameraAspectRatio.value = CameraConfigs.AspectRatio_4_3
            }
            MenuItem {
                text: aspectRatioName(CameraConfigs.AspectRatio_16_9)
                onClicked: frontCameraAspectRatio.value = CameraConfigs.AspectRatio_16_9
            }
        }
    }

    PartitionModel {
        id: partitions

        storageTypes: PartitionModel.External | PartitionModel.ExcludeParents
        onExternalStoragesPopulatedChanged: storageCombo.updateCurrentIndex()
    }
}

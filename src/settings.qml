// SPDX-FileCopyrightText: 2014 - 2022 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import QtMultimedia 5.6
import Nemo.Configuration 1.0
import com.jolla.camera 1.0

SettingsBase {
    property alias mode: modeSettings
    property alias global: globalSettings
    // Camera change goes here, CaptureView updates to global.deviceId
    property string deviceId: global.deviceId

    readonly property int aspectRatio: mode.aspectRatio
    property var viewfinderGridValues: [ "none", "thirds" ]

    readonly property var settingsDefaults: ({
                                                 "iso": 0,
                                                 "timer": 0,
                                                 "viewfinderGrid": "none",
                                                 "exposureMode": Camera.ExposureManual,
                                                 "flash": ((globalSettings.captureMode == "image")
                                                           && (globalSettings.position === Camera.BackFace)
                                                           ? Camera.FlashAuto : Camera.FlashOff)
                                             })

    readonly property bool defaultSettings: modeSettings.iso === settingsDefaults["iso"]
                                            && modeSettings.timer === settingsDefaults["timer"]
                                            && modeSettings.viewfinderGrid === settingsDefaults["viewfinderGrid"]
                                            && modeSettings.exposureMode === settingsDefaults["exposureMode"]
                                            && modeSettings.flash == settingsDefaults["flash"]

    function reset() {
        var basePath = globalSettings.path + "/" + modeSettings.path
        var i
        for (i in settingsDefaults) {
            _singleValue.key = basePath + "/" + i
            _singleValue.value = settingsDefaults[i]
        }
    }

    property ConfigurationValue _singleValue: ConfigurationValue {}

    property ConfigurationGroup _global: ConfigurationGroup {
        id: globalSettings

        path: "/apps/jolla-camera"

        // Note! don't touch this for changing between cameras, see cameraDevice on root
        property string deviceId
        property string previousBackFacingDeviceId
        property string frontFacingDeviceId
        property int position: Camera.BackFace
        property string captureMode: "image"

        // Need to be defined by adaptation to enable multiple back cameras,
        // e.g. normal, macro and wide angle camera labels could be ["1.0", "2.0", "0.6"]
        property var backCameraLabels: []

        property int portraitCaptureButtonLocation: 3
        property int landscapeCaptureButtonLocation: 5

        property string audioCodec: "audio/mpeg, mpegversion=(int)4"
        property int audioSampleRate: 48000
        property string videoCodec: "video/x-h264"
        property string mediaContainer: "video/quicktime, variant=(string)iso"

        property int videoEncodingMode: CameraRecorder.AverageBitRateEncoding
        property int videoBitRate: 12000000

        property bool saveLocationInfo

        property bool qrFilterEnabled: false
        property bool colorFiltersEnabled: false
        property bool colorFiltersAllowed: true

        property int exposureCompensation: 0
        property int whiteBalance: CameraImageProcessing.WhiteBalanceAuto

        property var exposureCompensationValues: [ 4, 3, 2, 1, 0, -1, -2, -3, -4 ]
        property string viewfinderGrid: "none"

        ConfigurationGroup {
            id: modeSettings

            path: {
                var position = globalSettings.position === Camera.FrontFace ? "front" : "back"
                return position + "/" + globalSettings.captureMode
            }

            property int iso: 0
            property int flash: Camera.FlashOff
            property int exposureMode: Camera.ExposureManual
            property int meteringMode: Camera.MeteringMatrix
            property int timer: 0
            property int aspectRatio: -1

            Component.onCompleted: {
                if (aspectRatio === -1) {
                    if (globalSettings.captureMode === "image") {
                        aspectRatio = CameraConfigs.AspectRatio_4_3
                    } else {
                        aspectRatio = CameraConfigs.AspectRatio_16_9
                    }
                }
            }
        }
    }

    function captureModeIcon(mode) {
        switch (mode) {
        case "image": return "image://theme/icon-camera-camera-mode"
        case "video": return "image://theme/icon-camera-video"
        default:  return ""
        }
    }

    function exposureText(exposure) {
        switch (exposure) {
        case -4: return "-2"
        case -3: return "-1.5"
        case -2: return "-1"
        case -1: return "-0.5"
        case 0:  return ""
        case 1:  return "+0.5"
        case 2:  return "+1"
        case 3:  return "+1.5"
        case 4:  return "+2"
        }
    }

    function timerIcon(timer) {
        return timer > 0
                ? "image://theme/icon-camera-timer-" + timer + "s"
                : "image://theme/icon-camera-timer"
    }

    function timerText(timer) {
        return timer > 0
                ? //% "%1 second delay"
                  qsTrId("camera_settings-la-timer-seconds-delay").arg(timer)
                : //% "No delay"
                  qsTrId("camera_settings-la-timer-no-delay")
    }

    function colorFiltersIcon(enabled) {
        return "image://theme/icon-camera-filter-" + (enabled ? "on" : "off")
    }

    function colorFiltersEnabledText(enabled) {
        return enabled
                ? //% "Color filters on"
                  qsTrId("camera_settings-la-color-filters-on")
                : //% "Color filters off"
                  qsTrId("camera_settings-la-color-filters-off")
    }

    function isoText(iso) {
        if (iso == 0) {
            //% "Light sensitivity • Automatic"
            return qsTrId("camera_settings-la-light-sensitivity-auto")
        } else {
            //: %1 replaced with iso value
            //% "Light sensitivity • ISO %1"
            return qsTrId("camera_settings-la-light-sensitivity-iso_value").arg(iso)
        }
    }

    function meteringModeIcon(mode) {
        switch (mode) {
        case Camera.MeteringMatrix:  return "image://theme/icon-camera-metering-matrix"
        case Camera.MeteringAverage: return "image://theme/icon-camera-metering-weighted"
        case Camera.MeteringSpot:    return "image://theme/icon-camera-metering-spot"
        }
    }

    function exposureModeIcon(exposureMode) {
        switch (exposureMode) {
        case Camera.ExposureManual:         return "image://theme/icon-camera-mode-automatic"
        case Camera.ExposurePortrait:       return "image://theme/icon-camera-mode-portrait"
        case Camera.ExposureNight:          return "image://theme/icon-camera-mode-night"
        case Camera.ExposureSports:         return "image://theme/icon-camera-mode-sports"
        case Camera.ExposureHDR:            return "image://theme/icon-camera-mode-hdr"
        default:
            return "" // not supported
        }
    }

    function exposureModeText(exposureMode) {
        switch (exposureMode) {
        //: "Automatic exposure mode"
        //% "Automatic exposure"
        case Camera.ExposureManual:         return qsTrId("camera_settings-la-exposure-automatic")
        //: "Portrait exposure mode"
        //% "Portrait exposure"
        case Camera.ExposurePortrait:       return qsTrId("camera_settings-la-exposure-portrait")
        //: "Night exposure mode"
        //% "Night exposure"
        case Camera.ExposureNight:          return qsTrId("camera_settings-la-exposure-night")
        //: "Sports exposure mode"
        //% "Sports exposure"
        case Camera.ExposureSports:         return qsTrId("camera_settings-la-exposure-sports")
        //: "HDR exposure mode"
        //% "HDR exposure"
        case Camera.ExposureHDR:            return qsTrId("camera_settings-la-exposure-hdr")
        default:
            return "" // not supported
        }
    }

    function flashIcon(flash) {
        switch (flash) {
        case Camera.FlashAuto:              return "image://theme/icon-camera-flash-automatic"
        case Camera.FlashOff:               return "image://theme/icon-camera-flash-off"
        case Camera.FlashTorch:
        case Camera.FlashOn:                return "image://theme/icon-camera-flash-on"
        // JB#54201: Red-eye mode does not work
        // case Camera.FlashRedEyeReduction:   return "image://theme/icon-camera-flash-redeye"
        default:
            return "" // not supported
        }
    }

    function flashText(flash) {
        switch (flash) {
        //: "Automatic camera flash mode"
        //% "Flash automatic"
        case Camera.FlashAuto:       return qsTrId("camera_settings-la-flash-auto")
        //: "Camera flash disabled"
        //% "Flash disabled"
        case Camera.FlashOff:   return qsTrId("camera_settings-la-flash-off")
        //: "Camera flash enabled"
        //% "Flash enabled"
        case Camera.FlashOn:      return qsTrId("camera_settings-la-flash-on")
        //: "Camera flash in torch mode"
        //% "Flash on"
        case Camera.FlashTorch:   return qsTrId("camera_settings-la-flash-torch")
        //: "Camera flash with red eye reduction"
        //% "Flash red eye"
        case Camera.FlashRedEyeReduction: return qsTrId("camera_settings-la-flash-redeye")
        default:
            return "" // not supported
        }
    }

    function whiteBalanceIcon(balance) {
        switch (balance) {
        case CameraImageProcessing.WhiteBalanceAuto:        return "image://theme/icon-camera-wb-automatic"
        case CameraImageProcessing.WhiteBalanceSunlight:    return "image://theme/icon-camera-wb-sunny"
        case CameraImageProcessing.WhiteBalanceCloudy:      return "image://theme/icon-camera-wb-cloudy"
        // case CameraImageProcessing.WhiteBalanceShade:       return "image://theme/icon-camera-wb-shade"
        // case CameraImageProcessing.WhiteBalanceSunset:      return "image://theme/icon-camera-wb-sunset"
        case CameraImageProcessing.WhiteBalanceFluorescent: return "image://theme/icon-camera-wb-fluorecent"
        case CameraImageProcessing.WhiteBalanceTungsten:    return "image://theme/icon-camera-wb-tungsten"
        default:
            return "" // not supported
        }
    }

    function whiteBalanceText(balance) {
        switch (balance) {
        //: "Automatic white balance"
        //% "Automatic"
        case CameraImageProcessing.WhiteBalanceAuto:        return qsTrId("camera_settings-la-wb-automatic")
        //: "Sunny white balance"
        //% "Sunny"
        case CameraImageProcessing.WhiteBalanceSunlight:    return qsTrId("camera_settings-la-wb-sunny")
        //: "Cloudy white balance"
        //% "Cloudy"
        case CameraImageProcessing.WhiteBalanceCloudy:      return qsTrId("camera_settings-la-wb-cloudy")
        //: "Shade white balance"
        //% "Shade"
        case CameraImageProcessing.WhiteBalanceShade:       return qsTrId("camera_settings-la-wb-shade")
        //: "Sunset white balance"
        //% "Sunset"
        case CameraImageProcessing.WhiteBalanceSunset:      return qsTrId("camera_settings-la-wb-sunset")
        //: "Fluorecent white balance"
        //% "Fluorecent"
        case CameraImageProcessing.WhiteBalanceFluorescent: return qsTrId("camera_settings-la-wb-fluorecent")
        //: "Tungsten white balance"
        //% "Tungsten"
        case CameraImageProcessing.WhiteBalanceTungsten:    return qsTrId("camera_settings-la-wb-tungsten")
        default:
            return "" // not supported
        }
    }

    function colorFilterText(filter) {
        switch (filter) {
        case CameraImageProcessing.ColorFilterNone:
            //% "Normal"
            return qsTrId("camera_settings-la-colorfilter_normal")
        case CameraImageProcessing.ColorFilterGrayscale:
            //% "Grayscale"
            return qsTrId("camera_settings-la-colorfilter_grayscale")
        case CameraImageProcessing.ColorFilterNegative:
            //% "Negative"
            return qsTrId("camera_settings-la-colorfilter_negative")
        case CameraImageProcessing.ColorFilterSolarize:
            //% "Solarize"
            return qsTrId("camera_settings-la-colorfilter_solarize")
        case CameraImageProcessing.ColorFilterSepia:
            //% "Sepia"
            return qsTrId("camera_settings-la-colorfilter_sepia")
        case CameraImageProcessing.ColorFilterPosterize:
            //% "Posterize"
            return qsTrId("camera_settings-la-colorfilter_posterize")
        case CameraImageProcessing.ColorFilterWhiteboard:
            //% "Whiteboard"
            return qsTrId("camera_settings-la-colorfilter_whiteboard")
        case CameraImageProcessing.ColorFilterBlackboard:
            //% "Blackboard"
            return qsTrId("camera_settings-la-colorfilter_blackboard")
        case CameraImageProcessing.ColorFilterAqua:
            //% "Aqua"
            return qsTrId("camera_settings-la-colorfilter_aqua")
        default:
            return "" // not supported
        }
    }

    function viewfinderGridIcon(grid) {
        switch (grid) {
        case "none": return "image://theme/icon-camera-grid-none"
        case "thirds": return "image://theme/icon-camera-grid-thirds"
        default: return ""
        }
    }

    function viewfinderGridText(grid) {
        switch (grid) {
        case "none":
            //% "No grid"
            return qsTrId("camera_settings-la-no_grid")
        case "thirds":
            //% "Thirds grid"
            return qsTrId("camera_settings-la-thirds_grid")
        default: return ""
        }
    }
}

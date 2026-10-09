// SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6

// The keypad's rules for the viewfinder, as plain functions over plain values so
// they can be tested without a camera.
QtObject {
    // The cameras Left and Right walk through, in order. With back camera labels the
    // labelled back cameras come first and the front camera last; without labels it
    // is the previous back camera and the front one, as the touch switch button.
    function cameraOrder(labelCount, backCameras, previousBackId, frontId, hasBothSides) {
        var order = []
        if (labelCount > 0) {
            var cameras = backCameras || []
            var count = Math.min(labelCount, cameras.length)
            for (var i = 0; i < count; ++i) {
                order.push(cameras[i].deviceId)
            }
            if (hasBothSides) {
                order.push(frontId)
            }
        } else if (hasBothSides) {
            order = [previousBackId, frontId]
        }
        return order
    }

    // The device to switch to, or "" when there is nowhere to go. A current device
    // outside the order goes to the first entry forwards and the last backwards.
    function nextCamera(order, currentId, step) {
        if (order.length < 2) {
            return ""
        }
        var index = order.indexOf(currentId)
        if (index < 0) {
            return step > 0 ? order[0] : order[order.length - 1]
        }
        return order[(index + step + order.length) % order.length]
    }

    // A tenth of the zoom range per press, clamped. The same value back means inert.
    function zoomStep(zoom, maximum, direction) {
        if (maximum <= 1) {
            return zoom
        }
        var step = (maximum - 1) / 10
        return Math.max(1, Math.min(maximum, zoom + direction * step))
    }

    // No capture in flight in any of its forms.
    function idle(countdown, focusWait, busy, starting, recording) {
        return !countdown && !focusWait && !busy && !starting && !recording
    }
}

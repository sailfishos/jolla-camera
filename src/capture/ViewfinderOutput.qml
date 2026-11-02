// SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import QtMultimedia 5.0
import com.jolla.camera 1.0

VideoOutput {
    id: viewfinder

    // The video backend draws the frame fitted whatever the fill mode, so on a small screen the
    // viewfinder is scaled up to cover the item, cropping the frame's edges.
    readonly property real fillScale: Settings.fillViewfinder && contentRect.width > 0 && contentRect.height > 0
                                      ? Math.max(width / contentRect.width, height / contentRect.height)
                                      : 1

    transform: Scale {
        origin.x: viewfinder.width / 2
        origin.y: viewfinder.height / 2
        xScale: viewfinder.fillScale
        yScale: viewfinder.fillScale
    }
}

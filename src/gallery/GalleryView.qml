// SPDX-FileCopyrightText: 2013 - 2021 Jolla Ltd.
// SPDX-FileCopyrightText: 2020 Open Mobile Platform LLC.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.1
import QtMultimedia 5.0
import Sailfish.Silica 1.0
import Sailfish.Silica.private 1.0 as Private
import Sailfish.Gallery 1.0
import com.jolla.camera 1.0
import ".."

PagedView {
    id: root

    property alias overlay: overlay
    readonly property bool positionLocked: !overlay.active && playing

    readonly property bool active: page.galleryActive
    property QtObject captureModel

    property CameraPage page

    readonly property url source: currentItem ? currentItem.source : ""
    readonly property QtObject player: playerLoader.item ? playerLoader.item.player : null
    readonly property bool playing: player && player.playing
    property int _oldCount

    function _positionViewAtBeginning() {
        moveTo(0, PagedView.Immediate)
    }

    model: captureModel

    clip: true
    interactive: !positionLocked
    direction: PagedView.RightToLeft
    wrapMode: PagedView.NoWrap

    onActiveChanged: {
        if (!active) {
            // TODO: Don't touch internal property that can change
            if (overlay._remorsePopup && overlay._remorsePopup.active) overlay._remorsePopup.trigger()
            overlay.active = Qt.binding( function () { return captureModel && captureModel.count > 0 })
        }
    }

    property Item previousItem
    onMovingChanged: {
        if (moving) {
            previousItem = currentItem
        } else if (player && previousItem != currentItem) {
            player.reset()
        }
    }

    Connections {
        target: captureModel
        onCountChanged: {
            if (captureModel.count === 0) page.returnToCaptureMode()
            // Move to the new added item if we are currently in the first one
            if (count > _oldCount && currentIndex === 1) _positionViewAtBeginning()
            _oldCount = count
        }
    }

    delegate: Loader {
        readonly property int index: model.index
        readonly property string mimeType: model.mimeType
        readonly property url source: model.url

        readonly property bool isImage: mimeType.indexOf("image/") == 0
        readonly property bool error: item && item.error

        readonly property bool isCurrentItem: ListView.isCurrentItem

        width: root.width
        height: root.height
        sourceComponent: isImage ? imageComponent : videoComponent
        asynchronous: !isCurrentItem

        Component {
            id: imageComponent

            ImageViewer {

                onZoomedChanged: overlay.active = !zoomed
                onClicked: {
                    if (zoomed) {
                        zoomOut()
                    } else {
                        overlay.active = !overlay.active
                    }
                }

                source: parent.source

                active: isCurrentItem && root.active
                viewMoving: root.moving
            }
        }

        Component {
            id: videoComponent

            VideoPoster {
                onClicked: overlay.active = !overlay.active
                onTogglePlay: {
                    playerLoader.active = true
                    player.togglePlay()
                }
                onDoubleClicked: overlay.seekForward()

                contentWidth: root.width
                contentHeight: root.height

                source: parent.source
                mimeType: model.mimeType
                playing: player && player.playing
                loaded: player && player.loaded
                overlayMode: overlay.active
            }
        }
    }

    contentItem.children: [
        Loader {
            id: playerLoader

            active: false
            width: root.width
            height: root.height
            sourceComponent: GalleryVideoOutput {
                player: GalleryMediaPlayer {
                    id: mediaPlayer

                    active: currentItem && !currentItem.isImage && Qt.application.active
                    source: active ? currentItem.source : ""
                    onPlayingChanged: {
                        if (playing && overlay.active) {
                            // go fullscreen for playback if triggered via Play icon.
                            overlay.active = false
                        }
                    }
                    onLoadedChanged: if (loaded) playerLoader.anchors.centerIn = currentItem
                    onDisplayError: root.currentItem.item.displayError()
                }
            }
        }
    ]


    GalleryOverlay {
        id: overlay

        onRemove: {
            var item = currentItem
            //: Delete an image
            //% "Deleting"
            remorseAction( qsTrId("camera-la-deleting"), function() {
                root.captureModel.deleteFile(item.index)
                item.ListView.delayRemove = false
            })
        }
        onCreatePlayer: playerLoader.active = true

        active: captureModel && captureModel.count > 0
        anchors.fill: parent
        player: root.player
        source: root.source
        isImage: !root.currentItem || root.currentItem.isImage
        error: currentItem && currentItem.error
        editingAllowed: false

        Private.DismissButton {
            popPageOnClick: false
            onClicked: page.returnToCaptureMode()
        }
    }
}

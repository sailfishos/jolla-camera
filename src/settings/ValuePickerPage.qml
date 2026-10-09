// SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import Sailfish.Silica 1.0

// Picks one of a setting's values from the keypad: the current one highlighted,
// Ok writes the chosen value through apply and pops, Back pops.
Page {
    id: page

    property string title
    property var values: []
    property var current
    property var textFor
    property var iconFor
    property var apply

    SilicaListView {
        anchors.fill: parent
        model: page.values
        currentIndex: page.values.indexOf(page.current)

        header: PageHeader { title: page.title }

        delegate: ListItem {
            id: row

            width: ListView.view.width
            highlighted: down || modelData === page.current

            Row {
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                anchors.verticalCenter: parent.verticalCenter

                Image {
                    source: page.iconFor ? page.iconFor(modelData) : ""
                    visible: source != ""
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    text: page.textFor(modelData)
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            onClicked: {
                page.apply(modelData)
                pageStack.pop()
            }
        }

        VerticalScrollDecorator {}
    }
}

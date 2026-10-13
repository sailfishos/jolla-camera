// SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6

// One entry of the Options soft key menu: what Silica reads from a pull-down menu
// item, without being an item.
QtObject {
    property string text
    property bool enabled: true
    property bool visible: true

    signal clicked()
}

// SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.6
import QtTest 1.0
import com.jolla.camera 1.0

Item {
    KeypadRules { id: rules }

    TestCase {
        name: "KeypadRules"

        function cams(ids) {
            var list = []
            for (var i = 0; i < ids.length; ++i) {
                list.push({ "deviceId": ids[i] })
            }
            return list
        }

        function test_order_is_labelled_back_cameras_then_front() {
            compare(rules.cameraOrder(2, cams(["0", "2", "3"]), "0", "1", true), ["0", "2", "1"])
        }

        function test_order_clamps_labels_to_the_cameras() {
            compare(rules.cameraOrder(5, cams(["0"]), "0", "1", true), ["0", "1"])
            compare(rules.cameraOrder(2, [], "0", "1", true), ["1"])
            compare(rules.cameraOrder(2, undefined, "0", "1", true), ["1"])
        }

        function test_order_without_labels_is_back_and_front() {
            compare(rules.cameraOrder(0, cams(["0", "2"]), "0", "1", true), ["0", "1"])
        }

        function test_order_without_labels_or_a_front_camera_is_empty() {
            compare(rules.cameraOrder(0, cams(["0"]), "0", "", false), [])
        }

        function test_order_with_labels_and_no_front_camera_is_the_back_cameras() {
            compare(rules.cameraOrder(2, cams(["0", "2"]), "0", "", false), ["0", "2"])
        }

        function test_next_wraps_both_ways() {
            var order = ["0", "1"]
            compare(rules.nextCamera(order, "0", 1), "1")
            compare(rules.nextCamera(order, "1", 1), "0")
            compare(rules.nextCamera(order, "0", -1), "1")
        }

        function test_next_from_outside_the_order_goes_to_an_end() {
            var order = ["0", "2", "1"]
            compare(rules.nextCamera(order, "7", 1), "0")
            compare(rules.nextCamera(order, "7", -1), "1")
        }

        function test_next_is_inert_with_fewer_than_two_cameras() {
            compare(rules.nextCamera(["0"], "0", 1), "")
            compare(rules.nextCamera([], "0", 1), "")
        }

        function test_zoom_steps_a_tenth_of_the_range() {
            fuzzyCompare(rules.zoomStep(1.0, 5.0, 1), 1.4, 0.0001)
            fuzzyCompare(rules.zoomStep(1.4, 5.0, -1), 1.0, 0.0001)
        }

        function test_zoom_clamps_at_both_ends() {
            compare(rules.zoomStep(4.9, 5.0, 1), 5.0)
            compare(rules.zoomStep(5.0, 5.0, 1), 5.0)
            compare(rules.zoomStep(1.1, 5.0, -1), 1.0)
            compare(rules.zoomStep(1.0, 5.0, -1), 1.0)
        }

        function test_zoom_is_inert_without_a_range() {
            compare(rules.zoomStep(1.0, 1.0, 1), 1.0)
            compare(rules.zoomStep(1.0, 0, 1), 1.0)
        }

        function test_idle_needs_every_state_clear() {
            compare(rules.idle(false, false, false, false, false), true)
            compare(rules.idle(true, false, false, false, false), false)
            compare(rules.idle(false, true, false, false, false), false)
            compare(rules.idle(false, false, true, false, false), false)
            compare(rules.idle(false, false, false, true, false), false)
            compare(rules.idle(false, false, false, false, true), false)
        }
    }
}

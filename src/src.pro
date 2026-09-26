# SPDX-FileCopyrightText: 2014 - 2021 Jolla Ltd.
# SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
#
# SPDX-License-Identifier: BSD-3-Clause

TEMPLATE = lib
TARGET  = vividcameraplugin
TARGET = $$qtLibraryTarget($$TARGET)

MODULENAME = com/vivid/camera
TARGETPATH = $$[QT_INSTALL_QML]/$$MODULENAME

QT += gui-private qml quick multimedia concurrent
CONFIG += plugin link_pkgconfig c++14

PKGCONFIG += mlite5 systemsettings libtiff-4

SOURCES += \
        camera2preview.cpp \
        cameraplugin.cpp \
        capturemodel.cpp \
        declarativecameraextensions.cpp \
        dnglensshading.cpp \
        imageadjustments.cpp \
        declarativesettings.cpp \
        cameraconfigs.cpp

HEADERS += \
        camera2preview.h \
        capturemodel.h \
        declarativecameraextensions.h \
        dnglensshading.h \
        imageadjustments.h \
        declarativesettings.h \
        cameraconfigs.h

DEFINES += \
        DEPLOYMENT_PATH=\"\\\"\"$${TARGETPATH}/\"\\\"\"

import.files = \
        DisabledByMdmView.qml \
        CameraPage.qml \
        capture \
        gallery \
        qmldir \
        settings \
        settings.qml \
        calibration

import.path = $$TARGETPATH
target.path = $$TARGETPATH

INSTALLS += target import

OTHER_FILES = \
        DisabledByMdmView.qml \
        CameraPage.qml \
        capture/*.qml \
        gallery/*.qml \
        settings/*.qml \
        settings.qml \
        qmldir

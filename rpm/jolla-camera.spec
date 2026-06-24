# SPDX-FileCopyrightText: 2013 - 2024 Jolla Ltd.
# SPDX-FileCopyrightText: 2019 - 2020 Open Mobile Platform LLC.
# SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
#
# SPDX-License-Identifier: BSD-3-Clause

Name:       jolla-camera
Summary:    Jolla Camera application
Version:    1.3.0
Release:    1
License:    BSD-3-Clause
URL:        https://github.com/sailfishos/jolla-camera
Source0:    %{name}-%{version}.tar.bz2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(Qt5Test)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(qdeclarative5-boostable)
BuildRequires:  pkgconfig(mlite5) >= 0.2.5
BuildRequires:  pkgconfig(systemsettings) >= 0.2.13
BuildRequires:  qt5-qttools
BuildRequires:  qt5-qttools-linguist
BuildRequires:  oneshot

Requires:  sailfishsilica-qt5 >= 1.1.79
Requires:  qt5-qtdeclarative-import-models2
Requires:  qt5-qtdeclarative-import-positioning
Requires:  qt5-qtdeclarative-import-multimedia
Requires:  qt5-qtdeclarative-import-sensors
Requires:  qt5-qtmultimedia-plugin-mediaservice-gstcamerabin >= 5.6.2+git25
Requires:  qt5-qtmultimedia-plugin-mediaservice-gstmediaplayer
Requires:  declarative-transferengine-qt5 >= 0.0.49
Requires:  nemo-qml-plugin-thumbnailer-qt5
Requires:  nemo-qml-plugin-dbus-qt5
Requires:  nemo-qml-plugin-policy-qt5
Requires:  nemo-qml-plugin-time-qt5
Requires:  nemo-qml-plugin-configuration-qt5
Requires:  nemo-qml-plugin-notifications-qt5 >= 1.1.2
Requires:  nemo-qml-plugin-systemsettings >= 0.5.21
Requires:  libkeepalive >= 1.7.0
Requires:  sailfish-components-media-qt5 >= 0.0.18
Requires:  sailfish-components-gallery-qt5 >= 1.1.10
Requires:  sailfish-policy >= 0.2.59
Requires:  jolla-settings
Requires:  jolla-settings-system >= 1.0.70
Requires:  libngf-qt5-declarative
Requires:  qr-filter-qml-plugin
Requires:  sailfish-content-graphics >= 1.2.2
Requires:  gstreamer1.0-plugins-good
Requires:  gstreamer1.0-plugins-bad
Requires:  dconf
Requires:  sailjail-launch-approval
Requires:  mapplauncherd-booster-silica-qt5-media
Provides:  jolla-camera-settings > 1.2.30
Obsoletes: jolla-camera-settings <= 1.2.30
Provides:  jolla-camera-lockscreen > 1.2.30
Obsoletes: jolla-camera-lockscreen <= 1.2.30

%{_oneshot_requires_post}

%description
The Jolla Camera application.

%package ts-devel
Summary:   Translation source for Jolla Camera

%description ts-devel
Translation source for Jolla Camera.

%package tests
Summary:    Unit tests for Jolla Camera
Requires:   %{name} = %{version}-%{release}
Requires:   qt5-qtdeclarative-import-qttest
Requires:   qt5-qtdeclarative-devel-tools

%description tests
This package contains QML unit tests for Jolla Camera application.

%prep
%setup -q -n %{name}-%{version}

%build

%qmake5 %{name}.pro
%make_build

%install
%qmake5_install
chmod +x %{buildroot}/opt/tests/jolla-camera/auto/run-tests.sh
chmod +x %{buildroot}/%{_oneshotdir}/*

%post
%{_bindir}/add-oneshot dconf-update || :
%{_bindir}/add-oneshot --new-users camera-enable-hints || :

%files
%license LICENSES/BSD-3-Clause.txt
%{_datadir}/applications/jolla-camera.desktop
%{_datadir}/applications/jolla-camera-viewfinder.desktop
# Define directory ownership explicitly as part of files in the datadir
# belongs to jolla-camera-lockscreen.
%dir %{_datadir}/jolla-camera
%{_datadir}/jolla-camera/camera.qml
%{_datadir}/jolla-camera/pages
%{_datadir}/jolla-camera/cover
%{_bindir}/jolla-camera
%{_datadir}/translations/jolla-camera_eng_en.qm
%{_datadir}/dbus-1/services/com.jolla.camera.service
%{_libdir}/qt5/qml/com/jolla/camera
%{_sysconfdir}/dconf/db/vendor.d/00-jolla-camera.txt
%{_oneshotdir}/camera-enable-hints
%{_userunitdir}/user-session.target.d/50-jolla-camera.conf
%{_bindir}/jolla-camera-lockscreen
%{_datadir}/applications/jolla-camera-lockscreen.desktop
%{_datadir}/jolla-camera/lockscreen.qml
%{_datadir}/jolla-camera/LockedGalleryView.qml
%{_datadir}/jolla-settings

%files ts-devel
%{_datadir}/translations/source/jolla-camera.ts

%files tests
/opt/tests/jolla-camera

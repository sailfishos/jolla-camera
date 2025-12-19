// SPDX-FileCopyrightText: 2013 - 2016 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

#include <QDir>
#include <QTranslator>
#include <QLocale>

#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlComponent>

#ifdef HAS_BOOSTER
#include <MDeclarativeCache>
#endif

Q_DECL_EXPORT int main(int argc, char *argv[])
{
    QQuickWindow::setDefaultAlphaBuffer(true);

#ifdef HAS_BOOSTER
    QScopedPointer<QGuiApplication> app(MDeclarativeCache::qApplication(argc, argv));
    QScopedPointer<QQuickView> view(MDeclarativeCache::qQuickView());
#else
    QScopedPointer<QGuiApplication> app(new QGuiApplication(argc, argv));
    QScopedPointer<QQuickView> view(new QQuickView);
#endif

    QString path(QLatin1String(DEPLOYMENT_PATH));

    view->engine()->setBaseUrl(QUrl::fromLocalFile(path));

    view->setSource(path + QLatin1String("camera.qml"));
    //% "Camera"
    view->setTitle(qtTrId("jolla-camera-ap-name"));

    if (app->arguments().contains("-desktop")) {
        view->resize(480, 854);
        view->rootObject()->setProperty("_desktop", true);
        view->show();
    } else {
        view->showFullScreen();
    }

    return app->exec();
}

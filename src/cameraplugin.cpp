// SPDX-FileCopyrightText: 2014 - 2021 Jolla Ltd.
// SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
//
// SPDX-License-Identifier: BSD-3-Clause

#include <QQmlExtensionPlugin>
#include <QTranslator>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QLocale>

#include <MDConfItem>
#include <qqml.h>

#include "capturemodel.h"
#include "declarativecameraextensions.h"
#include "declarativesettings.h"
#include "cameraconfigs.h"

template <typename T> static QObject *singletonFactory(QQmlEngine *, QJSEngine *)
{
    return new T;
}

// using custom translator so it gets properly removed from qApp when engine is deleted
class AppTranslator: public QTranslator
{
public:
    AppTranslator(QObject *parent)
        : QTranslator(parent)
    {
        qApp->installTranslator(this);
    }

    virtual ~AppTranslator()
    {
        qApp->removeTranslator(this);
    }
};


static bool isTranslationCatalogName(const QString &name)
{
    if (name.isEmpty() || name == QLatin1String("jolla-camera") || name.size() > 64)
        return false;

    for (const QChar &c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char('_') && c != QLatin1Char('-'))
            return false;
    }
    return true;
}

static void loadTranslationCatalog(QQmlEngine *engine, const QString &name)
{
    AppTranslator *engineeringEnglish = new AppTranslator(engine);
    AppTranslator *translator = new AppTranslator(engine);
    const QString directory = QStringLiteral("/usr/share/translations");

    engineeringEnglish->load(name + QLatin1String("_eng_en"), directory);
    translator->load(QLocale(), name, QLatin1String("-"), directory);
}

class CameraPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.jolla.camera")

public:

    void initializeEngine(QQmlEngine *engine, const char *uri)
    {
        Q_UNUSED(uri)
        Q_ASSERT(QLatin1String(uri) == QLatin1String("com.jolla.camera"));

        loadTranslationCatalog(engine, QStringLiteral("jolla-camera"));

        // Device configuration names the catalog that translates the
        // alternative-display menu. Absent on other devices.
        MDConfItem catalog(QStringLiteral("/apps/jolla-camera/alternativeDisplayCatalog"));
        const QString extra = catalog.value().toString();
        if (isTranslationCatalogName(extra))
            loadTranslationCatalog(engine, extra);
    }

    virtual void registerTypes(const char *uri)
    {
        Q_UNUSED(uri)
        Q_ASSERT(QLatin1String(uri) == QLatin1String("com.jolla.camera"));

        qmlRegisterType<CaptureModel>("com.jolla.camera", 1, 0, "CaptureModel");
        qmlRegisterType<DeclarativeCameraExtensions>("com.jolla.camera", 1, 0, "CameraExtensions");
        qmlRegisterType<DeclarativeSettings>("com.jolla.camera", 1, 0, "SettingsBase");
        qmlRegisterSingletonType<DeclarativeSettings>("com.jolla.camera", 1, 0, "Settings", DeclarativeSettings::factory);
        qmlRegisterSingletonType<CameraConfigs>("com.jolla.camera", 1, 0, "CameraConfigs", singletonFactory<CameraConfigs>);
    }
};

#include "cameraplugin.moc"

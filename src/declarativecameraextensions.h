/*
 * SPDX-FileCopyrightText: 2013 - 2014 Jolla Ltd.
 * SPDX-FileCopyrightText: 2025 Jolla Mobile Ltd
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DECLARATIVECAMERAEXTENSIONS_H
#define DECLARATIVECAMERAEXTENSIONS_H

#include <QQuickItem>

class DeclarativeCameraExtensions : public QObject
{
    Q_OBJECT

public:
    DeclarativeCameraExtensions(QObject *parent = nullptr);
    ~DeclarativeCameraExtensions();

    Q_INVOKABLE void disableNotifications(QQuickItem *item, bool disable);

private:
};

#endif

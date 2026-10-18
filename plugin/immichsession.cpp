/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
 *
 * Date        : 2026-10-18
 *
 * SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ============================================================ */


// Self includes

#include "immichsession.h"

// KDE includes

#include <kconfiggroup.h>
#include <ksharedconfig.h>

namespace DigikamGenericImmichPlugin
{

class ImmichSession::Private
{
public:

    QString url;
    QString apiKey;
    QString downloadDir;
    QString replaceMode = QStringLiteral("stack");
    QString exportTargetMode = QStringLiteral("library");
};

ImmichSession::ImmichSession()
    : d(new Private)
{
}

ImmichSession::~ImmichSession()
{
    delete d;
}

QString ImmichSession::url() const
{
    return d->url;
}

QString ImmichSession::apiKey() const
{
    return d->apiKey;
}

QString ImmichSession::downloadDir() const
{
    return d->downloadDir;
}

QString ImmichSession::replaceMode() const
{
    return d->replaceMode;
}

QString ImmichSession::exportTargetMode() const
{
    return d->exportTargetMode;
}

void ImmichSession::setUrl(const QString& url)
{
    d->url = url;
}

void ImmichSession::setApiKey(const QString& apiKey)
{
    d->apiKey = apiKey;
}

void ImmichSession::setDownloadDir(const QString& dir)
{
    d->downloadDir = dir;
}

void ImmichSession::setReplaceMode(const QString& mode)
{
    d->replaceMode = (mode == QStringLiteral("trash")) ? mode : QStringLiteral("stack");
}

void ImmichSession::setExportTargetMode(const QString& mode)
{
    d->exportTargetMode = (mode == QStringLiteral("album")) ? mode : QStringLiteral("library");
}

void ImmichSession::save()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Immich Settings"));
    group.writeEntry("ServerUrl", d->url);
    group.writeEntry("ApiKey", d->apiKey);
    group.writeEntry("DownloadDir", d->downloadDir);
    group.writeEntry("ReplaceMode", d->replaceMode);
    group.writeEntry("ExportTargetMode", d->exportTargetMode);
    group.sync();
}

void ImmichSession::load()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Immich Settings"));
    d->url         = group.readEntry("ServerUrl", QString());
    d->apiKey      = group.readEntry("ApiKey", QString());
    d->downloadDir = group.readEntry("DownloadDir", QString());
    setReplaceMode(group.readEntry("ReplaceMode", QStringLiteral("stack")));
    setExportTargetMode(group.readEntry("ExportTargetMode", QStringLiteral("library")));
}

} // namespace DigikamGenericImmichPlugin

/* ============================================================
 *
 * This file is a part of the immich-digikam project
 * https://github.com/mchugh19/immich-digikam
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2, or (at your option) any later version.
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

void ImmichSession::save()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Immich Settings"));
    group.writeEntry("ServerUrl", d->url);
    group.writeEntry("ApiKey", d->apiKey);
    group.writeEntry("DownloadDir", d->downloadDir);
    group.writeEntry("ReplaceMode", d->replaceMode);
    group.sync();
}

void ImmichSession::load()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Immich Settings"));
    d->url         = group.readEntry("ServerUrl", QString());
    d->apiKey      = group.readEntry("ApiKey", QString());
    d->downloadDir = group.readEntry("DownloadDir", QString());
    setReplaceMode(group.readEntry("ReplaceMode", QStringLiteral("stack")));
}

} // namespace DigikamGenericImmichPlugin

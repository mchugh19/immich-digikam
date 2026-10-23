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

#include <KWallet>
#include <kconfiggroup.h>
#include <ksharedconfig.h>

// Std includes

#include <memory>

namespace DigikamGenericImmichPlugin
{

namespace
{

// Function-local static variables, not namespace-scope QStrings:
// a non-POD global is constructed before main() and destroyed
// after it, which clazy rightly flags.
const QString& walletFolder()
{
    static const QString folder = QStringLiteral("digiKam Immich");
    return folder;
}

const QString& walletEntry()
{
    static const QString entry = QStringLiteral("ApiKey");
    return entry;
}

KWallet::Wallet* openWallet()
{
    return KWallet::Wallet::openWallet(KWallet::Wallet::NetworkWallet(),
                                       0, KWallet::Wallet::Synchronous);
}

bool readWalletApiKey(QString& apiKey)
{
    if (KWallet::Wallet::keyDoesNotExist(KWallet::Wallet::NetworkWallet(),
                                          walletFolder(), walletEntry()))
    {
        return false;
    }

    const std::unique_ptr<KWallet::Wallet> wallet(openWallet());

    if (!wallet || !wallet->hasFolder(walletFolder())
        || !wallet->setFolder(walletFolder()))
    {
        return false;
    }

    return wallet->readPassword(walletEntry(), apiKey) == 0;
}

bool writeWalletApiKey(const QString& apiKey)
{
    const std::unique_ptr<KWallet::Wallet> wallet(openWallet());

    if (!wallet)
    {
        return false;
    }

    if (!wallet->hasFolder(walletFolder()) && !wallet->createFolder(walletFolder()))
    {
        return false;
    }

    if (!wallet->setFolder(walletFolder()))
    {
        return false;
    }

    if (apiKey.isEmpty())
    {
        return !wallet->hasEntry(walletEntry())
               || wallet->removeEntry(walletEntry()) == 0;
    }

    return wallet->writePassword(walletEntry(), apiKey) == 0;
}

} // namespace

class Q_DECL_HIDDEN ImmichSession::Private
{
public:

    QString url;
    QString apiKey;
    QString downloadDir;
    QString replaceMode = QStringLiteral("stack");
    QString exportTargetMode = QStringLiteral("library");

    QHash<QString, QList<ImmichAssetRef>> listingCache;
};

ImmichSession::ImmichSession()
    : d(new Private)
{
}

ImmichSession::~ImmichSession()
{
    delete d;
}

const QString& ImmichSession::url() const
{
    return d->url;
}

const QString& ImmichSession::apiKey() const
{
    return d->apiKey;
}

const QString& ImmichSession::downloadDir() const
{
    return d->downloadDir;
}

const QString& ImmichSession::replaceMode() const
{
    return d->replaceMode;
}

const QString& ImmichSession::exportTargetMode() const
{
    return d->exportTargetMode;
}

void ImmichSession::setUrl(const QString& newUrl)
{
    // Store the server root, normalized once here so every
    // consumer sees the same shape: no trailing slash, and no
    // /api suffix (the talker appends it; a user pasting the
    // API base URL gets the root they meant).
    QString cleaned = newUrl.trimmed();

    while (cleaned.endsWith(QLatin1Char('/')))
    {
        cleaned.chop(1);
    }

    if (cleaned.endsWith(QStringLiteral("/api")))
    {
        cleaned.chop(4);
    }

    d->url = cleaned;
}

void ImmichSession::setApiKey(const QString& newKey)
{
    d->apiKey = newKey;
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

bool ImmichSession::hasListing(const QString& key) const
{
    return d->listingCache.contains(key);
}

QList<ImmichAssetRef> ImmichSession::listing(const QString& key) const
{
    return d->listingCache.value(key);
}

void ImmichSession::cacheListing(const QString& key,
                                 const QList<ImmichAssetRef>& assets)
{
    d->listingCache.insert(key, assets);
}

void ImmichSession::invalidateListing(const QString& key)
{
    d->listingCache.remove(key);
}

void ImmichSession::clearListings()
{
    d->listingCache.clear();
}

void ImmichSession::save()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Immich Settings"));
    group.writeEntry("ServerUrl", d->url);

    // Prefer KWallet for the secret. The plaintext config entry is
    // only a fallback for a wallet that cannot be opened or written;
    // whenever the wallet takes the key, any old config copy is
    // removed so the secret does not live in both places.
    if (writeWalletApiKey(d->apiKey) || d->apiKey.isEmpty())
    {
        group.deleteEntry("ApiKey");
    }
    else
    {
        group.writeEntry("ApiKey", d->apiKey);
    }

    group.writeEntry("DownloadDir", d->downloadDir);
    group.writeEntry("ReplaceMode", d->replaceMode);
    group.writeEntry("ExportTargetMode", d->exportTargetMode);
    group.sync();
}

void ImmichSession::load()
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("Immich Settings"));
    setUrl(group.readEntry("ServerUrl", QString()));

    // The wallet wins when it has the key. A legacy plaintext entry
    // is used only when the wallet has none, and is migrated into
    // the wallet (and removed from the config) when it can be.
    const QString configApiKey = group.readEntry("ApiKey", QString());
    QString       walletApiKey;

    if (readWalletApiKey(walletApiKey))
    {
        d->apiKey = walletApiKey;

        if (!configApiKey.isEmpty())
        {
            group.deleteEntry("ApiKey");
            group.sync();
        }
    }
    else
    {
        d->apiKey = configApiKey;

        if (!configApiKey.isEmpty() && writeWalletApiKey(configApiKey))
        {
            group.deleteEntry("ApiKey");
            group.sync();
        }
    }

    d->downloadDir = group.readEntry("DownloadDir", QString());
    setReplaceMode(group.readEntry("ReplaceMode", QStringLiteral("stack")));
    setExportTargetMode(group.readEntry("ExportTargetMode", QStringLiteral("library")));
}

} // namespace DigikamGenericImmichPlugin

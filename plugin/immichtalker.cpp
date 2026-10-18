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

#include "immichtalker.h"

// Qt includes

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

// Local includes

#include "immichsession.h"

namespace DigikamGenericImmichPlugin
{

class ImmichTalker::Private
{
public:

    ImmichSession*         session = nullptr;
    QNetworkAccessManager* net     = nullptr;

    // What a pending reply should be parsed as when it finishes.
    enum class Op { None, ListAlbums, ListTags, CreateAlbum, AddToAlbum, Upload,
                    AlbumAssets, DownloadOriginal, DownloadSidecar,
                    DownloadRecord, MetaSidecar, MetaRecord,
                    BulkCheck, Thumbnail };
    QHash<QNetworkReply*, Op> ops;

    // Download context per reply: asset id and destination path.
    QHash<QNetworkReply*, QPair<QString, QString> > downloadCtx;

    // Album-asset listing accumulates across search pages.
    QList<ImmichAssetRef> listingAccum;
    QString               listingAlbumId;
    QString               listingTagId;
    // Bumped by every new listing; page replies stamped with an
    // older generation are dropped, so a superseded listing (the
    // user clicked another source mid-listing) cannot leak its
    // assets into the current one.
    int                   listingGen = 0;
    QHash<QNetworkReply*, int> listingGenByReply;
    QHash<QNetworkReply*, QString> thumbCtx;
};

ImmichTalker::ImmichTalker(ImmichSession* const session, QObject* const parent)
    : QObject(parent),
      d(new Private)
{
    d->session = session;
    d->net     = new QNetworkAccessManager(this);

    connect(d->net, &QNetworkAccessManager::finished,
            this, &ImmichTalker::slotFinished);
}

ImmichTalker::~ImmichTalker()
{
    delete d;
}

QNetworkRequest ImmichTalker::makeRequest(const QString& path) const
{
    QString base = d->session->url();
    if (base.endsWith(QLatin1Char('/')))
    {
        base.chop(1);
    }

    // The session stores the server root URL, the way every Immich
    // client expects it entered; the REST API lives below /api.
    // Without this the requests land on the web frontend, which
    // answers JSON-only requests with 406.

    if (!base.endsWith(QLatin1String("/api")))
    {
        base += QLatin1String("/api");
    }

    QNetworkRequest req(QUrl(base + path));
    req.setRawHeader("x-api-key", d->session->apiKey().toUtf8());
    req.setRawHeader("Accept", "application/json");
    return req;
}

void ImmichTalker::sendJson(const QString& method, const QString& path, const QByteArray& body)
{
    QNetworkRequest req = makeRequest(path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply* const reply = d->net->sendCustomRequest(req, method.toUtf8(), body);
    d->ops.insert(reply, Private::Op::None);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::listAlbums()
{
    QNetworkReply* const reply = d->net->get(makeRequest(QStringLiteral("/albums")));
    d->ops.insert(reply, Private::Op::ListAlbums);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::createAlbum(const QString& name)
{
    const QJsonObject obj { { QStringLiteral("albumName"), name } };
    QNetworkReply* const reply = d->net->post(makeRequest(QStringLiteral("/albums")),
                                              QJsonDocument(obj).toJson());
    d->ops.insert(reply, Private::Op::CreateAlbum);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::addToAlbum(const QString& albumId, const QStringList& assetIds)
{
    QJsonArray ids;
    for (const QString& id : assetIds)
    {
        ids.append(id);
    }

    const QJsonObject obj { { QStringLiteral("ids"), ids } };
    const QByteArray body = QJsonDocument(obj).toJson();

    QNetworkRequest req = makeRequest(QStringLiteral("/albums/%1/assets").arg(albumId));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* const reply = d->net->put(req, body);
    d->ops.insert(reply, Private::Op::AddToAlbum);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::uploadAsset(const QString& filePath, const QString& sidecarPath)
{
    const QFileInfo info(filePath);

    // Field contract verified live against Immich 3.3.3:
    // deviceAssetId derives from the absolute path, timestamps are
    // the file's mtime, the sidecar travels as sidecarData.
    const QString deviceAssetId = QStringLiteral("digikam-") +
        QString::fromLatin1(QCryptographicHash::hash(info.absoluteFilePath().toUtf8(),
                                                     QCryptographicHash::Sha1).toHex().left(32));
    const QString mtime = info.lastModified().toUTC().toString(Qt::ISODate);

    auto* const multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto addTextPart = [multiPart](const QString& name, const QString& value)
    {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(name));
        part.setBody(value.toUtf8());
        multiPart->append(part);
    };

    addTextPart(QStringLiteral("deviceAssetId"), deviceAssetId);
    addTextPart(QStringLiteral("deviceId"), QStringLiteral("digikam-bridge"));
    addTextPart(QStringLiteral("fileCreatedAt"), mtime);
    addTextPart(QStringLiteral("fileModifiedAt"), mtime);

    auto* const file = new QFile(filePath, multiPart);
    if (!file->open(QIODevice::ReadOnly))
    {
        Q_EMIT signalError(QStringLiteral("Cannot open %1").arg(filePath));
        delete multiPart;
        return;
    }

    QHttpPart assetPart;
    assetPart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    assetPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                        QStringLiteral("form-data; name=\"assetData\"; filename=\"%1\"").arg(info.fileName()));
    assetPart.setBodyDevice(file);
    multiPart->append(assetPart);

    if (!sidecarPath.isEmpty() && QFileInfo::exists(sidecarPath))
    {
        auto* const sidecar = new QFile(sidecarPath, multiPart);
        if (sidecar->open(QIODevice::ReadOnly))
        {
            QHttpPart sidecarPart;
            sidecarPart.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml"));
            sidecarPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                                  QStringLiteral("form-data; name=\"sidecarData\"; filename=\"%1\"")
                                      .arg(QFileInfo(sidecarPath).fileName()));
            sidecarPart.setBodyDevice(sidecar);
            multiPart->append(sidecarPart);
        }
    }

    QNetworkReply* const reply = d->net->post(makeRequest(QStringLiteral("/assets")), multiPart);
    multiPart->setParent(reply);
    d->ops.insert(reply, Private::Op::Upload);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::listTags()
{
    QNetworkReply* const reply = d->net->get(makeRequest(QStringLiteral("/tags")));
    d->ops.insert(reply, Private::Op::ListTags);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::listAlbumAssets(const QString& albumId)
{
    d->listingAccum.clear();
    d->listingAlbumId = albumId;
    d->listingTagId.clear();
    ++d->listingGen;
    fetchListingPage(1);
}

void ImmichTalker::listTagAssets(const QString& tagId)
{
    d->listingAccum.clear();
    d->listingTagId   = tagId;
    d->listingAlbumId.clear();
    ++d->listingGen;
    fetchListingPage(1);
}

void ImmichTalker::fetchThumbnail(const QString& assetId)
{
    // Pin the small generated size (250px) explicitly: it is
    // what the endpoint defaults to today, and the previews and
    // covers have no use for the 1440px preview size.
    QNetworkReply* const reply = d->net->get(
        makeRequest(QStringLiteral("/assets/%1/thumbnail?size=thumbnail").arg(assetId)));
    d->ops.insert(reply, Private::Op::Thumbnail);
    d->thumbCtx.insert(reply, assetId);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::fetchListingPage(int page)
{
    QJsonObject obj {
        { QStringLiteral("size"), 250 },
        { QStringLiteral("page"), page }
    };

    if (!d->listingTagId.isEmpty())
    {
        obj.insert(QStringLiteral("tagIds"), QJsonArray { d->listingTagId });
    }
    else
    {
        obj.insert(QStringLiteral("albumIds"), QJsonArray { d->listingAlbumId });
    }

    QNetworkRequest req = makeRequest(QStringLiteral("/search/metadata"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* const reply = d->net->post(req, QJsonDocument(obj).toJson());
    d->ops.insert(reply, Private::Op::AlbumAssets);
    d->listingGenByReply.insert(reply, d->listingGen);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::downloadSidecarOnly(const QString& assetId, const QString& targetPath)
{
    QNetworkReply* const reply = d->net->get(
        makeRequest(QStringLiteral("/assets/%1/sidecar").arg(assetId)));
    d->ops.insert(reply, Private::Op::MetaSidecar);
    d->downloadCtx.insert(reply, qMakePair(assetId, targetPath + QStringLiteral(".xmp")));
    Q_EMIT signalBusy(true);
}

void ImmichTalker::downloadAsset(const QString& assetId, const QString& targetPath)
{
    QNetworkReply* const reply = d->net->get(
        makeRequest(QStringLiteral("/assets/%1/original").arg(assetId)));
    d->ops.insert(reply, Private::Op::DownloadOriginal);
    d->downloadCtx.insert(reply, qMakePair(assetId, targetPath));
    Q_EMIT signalBusy(true);
}

void ImmichTalker::checkUploads(const QMap<QString, QString>& checksumsByPath)
{
    QJsonArray assets;

    for (auto it = checksumsByPath.constBegin(); it != checksumsByPath.constEnd(); ++it)
    {
        assets.append(QJsonObject {
            { QStringLiteral("id"),       it.key()   },
            { QStringLiteral("checksum"), it.value() }
        });
    }

    const QJsonObject obj { { QStringLiteral("assets"), assets } };

    QNetworkRequest req = makeRequest(QStringLiteral("/assets/bulk-upload-check"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* const reply = d->net->post(req, QJsonDocument(obj).toJson());
    d->ops.insert(reply, Private::Op::BulkCheck);
    Q_EMIT signalBusy(true);
}

void ImmichTalker::cancel()
{
    for (QNetworkReply* const reply : d->ops.keys())
    {
        reply->abort();
    }
}

void ImmichTalker::slotFinished(QNetworkReply* reply)
{
    const Private::Op op = d->ops.take(reply);
    const QPair<QString, QString> ctx = d->downloadCtx.take(reply);
    Q_EMIT signalBusy(false);

    if (op == Private::Op::DownloadOriginal || op == Private::Op::DownloadSidecar
        || op == Private::Op::MetaSidecar)
    {
        const QString& assetId    = ctx.first;
        const QString& targetPath = ctx.second;
        const int httpStatus      = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body     = (reply->error() == QNetworkReply::NoError) ? reply->readAll()
                                                                               : QByteArray();
        const bool failed         = (reply->error() != QNetworkReply::NoError) && httpStatus != 404;
        reply->deleteLater();

        if (op == Private::Op::MetaSidecar)
        {
            // Metadata-only refresh of an existing file. A network
            // failure (including an abort) ends it quietly; a 404
            // falls through to the asset record for generation.
            if (failed)
            {
                Q_EMIT signalMetaFinished(assetId, false);
                return;
            }

            bool sidecarWritten = false;

            if (httpStatus == 200 && !body.isEmpty())
            {
                QFile file(targetPath);

                if (file.open(QIODevice::WriteOnly))
                {
                    sidecarWritten = (file.write(body) == body.size());
                }
            }

            if (sidecarWritten)
            {
                Q_EMIT signalMetaFinished(assetId, true);
                return;
            }

            const QString originalPath = targetPath.left(targetPath.size() - 4);   // strip ".xmp"
            QNetworkReply* const recordReply = d->net->get(
                makeRequest(QStringLiteral("/assets/%1").arg(assetId)));
            d->ops.insert(recordReply, Private::Op::MetaRecord);
            d->downloadCtx.insert(recordReply, qMakePair(assetId, originalPath));
            Q_EMIT signalBusy(true);
            return;
        }

        if (op == Private::Op::DownloadSidecar)
        {
            // The original is already on disk; a missing (404) or
            // unfetchable sidecar does not fail the download.
            bool sidecarWritten = false;

            if (!failed && httpStatus == 200 && !body.isEmpty())
            {
                QFile file(targetPath);

                if (file.open(QIODevice::WriteOnly))
                {
                    sidecarWritten = (file.write(body) == body.size());
                }
            }

            if (sidecarWritten)
            {
                Q_EMIT signalDownloadFinished(assetId, true);
                return;
            }

            // No server sidecar: fetch the asset record so the window
            // can generate a sidecar from the Immich metadata (the
            // tags above all) instead of the photo arriving bare.
            const QString originalPath = targetPath.left(targetPath.size() - 4);   // strip ".xmp"
            QNetworkReply* const recordReply = d->net->get(
                makeRequest(QStringLiteral("/assets/%1").arg(assetId)));
            d->ops.insert(recordReply, Private::Op::DownloadRecord);
            d->downloadCtx.insert(recordReply, qMakePair(assetId, originalPath));
            Q_EMIT signalBusy(true);
            return;
        }

        if (failed)
        {
            Q_EMIT signalDownloadFinished(assetId, false);
            return;
        }

        bool written = false;
        {
            QFile file(targetPath);
            if (file.open(QIODevice::WriteOnly))
            {
                written = (file.write(body) == body.size());
            }
        }

        if (!written)
        {
            Q_EMIT signalDownloadFinished(assetId, false);
            return;
        }

        // Chain the sidecar fetch for the asset just written.
        QNetworkReply* const sidecarReply = d->net->get(
            makeRequest(QStringLiteral("/assets/%1/sidecar").arg(assetId)));
        d->ops.insert(sidecarReply, Private::Op::DownloadSidecar);
        d->downloadCtx.insert(sidecarReply, qMakePair(assetId, targetPath + QStringLiteral(".xmp")));
        Q_EMIT signalBusy(true);
        return;
    }

    if (op == Private::Op::Thumbnail)
    {
        const QString  assetId = d->thumbCtx.take(reply);
        const QByteArray bytes = (reply->error() == QNetworkReply::NoError) ? reply->readAll()
                                                                            : QByteArray();
        reply->deleteLater();
        Q_EMIT signalThumbnail(assetId, bytes);
        return;
    }

    if (op == Private::Op::MetaRecord)
    {
        const QJsonObject record = (reply->error() == QNetworkReply::NoError)
                                       ? QJsonDocument::fromJson(reply->readAll()).object()
                                       : QJsonObject();
        reply->deleteLater();

        if (!record.isEmpty())
        {
            Q_EMIT signalMetaRecord(ctx.first, ctx.second, record);
        }
        else
        {
            Q_EMIT signalMetaFinished(ctx.first, false);
        }

        return;
    }

    if (op == Private::Op::DownloadRecord)
    {
        // A record that cannot be fetched still leaves a good
        // download behind: finish without a generated sidecar rather
        // than raising an error.
        const QJsonObject record = (reply->error() == QNetworkReply::NoError)
                                       ? QJsonDocument::fromJson(reply->readAll()).object()
                                       : QJsonObject();
        reply->deleteLater();

        if (!record.isEmpty())
        {
            Q_EMIT signalAssetRecord(ctx.first, ctx.second, record);
        }
        else
        {
            Q_EMIT signalDownloadFinished(ctx.first, true);
        }

        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        Q_EMIT signalError(reply->errorString());
        reply->deleteLater();
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    reply->deleteLater();

    switch (op)
    {
        case Private::Op::ListAlbums:
        {
            QList<ImmichAlbum> albums;
            for (const QJsonValue& val : doc.array())
            {
                const QJsonObject obj = val.toObject();
                ImmichAlbum album;
                album.id               = obj.value(QStringLiteral("id")).toString();
                album.name             = obj.value(QStringLiteral("albumName")).toString();
                album.assetCount       = obj.value(QStringLiteral("assetCount")).toInt();
                album.startDate        = obj.value(QStringLiteral("startDate")).toString();
                album.endDate          = obj.value(QStringLiteral("endDate")).toString();
                album.thumbnailAssetId = obj.value(QStringLiteral("albumThumbnailAssetId")).toString();
                albums.append(album);
            }
            Q_EMIT signalAlbums(albums);
            break;
        }

        case Private::Op::CreateAlbum:
        {
            const QJsonObject obj = doc.object();
            ImmichAlbum album;
            album.id   = obj.value(QStringLiteral("id")).toString();
            album.name = obj.value(QStringLiteral("albumName")).toString();
            Q_EMIT signalAlbumCreated(album);
            break;
        }

        case Private::Op::AddToAlbum:
        {
            // The response is one result per asset; an asset
            // already in the album comes back success=false
            // ("duplicate"), so "changed" is exactly "at least
            // one asset was actually added".
            bool changed = false;

            for (const QJsonValue& val : doc.array())
            {
                if (val.toObject().value(QStringLiteral("success")).toBool())
                {
                    changed = true;
                    break;
                }
            }

            Q_EMIT signalAlbumUpdated(changed);
            break;
        }

        case Private::Op::ListTags:
        {
            QList<ImmichTag> tags;

            for (const QJsonValue& val : doc.array())
            {
                const QJsonObject obj = val.toObject();
                ImmichTag tag;
                tag.id   = obj.value(QStringLiteral("id")).toString();
                tag.name = obj.value(QStringLiteral("value")).toString();

                if (tag.name.isEmpty())
                {
                    tag.name = obj.value(QStringLiteral("name")).toString();
                }

                if (!tag.id.isEmpty() && !tag.name.isEmpty())
                {
                    tags.append(tag);
                }
            }

            Q_EMIT signalTags(tags);
            break;
        }

        case Private::Op::Upload:
        {
            const QJsonObject obj = doc.object();
            Q_EMIT signalUploadSucceeded(obj.value(QStringLiteral("id")).toString(),
                                         obj.value(QStringLiteral("status")).toString());
            break;
        }

        case Private::Op::AlbumAssets:
        {
            if (d->listingGenByReply.take(reply) != d->listingGen)
            {
                // A page from a superseded listing: drop it and let
                // its pagination chain die here.
                break;
            }

            const QJsonObject assetsObj = doc.object().value(QStringLiteral("assets")).toObject();
            const QJsonArray items = assetsObj.value(QStringLiteral("items")).toArray();

            for (const QJsonValue& val : items)
            {
                const QJsonObject obj = val.toObject();
                ImmichAssetRef asset;
                asset.id            = obj.value(QStringLiteral("id")).toString();
                asset.fileName      = obj.value(QStringLiteral("originalFileName")).toString();
                asset.fileCreatedAt = obj.value(QStringLiteral("fileCreatedAt")).toString();
                d->listingAccum.append(asset);
            }

            // Follow the search pagination until the server stops
            // handing out a next page.
            const QString nextPage = assetsObj.value(QStringLiteral("nextPage")).toString();

            if (!nextPage.isEmpty())
            {
                fetchListingPage(nextPage.toInt());
                break;
            }

            const QList<ImmichAssetRef> assets = d->listingAccum;
            d->listingAccum.clear();
            Q_EMIT signalAlbumAssets(assets);
            break;
        }

        case Private::Op::BulkCheck:
        {
            QMap<QString, QString> duplicates;
            const QJsonArray results = doc.object().value(QStringLiteral("results")).toArray();

            for (const QJsonValue& val : results)
            {
                const QJsonObject obj = val.toObject();

                if (obj.value(QStringLiteral("action")).toString() == QLatin1String("reject"))
                {
                    const QString assetId = obj.value(QStringLiteral("assetId")).toString();

                    if (!assetId.isEmpty())
                    {
                        duplicates.insert(obj.value(QStringLiteral("id")).toString(), assetId);
                    }
                }
            }

            Q_EMIT signalUploadCheck(duplicates);
            break;
        }

        case Private::Op::DownloadOriginal:
        case Private::Op::DownloadSidecar:
        case Private::Op::None:
            break;
    }
}

} // namespace DigikamGenericImmichPlugin

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
    enum class Op { None, ListAlbums, CreateAlbum, AddToAlbum, Upload,
                    AlbumAssets, DownloadOriginal, DownloadSidecar };
    QHash<QNetworkReply*, Op> ops;

    // Download context per reply: asset id and destination path.
    QHash<QNetworkReply*, QPair<QString, QString> > downloadCtx;
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

    // Same field contract the Python prototype verified live:
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

void ImmichTalker::listAlbumAssets(const QString& albumId)
{
    // One large page instead of following nextPage: albums beyond
    // a thousand assets are out of scope for the current pull.
    const QJsonObject obj {
        { QStringLiteral("albumIds"), QJsonArray { albumId } },
        { QStringLiteral("size"),     1000 }
    };

    QNetworkRequest req = makeRequest(QStringLiteral("/search/metadata"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QNetworkReply* const reply = d->net->post(req, QJsonDocument(obj).toJson());
    d->ops.insert(reply, Private::Op::AlbumAssets);
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

    if (op == Private::Op::DownloadOriginal || op == Private::Op::DownloadSidecar)
    {
        const QString& assetId    = ctx.first;
        const QString& targetPath = ctx.second;
        const int httpStatus      = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body     = (reply->error() == QNetworkReply::NoError) ? reply->readAll()
                                                                               : QByteArray();
        const bool failed         = (reply->error() != QNetworkReply::NoError) && httpStatus != 404;
        reply->deleteLater();

        if (op == Private::Op::DownloadSidecar)
        {
            // The original is already on disk; a missing (404) or
            // unfetchable sidecar does not fail the download.
            if (!failed && httpStatus == 200 && !body.isEmpty())
            {
                QFile file(targetPath);
                if (file.open(QIODevice::WriteOnly))
                {
                    file.write(body);
                }
            }

            Q_EMIT signalDownloadFinished(assetId, true);
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
                album.id         = obj.value(QStringLiteral("id")).toString();
                album.name       = obj.value(QStringLiteral("albumName")).toString();
                album.assetCount = obj.value(QStringLiteral("assetCount")).toInt();
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
            Q_EMIT signalAlbumUpdated();
            break;

        case Private::Op::Upload:
        {
            const QJsonObject obj = doc.object();
            Q_EMIT signalUploadSucceeded(obj.value(QStringLiteral("id")).toString(),
                                         obj.value(QStringLiteral("status")).toString());
            break;
        }

        case Private::Op::AlbumAssets:
        {
            QList<ImmichAssetRef> assets;
            const QJsonArray items = doc.object()
                                         .value(QStringLiteral("assets")).toObject()
                                         .value(QStringLiteral("items")).toArray();
            for (const QJsonValue& val : items)
            {
                const QJsonObject obj = val.toObject();
                ImmichAssetRef asset;
                asset.id       = obj.value(QStringLiteral("id")).toString();
                asset.fileName = obj.value(QStringLiteral("originalFileName")).toString();
                assets.append(asset);
            }
            Q_EMIT signalAlbumAssets(assets);
            break;
        }

        case Private::Op::DownloadOriginal:
        case Private::Op::DownloadSidecar:
        case Private::Op::None:
            break;
    }
}

} // namespace DigikamGenericImmichPlugin

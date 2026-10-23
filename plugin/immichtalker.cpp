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

// KDE includes

#include <klocalizedstring.h>

// Local includes

#include "immichsession.h"

namespace DigikamGenericImmichPlugin
{

namespace
{

QJsonArray idsArray(const QStringList& ids)
{
    QJsonArray arr;

    for (const QString& id : ids)
    {
        arr.append(id);
    }

    return arr;
}

QStringList idsFromArray(const QJsonArray& arr)
{
    QStringList ids;

    for (const QJsonValue& v : arr)
    {
        ids.append(v.toObject().value(QStringLiteral("id")).toString());
    }

    return ids;
}

ImmichAlbum albumFromJson(const QJsonObject& obj)
{
    ImmichAlbum album;
    album.id               = obj.value(QStringLiteral("id")).toString();
    album.name             = obj.value(QStringLiteral("albumName")).toString();
    album.assetCount       = obj.value(QStringLiteral("assetCount")).toInt();
    album.startDate        = obj.value(QStringLiteral("startDate")).toString();
    album.endDate          = obj.value(QStringLiteral("endDate")).toString();
    album.thumbnailAssetId = obj.value(QStringLiteral("albumThumbnailAssetId")).toString();
    return album;
}

// The asset items of a /search/metadata response document.
QJsonArray searchItems(const QJsonObject& doc)
{
    return doc.value(QStringLiteral("assets")).toObject()
              .value(QStringLiteral("items")).toArray();
}

enum class SidecarWrite { Written, Identical, Failed };

// Write a downloaded sidecar only when its bytes differ from
// what is already on disk: rewriting an identical sidecar would
// bump the file's time and masquerade as a local metadata edit.
SidecarWrite writeSidecarIfChanged(const QString& path, const QByteArray& body)
{
    QFile existing(path);
    const QByteArray current = existing.open(QIODevice::ReadOnly)
                                   ? existing.readAll()
                                   : QByteArray();
    existing.close();

    if (current == body)
    {
        return SidecarWrite::Identical;
    }

    QFile file(path);

    if (file.open(QIODevice::WriteOnly) && (file.write(body) == body.size()))
    {
        return SidecarWrite::Written;
    }

    return SidecarWrite::Failed;
}

} // namespace

class Q_DECL_HIDDEN ImmichTalker::Private
{
public:

    ImmichSession*         session = nullptr;
    QNetworkAccessManager* net     = nullptr;

    // What a pending reply should be parsed as when it finishes.
    enum class Op { None, ListAlbums, ListTags, CreateAlbum, AddToAlbum, Upload,
                    AlbumAssets, DownloadOriginal, DownloadSidecar,
                    DownloadRecord, MetaSidecar, MetaRecord,
                    BulkCheck, Thumbnail,
                    SyncRecord, PatchAsset, CreateTag, BulkTagAssets,
                    SearchByName, AlbumsForAsset, DeleteAssets,
                    CreateStack, FetchStack };
    QHash<QNetworkReply*, Op> ops;
    // Sync context per reply: the asset id (or tag name / id).
    QHash<QNetworkReply*, QString> syncCtx;

    // Download context per reply: asset id and destination path.
    QHash<QNetworkReply*, QPair<QString, QString> > downloadCtx;

    // Album/tag asset listings accumulate across search pages.
    QList<ImmichAssetRef> pagingAccum;
    QString               pagingAlbumId;
    QString               pagingTagId;
    // Bumped by every new listing; page replies stamped with an
    // older generation are dropped, so a superseded listing (the
    // user clicked another source mid-listing) cannot leak its
    // assets into the current one.
    int                   pagingGen = 0;
    QHash<QNetworkReply*, int> pagingGenByReply;
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
    // The session stores the normalized server root; the REST
    // API lives below /api. Without this the requests land on
    // the web frontend, which answers JSON-only requests
    // with 406.
    QNetworkRequest req(QUrl(d->session->url() + QStringLiteral("/api") + path));
    req.setRawHeader("x-api-key", d->session->apiKey().toUtf8());
    req.setRawHeader("Accept", "application/json");
    return req;
}

QNetworkReply* ImmichTalker::sendGet(const QString& path)
{
    return d->net->get(makeRequest(path));
}

QNetworkReply* ImmichTalker::sendJson(const QString& method, const QString& path,
                                      const QJsonObject& body)
{
    QNetworkRequest req = makeRequest(path);

    // The content type is not decoration: without it Qt posts
    // the body as application/x-www-form-urlencoded, Immich's
    // JSON body parser never runs, and the fields are missing
    // when the DTO validates (a 400 against the real server,
    // first shipped by createAlbum, which set the header in
    // some copies of this boilerplate and not others).
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    return d->net->sendCustomRequest(req, method.toUtf8(), QJsonDocument(body).toJson());
}

void ImmichTalker::emitReplyError(QNetworkReply* reply)
{
    // Qt's errorString() alone ("server replied with status
    // code 400") discards the explanation Immich sent: the
    // error body's message field carries it, as a string or
    // as a list of validation failures. Append it where the
    // server sent one, so the next rejection explains itself
    // in the window's status line.
    QString message = reply->errorString();
    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    const QJsonValue reason = body.value(QStringLiteral("message"));

    if (reason.isString() && !reason.toString().isEmpty())
    {
        message += QStringLiteral(": ") + reason.toString();
    }
    else if (reason.isArray())
    {
        QStringList reasons;

        for (const QJsonValue& entry : reason.toArray())
        {
            if (entry.isString())
            {
                reasons.append(entry.toString());
            }
        }

        if (!reasons.isEmpty())
        {
            message += QStringLiteral(": ") + reasons.join(QStringLiteral("; "));
        }
    }

    Q_EMIT signalError(message);
}

QJsonObject ImmichTalker::takeSyncReply(QNetworkReply* reply, QString& ctx, int& httpStatus)
{
    ctx        = d->syncCtx.take(reply);
    httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();
    return obj;
}

void ImmichTalker::listAlbums()
{
    QNetworkReply* const reply = sendGet(QStringLiteral("/albums"));
    d->ops.insert(reply, Private::Op::ListAlbums);
}

void ImmichTalker::createAlbum(const QString& name)
{
    const QJsonObject obj { { QStringLiteral("albumName"), name } };
    QNetworkReply* const reply = sendJson(QStringLiteral("POST"),
                                          QStringLiteral("/albums"), obj);
    d->ops.insert(reply, Private::Op::CreateAlbum);
}

void ImmichTalker::addToAlbum(const QString& albumId, const QStringList& assetIds)
{
    const QJsonObject obj { { QStringLiteral("ids"), idsArray(assetIds) } };
    QNetworkReply* const reply = sendJson(QStringLiteral("PUT"),
                                          QStringLiteral("/albums/%1/assets").arg(albumId), obj);
    d->ops.insert(reply, Private::Op::AddToAlbum);
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
        Q_EMIT signalError(i18n("Cannot open %1", filePath));
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
}

void ImmichTalker::listTags()
{
    QNetworkReply* const reply = sendGet(QStringLiteral("/tags"));
    d->ops.insert(reply, Private::Op::ListTags);
}

void ImmichTalker::listAlbumAssets(const QString& albumId)
{
    d->pagingAccum.clear();
    d->pagingAlbumId = albumId;
    d->pagingTagId.clear();
    ++d->pagingGen;
    fetchSearchPage(1);
}

void ImmichTalker::listTagAssets(const QString& tagId)
{
    d->pagingAccum.clear();
    d->pagingTagId   = tagId;
    d->pagingAlbumId.clear();
    ++d->pagingGen;
    fetchSearchPage(1);
}

void ImmichTalker::fetchThumbnail(const QString& assetId)
{
    // Pin the small generated size (250px) explicitly: it is
    // what the endpoint defaults to today, and the previews and
    // covers have no use for the 1440px preview size.
    QNetworkReply* const reply = sendGet(
        QStringLiteral("/assets/%1/thumbnail?size=thumbnail").arg(assetId));
    d->ops.insert(reply, Private::Op::Thumbnail);
    d->thumbCtx.insert(reply, assetId);
}

void ImmichTalker::fetchSearchPage(int page)
{
    QJsonObject obj {
        { QStringLiteral("size"), 250 },
        { QStringLiteral("page"), page }
    };

    if (!d->pagingTagId.isEmpty())
    {
        obj.insert(QStringLiteral("tagIds"), QJsonArray { d->pagingTagId });
    }
    else
    {
        obj.insert(QStringLiteral("albumIds"), QJsonArray { d->pagingAlbumId });
    }

    QNetworkReply* const reply = sendJson(QStringLiteral("POST"),
                                          QStringLiteral("/search/metadata"), obj);
    d->ops.insert(reply, Private::Op::AlbumAssets);
    d->pagingGenByReply.insert(reply, d->pagingGen);
}

void ImmichTalker::downloadSidecarOnly(const QString& assetId, const QString& targetPath)
{
    QNetworkReply* const reply = sendGet(
        QStringLiteral("/assets/%1/sidecar").arg(assetId));
    d->ops.insert(reply, Private::Op::MetaSidecar);
    d->downloadCtx.insert(reply, qMakePair(assetId, targetPath + QStringLiteral(".xmp")));
}

void ImmichTalker::downloadAsset(const QString& assetId, const QString& targetPath)
{
    QNetworkReply* const reply = sendGet(
        QStringLiteral("/assets/%1/original").arg(assetId));
    d->ops.insert(reply, Private::Op::DownloadOriginal);
    d->downloadCtx.insert(reply, qMakePair(assetId, targetPath));
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
    QNetworkReply* const reply = sendJson(QStringLiteral("POST"),
                                          QStringLiteral("/assets/bulk-upload-check"), obj);
    d->ops.insert(reply, Private::Op::BulkCheck);
}

void ImmichTalker::fetchAssetForSync(const QString& assetId)
{
    QNetworkReply* const reply = sendGet(
        QStringLiteral("/assets/%1").arg(assetId));
    d->ops.insert(reply, Private::Op::SyncRecord);
    d->syncCtx.insert(reply, assetId);
}

void ImmichTalker::updateAsset(const QString& assetId, const QJsonObject& body)
{
    QNetworkReply* const reply = sendJson(QStringLiteral("PUT"),
                                          QStringLiteral("/assets/%1").arg(assetId), body);
    d->ops.insert(reply, Private::Op::PatchAsset);
    d->syncCtx.insert(reply, assetId);
}

void ImmichTalker::createTag(const QString& name)
{
    const QJsonObject obj { { QStringLiteral("name"), name } };
    QNetworkReply* const reply = sendJson(QStringLiteral("POST"),
                                          QStringLiteral("/tags"), obj);
    d->ops.insert(reply, Private::Op::CreateTag);
    d->syncCtx.insert(reply, name);
}

void ImmichTalker::searchByFileName(const QString& name, const QString& albumId)
{
    QJsonObject obj {
        { QStringLiteral("originalFileName"), name },
        { QStringLiteral("size"),             250  }
    };

    if (!albumId.isEmpty())
    {
        obj.insert(QStringLiteral("albumIds"), QJsonArray { albumId });
    }

    QNetworkReply* const reply = sendJson(QStringLiteral("POST"),
                                          QStringLiteral("/search/metadata"), obj);
    d->ops.insert(reply, Private::Op::SearchByName);
    d->syncCtx.insert(reply, name);
}

void ImmichTalker::listAlbumsForAsset(const QString& assetId)
{
    QNetworkReply* const reply = sendGet(
        QStringLiteral("/albums?assetId=%1").arg(assetId));
    d->ops.insert(reply, Private::Op::AlbumsForAsset);
    d->syncCtx.insert(reply, assetId);
}

void ImmichTalker::createStack(const QStringList& assetIds)
{
    const QJsonObject obj { { QStringLiteral("assetIds"), idsArray(assetIds) } };
    QNetworkReply* const reply = sendJson(QStringLiteral("POST"),
                                          QStringLiteral("/stacks"), obj);
    d->ops.insert(reply, Private::Op::CreateStack);
    d->syncCtx.insert(reply, assetIds.value(0));
}

void ImmichTalker::fetchStack(const QString& stackId)
{
    QNetworkReply* const reply = sendGet(
        QStringLiteral("/stacks/%1").arg(stackId));
    d->ops.insert(reply, Private::Op::FetchStack);
    d->syncCtx.insert(reply, stackId);
}

void ImmichTalker::deleteAssets(const QStringList& assetIds)
{
    const QJsonObject obj { { QStringLiteral("ids"), idsArray(assetIds) } };
    QNetworkReply* const reply = sendJson(QStringLiteral("DELETE"),
                                          QStringLiteral("/assets"), obj);
    d->ops.insert(reply, Private::Op::DeleteAssets);
}

void ImmichTalker::bulkTagAssets(const QString& tagId, const QStringList& assetIds)
{
    // TagBulkAssetsDto in 3.3.3: tagIds is an array too (a
    // single "tagId" field is rejected with a 400, verified
    // against the live server).
    const QJsonObject obj {
        { QStringLiteral("tagIds"),   QJsonArray { tagId } },
        { QStringLiteral("assetIds"), idsArray(assetIds)   }
    };

    QNetworkReply* const reply = sendJson(QStringLiteral("PUT"),
                                          QStringLiteral("/tags/assets"), obj);
    d->ops.insert(reply, Private::Op::BulkTagAssets);
    d->syncCtx.insert(reply, tagId);
}

void ImmichTalker::cancel()
{
    // A named copy, not the temporary keys() hands back:
    // aborting a reply finishes it, which removes it from
    // d->ops while the loop runs.
    const QList<QNetworkReply*> replies = d->ops.keys();

    for (QNetworkReply* const reply : replies)
    {
        reply->abort();
    }
}

void ImmichTalker::slotFinished(QNetworkReply* reply)
{
    const Private::Op op = d->ops.take(reply);
    // The download context (asset id, target path). Named
    // apart from the per-case sync contexts below, which hold
    // a single id: the two shared the name ctx until now.
    const QPair<QString, QString> dlCtx = d->downloadCtx.take(reply);

    if (op == Private::Op::DownloadOriginal || op == Private::Op::DownloadSidecar
        || op == Private::Op::MetaSidecar)
    {
        const QString& assetId    = dlCtx.first;
        const QString& targetPath = dlCtx.second;
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

            if (httpStatus == 200 && !body.isEmpty())
            {
                const SidecarWrite result = writeSidecarIfChanged(targetPath, body);

                if (result == SidecarWrite::Written)
                {
                    Q_EMIT signalMetaFinished(assetId, true);
                    return;
                }

                if (result == SidecarWrite::Identical)
                {
                    Q_EMIT signalMetaFinished(assetId, false);
                    return;
                }
            }

            const QString originalPath = targetPath.left(targetPath.size() - 4);   // strip ".xmp"
            QNetworkReply* const recordReply = sendGet(
                QStringLiteral("/assets/%1").arg(assetId));
            d->ops.insert(recordReply, Private::Op::MetaRecord);
            d->downloadCtx.insert(recordReply, qMakePair(assetId, originalPath));
            return;
        }

        if (op == Private::Op::DownloadSidecar)
        {
            // The original is already on disk; a missing (404) or
            // unfetchable sidecar does not fail the download. An
            // identical local sidecar is left untouched.
            if (!failed && httpStatus == 200 && !body.isEmpty()
                && writeSidecarIfChanged(targetPath, body) != SidecarWrite::Failed)
            {
                Q_EMIT signalDownloadFinished(assetId, true);
                return;
            }

            // No server sidecar: fetch the asset record so the window
            // can generate a sidecar from the Immich metadata (the
            // tags above all) instead of the photo arriving bare.
            const QString originalPath = targetPath.left(targetPath.size() - 4);   // strip ".xmp"
            QNetworkReply* const recordReply = sendGet(
                QStringLiteral("/assets/%1").arg(assetId));
            d->ops.insert(recordReply, Private::Op::DownloadRecord);
            d->downloadCtx.insert(recordReply, qMakePair(assetId, originalPath));
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
        QNetworkReply* const sidecarReply = sendGet(
            QStringLiteral("/assets/%1/sidecar").arg(assetId));
        d->ops.insert(sidecarReply, Private::Op::DownloadSidecar);
        d->downloadCtx.insert(sidecarReply, qMakePair(assetId, targetPath + QStringLiteral(".xmp")));
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

    if (op == Private::Op::CreateStack)
    {
        QString ctx;
        int     httpStatus = 0;
        const QJsonObject obj = takeSyncReply(reply, ctx, httpStatus);
        Q_EMIT signalStackCreated(ctx, obj.value(QStringLiteral("id")).toString(), httpStatus);
        return;
    }

    if (op == Private::Op::FetchStack)
    {
        QString ctx;
        int     httpStatus = 0;
        const QJsonObject obj = takeSyncReply(reply, ctx, httpStatus);
        Q_EMIT signalStackFetched(ctx, idsFromArray(obj.value(QStringLiteral("assets")).toArray()),
                                  httpStatus);
        return;
    }

    if (op == Private::Op::SearchByName)
    {
        // A failed search is reported like any other failed
        // request; the window's error handling completes the
        // identity step it was waiting for. Emitting an empty
        // result instead would read as "no such photo", which
        // sends the file down the wrong path.
        if (reply->error() != QNetworkReply::NoError)
        {
            d->syncCtx.take(reply);
            emitReplyError(reply);
            reply->deleteLater();
            return;
        }

        QString ctx;
        int     httpStatus = 0;
        const QJsonObject obj = takeSyncReply(reply, ctx, httpStatus);
        Q_EMIT signalIdentitySearch(ctx, searchItems(obj));
        return;
    }

    if (op == Private::Op::AlbumsForAsset)
    {
        if (reply->error() != QNetworkReply::NoError)
        {
            d->syncCtx.take(reply);
            emitReplyError(reply);
            reply->deleteLater();
            return;
        }

        const QString    ctx = d->syncCtx.take(reply);
        const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        reply->deleteLater();
        Q_EMIT signalAlbumsForAsset(ctx, idsFromArray(arr));
        return;
    }

    if (op == Private::Op::DeleteAssets)
    {
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        Q_EMIT signalAssetsDeleted(httpStatus);
        return;
    }

    if (op == Private::Op::SyncRecord || op == Private::Op::PatchAsset
        || op == Private::Op::CreateTag || op == Private::Op::BulkTagAssets)
    {
        // The sync flow wants the HTTP status itself (a 403 is
        // information, not a generic error), so these replies
        // bypass the shared error branch below.
        QString ctx;
        int     httpStatus = 0;
        const QJsonObject obj = takeSyncReply(reply, ctx, httpStatus);

        switch (op)
        {
            case Private::Op::SyncRecord:
                Q_EMIT signalSyncRecord(ctx, obj, httpStatus);
                break;
            case Private::Op::PatchAsset:
                Q_EMIT signalAssetPatched(ctx, httpStatus);
                break;
            case Private::Op::CreateTag:
                Q_EMIT signalTagCreated(ctx, obj.value(QStringLiteral("id")).toString(), httpStatus);
                break;
            case Private::Op::BulkTagAssets:
                Q_EMIT signalBulkTagged(ctx, httpStatus);
                break;
            default:
                break;
        }

        return;
    }

    if (op == Private::Op::MetaRecord || op == Private::Op::DownloadRecord)
    {
        // A record that cannot be fetched still leaves a good
        // download behind: the meta refresh finishes unchanged,
        // the download finishes without a generated sidecar,
        // rather than either raising an error.
        const QJsonObject record = (reply->error() == QNetworkReply::NoError)
                                       ? QJsonDocument::fromJson(reply->readAll()).object()
                                       : QJsonObject();
        reply->deleteLater();

        if (op == Private::Op::MetaRecord)
        {
            if (!record.isEmpty())
            {
                Q_EMIT signalMetaRecord(dlCtx.first, dlCtx.second, record);
            }
            else
            {
                Q_EMIT signalMetaFinished(dlCtx.first, false);
            }
        }
        else
        {
            if (!record.isEmpty())
            {
                Q_EMIT signalAssetRecord(dlCtx.first, dlCtx.second, record);
            }
            else
            {
                Q_EMIT signalDownloadFinished(dlCtx.first, true);
            }
        }

        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        emitReplyError(reply);
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
            const QJsonArray items = doc.array();

            for (const QJsonValue& val : items)
            {
                albums.append(albumFromJson(val.toObject()));
            }

            Q_EMIT signalAlbums(albums);
            break;
        }

        case Private::Op::CreateAlbum:
        {
            Q_EMIT signalAlbumCreated(albumFromJson(doc.object()));
            break;
        }

        case Private::Op::AddToAlbum:
        {
            // The response is one result per asset; an asset
            // already in the album comes back success=false
            // ("duplicate"), so "changed" is exactly "at least
            // one asset was actually added".
            bool changed = false;
            const QJsonArray items = doc.array();

            for (const QJsonValue& val : items)
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
            const QJsonArray items = doc.array();

            for (const QJsonValue& val : items)
            {
                const QJsonObject obj = val.toObject();
                ImmichTag tag;
                tag.id   = obj.value(QStringLiteral("id")).toString();
                tag.name = obj.value(QStringLiteral("value")).toString();

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
            if (d->pagingGenByReply.take(reply) != d->pagingGen)
            {
                // A page from a superseded listing: drop it and let
                // its pagination chain die here.
                break;
            }

            const QJsonObject assetsObj = doc.object().value(QStringLiteral("assets")).toObject();
            const QJsonArray items = searchItems(doc.object());

            for (const QJsonValue& val : items)
            {
                const QJsonObject obj = val.toObject();
                ImmichAssetRef asset;
                asset.id            = obj.value(QStringLiteral("id")).toString();
                asset.fileName      = obj.value(QStringLiteral("originalFileName")).toString();
                asset.fileCreatedAt = obj.value(QStringLiteral("fileCreatedAt")).toString();
                d->pagingAccum.append(asset);
            }

            // Follow the search pagination until the server stops
            // handing out a next page.
            const QString nextPage = assetsObj.value(QStringLiteral("nextPage")).toString();

            if (!nextPage.isEmpty())
            {
                fetchSearchPage(nextPage.toInt());
                break;
            }

            const QList<ImmichAssetRef> assets = d->pagingAccum;
            d->pagingAccum.clear();
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

        default:
            // Every other op returned earlier (downloads,
            // records, the sync family) or has no parse step.
            break;
    }
}

} // namespace DigikamGenericImmichPlugin

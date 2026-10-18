// SPDX-FileCopyrightText: 2026 Christian McHugh
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtGlobal>

namespace DigikamGenericImmichPlugin
{

// The metadata fields the bridge syncs, in API space: rating 0
// means unrated (digiKam's own -1 sentinel is mapped to 0 by the
// caller; Immich's -1 "rejected" is a pick-label concept digiKam
// keeps separately, so the sync never produces it), tags are the
// flat value strings both sides share.
struct SyncFields
{
    QStringList tags;
    int         rating      = 0;
    QString     description;
    bool        hasGps      = false;
    double      latitude    = 0.0;
    double      longitude   = 0.0;
};

// One side of the comparison, from an Immich asset record:
// tags from the tags array, rating/description/GPS from
// exifInfo (with the top-level rating fallback older records
// need), plus the record's updatedAt and checksum for the
// snapshot bookkeeping.
struct ServerFields
{
    SyncFields fields;
    QString    updatedAt;
    QString    checksum;
};

inline ServerFields serverFieldsFromRecord(const QJsonObject& record)
{
    ServerFields out;
    const QJsonObject exif = record.value(QStringLiteral("exifInfo")).toObject();

    for (const QJsonValue& value : record.value(QStringLiteral("tags")).toArray())
    {
        const QJsonObject tag = value.toObject();
        QString           name = tag.value(QStringLiteral("value")).toString();

        if (name.isEmpty())
        {
            name = tag.value(QStringLiteral("name")).toString();
        }

        if (!name.isEmpty() && !out.fields.tags.contains(name))
        {
            out.fields.tags << name;
        }
    }

    const QJsonValue rating = exif.value(QStringLiteral("rating"));

    if (rating.isDouble())
    {
        out.fields.rating = rating.toInt();
    }
    else
    {
        out.fields.rating = record.value(QStringLiteral("rating")).toInt();
    }

    if (out.fields.rating < 0)
    {
        out.fields.rating = 0;
    }

    out.fields.description = exif.value(QStringLiteral("description")).toString();

    const QJsonValue lat = exif.value(QStringLiteral("latitude"));
    const QJsonValue lon = exif.value(QStringLiteral("longitude"));

    if (lat.isDouble() && lon.isDouble())
    {
        out.fields.hasGps    = true;
        out.fields.latitude  = lat.toDouble();
        out.fields.longitude = lon.toDouble();
    }

    out.updatedAt = record.value(QStringLiteral("updatedAt")).toString();
    out.checksum  = record.value(QStringLiteral("checksum")).toString();
    return out;
}

// --- snapshot gating -------------------------------------------------

enum class GateAction
{
    Push,        // write the plan as computed
    LeaveAlone,  // only the server changed: keep its state
    DropScalars  // both changed and the server is newer:
                 // tags still merge, scalars stay server-side
};

struct GateResult
{
    GateAction action   = GateAction::Push;
    bool       conflict = false;
};

// Decides what an export sync may do with one asset, from the
// snapshot taken at the last sync and both sides' current state.
// Timestamps are only the tiebreak for the both-changed case.
inline GateResult gateSync(bool haveSnapshot,
                           const QString& snapshotServerUpdatedAt,
                           const QString& serverUpdatedAt,
                           const QString& snapshotDkHash,
                           const QString& dkHash,
                           const QDateTime& serverTime,
                           const QDateTime& localTime)
{
    GateResult result;

    if (!haveSnapshot)
    {
        return result;
    }

    const bool serverChanged = (snapshotServerUpdatedAt != serverUpdatedAt);
    const bool dkChanged     = (snapshotDkHash != dkHash);

    if (!serverChanged)
    {
        return result;
    }

    if (!dkChanged)
    {
        result.action = GateAction::LeaveAlone;
        return result;
    }

    result.conflict = true;

    if (serverTime.isValid() && localTime.isValid() && serverTime > localTime)
    {
        result.action = GateAction::DropScalars;
    }

    return result;
}

// What an export run should write to one existing asset.
// Tag changes are add-only by design: a tag the server has and
// digiKam does not is never removed (it may have been set in
// Immich itself, and an export is not a deletion channel).
struct SyncPlan
{
    QStringList tagsToAdd;
    bool        setRating     = false;
    int         rating        = 0;   // 0 = clear to unrated
    bool        setDescription = false;
    QString     description;
    bool        setGps        = false;
    double      latitude      = 0.0;
    double      longitude     = 0.0;

    bool any() const
    {
        return !tagsToAdd.isEmpty() || setRating || setDescription || setGps;
    }

    bool scalars() const
    {
        return setRating || setDescription || setGps;
    }

    void dropScalars()
    {
        setRating = setDescription = setGps = false;
    }
};

inline SyncPlan planSync(const SyncFields& dk, const SyncFields& server)
{
    SyncPlan plan;

    for (const QString& tag : dk.tags)
    {
        if (!server.tags.contains(tag))
        {
            plan.tagsToAdd << tag;
        }
    }

    // Ratings are 0..5 here (0 = unrated); clamp defensively so
    // a stray digiKam sentinel (-1 = unset) can never reach the
    // server, where -1 means "rejected".
    const int dkRating = qBound(0, dk.rating, 5);

    if (dkRating != server.rating)
    {
        plan.setRating = true;
        plan.rating    = dkRating;
    }

    if (dk.description != server.description)
    {
        plan.setDescription = true;
        plan.description    = dk.description;
    }

    if (dk.hasGps
        && (!server.hasGps
            || qAbs(dk.latitude - server.latitude) > 1e-5
            || qAbs(dk.longitude - server.longitude) > 1e-5))
    {
        plan.setGps    = true;
        plan.latitude  = dk.latitude;
        plan.longitude = dk.longitude;
    }

    return plan;
}

// A stable fingerprint of the synced fields, used by the sync
// snapshots to tell whether a side changed since the last sync
// without storing the values themselves.
inline QString syncFieldHash(const SyncFields& f)
{
    QStringList tags = f.tags;
    tags.sort();
    const QString material = tags.join(QLatin1Char('\n'))
                             + QLatin1Char('|') + QString::number(f.rating)
                             + QLatin1Char('|') + f.description;
    return QString::fromLatin1(
        QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha1).toBase64());
}

// Per-asset sync snapshot: what the server held and what
// digiKam held the last time this asset was synced (in either
// direction). Keyed by asset id in a JSON file the window owns.
struct SyncSnapshot
{
    QString checksum;
    QString serverUpdatedAt;
    QString serverHash;
    QString dkHash;

    QJsonObject toJson() const
    {
        return QJsonObject {
            { QStringLiteral("checksum"),        checksum        },
            { QStringLiteral("serverUpdatedAt"), serverUpdatedAt },
            { QStringLiteral("serverHash"),      serverHash      },
            { QStringLiteral("dkHash"),          dkHash          },
        };
    }

    static SyncSnapshot fromJson(const QJsonObject& obj)
    {
        SyncSnapshot s;
        s.checksum        = obj.value(QStringLiteral("checksum")).toString();
        s.serverUpdatedAt = obj.value(QStringLiteral("serverUpdatedAt")).toString();
        s.serverHash      = obj.value(QStringLiteral("serverHash")).toString();
        s.dkHash          = obj.value(QStringLiteral("dkHash")).toString();
        return s;
    }
};

// --- identity matching -----------------------------------------------
//
// Immich identifies an asset by content checksum only, so a file
// whose bytes changed (digiKam writing metadata into it, say)
// uploads as a brand new photo. The export therefore gives a
// checksum-missed file a second chance to be recognised: same
// file name, same capture time and same dimensions on the server
// means it is very likely the same photo, and a pixel comparison
// against the server's thumbnail then separates a metadata-only
// change (sync the metadata, upload nothing) from a real edit
// (upload and replace). The replace decision additionally
// requires the capture times to match exactly ("strict"): with
// no reliable time evidence the file uploads as new instead, so
// a stranger's photo is never overwritten on a name collision.

struct IdentityCandidate
{
    QString id;
    QString name;
    QString localDateTime;   // server wall-clock capture time
    QString createdAt;       // server ingest time (newest wins)
    int     width  = 0;
    int     height = 0;
};

struct IdentityMatch
{
    QString assetId;
    int     candidates  = 0;   // name+dimension matches found
    bool    strictTime  = false;
};

inline IdentityMatch chooseIdentityMatch(const QList<IdentityCandidate>& candidates,
                                         const QString& fileName,
                                         const QString& dateStr,
                                         int width, int height)
{
    IdentityMatch result;
    QList<IdentityCandidate> pool;

    for (const IdentityCandidate& c : candidates)
    {
        if (c.name != fileName)
        {
            continue;
        }

        const bool dims = (c.width == width && c.height == height)
                       || (c.width == height && c.height == width);

        if (dims)
        {
            pool.append(c);
        }
    }

    result.candidates = pool.size();

    if (pool.isEmpty())
    {
        return result;
    }

    QList<IdentityCandidate> timed;

    for (const IdentityCandidate& c : pool)
    {
        if (!dateStr.isEmpty() && c.localDateTime.left(19) == dateStr)
        {
            timed.append(c);
        }
    }

    if (!timed.isEmpty())
    {
        result.strictTime = true;
        pool = timed;
    }

    const IdentityCandidate* best = &pool.constFirst();

    for (const IdentityCandidate& c : pool)
    {
        if (c.createdAt > best->createdAt)
        {
            best = &c;
        }
    }

    result.assetId = best->id;
    return result;
}

// Mean absolute per-channel difference between two images at a
// common 16x16 grayscale-ish scale, 0..255; -1 when either side
// is missing. A server thumbnail is a lossy re-encode of the same
// pixels, so identical pictures score in the low single digits
// while any visible edit (a colour grade moves whole regions)
// scores far higher.
inline double imageDifference(const QImage& a, const QImage& b)
{
    if (a.isNull() || b.isNull())
    {
        return -1.0;
    }

    const QSize size(16, 16);
    const QImage x = a.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_RGB32);
    const QImage y = b.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                      .convertToFormat(QImage::Format_RGB32);
    double sum = 0.0;

    for (int row = 0 ; row < size.height() ; ++row)
    {
        const QRgb* const xr = reinterpret_cast<const QRgb*>(x.constScanLine(row));
        const QRgb* const yr = reinterpret_cast<const QRgb*>(y.constScanLine(row));

        for (int col = 0 ; col < size.width() ; ++col)
        {
            sum += qAbs(qRed(xr[col])   - qRed(yr[col]))
                 + qAbs(qGreen(xr[col]) - qGreen(yr[col]))
                 + qAbs(qBlue(xr[col])  - qBlue(yr[col]));
        }
    }

    return sum / (size.width() * size.height() * 3.0);
}

inline bool imagesLookSame(const QImage& a, const QImage& b)
{
    const double diff = imageDifference(a, b);
    return diff >= 0.0 && diff <= 10.0;
}

inline QJsonObject loadSyncState(const QString& path)
{
    QFile file(path);

    if (file.open(QIODevice::ReadOnly))
    {
        return QJsonDocument::fromJson(file.readAll()).object();
    }

    return QJsonObject();
}

inline void saveSyncState(const QString& path, const QJsonObject& state)
{
    QFile file(path);

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    }
}

} // namespace DigikamGenericImmichPlugin

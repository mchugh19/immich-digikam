// SPDX-FileCopyrightText: 2026 Christian McHugh
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace DigikamGenericImmichPlugin
{

// Build the XMP sidecar packet used in both directions: generated on
// upload from digiKam's database, and generated on download from the
// Immich asset record when the server holds no sidecar file. The shape
// mirrors the packet whose round-trip through Immich was verified
// against a live server (tags, rating, caption, GPS all land).
inline QString buildSidecarPacket(const QStringList& tags, int rating,
                                  const QString& description,
                                  bool hasGps, double latitude, double longitude)
{
    auto esc = [](QString s)
    {
        s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
        s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
        s.replace(QLatin1Char('>'), QLatin1String("&gt;"));
        s.replace(QLatin1Char('"'), QLatin1String("&quot;"));
        return s;
    };

    // GPS as degrees,decimal-minutes with hemisphere (the exif form),
    // e.g. 53.1054 -> "53,6.3240N"
    auto gps = [](double v, QChar pos, QChar neg)
    {
        const double a       = qAbs(v);
        const int    deg     = int(a);
        const double minutes = (a - deg) * 60.0;
        return QStringLiteral("%1,%2%3")
            .arg(deg)
            .arg(minutes, 0, 'f', 4)
            .arg(v >= 0 ? pos : neg);
    };

    QString xml;
    xml += QLatin1String("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n");
    xml += QLatin1String(" <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n");
    xml += QLatin1String("  <rdf:Description rdf:about=\"\">\n");

    if (!tags.isEmpty())
    {
        xml += QLatin1String("   <digiKam:TagsList xmlns:digiKam=\"http://www.digikam.org/ns/1.0/\">\n"
                             "    <rdf:Bag>\n");

        for (const QString& t : tags)
        {
            xml += QStringLiteral("     <rdf:li>%1</rdf:li>\n").arg(esc(t));
        }

        xml += QLatin1String("    </rdf:Bag>\n"
                             "   </digiKam:TagsList>\n");
    }

    if (rating != 0)
    {
        xml += QStringLiteral("   <xmp:Rating xmlns:xmp=\"http://ns.adobe.com/xap/1.0/\">%1</xmp:Rating>\n")
                   .arg(rating);
    }

    if (!description.isEmpty())
    {
        xml += QLatin1String("   <dc:description xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n"
                             "    <rdf:Alt>\n");
        xml += QStringLiteral("     <rdf:li xml:lang=\"x-default\">%1</rdf:li>\n").arg(esc(description));
        xml += QLatin1String("    </rdf:Alt>\n"
                             "   </dc:description>\n");
    }

    if (hasGps)
    {
        xml += QStringLiteral("   <exif:GPSLatitude xmlns:exif=\"http://ns.adobe.com/exif/1.0/\">%1</exif:GPSLatitude>\n")
                   .arg(gps(latitude, QLatin1Char('N'), QLatin1Char('S')));
        xml += QStringLiteral("   <exif:GPSLongitude xmlns:exif=\"http://ns.adobe.com/exif/1.0/\">%1</exif:GPSLongitude>\n")
                   .arg(gps(longitude, QLatin1Char('E'), QLatin1Char('W')));
    }

    xml += QLatin1String("  </rdf:Description>\n </rdf:RDF>\n</x:xmpmeta>\n");
    return xml;
}

} // namespace DigikamGenericImmichPlugin

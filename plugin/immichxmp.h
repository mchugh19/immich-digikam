// SPDX-FileCopyrightText: 2026 by Christian McHugh <mchugh19@users.noreply.github.com>
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QXmlStreamReader>
#include <QtGlobal>

#include <functional>

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

// Merge a generated packet with an existing one (the file's own
// sidecar, or its embedded packet): every property the generated
// packet does not set is carried over from the base verbatim, so
// uploading never silently drops XMP the file already carried
// (digiKam's own ColorLabel/PickLabel properties, say) just
// because digiKam's database fields were generated on top. The
// generated values win for the properties in generatedKeys.
// Namespace declarations found on the base packet's ancestors
// are re-declared on the generated description so preserved
// elements keep their prefixes.
inline QString mergeSidecarPackets(const QString& generated,
                                   const QString& base,
                                   const QStringList& generatedKeys)
{
    if (base.trimmed().isEmpty())
    {
        return generated;
    }

    auto escAttr = [](QString s)
    {
        s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
        s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
        s.replace(QLatin1Char('"'), QLatin1String("&quot;"));
        return s;
    };

    auto escText = [](QString s)
    {
        s.replace(QLatin1Char('&'), QLatin1String("&amp;"));
        s.replace(QLatin1Char('<'), QLatin1String("&lt;"));
        s.replace(QLatin1Char('>'), QLatin1String("&gt;"));
        return s;
    };

    QXmlStreamReader reader(base);
    QString preserved;
    QString extraDecls;
    bool    complete = false;

    // Serialize the element subtree the reader is positioned on
    // (a StartElement token) back to XML text.
    std::function<void()> copySubtree = [&]()
    {
        int depth = 0;

        do
        {
            const QXmlStreamReader::TokenType token = reader.tokenType();

            if (token == QXmlStreamReader::StartElement)
            {
                ++depth;
                preserved += QLatin1Char('<') + reader.qualifiedName().toString();

                const QXmlStreamNamespaceDeclarations decls = reader.namespaceDeclarations();

                for (const QXmlStreamNamespaceDeclaration& decl : decls)
                {
                    preserved += QStringLiteral(" xmlns:%1=\"%2\"")
                                     .arg(decl.prefix().toString(), escAttr(decl.namespaceUri().toString()));
                }

                const QXmlStreamAttributes attrs = reader.attributes();

                for (const QXmlStreamAttribute& attr : attrs)
                {
                    preserved += QStringLiteral(" %1=\"%2\"")
                                     .arg(attr.qualifiedName().toString(), escAttr(attr.value().toString()));
                }

                preserved += QLatin1Char('>');
            }
            else if (token == QXmlStreamReader::EndElement)
            {
                --depth;
                preserved += QStringLiteral("</%1>").arg(reader.qualifiedName().toString());
            }
            else if (token == QXmlStreamReader::Characters)
            {
                preserved += escText(reader.text().toString());
            }

            if (depth == 0)
            {
                break;
            }
        }
        while (reader.readNext() != QXmlStreamReader::Invalid);
    };

    while (reader.readNext() != QXmlStreamReader::Invalid)
    {
        if (reader.tokenType() == QXmlStreamReader::EndDocument)
        {
            complete = true;
            break;
        }

        if (reader.tokenType() != QXmlStreamReader::StartElement)
        {
            continue;
        }

        const QXmlStreamNamespaceDeclarations decls = reader.namespaceDeclarations();

        for (const QXmlStreamNamespaceDeclaration& decl : decls)
        {
            const QString prefix = decl.prefix().toString();

            if (prefix != QLatin1String("x") && prefix != QLatin1String("rdf")
                && !extraDecls.contains(QStringLiteral("xmlns:%1=").arg(prefix)))
            {
                extraDecls += QStringLiteral(" xmlns:%1=\"%2\"")
                                  .arg(prefix, escAttr(decl.namespaceUri().toString()));
            }
        }

        if (reader.qualifiedName() != QLatin1String("rdf:Description"))
        {
            continue;
        }

        // Attributes on the description itself, minus the ones
        // the generated packet sets.
        const QXmlStreamAttributes attrs = reader.attributes();

        for (const QXmlStreamAttribute& attr : attrs)
        {
            const QString name = attr.qualifiedName().toString();

            if (name != QLatin1String("rdf:about") && !generatedKeys.contains(name))
            {
                extraDecls += QStringLiteral(" %1=\"%2\"").arg(name, escAttr(attr.value().toString()));
            }
        }

        // Child elements of this description.
        int depth = 1;

        while (depth > 0 && reader.readNext() != QXmlStreamReader::Invalid)
        {
            if (reader.tokenType() == QXmlStreamReader::StartElement)
            {
                if (depth == 1 && generatedKeys.contains(reader.qualifiedName().toString()))
                {
                    reader.skipCurrentElement();
                }
                else if (depth == 1)
                {
                    preserved += QStringLiteral("   ");
                    copySubtree();
                    preserved += QLatin1Char('\n');
                }
                else
                {
                    ++depth;
                }
            }
            else if (reader.tokenType() == QXmlStreamReader::EndElement)
            {
                --depth;
            }
        }
    }

    if (!complete)
    {
        // A base packet we cannot parse to its end contributes
        // nothing; the generated packet stands on its own.
        // (hasError() cannot be the test: reading past the end
        // of a valid document reports "premature end" too.)
        return generated;
    }

    QString merged = generated;

    if (!extraDecls.isEmpty())
    {
        merged.replace(QLatin1String("<rdf:Description rdf:about=\"\">"),
                       QLatin1String("<rdf:Description rdf:about=\"\"") + extraDecls + QLatin1Char('>'));
    }

    if (!preserved.isEmpty())
    {
        merged.replace(QLatin1String("  </rdf:Description>"),
                       preserved + QLatin1String("  </rdf:Description>"));
    }

    return merged;
}

} // namespace DigikamGenericImmichPlugin

// Unit tests for the plugin's pure logic: the XMP sidecar
// builder and preservation merge (immichxmp.h) and the export
// metadata sync planning, field hashing, snapshots and gating
// (immichsync.h). No digiKam or network dependency.
//
// Build and run:
//   cmake -S tests -B build-tests && cmake --build build-tests
//   ctest --test-dir build-tests --output-on-failure

#include <QtTest>

#include "immichsync.h"
#include "immichxmp.h"

using namespace DigikamGenericImmichPlugin;

class TestPluginLogic : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // --- sidecar builder (immichxmp.h) -------------------------

    void sidecarCarriesContractFields()
    {
        const QString xmp = buildSidecarPacket(
            QStringList { QStringLiteral("BridgeTest"), QStringLiteral("Family/Holidays") },
            4, QStringLiteral("a caption"), true, 53.1054, -6.0506);

        QVERIFY(xmp.contains(QStringLiteral("digiKam:TagsList")));
        QVERIFY(xmp.contains(QStringLiteral("<rdf:li>BridgeTest</rdf:li>")));
        QVERIFY(xmp.contains(QStringLiteral("<rdf:li>Family/Holidays</rdf:li>")));
        QVERIFY(xmp.contains(QStringLiteral("<xmp:Rating")));
        QVERIFY(xmp.contains(QStringLiteral(">4</xmp:Rating>")));
        QVERIFY(xmp.contains(QStringLiteral("a caption")));
        QVERIFY(xmp.contains(QStringLiteral("53,6.3240N")));
        QVERIFY(xmp.contains(QStringLiteral("6,3.0360W")));
    }

    void sidecarOmitsUnsetFields()
    {
        const QString xmp = buildSidecarPacket(QStringList(), 0, QString(), false, 0.0, 0.0);

        QVERIFY(!xmp.contains(QStringLiteral("xmp:Rating")));
        QVERIFY(!xmp.contains(QStringLiteral("TagsList")));
        QVERIFY(!xmp.contains(QStringLiteral("GPSLatitude")));
        QVERIFY(!xmp.contains(QStringLiteral("dc:description")));
    }

    void sidecarEscapesXml()
    {
        const QString xmp = buildSidecarPacket(
            QStringList { QStringLiteral("A & B <C>") }, 0,
            QStringLiteral("5 > 3 & \"quoted\""), false, 0.0, 0.0);

        QVERIFY(xmp.contains(QStringLiteral("A &amp; B &lt;C&gt;")));
        QVERIFY(xmp.contains(QStringLiteral("5 &gt; 3 &amp; &quot;quoted&quot;")));
    }

    // --- preservation merge (immichxmp.h) ----------------------

    void mergeGeneratedFieldsWin()
    {
        const QString base = buildSidecarPacket(
            QStringList { QStringLiteral("SimpleTag") }, 5,
            QStringLiteral("minimal xmp test"), false, 0.0, 0.0);
        const QString generated = buildSidecarPacket(
            QStringList { QStringLiteral("test") }, 4, QString(), false, 0.0, 0.0);
        // The generated packet sets tags and rating only, so the
        // base description is not a generated key and survives.
        const QString merged = mergeSidecarPackets(
            generated, base,
            { QStringLiteral("digiKam:TagsList"), QStringLiteral("xmp:Rating") });

        QVERIFY(merged.contains(QStringLiteral(">4</xmp:Rating>")));
        QVERIFY(!merged.contains(QStringLiteral(">5</xmp:Rating>")));
        QVERIFY(merged.contains(QStringLiteral("<rdf:li>test</rdf:li>")));
        QVERIFY(!merged.contains(QStringLiteral("SimpleTag")));
        QVERIFY(merged.contains(QStringLiteral("minimal xmp test")));
    }

    void mergePreservesDigikamLabels()
    {
        const QString base = QStringLiteral(
            "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n"
            " <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n"
            "  <rdf:Description rdf:about=\"\"\n"
            "   xmlns:digiKam=\"http://www.digikam.org/ns/1.0/\"\n"
            "   digiKam:ColorLabel=\"3\"\n"
            "   digiKam:PickLabel=\"2\"/>\n"
            " </rdf:RDF>\n"
            "</x:xmpmeta>\n");
        const QString generated = buildSidecarPacket(
            QStringList { QStringLiteral("test") }, 4, QString(), false, 0.0, 0.0);
        const QString merged = mergeSidecarPackets(
            generated, base,
            { QStringLiteral("digiKam:TagsList"), QStringLiteral("xmp:Rating") });

        QVERIFY(merged.contains(QStringLiteral("digiKam:ColorLabel=\"3\"")));
        QVERIFY(merged.contains(QStringLiteral("digiKam:PickLabel=\"2\"")));
        QVERIFY(merged.contains(QStringLiteral(">4</xmp:Rating>")));
    }

    void mergeCaptionFromGeneratedSuppressesBase()
    {
        const QString base = buildSidecarPacket(
            QStringList(), 0, QStringLiteral("old caption"), false, 0.0, 0.0);
        const QString generated = buildSidecarPacket(
            QStringList(), 0, QStringLiteral("new caption"), false, 0.0, 0.0);
        const QString merged = mergeSidecarPackets(
            generated, base, { QStringLiteral("dc:description") });

        QVERIFY(merged.contains(QStringLiteral("new caption")));
        QVERIFY(!merged.contains(QStringLiteral("old caption")));
    }

    void mergeEdgeCases()
    {
        const QString generated = buildSidecarPacket(
            QStringList { QStringLiteral("test") }, 4, QString(), false, 0.0, 0.0);

        QCOMPARE(mergeSidecarPackets(generated, QString(),
                                     { QStringLiteral("xmp:Rating") }),
                 generated);
        QCOMPARE(mergeSidecarPackets(generated, QStringLiteral("not xml at all"),
                                     { QStringLiteral("xmp:Rating") }),
                 generated);
    }

    // --- sync planning (immichsync.h) --------------------------

    SyncFields fields(const QStringList& tags, int rating, const QString& desc)
    {
        SyncFields f;
        f.tags        = tags;
        f.rating      = rating;
        f.description = desc;
        return f;
    }

    void planIdenticalIsEmpty()
    {
        const SyncPlan plan = planSync(fields({ QStringLiteral("A") }, 4, QStringLiteral("x")),
                                       fields({ QStringLiteral("A") }, 4, QStringLiteral("x")));
        QVERIFY(!plan.any());
    }

    void planTagsAreAddOnly()
    {
        const SyncPlan plan = planSync(
            fields({ QStringLiteral("A"), QStringLiteral("B") }, 0, QString()),
            fields({ QStringLiteral("A"), QStringLiteral("ServerOnly") }, 0, QString()));

        QCOMPARE(plan.tagsToAdd, QStringList { QStringLiteral("B") });
    }

    void planRatingSetAndClear()
    {
        SyncPlan plan = planSync(fields(QStringList(), 4, QString()),
                                 fields(QStringList(), 2, QString()));
        QVERIFY(plan.setRating);
        QCOMPARE(plan.rating, 4);

        plan = planSync(fields(QStringList(), 0, QString()),
                        fields(QStringList(), 3, QString()));
        QVERIFY(plan.setRating);
        QCOMPARE(plan.rating, 0);   // cleared to unrated
    }

    void planClampsOutOfRangeRating()
    {
        // digiKam's unset sentinel (-1) is treated as unrated,
        // never sent to the server (where -1 means "rejected").
        SyncPlan plan = planSync(fields(QStringList(), -1, QString()),
                                 fields(QStringList(), 3, QString()));
        QVERIFY(plan.setRating);
        QCOMPARE(plan.rating, 0);

        plan = planSync(fields(QStringList(), 9, QString()),
                        fields(QStringList(), 3, QString()));
        QVERIFY(plan.setRating);
        QCOMPARE(plan.rating, 5);
    }

    void planDescriptionAndGps()
    {
        SyncFields dk = fields(QStringList(), 0, QStringLiteral("new"));
        dk.hasGps     = true;
        dk.latitude   = 53.1054;
        dk.longitude  = -6.0506;
        SyncFields sv = fields(QStringList(), 0, QStringLiteral("old"));

        SyncPlan plan = planSync(dk, sv);
        QVERIFY(plan.setDescription);
        QVERIFY(plan.setGps);

        sv.hasGps    = true;
        sv.latitude  = 53.10541;   // within rounding tolerance
        sv.longitude = -6.05061;
        plan = planSync(dk, sv);
        QVERIFY(!plan.setGps);
    }

    void planDropScalarsKeepsTags()
    {
        SyncPlan plan = planSync(
            fields({ QStringLiteral("B") }, 5, QStringLiteral("d")),
            fields({ QStringLiteral("A") }, 1, QStringLiteral("e")));
        plan.dropScalars();

        QVERIFY(!plan.scalars());
        QCOMPARE(plan.tagsToAdd, QStringList { QStringLiteral("B") });
    }

    // --- field hash, snapshots, gating -------------------------

    void fieldHashIgnoresTagOrder()
    {
        const SyncFields a = fields({ QStringLiteral("X"), QStringLiteral("Y") }, 3, QStringLiteral("d"));
        const SyncFields b = fields({ QStringLiteral("Y"), QStringLiteral("X") }, 3, QStringLiteral("d"));
        QCOMPARE(syncFieldHash(a), syncFieldHash(b));

        SyncFields c = b;
        c.rating = 4;
        QVERIFY(syncFieldHash(a) != syncFieldHash(c));
    }

    void snapshotRoundTrips()
    {
        SyncSnapshot snap;
        snap.checksum        = QStringLiteral("abc=");
        snap.serverUpdatedAt = QStringLiteral("2026-10-18T00:00:00.000Z");
        snap.serverHash      = QStringLiteral("s1");
        snap.dkHash          = QStringLiteral("d1");

        const SyncSnapshot back = SyncSnapshot::fromJson(snap.toJson());

        QCOMPARE(back.checksum, snap.checksum);
        QCOMPARE(back.serverUpdatedAt, snap.serverUpdatedAt);
        QCOMPARE(back.serverHash, snap.serverHash);
        QCOMPARE(back.dkHash, snap.dkHash);
    }

    void gateNoSnapshotPushes()
    {
        const GateResult g = gateSync(false, QString(), QStringLiteral("t1"),
                                      QString(), QStringLiteral("h"),
                                      QDateTime(), QDateTime());
        QVERIFY(g.action == GateAction::Push);
        QVERIFY(!g.conflict);
    }

    void gateOnlyServerChangedLeavesAlone()
    {
        const GateResult g = gateSync(true, QStringLiteral("t0"), QStringLiteral("t1"),
                                      QStringLiteral("h"), QStringLiteral("h"),
                                      QDateTime(), QDateTime());
        QVERIFY(g.action == GateAction::LeaveAlone);
        QVERIFY(!g.conflict);
    }

    void gateOnlyDigiKamChangedPushes()
    {
        const GateResult g = gateSync(true, QStringLiteral("t1"), QStringLiteral("t1"),
                                      QStringLiteral("h0"), QStringLiteral("h1"),
                                      QDateTime(), QDateTime());
        QVERIFY(g.action == GateAction::Push);
        QVERIFY(!g.conflict);
    }

    // --- identity matching (immichsync.h) ----------------------

    IdentityCandidate cand(const QString& id, const QString& name, const QString& time,
                           const QString& created, int w, int h)
    {
        IdentityCandidate c;
        c.id            = id;
        c.name          = name;
        c.localDateTime = time;
        c.createdAt     = created;
        c.width         = w;
        c.height        = h;
        return c;
    }

    void identityNeedsNameAndDimensions()
    {
        const QList<IdentityCandidate> cands {
            cand(QStringLiteral("a"), QStringLiteral("other.jpg"),
                 QStringLiteral("2026-10-18T19:15:00.000Z"), QStringLiteral("2026-10-18"), 800, 600),
            cand(QStringLiteral("b"), QStringLiteral("photo.jpg"),
                 QStringLiteral("2026-10-18T19:15:00.000Z"), QStringLiteral("2026-10-18"), 1024, 768),
        };
        const IdentityMatch m = chooseIdentityMatch(
            cands, QStringLiteral("photo.jpg"), QStringLiteral("2026-10-18T19:15:00"), 800, 600);
        QVERIFY(m.assetId.isEmpty());
        QCOMPARE(m.candidates, 0);
    }

    void identityMatchesAndSwappedDimensions()
    {
        const QList<IdentityCandidate> cands {
            cand(QStringLiteral("a"), QStringLiteral("photo.jpg"),
                 QStringLiteral("2026-10-18T19:15:00.000Z"), QStringLiteral("2026-10-18"), 600, 800),
        };
        const IdentityMatch m = chooseIdentityMatch(
            cands, QStringLiteral("photo.jpg"), QStringLiteral("2026-10-18T19:15:00"), 800, 600);
        QCOMPARE(m.assetId, QStringLiteral("a"));
        QVERIFY(m.strictTime);
        QCOMPARE(m.candidates, 1);
    }

    void identityTimeDisambiguates()
    {
        const QList<IdentityCandidate> cands {
            cand(QStringLiteral("old"), QStringLiteral("photo.jpg"),
                 QStringLiteral("2026-10-18T10:00:00.000Z"), QStringLiteral("2026-10-19"), 800, 600),
            cand(QStringLiteral("timed"), QStringLiteral("photo.jpg"),
                 QStringLiteral("2026-10-18T19:15:00.000Z"), QStringLiteral("2026-10-18"), 800, 600),
        };
        const IdentityMatch m = chooseIdentityMatch(
            cands, QStringLiteral("photo.jpg"), QStringLiteral("2026-10-18T19:15:00"), 800, 600);
        QCOMPARE(m.assetId, QStringLiteral("timed"));
        QVERIFY(m.strictTime);
        QCOMPARE(m.candidates, 2);
    }

    void identityWithoutTimeTakesNewest()
    {
        const QList<IdentityCandidate> cands {
            cand(QStringLiteral("first"), QStringLiteral("photo.jpg"),
                 QStringLiteral("2026-10-18T10:00:00.000Z"), QStringLiteral("2026-10-18T01:00:00Z"), 800, 600),
            cand(QStringLiteral("second"), QStringLiteral("photo.jpg"),
                 QStringLiteral("2026-10-18T11:00:00.000Z"), QStringLiteral("2026-10-18T02:00:00Z"), 800, 600),
        };
        const IdentityMatch m = chooseIdentityMatch(
            cands, QStringLiteral("photo.jpg"), QStringLiteral("2026-10-18T19:15:00"), 800, 600);
        QCOMPARE(m.assetId, QStringLiteral("second"));
        QVERIFY(!m.strictTime);
    }

    // --- pixel comparison (immichsync.h) -----------------------

    QImage solid(int w, int h, int gray)
    {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(QColor(gray, gray, gray).rgb());
        return img;
    }

    void pixelsIdenticalAndReencoded()
    {
        QVERIFY(imagesLookSame(solid(64, 64, 100), solid(64, 64, 100)));
        QCOMPARE(imageDifference(solid(64, 64, 100), solid(64, 64, 100)), 0.0);
        // A few levels of re-encode noise still count as the same.
        QVERIFY(imagesLookSame(solid(64, 64, 100), solid(64, 64, 103)));
        // A different resolution compares fine after scaling.
        QVERIFY(imagesLookSame(solid(800, 600, 100), solid(250, 188, 100)));
    }

    void pixelsEditedAndDifferent()
    {
        QVERIFY(!imagesLookSame(solid(64, 64, 100), solid(64, 64, 130)));
        QVERIFY(!imagesLookSame(solid(64, 64, 0), solid(64, 64, 255)));
        QVERIFY(imageDifference(QImage(), solid(64, 64, 100)) < 0.0);
    }

    void gateBothChangedNewerSideWins()
    {
        const QDateTime older(QDate(2026, 10, 18), QTime(10, 0), QTimeZone::UTC);
        const QDateTime newer(QDate(2026, 10, 18), QTime(12, 0), QTimeZone::UTC);

        GateResult g = gateSync(true, QStringLiteral("t0"), QStringLiteral("t1"),
                                QStringLiteral("h0"), QStringLiteral("h1"),
                                newer, older);
        QVERIFY(g.conflict);
        QVERIFY(g.action == GateAction::DropScalars);

        g = gateSync(true, QStringLiteral("t0"), QStringLiteral("t1"),
                     QStringLiteral("h0"), QStringLiteral("h1"),
                     older, newer);
        QVERIFY(g.conflict);
        QVERIFY(g.action == GateAction::Push);
    }
};

QTEST_MAIN(TestPluginLogic)
#include "test_plugin_logic.moc"

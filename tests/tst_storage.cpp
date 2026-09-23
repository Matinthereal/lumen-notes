#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>
#include <QSignalSpy>
#include "ink/inkdocument.h"
#include "storage/database.h"
#include "storage/journal.h"
#include "storage/library.h"
#include "media/images.h"
#include "media/shapes.h"
#include "storage/paths.h"
#include "storage/pagestore.h"
#include "storage/notebookfile.h"
#include "storage/schema.h"
#include "storage/strokecodec.h"
#include "text/textblocks.h"

static Stroke randomStroke(quint64 id, int n = 50)
{
    auto *r = QRandomGenerator::global();
    Stroke s; s.id = id; s.tool = (id % 5 == 0) ? InkTool::Highlighter : InkTool::Pen;
    s.color = 0xff000000u | r->bounded(0xffffffu); s.width = 1.f + float(r->bounded(3.0));
    s.recordingId = id % 3 == 0 ? 7 : 0; s.startMs = quint32(r->bounded(100000));
    float x = float(r->bounded(700.0)), y = float(r->bounded(1000.0)); quint32 t = 0;
    for (int i = 0; i < n; ++i) {
        x += float(r->bounded(6.0)) - 3; y += float(r->bounded(6.0)) - 3; t += 2 + r->bounded(3);
        s.points.append({x, y, float(r->bounded(1.0)), float(r->bounded(120.0)) - 60, float(r->bounded(120.0)) - 60, t});
    }
    s.updateBounds();
    return s;
}

class TstStorage : public QObject {
    Q_OBJECT
private slots:
    void codecRoundTripIsExactWhereItPromises() {
        QVector<Stroke> in; for (int i = 1; i <= 40; ++i) in.append(randomStroke(i, 5 + i));
        in.append(Stroke{}); in.last().id = 99; in.last().points.append({1.5f, -2.25f, 0.5f, 0, 0, 0}); in.last().updateBounds();
        const QByteArray blob = strokecodec::encode(in);
        QVERIFY(blob.startsWith("MYNK"));
        QVector<Stroke> out; QVERIFY(strokecodec::decode(blob, out));
        QCOMPARE(out.size(), in.size());
        for (int i = 0; i < in.size(); ++i) {
            QCOMPARE(out[i].id, in[i].id); QCOMPARE(int(out[i].tool), int(in[i].tool)); QCOMPARE(out[i].color, in[i].color);
            QCOMPARE(out[i].width, in[i].width); QCOMPARE(out[i].recordingId, in[i].recordingId); QCOMPARE(out[i].startMs, in[i].startMs);
            QCOMPARE(out[i].points.size(), in[i].points.size());
            for (int k = 0; k < in[i].points.size(); ++k) {
                QVERIFY(std::abs(out[i].points[k].x - in[i].points[k].x) <= 1.f / 128 + 1e-4f);
                QVERIFY(std::abs(out[i].points[k].y - in[i].points[k].y) <= 1.f / 128 + 1e-4f);
                QVERIFY(std::abs(out[i].points[k].pressure - in[i].points[k].pressure) < 1e-4f);
                QCOMPARE(out[i].points[k].tMs, in[i].points[k].tMs);   // timestamps are exact
            }
        }
        QVector<Stroke> none; QVERIFY(strokecodec::decode(QByteArray(), none)); QVERIFY(none.isEmpty());
        QVERIFY(!strokecodec::decode(blob.left(blob.size() / 2), none));  // truncated → refused, not garbage
    }
    void journalReplaysAndSurvivesATornTail() {
        QTemporaryDir dir;
        const QString path = dir.path() + "/1.log";
        InkDocument a;
        { PageJournal j(path); QVERIFY(j.open()); QVERIFY(j.isEmpty());
          const Stroke s1 = randomStroke(1), s2 = randomStroke(2);
          a.addStroke(s1); j.appendAdd(0, s1); a.addStroke(s2); j.appendAdd(1, s2);
          j.appendRemove(1); a.removeStroke(1);
          Stroke s2b = s2; s2b.width = 9; j.appendUpdate(s2b); a.updateStroke(s2b);
          QVERIFY(!j.isEmpty()); }
        // tear the tail: append half a bogus record
        { QFile f(path); QVERIFY(f.open(QIODevice::Append)); f.write("\x40\x00\x00\x00\x01garbage", 12); }
        InkDocument b; PageJournal j(path);
        QCOMPARE(j.replay(b), 4);
        QCOMPARE(b.count(), 1); QCOMPARE(b.strokes()[0].id, 2ull); QCOMPARE(b.strokes()[0].width, 9.f);
        QVERIFY(j.truncate()); QVERIFY(j.isEmpty());
    }
    void pageStoreFoldsOnIdleAndRecoversJournal() {
        QTemporaryDir dir;
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QCOMPARE(ensureSchema(db), kSchemaVersion);
        Library lib(db); QVERIFY(lib.seedDefaults());
        const qint64 pid = lib.firstPageId(); QVERIFY(pid > 0);
        {
            InkDocument doc; PageStore store(db, dir.path() + "/journal"); store.setIdleMs(50); store.attach(&doc);
            QVERIFY(store.load(pid));
            for (int i = 0; i < 20; ++i) doc.addStroke(randomStroke(0, 10));
            QVERIFY(store.dirty());
            QTRY_VERIFY_WITH_TIMEOUT(!store.dirty(), 3000);     // folded on idle
            PageJournal j(dir.path() + QStringLiteral("/journal/%1.log").arg(pid)); QVERIFY(j.isEmpty());
            doc.addStroke(randomStroke(0, 10));                 // journaled, not yet folded
            doc.removeStroke(doc.strokes()[0].id);
            // simulate a crash: store goes away without flush — the journal holds the last two ops
            store.disconnect(); store.setIdleMs(1000000);
        }
        InkDocument doc2; PageStore store2(db, dir.path() + "/journal"); store2.attach(&doc2);
        QVERIFY(store2.load(pid));
        QCOMPARE(doc2.count(), 20);                             // 20 folded + 1 added − 1 removed
        Database::Query q(db, "SELECT stroke_count FROM stroke_blob WHERE page_id=?"); q.bind(1, pid);
        QVERIFY(q.step()); QCOMPARE(q.i32(0), 20);              // recovery folded immediately
    }
    void libraryCrudAndSoftDelete() {
        QTemporaryDir dir;
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db);
        QVERIFY(lib.seedDefaults()); QVERIFY(!lib.seedDefaults());
        const QVariantList nbs = lib.notebooks(); QCOMPARE(nbs.size(), 1);
        QCOMPARE(nbs[0].toMap().value("name").toString(), QStringLiteral("My notebook"));
        const qint64 maths = nbs[0].toMap().value("id").toLongLong();
        const QVariantList secs = lib.sections(maths); QCOMPARE(secs.size(), 1);
        QCOMPARE(lib.page(lib.pages(secs[0].toMap().value("id").toLongLong())[0].toMap().value("id").toLongLong()).value("sizeMode").toString(), QStringLiteral("typed"));
        QVERIFY(!lib.search("welcome").isEmpty());          // the welcome page is searchable from the start
        const qint64 pure1 = secs[0].toMap().value("id").toLongLong();
        QCOMPARE(lib.pages(pure1).size(), 1);
        const qint64 p2 = lib.createPage(pure1, "grid", "infinite");
        QCOMPARE(lib.pages(pure1).size(), 2);
        QCOMPARE(lib.page(p2).value("style").toString(), QStringLiteral("grid"));
        QCOMPARE(lib.page(p2).value("notebookName").toString(), QStringLiteral("My notebook"));
        lib.rename("page", p2, "Integration");
        QCOMPARE(lib.page(p2).value("title").toString(), QStringLiteral("Integration"));
        const qint64 p1 = lib.pages(pure1)[0].toMap().value("id").toLongLong();
        QCOMPARE(lib.nextPageId(p1, 1), p2); QCOMPARE(lib.nextPageId(p2, 1), 0ll);
        lib.movePage(p2, 0); QCOMPARE(lib.pages(pure1)[0].toMap().value("id").toLongLong(), p2);
        lib.remove("page", p2); QCOMPARE(lib.pages(pure1).size(), 1);
        lib.restore("page", p2); QCOMPARE(lib.pages(pure1).size(), 2);
        lib.remove("page", p2); QCOMPARE(lib.purgeDeleted(0), 1); QCOMPARE(lib.pages(pure1).size(), 1);
        lib.setSetting("page.style", "lined"); QCOMPARE(lib.setting("page.style"), QStringLiteral("lined"));
        // FTS: index and search with prefixes, snippets and page context
        lib.indexText("text", p1, 0, "The integral of x squared is one third x cubed");
        lib.indexText("pdf", p1, 0, "Slide about differentiation");
        QVariantList hits = lib.search("integ");
        QCOMPARE(hits.size(), 1); QCOMPARE(hits[0].toMap().value("kind").toString(), QStringLiteral("text"));
        QCOMPARE(hits[0].toMap().value("notebookName").toString(), QStringLiteral("My notebook"));
        QVERIFY(hits[0].toMap().value("snippet").toString().contains("integral"));
        QCOMPARE(lib.search("x cubed").size(), 1);
        QCOMPARE(lib.search("differ").size(), 1);
        lib.unindex("text", p1); QCOMPARE(lib.search("integ").size(), 0);
        QVERIFY(lib.search("\"unbalanced (syntax").isEmpty());
        // text blocks: CRUD, FTS, edit timeline
        TextBlocks tb(db, lib);
        const int welcomeBlocks = tb.list(p1).size();          // the seeded welcome page already has its text
        const qint64 b1 = tb.create(p1, 100, 120, 380, 0, 0);
        QVERIFY(b1 > 0); QCOMPARE(tb.list(p1).size(), welcomeBlocks + 1);
        tb.setMarkdown(b1, "# Kinematics\n\n$v = u + at$ and the *suvat* set", 1500);
        tb.setMarkdown(b1, "# Kinematics\n\n$v = u + at$ and the *suvat* set, finished", 4200);
        QVERIFY(tb.block(b1).value("markdown").toString().endsWith("finished"));
        QCOMPARE(lib.search("suvat").size(), 1);
        QCOMPARE(lib.search("suvat")[0].toMap().value("refId").toLongLong(), b1);
        QVERIFY(tb.list(p1).last().toMap().value("editTimes").toString().contains("4200"));
        tb.setGeometry(b1, 50, 60, 500); QCOMPARE(tb.block(b1).value("w").toDouble(), 500.0);
        QVERIFY(tb.pageText(p1).contains("Kinematics"));
        tb.remove(b1); QCOMPARE(tb.list(p1).size(), welcomeBlocks); QCOMPARE(lib.search("suvat").size(), 0);
        QCOMPARE(lib.page(lib.createPage(pure1)).value("style").toString(), QStringLiteral("lined"));
    }
    void truncatedBlobKeepsWhatDecodes() {
        QVector<Stroke> in;
        for (int i = 0; i < 4; ++i) { Stroke s; s.id = i + 1; s.points = {{0, 0, 1, 0, 0, 0}, {10, float(i), 1, 0, 0, 1}}; in.append(s); }
        QByteArray blob = strokecodec::encode(in);
        blob.chop(6);                                      // torn tail: the last stroke is unreadable
        QVector<Stroke> out;
        QVERIFY(!strokecodec::decode(blob, out));
        QCOMPARE(out.size(), 3);                           // the first three survive
        QByteArray bogus = blob.left(6); bogus[2] = char(0xff); bogus[3] = char(0xff); bogus[4] = char(0xff); bogus[5] = char(0x7f);
        QVector<Stroke> none; QVERIFY(!strokecodec::decode(bogus, none));   // absurd count is rejected before reserve()
    }
    void reusedPageIdNeverInheritsAJournal() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db);
        const qint64 nb = lib.createNotebook("T", "#000"); const qint64 sec = lib.createSection(nb, "S");
        const qint64 p = lib.createPage(sec);
        QDir().mkpath(paths::journalDir());
        const QString jpath = QStringLiteral("%1/%2.log").arg(paths::journalDir()).arg(p);
        { QFile f(jpath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("stale"); }
        lib.remove("page", p);
        QVERIFY(lib.purgeDeleted(0) >= 1);
        QVERIFY2(!QFile::exists(jpath), "purge must take the journal with the page");
        { QFile f(jpath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("stale"); }
        const qint64 again = lib.createPage(sec);
        if (again == p) QVERIFY2(!QFile::exists(jpath), "a recycled rowid must start with a clean journal");
    }
    void cropAndTurnLeaveTheFileAlone() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db);
        const qint64 page = lib.createPage(lib.createSection(lib.createNotebook("N", "#000"), "S"));
        Images images(db);
        QImage src(200, 100, QImage::Format_RGB32); src.fill(Qt::blue);
        const QString file = dir.path() + "/photo.png";
        QVERIFY(src.save(file, "PNG"));
        const QByteArray original = QFile(file).readAll();

        const qint64 id = images.insertFile(page, QUrl::fromLocalFile(file), 0, 0, 200);
        QCOMPARE(images.image(id).value("cropW").toDouble(), 1.0);
        QCOMPARE(images.image(id).value("rotation").toInt(), 0);

        // Trim the right half: the crop is a fraction of the picture, the rect is where it goes.
        images.setCrop(id, 0, 0, 0.5, 1, 0, 0, 100, 100);
        QVariantMap m = images.image(id);
        QCOMPARE(m.value("cropW").toDouble(), 0.5);
        QCOMPARE(m.value("w").toDouble(), 100.0);

        // A quarter turn turns the crop with it and swaps the sides around the same middle.
        images.rotate(id, 1);
        m = images.image(id);
        QCOMPARE(m.value("rotation").toInt(), 90);
        QCOMPARE(m.value("cropX").toDouble(), 0.0);
        QCOMPARE(m.value("cropY").toDouble(), 0.0);
        QCOMPARE(m.value("cropW").toDouble(), 1.0);
        QCOMPARE(m.value("cropH").toDouble(), 0.5);
        QCOMPARE(m.value("w").toDouble(), 100.0);           // it was square, so the sides look the same
        images.rotate(id, 4);                               // four quarter turns is where it started
        QCOMPARE(images.image(id).value("rotation").toInt(), 90);

        images.resetCrop(id);
        m = images.image(id);
        QCOMPARE(m.value("rotation").toInt(), 0);
        QCOMPARE(m.value("cropW").toDouble(), 1.0);
        QCOMPARE(m.value("w").toDouble(), 200.0);           // the whole picture again, 2:1 as it was
        QCOMPARE(m.value("h").toDouble(), 100.0);

        QCOMPARE(QFile(file).readAll(), original);          // nothing was ever written to the picture
        QCOMPARE(QFile(m.value("path").toString()).readAll(), original);
    }

    void picturesLandOnAPageAndComeBack() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db);
        const qint64 page = lib.createPage(lib.createSection(lib.createNotebook("N", "#000"), "S"));
        Images images(db);

        QImage src(120, 60, QImage::Format_RGB32);
        src.fill(Qt::red);
        const QString file = dir.path() + "/diagram.png";
        QVERIFY(src.save(file, "PNG"));

        const qint64 id = images.insertFile(page, QUrl::fromLocalFile(file), 40, 50, 90);
        QVERIFY(id > 0);
        QCOMPARE(images.count(page), 1);
        const QVariantMap m = images.image(id);
        QCOMPARE(m.value("x").toDouble(), 40.0);
        QCOMPARE(m.value("w").toDouble(), 90.0);            // clamped to maxWidth
        QCOMPARE(m.value("h").toDouble(), 45.0);            // aspect kept
        QVERIFY2(QFileInfo::exists(m.value("path").toString()), "the picture must be copied into attachments");

        images.setGeometry(id, 10, 20, 200, 100);
        QCOMPARE(images.image(id).value("y").toDouble(), 20.0);
        QCOMPARE(images.image(id).value("w").toDouble(), 200.0);

        const qint64 second = images.insertFile(page, QUrl::fromLocalFile(file), 0, 0, 300);
        QCOMPARE(images.count(page), 2);                     // same file twice = two placements, one attachment
        Database::Query att(db, "SELECT COUNT(*) FROM attachment");
        QVERIFY(att.step()); QCOMPARE(att.i32(0), 1);

        images.remove(id);
        QCOMPARE(images.count(page), 1);
        QVERIFY(images.image(id).isEmpty());
        QVERIFY(!images.image(second).isEmpty());
        QCOMPARE(images.insertFile(page, QUrl::fromLocalFile(dir.path() + "/nope.png"), 0, 0), qint64(0));
        QCOMPARE(images.count(page), 1);                     // a file that is not a picture changes nothing
    }
    void shapesStayEditableAfterYouDrawThem() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) >= 5);
        Library lib(db);
        const qint64 page = lib.createPage(lib.createSection(lib.createNotebook("N", "#000"), "S"));
        Shapes shapes(db);

        const qint64 id = shapes.create(page, "ellipse", 100, 120, 200, 90, "#E0403C", "", 2.5);
        QVERIFY(id > 0);
        QCOMPARE(shapes.count(page), 1);
        QVariantMap s = shapes.shape(id);
        QCOMPARE(s.value("kind").toString(), QStringLiteral("ellipse"));
        QCOMPARE(s.value("fill").toString(), QString());          // no fill is a real choice, not a missing value

        shapes.setStyle(id, "#1F6FEB", "#CFE0FA", 6);
        s = shapes.shape(id);
        QCOMPARE(s.value("stroke").toString(), QStringLiteral("#1F6FEB"));
        QCOMPARE(s.value("fill").toString(), QStringLiteral("#CFE0FA"));
        QCOMPARE(s.value("width").toDouble(), 6.0);

        shapes.setStyle(id, "", "", 0);                            // clearing the fill keeps colour and width
        s = shapes.shape(id);
        QCOMPARE(s.value("fill").toString(), QString());
        QCOMPARE(s.value("stroke").toString(), QStringLiteral("#1F6FEB"));
        QCOMPARE(s.value("width").toDouble(), 6.0);

        shapes.setGeometry(id, 10, 20, 300, 40);
        s = shapes.shape(id);
        QCOMPARE(s.value("x").toDouble(), 10.0);
        QCOMPARE(s.value("w").toDouble(), 300.0);

        const qint64 copy = shapes.duplicate(id);
        QVERIFY(copy > 0 && copy != id);
        QCOMPARE(shapes.count(page), 2);
        QCOMPARE(shapes.shape(copy).value("stroke").toString(), QStringLiteral("#1F6FEB"));
        QCOMPARE(shapes.shape(copy).value("x").toDouble(), 28.0);   // offset so it is visibly a copy

        // a shape too small to grab is never created
        const qint64 sliver = shapes.create(page, "rect", 0, 0, 1, 1, "#000000", "", 2);
        QCOMPARE(shapes.shape(sliver).value("w").toDouble(), 4.0);

        shapes.remove(id);
        QCOMPARE(shapes.count(page), 2);
        QVERIFY(shapes.shape(id).isEmpty());
    }
    void automaticTitlesNeedWordsInThem() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db);
        const qint64 sec = lib.createSection(lib.createNotebook("N", "#000"), "S");

        const qint64 junk = lib.createPage(sec);
        lib.suggestTitle(junk, QStringLiteral("0 000 000 000 000 000 000\n11 . 0"));
        QCOMPARE(lib.page(junk).value("title").toString(), QString());     // better nameless than that

        const qint64 good = lib.createPage(sec);
        lib.suggestTitle(good, QStringLiteral("## Circular motion and centripetal force\nmore text"));
        const QString title = lib.page(good).value("title").toString();
        QVERIFY2(title.contains(QLatin1String("Circular motion")), qPrintable(title));
        QVERIFY2(title.size() <= 41, qPrintable(QString::number(title.size())));   // 40 plus the auto mark

        lib.rename("page", good, QStringLiteral("Mine"));
        lib.suggestTitle(good, QStringLiteral("Something else entirely"));
        QCOMPARE(lib.page(good).value("title").toString(), QStringLiteral("Mine"));   // never overrides yours
    }
    void pageLinksFollowTheIdAndSurviveARename() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QCOMPARE(ensureSchema(db), kSchemaVersion);
        Library lib(db);
        TextBlocks tb(db, lib);
        const qint64 sec = lib.createSection(lib.createNotebook("Physics", "#000"), "Mechanics");
        const qint64 kin = lib.createPage(sec), forces = lib.createPage(sec);
        lib.rename("page", kin, "Kinematics");
        lib.rename("page", forces, "Forces");

        // [[Title]] typed in full becomes a link by id, whatever the case you typed it in
        const QString typed = QStringLiteral("See [[kinematics]] for *suvat*, and [[No such page]].");
        const QString resolved = lib.resolveLinks(typed);
        QCOMPARE(resolved, QStringLiteral("See [Kinematics](lumen://page/%1) for *suvat*, and [[No such page]].").arg(kin));
        const qint64 block = tb.create(forces, 0, 0, 400);
        QSignalSpy linked(&lib, &Library::linksChanged);
        tb.setMarkdown(block, resolved);
        QCOMPARE(linked.size(), 1);
        tb.setMarkdown(block, resolved + " ");                  // same links: nothing to re-index
        QCOMPARE(linked.size(), 1);

        QVariantList back = lib.backlinks(kin);
        QCOMPARE(back.size(), 1);
        QCOMPARE(back[0].toMap().value("id").toLongLong(), forces);
        QCOMPARE(back[0].toMap().value("context").toString(), QStringLiteral("See Kinematics for suvat, and [[No such page]]."));
        QVERIFY(lib.backlinks(forces).isEmpty());
        QCOMPARE(lib.linkTarget(QStringLiteral("lumen://page/%1").arg(kin)), kin);
        QCOMPARE(lib.linkTarget(QStringLiteral("lumen://page/987654")), qint64(0));
        QCOMPARE(lib.linkTarget(QStringLiteral("https://example.org")), qint64(0));

        // Rename the target: the link still points at it, and the linking text now says the new name.
        lib.rename("page", kin, "Motion [1D]");
        const QString after = tb.block(block).value("markdown").toString();
        QVERIFY2(after.contains(QStringLiteral("[Motion \\[1D\\]](lumen://page/%1)").arg(kin)), qPrintable(after));
        QCOMPARE(lib.backlinks(kin).size(), 1);
        QCOMPARE(lib.search("motion").size(), 1);
        QCOMPARE(lib.resolveLinks(QStringLiteral("[Old name](lumen://page/%1)").arg(kin)), QStringLiteral("[Motion \\[1D\\]](lumen://page/%1)").arg(kin));

        // Autocomplete: titled pages matching the words, never the page you are on
        const QVariantList some = lib.linkCandidates("mot", forces);
        QCOMPARE(some.size(), 1);
        QCOMPARE(some[0].toMap().value("title").toString(), QStringLiteral("Motion [1D]"));
        for (const QVariant &v : lib.linkCandidates("", kin)) QVERIFY(v.toMap().value("id").toLongLong() != kin);
        QCOMPARE(lib.pageByTitle("FORCES"), forces);

        // A copy of the linking page links too; a deleted one stops counting.
        const qint64 copy = lib.duplicatePage(forces);
        QCOMPARE(lib.backlinks(kin).size(), 2);
        lib.remove("page", copy);
        QCOMPARE(lib.backlinks(kin).size(), 1);
        tb.remove(block);
        QVERIFY(lib.backlinks(kin).isEmpty());
    }
    void pageTagsFilterBrowsingAndSearch() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db);
        TextBlocks tb(db, lib);
        const qint64 sec = lib.createSection(lib.createNotebook("Physics", "#000"), "Waves");
        const qint64 a = lib.createPage(sec), b = lib.createPage(sec), c = lib.createPage(sec);
        tb.setMarkdown(tb.create(a, 0, 0, 400), "Standing waves on a string");
        tb.setMarkdown(tb.create(b, 0, 0, 400), "Standing waves in a pipe");

        QCOMPARE(Library::normaliseTag("  ##Exam   revision. "), QStringLiteral("Exam revision"));
        QCOMPARE(Library::normaliseTag("#"), QString());
        QSignalSpy changed(&lib, &Library::tagsChanged);
        const qint64 exam = lib.addPageTag(a, "#exam");
        QVERIFY(exam > 0);
        QCOMPARE(lib.addPageTag(b, "EXAM"), exam);                // one tag, however it is typed
        QCOMPARE(lib.addPageTag(b, "exam"), exam);                // already there: no change, no signal
        QCOMPARE(changed.size(), 2);
        QCOMPARE(lib.addPageTag(a, "   "), qint64(0));
        lib.addPageTag(c, "formulae");

        QCOMPARE(lib.pageTags(a).size(), 1);
        QCOMPARE(lib.pageTags(a)[0].toMap().value("name").toString(), QStringLiteral("exam"));
        const QVariantList all = lib.tags();
        QCOMPARE(all.size(), 2);
        QCOMPARE(all[0].toMap().value("name").toString(), QStringLiteral("exam"));
        QCOMPARE(all[0].toMap().value("count").toInt(), 2);

        QCOMPARE(lib.pagesWithTag("Exam").size(), 2);
        QCOMPARE(lib.search("standing").size(), 2);
        QCOMPARE(lib.search("standing", 40, "formulae").size(), 0);
        lib.removePageTag(b, exam);
        QCOMPARE(lib.search("standing", 40, "#exam").size(), 1);
        QCOMPARE(lib.search("standing", 40, "exam")[0].toMap().value("pageId").toLongLong(), a);

        // a copy carries its tags; a page in the trash does not count
        const qint64 copy = lib.duplicatePage(a);
        QCOMPARE(lib.pageTags(copy).size(), 1);
        QCOMPARE(lib.pagesWithTag("exam").size(), 2);
        lib.remove("page", copy);
        QCOMPARE(lib.pagesWithTag("exam").size(), 1);
        QCOMPARE(lib.tags()[0].toMap().value("count").toInt(), 1);
    }
    void headingsBecomeThePageOutline() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db);
        TextBlocks tb(db, lib);
        const qint64 page = lib.createPage(lib.createSection(lib.createNotebook("N", "#000"), "S"));
        const qint64 first = tb.create(page, 10, 20, 400);
        tb.setMarkdown(first, "# Waves\n\nSome text\n\n## Standing *waves*\n\n#hashtag is not a heading\n### Nodes ###");
        const qint64 second = tb.create(page, 10, 400, 400);
        tb.setMarkdown(second, "not a heading\n## Interference");

        const QVariantList out = tb.outline(page);
        QCOMPARE(out.size(), 4);
        QCOMPARE(out[0].toMap().value("level").toInt(), 1);
        QCOMPARE(out[0].toMap().value("text").toString(), QStringLiteral("Waves"));
        QCOMPARE(out[1].toMap().value("level").toInt(), 2);
        QCOMPARE(out[1].toMap().value("text").toString(), QStringLiteral("Standing waves"));
        QCOMPARE(out[2].toMap().value("text").toString(), QStringLiteral("Nodes"));
        QCOMPARE(out[3].toMap().value("blockId").toLongLong(), second);
        QCOMPARE(out[3].toMap().value("y").toDouble(), 400.0);
        QVERIFY(tb.outline(page + 999).isEmpty());
    }
    void aNotebookTravelsInOneFile() {
        QTemporaryDir from, to, out;
        const QString file = out.path() + "/Physics.lumen";
        qint64 pagesExported = 0;
        {   // ---- the library it leaves
            qputenv("LUMEN_DATA_DIR", from.path().toUtf8());
            QDir().mkpath(paths::attachmentsDir());
            Database db; QVERIFY(db.open(from.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
            Library lib(db);
            TextBlocks tb(db, lib);
            Images images(db);
            Shapes shapes(db);
            const qint64 nb = lib.createNotebook("Physics", "#1F6FEB");
            const qint64 other = lib.createNotebook("Chemistry", "#12855B");
            const qint64 sec = lib.createSection(nb, "Waves");
            const qint64 a = lib.createPage(sec, "lined", "a4");
            const qint64 b = lib.createPage(sec, "", "typed");
            const qint64 outside = lib.createPage(lib.createSection(other, "Bonding"));
            lib.rename("page", a, "Interference");
            lib.rename("page", b, "Notes");
            lib.setStarred(a, true);
            lib.addPageTag(a, "exam");

            // ink
            QVector<Stroke> ink{randomStroke(1, 12), randomStroke(2, 9)};
            {
                Database::Query q(db, "INSERT INTO stroke_blob(page_id, schema, data, stroke_count) VALUES (?,?,?,?)");
                q.bind(1, a).bind(2, strokecodec::kVersion).bind(3, strokecodec::encode(ink)).bind(4, ink.size());
                QVERIFY(q.run());
            }
            // typed text, one link inside the notebook and one to a page that stays behind
            const qint64 block = tb.create(b, 20, 30, 400);
            tb.setMarkdown(block, lib.resolveLinks(QStringLiteral("see [[Interference]] and [away](lumen://page/%1)").arg(outside)));
            QCOMPARE(lib.backlinks(a).size(), 1);
            shapes.create(a, "rect", 10, 20, 100, 60, "#E0403C", "", 2.5);

            QImage picture(40, 20, QImage::Format_RGB32);
            picture.fill(Qt::magenta);
            const QString png = from.path() + "/diagram.png";
            QVERIFY(picture.save(png, "PNG"));
            QVERIFY(images.insertFile(a, QUrl::fromLocalFile(png), 50, 60, 120) > 0);

            const notebookfile::Result r = notebookfile::exportNotebook(db, nb, file);
            QVERIFY2(r.ok, qPrintable(r.error));
            QCOMPARE(r.name, QStringLiteral("Physics"));
            QCOMPARE(r.pages, 2);
            QCOMPARE(r.attachments, 1);
            pagesExported = r.pages;
            QVERIFY(QFileInfo(file).size() > 0);
            QCOMPARE(notebookfile::describe(file), QStringLiteral("Physics · 2 pages"));
        }
        {   // ---- and the library it lands in
            qputenv("LUMEN_DATA_DIR", to.path().toUtf8());
            QDir().mkpath(paths::attachmentsDir());
            Database db; QVERIFY(db.open(to.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
            Library lib(db);
            TextBlocks tb(db, lib);
            Images images(db);
            Shapes shapes(db);
            lib.createNotebook("Physics", "#000");          // a name clash, on purpose

            const notebookfile::Result r = notebookfile::importNotebook(db, file);
            QVERIFY2(r.ok, qPrintable(r.error));
            QCOMPARE(r.name, QStringLiteral("Physics (imported)"));
            QCOMPARE(r.pages, int(pagesExported));
            QVERIFY(r.notebookId > 0);

            const QVariantList sections = lib.sections(r.notebookId);
            QCOMPARE(sections.size(), 1);
            const qint64 sec = sections[0].toMap().value("id").toLongLong();
            const QVariantList pages = lib.pages(sec);
            QCOMPARE(pages.size(), 2);
            const qint64 a = pages[0].toMap().value("id").toLongLong(), b = pages[1].toMap().value("id").toLongLong();
            QCOMPARE(lib.page(a).value("title").toString(), QStringLiteral("Interference"));
            QCOMPARE(lib.page(a).value("style").toString(), QStringLiteral("lined"));
            QVERIFY(lib.isStarred(a));
            QCOMPARE(lib.pageTags(a).size(), 1);
            QCOMPARE(lib.pageTags(a)[0].toMap().value("name").toString(), QStringLiteral("exam"));
            QCOMPARE(shapes.count(a), 1);
            QCOMPARE(images.count(a), 1);
            QVERIFY2(QFileInfo::exists(images.list(a)[0].toMap().value("path").toString()),
                     "the picture's bytes must come with the notebook");
            {
                Database::Query q(db, "SELECT stroke_count, data FROM stroke_blob WHERE page_id=?");
                q.bind(1, a);
                QVERIFY(q.step());
                QCOMPARE(q.i32(0), 2);
                QVector<Stroke> back;
                QVERIFY(strokecodec::decode(q.blob(1), back));
                QCOMPARE(back.size(), 2);
            }
            // The link inside the notebook now points at the page's new id; the one that pointed
            // outside it points nowhere rather than at some unrelated page.
            const QString text = tb.pageText(b);
            QVERIFY2(text.contains(QStringLiteral("lumen://page/%1").arg(a)), qPrintable(text));
            QVERIFY2(text.contains(QStringLiteral("lumen://page/0")), qPrintable(text));
            QCOMPARE(lib.backlinks(a).size(), 1);
            QCOMPARE(lib.backlinks(a)[0].toMap().value("id").toLongLong(), b);
            QVERIFY(!lib.search("Interference").isEmpty());   // searchable without opening it

            // Importing the same file twice makes a second notebook, never a merge.
            const notebookfile::Result again = notebookfile::importNotebook(db, file);
            QVERIFY(again.ok);
            QVERIFY(again.notebookId != r.notebookId);
            QCOMPARE(again.name, QStringLiteral("Physics (imported 2)"));
            QCOMPARE(lib.notebooks().size(), 3);
            QVERIFY(!notebookfile::importNotebook(db, to.path() + "/t.db").ok);   // not an export file
            QVERIFY(!notebookfile::importNotebook(db, to.path() + "/nope.lumen").ok);
        }
        qputenv("LUMEN_DATA_DIR", QByteArray());
    }
    void schemaSixIndexesLinksAlreadyInText() {
        QTemporaryDir dir; qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        const QString path = dir.path() + "/t.db";
        qint64 a = 0, b = 0;
        {
            Database db; QVERIFY(db.open(path)); QCOMPARE(ensureSchema(db), kSchemaVersion);
            Library lib(db);
            const qint64 sec = lib.createSection(lib.createNotebook("N", "#000"), "S");
            a = lib.createPage(sec); b = lib.createPage(sec);
            // what a version-5 file looks like: the link is in the text, the index does not exist yet
            QVERIFY(db.exec(QStringLiteral("INSERT INTO text_block(page_id, x, y, w, markdown) VALUES (%1, 0, 0, 400, 'go to [A](lumen://page/%2) now')").arg(b).arg(a)));
            QVERIFY(db.exec("DROP TABLE page_link"));
            QVERIFY(db.exec("UPDATE schema_version SET version=5"));
        }
        Database db; QVERIFY(db.open(path));
        QCOMPARE(ensureSchema(db), kSchemaVersion);   // through 6, and 7 over crop columns it already has
        Library lib(db);
        const QVariantList back = lib.backlinks(a);
        QCOMPARE(back.size(), 1);
        QCOMPARE(back[0].toMap().value("id").toLongLong(), b);
    }
};
QTEST_GUILESS_MAIN(TstStorage)
#include "tst_storage.moc"

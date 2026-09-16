#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "workers/workersupervisor.h"

// Drives the pdf worker end to end: make a PDF, read its info/words/text, render it, export ink onto it.
class TstPdf : public QObject {
    Q_OBJECT
    static QJsonObject callSync(WorkerSupervisor &w, const QString &m, const QJsonObject &p, int ms = 60000) {
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request(m, p);
        QElapsedTimer t; t.start();
        while (t.elapsed() < ms) {
            QTest::qWait(10);
            for (int i = 0; i < spy.count(); ++i) if (spy[i].at(0).toInt() == id) {
                const QJsonObject err = spy[i].at(2).toJsonObject();
                if (!err.isEmpty()) qWarning() << m << "error:" << err;
                return spy[i].at(1).toJsonObject();
            }
        }
        qWarning() << m << "timed out"; return {};
    }
private slots:
    void workerRoundTrip() {
        QTemporaryDir dir;
        WorkerSupervisor w("pdf");
        w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 60000);
        const QString pdf = dir.path() + "/t.pdf";
        QJsonObject r = callSync(w, "make_test_pdf", {{"out", pdf}, {"lines", QJsonArray{"Hello Slate", "Second line of text"}}});
        QVERIFY(QFileInfo::exists(pdf));
        r = callSync(w, "info", {{"path", pdf}});
        QCOMPARE(r.value("pages").toInt(), 1);
        QVERIFY(r.value("sizes").toArray().at(0).toArray().at(0).toDouble() > 700);   // A4-ish in page units
        r = callSync(w, "words", {{"path", pdf}, {"index", 0}});
        const QJsonArray words = r.value("words").toArray();
        QVERIFY(words.size() >= 6);
        QCOMPARE(words.at(0).toArray().at(4).toString(), QStringLiteral("Hello"));
        QVERIFY(words.at(0).toArray().at(0).toDouble() > 80 && words.at(0).toArray().at(1).toDouble() > 60);
        r = callSync(w, "text", {{"path", pdf}, {"index", 0}});
        QVERIFY(r.value("text").toString().contains("Second line"));
        r = callSync(w, "search", {{"path", pdf}, {"query", "Slate"}});
        QCOMPARE(r.value("hits").toArray().size(), 1);
        r = callSync(w, "render", {{"path", pdf}, {"index", 0}, {"scale", 1.0}, {"out_dir", dir.path() + "/cache"}});
        const QString png = r.value("file").toString();
        QVERIFY(QFileInfo::exists(png));
        QVERIFY(r.value("width").toInt() > 700 && r.value("height").toInt() > 1000);
        QImage img(png); QVERIFY(!img.isNull()); QCOMPARE(img.width(), r.value("width").toInt());
        const QString out = dir.path() + "/out.pdf";
        QJsonArray pts; for (int i = 0; i <= 20; ++i) pts.append(QJsonArray{100.0 + i * 10, 300.0 + (i % 2) * 3, 3.0});
        const QJsonObject page{{"src", pdf}, {"index", 0}, {"width", 794}, {"height", 1123},
                               {"polys", QJsonArray{QJsonObject{{"points", pts}, {"color", QJsonArray{0.1, 0.2, 0.8}}, {"opacity", 1.0}, {"round", true}}}}};
        // typed blocks and pictures belong in the export too
        QImage sample(60, 30, QImage::Format_RGB32); sample.fill(Qt::magenta);
        const QString picture = dir.path() + "/pic.png";
        QVERIFY(sample.save(picture, "PNG"));
        const QJsonObject blank{{"width", 794}, {"height", 1123}, {"polys", QJsonArray{}},
                                {"blocks", QJsonArray{QJsonObject{{"x", 80.0}, {"y", 120.0}, {"w", 400.0},
                                                                  {"markdown", "## Waves\n\nThe **speed** is `f * lambda`."}}}},
                                {"images", QJsonArray{QJsonObject{{"path", picture}, {"x", 80.0}, {"y", 400.0}, {"w", 200.0}, {"h", 100.0}}}}};
        r = callSync(w, "export", {{"pages", QJsonArray{page, blank}}, {"out", out}});
        QCOMPARE(r.value("pages").toInt(), 2);
        QVERIFY(QFileInfo(out).size() > 1000);
        r = callSync(w, "info", {{"path", out}});
        QCOMPARE(r.value("pages").toInt(), 2);
        r = callSync(w, "text", {{"path", out}, {"index", 0}});
        QVERIFY(r.value("text").toString().contains("Hello"));   // the original text survived the overlay
        r = callSync(w, "text", {{"path", out}, {"index", 1}});
        const QString typed = r.value("text").toString();
        QVERIFY2(typed.contains("Waves"), qPrintable("typed heading missing from the export: " + typed));
        QVERIFY2(typed.contains("speed"), qPrintable("typed body missing from the export: " + typed));
        QVERIFY2(!typed.contains("**"), "Markdown syntax should not be printed literally");
    }
    void latexWorkerRendersAndReportsErrors() {
        QTemporaryDir dir;
        WorkerSupervisor w("latex");
        w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 90000);
        QJsonObject r = callSync(w, "render", {{"tex", "\\frac{d}{dx}\\left(x^2\\right) = 2x"}, {"out_dir", dir.path()}, {"dpi", 160}, {"color", "#123456"}});
        QVERIFY(r.value("ok").toBool());
        const QImage img(r.value("file").toString());
        QVERIFY(!img.isNull()); QVERIFY(img.width() > 40 && img.height() > 10);
        r = callSync(w, "check", {{"tex", "\\frac{1}{2"}});
        QVERIFY(!r.value("ok").toBool());
        r = callSync(w, "render", {{"tex", "\\frac{1}{2"}, {"out_dir", dir.path()}});
        QVERIFY(!r.value("ok").toBool()); QVERIFY(QFileInfo::exists(r.value("file").toString()));   // an error image, never a hang
    }
};
QTEST_MAIN(TstPdf)
#include "tst_pdf.moc"

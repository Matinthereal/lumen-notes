#include <QFont>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "ocr/linesegment.h"
#include "storage/paths.h"
#include "workers/workersupervisor.h"

class TstOcr : public QObject {
    Q_OBJECT
    static QJsonObject callSync(WorkerSupervisor &w, const QString &m, const QJsonObject &p, int ms = 600000) {
        QSignalSpy spy(&w, &WorkerSupervisor::response);
        const int id = w.request(m, p);
        QElapsedTimer t; t.start();
        while (t.elapsed() < ms) {
            QTest::qWait(20);
            for (int i = 0; i < spy.count(); ++i) if (spy[i].at(0).toInt() == id) {
                const QJsonObject err = spy[i].at(2).toJsonObject();
                if (!err.isEmpty()) qWarning() << m << "error:" << err;
                return spy[i].at(1).toJsonObject();
            }
        }
        qWarning() << m << "timed out"; return {};
    }
    static Stroke seg(quint64 id, float x0, float y0, float x1, float y1) {
        Stroke s; s.id = id; s.width = 1.5f;
        for (int i = 0; i <= 10; ++i) s.points.append({x0 + (x1 - x0) * i / 10.f, y0 + (y1 - y0) * i / 10.f, 0.5f, 0, 0, quint32(i * 3)});
        s.updateBounds(); return s;
    }
private slots:
    void segmentationGroupsLinesAndOrdersLeftToRight() {
        QVector<Stroke> strokes;
        strokes << seg(1, 120, 100, 140, 130) << seg(2, 60, 98, 90, 132) << seg(3, 200, 105, 230, 128)   // line 1 (out of order)
                << seg(4, 70, 200, 100, 230) << seg(5, 150, 198, 170, 232)                                // line 2
                << seg(6, 95, 92, 96, 93);                                                                // an i-dot above line 1
        Stroke hl = seg(7, 50, 110, 250, 112); hl.tool = InkTool::Highlighter; strokes << hl;
        const auto lines = segmentLines(strokes);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].ids, (QVector<quint64>{2, 6, 1, 3}));
        QCOMPARE(lines[1].ids, (QVector<quint64>{4, 5}));
        QVERIFY(lines[0].box.top() < lines[1].box.top());
    }
    void workerReadsPrintedTextAndMaths() {
        QTemporaryDir dir;
        WorkerSupervisor w("ocr");
        w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 120000);
        QJsonObject r = callSync(w, "prepare", {{"models_dir", paths::modelsDir()}});   // first run downloads TrOCR-small (~250 MB)
        QVERIFY2(r.value("ok").toBool(), "TrOCR could not be prepared (network?)");
        QImage img(360, 80, QImage::Format_RGB32); img.fill(Qt::white);
        { QPainter p(&img); p.setPen(Qt::black); QFont f("DejaVu Serif", 36); f.setItalic(true); p.setFont(f); p.drawText(QRect(10, 0, 340, 80), Qt::AlignVCenter | Qt::AlignLeft, "hello notes"); }
        const QString png = dir.path() + "/line.png"; QVERIFY(img.save(png));
        r = callSync(w, "recognize_lines", {{"lines", QJsonArray{QJsonObject{{"id", 0}, {"png", png}}}}});
        const QJsonArray lines = r.value("lines").toArray();
        QCOMPARE(lines.size(), 1);
        const QString text = lines.at(0).toObject().value("text").toString();
        qInfo() << "TrOCR read:" << text << "confidence" << lines.at(0).toObject().value("confidence").toDouble() << "ms" << lines.at(0).toObject().value("ms").toInt();
        QVERIFY2(text.toLower().contains("hello") || text.toLower().contains("notes"), qPrintable(text));
        // maths: a clean rendering of x^2 + 1 through the latex worker, then pix2tex
        WorkerSupervisor lw("latex"); lw.start();
        QTRY_VERIFY_WITH_TIMEOUT(lw.state() == WorkerSupervisor::State::Ready, 90000);
        r = callSync(lw, "render", {{"tex", "x^{2} + 1"}, {"out_dir", dir.path()}, {"dpi", 220}});
        QVERIFY(r.value("ok").toBool());
        r = callSync(w, "latex", {{"png", r.value("file").toString()}});   // first run downloads pix2tex weights
        const QString tex = r.value("latex").toString();
        qInfo() << "pix2tex read:" << tex;
        QVERIFY2(tex.contains("x") && tex.contains("2"), qPrintable(tex));
    }
};
QTEST_MAIN(TstOcr)
#include "tst_ocr.moc"

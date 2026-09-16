#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "papers/papersservice.h"
#include "pdf/pdfservice.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/schema.h"
#include "workers/workersupervisor.h"

class TstPapers : public QObject {
    Q_OBJECT
private slots:
    void importDetectMarkAndDashboard() {
        QTemporaryDir dir;
        qputenv("LUMEN_DATA_DIR", dir.path().toUtf8());
        Database db; QVERIFY(db.open(dir.path() + "/t.db")); QVERIFY(ensureSchema(db) > 0);
        Library lib(db); lib.seedDefaults();
        WorkerSupervisor w("pdf"); w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 60000);
        PdfService pdf(db, lib, &w);
        PapersService papers(db, lib, pdf, &w);
        // a paper: question numbers at the left margin, marks in brackets at the right
        QSignalSpy made(&w, &WorkerSupervisor::response);
        const QString paper = dir.path() + "/paper.pdf";
        w.request("make_test_pdf", {{"out", paper}, {"lines", QJsonArray{"1.   Differentiate y = x^3.                          [2]", "     Show your working.", "2.   Integrate 3x^2 between 0 and 2.               [3]", "3.   State Newton's second law.                     [1]"}}});
        QTRY_VERIFY_WITH_TIMEOUT(made.count() >= 1, 30000);
        QSignalSpy imported(&papers, &PapersService::imported);
        QSignalSpy detected(&papers, &PapersService::questionsDetected);
        papers.importPair(QUrl::fromLocalFile(paper), QUrl(), "Maths", 2024, "Paper 1", "Edexcel", 90);
        QTRY_VERIFY_WITH_TIMEOUT(imported.count() == 1, 60000);
        const qint64 paperId = imported.first().at(0).toLongLong();
        QTRY_VERIFY_WITH_TIMEOUT(detected.count() == 1, 60000);
        QVariantList qs = papers.questions(paperId);
        QCOMPARE(qs.size(), 3);
        QCOMPARE(qs[0].toMap().value("label").toString(), QStringLiteral("1"));
        QCOMPARE(qs[0].toMap().value("marks").toInt(), 2);
        QCOMPARE(qs[1].toMap().value("marks").toInt(), 3);
        QCOMPARE(papers.paper(paperId).value("totalMarks").toInt(), 6);
        QCOMPARE(papers.papers("Maths").size(), 1);
        QVERIFY(papers.paperForPage(imported.first().at(1).toLongLong()) == paperId);
        papers.setQuestion(qs[0].toMap().value("id").toLongLong(), "1", 2, "differentiation");
        papers.setQuestion(qs[1].toMap().value("id").toLongLong(), "2", 3, "integration");
        papers.setQuestion(qs[2].toMap().value("id").toLongLong(), "3", 1, "mechanics, laws");
        const qint64 attempt = papers.startAttempt(paperId, true);
        QVERIFY(attempt > 0); QCOMPARE(papers.activeAttempt(), attempt);
        papers.recordAnswer(attempt, qs[0].toMap().value("id").toLongLong(), 2, 120, "");
        papers.recordAnswer(attempt, qs[1].toMap().value("id").toLongLong(), 1, 300, "did not know");
        papers.recordAnswer(attempt, qs[2].toMap().value("id").toLongLong(), 0, 30, "careless");
        const QVariantMap sum = papers.attemptSummary(attempt);
        QCOMPARE(sum.value("scored").toInt(), 3); QCOMPARE(sum.value("available").toInt(), 6); QCOMPARE(sum.value("seconds").toInt(), 450);
        papers.finishAttempt(attempt);
        QCOMPARE(papers.activeAttempt(), 0ll);
        const QVariantList weak = papers.weakTopics("Maths");
        QVERIFY(weak.size() >= 3);
        QCOMPARE(weak[0].toMap().value("percent").toDouble(), 0.0);           // mechanics (0/1) is weakest
        QVERIFY(weak.last().toMap().value("percent").toDouble() == 100.0);    // differentiation 2/2
        const QVariantList errs = papers.errorBreakdown("Maths");
        QVERIFY(!errs.isEmpty()); QCOMPARE(errs[0].toMap().value("errorType").toString(), QStringLiteral("did not know"));   // lost the most marks
        const QVariantList tr = papers.trend("Maths");
        QCOMPARE(tr.size(), 1); QCOMPARE(tr[0].toMap().value("percent").toDouble(), 50.0);
    }
};
QTEST_MAIN(TstPapers)
#include "tst_papers.moc"

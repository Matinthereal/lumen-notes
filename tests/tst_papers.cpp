#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include "papers/paperdetect.h"
#include "papers/papersservice.h"
#include "pdf/pdfservice.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/schema.h"
#include "workers/workersupervisor.h"

// Synthetic pages laid out the way each board prints (no exam text): A4 at 96 dpi, words placed
// left to right, the way the pdf helper reports them.
struct Sheet {
    QList<paperdetect::Page> pages;
    Sheet &page() { pages.append({QSizeF(794, 1123), {}}); return *this; }
    // Words from x on the row at y; each word 7 px a character, 5 px apart.
    Sheet &row(double x, double y, const QStringList &words, double h = 18)
    {
        for (const QString &w : words) {
            const double width = 7.0 * w.size();
            pages.last().words.append({QRectF(x, y, width, h), w});
            x += width + 5;
        }
        return *this;
    }
    Sheet &word(double x, double y, const QString &w, double width, double h = 18) { pages.last().words.append({QRectF(x, y, width, h), w}); return *this; }
};

QStringList labelsOf(const QList<paperdetect::Question> &qs) { QStringList out; for (const auto &q : qs) out << q.label; return out; }
QList<int> marksOf(const QList<paperdetect::Question> &qs) { QList<int> out; for (const auto &q : qs) out << q.marks; return out; }

class TstPapers : public QObject {
    Q_OBJECT
private slots:
    // AQA: question numbers in boxes, one digit each ("0 1 . 1"), a stem above its parts, marks as
    // "[1 mark]" / "[2 marks]", multiple choice numbered on. The old detector made every digit a
    // question worth 0.
    void detectsAqaBoxedNumbers()
    {
        Sheet s;
        s.page().row(75, 400, {"AS", "PHYSICS"}).row(99, 950, {"The", "maximum", "mark", "for", "this", "paper", "is", "22"});
        s.page().row(390, 40, {"2"}).row(723, 37, {"Do", "not", "write"})
            .word(90.7, 190, "0", 8.2, 20.6).word(114.2, 190, "1", 8.2, 20.6).row(160, 190, {"This", "question", "is", "about", "a", "wire."})
            .row(75.5, 1052, {"*02*"});
        s.page().word(90.7, 90, "0", 8.2, 20.6).word(114.2, 90, "1", 8.2, 20.6).word(135.5, 90, ".", 4.1, 20.6).word(152.6, 90, "1", 8.2, 20.6)
            .row(190, 90, {"Read", "the", "scale."})
            .word(652.3, 144, "[1", 13).word(669.4, 144, "mark]", 40)
            .word(90.7, 252, "0", 8.2, 20.6).word(114.2, 252, "1", 8.2, 20.6).word(135.5, 252, ".", 4.1, 20.6).word(152.6, 252, "2", 8.2, 20.6)
            .row(190, 252, {"Figure", "2", "shows", "the", "graph."})
            .row(100, 400, {"0", "10", "20"})                                  // a table's first column, not questions
            .word(644.2, 600, "[4", 13).word(661.4, 600, "marks]", 48)
            .row(75.5, 1052, {"*03*"});
        s.page().word(90.7, 90, "0", 8.2, 20.6).word(114.2, 90, "2", 8.2, 20.6).row(190, 90, {"Which", "is", "a", "scalar?"})
            .word(652.3, 108, "[1", 13).word(669.4, 108, "mark]", 40)
            .word(90.7, 480, "0", 8.2, 20.6).word(114.2, 480, "3", 8.2, 20.6).row(190, 480, {"A", "cell", "of", "negligible", "resistance."})
            .word(652.3, 551, "[1", 13).word(669.4, 551, "mark]", 40)
            .word(90.7, 700, "0", 8.2, 20.6).word(114.2, 700, "4", 8.2, 20.6).word(135.5, 700, ".", 4.1, 20.6).word(152.6, 700, "1", 8.2, 20.6)
            .row(190, 700, {"Calculate", "the", "current."})
            .word(644.2, 760, "[3", 13).word(661.4, 760, "marks]", 48);
        s.page().word(90.7, 90, "0", 8.2, 20.6).word(114.2, 90, "4", 8.2, 20.6).word(135.5, 90, ".", 4.1, 20.6).word(152.6, 90, "2", 8.2, 20.6)
            .row(190, 90, {"Explain", "why."})
            .word(644.2, 300, "[12", 20).word(668, 300, "marks]", 48);
        const auto qs = paperdetect::detect(s.pages);
        QCOMPARE(labelsOf(qs), (QStringList{"01.1", "01.2", "02", "03", "04.1", "04.2"}));
        QCOMPARE(marksOf(qs), (QList<int>{1, 4, 1, 1, 3, 12}));
        QCOMPARE(qs.at(0).page, 2);
        QVERIFY(qs.at(0).rect.top() < 90 && qs.at(0).rect.bottom() <= 252);   // 01.1 ends where 01.2 starts
        QCOMPARE(qs.at(4).page, 3);
        QVERIFY(qs.at(4).rect.top() < 700);
    }

    // Edexcel: "1." then "(a)", "(b)", "(i)"; marks a bare "(3)" at the right margin; the total
    // line, "Question 1 continued", page numbers and the rotated "DO NOT WRITE IN THIS AREA" are
    // not questions or marks.
    void detectsEdexcelParts()
    {
        Sheet s;
        s.page().row(155, 844, {"The", "total", "mark", "for", "this", "paper", "is", "30."});
        s.page().row(56.6, 80, {"1.", "The", "line", "l1", "has", "equation", "2x", "+", "4y", "=", "3"})
            .row(81.2, 176, {"(a)", "find", "the", "value", "of", "m."}).row(661.3, 194.6, {"(2)"})
            .word(761.8, 182.8, "WRITE", 16, 45.6).word(761.8, 232, "IN", 16, 15.6)
            .row(81.2, 226, {"Using", "equation", "(1)", "gives", "P."}).row(560, 226, {"by", "(2)", "follows."})     // a reference, not marks
            .row(81.2, 258.6, {"(b)", "Find", "the", "x", "coordinate", "of", "P."}).row(661.3, 277.3, {"(2)"})
            .row(82, 1066.6, {"2"});
        s.page().word(16, 70, "AREA", 16, 39).row(56.6, 80, {"Question", "1", "continued"})
            .row(456.5, 1021.3, {"(Total", "for", "Question", "1", "is", "4", "marks)"})
            .row(714.7, 1067, {"3"});
        s.page().row(56.6, 80, {"2.", "Find", "all", "real", "solutions"})
            .row(81.2, 112, {"(i)", "16a2", "=", "2"}).row(661.3, 129.3, {"(4)"})
            .row(81.2, 161.3, {"(ii)", "b4", "+", "7b2", "=", "18"}).row(661.3, 178.7, {"(4)"})
            .row(56.6, 400, {"3.", "(a)", "Given", "that", "k", "is", "a", "constant,", "find"}).row(661.3, 475, {"(3)"})
            .row(81.2, 507, {"(b)", "Hence", "find", "k."}).row(661.3, 607.9, {"(3)"})
            .row(82, 1066.6, {"4"});
        s.page().row(56.6, 80, {"4.", "A", "car", "is", "modelled"})
            .row(81.2, 217.8, {"(a)", "find", "the", "initial", "value."}).row(661.3, 233.8, {"(1)"})
            .row(81.2, 316.4, {"(b)", "(i)", "show", "that"})
            .row(101.1, 390.3, {"(ii)", "Hence", "find", "the", "age."}).row(661.3, 459.6, {"(6)"})
            .row(456.5, 1021.3, {"(Total", "for", "Question", "4", "is", "7", "marks)"});
        const auto qs = paperdetect::detect(s.pages);
        QCOMPARE(labelsOf(qs), (QStringList{"1(a)", "1(b)", "2(i)", "2(ii)", "3(a)", "3(b)", "4(a)", "4(b)(i)", "4(b)(ii)"}));
        QCOMPARE(marksOf(qs), (QList<int>{2, 2, 4, 4, 3, 3, 1, 0, 6}));
        int total = 0; for (int m : marksOf(qs)) total += m;
        QCOMPARE(total, 25);
        // "1." only introduces its parts: its region goes to (a), which starts at the stem.
        QCOMPARE(qs.at(0).page, 1);
        QVERIFY(qs.at(0).rect.top() <= 80);
    }

    // OCR: "1" alone, "(a)" and "(i)" under it (roman numerals right-aligned, so some sit further
    // right than 16 % of the page), marks "[2]" at the right edge, "*" on extended answers, and
    // numbered answer lines ("1 ....") that are not questions.
    void detectsOcrParts()
    {
        Sheet s;
        s.page().row(99, 800, {"The", "total", "mark", "for", "this", "paper", "is", "37."}).row(108.9, 1014.9, {"©", "OCR", "2023", "[601/4911/5]"});
        s.page().row(66.1, 82, {"1"}).row(96.4, 82, {"A", "small", "business", "uses", "computers."})
            .row(96.4, 134, {"(a)", "A", "spreadsheet", "is", "used."})
            .row(127.9, 168.8, {"(i)", "Give", "one", "benefit."})
            .row(159.9, 255.7, {".........................................."}).row(709.3, 255.5, {"[1]"})
            .row(123.8, 290.2, {"(ii)", "Give", "two", "other", "packages."})
            .row(156.9, 400, {"1", "................................"}).row(156.9, 470, {"2", "................................"})
            .row(709.6, 602.2, {"[4]"})
            .row(119.7, 636.9, {"(iii)", "Describe", "a", "drawback."}).row(709.3, 862.2, {"[3]"});
        s.page().row(96.4, 82, {"(b)", "Each", "computer", "has", "a", "BIOS."})
            .row(126.6, 117, {"Tick", "(3)", "one", "box", "in", "each", "row."}).row(709.6, 366.6, {"[3]"})
            .row(96.4, 435.9, {"(c)*", "Explain", "memory", "management."}).row(709.6, 603.5, {"[9]"});
        s.page().row(66.1, 82, {"2"}).row(96.4, 498, {"(a)", "Complete", "the", "pseudocode."}).row(709.7, 948.5, {"[5]"});
        s.page().row(66.1, 82, {"3*"}).row(96.4, 82.4, {"Discuss", "the", "Act."}).row(701.5, 255.5, {"[12]"})
            .row(66, 600, {"END", "OF", "QUESTION", "PAPER"});
        const auto qs = paperdetect::detect(s.pages);
        QCOMPARE(labelsOf(qs), (QStringList{"1(a)(i)", "1(a)(ii)", "1(a)(iii)", "1(b)", "1(c)", "2(a)", "3"}));
        QCOMPARE(marksOf(qs), (QList<int>{1, 4, 3, 3, 9, 5, 12}));
    }

    // The first word of a question's text can be a lone digit ("3 people"); it is not a box of the
    // label before it.
    void bodyDigitIsNotPartOfLabel()
    {
        Sheet s;
        s.page().word(90.7, 90, "0", 8.2, 20.6).word(114.2, 90, "4", 8.2, 20.6).word(135.5, 90, ".", 4.1, 20.6).word(152.6, 90, "2", 8.2, 20.6)
            .row(190, 90, {"3", "people", "share", "a", "van."})
            .word(644.2, 130, "[1", 13).word(661.4, 130, "mark]", 48)
            .word(90.7, 300, "0", 8.2, 20.6).word(114.2, 300, "4", 8.2, 20.6).word(135.5, 300, ".", 4.1, 20.6).word(152.6, 300, "3", 8.2, 20.6)
            .row(190, 300, {"Find", "the", "speed."})
            .word(644.2, 340, "[2", 13).word(661.4, 340, "marks]", 48);
        s.page().word(90.7, 90, "0", 8.2, 20.6).word(114.2, 90, "4", 8.2, 20.6).word(135.5, 90, ".", 4.1, 20.6).word(152.6, 90, "4", 8.2, 20.6)
            .row(190, 90, {"Explain", "why."})
            .word(644.2, 130, "[3", 13).word(661.4, 130, "marks]", 48);
        QCOMPARE(labelsOf(paperdetect::detect(s.pages)), (QStringList{"04.2", "04.3", "04.4"}));

        // Edexcel with a bare number: "5" then text that starts "3 men".
        Sheet e;
        e.page().row(56.6, 80, {"4"}).row(81.2, 80, {"A", "car", "stops."}).row(661.3, 120, {"(2)"})
            .row(56.6, 300, {"5"}).row(81.2, 300, {"3", "men", "share", "a", "van."}).row(661.3, 340, {"(3)"})
            .row(56.6, 500, {"6"}).row(81.2, 500, {"Find", "x."}).row(661.3, 540, {"(4)"});
        QCOMPARE(labelsOf(paperdetect::detect(e.pages)), (QStringList{"4", "5", "6"}));
        QCOMPARE(marksOf(paperdetect::detect(e.pages)), (QList<int>{2, 3, 4}));

        // And with the dot Edexcel usually prints: "5." then "3 men" is not part 5.3.
        Sheet d;
        d.page().row(56.6, 80, {"4."}).row(81.2, 80, {"A", "car", "stops."}).row(661.3, 120, {"(2)"})
            .row(56.6, 300, {"5."}).row(81.2, 300, {"3", "men", "share", "a", "van."}).row(661.3, 340, {"(3)"})
            .row(56.6, 500, {"6."}).row(81.2, 500, {"Find", "x."}).row(661.3, 540, {"(4)"});
        QCOMPARE(labelsOf(paperdetect::detect(d.pages)), (QStringList{"4", "5", "6"}));
    }

    // A real two-digit part still joins: boxes "1 0" after "1 2 . 9", in a two-digit question.
    void twoDigitAqaPartsStillJoin()
    {
        Sheet s;
        const auto box = [&s](double y, const QStringList &digits) {
            double x = 90.7;
            for (const QString &d : digits) {
                s.word(x, y, d, d == "." ? 4.1 : 8.2, 20.6);
                x += d == "." ? 17.1 : 23.5;
            }
        };
        s.page();
        box(90, {"1", "2", ".", "8"}); s.row(210, 90, {"Read", "it."}).word(652.3, 130, "[1", 13).word(669.4, 130, "mark]", 40);
        box(250, {"1", "2", ".", "9"}); s.row(210, 250, {"Read", "it."}).word(652.3, 290, "[2", 13).word(669.4, 290, "marks]", 48);
        box(450, {"1", "2", ".", "1", "0"}); s.row(240, 450, {"Read", "it."}).word(644.2, 490, "[3", 13).word(661.4, 490, "marks]", 48);
        QCOMPARE(labelsOf(paperdetect::detect(s.pages)), (QStringList{"12.8", "12.9", "12.10"}));
        QCOMPARE(marksOf(paperdetect::detect(s.pages)), (QList<int>{1, 2, 3}));
    }

    // "(i)" is a roman numeral, except straight after "(h)", where it is the next letter.
    void letterIAfterH()
    {
        Sheet s;
        s.page().row(60, 80, {"5"}).row(90, 120, {"(a)", "Answer."})
            .row(120, 160, {"(i)", "One."}).row(700, 180, {"[1]"})
            .row(120, 200, {"(ii)", "Two."}).row(700, 220, {"[1]"});
        const char *letters = "bcdefgh";
        for (int k = 0; k < 7; ++k) s.row(90, 240 + 40 * k, {QStringLiteral("(%1)").arg(QLatin1Char(letters[k])), "Answer."}).row(700, 255 + 40 * k, {"[1]"});
        s.row(90, 600, {"(i)", "Last."}).row(700, 620, {"[1]"});
        const auto qs = paperdetect::detect(s.pages);
        QCOMPARE(labelsOf(qs), (QStringList{"5(a)(i)", "5(a)(ii)", "5(b)", "5(c)", "5(d)", "5(e)", "5(f)", "5(g)", "5(h)", "5(i)"}));
    }

    void yearsFromNamesAndCovers()
    {
        QCOMPARE(paperdetect::yearFromName("AQA_AS_Physics_Paper2_June2016_QP.pdf"), 2016);
        QCOMPARE(paperdetect::yearFromName("June 2019 QP (1).pdf"), 2019);
        QCOMPARE(paperdetect::yearFromName("AQA-74072-QP-JUN16.PDF"), 2016);
        QCOMPARE(paperdetect::yearFromName("8ma0-01-que-20190515.pdf"), 2019);
        QCOMPARE(paperdetect::yearFromName("H446-01 Summer 2022.pdf"), 2022);
        QCOMPARE(paperdetect::yearFromName("maths 2021 paper 1.pdf"), 2021);
        QCOMPARE(paperdetect::yearFromName("703780-question-paper-computer-systems.pdf"), 0);
        QCOMPARE(paperdetect::yearFromName("7407-2 Paper 2.pdf"), 0);
        QCOMPARE(paperdetect::yearFromCover("AS PHYSICS Paper 2 Thursday 9 June 2016 Afternoon Time allowed"), 2016);
        QCOMPARE(paperdetect::yearFromCover("Monday 12 June 2023 – Afternoon"), 2023);
        QCOMPARE(paperdetect::yearFromCover("Summer 2019"), 2019);
        QCOMPARE(paperdetect::yearFromCover("*Jun167407201* M/IB/Jun16/E4 7407/2"), 0);     // AQA's codes are not a year
        QCOMPARE(paperdetect::yearFromCover("For first teaching in September 2015 Thursday 9 June 2016 Afternoon"), 2016);
        QCOMPARE(paperdetect::yearFromCover("Specification first assessment September 2017 Paper 1 Tuesday 14 May 2019"), 2019);
        QCOMPARE(paperdetect::yearFromCover("First examination June 2017 Summer 2019"), 2019);
        QCOMPARE(paperdetect::yearFromCover("Specification September 2015"), 0);
        QCOMPARE(paperdetect::yearFromCover("Thursday 9 September 2021 Resit"), 2021);
        QCOMPARE(paperdetect::yearFromCover("Resit September 2021 Thursday 9 June 2022"), 2022);
        QCOMPARE(paperdetect::yearFromCover("© OCR 2023 [601/4911/5] Time allowed: 2 hours 30 minutes"), 0);
    }

    // LUMEN_PAPERS_CHECK=<pdf>[;<pdf>…]: run the detector on real papers through the pdf helper and
    // print what it found against the total printed on the cover. The papers stay on this machine.
    void realPapers()
    {
        const QString list = qEnvironmentVariable("LUMEN_PAPERS_CHECK");
        if (list.isEmpty()) QSKIP("set LUMEN_PAPERS_CHECK to check real papers");
        WorkerSupervisor w("pdf"); w.start();
        QTRY_VERIFY_WITH_TIMEOUT(w.state() == WorkerSupervisor::State::Ready, 60000);
        const auto ask = [&w](const QString &method, const QJsonObject &params) {
            QSignalSpy spy(&w, &WorkerSupervisor::response);
            const int id = w.request(method, params);
            QJsonObject result;
            for (int tries = 0; tries < 600 && result.isEmpty(); ++tries) {
                if (spy.isEmpty()) spy.wait(100);
                for (const auto &args : std::as_const(spy)) if (args.at(0).toInt() == id) result = args.at(1).toJsonObject();
            }
            return result;
        };
        for (const QString &path : list.split(u';', Qt::SkipEmptyParts)) {
            const QJsonObject info = ask("info", {{"path", path}});
            QList<paperdetect::Page> pages;
            for (int i = 0; i < info.value("pages").toInt(); ++i) {
                const QJsonArray size = info.value("sizes").toArray().at(i).toArray();
                pages << paperdetect::fromWorker(ask("words", {{"path", path}, {"index", i}}).value("words").toArray(), QSizeF(size.at(0).toDouble(), size.at(1).toDouble()));
            }
            const QString cover = ask("text", {{"path", path}, {"index", 0}}).value("text").toString();
            const auto printed = QRegularExpression(R"((?:maximum|total) mark for this paper is (\d+))").match(cover);
            const auto qs = paperdetect::detect(pages);
            int total = 0; for (const auto &q : qs) total += q.marks;
            QStringList shown; for (const auto &q : qs) shown << QStringLiteral("%1=%2").arg(q.label).arg(q.marks);
            qInfo("%s\n  year %d · %lld questions · %d marks · cover says %s\n  %s", qPrintable(QFileInfo(path).fileName()), paperdetect::yearFromCover(cover),
                  qlonglong(qs.size()), total, qPrintable(printed.hasMatch() ? printed.captured(1) : QStringLiteral("?")), qPrintable(shown.join(u' ')));
            if (printed.hasMatch()) QCOMPARE(total, printed.captured(1).toInt());
        }
    }

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
        // Detecting again (the paper bar's button) clears what an older detector left, 0-mark
        // labels like AQA's single digits, and never doubles what is already there.
        papers.addQuestion(paperId, "0", 0, QRectF(0, 0, 10, 10), 0);
        papers.addQuestion(paperId, "1", 0, QRectF(0, 20, 10, 10), 0);
        papers.detectQuestions(paperId);
        QTRY_VERIFY_WITH_TIMEOUT(detected.count() == 2, 60000);
        qs = papers.questions(paperId);
        QStringList again; for (const QVariant &q : std::as_const(qs)) again << q.toMap().value("label").toString();
        QCOMPARE(again, (QStringList{"1", "2", "3"}));
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

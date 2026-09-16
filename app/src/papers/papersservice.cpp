#include "papersservice.h"
#include "pdf/pdfservice.h"
#include "storage/attachments.h"
#include "storage/database.h"
#include "storage/library.h"
#include "workers/workersupervisor.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

PapersService::PapersService(Database &db, Library &lib, PdfService &pdf, WorkerSupervisor *pdfWorker, QObject *parent)
    : QObject(parent), m_db(db), m_lib(lib), m_pdf(pdf), m_worker(pdfWorker)
{
    connect(pdfWorker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &r, const QJsonObject &e) { const Callback cb = m_pending.take(id); if (cb) cb(r, e); });
}

void PapersService::call(const QString &method, const QJsonObject &params, Callback cb) { m_pending.insert(m_worker->request(method, params), std::move(cb)); }

void PapersService::importPair(const QUrl &paperPdf, const QUrl &schemePdf, const QString &subject, int year, const QString &name, const QString &board, int minutes)
{
    QString err;
    const QString paperSha = attachments::store(m_db, paperPdf.toLocalFile(), "application/pdf", &err);
    if (paperSha.isEmpty()) { emit failed(err); return; }
    QString schemeSha;
    if (schemePdf.isValid() && !schemePdf.isEmpty()) schemeSha = attachments::store(m_db, schemePdf.toLocalFile(), "application/pdf", &err);
    // The paper's pages live in a "Past papers" section of the subject's notebook.
    qint64 notebookId = 0;
    for (const QVariant &n : m_lib.notebooks()) if (n.toMap().value("name").toString() == subject) notebookId = n.toMap().value("id").toLongLong();
    if (!notebookId) notebookId = m_lib.createNotebook(subject, "#5C6B7A", board);
    const QString title = name.isEmpty() ? QFileInfo(paperPdf.toLocalFile()).completeBaseName() : name;
    Database::Query q(m_db, "INSERT INTO paper(subject, year, name, board, paper_attachment, scheme_attachment, minutes, created) VALUES (?,?,?,?,?,?,?,?)");
    q.bind(1, subject).bind(2, year).bind(3, title).bind(4, board).bind(5, paperSha); if (schemeSha.isEmpty()) q.bindNull(6); else q.bind(6, schemeSha); q.bind(7, minutes).bind(8, QDateTime::currentSecsSinceEpoch());
    if (!q.run()) { emit failed(m_db.lastError()); return; }
    const qint64 paperId = m_db.lastInsertId();
    const QString token = QStringLiteral("paper-%1").arg(paperId);
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(&m_pdf, &PdfService::imported, this, [this, paperId, conn, schemePdf, notebookId, title, token](qint64 sectionId, qint64 firstPageId, const QString &tok) {
        if (tok != token) return;                        // another import finishing first must not claim this paper
        QObject::disconnect(*conn);
        Database::Query u(m_db, "UPDATE paper SET section_id=? WHERE id=?"); u.bind(1, sectionId).bind(2, paperId); u.run();
        if (schemePdf.isValid() && !schemePdf.isEmpty()) m_pdf.importAsSection(schemePdf, notebookId, title + " — mark scheme");
        emit changed();
        emit imported(paperId, firstPageId);
        detectQuestions(paperId);
    });
    m_pdf.importAsSection(paperPdf, notebookId, QStringLiteral("%1 %2").arg(year).arg(title), token);
}

QVariantList PapersService::subjects() const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT subject, COUNT(*) FROM paper WHERE deleted_at IS NULL GROUP BY subject ORDER BY subject");
    while (q.step()) out.append(QVariantMap{{"subject", q.text(0)}, {"count", q.i32(1)}});
    return out;
}

QVariantList PapersService::papers(const QString &subject) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT p.id, p.subject, p.year, p.name, p.board, p.minutes, p.section_id, (SELECT COUNT(*) FROM paper_question x WHERE x.paper_id=p.id),"
                            " (SELECT COUNT(*) FROM paper_attempt a WHERE a.paper_id=p.id AND a.finished IS NOT NULL), p.scheme_attachment IS NOT NULL"
                            " FROM paper p WHERE p.deleted_at IS NULL AND (?='' OR p.subject=?) ORDER BY p.subject, p.year DESC, p.name");
    q.bind(1, subject).bind(2, subject);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"subject", q.text(1)}, {"year", q.i32(2)}, {"name", q.text(3)}, {"board", q.text(4)}, {"minutes", q.i32(5)}, {"sectionId", q.i64(6)}, {"questionCount", q.i32(7)}, {"attempts", q.i32(8)}, {"hasScheme", q.i32(9) != 0}});
    return out;
}

QVariantMap PapersService::paper(qint64 id) const
{
    Database::Query q(m_db, "SELECT id, subject, year, name, board, minutes, section_id, total_marks FROM paper WHERE id=?");
    q.bind(1, id);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"subject", q.text(1)}, {"year", q.i32(2)}, {"name", q.text(3)}, {"board", q.text(4)}, {"minutes", q.i32(5)}, {"sectionId", q.i64(6)}, {"totalMarks", q.i32(7)}};
}

qint64 PapersService::paperForPage(qint64 pageId) const
{
    Database::Query q(m_db, "SELECT p.id FROM paper p JOIN page g ON g.section_id=p.section_id WHERE g.id=? AND p.deleted_at IS NULL");
    q.bind(1, pageId);
    return q.step() ? q.i64(0) : 0;
}

void PapersService::removePaper(qint64 id) { Database::Query q(m_db, "UPDATE paper SET deleted_at=? WHERE id=?"); q.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, id); q.run(); emit changed(); }

void PapersService::restorePaper(qint64 id) { Database::Query q(m_db, "UPDATE paper SET deleted_at=NULL WHERE id=?"); q.bind(1, id); q.run(); emit changed(); }

QVariantList PapersService::questions(qint64 paperId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, label, page_index, x, y, w, h, marks_available, topic_tags FROM paper_question WHERE paper_id=? ORDER BY sort, page_index, y");
    q.bind(1, paperId);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"label", q.text(1)}, {"pageIndex", q.i32(2)}, {"x", q.f64(3)}, {"y", q.f64(4)}, {"w", q.f64(5)}, {"h", q.f64(6)}, {"marks", q.i32(7)}, {"topics", q.text(8)}});
    return out;
}

qint64 PapersService::addQuestion(qint64 paperId, const QString &label, int pageIndex, const QRectF &rect, int marks)
{
    Database::Query q(m_db, "INSERT INTO paper_question(paper_id, label, page_index, x, y, w, h, marks_available, sort) VALUES (?,?,?,?,?,?,?,?,(SELECT COALESCE(MAX(sort),0)+1 FROM paper_question WHERE paper_id=?))");
    q.bind(1, paperId).bind(2, label).bind(3, pageIndex).bind(4, rect.x()).bind(5, rect.y()).bind(6, rect.width()).bind(7, rect.height()).bind(8, marks).bind(9, paperId);
    if (!q.run()) return 0;
    emit changed();
    return m_db.lastInsertId();
}

void PapersService::setQuestion(qint64 id, const QString &label, int marks, const QString &topics)
{
    Database::Query q(m_db, "UPDATE paper_question SET label=?, marks_available=?, topic_tags=? WHERE id=?"); q.bind(1, label).bind(2, marks).bind(3, topics.trimmed()).bind(4, id); q.run();
    emit changed();
}
void PapersService::setRegion(qint64 id, int pageIndex, const QRectF &r)
{
    Database::Query q(m_db, "UPDATE paper_question SET page_index=?, x=?, y=?, w=?, h=? WHERE id=?"); q.bind(1, pageIndex).bind(2, r.x()).bind(3, r.y()).bind(4, r.width()).bind(5, r.height()).bind(6, id); q.run();
    emit changed();
}
void PapersService::removeQuestion(qint64 id) { Database::Query q(m_db, "DELETE FROM paper_question WHERE id=?"); q.bind(1, id); q.run(); emit changed(); }

// Question labels sit at the left margin: "1", "1.", "2(a)", "(b)", "(ii)". Marks appear as "[3]" or "(3 marks)".
void PapersService::detectQuestions(qint64 paperId)
{
    const QVariantMap p = paper(paperId);
    QString sha; { Database::Query q(m_db, "SELECT paper_attachment FROM paper WHERE id=?"); q.bind(1, paperId); if (q.step()) sha = q.text(0); }
    if (sha.isEmpty()) return;
    const QString path = attachments::pathFor(sha, "application/pdf");
    call("info", {{"path", path}}, [this, paperId, path](const QJsonObject &r, const QJsonObject &e) {
        if (!e.isEmpty()) { emit failed(e.value("message").toString()); return; }
        const int pages = r.value("pages").toInt();
        const QJsonArray sizes = r.value("sizes").toArray();
        auto remaining = std::make_shared<int>(pages);
        auto found = std::make_shared<QVector<QVariantMap>>();
        for (int i = 0; i < pages; ++i) {
            const double pw = sizes.at(i).toArray().at(0).toDouble(794), ph = sizes.at(i).toArray().at(1).toDouble(1123);
            call("words", {{"path", path}, {"index", i}}, [this, paperId, i, pw, ph, remaining, found](const QJsonObject &r, const QJsonObject &) {
                static const QRegularExpression label(R"(^(\d{1,2}[.)]?|\d{1,2}\([a-z]\)|\([a-z]\)|\([ivx]{1,4}\))$)");
                static const QRegularExpression bracketMarks(R"(^\[(\d{1,3})\]$)");          // [3]
                static const QRegularExpression parenMarks(R"(^\((\d{1,3})$)");              // (3 marks)
                QVector<QVariantMap> labels;
                QVector<QPair<double, int>> markRows;   // y → marks
                const QJsonArray words = r.value("words").toArray();
                for (int wi = 0; wi < words.size(); ++wi) {
                    const QJsonArray w = words.at(wi).toArray();
                    const double x0 = w.at(0).toDouble(), y0 = w.at(1).toDouble(), x1 = w.at(2).toDouble(), y1 = w.at(3).toDouble();
                    const QString text = w.at(4).toString();
                    if (x0 < pw * 0.16 && label.match(text).hasMatch()) labels.append(QVariantMap{{"label", text}, {"y", y0}});
                    const auto bm = bracketMarks.match(text);
                    if (bm.hasMatch()) { markRows.append({y1, bm.captured(1).toInt()}); continue; }
                    const auto pmk = parenMarks.match(text);
                    if (pmk.hasMatch() && wi + 1 < words.size() && words.at(wi + 1).toArray().at(4).toString().startsWith("mark", Qt::CaseInsensitive))
                        markRows.append({y1, pmk.captured(1).toInt()});
                    Q_UNUSED(x1);
                }
                std::sort(labels.begin(), labels.end(), [](const QVariantMap &a, const QVariantMap &b) { return a.value("y").toDouble() < b.value("y").toDouble(); });
                for (int k = 0; k < labels.size(); ++k) {
                    const double y = labels[k].value("y").toDouble() - 4;
                    const double yEnd = k + 1 < labels.size() ? labels[k + 1].value("y").toDouble() - 4 : ph - 30;
                    int mk = 0;
                    for (const auto &mr : markRows) if (mr.first > y && mr.first <= yEnd + 2) mk += mr.second;
                    found->append(QVariantMap{{"label", labels[k].value("label")}, {"page", i}, {"rect", QRectF(pw * 0.05, y, pw * 0.9, std::max(20.0, yEnd - y))}, {"marks", mk}});
                }
                if (--*remaining == 0) {
                    std::sort(found->begin(), found->end(), [](const QVariantMap &a, const QVariantMap &b) { return a.value("page").toInt() != b.value("page").toInt() ? a.value("page").toInt() < b.value("page").toInt() : a.value("rect").toRectF().y() < b.value("rect").toRectF().y(); });
                    m_db.begin();
                    Database::Query d(m_db, "DELETE FROM paper_question WHERE paper_id=? AND marks_available=0 AND topic_tags=''"); d.bind(1, paperId); d.run();
                    int n = 0;
                    QString parent;   // "3" carries into "(a)" → "3(a)"
                    for (const QVariantMap &f : *found) {
                        QString lbl = f.value("label").toString();
                        if (lbl.endsWith('.')) lbl.chop(1);
                        if (lbl.startsWith('(')) lbl = parent + lbl;               // "(a)" under "3" → "3(a)"
                        else { parent = lbl.section('(', 0, 0); if (lbl.endsWith(')') && !lbl.contains('(')) lbl.chop(1); }
                        if (addQuestion(paperId, lbl, f.value("page").toInt(), f.value("rect").toRectF(), f.value("marks").toInt())) ++n;
                    }
                    Database::Query t(m_db, "UPDATE paper SET total_marks=(SELECT COALESCE(SUM(marks_available),0) FROM paper_question WHERE paper_id=?) WHERE id=?"); t.bind(1, paperId).bind(2, paperId); t.run();
                    m_db.commit();
                    emit questionsDetected(paperId, n);
                    emit changed();
                }
            });
        }
    });
}

qint64 PapersService::startAttempt(qint64 paperId, bool timerMode)
{
    Database::Query q(m_db, "INSERT INTO paper_attempt(paper_id, started, timer_mode) VALUES (?,?,?)");
    q.bind(1, paperId).bind(2, QDateTime::currentSecsSinceEpoch()).bind(3, timerMode ? 1 : 0);
    if (!q.run()) return 0;
    const qint64 id = m_db.lastInsertId();
    setActive(paperId, id);
    emit changed();
    return id;
}

void PapersService::recordAnswer(qint64 attemptId, qint64 questionId, int marksScored, int seconds, const QString &errorType)
{
    Database::Query q(m_db, "INSERT OR REPLACE INTO paper_question_attempt(attempt_id, question_id, marks_scored, seconds, error_type) VALUES (?,?,?,?,?)");
    q.bind(1, attemptId).bind(2, questionId); if (marksScored < 0) q.bindNull(3); else q.bind(3, marksScored); q.bind(4, seconds).bind(5, errorType); q.run();
    emit changed();
}

void PapersService::finishAttempt(qint64 attemptId)
{
    Database::Query q(m_db, "UPDATE paper_attempt SET finished=? WHERE id=?"); q.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, attemptId); q.run();
    if (m_activeAttempt == attemptId) { m_activeAttempt = 0; emit attemptChanged(); }
    emit changed();
}

QVariantList PapersService::attempts(qint64 paperId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT a.id, a.started, a.finished, a.timer_mode, COALESCE(SUM(x.marks_scored),0), (SELECT COALESCE(SUM(marks_available),0) FROM paper_question WHERE paper_id=a.paper_id)"
                            " FROM paper_attempt a LEFT JOIN paper_question_attempt x ON x.attempt_id=a.id WHERE a.paper_id=? GROUP BY a.id ORDER BY a.started DESC");
    q.bind(1, paperId);
    while (q.step()) out.append(QVariantMap{{"id", q.i64(0)}, {"started", q.i64(1)}, {"finished", q.isNull(2) ? 0 : q.i64(2)}, {"timerMode", q.i32(3) != 0}, {"scored", q.i32(4)}, {"available", q.i32(5)}});
    return out;
}

QVariantList PapersService::attemptResults(qint64 attemptId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT q.id, q.label, q.marks_available, q.topic_tags, x.marks_scored, x.seconds, x.error_type FROM paper_question q"
                            " JOIN paper_attempt a ON a.paper_id=q.paper_id LEFT JOIN paper_question_attempt x ON x.question_id=q.id AND x.attempt_id=a.id WHERE a.id=? ORDER BY q.sort");
    q.bind(1, attemptId);
    while (q.step()) out.append(QVariantMap{{"questionId", q.i64(0)}, {"label", q.text(1)}, {"marks", q.i32(2)}, {"topics", q.text(3)}, {"scored", q.isNull(4) ? -1 : q.i32(4)}, {"seconds", q.i32(5)}, {"errorType", q.text(6)}});
    return out;
}

QVariantMap PapersService::attemptSummary(qint64 attemptId) const
{
    int scored = 0, available = 0, answered = 0, seconds = 0;
    for (const QVariant &v : attemptResults(attemptId)) { const QVariantMap m = v.toMap(); available += m.value("marks").toInt(); if (m.value("scored").toInt() >= 0) { scored += m.value("scored").toInt(); ++answered; } seconds += m.value("seconds").toInt(); }
    return QVariantMap{{"scored", scored}, {"available", available}, {"answered", answered}, {"seconds", seconds}, {"percent", available ? 100.0 * scored / available : 0.0}};
}

QVariantList PapersService::weakTopics(const QString &subject, int days) const
{
    const qint64 since = QDateTime::currentSecsSinceEpoch() - qint64(days) * 86400;
    QHash<QString, QPair<int, int>> totals;   // topic → (scored, available)
    QHash<QString, int> counts;
    Database::Query q(m_db, "SELECT q.topic_tags, x.marks_scored, q.marks_available FROM paper_question_attempt x JOIN paper_question q ON q.id=x.question_id"
                            " JOIN paper_attempt a ON a.id=x.attempt_id JOIN paper p ON p.id=a.paper_id WHERE (?='' OR p.subject=?) AND a.started>=? AND x.marks_scored IS NOT NULL AND q.marks_available>0");
    q.bind(1, subject).bind(2, subject).bind(3, since);
    while (q.step()) {
        QStringList topics = q.text(0).split(QRegularExpression("[,;]"), Qt::SkipEmptyParts);
        if (topics.isEmpty()) topics << "(untagged)";
        for (QString t : topics) { t = t.trimmed(); totals[t].first += q.i32(1); totals[t].second += q.i32(2); counts[t]++; }
    }
    QVariantList out;
    for (auto it = totals.cbegin(); it != totals.cend(); ++it)
        out.append(QVariantMap{{"topic", it.key()}, {"scored", it.value().first}, {"available", it.value().second}, {"percent", it.value().second ? 100.0 * it.value().first / it.value().second : 0.0}, {"questions", counts.value(it.key())}});
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) { return a.toMap().value("percent").toDouble() < b.toMap().value("percent").toDouble(); });
    return out;
}

QVariantList PapersService::errorBreakdown(const QString &subject, int days) const
{
    const qint64 since = QDateTime::currentSecsSinceEpoch() - qint64(days) * 86400;
    QVariantList out;
    Database::Query q(m_db, "SELECT COALESCE(NULLIF(x.error_type,''),'(none)'), COUNT(*), COALESCE(SUM(q.marks_available - COALESCE(x.marks_scored,0)),0) FROM paper_question_attempt x JOIN paper_question q ON q.id=x.question_id"
                            " JOIN paper_attempt a ON a.id=x.attempt_id JOIN paper p ON p.id=a.paper_id WHERE (?='' OR p.subject=?) AND a.started>=? GROUP BY 1 ORDER BY 3 DESC");
    q.bind(1, subject).bind(2, subject).bind(3, since);
    while (q.step()) out.append(QVariantMap{{"errorType", q.text(0)}, {"questions", q.i32(1)}, {"marksLost", q.i32(2)}});
    return out;
}

QVariantList PapersService::trend(const QString &subject, int days) const
{
    const qint64 since = QDateTime::currentSecsSinceEpoch() - qint64(days) * 86400;
    QVariantList out;
    Database::Query q(m_db, "SELECT a.id, a.started, p.name, p.year, COALESCE(SUM(x.marks_scored),0), (SELECT COALESCE(SUM(marks_available),0) FROM paper_question WHERE paper_id=p.id)"
                            " FROM paper_attempt a JOIN paper p ON p.id=a.paper_id LEFT JOIN paper_question_attempt x ON x.attempt_id=a.id WHERE (?='' OR p.subject=?) AND a.started>=? AND a.finished IS NOT NULL GROUP BY a.id ORDER BY a.started");
    q.bind(1, subject).bind(2, subject).bind(3, since);
    while (q.step()) { const int av = q.i32(5); out.append(QVariantMap{{"attemptId", q.i64(0)}, {"started", q.i64(1)}, {"paper", QStringLiteral("%1 %2").arg(q.i32(3)).arg(q.text(2))}, {"scored", q.i32(4)}, {"available", av}, {"percent", av ? 100.0 * q.i32(4) / av : 0.0}}); }
    return out;
}

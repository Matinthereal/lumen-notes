#include "papersservice.h"
#include "storage/paths.h"
#include "pdf/pdfservice.h"
#include "storage/attachments.h"
#include "storage/database.h"
#include "storage/library.h"
#include "workers/workersupervisor.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

PapersService::PapersService(Database &db, Library &lib, PdfService &pdf, WorkerSupervisor *pdfWorker, QObject *parent)
    : QObject(parent), m_db(db), m_lib(lib), m_pdf(pdf), m_worker(pdfWorker)
{
    connect(pdfWorker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &r, const QJsonObject &e) { const Callback cb = m_pending.take(id); if (cb) cb(r, e); });
}

void PapersService::call(const QString &method, const QJsonObject &params, Callback cb) { m_pending.insert(m_worker->request(method, params), std::move(cb)); }

void PapersService::importPair(const QUrl &paperPdf, const QUrl &schemePdf, const QString &subject, int year, const QString &name, const QString &board, int minutes)
{
    QString err;
    const QString paperSha = attachments::store(m_db, paths::openable(paperPdf), "application/pdf", &err);
    if (paperSha.isEmpty()) { emit failed(err); return; }
    QString schemeSha;
    if (schemePdf.isValid() && !schemePdf.isEmpty()) schemeSha = attachments::store(m_db, paths::openable(schemePdf), "application/pdf", &err);
    // The paper's pages live in a "Past papers" section of the subject's notebook.
    qint64 notebookId = 0;
    for (const QVariant &n : m_lib.notebooks()) if (n.toMap().value("name").toString() == subject) notebookId = n.toMap().value("id").toLongLong();
    if (!notebookId) notebookId = m_lib.createNotebook(subject, "#5C6B7A", board);
    const QString title = name.isEmpty() ? QFileInfo(paths::openable(paperPdf)).completeBaseName() : name;
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
        // Its own token: the window opens what an import brings in, but never the mark scheme of
        // a paper you are about to sit (Main.qml).
        if (schemePdf.isValid() && !schemePdf.isEmpty()) m_pdf.importAsSection(schemePdf, notebookId, title + " — mark scheme", QStringLiteral("scheme-%1").arg(paperId));
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

// The words of every page, then paperdetect reads the questions off them (paperdetect.h).
void PapersService::detectQuestions(qint64 paperId)
{
    QString sha; { Database::Query q(m_db, "SELECT paper_attachment FROM paper WHERE id=?"); q.bind(1, paperId); if (q.step()) sha = q.text(0); }
    if (sha.isEmpty()) return;
    const QString path = attachments::pathFor(sha, "application/pdf");
    call("info", {{"path", path}}, [this, paperId, path](const QJsonObject &r, const QJsonObject &e) {
        if (!e.isEmpty()) { emit failed(e.value("message").toString()); return; }
        const int count = r.value("pages").toInt();
        const QJsonArray sizes = r.value("sizes").toArray();
        if (count <= 0) { saveDetected(paperId, {}); return; }
        auto pages = std::make_shared<QList<paperdetect::Page>>(count);
        auto remaining = std::make_shared<int>(count);
        for (int i = 0; i < count; ++i) {
            const QSizeF size(sizes.at(i).toArray().at(0).toDouble(794), sizes.at(i).toArray().at(1).toDouble(1123));
            call("words", {{"path", path}, {"index", i}}, [this, paperId, i, size, pages, remaining](const QJsonObject &r, const QJsonObject &) {
                (*pages)[i] = paperdetect::fromWorker(r.value("words").toArray(), size);
                if (--*remaining == 0) saveDetected(paperId, paperdetect::detect(*pages));
            });
        }
    });
}

void PapersService::saveDetected(qint64 paperId, const QList<paperdetect::Question> &found)
{
    m_db.begin();
    // Detecting again replaces what nobody has touched (no marks, no topics: all an earlier
    // detector left behind) and never duplicates a question that is already there.
    Database::Query d(m_db, "DELETE FROM paper_question WHERE paper_id=? AND marks_available=0 AND topic_tags=''"); d.bind(1, paperId); d.run();
    QSet<QString> have;
    { Database::Query q(m_db, "SELECT label FROM paper_question WHERE paper_id=?"); q.bind(1, paperId); while (q.step()) have.insert(q.text(0)); }
    for (const paperdetect::Question &f : found)
        if (!have.contains(f.label)) addQuestion(paperId, f.label, f.page, f.rect, f.marks);
    // Kept and new questions in the order they are printed.
    Database::Query o(m_db, "UPDATE paper_question SET sort=(SELECT COUNT(*) FROM paper_question x WHERE x.paper_id=paper_question.paper_id"
                            " AND (x.page_index<paper_question.page_index OR (x.page_index=paper_question.page_index AND (x.y<paper_question.y OR (x.y=paper_question.y AND x.id<=paper_question.id)))))"
                            " WHERE paper_id=?");
    o.bind(1, paperId); o.run();
    Database::Query t(m_db, "UPDATE paper SET total_marks=(SELECT COALESCE(SUM(marks_available),0) FROM paper_question WHERE paper_id=?) WHERE id=?"); t.bind(1, paperId).bind(2, paperId); t.run();
    m_db.commit();
    emit questionsDetected(paperId, int(found.size()));
    emit changed();
}

int PapersService::yearFromName(const QUrl &pdf) const
{
    return paperdetect::yearFromName(pdf.isLocalFile() ? QFileInfo(pdf.toLocalFile()).fileName() : pdf.fileName());
}

void PapersService::readCover(const QUrl &pdf)
{
    // The helper runs in a directory of its own: hand it a path that does not depend on ours.
    const QString path = pdf.isLocalFile() ? QFileInfo(pdf.toLocalFile()).absoluteFilePath() : paths::openable(pdf);
    call("text", {{"path", path}, {"index", 0}}, [this, pdf](const QJsonObject &r, const QJsonObject &e) {
        emit coverRead(pdf, e.isEmpty() ? paperdetect::yearFromCover(r.value("text").toString()) : 0);
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

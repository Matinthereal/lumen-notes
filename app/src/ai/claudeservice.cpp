#include "claudeservice.h"
#include "audio/audioservice.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/paths.h"
#include "text/textblocks.h"
#include "workers/workersupervisor.h"
#include <QDateTime>
#include <QTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

ClaudeService::ClaudeService(Database &db, Library &lib, TextBlocks &blocks, AudioService &audio, WorkerSupervisor *worker, const QString &promptsDir, QObject *parent)
    : QObject(parent), m_db(db), m_lib(lib), m_blocks(blocks), m_audio(audio), m_worker(worker), m_promptsDir(promptsDir)
{
    connect(worker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &r, const QJsonObject &e) {
        const Callback cb = m_pending.take(id);
        if (cb) cb(r, e);
    });
    connect(worker, &WorkerSupervisor::stateChanged, this, [this] { if (m_worker->state() == WorkerSupervisor::State::Ready) refreshStatus(); });
    m_online = lib.setting("claude.online", "0") == "1";
    QTimer::singleShot(8000, this, &ClaudeService::refreshStatus);   // first status check starts the worker on demand
}

void ClaudeService::call(const QString &method, const QJsonObject &params, Callback cb) { m_pending.insert(m_worker->request(method, params), std::move(cb)); }
void ClaudeService::setBusy(bool b) { if (m_busy == b) return; m_busy = b; emit busyChanged(); }
void ClaudeService::setOnline(bool v) { m_online = v; m_lib.setSetting("claude.online", v ? "1" : "0"); emit statusChanged(); }

void ClaudeService::refreshStatus()
{
    call("status", {}, [this](const QJsonObject &r, const QJsonObject &e) {
        m_available = e.isEmpty() && r.value("available").toBool();
        m_version = r.value("version").toString();
        m_reason = m_available ? QString() : (e.isEmpty() ? r.value("reason").toString() : e.value("message").toString());
        emit statusChanged();
    });
}

QString ClaudeService::promptTemplate(const QString &name) const
{
    QFile f(m_promptsDir + "/" + name + ".md");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QStringLiteral("(missing prompt template %1)").arg(name);
    return QString::fromUtf8(f.readAll());
}

QString ClaudeService::fill(const QString &tmpl, const QVariantMap &vars) const
{
    QString out = tmpl;
    for (auto it = vars.cbegin(); it != vars.cend(); ++it) out.replace("{{" + it.key() + "}}", it.value().toString());
    return out;
}

QString ClaudeService::subjectFor(qint64 pageId) const
{
    const QVariantMap p = m_lib.page(pageId);
    if (p.isEmpty()) return QStringLiteral("general");
    Database::Query q(m_db, "SELECT board FROM notebook WHERE id=?"); q.bind(1, p.value("notebookId").toLongLong());
    const QString board = q.step() ? q.text(0) : QString();
    return board.isEmpty() ? p.value("notebookName").toString() : p.value("notebookName").toString() + " (" + board + ")";
}

void ClaudeService::prepareLectureNotes(qint64 recordingId, qint64 pageId)
{
    QStringList transcript;
    for (const QVariant &s : m_audio.segments(recordingId)) transcript << s.toMap().value("text").toString();
    if (transcript.isEmpty()) { emit failed("lecture-notes", "This recording has no transcript yet."); return; }
    const QVariantMap page = m_lib.page(pageId);
    QStringList slides;
    {   // slide text of every PDF page in the same section
        Database::Query q(m_db, "SELECT s.text FROM search s JOIN page p ON p.id=s.page_id WHERE s.kind='pdf' AND p.section_id=? ORDER BY p.sort LIMIT 60");
        q.bind(1, page.value("sectionId").toLongLong());
        while (q.step()) slides << q.text(0);
    }
    QStringList notes;
    notes << m_blocks.pageText(pageId);
    {
        Database::Query q(m_db, "SELECT text FROM ocr_result WHERE page_id=? ORDER BY y, x"); q.bind(1, pageId);
        while (q.step()) notes << q.text(0);
    }
    const QVariantMap info = m_audio.recordingInfo(recordingId);
    const QVariantMap vars{{"subject", subjectFor(pageId)}, {"title", info.value("title").toString()},
                           {"date", QDateTime::fromSecsSinceEpoch(info.value("startedAt").toLongLong()).toString("d MMMM yyyy")},
                           {"transcript", transcript.join('\n')}, {"slides", slides.isEmpty() ? "(none)" : slides.join("\n\n---\n\n").left(60000)},
                           {"notes", notes.join('\n').trimmed().isEmpty() ? "(none)" : notes.join('\n').left(20000)}};
    emit promptReady("lecture-notes", fill(promptTemplate("lecture-notes"), vars), QVariantMap{{"pageId", pageId}, {"recordingId", recordingId}, {"title", info.value("title")}});
}

void ClaudeService::prepareFlashcards(qint64 pageId, qint64 recordingId)
{
    QStringList material;
    material << m_blocks.pageText(pageId);
    { Database::Query q(m_db, "SELECT text FROM ocr_result WHERE page_id=? ORDER BY y, x"); q.bind(1, pageId); while (q.step()) material << q.text(0); }
    { Database::Query q(m_db, "SELECT text FROM search WHERE kind='pdf' AND page_id=?"); q.bind(1, pageId); while (q.step()) material << q.text(0); }
    if (recordingId) for (const QVariant &s : m_audio.segments(recordingId)) material << s.toMap().value("text").toString();
    const QString m = material.join('\n').trimmed();
    if (m.isEmpty()) { emit failed("flashcards", "Nothing on this page to make cards from yet."); return; }
    emit promptReady("flashcards", fill(promptTemplate("flashcards"), {{"subject", subjectFor(pageId)}, {"material", m.left(40000)}}), QVariantMap{{"pageId", pageId}});
}

void ClaudeService::prepareExplain(const QString &selection, qint64 pageId, const QString &imagePath)
{
    QString sel = selection.trimmed();
    if (!imagePath.isEmpty()) sel = QStringLiteral("Read the image at %1 (a photo of handwritten notes) and use it as the selection.\n\n%2").arg(imagePath, sel);
    if (sel.isEmpty()) { emit failed("explain", "Select some ink or text first."); return; }
    emit promptReady("explain", fill(promptTemplate("explain"), {{"subject", subjectFor(pageId)}, {"selection", sel}}), QVariantMap{{"pageId", pageId}, {"allowRead", !imagePath.isEmpty()}});
}

void ClaudeService::prepareAsk(const QString &question)
{
    QStringList ctx;
    QVariantList hits = m_lib.search(question, 12);
    for (const QVariant &h : hits) {
        const QVariantMap m = h.toMap();
        Database::Query q(m_db, "SELECT text FROM search WHERE kind=? AND page_id=? AND ref_id=?");
        q.bind(1, m.value("kind").toString()).bind(2, m.value("pageId").toLongLong()).bind(3, m.value("refId").toLongLong());
        const QString full = q.step() ? q.text(0) : m.value("snippet").toString();
        ctx << QStringLiteral("[page %1] (%2 › %3, %4)\n%5").arg(m.value("pageId").toLongLong()).arg(m.value("notebookName").toString(), m.value("sectionName").toString(), m.value("kind").toString(), full.left(1500));
    }
    emit promptReady("ask", fill(promptTemplate("ask"), {{"question", question}, {"context", ctx.isEmpty() ? "(no matching notes)" : ctx.join("\n\n")}}), QVariantMap{{"question", question}});
}

void ClaudeService::improveLatex(const QString &imagePath, const QString &draft)
{
    if (imagePath.isEmpty()) { emit failed("latex-improve", "no image of the selection"); return; }
    send("latex-improve", fill(promptTemplate("latex-improve"), {{"image", imagePath}, {"draft", draft}}), QVariantMap{{"allowRead", true}});
}

void ClaudeService::send(const QString &feature, const QString &prompt, const QVariantMap &meta)
{
    if (!m_available) { emit failed(feature, m_reason.isEmpty() ? "Claude is not available" : m_reason); return; }
    setBusy(true);
    const bool allowRead = meta.value("allowRead").toBool();
    call("run", {{"prompt", prompt}, {"feature", feature}, {"allow_web", m_online}, {"allow_read", allowRead}, {"log_dir", paths::claudeLogDir()}},
         [this, feature, meta, prompt](const QJsonObject &r, const QJsonObject &e) {
             setBusy(false);
             const bool ok = e.isEmpty() && r.value("ok").toBool();
             Database::Query q(m_db, "INSERT INTO claude_log(at, feature, prompt_chars, response_chars, ok, path) VALUES (?,?,?,?,?,?)");
             q.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, feature).bind(3, prompt.size()).bind(4, r.value("text").toString().size()).bind(5, ok ? 1 : 0).bind(6, r.value("log").toString());
             q.run();
             if (!ok) { emit failed(feature, e.isEmpty() ? r.value("error").toString() : e.value("message").toString()); return; }
             const QString text = r.value("text").toString();
             if (feature == "lecture-notes") {
                 const qint64 pageId = meta.value("pageId").toLongLong();
                 const QVariantMap page = m_lib.page(pageId);
                 const qint64 newPage = m_lib.createPage(page.value("sectionId").toLongLong(), "plain", "a4");
                 m_lib.rename("page", newPage, "AI Notes — " + meta.value("title").toString());
                 const qint64 block = m_blocks.create(newPage, 48, 48, 700, 0, 0);
                 m_blocks.setMarkdown(block, text, 0);
                 emit notesCreated(newPage);
             } else if (feature == "flashcards") {
                 // Extract the JSON array even if prose slipped around it.
                 const int a = text.indexOf('['), b = text.lastIndexOf(']');
                 QVariantList cards;
                 if (a >= 0 && b > a) cards = QJsonDocument::fromJson(text.mid(a, b - a + 1).toUtf8()).array().toVariantList();
                 if (cards.isEmpty()) { emit failed(feature, "Claude did not return a card list; the reply is in the log."); return; }
                 emit cardsProposed(cards, meta);
             }
             emit responded(feature, text, meta);
         });
}

QVariantList ClaudeService::recentLog(int limit) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT at, feature, prompt_chars, response_chars, ok, path FROM claude_log ORDER BY id DESC LIMIT ?");
    q.bind(1, limit);
    while (q.step()) out.append(QVariantMap{{"at", q.i64(0)}, {"feature", q.text(1)}, {"promptChars", q.i32(2)}, {"responseChars", q.i32(3)}, {"ok", q.i32(4) != 0}, {"path", q.text(5)}});
    return out;
}

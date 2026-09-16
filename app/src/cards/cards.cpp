#include "cards.h"
#include "storage/database.h"
#include "storage/library.h"
#include <QDateTime>
#include <QJsonArray>
#include "fsrs.h"
#include "workers/workersupervisor.h"

Cards::Cards(Database &db, Library &lib, WorkerSupervisor *worker, QObject *parent) : QObject(parent), m_db(db), m_lib(lib), m_worker(worker)
{
    if (worker)
        connect(worker, &WorkerSupervisor::response, this, [this](int id, const QJsonObject &r, const QJsonObject &e) { const auto cb = m_pending.take(id); if (cb) cb(r, e); });
}

static qint64 dayStart(qint64 t) { return QDateTime::fromSecsSinceEpoch(t).date().startOfDay().toSecsSinceEpoch(); }

int Cards::dueCount() const
{
    Database::Query q(m_db, "SELECT COUNT(*) FROM card c JOIN card_state s ON s.card_id=c.id WHERE c.deleted_at IS NULL AND s.due<=?");
    q.bind(1, QDateTime::currentSecsSinceEpoch()); return q.step() ? q.i32(0) : 0;
}

int Cards::reviewedToday() const
{
    Database::Query q(m_db, "SELECT COUNT(*) FROM review_log WHERE reviewed>=?"); q.bind(1, dayStart(QDateTime::currentSecsSinceEpoch())); return q.step() ? q.i32(0) : 0;
}

QVariantList Cards::due(int limit) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT c.id, c.kind, c.front, c.back, t.name, s.state, s.due, s.stability, s.difficulty, s.reps FROM card c JOIN card_state s ON s.card_id=c.id"
                            " LEFT JOIN tag t ON t.id=c.tag_id WHERE c.deleted_at IS NULL AND s.due<=? ORDER BY s.state DESC, s.due LIMIT ?");
    q.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, limit);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"kind", q.text(1)}, {"front", q.text(2)}, {"back", q.text(3)}, {"tag", q.text(4)}, {"state", q.i32(5)}, {"due", q.i64(6)}, {"stability", q.f64(7)}, {"difficulty", q.f64(8)}, {"reps", q.i32(9)}});
    return out;
}

static fsrs::CardState loadState(Database &db, qint64 cardId, qint64 *lastReview)
{
    fsrs::CardState st;
    Database::Query q(db, "SELECT stability, difficulty, state, reps, lapses, last_review FROM card_state WHERE card_id=?"); q.bind(1, cardId);
    if (q.step()) { st.stability = q.f64(0); st.difficulty = q.f64(1); st.state = fsrs::State(q.i32(2)); st.reps = q.i32(3); st.lapses = q.i32(4); if (lastReview) *lastReview = q.isNull(5) ? 0 : q.i64(5); }
    return st;
}

QVariantMap Cards::preview(qint64 cardId) const
{
    qint64 last = 0;
    const fsrs::CardState st = loadState(m_db, cardId, &last);
    const double elapsed = last ? (QDateTime::currentSecsSinceEpoch() - last) / 86400.0 : 0;
    QVariantMap out;
    const char *names[] = {"again", "hard", "good", "easy"};
    for (int g = 1; g <= 4; ++g) {
        const fsrs::Outcome o = fsrs::review(fsrs::Params{}, st, g, elapsed);
        out.insert(names[g - 1], o.intervalDays == 0 ? QStringLiteral("10 min") : (o.intervalDays < 30 ? QStringLiteral("%1 d").arg(int(o.intervalDays)) : QStringLiteral("%1 mo").arg(o.intervalDays / 30.0, 0, 'f', 1)));
    }
    return out;
}

void Cards::review(qint64 cardId, int rating)
{
    qint64 last = 0;
    const fsrs::CardState st = loadState(m_db, cardId, &last);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const double elapsed = last ? (now - last) / 86400.0 : 0;
    const fsrs::Outcome o = fsrs::review(fsrs::Params{}, st, rating, elapsed);
    const qint64 due = o.intervalDays == 0 ? now + 600 : now + qint64(o.intervalDays * 86400);
    m_db.begin();
    Database::Query u(m_db, "UPDATE card_state SET stability=?, difficulty=?, due=?, last_review=?, reps=?, lapses=?, state=? WHERE card_id=?");
    u.bind(1, o.next.stability).bind(2, o.next.difficulty).bind(3, due).bind(4, now).bind(5, o.next.reps).bind(6, o.next.lapses).bind(7, int(o.next.state)).bind(8, cardId); u.run();
    Database::Query l(m_db, "INSERT INTO review_log(card_id, reviewed, rating, stability, difficulty, elapsed_days, scheduled_days) VALUES (?,?,?,?,?,?,?)");
    l.bind(1, cardId).bind(2, now).bind(3, rating).bind(4, o.next.stability).bind(5, o.next.difficulty).bind(6, elapsed).bind(7, o.scheduledDays); l.run();
    m_db.commit();
    emit changed();
}

QVariantList Cards::all(const QString &filter, int limit) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT c.id, c.kind, c.front, c.back, t.name, s.due, s.reps, s.stability FROM card c JOIN card_state s ON s.card_id=c.id LEFT JOIN tag t ON t.id=c.tag_id"
                            " WHERE c.deleted_at IS NULL AND (?='' OR c.front LIKE '%'||?||'%' OR c.back LIKE '%'||?||'%' OR t.name LIKE '%'||?||'%') ORDER BY c.id DESC LIMIT ?");
    q.bind(1, filter).bind(2, filter).bind(3, filter).bind(4, filter).bind(5, limit);
    while (q.step()) out.append(QVariantMap{{"id", q.i64(0)}, {"kind", q.text(1)}, {"front", q.text(2)}, {"back", q.text(3)}, {"tag", q.text(4)}, {"due", q.i64(5)}, {"reps", q.i32(6)}, {"stability", q.f64(7)}});
    return out;
}

QVariantList Cards::stats(int days) const
{
    QVariantList out;
    const qint64 since = QDateTime::currentSecsSinceEpoch() - qint64(days) * 86400, now = QDateTime::currentSecsSinceEpoch();
    Database::Query q(m_db, "SELECT COALESCE(t.name, '(untagged)'), COUNT(DISTINCT c.id),"
                            " (SELECT COUNT(*) FROM review_log r JOIN card c2 ON c2.id=r.card_id WHERE COALESCE(c2.tag_id,0)=COALESCE(c.tag_id,0) AND r.reviewed>=?),"
                            " (SELECT COUNT(*) FROM review_log r JOIN card c2 ON c2.id=r.card_id WHERE COALESCE(c2.tag_id,0)=COALESCE(c.tag_id,0) AND r.reviewed>=? AND r.rating>=3),"
                            " SUM(CASE WHEN s.due<=? THEN 1 ELSE 0 END)"
                            " FROM card c JOIN card_state s ON s.card_id=c.id LEFT JOIN tag t ON t.id=c.tag_id WHERE c.deleted_at IS NULL GROUP BY COALESCE(c.tag_id,0) ORDER BY 2 DESC");
    q.bind(1, since).bind(2, since).bind(3, now);
    while (q.step()) {
        const int reviews = q.i32(2), good = q.i32(3);
        out.append(QVariantMap{{"tag", q.text(0)}, {"cards", q.i32(1)}, {"reviews", reviews}, {"accuracy", reviews ? double(good) / reviews : -1.0}, {"due", q.i32(4)}});
    }
    return out;
}

void Cards::exportAnki(const QUrl &dest)
{
    if (!m_worker) { emit failed("cards worker not available"); return; }
    QJsonArray arr;
    for (const QVariant &v : all(QString(), 100000)) {
        const QVariantMap c = v.toMap();
        arr.append(QJsonObject{{"id", c.value("id").toLongLong()}, {"kind", c.value("kind").toString()}, {"front", c.value("front").toString()}, {"back", c.value("back").toString()},
                               {"tags", c.value("tag").toString().isEmpty() ? QJsonArray{} : QJsonArray{c.value("tag").toString()}}});
    }
    const int id = m_worker->request("export_apkg", {{"cards", arr}, {"out", dest.toLocalFile()}, {"deck_name", "Lumen"}});
    m_pending.insert(id, [this](const QJsonObject &r, const QJsonObject &e) { if (!e.isEmpty()) emit failed(e.value("message").toString()); else emit exported(r.value("file").toString(), r.value("cards").toInt()); });
}

void Cards::importAnki(const QUrl &file, qint64 sourcePage)
{
    if (!m_worker) { emit failed("cards worker not available"); return; }
    const int id = m_worker->request("import_apkg", {{"path", file.toLocalFile()}});
    m_pending.insert(id, [this, sourcePage](const QJsonObject &r, const QJsonObject &e) {
        if (!e.isEmpty()) { emit failed(e.value("message").toString()); return; }
        emit imported(addMany(r.value("cards").toArray().toVariantList(), sourcePage));
    });
}


qint64 Cards::tagId(const QString &name)
{
    if (name.trimmed().isEmpty()) return 0;
    { Database::Query q(m_db, "SELECT id FROM tag WHERE name=? AND parent_id IS NULL"); q.bind(1, name.trimmed()); if (q.step()) return q.i64(0); }
    Database::Query q(m_db, "INSERT INTO tag(name) VALUES (?)"); q.bind(1, name.trimmed()); q.run();
    return m_db.lastInsertId();
}

qint64 Cards::add(const QString &kind, const QString &front, const QString &back, qint64 sourcePage, const QString &tag, bool reverse)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 t = tagId(tag);
    Database::Query q(m_db, "INSERT INTO card(kind, front, back, source_page, tag_id, created) VALUES (?,?,?,?,?,?)");
    q.bind(1, kind).bind(2, front).bind(3, back); if (sourcePage) q.bind(4, sourcePage); else q.bindNull(4); if (t) q.bind(5, t); else q.bindNull(5); q.bind(6, now);
    if (!q.run()) return 0;
    const qint64 id = m_db.lastInsertId();
    Database::Query s(m_db, "INSERT INTO card_state(card_id, due) VALUES (?,?)"); s.bind(1, id).bind(2, now); s.run();
    if (reverse && kind == "basic") {
        Database::Query r(m_db, "INSERT INTO card(kind, front, back, source_page, tag_id, created, sibling_id) VALUES ('basic',?,?,?,?,?,?)");
        r.bind(1, back).bind(2, front); if (sourcePage) r.bind(3, sourcePage); else r.bindNull(3); if (t) r.bind(4, t); else r.bindNull(4); r.bind(5, now).bind(6, id); r.run();
        const qint64 rid = m_db.lastInsertId();
        Database::Query s2(m_db, "INSERT INTO card_state(card_id, due) VALUES (?,?)"); s2.bind(1, rid).bind(2, now); s2.run();
        Database::Query u(m_db, "UPDATE card SET sibling_id=? WHERE id=?"); u.bind(1, rid).bind(2, id); u.run();
    }
    emit changed();
    return id;
}

int Cards::addMany(const QVariantList &cards, qint64 sourcePage)
{
    int n = 0;
    m_db.begin();
    for (const QVariant &v : cards) {
        const QVariantMap c = v.toMap();
        const QString kind = c.value("kind", "basic").toString() == "cloze" ? "cloze" : "basic";
        const QString front = c.value("front").toString().trimmed();
        if (front.isEmpty()) continue;
        const QVariantList tags = c.value("tags").toList();
        if (add(kind, front, c.value("back").toString(), sourcePage, tags.isEmpty() ? QString() : tags.first().toString(), c.value("reverse").toBool())) ++n;
    }
    m_db.commit();
    emit changed();
    return n;
}

void Cards::remove(qint64 id)
{
    Database::Query q(m_db, "UPDATE card SET deleted_at=? WHERE id=? OR sibling_id=?"); q.bind(1, QDateTime::currentSecsSinceEpoch()).bind(2, id).bind(3, id); q.run();
    emit changed();
}

void Cards::restore(qint64 id)
{
    Database::Query q(m_db, "UPDATE card SET deleted_at=NULL WHERE id=? OR sibling_id=?"); q.bind(1, id).bind(2, id); q.run();
    emit changed();
}

QVariantMap Cards::card(qint64 id) const
{
    Database::Query q(m_db, "SELECT c.id, c.kind, c.front, c.back, c.source_page, t.name FROM card c LEFT JOIN tag t ON t.id=c.tag_id WHERE c.id=?");
    q.bind(1, id);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"kind", q.text(1)}, {"front", q.text(2)}, {"back", q.text(3)}, {"sourcePage", q.i64(4)}, {"tag", q.text(5)}};
}

int Cards::total() const { Database::Query q(m_db, "SELECT COUNT(*) FROM card WHERE deleted_at IS NULL"); return q.step() ? q.i32(0) : 0; }

#include "library.h"
#include "database.h"
#include <QDateTime>
#include <QFile>
#include <algorithm>
#include "paths.h"
#include <QRegularExpression>

Library::Library(Database &db, QObject *parent) : QObject(parent), m_db(db) {}

qint64 Library::now() const { return QDateTime::currentSecsSinceEpoch(); }

QVariantList Library::notebooks() const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT n.id, n.name, n.colour, n.board, (SELECT COUNT(*) FROM section s WHERE s.notebook_id=n.id AND s.deleted_at IS NULL)"
                            " FROM notebook n WHERE n.deleted_at IS NULL ORDER BY n.sort, n.id");
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"name", q.text(1)}, {"colour", q.text(2)}, {"board", q.text(3)}, {"sectionCount", q.i32(4)}});
    return out;
}

QVariantList Library::sections(qint64 notebookId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT s.id, s.name, s.kind, (SELECT COUNT(*) FROM page p WHERE p.section_id=s.id AND p.deleted_at IS NULL)"
                            " FROM section s WHERE s.notebook_id=? AND s.deleted_at IS NULL ORDER BY s.sort, s.id");
    q.bind(1, notebookId);
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"name", q.text(1)}, {"kind", q.text(2)}, {"pageCount", q.i32(3)}, {"notebookId", notebookId}});
    return out;
}

QVariantList Library::pages(qint64 sectionId) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT id, title, style, size_mode, modified FROM page WHERE section_id=? AND deleted_at IS NULL ORDER BY sort, id");
    q.bind(1, sectionId);
    int i = 0;
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"title", q.text(1)}, {"style", q.text(2)}, {"sizeMode", q.text(3)}, {"modified", q.i64(4)}, {"index", i++}, {"sectionId", sectionId}});
    return out;
}

QVariantList Library::allSections() const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT s.id, s.name, n.name, n.colour, n.id FROM section s JOIN notebook n ON n.id=s.notebook_id"
                            " WHERE s.deleted_at IS NULL AND n.deleted_at IS NULL ORDER BY n.sort, n.id, s.sort, s.id");
    while (q.step())
        out.append(QVariantMap{{"id", q.i64(0)}, {"name", q.text(1)}, {"notebookName", q.text(2)},
                               {"colour", q.text(3)}, {"notebookId", q.i64(4)}});
    return out;
}

QVariantMap Library::page(qint64 pageId) const
{
    Database::Query q(m_db, "SELECT p.id, p.title, p.style, p.size_mode, p.width, p.height, p.section_id, s.notebook_id, s.name, n.name, n.colour, p.paper, p.starred"
                            " FROM page p JOIN section s ON s.id=p.section_id JOIN notebook n ON n.id=s.notebook_id"
                            " WHERE p.id=? AND p.deleted_at IS NULL AND s.deleted_at IS NULL AND n.deleted_at IS NULL");
    q.bind(1, pageId);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"title", q.text(1)}, {"style", q.text(2)}, {"sizeMode", q.text(3)}, {"width", q.f64(4)}, {"height", q.f64(5)},
                       {"sectionId", q.i64(6)}, {"notebookId", q.i64(7)}, {"sectionName", q.text(8)}, {"notebookName", q.text(9)}, {"colour", q.text(10)},
                       {"paper", q.text(11)}, {"starred", q.i32(12) != 0}};
}

QVariantMap Library::section(qint64 sectionId) const
{
    Database::Query q(m_db, "SELECT id, notebook_id, name, kind FROM section WHERE id=?");
    q.bind(1, sectionId);
    if (!q.step()) return {};
    return QVariantMap{{"id", q.i64(0)}, {"notebookId", q.i64(1)}, {"name", q.text(2)}, {"kind", q.text(3)}};
}

qint64 Library::createNotebook(const QString &name, const QString &colour, const QString &board)
{
    Database::Query q(m_db, "INSERT INTO notebook(name, colour, board, sort, created) VALUES (?,?,?,(SELECT COALESCE(MAX(sort),0)+1 FROM notebook),?)");
    q.bind(1, name).bind(2, colour).bind(3, board).bind(4, now());
    if (!q.run()) return 0;
    emit changed();
    return m_db.lastInsertId();
}

qint64 Library::createSection(qint64 notebookId, const QString &name, const QString &kind)
{
    Database::Query q(m_db, "INSERT INTO section(notebook_id, name, kind, sort, created) VALUES (?,?,?,(SELECT COALESCE(MAX(sort),0)+1 FROM section WHERE notebook_id=?),?)");
    q.bind(1, notebookId).bind(2, name).bind(3, kind).bind(4, notebookId).bind(5, now());
    if (!q.run()) return 0;
    emit changed();
    return m_db.lastInsertId();
}

qint64 Library::createPage(qint64 sectionId, const QString &style, const QString &sizeMode, int afterIndex)
{
    const QString st = style.isEmpty() ? setting("page.style", "dotted") : style;
    const QString sm = sizeMode.isEmpty() ? setting("page.sizeMode", "typed") : sizeMode;
    qint64 sort = 0;
    if (afterIndex < 0) {
        Database::Query q(m_db, "SELECT COALESCE(MAX(sort),0)+1 FROM page WHERE section_id=?");
        q.bind(1, sectionId); if (q.step()) sort = q.i64(0);
    } else {
        // Make room: shift everything after the anchor by one.
        Database::Query q(m_db, "SELECT sort FROM page WHERE section_id=? AND deleted_at IS NULL ORDER BY sort, id LIMIT 1 OFFSET ?");
        q.bind(1, sectionId).bind(2, afterIndex);
        sort = q.step() ? q.i64(0) + 1 : 1;
        Database::Query u(m_db, "UPDATE page SET sort=sort+1 WHERE section_id=? AND sort>=?");
        u.bind(1, sectionId).bind(2, sort); u.run();
    }
    Database::Query q(m_db, "INSERT INTO page(section_id, title, sort, style, size_mode, created, modified) VALUES (?,?,?,?,?,?,?)");
    q.bind(1, sectionId).bind(2, QString()).bind(3, sort).bind(4, st).bind(5, sm).bind(6, now()).bind(7, now());
    if (!q.run()) return 0;
    const qint64 id = m_db.lastInsertId();
    QFile::remove(QStringLiteral("%1/%2.log").arg(paths::journalDir()).arg(id));   // a reused rowid must never inherit a stale journal
    emit changed();
    return id;
}

static QString tableFor(const QString &kind) { return kind == "notebook" ? "notebook" : kind == "section" ? "section" : "page"; }

void Library::rename(const QString &kind, qint64 id, const QString &name)
{
    const QString col = kind == "page" ? "title" : "name";
    QString clean = name; clean.remove(QChar(0x200b));
    Database::Query q(m_db, QStringLiteral("UPDATE %1 SET %2=? WHERE id=?").arg(tableFor(kind), col));
    q.bind(1, clean).bind(2, id); q.run();
    emit changed();
}
void Library::tidyAutomaticTitles()
{
    // Titles the app chose for you (marked with a zero-width space) that have almost no letters are
    // recognition noise — "0 000 000 000…". A page with no title reads better than that.
    QVector<qint64> noisy;
    {
        Database::Query q(m_db, "SELECT id, title FROM page WHERE title LIKE '\u200b%'");
        while (q.step()) {
            const QString title = q.text(1);
            int letters = 0;
            for (const QChar &c : title) if (c.isLetter()) ++letters;
            if (letters < 3) noisy.append(q.i64(0));
        }
    }
    for (qint64 id : noisy) {
        Database::Query q(m_db, "UPDATE page SET title='' WHERE id=?");
        q.bind(1, id);
        q.run();
    }
    if (!noisy.isEmpty()) emit changed();
}

void Library::suggestTitle(qint64 pageId, const QString &text)
{
    // First non-empty line, Markdown heading marks stripped, capped; never overrides a manual title.
    QString line;
    for (const QString &l : text.split('\n')) {
        QString t = l.trimmed();
        t.remove(QRegularExpression("^#+\\s*"));
        t.remove(QRegularExpression("[*_`$]"));
        t.replace(QRegularExpression("\\s+"), QStringLiteral(" "));
        if (t.size() < 3) continue;
        // Recognised handwriting can come back as "0 000 000 000 000…"; a title made of digits and
        // punctuation is worse than no title, so hold out for something with words in it.
        int letters = 0;
        for (const QChar &c : t) if (c.isLetter()) ++letters;
        if (letters < 3) continue;
        line = t.left(40).trimmed();
        break;
    }
    if (line.isEmpty()) return;
    Database::Query q(m_db, "UPDATE page SET title=? WHERE id=? AND (title='' OR title LIKE '\u200b%')");   // auto titles carry a zero-width mark
    q.bind(1, QStringLiteral("\u200b") + line).bind(2, pageId); q.run();
    if (m_db.changes()) emit changed();
}

QVariantList Library::recentPages(int limit) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT p.id, p.title, s.name, n.name, n.colour, COALESCE(NULLIF(setting.value,''),'0') FROM page p JOIN section s ON s.id=p.section_id JOIN notebook n ON n.id=s.notebook_id"
                            " LEFT JOIN setting ON setting.key='opened.'||p.id WHERE p.deleted_at IS NULL AND s.deleted_at IS NULL AND n.deleted_at IS NULL ORDER BY CAST(COALESCE(setting.value,'0') AS INTEGER) DESC, p.modified DESC LIMIT ?");
    q.bind(1, limit);
    while (q.step()) out.append(QVariantMap{{"id", q.i64(0)}, {"title", q.text(1)}, {"sectionName", q.text(2)}, {"notebookName", q.text(3)}, {"colour", q.text(4)}});
    return out;
}

QVariantList Library::starredPages(int limit) const
{
    QVariantList out;
    Database::Query q(m_db, "SELECT p.id, p.title, s.name, n.name, n.colour FROM page p JOIN section s ON s.id=p.section_id"
                            " JOIN notebook n ON n.id=s.notebook_id WHERE p.starred=1 AND p.deleted_at IS NULL AND s.deleted_at IS NULL"
                            " AND n.deleted_at IS NULL ORDER BY n.sort, s.sort, p.sort LIMIT ?");
    q.bind(1, limit);
    while (q.step()) out.append(QVariantMap{{"id", q.i64(0)}, {"title", q.text(1)}, {"sectionName", q.text(2)},
                                            {"notebookName", q.text(3)}, {"colour", q.text(4)}});
    return out;
}

void Library::setStarred(qint64 pageId, bool starred)
{
    Database::Query q(m_db, "UPDATE page SET starred=? WHERE id=?");
    q.bind(1, starred ? 1 : 0).bind(2, pageId);
    if (q.run()) emit changed();
}

bool Library::isStarred(qint64 pageId) const
{
    Database::Query q(m_db, "SELECT starred FROM page WHERE id=?");
    q.bind(1, pageId);
    return q.step() && q.i32(0) != 0;
}

void Library::touchPage(qint64 pageId) { setSetting(QStringLiteral("opened.%1").arg(pageId), QString::number(QDateTime::currentSecsSinceEpoch())); }

void Library::setNotebookColour(qint64 id, const QString &colour) { Database::Query q(m_db, "UPDATE notebook SET colour=? WHERE id=?"); q.bind(1, colour).bind(2, id); q.run(); emit changed(); }
void Library::setPageStyle(qint64 id, const QString &style) { Database::Query q(m_db, "UPDATE page SET style=? WHERE id=?"); q.bind(1, style).bind(2, id); q.run(); emit changed(); }

void Library::setPagePaper(qint64 id, const QString &colour)
{
    Database::Query q(m_db, "UPDATE page SET paper=?, modified=? WHERE id=?");
    q.bind(1, colour).bind(2, now()).bind(3, id);
    if (q.run()) emit changed();
}
void Library::setPageSizeMode(qint64 id, const QString &mode) { Database::Query q(m_db, "UPDATE page SET size_mode=? WHERE id=?"); q.bind(1, mode).bind(2, id); q.run(); emit changed(); }

void Library::setPageSize(qint64 id, double width, double height)
{
    Database::Query q(m_db, "UPDATE page SET width=?, height=? WHERE id=?"); q.bind(1, width).bind(2, height).bind(3, id); q.run(); emit changed();
}

void Library::setPagePdf(qint64 pageId, const QString &sha256, int index)
{
    Database::Query q(m_db, "INSERT OR REPLACE INTO pdf_page(page_id, attachment, page_index) VALUES (?,?,?)");
    q.bind(1, pageId).bind(2, sha256).bind(3, index); q.run();
}

void Library::indexText(const QString &kind, qint64 pageId, qint64 refId, const QString &text)
{
    unindex(kind, pageId, refId);
    if (text.trimmed().isEmpty()) return;
    Database::Query q(m_db, "INSERT INTO search(kind, page_id, ref_id, text) VALUES (?,?,?,?)");
    q.bind(1, kind).bind(2, pageId).bind(3, refId).bind(4, text); q.run();
}

void Library::unindex(const QString &kind, qint64 pageId, qint64 refId)
{
    if (refId < 0) { Database::Query q(m_db, "DELETE FROM search WHERE kind=? AND page_id=?"); q.bind(1, kind).bind(2, pageId); q.run(); }
    else { Database::Query q(m_db, "DELETE FROM search WHERE kind=? AND page_id=? AND ref_id=?"); q.bind(1, kind).bind(2, pageId).bind(3, refId); q.run(); }
}

QVariantList Library::search(const QString &query, int limit) const
{
    QVariantList out;
    // Each word becomes a prefix term; quotes keep FTS5 syntax characters harmless.
    QStringList terms;
    for (const QString &w : query.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
        QString t = w; t.remove('"');
        if (!t.isEmpty()) terms << QStringLiteral("\"%1\"*").arg(t);
    }
    if (terms.isEmpty()) return out;
    Database::Query q(m_db, "SELECT s.kind, s.page_id, s.ref_id, snippet(search, 3, '\u2039', '\u203a', '\u2026', 14), bm25(search),"
                            " p.title, sec.name, n.name FROM search s JOIN page p ON p.id=s.page_id JOIN section sec ON sec.id=p.section_id JOIN notebook n ON n.id=sec.notebook_id"
                            " WHERE search MATCH ? AND p.deleted_at IS NULL AND sec.deleted_at IS NULL AND n.deleted_at IS NULL"
                            " ORDER BY bm25(search) LIMIT ?");
    q.bind(1, terms.join(' ')).bind(2, limit);
    while (q.step())
        out.append(QVariantMap{{"kind", q.text(0)}, {"pageId", q.i64(1)}, {"refId", q.i64(2)}, {"snippet", q.text(3)}, {"score", q.f64(4)},
                               {"pageTitle", q.text(5)}, {"sectionName", q.text(6)}, {"notebookName", q.text(7)}});
    return out;
}

void Library::movePage(qint64 pageId, int newIndex)
{
    const QVariantMap p = page(pageId);
    if (p.isEmpty()) return;
    QVariantList list = pages(p.value("sectionId").toLongLong());
    QVector<qint64> ids;
    for (const QVariant &v : list) ids.append(v.toMap().value("id").toLongLong());
    const int from = ids.indexOf(pageId);
    if (from < 0) return;
    ids.remove(from);
    ids.insert(std::clamp(newIndex, 0, int(ids.size())), pageId);
    m_db.begin();
    Database::Query u(m_db, "UPDATE page SET sort=? WHERE id=?");
    for (int i = 0; i < ids.size(); ++i) { u.reset(); u.bind(1, i + 1).bind(2, ids[i]); u.run(); }
    m_db.commit();
    emit changed();
}

void Library::remove(const QString &kind, qint64 id)
{
    Database::Query q(m_db, QStringLiteral("UPDATE %1 SET deleted_at=? WHERE id=?").arg(tableFor(kind)));
    q.bind(1, now()).bind(2, id); q.run();
    emit changed();
}
void Library::restore(const QString &kind, qint64 id)
{
    Database::Query q(m_db, QStringLiteral("UPDATE %1 SET deleted_at=NULL WHERE id=?").arg(tableFor(kind)));
    q.bind(1, id); q.run();
    emit changed();
}
QVariantList Library::deletedItems(int limit) const
{
    QVariantList out;
    const auto add = [&out](const QString &kind, qint64 id, const QString &name, const QString &where, qint64 when) {
        out.append(QVariantMap{{"kind", kind}, {"id", id}, {"name", name}, {"where", where}, {"deletedAt", when}});
    };
    {   // pages, with the trail back to where they came from
        Database::Query q(m_db, "SELECT p.id, p.title, p.deleted_at, s.name, n.name FROM page p JOIN section s ON s.id=p.section_id"
                                " JOIN notebook n ON n.id=s.notebook_id WHERE p.deleted_at IS NOT NULL ORDER BY p.deleted_at DESC LIMIT ?");
        q.bind(1, limit);
        while (q.step()) add(QStringLiteral("page"), q.i64(0), q.text(1), q.text(4) + QStringLiteral(" › ") + q.text(3), q.i64(2));
    }
    {
        Database::Query q(m_db, "SELECT s.id, s.name, s.deleted_at, n.name, (SELECT COUNT(*) FROM page p WHERE p.section_id=s.id)"
                                " FROM section s JOIN notebook n ON n.id=s.notebook_id WHERE s.deleted_at IS NOT NULL ORDER BY s.deleted_at DESC LIMIT ?");
        q.bind(1, limit);
        while (q.step()) add(QStringLiteral("section"), q.i64(0), q.text(1), QStringLiteral("%1 · %2 pages").arg(q.text(3)).arg(q.i32(4)), q.i64(2));
    }
    {
        Database::Query q(m_db, "SELECT id, name, deleted_at FROM notebook WHERE deleted_at IS NOT NULL ORDER BY deleted_at DESC LIMIT ?");
        q.bind(1, limit);
        while (q.step()) add(QStringLiteral("notebook"), q.i64(0), q.text(1), QStringLiteral("notebook"), q.i64(2));
    }
    {
        Database::Query q(m_db, "SELECT id, front, deleted_at FROM card WHERE deleted_at IS NOT NULL ORDER BY deleted_at DESC LIMIT ?");
        q.bind(1, limit);
        while (q.step()) add(QStringLiteral("card"), q.i64(0), q.text(1), QStringLiteral("flashcard"), q.i64(2));
    }
    {
        Database::Query q(m_db, "SELECT id, name, deleted_at, subject, year FROM paper WHERE deleted_at IS NOT NULL ORDER BY deleted_at DESC LIMIT ?");
        q.bind(1, limit);
        while (q.step()) add(QStringLiteral("paper"), q.i64(0), q.text(1), QStringLiteral("%1 %2").arg(q.text(3)).arg(q.i32(4)), q.i64(2));
    }
    std::sort(out.begin(), out.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("deletedAt").toLongLong() > b.toMap().value("deletedAt").toLongLong();
    });
    if (out.size() > limit) out.resize(limit);
    return out;
}

void Library::purgeOne(const QString &kind, qint64 id)
{
    static const QHash<QString, QString> table{{QStringLiteral("page"), QStringLiteral("page")},
                                               {QStringLiteral("section"), QStringLiteral("section")},
                                               {QStringLiteral("notebook"), QStringLiteral("notebook")},
                                               {QStringLiteral("card"), QStringLiteral("card")},
                                               {QStringLiteral("paper"), QStringLiteral("paper")}};
    if (!table.contains(kind)) return;
    if (kind == QLatin1String("page")) QFile::remove(QStringLiteral("%1/%2.log").arg(paths::journalDir()).arg(id));
    Database::Query q(m_db, QStringLiteral("DELETE FROM %1 WHERE id=? AND deleted_at IS NOT NULL").arg(table.value(kind)));
    q.bind(1, id);
    if (q.run()) emit changed();
}

qint64 Library::duplicatePage(qint64 pageId)
{
    const QVariantMap src = page(pageId);
    if (src.isEmpty()) return 0;
    const qint64 sectionId = src.value(QStringLiteral("sectionId")).toLongLong();
    const QVariantList siblings = pages(sectionId);
    int index = -1;
    for (int i = 0; i < siblings.size(); ++i)
        if (siblings[i].toMap().value(QStringLiteral("id")).toLongLong() == pageId) { index = i; break; }
    const qint64 copy = createPage(sectionId, src.value(QStringLiteral("style")).toString(),
                                   src.value(QStringLiteral("sizeMode")).toString(), index);
    if (!copy) return 0;
    setPageSize(copy, src.value(QStringLiteral("width")).toDouble(), src.value(QStringLiteral("height")).toDouble());
    const QString title = src.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) rename(QStringLiteral("page"), copy, title + QStringLiteral(" (copy)"));
    for (const char *sql : {"INSERT INTO stroke_blob(page_id, schema, data, stroke_count) SELECT ?, schema, data, stroke_count FROM stroke_blob WHERE page_id=?",
                            "INSERT INTO text_block(page_id, x, y, w, markdown, created_t, recording_id, sort) SELECT ?, x, y, w, markdown, created_t, recording_id, sort FROM text_block WHERE page_id=?",
                            "INSERT INTO image(page_id, attachment, x, y, w, h) SELECT ?, attachment, x, y, w, h FROM image WHERE page_id=?",
                            "INSERT INTO pdf_page(page_id, attachment, page_index) SELECT ?, attachment, page_index FROM pdf_page WHERE page_id=?"}) {
        Database::Query q(m_db, sql);
        q.bind(1, copy).bind(2, pageId);
        q.run();
    }
    emit changed();
    return copy;
}

void Library::movePageToSection(qint64 pageId, qint64 sectionId)
{
    if (page(pageId).isEmpty()) return;
    int sort = 0;
    { Database::Query q(m_db, "SELECT COALESCE(MAX(sort), -1) + 1 FROM page WHERE section_id=? AND deleted_at IS NULL"); q.bind(1, sectionId); if (q.step()) sort = q.i32(0); }
    Database::Query q(m_db, "UPDATE page SET section_id=?, sort=? WHERE id=?");
    q.bind(1, sectionId).bind(2, sort).bind(3, pageId);
    if (q.run()) emit changed();
}

int Library::purgeDeleted(int olderThanDays)
{
    const qint64 cutoff = now() - qint64(olderThanDays) * 86400;
    int n = 0;
    {   // journals of pages about to vanish (including pages under purged sections/notebooks) go with them
        Database::Query q(m_db, "SELECT p.id FROM page p LEFT JOIN section s ON s.id=p.section_id LEFT JOIN notebook nb ON nb.id=s.notebook_id"
                                " WHERE (p.deleted_at IS NOT NULL AND p.deleted_at<=?) OR (s.deleted_at IS NOT NULL AND s.deleted_at<=?) OR (nb.deleted_at IS NOT NULL AND nb.deleted_at<=?)");
        q.bind(1, cutoff).bind(2, cutoff).bind(3, cutoff);
        while (q.step()) QFile::remove(QStringLiteral("%1/%2.log").arg(paths::journalDir()).arg(q.i64(0)));
    }
    for (const char *t : {"page", "section", "notebook"}) {
        Database::Query q(m_db, QStringLiteral("DELETE FROM %1 WHERE deleted_at IS NOT NULL AND deleted_at<=?").arg(t));
        q.bind(1, cutoff); q.run(); n += m_db.changes();
    }
    if (n) emit changed();
    return n;
}

qint64 Library::firstPageId() const
{
    Database::Query q(m_db, "SELECT p.id FROM page p JOIN section s ON s.id=p.section_id JOIN notebook n ON n.id=s.notebook_id"
                            " WHERE p.deleted_at IS NULL AND s.deleted_at IS NULL AND n.deleted_at IS NULL ORDER BY n.sort, n.id, s.sort, s.id, p.sort, p.id LIMIT 1");
    return q.step() ? q.i64(0) : 0;
}

qint64 Library::nextPageId(qint64 pageId, int delta) const
{
    const QVariantMap p = page(pageId);
    if (p.isEmpty()) return 0;
    const QVariantList list = pages(p.value("sectionId").toLongLong());
    for (int i = 0; i < list.size(); ++i)
        if (list[i].toMap().value("id").toLongLong() == pageId) {
            const int j = i + delta;
            return (j >= 0 && j < list.size()) ? list[j].toMap().value("id").toLongLong() : 0;
        }
    return 0;
}

QString Library::setting(const QString &key, const QString &fallback) const
{
    Database::Query q(m_db, "SELECT value FROM setting WHERE key=?");
    q.bind(1, key);
    return q.step() ? q.text(0) : fallback;
}
void Library::setSetting(const QString &key, const QString &value)
{
    Database::Query q(m_db, "INSERT OR REPLACE INTO setting(key, value) VALUES (?,?)");
    q.bind(1, key).bind(2, value); q.run();
}

bool Library::seedDefaults()
{
    {
        Database::Query q(m_db, "SELECT COUNT(*) FROM notebook");
        if (q.step() && q.i64(0) > 0) return false;
    }
    m_db.begin();
    const qint64 nb = createNotebook(QStringLiteral("My notebook"), QStringLiteral("#1F6FEB"), QString());
    const qint64 section = createSection(nb, QStringLiteral("Notes"));
    const qint64 page = createPage(section, QString(), QStringLiteral("typed"));
    rename(QStringLiteral("page"), page, QStringLiteral("Welcome"));
    Database::Query t(m_db, "INSERT INTO text_block(page_id, x, y, w, markdown, sort) VALUES (?, 0, 0, 794, ?, 0)");
    t.bind(1, page).bind(2, QStringLiteral(
        "# Welcome to Lumen\n\n"
        "This is a **typed page**. Click anywhere and start typing; it saves as you go.\n\n"
        "## The basics\n\n"
        "- **Ctrl+B**, **Ctrl+I**, **Ctrl+U** for bold, italic and underline\n"
        "- **Ctrl+Alt+1**, **2**, **3** for headings, **Ctrl+Alt+0** for normal text\n"
        "- **Ctrl+Shift+8** for a bulleted list, **Ctrl+Shift+7** for a numbered one\n"
        "- **Ctrl+N** for a new page, **Ctrl+K** to search everything\n\n"
        "## Things to try\n\n"
        "- [ ] Rename this page: hold it (or right-click it) in the sidebar\n"
        "- [ ] Make a handwritten page if you have a pen or touchscreen\n"
        "- [ ] Import a PDF and write on it\n"));
    t.run();
    const qint64 block = m_db.lastInsertId();
    Database::Query md(m_db, "SELECT markdown FROM text_block WHERE id=?"); md.bind(1, block);
    if (md.step()) indexText(QStringLiteral("text"), page, block, md.text(0));
    m_db.commit();
    emit changed();
    return true;
}

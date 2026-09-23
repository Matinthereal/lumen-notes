#include "notebookfile.h"

#include "attachments.h"
#include "database.h"
#include "paths.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

namespace notebookfile {
namespace {

// Column lists, written out rather than SELECT *: the live schema grows over time and an export
// must keep meaning the same thing in a file written by another version.
const char *kCols[][2] = {
    {"notebook", "id, name, colour, sort, board, created"},
    {"section", "id, notebook_id, name, sort, kind, created"},
    {"page", "id, section_id, title, sort, style, size_mode, width, height, created, modified, starred, paper"},
    {"stroke_blob", "page_id, schema, data, stroke_count"},
    {"text_block", "id, page_id, x, y, w, markdown, created_t, recording_id, sort"},
    {"image", "id, page_id, attachment, x, y, w, h"},
    {"pdf_page", "page_id, attachment, page_index"},
    {"shape", "id, page_id, kind, x, y, w, h, stroke, fill, width, sort"},
    {"tag", "id, name"},
    {"page_tag", "page_id, tag_id"},
    {"page_link", "block_id, src_page, dst_page"},
    {"attachment", "sha256, mime, bytes, name, created"},
};

QString columnsOf(const QString &table)
{
    for (const auto &c : kCols)
        if (table == QLatin1String(c[0])) return QString::fromLatin1(c[1]);
    return {};
}

// The file's own schema: plain tables, no foreign keys — it is a transport format, and the
// importer is what decides where the rows land.
bool makeTables(Database &db)
{
    static const char *kDdl[] = {
        "CREATE TABLE ex.lumen_export (format INTEGER NOT NULL, app TEXT, exported INTEGER NOT NULL, notebook TEXT, pages INTEGER)",
        "CREATE TABLE ex.notebook (id INTEGER PRIMARY KEY, name TEXT NOT NULL, colour TEXT, sort INTEGER, board TEXT, created INTEGER)",
        "CREATE TABLE ex.section (id INTEGER PRIMARY KEY, notebook_id INTEGER, name TEXT NOT NULL, sort INTEGER, kind TEXT, created INTEGER)",
        "CREATE TABLE ex.page (id INTEGER PRIMARY KEY, section_id INTEGER, title TEXT, sort INTEGER, style TEXT, size_mode TEXT,"
        "  width REAL, height REAL, created INTEGER, modified INTEGER, starred INTEGER, paper TEXT)",
        "CREATE TABLE ex.stroke_blob (page_id INTEGER PRIMARY KEY, schema INTEGER, data BLOB, stroke_count INTEGER)",
        "CREATE TABLE ex.text_block (id INTEGER PRIMARY KEY, page_id INTEGER, x REAL, y REAL, w REAL, markdown TEXT, created_t INTEGER, recording_id INTEGER, sort INTEGER)",
        "CREATE TABLE ex.image (id INTEGER PRIMARY KEY, page_id INTEGER, attachment TEXT, x REAL, y REAL, w REAL, h REAL)",
        "CREATE TABLE ex.pdf_page (page_id INTEGER PRIMARY KEY, attachment TEXT, page_index INTEGER)",
        "CREATE TABLE ex.shape (id INTEGER PRIMARY KEY, page_id INTEGER, kind TEXT, x REAL, y REAL, w REAL, h REAL, stroke TEXT, fill TEXT, width REAL, sort INTEGER)",
        "CREATE TABLE ex.tag (id INTEGER PRIMARY KEY, name TEXT NOT NULL)",
        "CREATE TABLE ex.page_tag (page_id INTEGER, tag_id INTEGER, PRIMARY KEY(page_id, tag_id))",
        "CREATE TABLE ex.page_link (block_id INTEGER, src_page INTEGER, dst_page INTEGER, PRIMARY KEY(block_id, dst_page))",
        "CREATE TABLE ex.attachment (sha256 TEXT PRIMARY KEY, mime TEXT, bytes INTEGER, name TEXT, created INTEGER)",
        "CREATE TABLE ex.attachment_data (sha256 TEXT PRIMARY KEY, data BLOB NOT NULL)",
    };
    for (const char *sql : kDdl)
        if (!db.exec(QString::fromUtf8(sql))) return false;
    return true;
}

bool attach(Database &db, const QString &path, const char *as)
{
    Database::Query q(db, QStringLiteral("ATTACH DATABASE ? AS %1").arg(QString::fromLatin1(as)));
    q.bind(1, path);
    return q.run();
}

bool copyInto(Database &db, const QString &table, const QString &where)
{
    const QString cols = columnsOf(table);
    Database::Query q(db, QStringLiteral("INSERT INTO ex.%1(%2) SELECT %2 FROM main.%1 WHERE %3").arg(table, cols, where));
    return q.run();
}

int countOf(Database &db, const QString &sql)
{
    Database::Query q(db, sql);
    return q.step() ? q.i32(0) : 0;
}

} // namespace

Result exportNotebook(Database &db, qint64 notebookId, const QString &path)
{
    Result out;
    QString name;
    {
        Database::Query q(db, "SELECT name FROM notebook WHERE id=? AND deleted_at IS NULL");
        q.bind(1, notebookId);
        if (!q.step()) { out.error = QStringLiteral("no such notebook"); return out; }
        name = q.text(0);
    }
    out.name = name;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile::remove(path);
    if (!attach(db, path, "ex")) { out.error = db.lastError(); return out; }
    const auto finish = [&db](Result r) { db.exec("DETACH DATABASE ex"); return r; };
    if (!makeTables(db)) { out.error = db.lastError(); return finish(out); }

    // Everything hangs off the notebook: sections that are not in the trash, their live pages, and
    // then whatever belongs to those pages.
    const QString live = QStringLiteral("deleted_at IS NULL");
    const QString sections = QStringLiteral("section_id IN (SELECT id FROM ex.section)");
    const QString pages = QStringLiteral("page_id IN (SELECT id FROM ex.page)");
    const bool copied =
        copyInto(db, QStringLiteral("notebook"), QStringLiteral("id=%1").arg(notebookId))
        && copyInto(db, QStringLiteral("section"), QStringLiteral("notebook_id=%1 AND %2").arg(notebookId).arg(live))
        && copyInto(db, QStringLiteral("page"), QStringLiteral("%1 AND %2").arg(sections, live))
        && copyInto(db, QStringLiteral("stroke_blob"), pages)
        && copyInto(db, QStringLiteral("text_block"), pages)
        && copyInto(db, QStringLiteral("image"), pages)
        && copyInto(db, QStringLiteral("pdf_page"), pages)
        && copyInto(db, QStringLiteral("shape"), pages)
        && copyInto(db, QStringLiteral("page_tag"), pages)
        && copyInto(db, QStringLiteral("tag"), QStringLiteral("id IN (SELECT tag_id FROM ex.page_tag)"))
        // A link to a page outside the notebook would land on whatever page had that id: only
        // links that stay inside are worth carrying.
        && copyInto(db, QStringLiteral("page_link"), QStringLiteral("src_page IN (SELECT id FROM ex.page) AND dst_page IN (SELECT id FROM ex.page)"))
        && copyInto(db, QStringLiteral("attachment"),
                    QStringLiteral("sha256 IN (SELECT attachment FROM ex.image UNION SELECT attachment FROM ex.pdf_page)"));
    if (!copied) { out.error = db.lastError(); return finish(out); }

    // The attachment files themselves travel inside the file, so it is one thing to send.
    QVector<QPair<QString, QString>> files;   // sha, mime
    {
        Database::Query q(db, "SELECT sha256, mime FROM ex.attachment");
        while (q.step()) files.append({q.text(0), q.text(1)});
    }
    for (const auto &[sha, mime] : files) {
        QFile f(attachments::pathFor(sha, mime));
        if (!f.open(QIODevice::ReadOnly)) continue;      // the row stays: the page still knows what it wanted
        Database::Query q(db, "INSERT OR REPLACE INTO ex.attachment_data(sha256, data) VALUES (?,?)");
        q.bind(1, sha).bind(2, f.readAll());
        if (q.run()) ++out.attachments;
    }

    out.pages = countOf(db, QStringLiteral("SELECT COUNT(*) FROM ex.page"));
    {
        Database::Query q(db, "INSERT INTO ex.lumen_export(format, app, exported, notebook, pages) VALUES (?,?,?,?,?)");
        q.bind(1, kFormat).bind(2, QStringLiteral("lumen")).bind(3, QDateTime::currentSecsSinceEpoch()).bind(4, name).bind(5, out.pages);
        q.run();
    }
    out.ok = true;
    return finish(out);
}

QString describe(const QString &path)
{
    if (!QFileInfo::exists(path)) return {};
    Database file;
    if (!file.open(path)) return {};
    Database::Query q(file, "SELECT notebook, pages, format FROM lumen_export LIMIT 1");
    if (!q.step() || q.i32(2) > kFormat) return {};
    const int pages = q.i32(1);
    return QStringLiteral("%1 · %2 page%3").arg(q.text(0)).arg(pages).arg(pages == 1 ? "" : "s");
}

Result importNotebook(Database &db, const QString &path)
{
    Result out;
    if (!QFileInfo::exists(path)) { out.error = QStringLiteral("no such file"); return out; }
    if (!attach(db, path, "im")) { out.error = db.lastError(); return out; }
    const auto finish = [&db](Result r) { db.exec("DETACH DATABASE im"); return r; };
    {
        Database::Query q(db, "SELECT notebook, format FROM im.lumen_export LIMIT 1");
        if (!q.step()) { out.error = QStringLiteral("that is not a Lumen notebook file"); return finish(out); }
        out.name = q.text(0);
        if (q.i32(1) > kFormat) { out.error = QStringLiteral("that file was written by a newer Lumen"); return finish(out); }
    }
    if (!db.begin()) { out.error = db.lastError(); return finish(out); }
    const auto fail = [&db, &finish](Result r, const QString &why) { r.error = why; db.rollback(); return finish(r); };

    // The notebook itself, never merged into one that is already here.
    QString name = out.name;
    for (int n = 1; n < 100; ++n) {
        Database::Query q(db, "SELECT COUNT(*) FROM notebook WHERE name=? AND deleted_at IS NULL");
        q.bind(1, name);
        if (!q.step() || q.i32(0) == 0) break;
        name = n == 1 ? out.name + QStringLiteral(" (imported)") : QStringLiteral("%1 (imported %2)").arg(out.name).arg(n);
    }
    out.name = name;
    {
        Database::Query q(db, "INSERT INTO notebook(name, colour, board, sort, created) "
                              "SELECT ?, COALESCE(colour,'#1F3A93'), board, (SELECT COALESCE(MAX(sort),0)+1 FROM notebook), ? FROM im.notebook LIMIT 1");
        q.bind(1, name).bind(2, QDateTime::currentSecsSinceEpoch());
        if (!q.run() || db.changes() == 0) return fail(out, db.lastError());
    }
    out.notebookId = db.lastInsertId();

    QHash<qint64, qint64> sectionIds, pageIds, blockIds, tagIds;
    {
        Database::Query q(db, "SELECT id, name, sort, kind, created FROM im.section ORDER BY sort, id");
        Database::Query ins(db, "INSERT INTO section(notebook_id, name, sort, kind, created) VALUES (?,?,?,?,?)");
        while (q.step()) {
            ins.reset();
            ins.bind(1, out.notebookId).bind(2, q.text(1)).bind(3, q.i64(2)).bind(4, q.text(3)).bind(5, q.i64(4));
            if (!ins.run()) return fail(out, db.lastError());
            sectionIds.insert(q.i64(0), db.lastInsertId());
        }
    }
    {
        Database::Query q(db, "SELECT id, section_id, title, sort, style, size_mode, width, height, created, modified, starred, paper FROM im.page ORDER BY sort, id");
        Database::Query ins(db, "INSERT INTO page(section_id, title, sort, style, size_mode, width, height, created, modified, starred, paper) VALUES (?,?,?,?,?,?,?,?,?,?,?)");
        while (q.step()) {
            const qint64 section = sectionIds.value(q.i64(1));
            if (!section) continue;
            ins.reset();
            ins.bind(1, section).bind(2, q.text(2)).bind(3, q.i64(3)).bind(4, q.text(4)).bind(5, q.text(5))
               .bind(6, q.f64(6)).bind(7, q.f64(7)).bind(8, q.i64(8)).bind(9, q.i64(9)).bind(10, q.i64(10)).bind(11, q.text(11));
            if (!ins.run()) return fail(out, db.lastError());
            const qint64 id = db.lastInsertId();
            pageIds.insert(q.i64(0), id);
            QFile::remove(QStringLiteral("%1/%2.log").arg(paths::journalDir()).arg(id));   // a reused rowid must not inherit a journal
            ++out.pages;
        }
    }
    {
        Database::Query q(db, "SELECT page_id, schema, data, stroke_count FROM im.stroke_blob");
        Database::Query ins(db, "INSERT INTO stroke_blob(page_id, schema, data, stroke_count) VALUES (?,?,?,?)");
        while (q.step()) {
            const qint64 page = pageIds.value(q.i64(0));
            if (!page) continue;
            ins.reset();
            ins.bind(1, page).bind(2, q.i64(1)).bind(3, q.blob(2)).bind(4, q.i64(3));
            if (!ins.run()) return fail(out, db.lastError());
        }
    }
    {
        Database::Query q(db, "SELECT id, page_id, x, y, w, markdown, created_t, sort FROM im.text_block ORDER BY page_id, sort, id");
        Database::Query ins(db, "INSERT INTO text_block(page_id, x, y, w, markdown, created_t, sort, edit_times) VALUES (?,?,?,?,?,?,?,'[]')");
        while (q.step()) {
            const qint64 page = pageIds.value(q.i64(1));
            if (!page) continue;
            ins.reset();
            ins.bind(1, page).bind(2, q.f64(2)).bind(3, q.f64(3)).bind(4, q.f64(4)).bind(5, q.text(5)).bind(6, q.i64(6)).bind(7, q.i64(7));
            if (!ins.run()) return fail(out, db.lastError());
            blockIds.insert(q.i64(0), db.lastInsertId());
        }
    }
    // Links are written into the text as lumen://page/<id>, so every one of them has to be moved
    // to the id the page has here; a link to a page that did not come along is emptied rather than
    // left pointing at whatever page now owns that number.
    {
        static const QRegularExpression link(QStringLiteral("lumen://page/(\\d+)"));
        QVector<QPair<qint64, QString>> rewritten;
        {
            Database::Query q(db, "SELECT b.id, b.markdown, b.page_id FROM text_block b WHERE b.page_id IN (SELECT id FROM main.page WHERE section_id IN (SELECT id FROM main.section WHERE notebook_id=?)) AND b.markdown LIKE '%lumen://page/%'");
            q.bind(1, out.notebookId);
            while (q.step()) {
                QString md = q.text(1);
                QString done;
                qsizetype at = 0;
                for (auto it = link.globalMatch(md); it.hasNext();) {
                    const auto m = it.next();
                    done += md.mid(at, m.capturedStart() - at);
                    done += QStringLiteral("lumen://page/%1").arg(pageIds.value(m.captured(1).toLongLong(), 0));
                    at = m.capturedEnd();
                }
                done += md.mid(at);
                if (done != md) rewritten.append({q.i64(0), done});
            }
        }
        Database::Query up(db, "UPDATE text_block SET markdown=? WHERE id=?");
        for (const auto &[id, md] : rewritten) { up.reset(); up.bind(1, md).bind(2, id); up.run(); }
    }
    {
        Database::Query q(db, "SELECT block_id, src_page, dst_page FROM im.page_link");
        Database::Query ins(db, "INSERT OR IGNORE INTO page_link(block_id, src_page, dst_page) VALUES (?,?,?)");
        while (q.step()) {
            const qint64 block = blockIds.value(q.i64(0)), src = pageIds.value(q.i64(1)), dst = pageIds.value(q.i64(2));
            if (!block || !src || !dst) continue;
            ins.reset();
            ins.bind(1, block).bind(2, src).bind(3, dst);
            ins.run();
        }
    }
    {
        Database::Query q(db, "SELECT id, page_id, kind, x, y, w, h, stroke, fill, width, sort FROM im.shape");
        Database::Query ins(db, "INSERT INTO shape(page_id, kind, x, y, w, h, stroke, fill, width, sort) VALUES (?,?,?,?,?,?,?,?,?,?)");
        while (q.step()) {
            const qint64 page = pageIds.value(q.i64(1));
            if (!page) continue;
            ins.reset();
            ins.bind(1, page).bind(2, q.text(2)).bind(3, q.f64(3)).bind(4, q.f64(4)).bind(5, q.f64(5)).bind(6, q.f64(6))
               .bind(7, q.text(7)).bind(8, q.text(8)).bind(9, q.f64(9)).bind(10, q.i64(10));
            if (!ins.run()) return fail(out, db.lastError());
        }
    }
    // Attachments: the bytes back onto disk under their hash, then the rows that point at them.
    {
        Database::Query q(db, "SELECT a.sha256, a.mime, a.bytes, a.name, a.created, d.data FROM im.attachment a LEFT JOIN im.attachment_data d ON d.sha256=a.sha256");
        Database::Query ins(db, "INSERT OR IGNORE INTO attachment(sha256, mime, bytes, name, created) VALUES (?,?,?,?,?)");
        while (q.step()) {
            const QString sha = q.text(0), mime = q.text(1);
            if (!q.isNull(5)) {
                const QString dest = attachments::pathFor(sha, mime);
                QDir().mkpath(QFileInfo(dest).absolutePath());
                if (!QFileInfo::exists(dest)) {
                    QFile f(dest);
                    if (f.open(QIODevice::WriteOnly)) { f.write(q.blob(5)); f.close(); ++out.attachments; }
                }
            }
            ins.reset();
            ins.bind(1, sha).bind(2, mime).bind(3, q.i64(2)).bind(4, q.text(3)).bind(5, q.i64(4));
            ins.run();
        }
    }
    {
        Database::Query q(db, "SELECT id, page_id, attachment, x, y, w, h FROM im.image");
        Database::Query ins(db, "INSERT INTO image(page_id, attachment, x, y, w, h) VALUES (?,?,?,?,?,?)");
        while (q.step()) {
            const qint64 page = pageIds.value(q.i64(1));
            if (!page) continue;
            ins.reset();
            ins.bind(1, page).bind(2, q.text(2)).bind(3, q.f64(3)).bind(4, q.f64(4)).bind(5, q.f64(5)).bind(6, q.f64(6));
            if (!ins.run()) return fail(out, db.lastError());
        }
    }
    {
        Database::Query q(db, "SELECT page_id, attachment, page_index FROM im.pdf_page");
        Database::Query ins(db, "INSERT OR REPLACE INTO pdf_page(page_id, attachment, page_index) VALUES (?,?,?)");
        while (q.step()) {
            const qint64 page = pageIds.value(q.i64(0));
            if (!page) continue;
            ins.reset();
            ins.bind(1, page).bind(2, q.text(1)).bind(3, q.i64(2));
            if (!ins.run()) return fail(out, db.lastError());
        }
    }
    // Tags are shared by name with the ones already here, so an imported #exam is the same #exam.
    {
        Database::Query q(db, "SELECT id, name FROM im.tag");
        while (q.step()) {
            const QString tag = q.text(1).trimmed();
            if (tag.isEmpty()) continue;
            qint64 id = 0;
            { Database::Query f(db, "SELECT id FROM tag WHERE name=? COLLATE NOCASE AND parent_id IS NULL ORDER BY id LIMIT 1"); f.bind(1, tag); if (f.step()) id = f.i64(0); }
            if (!id) {
                Database::Query ins(db, "INSERT INTO tag(name) VALUES (?)");
                ins.bind(1, tag);
                if (!ins.run()) return fail(out, db.lastError());
                id = db.lastInsertId();
            }
            tagIds.insert(q.i64(0), id);
        }
        Database::Query q2(db, "SELECT page_id, tag_id FROM im.page_tag");
        Database::Query ins(db, "INSERT OR IGNORE INTO page_tag(page_id, tag_id) VALUES (?,?)");
        while (q2.step()) {
            const qint64 page = pageIds.value(q2.i64(0)), tag = tagIds.value(q2.i64(1));
            if (!page || !tag) continue;
            ins.reset();
            ins.bind(1, page).bind(2, tag);
            ins.run();
        }
    }
    // Typed text is searchable the moment it lands, without waiting for anything to open it.
    {
        Database::Query q(db, "SELECT id, page_id, markdown FROM text_block WHERE page_id IN (SELECT id FROM page WHERE section_id IN (SELECT id FROM section WHERE notebook_id=?))");
        q.bind(1, out.notebookId);
        Database::Query ins(db, "INSERT INTO search(kind, page_id, ref_id, text) VALUES ('text',?,?,?)");
        while (q.step()) {
            if (q.text(2).trimmed().isEmpty()) continue;
            ins.reset();
            ins.bind(1, q.i64(1)).bind(2, q.i64(0)).bind(3, q.text(2));
            ins.run();
        }
    }
    if (!db.commit()) return fail(out, db.lastError());
    out.ok = true;
    return finish(out);
}

} // namespace notebookfile

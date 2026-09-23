#include "schema.h"
#include "database.h"
#include <QRegularExpression>
#include <QStringList>
#include <QVector>
#include <tuple>

static const char *kSchemaV1[] = {
    "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS notebook (id INTEGER PRIMARY KEY, name TEXT NOT NULL, colour TEXT NOT NULL DEFAULT '#1F3A93',"
    "  sort INTEGER NOT NULL DEFAULT 0, board TEXT, created INTEGER NOT NULL, deleted_at INTEGER)",
    "CREATE TABLE IF NOT EXISTS section (id INTEGER PRIMARY KEY, notebook_id INTEGER NOT NULL REFERENCES notebook(id) ON DELETE CASCADE,"
    "  name TEXT NOT NULL, sort INTEGER NOT NULL DEFAULT 0, kind TEXT NOT NULL DEFAULT 'notes', created INTEGER NOT NULL, deleted_at INTEGER)",
    "CREATE TABLE IF NOT EXISTS page (id INTEGER PRIMARY KEY, section_id INTEGER NOT NULL REFERENCES section(id) ON DELETE CASCADE,"
    "  title TEXT NOT NULL DEFAULT '', sort INTEGER NOT NULL DEFAULT 0, style TEXT NOT NULL DEFAULT 'dotted',"
    "  size_mode TEXT NOT NULL DEFAULT 'a4', width REAL NOT NULL DEFAULT 794, height REAL NOT NULL DEFAULT 1123,"
    "  created INTEGER NOT NULL, modified INTEGER NOT NULL, deleted_at INTEGER)",
    "CREATE INDEX IF NOT EXISTS page_section ON page(section_id, sort)",
    "CREATE TABLE IF NOT EXISTS stroke_blob (page_id INTEGER PRIMARY KEY REFERENCES page(id) ON DELETE CASCADE,"
    "  schema INTEGER NOT NULL, data BLOB NOT NULL, stroke_count INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS text_block (id INTEGER PRIMARY KEY, page_id INTEGER NOT NULL REFERENCES page(id) ON DELETE CASCADE,"
    "  x REAL NOT NULL, y REAL NOT NULL, w REAL NOT NULL, markdown TEXT NOT NULL DEFAULT '', created_t INTEGER, recording_id INTEGER, sort INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS attachment (sha256 TEXT PRIMARY KEY, mime TEXT NOT NULL, bytes INTEGER NOT NULL, name TEXT, created INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS image (id INTEGER PRIMARY KEY, page_id INTEGER NOT NULL REFERENCES page(id) ON DELETE CASCADE,"
    "  attachment TEXT NOT NULL REFERENCES attachment(sha256), x REAL NOT NULL, y REAL NOT NULL, w REAL NOT NULL, h REAL NOT NULL)",
    "CREATE TABLE IF NOT EXISTS pdf_page (page_id INTEGER PRIMARY KEY REFERENCES page(id) ON DELETE CASCADE,"
    "  attachment TEXT NOT NULL REFERENCES attachment(sha256), page_index INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS recording (id INTEGER PRIMARY KEY, section_id INTEGER REFERENCES section(id) ON DELETE SET NULL,"
    "  page_id INTEGER REFERENCES page(id) ON DELETE SET NULL, attachment TEXT REFERENCES attachment(sha256),"
    "  started_at INTEGER NOT NULL, duration_ms INTEGER NOT NULL DEFAULT 0, audio_deleted INTEGER NOT NULL DEFAULT 0, title TEXT)",
    "CREATE TABLE IF NOT EXISTS transcript_segment (id INTEGER PRIMARY KEY, recording_id INTEGER NOT NULL REFERENCES recording(id) ON DELETE CASCADE,"
    "  t0_ms INTEGER NOT NULL, t1_ms INTEGER NOT NULL, text TEXT NOT NULL, pass TEXT NOT NULL DEFAULT 'live')",
    "CREATE INDEX IF NOT EXISTS transcript_rec ON transcript_segment(recording_id, t0_ms)",
    "CREATE TABLE IF NOT EXISTS ocr_result (id INTEGER PRIMARY KEY, page_id INTEGER NOT NULL REFERENCES page(id) ON DELETE CASCADE,"
    "  x REAL NOT NULL, y REAL NOT NULL, w REAL NOT NULL, h REAL NOT NULL, text TEXT NOT NULL, confidence REAL NOT NULL DEFAULT 0,"
    "  corrected INTEGER NOT NULL DEFAULT 0, stroke_ids TEXT, updated INTEGER NOT NULL)",
    "CREATE TABLE IF NOT EXISTS tag (id INTEGER PRIMARY KEY, name TEXT NOT NULL, parent_id INTEGER REFERENCES tag(id) ON DELETE CASCADE, notebook_id INTEGER REFERENCES notebook(id) ON DELETE CASCADE)",
    "CREATE TABLE IF NOT EXISTS page_tag (page_id INTEGER NOT NULL REFERENCES page(id) ON DELETE CASCADE, tag_id INTEGER NOT NULL REFERENCES tag(id) ON DELETE CASCADE, PRIMARY KEY(page_id, tag_id))",
    "CREATE TABLE IF NOT EXISTS card (id INTEGER PRIMARY KEY, kind TEXT NOT NULL DEFAULT 'basic', front TEXT NOT NULL, back TEXT NOT NULL DEFAULT '',"
    "  source_page INTEGER REFERENCES page(id) ON DELETE SET NULL, sibling_id INTEGER, tag_id INTEGER REFERENCES tag(id) ON DELETE SET NULL,"
    "  created INTEGER NOT NULL, deleted_at INTEGER)",
    "CREATE TABLE IF NOT EXISTS card_state (card_id INTEGER PRIMARY KEY REFERENCES card(id) ON DELETE CASCADE, stability REAL NOT NULL DEFAULT 0,"
    "  difficulty REAL NOT NULL DEFAULT 0, due INTEGER NOT NULL, last_review INTEGER, reps INTEGER NOT NULL DEFAULT 0, lapses INTEGER NOT NULL DEFAULT 0, state INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS review_log (id INTEGER PRIMARY KEY, card_id INTEGER NOT NULL REFERENCES card(id) ON DELETE CASCADE, reviewed INTEGER NOT NULL,"
    "  rating INTEGER NOT NULL, stability REAL NOT NULL, difficulty REAL NOT NULL, elapsed_days REAL NOT NULL, scheduled_days REAL NOT NULL)",
    "CREATE TABLE IF NOT EXISTS paper (id INTEGER PRIMARY KEY, subject TEXT NOT NULL, year INTEGER NOT NULL, name TEXT NOT NULL, board TEXT,"
    "  paper_attachment TEXT REFERENCES attachment(sha256), scheme_attachment TEXT REFERENCES attachment(sha256), section_id INTEGER REFERENCES section(id) ON DELETE SET NULL,"
    "  total_marks INTEGER, minutes INTEGER, created INTEGER NOT NULL, deleted_at INTEGER)",
    "CREATE TABLE IF NOT EXISTS paper_question (id INTEGER PRIMARY KEY, paper_id INTEGER NOT NULL REFERENCES paper(id) ON DELETE CASCADE, label TEXT NOT NULL,"
    "  page_index INTEGER NOT NULL, x REAL, y REAL, w REAL, h REAL, marks_available INTEGER NOT NULL DEFAULT 0, topic_tags TEXT NOT NULL DEFAULT '', sort INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS paper_attempt (id INTEGER PRIMARY KEY, paper_id INTEGER NOT NULL REFERENCES paper(id) ON DELETE CASCADE, started INTEGER NOT NULL,"
    "  finished INTEGER, timer_mode INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE IF NOT EXISTS paper_question_attempt (attempt_id INTEGER NOT NULL REFERENCES paper_attempt(id) ON DELETE CASCADE,"
    "  question_id INTEGER NOT NULL REFERENCES paper_question(id) ON DELETE CASCADE, marks_scored INTEGER, seconds INTEGER, error_type TEXT, PRIMARY KEY(attempt_id, question_id))",
    "CREATE TABLE IF NOT EXISTS shape (id INTEGER PRIMARY KEY, page_id INTEGER NOT NULL REFERENCES page(id) ON DELETE CASCADE,"
    "  kind TEXT NOT NULL DEFAULT 'rect', x REAL NOT NULL, y REAL NOT NULL, w REAL NOT NULL, h REAL NOT NULL,"
    "  stroke TEXT NOT NULL DEFAULT '#1A1A1A', fill TEXT NOT NULL DEFAULT '', width REAL NOT NULL DEFAULT 2, sort INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX IF NOT EXISTS shape_page ON shape(page_id, sort, id)",
    "CREATE TABLE IF NOT EXISTS setting (key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE IF NOT EXISTS recording_mark (id INTEGER PRIMARY KEY, recording_id INTEGER NOT NULL REFERENCES recording(id) ON DELETE CASCADE,"
    "  t_ms INTEGER NOT NULL, label TEXT NOT NULL DEFAULT '')",
    "CREATE INDEX IF NOT EXISTS mark_rec ON recording_mark(recording_id, t_ms)",
    "CREATE TABLE IF NOT EXISTS claude_log (id INTEGER PRIMARY KEY, at INTEGER NOT NULL, feature TEXT NOT NULL, prompt_chars INTEGER, response_chars INTEGER, ok INTEGER, path TEXT)",
    // [[page]] links, one row per (text block, page it points at). The link itself lives in the
    // block's Markdown as [Title](lumen://page/<id>); this table is the index that answers "what
    // links here?" without scanning every block.
    "CREATE TABLE IF NOT EXISTS page_link (block_id INTEGER NOT NULL REFERENCES text_block(id) ON DELETE CASCADE,"
    "  src_page INTEGER NOT NULL REFERENCES page(id) ON DELETE CASCADE, dst_page INTEGER NOT NULL REFERENCES page(id) ON DELETE CASCADE,"
    "  PRIMARY KEY(block_id, dst_page))",
    "CREATE INDEX IF NOT EXISTS page_link_dst ON page_link(dst_page)",
    "CREATE INDEX IF NOT EXISTS page_tag_tag ON page_tag(tag_id)",
    "CREATE VIRTUAL TABLE IF NOT EXISTS search USING fts5(kind UNINDEXED, page_id UNINDEXED, ref_id UNINDEXED, text, tokenize='unicode61')",
};

int ensureSchema(Database &db)
{
    if (!db.begin()) return 0;
    for (const char *sql : kSchemaV1)
        if (!db.exec(QString::fromUtf8(sql))) { db.rollback(); return 0; }
    int version = 0;
    {
        Database::Query q(db, "SELECT version FROM schema_version LIMIT 1");
        if (q.step()) version = q.i32(0);
    }
    if (version == 0) {
        db.exec("ALTER TABLE text_block ADD COLUMN edit_times TEXT NOT NULL DEFAULT '[]'");
        db.exec("ALTER TABLE page ADD COLUMN starred INTEGER NOT NULL DEFAULT 0");
        db.exec("ALTER TABLE page ADD COLUMN paper TEXT NOT NULL DEFAULT ''");
        for (const char *col : {"crop_x REAL NOT NULL DEFAULT 0", "crop_y REAL NOT NULL DEFAULT 0",
                                "crop_w REAL NOT NULL DEFAULT 1", "crop_h REAL NOT NULL DEFAULT 1",
                                "rotation INTEGER NOT NULL DEFAULT 0"})
            db.exec(QStringLiteral("ALTER TABLE image ADD COLUMN %1").arg(QLatin1String(col)));
        db.exec(QStringLiteral("INSERT INTO schema_version(version) VALUES (%1)").arg(kSchemaVersion));
        version = kSchemaVersion;
    }
    if (version < 2) {   // 2026-09-03: keystroke-batch timeline per text block (Phase 5 tap-to-seek)
        if (!db.exec("ALTER TABLE text_block ADD COLUMN edit_times TEXT NOT NULL DEFAULT '[]'")) { db.rollback(); return 0; }
        db.exec("UPDATE schema_version SET version=2");
        version = 2;
    }
    if (version < 3) {   // 2026-09-05: starred pages, and marks dropped into a recording
        if (!db.exec("ALTER TABLE page ADD COLUMN starred INTEGER NOT NULL DEFAULT 0")) { db.rollback(); return 0; }
        db.exec("UPDATE schema_version SET version=3");
        version = 3;
    }
    if (version < 4) {   // 2026-09-05: the paper has its own colour, so ink never vanishes with the theme
        if (!db.exec("ALTER TABLE page ADD COLUMN paper TEXT NOT NULL DEFAULT ''")) { db.rollback(); return 0; }
        db.exec("UPDATE schema_version SET version=4");
        version = 4;
    }
    if (version < 5) {   // 2026-09-05: shapes are objects, not frozen ink — they keep fill, outline and size
        db.exec("UPDATE schema_version SET version=5");
        version = 5;     // the CREATE above is enough; nothing to migrate
    }
    if (version < 6) {   // 2026-09-22: [[page]] links get a backlink index; page tags get a lookup by tag
        // The CREATEs above made the tables. Links already written into text (by hand, or by an
        // older build that could not index them) are indexed now so "Linked from" is complete.
        static const QRegularExpression link(QStringLiteral("\\]\\(lumen://page/(\\d+)\\)"));
        QVector<std::tuple<qint64, qint64, qint64>> found;
        {
            Database::Query q(db, "SELECT id, page_id, markdown FROM text_block WHERE markdown LIKE '%lumen://page/%'");
            while (q.step())
                for (auto it = link.globalMatch(q.text(2)); it.hasNext();)
                    found.append({q.i64(0), q.i64(1), it.next().captured(1).toLongLong()});
        }
        for (const auto &[block, src, dst] : found) {
            Database::Query q(db, "INSERT OR IGNORE INTO page_link(block_id, src_page, dst_page) SELECT ?, ?, id FROM page WHERE id=? AND id<>?");
            q.bind(1, block).bind(2, src).bind(3, dst).bind(4, src);
            q.run();
        }
        db.exec("UPDATE schema_version SET version=6");
        version = 6;
    }
    if (version < 7) {   // 2026-09-23: pictures are cropped and turned without touching the original file
        // A file can already have some of these: a build where this was migration 6 wrote them.
        QStringList have;
        {
            Database::Query q(db, "PRAGMA table_info(image)");
            while (q.step()) have << q.text(1);
        }
        for (const char *col : {"crop_x REAL NOT NULL DEFAULT 0", "crop_y REAL NOT NULL DEFAULT 0",
                                "crop_w REAL NOT NULL DEFAULT 1", "crop_h REAL NOT NULL DEFAULT 1",
                                "rotation INTEGER NOT NULL DEFAULT 0"}) {
            const QString def = QLatin1String(col);
            if (have.contains(def.section(QLatin1Char(' '), 0, 0))) continue;
            if (!db.exec(QStringLiteral("ALTER TABLE image ADD COLUMN %1").arg(def))) { db.rollback(); return 0; }
        }
        db.exec("UPDATE schema_version SET version=7");
        version = 7;
    }
    if (!db.commit()) return 0;
    return version;
}

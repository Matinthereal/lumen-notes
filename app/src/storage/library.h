#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class Database;

// Notebooks → sections → pages, plus settings. Exposed to QML as `library`. Deletion is soft
// (`deleted_at`) so "Undo" replaces "Are you sure?" (Q22); purge happens on start for items older
// than 30 days.
class Library : public QObject {
    Q_OBJECT
public:
    explicit Library(Database &db, QObject *parent = nullptr);

    Q_INVOKABLE QVariantList notebooks() const;
    Q_INVOKABLE QVariantList sections(qint64 notebookId) const;
    Q_INVOKABLE QVariantList pages(qint64 sectionId) const;
    Q_INVOKABLE QVariantMap page(qint64 pageId) const;
    Q_INVOKABLE QVariantMap section(qint64 sectionId) const;
    Q_INVOKABLE QVariantList allSections() const;      // every section, with its notebook, for "move to…"

    Q_INVOKABLE qint64 createNotebook(const QString &name, const QString &colour, const QString &board = {});
    Q_INVOKABLE qint64 createSection(qint64 notebookId, const QString &name, const QString &kind = QStringLiteral("notes"));
    Q_INVOKABLE qint64 createPage(qint64 sectionId, const QString &style = {}, const QString &sizeMode = {}, int afterIndex = -1);
    Q_INVOKABLE void rename(const QString &kind, qint64 id, const QString &name);
    void suggestTitle(qint64 pageId, const QString &text);     // auto-title: only while the page has no maker-given name
    void tidyAutomaticTitles();                                // clears auto titles that turned out to be recognition noise
    Q_INVOKABLE QVariantList recentPages(int limit = 8) const;
    Q_INVOKABLE QVariantList starredPages(int limit = 40) const;
    Q_INVOKABLE void setStarred(qint64 pageId, bool starred);
    Q_INVOKABLE bool isStarred(qint64 pageId) const;
    Q_INVOKABLE void touchPage(qint64 pageId);                  // records "opened just now" for recents
    Q_INVOKABLE void setNotebookColour(qint64 id, const QString &colour);
    Q_INVOKABLE void setPageStyle(qint64 id, const QString &style);
    Q_INVOKABLE void setPagePaper(qint64 id, const QString &colour);   // "" = the default from settings
    Q_INVOKABLE void setPageSizeMode(qint64 id, const QString &mode);
    Q_INVOKABLE void setPageSize(qint64 id, double width, double height);
    void setPagePdf(qint64 pageId, const QString &sha256, int index);
    // Full-text index (FTS5): one row per (kind, page, ref). kind ∈ text|ocr|transcript|pdf.
    Q_INVOKABLE void indexText(const QString &kind, qint64 pageId, qint64 refId, const QString &text);
    Q_INVOKABLE void unindex(const QString &kind, qint64 pageId, qint64 refId = -1);
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 40, const QString &tag = {}) const;
    Q_INVOKABLE void movePage(qint64 pageId, int newIndex);
    Q_INVOKABLE void remove(const QString &kind, qint64 id);
    Q_INVOKABLE void restore(const QString &kind, qint64 id);
    Q_INVOKABLE int purgeDeleted(int olderThanDays = 30);
    Q_INVOKABLE QVariantList deletedItems(int limit = 200) const;    // the trash, newest first
    Q_INVOKABLE void purgeOne(const QString &kind, qint64 id);       // gone for good
    Q_INVOKABLE qint64 duplicatePage(qint64 pageId);                 // ink, text, pictures and PDF page with it
    Q_INVOKABLE void movePageToSection(qint64 pageId, qint64 sectionId);

    // [[page]] links. In text a link is [Title](lumen://page/<id>): the id is what it follows, so a
    // rename never breaks it, and the label is refreshed to the current title whenever it is shown.
    void syncLinks(qint64 blockId, qint64 pageId, const QString &markdown);   // re-index one block's links
    Q_INVOKABLE QVariantList backlinks(qint64 pageId) const;                  // pages whose text links here
    Q_INVOKABLE QVariantList linkCandidates(const QString &query, qint64 excludePageId = 0, int limit = 8) const;
    Q_INVOKABLE QString resolveLinks(const QString &markdown) const;          // fresh labels; [[Title]] → a link
    Q_INVOKABLE qint64 linkTarget(const QString &url) const;                  // 0 unless it names a live page
    Q_INVOKABLE qint64 pageByTitle(const QString &title) const;
    Q_INVOKABLE QString displayTitle(qint64 pageId) const;                    // title without the auto mark
    static QString pageUrl(qint64 pageId) { return QStringLiteral("lumen://page/%1").arg(pageId); }
    static QString linkMarkdown(const QString &title, qint64 pageId);

    // Page tags. A tag is a name ("#" and spacing tidied, case kept as first typed, matched without
    // case); the tag table is shared with flashcards.
    static QString normaliseTag(const QString &name);
    Q_INVOKABLE QVariantList tags() const;                          // every tag on a live page, with its count
    Q_INVOKABLE QVariantList pageTags(qint64 pageId) const;
    Q_INVOKABLE qint64 addPageTag(qint64 pageId, const QString &name);   // the tag id, 0 if the name was empty
    Q_INVOKABLE void removePageTag(qint64 pageId, qint64 tagId);
    Q_INVOKABLE QVariantList pagesWithTag(const QString &name) const;

    Q_INVOKABLE qint64 firstPageId() const;
    Q_INVOKABLE qint64 nextPageId(qint64 pageId, int delta) const;   // ±1 within the section
    Q_INVOKABLE QString setting(const QString &key, const QString &fallback = {}) const;
    Q_INVOKABLE void setSetting(const QString &key, const QString &value);

    bool seedDefaults();   // one notebook with a typed welcome page when the library is empty

signals:
    void changed();
    void linksChanged();
    void tagsChanged();

private:
    qint64 now() const;
    Database &m_db;
};

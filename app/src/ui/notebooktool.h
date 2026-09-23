#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantMap>

class Database;
class Library;

// "Export notebook…" / "Import a notebook…" from QML: a whole notebook in one .lumen file.
class NotebookTool : public QObject {
    Q_OBJECT
public:
    NotebookTool(Database &db, Library &lib, QObject *parent = nullptr);
    Q_INVOKABLE QVariantMap exportNotebook(qint64 notebookId, const QUrl &file);
    Q_INVOKABLE QVariantMap importNotebook(const QUrl &file);
    Q_INVOKABLE QString suggestedName(qint64 notebookId) const;
    Q_INVOKABLE QString describe(const QUrl &file) const;

private:
    Database &m_db;
    Library &m_lib;
};

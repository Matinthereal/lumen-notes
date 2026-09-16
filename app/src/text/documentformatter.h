#pragma once
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickTextDocument>

class QTextDocument;
class QTextCursor;

// Formatting for a typed page: the QML TextArea owns the text, this applies bold, headings, lists
// and checklists to its selection through QTextCursor, and reads the whole thing back as Markdown.
class DocumentFormatter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int selectionStart MEMBER m_selectionStart WRITE setSelectionStart NOTIFY stateChanged)
    Q_PROPERTY(int selectionEnd MEMBER m_selectionEnd WRITE setSelectionEnd NOTIFY stateChanged)
    Q_PROPERTY(bool bold READ bold NOTIFY stateChanged)
    Q_PROPERTY(bool italic READ italic NOTIFY stateChanged)
    Q_PROPERTY(bool underline READ underline NOTIFY stateChanged)
    Q_PROPERTY(bool strikeout READ strikeout NOTIFY stateChanged)
    Q_PROPERTY(int heading READ heading NOTIFY stateChanged)
    Q_PROPERTY(QString list READ list NOTIFY stateChanged)        // "" | "bullet" | "number" | "check"
public:
    explicit DocumentFormatter(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *doc);
    void setSelectionStart(int v);
    void setSelectionEnd(int v);

    bool bold() const;
    bool italic() const;
    bool underline() const;
    bool strikeout() const;
    int heading() const;
    QString list() const;

    Q_INVOKABLE void toggleBold();
    Q_INVOKABLE void toggleItalic();
    Q_INVOKABLE void toggleUnderline();
    Q_INVOKABLE void toggleStrikeout();
    Q_INVOKABLE void setHeading(int level);                    // 0 = body text
    Q_INVOKABLE void toggleList(const QString &kind);           // bullet | number | check
    Q_INVOKABLE void indent(int delta);
    Q_INVOKABLE bool toggleCheckAt(int position);               // true if a checklist box was flipped
    Q_INVOKABLE bool continueList();                            // Enter on an empty list item ends the list
    Q_INVOKABLE QString markdown() const;
    Q_INVOKABLE void setMarkdown(const QString &markdown);
    Q_INVOKABLE QString plainText() const;
    Q_INVOKABLE int blockStart(int position) const;
    Q_INVOKABLE int wordCount() const;

signals:
    void documentChanged();
    void stateChanged();
    void modified();

private:
    QTextDocument *doc() const;
    QTextCursor cursor() const;
    QTextCursor wordOrSelection() const;
    void mergeCharFormat(const class QTextCharFormat &fmt);
    QPointer<QQuickTextDocument> m_document;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    bool m_loading = false;
};

#include "documentformatter.h"

#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

namespace {
const qreal kHeadingSizes[] = {0, 26, 21, 17};

QTextListFormat::Style styleFor(const QString &kind)
{
    return kind == QLatin1String("number") ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc;
}
} // namespace

DocumentFormatter::DocumentFormatter(QObject *parent) : QObject(parent) {}

void DocumentFormatter::setDocument(QQuickTextDocument *d)
{
    if (d == m_document) return;
    if (doc()) disconnect(doc(), nullptr, this, nullptr);
    m_document = d;
    if (doc()) {
        doc()->setIndentWidth(28);
        connect(doc(), &QTextDocument::contentsChanged, this, [this] { if (!m_loading) emit modified(); });
        connect(doc(), &QTextDocument::contentsChanged, this, &DocumentFormatter::stateChanged);
    }
    emit documentChanged();
    emit stateChanged();
}

void DocumentFormatter::setSelectionStart(int v) { if (v == m_selectionStart) return; m_selectionStart = v; emit stateChanged(); }
void DocumentFormatter::setSelectionEnd(int v) { if (v == m_selectionEnd) return; m_selectionEnd = v; emit stateChanged(); }

QTextDocument *DocumentFormatter::doc() const { return m_document ? m_document->textDocument() : nullptr; }

QTextCursor DocumentFormatter::cursor() const
{
    QTextDocument *d = doc();
    if (!d) return {};
    QTextCursor c(d);
    const int last = std::max(0, d->characterCount() - 1);
    const int a = std::clamp(m_selectionStart, 0, last), b = std::clamp(m_selectionEnd, 0, last);
    if (a == b) { c.setPosition(a); return c; }
    c.setPosition(a);
    c.setPosition(b, QTextCursor::KeepAnchor);
    return c;
}

QTextCursor DocumentFormatter::wordOrSelection() const
{
    QTextCursor c = cursor();
    if (!c.hasSelection()) c.select(QTextCursor::WordUnderCursor);
    return c;
}

void DocumentFormatter::mergeCharFormat(const QTextCharFormat &fmt)
{
    QTextCursor c = wordOrSelection();
    if (c.isNull()) return;
    c.mergeCharFormat(fmt);
    emit stateChanged();
}

namespace {
QTextCharFormat charFormatAt(const QTextCursor &c)
{
    if (c.isNull()) return {};
    if (!c.hasSelection()) return c.charFormat();
    QTextCursor probe(c.document());
    probe.setPosition(std::min(c.selectionStart(), c.selectionEnd()) + 1);
    return probe.charFormat();
}
} // namespace

bool DocumentFormatter::bold() const { return charFormatAt(cursor()).fontWeight() >= QFont::Bold; }
bool DocumentFormatter::italic() const { return charFormatAt(cursor()).fontItalic(); }
bool DocumentFormatter::underline() const { return charFormatAt(cursor()).fontUnderline(); }
bool DocumentFormatter::strikeout() const { return charFormatAt(cursor()).fontStrikeOut(); }

int DocumentFormatter::heading() const
{
    const QTextCursor c = cursor();
    return c.isNull() ? 0 : c.blockFormat().headingLevel();
}

QString DocumentFormatter::list() const
{
    const QTextCursor c = cursor();
    if (c.isNull()) return {};
    const QTextBlock block = c.block();
    if (block.blockFormat().marker() != QTextBlockFormat::MarkerType::NoMarker) return QStringLiteral("check");
    if (QTextList *l = block.textList())
        return l->format().style() == QTextListFormat::ListDecimal ? QStringLiteral("number") : QStringLiteral("bullet");
    return {};
}

void DocumentFormatter::toggleBold()
{
    QTextCharFormat f;
    f.setFontWeight(bold() ? QFont::Normal : QFont::Bold);
    mergeCharFormat(f);
}
void DocumentFormatter::toggleItalic() { QTextCharFormat f; f.setFontItalic(!italic()); mergeCharFormat(f); }
void DocumentFormatter::toggleUnderline() { QTextCharFormat f; f.setFontUnderline(!underline()); mergeCharFormat(f); }
void DocumentFormatter::toggleStrikeout() { QTextCharFormat f; f.setFontStrikeOut(!strikeout()); mergeCharFormat(f); }

void DocumentFormatter::setHeading(int level)
{
    QTextCursor c = cursor();
    if (c.isNull()) return;
    level = std::clamp(level, 0, 3);
    c.beginEditBlock();
    QTextBlockFormat bf;
    bf.setHeadingLevel(level);
    bf.setTopMargin(level ? 14 : 0);
    bf.setBottomMargin(level ? 4 : 0);
    c.mergeBlockFormat(bf);
    // A heading is its whole line: select the blocks and size every character in them.
    QTextCursor lines = c;
    const int a = std::min(c.selectionStart(), c.selectionEnd()), b = std::max(c.selectionStart(), c.selectionEnd());
    lines.setPosition(a);
    lines.movePosition(QTextCursor::StartOfBlock);
    lines.setPosition(b, QTextCursor::KeepAnchor);
    lines.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QTextCharFormat cf;
    if (level) { cf.setFontPointSize(kHeadingSizes[level] * 0.75); cf.setFontWeight(QFont::Bold); }
    else { cf.setProperty(QTextFormat::FontPointSize, QVariant()); cf.setFontWeight(QFont::Normal); }
    if (level) lines.mergeCharFormat(cf);
    else {
        QTextCharFormat plain;
        plain.setFontWeight(QFont::Normal);
        lines.mergeCharFormat(plain);
        // Clearing a size has to remove the property, which merge cannot do; walk the fragments.
        for (QTextBlock blk = doc()->findBlock(a); blk.isValid() && blk.position() <= b; blk = blk.next())
            for (auto it = blk.begin(); !it.atEnd(); ++it) {
                QTextFragment frag = it.fragment();
                QTextCharFormat f = frag.charFormat();
                if (!f.hasProperty(QTextFormat::FontPointSize)) continue;
                f.clearProperty(QTextFormat::FontPointSize);
                QTextCursor fc(doc());
                fc.setPosition(frag.position());
                fc.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
                fc.setCharFormat(f);
            }
    }
    c.endEditBlock();
    emit stateChanged();
}

void DocumentFormatter::toggleList(const QString &kind)
{
    QTextCursor c = cursor();
    if (c.isNull()) return;
    const QString now = list();
    c.beginEditBlock();
    QTextBlockFormat bf = c.blockFormat();
    if (now == kind) {                                  // same button again: back to a paragraph
        if (QTextList *l = c.currentList()) {
            const int a = std::min(c.selectionStart(), c.selectionEnd()), b = std::max(c.selectionStart(), c.selectionEnd());
            for (QTextBlock blk = doc()->findBlock(a); blk.isValid() && blk.position() <= b; blk = blk.next())
                if (blk.textList()) blk.textList()->remove(blk);
            Q_UNUSED(l);
        }
        bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
        bf.setIndent(0);
        c.setBlockFormat(bf);
    } else {
        bf.setMarker(kind == QLatin1String("check") ? QTextBlockFormat::MarkerType::Unchecked : QTextBlockFormat::MarkerType::NoMarker);
        c.setBlockFormat(bf);
        QTextListFormat lf;
        if (QTextList *existing = c.currentList()) lf = existing->format();
        else lf.setIndent(1);
        lf.setStyle(styleFor(kind));
        c.createList(lf);
    }
    c.endEditBlock();
    emit stateChanged();
}

void DocumentFormatter::indent(int delta)
{
    QTextCursor c = cursor();
    if (c.isNull()) return;
    QTextList *l = c.currentList();
    if (!l) return;
    QTextListFormat lf = l->format();
    const int level = std::clamp(lf.indent() + delta, 1, 6);
    if (level == lf.indent()) return;
    c.beginEditBlock();
    lf.setIndent(level);
    const QTextBlockFormat keep = c.blockFormat();
    c.createList(lf);
    QTextBlockFormat bf; bf.setMarker(keep.marker());
    c.mergeBlockFormat(bf);
    c.endEditBlock();
    emit stateChanged();
}

bool DocumentFormatter::toggleCheckAt(int position)
{
    QTextDocument *d = doc();
    if (!d) return false;
    QTextBlock blk = d->findBlock(position);
    if (!blk.isValid()) return false;
    const auto marker = blk.blockFormat().marker();
    if (marker == QTextBlockFormat::MarkerType::NoMarker) return false;
    QTextCursor c(blk);
    QTextBlockFormat bf = blk.blockFormat();
    bf.setMarker(marker == QTextBlockFormat::MarkerType::Checked ? QTextBlockFormat::MarkerType::Unchecked
                                                                  : QTextBlockFormat::MarkerType::Checked);
    c.setBlockFormat(bf);
    emit stateChanged();
    return true;
}

bool DocumentFormatter::continueList()
{
    QTextCursor c = cursor();
    if (c.isNull() || c.hasSelection()) return false;
    const QTextBlock blk = c.block();
    if (!blk.textList() || !blk.text().isEmpty()) return false;
    // Enter on an empty item: leave the list instead of adding another empty bullet.
    c.beginEditBlock();
    blk.textList()->remove(blk);
    QTextBlockFormat bf = blk.blockFormat();
    bf.setMarker(QTextBlockFormat::MarkerType::NoMarker);
    bf.setIndent(0);
    c.setBlockFormat(bf);
    c.endEditBlock();
    emit stateChanged();
    return true;
}

QString DocumentFormatter::markdown() const
{
    QTextDocument *d = doc();
    return d ? d->toMarkdown(QTextDocument::MarkdownDialectGitHub) : QString();
}

void DocumentFormatter::setMarkdown(const QString &md)
{
    QTextDocument *d = doc();
    if (!d) return;
    m_loading = true;
    d->setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
    // Page links look the way insertPageLink() makes them, whatever the importer chose.
    for (QTextBlock b = d->begin(); b.isValid(); b = b.next())
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.charFormat().anchorHref().startsWith(QLatin1String("lumen://"))) continue;
            QTextCursor c(d);
            c.setPosition(f.position());
            c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            QTextCharFormat look;
            look.setFontUnderline(true);
            look.setForeground(QGuiApplication::palette().highlight());
            c.mergeCharFormat(look);
        }
    m_loading = false;
    d->setIndentWidth(28);
    d->clearUndoRedoStacks();
    emit stateChanged();
}

QString DocumentFormatter::plainText() const
{
    QTextDocument *d = doc();
    return d ? d->toPlainText() : QString();
}

int DocumentFormatter::blockStart(int position) const
{
    QTextDocument *d = doc();
    return d ? d->findBlock(position).position() : 0;
}

int DocumentFormatter::wordCount() const
{
    static const QRegularExpression words(QStringLiteral("\\b\\w+\\b"));
    const QString t = plainText();
    int n = 0;
    for (auto it = words.globalMatch(t); it.hasNext(); it.next()) ++n;
    return n;
}

int DocumentFormatter::insertPageLink(int start, int end, const QString &title, const QString &url)
{
    QTextDocument *d = doc();
    if (!d || start < 0 || end < start) return end;
    QTextCursor c(d);
    c.beginEditBlock();
    c.setPosition(start);
    c.setPosition(end, QTextCursor::KeepAnchor);
    QTextCharFormat plain = c.charFormat();
    // Cleared, not set empty: an empty href still counts as a link to the Markdown writer.
    for (int p : {int(QTextFormat::IsAnchor), int(QTextFormat::AnchorHref), int(QTextFormat::AnchorName),
                  int(QTextFormat::TextUnderlineStyle), int(QTextFormat::ForegroundBrush)})
        plain.clearProperty(p);
    QTextCharFormat link = plain;
    link.setAnchor(true);
    link.setAnchorHref(url);
    link.setFontUnderline(true);
    link.setForeground(QGuiApplication::palette().highlight());   // the accent, as every link in the app
    c.removeSelectedText();
    c.insertText(title, link);
    c.insertText(QStringLiteral(" "), plain);
    c.endEditBlock();
    return c.position();
}

QString DocumentFormatter::textBefore(int position, int maxChars) const
{
    QTextDocument *d = doc();
    if (!d) return {};
    const QTextBlock block = d->findBlock(position);
    const int from = std::max(block.position(), position - maxChars);
    return block.text().mid(from - block.position(), position - from);
}

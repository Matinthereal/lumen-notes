#include "paperdetect.h"
#include <QDate>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace paperdetect {
namespace {

// Two words are on one printed line when they are of a size and share most of their height.
bool sameRow(const QRectF &a, const QRectF &b)
{
    const double lo = std::min(a.height(), b.height()), hi = std::max(a.height(), b.height());
    if (lo <= 0 || hi > lo * 1.6) return false;
    return std::min(a.bottom(), b.bottom()) - std::max(a.top(), b.top()) >= lo * 0.5;
}

// The words to the right of `from` on its row, nearest first.
QList<int> rightOf(const Page &page, int from)
{
    const QRectF box = page.words.at(from).box;
    QList<int> out;
    for (int j = 0; j < page.words.size(); ++j)
        if (j != from && page.words.at(j).box.left() > box.left() && sameRow(box, page.words.at(j).box)) out << j;
    std::sort(out.begin(), out.end(), [&](int a, int b) { return page.words.at(a).box.left() < page.words.at(b).box.left(); });
    return out;
}

int romanValue(const QString &s)
{
    static const QStringList numerals{"i", "ii", "iii", "iv", "v", "vi", "vii", "viii", "ix", "x", "xi", "xii"};
    return int(numerals.indexOf(s)) + 1;
}

// A label at the start of a printed line: "1", "1.", "7*", "01.1" (AQA prints each digit in its
// own box: "0 1 . 1"), "3. (a)", "(b)", "(ii)", "2(a)(i)".
struct Candidate {
    int page = 0;
    double x = 0, top = 0;
    int number = 0;            // 0: no question number on this line
    QString numberText;        // as printed: "01", "3"
    int sub = 0;               // AQA's part number: the 1 in "01.1"
    QStringList parts;         // "a", "ii", in order
};

QList<Candidate> candidates(const QList<Page> &pages)
{
    static const QRegularExpression piece(R"(^[0-9a-z().*]{1,12}$)");
    static const QRegularExpression label(R"(^(?:(\d{1,2})(?:\.(\d{1,2}))?[.)]?)?\*?((?:\((?:[a-z]|[ivx]{1,4})\)\*?)*)\.?$)");
    static const QRegularExpression part(R"(\(([a-z]+)\))");
    QList<Candidate> out;
    for (int p = 0; p < pages.size(); ++p) {
        const Page &page = pages.at(p);
        QList<int> starts;
        for (int i = 0; i < page.words.size(); ++i) {
            const Word &w = page.words.at(i);
            if (w.box.left() < page.size.width() * 0.25 && !w.text.isEmpty() && (w.text.at(0).isDigit() || w.text.at(0) == u'('))
                starts << i;
        }
        std::sort(starts.begin(), starts.end(), [&](int a, int b) { return page.words.at(a).box.left() < page.words.at(b).box.left(); });
        QSet<int> used;
        for (const int s : std::as_const(starts)) {
            if (used.contains(s)) continue;
            const QRectF box = page.words.at(s).box;
            // Something printed just before it ("Question 1 continued") means it is not a label.
            bool afterText = false;
            for (const Word &o : page.words)
                if (o.box.right() <= box.left() + 1 && box.left() - o.box.right() < 12 && sameRow(box, o.box)) { afterText = true; break; }
            if (afterText) continue;
            // The label's pieces, then the longest run of them that reads as one label.
            QList<int> tokens{s};
            double right = box.right();
            for (const int j : rightOf(page, s)) {
                const Word &w = page.words.at(j);
                if (w.box.left() - right > 40 || !piece.match(w.text).hasMatch()) break;
                tokens << j;
                right = w.box.right();
            }
            QRegularExpressionMatch best;
            int bestLen = 0;
            QString joined;
            for (int k = 0; k < tokens.size(); ++k) {
                joined += page.words.at(tokens.at(k)).text;
                const auto m = label.match(joined);
                if (m.hasMatch() && (!m.captured(1).isEmpty() || !m.captured(3).isEmpty())) { best = m; bestLen = k + 1; }
            }
            if (!bestLen) continue;
            Candidate c;
            c.page = p;
            c.x = box.left();
            c.top = box.top();
            c.numberText = best.captured(1);
            c.number = c.numberText.toInt();
            c.sub = best.captured(2).toInt();
            for (auto it = part.globalMatch(best.captured(3)); it.hasNext();) c.parts << it.next().captured(1);
            if (!c.numberText.isEmpty() && c.number < 1) continue;          // a "0" is never a question
            for (int k = 0; k < bestLen; ++k) used.insert(tokens.at(k));
            out << c;
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const Candidate &a, const Candidate &b) {
        return a.page != b.page ? a.page < b.page : a.top != b.top ? a.top < b.top : a.x < b.x;
    });
    return out;
}

// Question numbers are printed down one column and count up by one. Of every column of numbers
// on the pages, the one holding the longest such run is the paper's; the rest are page numbers,
// numbered answer lines, table cells and axis labels.
QSet<int> questionNumbers(const QList<Candidate> &cands)
{
    QList<int> numbered;
    for (int i = 0; i < cands.size(); ++i) if (cands.at(i).number) numbered << i;
    QList<int> byX = numbered;
    std::sort(byX.begin(), byX.end(), [&](int a, int b) { return cands.at(a).x < cands.at(b).x; });
    QList<QList<int>> columns;
    double columnX = -1e9;
    for (const int i : std::as_const(byX)) {
        if (cands.at(i).x > columnX + 10) { columns.append(QList<int>()); columnX = cands.at(i).x; }
        columns.last() << i;
    }
    const auto follows = [&](const Candidate &a, const Candidate &b) {
        if (b.number == a.number + 1) return b.sub <= 1;
        if (b.number != a.number) return false;
        if (b.sub) return b.sub == a.sub + 1;                     // AQA: 01.1 → 01.2
        return !a.sub && !b.parts.isEmpty();                      // "2" → "2(a)" → "2(b)", numbered each time
    };
    QList<int> best;
    for (QList<int> col : std::as_const(columns)) {
        std::sort(col.begin(), col.end());                        // reading order
        QList<int> len(col.size(), 1), prev(col.size(), -1);
        for (int b = 0; b < col.size(); ++b)
            for (int a = 0; a < b; ++a)
                if (len.at(a) + 1 > len.at(b) && follows(cands.at(col.at(a)), cands.at(col.at(b)))) { len[b] = len.at(a) + 1; prev[b] = a; }
        int end = -1;
        for (int k = 0; k < col.size(); ++k) if (end < 0 || len.at(k) > len.at(end)) end = k;
        QList<int> chain;
        for (int k = end; k >= 0; k = prev.at(k)) chain.prepend(col.at(k));
        if (chain.size() > best.size()) best = chain;
    }
    return QSet<int>(best.cbegin(), best.cend());
}

struct Label { QString text; int page = 0; double top = 0; int marks = 0; };

// Read the lines top to bottom, keeping track of question, part and sub-part, and take a line as a
// label only when it is the next one in sequence. "(i)" is the letter after "(h)", otherwise a
// roman numeral.
QList<Label> labels(const QList<Candidate> &cands, const QSet<int> &numbers)
{
    QList<Label> out;
    QString number;
    int sub = 0;
    QChar letter;
    QString roman;
    for (int i = 0; i < cands.size(); ++i) {
        const Candidate &c = cands.at(i);
        bool isLabel = false;
        if (c.number) {
            if (!numbers.contains(i)) continue;
            if (c.numberText.toInt() != number.toInt()) { number = c.numberText; sub = 0; letter = QChar(); roman.clear(); }
            if (c.sub) { sub = c.sub; letter = QChar(); roman.clear(); }
            isLabel = true;
        } else if (number.isEmpty()) {
            continue;
        }
        for (const QString &p : c.parts) {
            const bool asLetter = p.size() == 1 && p.at(0) == (letter.isNull() ? QChar(u'a') : QChar(letter.unicode() + 1));
            const int r = romanValue(p);
            const bool asRoman = r > 0 && r == romanValue(roman) + 1;
            if (asLetter && !(asRoman && !roman.isEmpty())) { letter = p.at(0); roman.clear(); }
            else if (asRoman) roman = p;
            else break;
            isLabel = true;
        }
        if (!isLabel) continue;
        QString text = number;
        if (sub) text += u'.' + QString::number(sub);
        if (!letter.isNull()) text += u'(' + QString(letter) + u')';
        if (!roman.isEmpty()) text += u'(' + roman + u')';
        out.append({text, c.page, c.top, 0});
    }
    return out;
}

// Marks at the right-hand side: "[3]", "[2 marks]", "(3 marks)", or a bare "(3)" ending its line.
struct Mark { int page; double y; int marks; };

QList<Mark> marks(const QList<Page> &pages)
{
    static const QRegularExpression bracket(R"(^\[(\d{1,2})\]$)");
    static const QRegularExpression bracketOpen(R"(^\[(\d{1,2})$)");
    static const QRegularExpression bracketWord(R"(^marks?\]$)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression paren(R"(^\((\d{1,2})\)$)");
    static const QRegularExpression parenOpen(R"(^\((\d{1,2})$)");
    static const QRegularExpression parenWord(R"(^marks?\)[.,;]?$)", QRegularExpression::CaseInsensitiveOption);
    QList<Mark> out;
    for (int p = 0; p < pages.size(); ++p) {
        const Page &page = pages.at(p);
        const double pw = page.size.width();
        for (int i = 0; i < page.words.size(); ++i) {
            const Word &w = page.words.at(i);
            if (w.box.left() < pw * 0.5 || (!w.text.startsWith(u'[') && !w.text.startsWith(u'('))) continue;
            const QList<int> after = rightOf(page, i);
            const Word *next = after.isEmpty() ? nullptr : &page.words.at(after.first());
            const bool nextClose = next && next->box.left() - w.box.right() < 20;
            int n = 0;
            if (const auto m = bracket.match(w.text); m.hasMatch()) n = m.captured(1).toInt();
            else if (const auto m = bracketOpen.match(w.text); m.hasMatch() && nextClose && bracketWord.match(next->text).hasMatch()) n = m.captured(1).toInt();
            else if (const auto m = parenOpen.match(w.text); m.hasMatch() && nextClose && parenWord.match(next->text).hasMatch()) n = m.captured(1).toInt();
            else if (const auto m = paren.match(w.text); m.hasMatch() && w.box.left() > pw * 0.7 && !(next && next->box.left() - w.box.right() < 40)) n = m.captured(1).toInt();
            if (n > 0) out.append({p, w.box.center().y(), n});
        }
    }
    return out;
}

int plausible(int year) { return year >= 1990 && year <= QDate::currentDate().year() + 1 ? year : 0; }

int firstYear(const QRegularExpression &re, const QString &text, int base = 0)
{
    for (auto it = re.globalMatch(text); it.hasNext();)
        if (const int y = plausible(base + it.next().captured(1).toInt())) return y;
    return 0;
}

const char *const Months = "jan(?:uary)?|feb(?:ruary)?|mar(?:ch)?|apr(?:il)?|may|june?|july?|aug(?:ust)?|sep(?:t(?:ember)?)?"
                           "|oct(?:ober)?|nov(?:ember)?|dec(?:ember)?|summer|autumn|winter|spring";

const QRegularExpression &datedYear()
{
    static const QRegularExpression re(QStringLiteral(R"((?<![a-z])(?:%1)[\s_.-]*((?:19|20)\d\d)(?!\d))").arg(QLatin1String(Months)),
                                       QRegularExpression::CaseInsensitiveOption);
    return re;
}

} // namespace

Page fromWorker(const QJsonArray &words, const QSizeF &size)
{
    Page page{size, {}};
    page.words.reserve(words.size());
    for (const QJsonValue &v : words) {
        const QJsonArray w = v.toArray();
        page.words.append({QRectF(QPointF(w.at(0).toDouble(), w.at(1).toDouble()), QPointF(w.at(2).toDouble(), w.at(3).toDouble())), w.at(4).toString()});
    }
    return page;
}

QList<Question> detect(const QList<Page> &pages)
{
    const QList<Candidate> cands = candidates(pages);
    QList<Label> found = labels(cands, questionNumbers(cands));
    if (found.isEmpty()) return {};
    // Each mark belongs to the last label printed before it, across page breaks too.
    for (const Mark &m : marks(pages)) {
        for (int k = int(found.size()) - 1; k >= 0; --k) {
            const Label &l = found.at(k);
            if (l.page < m.page || (l.page == m.page && l.top <= m.y)) { found[k].marks += m.marks; break; }
        }
    }
    QList<Question> out;
    for (int k = 0; k < found.size(); ++k) {
        const Label &l = found.at(k);
        const QSizeF size = pages.at(l.page).size;
        const double top = l.top - 4;
        const double bottom = k + 1 < found.size() && found.at(k + 1).page == l.page ? found.at(k + 1).top - 4 : size.height() - 30;
        out.append({l.text, l.page, QRectF(size.width() * 0.05, top, size.width() * 0.9, std::max(20.0, bottom - top)), l.marks});
    }
    // A stem with no marks of its own ("1" above "1(a)", AQA's "01" above "01.1") is not a question
    // to answer: its first part takes over its region when they share a page.
    QList<Question> kept;
    for (int k = 0; k < out.size(); ++k) {
        const Question &q = out.at(k);
        if (q.marks == 0 && k + 1 < out.size()) {
            Question &next = out[k + 1];
            const bool stem = next.label.size() > q.label.size() && next.label.startsWith(q.label)
                              && (next.label.at(q.label.size()) == u'(' || next.label.at(q.label.size()) == u'.');
            if (stem) {
                if (next.page == q.page) next.rect.setTop(std::min(next.rect.top(), q.rect.top()));
                continue;
            }
        }
        kept.append(q);
    }
    return kept;
}

int yearFromCover(const QString &text) { return firstYear(datedYear(), text); }

int yearFromName(const QString &fileName)
{
    static const QRegularExpression bare(R"((?<!\d)((?:19|20)\d\d)(?!\d))");
    static const QRegularExpression compact(R"((?<!\d)((?:19|20)\d\d)(?:0[1-9]|1[0-2])(?:[0-2]\d|3[01])(?!\d))");    // 20190515
    static const QRegularExpression shortYear(QStringLiteral(R"((?<![a-z])(?:%1)[\s_.-]*(\d\d)(?!\d))").arg(QLatin1String(Months)),
                                              QRegularExpression::CaseInsensitiveOption);            // QP-JUN16
    if (const int y = firstYear(datedYear(), fileName)) return y;
    if (const int y = firstYear(bare, fileName)) return y;
    if (const int y = firstYear(compact, fileName)) return y;
    return firstYear(shortYear, fileName, 2000);
}

}

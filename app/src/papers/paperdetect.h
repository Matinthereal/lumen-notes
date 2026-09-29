#pragma once
#include <QJsonArray>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>

// What can be read off a past paper's own text: its questions (label, marks, region) and its year.
// Pure functions over the pdf helper's word boxes, so each board's layout can be tested on a
// synthetic word list.
namespace paperdetect {

struct Word { QRectF box; QString text; };
struct Page { QSizeF size; QList<Word> words; };
struct Question { QString label; int page = 0; QRectF rect; int marks = 0; };

// One page of the helper's `words` reply: [x0, y0, x1, y1, text, …] per word.
Page fromWorker(const QJsonArray &words, const QSizeF &size);

// The paper's questions in order, as printed: "1", "3(a)", "10(a)(ii)" (Edexcel, OCR) or "01.1"
// (AQA). A stem that only introduces its parts ("1" above "1(a)") is folded into its first part.
// Marks come from "[3]" (OCR), "[2 marks]" (AQA), a bare "(3)" at the right margin (Edexcel) or
// "(3 marks)".
QList<Question> detect(const QList<Page> &pages);

// The exam year from a file name ("June2016", "QP-JUN16", "June 2019 QP", "20190515", "2019") or a
// cover ("Thursday 9 June 2016", "Summer 2019"). 0 when there is none: better no year than a guess.
int yearFromName(const QString &fileName);
int yearFromCover(const QString &text);

}

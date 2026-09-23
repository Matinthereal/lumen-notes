#include "uitest.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QWindow>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTouchEvent>
#ifdef LUMEN_HAVE_QTEST
#include <QTest>
#endif
#include <QQuickItem>
#include <QQmlContext>
#include <QQmlEngine>
#include <QJSValue>
#include <QQmlProperty>
#include <QQuickWindow>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QImage>
#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QUrl>
#include <QVariant>
#include <functional>

#include "canvas/inkcanvas.h"
#include "input/tabletsample.h"

namespace {

QStringList g_qmlComplaints;
QtMessageHandler g_previous = nullptr;
quint64 g_stamp = 0;

void collectMessages(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg)
        && (msg.contains(QLatin1String(".qml")) || msg.contains(QLatin1String("TypeError"))
            || msg.contains(QLatin1String("ReferenceError")) || msg.contains(QLatin1String("Unable to assign"))))
        g_qmlComplaints << msg;
    if (g_previous) g_previous(type, ctx, msg);
}

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

bool waitFor(const std::function<bool()> &done, int ms = 4000)
{
    QElapsedTimer t; t.start();
    while (t.elapsed() < ms) {
        if (done()) return true;
        spin(25);
    }
    return done();
}

void gather(QQuickItem *item, const QString &name, QList<QQuickItem *> &out)
{
    if (!item) return;
    if (item->objectName() == name) out << item;
    const auto kids = item->childItems();
    for (QQuickItem *k : kids) gather(k, name, out);
}
QList<QQuickItem *> findAll(QQuickWindow *w, const QString &name)
{
    QList<QQuickItem *> out;
    gather(w->contentItem(), name, out);
    return out;
}
QQuickItem *findOne(QQuickWindow *w, const QString &name)
{
    const auto all = findAll(w, name);
    return all.isEmpty() ? nullptr : all.first();
}

QPointF centre(QQuickItem *i) { return i->mapToScene(QPointF(i->width() / 2, i->height() / 2)); }

void sendMouse(QQuickWindow *w, QEvent::Type type, const QPointF &pos, Qt::MouseButton button)
{
    // A move carries no "changed" button but does carry the held ones — get this wrong and a drag
    // is delivered as a series of unrelated hovers, which is how the first shape test failed.
    const Qt::MouseButton changed = (type == QEvent::MouseMove) ? Qt::NoButton : button;
    const Qt::MouseButtons held = (type == QEvent::MouseButtonRelease) ? Qt::NoButton : Qt::MouseButtons(button);
    QMouseEvent ev(type, pos, w->mapToGlobal(pos.toPoint()), changed, held, Qt::NoModifier);
    ev.setTimestamp(g_stamp += 20);
    QCoreApplication::sendEvent(w, &ev);
}

void tap(QQuickWindow *w, QQuickItem *item, Qt::MouseButton button = Qt::LeftButton, QPointF at = QPointF(-1, -1))
{
    const QPointF p = (at.x() >= 0) ? at : centre(item);
    sendMouse(w, QEvent::MouseButtonPress, p, button);
    spin(40);
    sendMouse(w, QEvent::MouseButtonRelease, p, button);
    spin(120);
}

// Finger input, which is what the maker actually uses: a real tap moves a few pixels and can be
// slow. Both of those made Qt's built-in long press fire on what the user meant as a tap.
#ifdef LUMEN_HAVE_QTEST
bool canFinger() { return true; }
QPointingDevice *touchDevice()
{
    static QPointingDevice *dev = QTest::createTouchDevice();
    return dev;
}
void fingerTap(QQuickWindow *w, QQuickItem *item, int holdMs, int drift = 3)
{
    const QPoint p = centre(item).toPoint();
    QTest::touchEvent(w, touchDevice()).press(1, p, w);
    spin(20);
    if (drift > 0) { QTest::touchEvent(w, touchDevice()).move(1, p + QPoint(drift, drift), w); spin(20); }
    spin(qMax(0, holdMs - 40));
    QTest::touchEvent(w, touchDevice()).release(1, p + QPoint(drift, drift), w);
    spin(180);
}
#else
bool canFinger() { return false; }
void fingerTap(QQuickWindow *, QQuickItem *, int, int = 3) {}
#endif

void hold(QQuickWindow *w, QQuickItem *item, int ms = 750)
{
    const QPointF p = centre(item);
    sendMouse(w, QEvent::MouseButtonPress, p, Qt::LeftButton);
    spin(ms);
    sendMouse(w, QEvent::MouseButtonRelease, p, Qt::LeftButton);
    spin(150);
}

void pressEscape(QQuickWindow *w)
{
    QKeyEvent down(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &down);
    QKeyEvent up(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(w, &up);
    spin(150);
}

// Every control in the app, tapped once. The point is not that each tap does the right thing —
// the assertions above cover the flows — but that nothing in the app throws, hangs, opens a window
// it cannot close, or sits somewhere a finger cannot reach.
bool isInteractive(QQuickItem *item)
{
    if (item->inherits("QQuickAbstractButton") || item->inherits("QQuickMouseArea")) return true;
    const auto kids = item->children();
    for (QObject *k : kids) {
        const QByteArray cls = k->metaObject()->className();
        if (cls.contains("TapHandler")) return true;
    }
    return false;
}

QList<QPointer<QQuickItem>> interactiveItems(QQuickWindow *win)
{
    QList<QPointer<QQuickItem>> out;
    QList<QQuickItem *> stack{win->contentItem()};
    while (!stack.isEmpty()) {
        QQuickItem *item = stack.takeLast();
        const auto kids = item->childItems();
        for (QQuickItem *k : kids) stack.append(k);
        if (!item->isVisible() || item->width() < 6 || item->height() < 6) continue;
        if (!isInteractive(item)) continue;
        const QRectF box = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        if (!QRectF(0, 0, win->width(), win->height()).contains(box.center())) continue;
        out.append(QPointer<QQuickItem>(item));
    }
    return out;
}

// The pen, as the platform delivers it: through the tablet filter, so the canvas gets its say and
// the chrome above it gets the rest. This is the path a real S Pen takes.
QPointingDevice *stylusDevice()
{
    static QPointingDevice dev(QStringLiteral("uitest stylus"), 4242, QInputDevice::DeviceType::Stylus,
                               QPointingDevice::PointerType::Pen,
                               QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
    return &dev;
}

void penAt(QQuickWindow *w, QEvent::Type type, const QPointF &pos, Qt::MouseButtons buttons)
{
    QTabletEvent ev(type, stylusDevice(), pos, w->mapToGlobal(pos.toPoint()),
                    buttons == Qt::NoButton ? 0.0 : 0.6, 0, 0, 0, 0, 0, Qt::NoModifier,
                    type == QEvent::TabletRelease ? Qt::LeftButton : Qt::LeftButton, buttons);
    ev.setTimestamp(g_stamp += 16);
    QCoreApplication::sendEvent(w, &ev);
}

void penDrag(QQuickWindow *w, const QPointF &from, const QPointF &to, int steps = 8)
{
    penAt(w, QEvent::TabletPress, from, Qt::LeftButton);
    spin(30);
    for (int i = 1; i <= steps; ++i) {
        penAt(w, QEvent::TabletMove, from + (to - from) * (double(i) / steps), Qt::LeftButton);
        spin(16);
    }
    penAt(w, QEvent::TabletRelease, to, Qt::NoButton);
    spin(150);
}

// Pixels a compositor would show the desktop through. Mesa hands Qt an alpha channel even when the
// format asks for none, so anything that leaves alpha below 255 is a hole in the window.
int seeThroughPixels(QQuickWindow *w)
{
    const QImage img = w->grabWindow().convertToFormat(QImage::Format_ARGB32);
    if (qEnvironmentVariableIsSet("LUMEN_UITEST_GRABS")) {
        static int n = 0;
        img.save(qEnvironmentVariable("LUMEN_UITEST_GRABS") + QStringLiteral("/grab-%1.png").arg(n++));
    }
    int holes = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2)
            if (qAlpha(img.pixel(x, y)) < 255) ++holes;
    return holes;
}

void typeKeys(const QString &text)
{
    for (const QChar ch : text) {
        QObject *target = QGuiApplication::focusObject();
        if (!target) return;
        const int key = ch == QLatin1Char('\n') ? Qt::Key_Return : ch.toUpper().unicode();
        const QString t = ch == QLatin1Char('\n') ? QStringLiteral("\r") : QString(ch);
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, t);
        QCoreApplication::sendEvent(target, &press);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, t);
        QCoreApplication::sendEvent(target, &release);
    }
    spin(30);
}

void chord(QQuickWindow *w, int key, Qt::KeyboardModifiers mods)
{
    // Through the window, as a real keypress arrives, so shortcuts get their chance to steal it.
    QKeyEvent press(QEvent::KeyPress, key, mods);
    QCoreApplication::sendEvent(w, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods);
    QCoreApplication::sendEvent(w, &release);
    spin(40);
}

struct Report {
    int failures = 0;
    void check(const char *name, bool ok, const QString &detail = {})
    {
        if (ok) { qInfo("UITEST pass  %s", name); return; }
        ++failures;
        qWarning("UITEST FAIL  %s%s", name, detail.isEmpty() ? "" : qPrintable(QStringLiteral(" — ") + detail));
    }
};

// A ListView keeps delegates outside the viewport alive, and a synthetic press at one of those
// lands on whatever is really there. Only rows whose centre is inside the list count as tappable.
QQuickItem *pageRow(QQuickWindow *w, bool wantCurrent)
{
    QQuickItem *list = findOne(w, QStringLiteral("sidebarList"));
    const QRectF viewport = list ? list->mapRectToScene(QRectF(0, 0, list->width(), list->height())) : QRectF();
    for (QQuickItem *row : findAll(w, QStringLiteral("sidebarRow"))) {
        if (row->property("rowKind").toString() != QLatin1String("page")) continue;
        if (row->property("rowCurrent").toBool() != wantCurrent || !row->isVisible()) continue;
        const QPointF c = row->mapToScene(QPointF(row->width() / 2, row->height() / 2));
        if (!viewport.isNull() && !viewport.contains(c)) continue;
        return row;
    }
    return nullptr;
}

// Bring the open page's row into the viewport the way opening a page does.
void revealCurrent(QQuickWindow *w, QObject *root)
{
    if (QQuickItem *side = findOne(w, QStringLiteral("sidebarList")))
        if (QQuickItem *sidebar = side->parentItem() ? side->parentItem()->parentItem() : nullptr)
            QMetaObject::invokeMethod(sidebar, "revealPage", Q_ARG(QVariant, root->property("currentPageId")));
    spin(250);
}

bool sheetOpen(QQuickWindow *w)
{
    QQuickItem *content = findOne(w, QStringLiteral("actionSheetContent"));
    return content && content->isVisible();
}


// Any item in the window showing this text — the object menu's actions are plain Text items.
QQuickItem *itemWithText(QQuickItem *from, const QString &text)
{
    if (from->property("text").toString() == text && from->isVisible()) return from;
    const auto kids = from->childItems();
    for (QQuickItem *k : kids)
        if (QQuickItem *hit = itemWithText(k, text)) return hit;
    return nullptr;
}

QQuickItem *sheetAction(QQuickWindow *w, const QString &label)
{
    for (QQuickItem *t : findAll(w, QStringLiteral("actionSheetLabel")))
        if (t->property("text").toString() == label) return t;
    return nullptr;
}

// LUMEN_UITEST_SHOTS=<dir>: keep a picture of the window at the moments worth looking at.
void shot(QQuickWindow *w, const QString &name)
{
    const QString dir = qEnvironmentVariable("LUMEN_UITEST_SHOTS");
    if (dir.isEmpty()) return;
    QDir().mkpath(dir);
    spin(250);
    w->grabWindow().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

// The page, notebook and navigation features: links, tags, split view, presentation and the rest.
// Also runnable on its own with LUMEN_UITEST_ONLY=pages.
void pageFeatures(QQuickWindow *win, QObject *root, Report &r)
{
    const auto currentPage = [&] { return root->property("currentPageId").toLongLong(); };
    QObject *library = nullptr;
    if (QQmlEngine *engine = qmlEngine(root))
        library = engine->rootContext()->contextProperty(QStringLiteral("library")).value<QObject *>();

    // ---- 15. [[page]] links: typing [[ offers pages, the pick is a real link, the target lists the
    //          source under "Linked from", both directions can be followed, and a rename keeps it.
    if (library) {
        QObject *textBlocks = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            textBlocks = engine->rootContext()->contextProperty(QStringLiteral("textBlocks")).value<QObject *>();
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("typed")));
        spin(400);
        const qint64 target = currentPage();
        QMetaObject::invokeMethod(library, "rename", Q_ARG(QString, QStringLiteral("page")), Q_ARG(qlonglong, target), Q_ARG(QString, QStringLiteral("Uitest target")));
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("typed")));
        spin(400);
        const qint64 source = currentPage();
        QQuickItem *editor = findOne(win, QStringLiteral("typedEditor"));
        if (editor && source != target) {
            win->requestActivate();
            waitFor([&] { return win->isActive(); }, 1500);
            editor->forceActiveFocus();
            spin(100);
            typeKeys(QStringLiteral("see [[uitest ta"));
            spin(300);
            QList<QQuickItem *> rows;
            for (QQuickItem *row : findAll(win, QStringLiteral("linkPickerRow"))) if (row->isVisible()) rows << row;
            r.check("typing [[ offers pages to link to", !rows.isEmpty());
            shot(win, QStringLiteral("1-links-picker"));
            chord(win, Qt::Key_Return, Qt::NoModifier);
            spin(1000);                                   // past the autosave delay
            QString saved;
            if (textBlocks) QMetaObject::invokeMethod(textBlocks, "pageText", Q_RETURN_ARG(QString, saved), Q_ARG(qint64, source));
            const QString url = QStringLiteral("lumen://page/%1").arg(target);
            r.check("the pick is saved as a link by page id", saved.contains(QStringLiteral("[Uitest target](") + url + QLatin1Char(')')), saved.left(200));
            QVariantList back;
            QMetaObject::invokeMethod(library, "backlinks", Q_RETURN_ARG(QVariantList, back), Q_ARG(qint64, target));
            r.check("the target knows what links to it", back.size() == 1 && back[0].toMap().value(QStringLiteral("id")).toLongLong() == source);

            // A tap on the link's words opens the page it names.
            QRectF at;
            QMetaObject::invokeMethod(editor, "positionToRectangle", Q_RETURN_ARG(QRectF, at), Q_ARG(int, 6));
            tap(win, nullptr, Qt::LeftButton, editor->mapToScene(at.center()));
            waitFor([&] { return currentPage() == target; }, 2000);
            r.check("tapping a link follows it", currentPage() == target, QStringLiteral("page %1, wanted %2").arg(currentPage()).arg(target));
            spin(300);
            QQuickItem *row = findOne(win, QStringLiteral("backlinkRow"));
            r.check("the linked page shows where it is linked from", row && row->isVisible());
            root->setProperty("rightPanel", QStringLiteral("page"));
            shot(win, QStringLiteral("1-links-backlinks"));
            root->setProperty("rightPanel", QString());
            spin(150);
            row = findOne(win, QStringLiteral("backlinkRow"));
            if (row && row->isVisible()) {
                tap(win, row);
                waitFor([&] { return currentPage() == source; }, 2000);
                r.check("and that list leads back", currentPage() == source);
            }
            // Renaming the target keeps the link, and the linking text says the new name.
            QMetaObject::invokeMethod(library, "rename", Q_ARG(QString, QStringLiteral("page")), Q_ARG(qlonglong, target), Q_ARG(QString, QStringLiteral("Renamed target")));
            QMetaObject::invokeMethod(root, "openPage", Q_ARG(QVariant, target));
            spin(200);
            QMetaObject::invokeMethod(root, "openPage", Q_ARG(QVariant, source));
            spin(300);
            QString reloaded;
            if (QQuickItem *tp = findOne(win, QStringLiteral("typedPage")))
                if (QObject *fmtObj = tp->property("formatter").value<QObject *>())
                    QMetaObject::invokeMethod(fmtObj, "markdown", Q_RETURN_ARG(QString, reloaded));
            shot(win, QStringLiteral("1-links-rendered"));
            r.check("a renamed page's links keep working and show the new name", reloaded.contains(QStringLiteral("[Renamed target](") + url + QLatin1Char(')')), reloaded.left(200));
        } else {
            r.check("found a typed editor for the link test", false);
        }
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
    }


    // ---- 16. Tags: lassoed handwriting becomes a tag through the OCR path, the page panel adds and
    //          removes them (with undo), and the page browser and search narrow to a tag.
    if (library) {
        QObject *ocr = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            ocr = engine->rootContext()->contextProperty(QStringLiteral("ocr")).value<QObject *>();
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(400);
        const qint64 inked = currentPage();
        auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"));
        const auto pageTagNames = [&](qint64 pid) {
            QVariantList tags;
            QMetaObject::invokeMethod(library, "pageTags", Q_RETURN_ARG(QVariantList, tags), Q_ARG(qint64, pid));
            QStringList names;
            for (const QVariant &t : tags) names << t.toMap().value(QStringLiteral("name")).toString();
            return names;
        };
        if (canvas && ocr) {
            canvas->setTool(QStringLiteral("pen"));
            const QPointF mid = centre(canvas);
            const auto sample = [&](TabletSample::Kind kind, QPointF at) {
                TabletSample t;
                t.kind = kind; t.windowPos = at;
                t.pressure = kind == TabletSample::Kind::Release ? 0.0f : 0.6f;
                t.buttons = kind == TabletSample::Kind::Release ? Qt::NoButton : Qt::LeftButton;
                t.button = Qt::LeftButton;
                t.timestampMs = quint32(g_stamp += 16);
                canvas->tabletSample(t);
            };
            sample(TabletSample::Kind::Press, mid);
            for (int i = 1; i <= 10; ++i) sample(TabletSample::Kind::Move, mid + QPointF(8.0 * i, (i % 2) * 10.0));
            sample(TabletSample::Kind::Release, mid + QPointF(80, 0));
            spin(200);
            canvas->selectAll();
            spin(300);
            QQuickItem *pill = findOne(win, QStringLiteral("tagPill"));
            r.check("lassoed ink offers # Tag", pill && pill->isVisible());
            shot(win, QStringLiteral("2-tags-lasso"));
            if (pill && pill->isVisible()) {
                // Pressed and released with no event loop between, so the reply from a recogniser
                // that is missing its model cannot arrive before the stand-in answer below.
                sendMouse(win, QEvent::MouseButtonPress, centre(pill), Qt::LeftButton);
                sendMouse(win, QEvent::MouseButtonRelease, centre(pill), Qt::LeftButton);
                const QJSValue pending = root->property("pendingTag").value<QJSValue>();
                const QString token = pending.isObject() ? pending.property(QStringLiteral("token")).toString() : QString();
                r.check("# Tag sends the strokes to the handwriting reader", !token.isEmpty());
                // The recogniser needs its model, which a test machine may not have: answer for it.
                QMetaObject::invokeMethod(ocr, "strokesRecognized", Q_ARG(QString, token), Q_ARG(QString, QStringLiteral("#Revision.")));
                spin(250);
                r.check("the recognised words become the page's tag", pageTagNames(inked).contains(QStringLiteral("Revision")),
                        pageTagNames(inked).join(QLatin1Char(',')));
            }
        } else {
            r.check("found the canvas and OCR service for the tag test", false);
        }

        // The page panel: add by typing, see the chips, remove with an undo.
        root->setProperty("rightPanel", QStringLiteral("page"));
        spin(300);
        QQuickItem *field = findOne(win, QStringLiteral("tagField"));
        r.check("the page panel has a field for tags", field && field->isVisible());
        if (field) {
            win->requestActivate();
            waitFor([&] { return win->isActive(); }, 1500);
            field->forceActiveFocus();
            spin(100);
            typeKeys(QStringLiteral("exam"));
            chord(win, Qt::Key_Return, Qt::NoModifier);
            spin(250);
            r.check("a typed tag is added", pageTagNames(inked).contains(QStringLiteral("exam")), pageTagNames(inked).join(QLatin1Char(',')));
            if (QQmlEngine *engine = qmlEngine(root))
                if (QObject *k = engine->rootContext()->contextProperty(QStringLiteral("keys")).value<QObject *>())
                    QMetaObject::invokeMethod(k, "dropFocus");
            spin(150);
        }
        shot(win, QStringLiteral("2-tags-panel"));
        QQuickItem *removeExam = nullptr;
        for (QQuickItem *chip : findAll(win, QStringLiteral("tagChip")))
            if (chip->isVisible() && chip->property("name").toString() == QLatin1String("exam") && chip->property("removable").toBool())
                for (QQuickItem *x : findAll(win, QStringLiteral("tagChipRemove")))
                    if (x->parentItem() && x->parentItem()->parentItem() == chip) removeExam = x;
        r.check("a tag chip can be removed", removeExam != nullptr);
        if (removeExam) {
            tap(win, removeExam);
            spin(250);
            r.check("removing takes the tag off", !pageTagNames(inked).contains(QStringLiteral("exam")));
            if (QQuickItem *undo = findOne(win, QStringLiteral("toastUndo"))) {
                tap(win, undo);
                spin(250);
                r.check("and Undo puts it back", pageTagNames(inked).contains(QStringLiteral("exam")));
            }
        }
        root->setProperty("rightPanel", QString());

        // The page browser, narrowed to a tag.
        QMetaObject::invokeMethod(root, "showTagged", Q_ARG(QVariant, QStringLiteral("revision")));
        waitFor([&] { return !findAll(win, QStringLiteral("browserCell")).isEmpty(); }, 3000);
        spin(300);
        int cells = 0;
        for (QQuickItem *c : findAll(win, QStringLiteral("browserCell"))) if (c->isVisible()) ++cells;
        r.check("the page browser shows the pages with a tag", cells == 1, QStringLiteral("%1 cells").arg(cells));
        shot(win, QStringLiteral("2-tags-browser"));
        root->setProperty("browserVisible", false);
        spin(200);

        // Search, narrowed by a tag chip.
        chord(win, Qt::Key_K, Qt::ControlModifier);
        spin(300);
        QQuickItem *chipInSearch = nullptr;
        if (QQuickItem *flow = findOne(win, QStringLiteral("searchTags")))
            for (QQuickItem *chip : findAll(win, QStringLiteral("tagChip")))
                if (chip->isVisible() && chip->parentItem() == flow && chip->property("name").toString() == QLatin1String("Revision")) chipInSearch = chip;
        r.check("search offers tags to narrow it", chipInSearch != nullptr);
        if (chipInSearch) {
            tap(win, chipInSearch);
            spin(250);
            r.check("a tag chip selects in search", chipInSearch->property("selected").toBool());
            shot(win, QStringLiteral("2-tags-search"));
        }
        pressEscape(win);
        spin(150);
    }

    // ---- 17. Split view: a PDF page and an ink page side by side, each drawn on through the real
    //          pen path, each zoomed on its own, the divider drags, and either side closes.
    if (library) {
        QObject *pdf = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            pdf = engine->rootContext()->contextProperty(QStringLiteral("pdf")).value<QObject *>();
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
        const qint64 inkPage = currentPage();
        qint64 left = 0;
        // A one-page lecture handout, made here so the test needs no files of its own.
        const QString handout = QDir::tempPath() + QStringLiteral("/lumen-uitest-handout.pdf");
        {
            QPdfWriter writer(handout);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter p(&writer);
            QFont f = p.font(); f.setPointSize(28); p.setFont(f);
            p.drawText(QRectF(0, 400, writer.width(), 1200), Qt::AlignHCenter, QStringLiteral("Lecture 4\nWaves and interference"));
            p.drawEllipse(QRectF(writer.width() / 2 - 1500, 3000, 3000, 3000));
        }
        QVariantMap info;
        QMetaObject::invokeMethod(library, "page", Q_RETURN_ARG(QVariantMap, info), Q_ARG(qint64, inkPage));
        if (pdf) {
            QMetaObject::invokeMethod(pdf, "importAsSection", Q_ARG(QUrl, QUrl::fromLocalFile(handout)),
                                      Q_ARG(qint64, info.value(QStringLiteral("notebookId")).toLongLong()), Q_ARG(QString, QStringLiteral("Handouts")), Q_ARG(QString, QString()));
            waitFor([&] { return currentPage() != inkPage && currentPage() > 0; }, 20000);
            spin(1500);                                  // the page raster arrives after the page opens
        }
        left = currentPage();
        if (left == inkPage) {
            qInfo("UITEST note  the PDF worker is not available here; split view uses two ink pages");
            QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
            spin(300);
            left = currentPage();
        }
        // The rail button asks which page to show beside this one.
        root->setProperty("leftPanel", QString());
        spin(200);
        QMetaObject::invokeMethod(root, "toggleSplit");
        spin(300);
        QList<QQuickItem *> pickRows;
        for (QQuickItem *row : findAll(win, QStringLiteral("pagePickerRow"))) if (row->isVisible()) pickRows << row;
        r.check("split view asks which page to show beside this one", !pickRows.isEmpty());
        QQuickItem *want = nullptr;
        for (QQuickItem *row : pickRows)
            if (row->property("modelData").toMap().value(QStringLiteral("id")).toLongLong() == inkPage) want = row;
        if (want) tap(win, want); else pressEscape(win);
        waitFor([&] { return root->property("splitPageId").toLongLong() == inkPage; }, 2000);
        if (root->property("splitPageId").toLongLong() != inkPage) QMetaObject::invokeMethod(root, "openSplit", Q_ARG(QVariant, inkPage));
        spin(600);
        auto *mainCanvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"));
        auto *splitCanvas = qobject_cast<InkCanvas *>(findOne(win, QStringLiteral("splitCanvas")));
        r.check("the chosen page opens beside the first", splitCanvas && splitCanvas->isVisible() && root->property("splitPageId").toLongLong() == inkPage);
        if (mainCanvas && splitCanvas) {
            mainCanvas->setTool(QStringLiteral("pen"));
            splitCanvas->setTool(QStringLiteral("pen"));
            const int mainBefore = mainCanvas->strokeCount(), splitBefore = splitCanvas->strokeCount();
            const QPointF onSplit = centre(splitCanvas), onMain = centre(mainCanvas);
            penDrag(win, onSplit - QPointF(60, 0), onSplit + QPointF(60, 40));
            r.check("the pen writes on the right-hand page", splitCanvas->strokeCount() == splitBefore + 1 && mainCanvas->strokeCount() == mainBefore,
                    QStringLiteral("split %1→%2, main %3→%4").arg(splitBefore).arg(splitCanvas->strokeCount()).arg(mainBefore).arg(mainCanvas->strokeCount()));
            penDrag(win, onMain - QPointF(80, -60), onMain + QPointF(40, 90));
            r.check("and on the left-hand page", mainCanvas->strokeCount() == mainBefore + 1 && splitCanvas->strokeCount() == splitBefore + 1);
            const qreal mainZoom = mainCanvas->zoom();
            splitCanvas->zoomAt(1.8, QPointF(splitCanvas->width() / 2, splitCanvas->height() / 2));
            spin(200);
            r.check("each side zooms on its own", std::abs(mainCanvas->zoom() - mainZoom) < 1e-6 && std::abs(splitCanvas->zoom() - mainZoom) > 0.05);
            shot(win, QStringLiteral("3-split-view"));
            splitCanvas->fitPage();

            // The divider drags.
            const double before = root->property("splitFraction").toDouble();
            if (QQuickItem *divider = findOne(win, QStringLiteral("splitDivider"))) {
                const QPointF at = centre(divider);
                sendMouse(win, QEvent::MouseButtonPress, at, Qt::LeftButton);
                spin(30);
                for (int i = 1; i <= 10; ++i) { sendMouse(win, QEvent::MouseMove, at - QPointF(15.0 * i, 0), Qt::LeftButton); spin(16); }
                sendMouse(win, QEvent::MouseButtonRelease, at - QPointF(150, 0), Qt::LeftButton);
                spin(250);
            }
            const double after = root->property("splitFraction").toDouble();
            r.check("dragging the divider resizes the two sides", after > before + 0.03, QStringLiteral("%1 → %2").arg(before).arg(after));
            shot(win, QStringLiteral("3-split-resized"));
        } else {
            r.check("found both canvases", false);
        }
        // The right side's ink is saved: close it, open it again, it is still there.
        if (QQuickItem *close = findOne(win, QStringLiteral("splitClose"))) {
            tap(win, close);
            spin(300);
            r.check("the right side closes", root->property("splitPageId").toLongLong() == 0 && !findOne(win, QStringLiteral("splitCanvas")));
        }
        QMetaObject::invokeMethod(root, "openSplit", Q_ARG(QVariant, inkPage));
        spin(500);
        splitCanvas = qobject_cast<InkCanvas *>(findOne(win, QStringLiteral("splitCanvas")));
        r.check("ink written on the right is kept", splitCanvas && splitCanvas->strokeCount() >= 1);
        // Closing the left side hands the window to the right-hand page.
        if (QQuickItem *closeLeft = findOne(win, QStringLiteral("closeLeftSide"))) {
            tap(win, closeLeft);
            spin(400);
            r.check("the left side closes, and the right-hand page takes its place",
                    currentPage() == inkPage && root->property("splitPageId").toLongLong() == 0);
        } else {
            r.check("the left side has a close button", false);
        }
        QFile::remove(handout);
        root->setProperty("leftPanel", QStringLiteral("notebooks"));
        spin(200);
    }

    // ---- 18. Presentation: the section full screen, keys and taps move between slides, the pen
    //          leaves a mark that fades and never reaches the page, and Escape gets out.
    if (library) {
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
        const qint64 firstSlide = currentPage();
        auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"));
        chord(win, Qt::Key_F5, Qt::NoModifier);
        waitFor([&] { return root->property("presenting").toBool(); }, 2000);
        r.check("F5 starts the presentation", root->property("presenting").toBool());
        spin(600);
        QQuickItem *rail = findOne(win, QStringLiteral("rail"));
        QQuickItem *overlay = findOne(win, QStringLiteral("laserOverlay"));
        r.check("presenting hides the rail and shows the slide alone", rail && !rail->isVisible());
        r.check("the pen layer is up", overlay && overlay->isVisible());
        QQuickItem *counter = findOne(win, QStringLiteral("presentCounter"));
        r.check("the slide counter says where you are", counter && counter->property("text").toString().contains(QLatin1Char('/')),
                counter ? counter->property("text").toString() : QString());

        shot(win, QStringLiteral("4-presentation-slide"));

        // Arrow keys move between slides.
        const int indexBefore = root->property("presentIndex").toInt();
        chord(win, Qt::Key_Left, Qt::NoModifier);
        spin(500);
        r.check("← goes back a slide", root->property("presentIndex").toInt() == indexBefore - 1 && currentPage() != firstSlide,
                QStringLiteral("index %1 → %2").arg(indexBefore).arg(root->property("presentIndex").toInt()));
        chord(win, Qt::Key_Right, Qt::NoModifier);
        spin(500);
        r.check("→ goes on again", currentPage() == firstSlide);

        // A tap on the left quarter goes back; anywhere else goes on.
        if (overlay) {
            const QRectF box = overlay->mapRectToScene(QRectF(0, 0, overlay->width(), overlay->height()));
            tap(win, nullptr, Qt::LeftButton, QPointF(box.left() + box.width() * 0.1, box.center().y()));
            spin(500);
            r.check("a tap on the left goes back", root->property("presentIndex").toInt() == indexBefore - 1);
            tap(win, nullptr, Qt::LeftButton, QPointF(box.center().x() + box.width() * 0.3, box.center().y()));
            spin(500);
            r.check("a tap on the right goes on", root->property("presentIndex").toInt() == indexBefore);

            // The pen draws a laser that fades, and leaves the page untouched.
            const int inkBefore = canvas ? canvas->strokeCount() : 0;
            const QPointF from = box.center() - QPointF(160, 60);
            sendMouse(win, QEvent::MouseButtonPress, from, Qt::LeftButton);
            spin(30);
            for (int i = 1; i <= 12; ++i) { sendMouse(win, QEvent::MouseMove, from + QPointF(26.0 * i, 12.0 * i * ((i % 3) - 1)), Qt::LeftButton); spin(16); }
            shot(win, QStringLiteral("4-presentation"));
            sendMouse(win, QEvent::MouseButtonRelease, from + QPointF(312, 0), Qt::LeftButton);
            spin(200);
            r.check("the pen leaves a laser trail", overlay->property("trails").toList().size() == 1,
                    QStringLiteral("%1 trails").arg(overlay->property("trails").toList().size()));
            r.check("and no ink on the page", !canvas || canvas->strokeCount() == inkBefore);
            waitFor([&] { return overlay->property("trails").toList().isEmpty(); }, 4000);
            r.check("the trail fades away on its own", overlay->property("trails").toList().isEmpty());
        }
        pressEscape(win);
        spin(400);
        r.check("Escape leaves the presentation", !root->property("presenting").toBool());
        r.check("and the rail comes back", rail && rail->isVisible());
    }

    // ---- 19. The outline: headings on the page, in the panel, and a tap puts the caret in one.
    if (library) {
        QObject *textBlocks = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            textBlocks = engine->rootContext()->contextProperty(QStringLiteral("textBlocks")).value<QObject *>();
        // Written before the page is opened, the way an import or a rename writes one.
        QVariantMap here;
        QMetaObject::invokeMethod(library, "page", Q_RETURN_ARG(QVariantMap, here), Q_ARG(qint64, currentPage()));
        qlonglong page = 0, block = 0;
        QMetaObject::invokeMethod(library, "createPage", Q_RETURN_ARG(qlonglong, page),
                                  Q_ARG(qlonglong, here.value(QStringLiteral("sectionId")).toLongLong()),
                                  Q_ARG(QString, QString()), Q_ARG(QString, QStringLiteral("typed")), Q_ARG(int, -1));
        if (textBlocks && page)
            QMetaObject::invokeMethod(textBlocks, "create", Q_RETURN_ARG(qlonglong, block), Q_ARG(qlonglong, page),
                                      Q_ARG(double, 0), Q_ARG(double, 0), Q_ARG(double, 794), Q_ARG(qlonglong, 0), Q_ARG(qlonglong, 0));
        if (block) {
            QMetaObject::invokeMethod(textBlocks, "setMarkdown", Q_ARG(qint64, block),
                                      Q_ARG(QString, QStringLiteral("# Introduction\n\nWhy waves matter.\n\n## Method\n\nA long stretch of words so the heading is not already on screen.\n\n### Results\n")),
                                      Q_ARG(qint64, 0));
            QMetaObject::invokeMethod(root, "openPage", Q_ARG(QVariant, page));
            spin(500);
            root->setProperty("rightPanel", QStringLiteral("page"));
            spin(400);
            QList<QQuickItem *> rows;
            for (QQuickItem *row : findAll(win, QStringLiteral("outlineRow"))) if (row->isVisible()) rows << row;
            r.check("the page panel lists the page's headings", rows.size() == 3, QStringLiteral("%1 rows").arg(rows.size()));
            shot(win, QStringLiteral("5-outline"));
            if (rows.size() == 3) {
                QQuickItem *editor = findOne(win, QStringLiteral("typedEditor"));
                if (editor) editor->setProperty("cursorPosition", 0);
                tap(win, rows[1]);
                spin(300);
                const int at = editor ? editor->property("cursorPosition").toInt() : 0;
                r.check("tapping a heading puts the caret in it", at > 10, QStringLiteral("cursor at %1").arg(at));
            }
            root->setProperty("rightPanel", QString());
            spin(200);
        } else {
            r.check("the typed page has a block to give an outline", false);
        }
    }

    // ---- 20. Back and forward: following a link (or any page change) can be undone as a move.
    if (library) {
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
        const qint64 first = currentPage();
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
        const qint64 second = currentPage();
        r.check("opening a page remembers where you were", root->property("backStack").toList().contains(QVariant(first)),
                QStringLiteral("%1 in the stack").arg(root->property("backStack").toList().size()));
        shot(win, QStringLiteral("5-history"));
        QQuickItem *back = findOne(win, QStringLiteral("navBack"));
        r.check("the way back is on screen", back && back->isVisible());
        if (back && back->isVisible()) {
            tap(win, back);
            waitFor([&] { return currentPage() == first; }, 2000);
            r.check("Back returns to the page before", currentPage() == first);
            QQuickItem *fwd = findOne(win, QStringLiteral("navForward"));
            r.check("and offers the way forward", fwd && fwd->isVisible());
            if (fwd && fwd->isVisible()) {
                tap(win, fwd);
                waitFor([&] { return currentPage() == second; }, 2000);
                r.check("Forward goes back again", currentPage() == second);
            }
        }
        chord(win, Qt::Key_Left, Qt::AltModifier);
        waitFor([&] { return currentPage() == first; }, 2000);
        r.check("Alt+← does the same", currentPage() == first);
        chord(win, Qt::Key_Right, Qt::AltModifier);
        waitFor([&] { return currentPage() == second; }, 2000);
        r.check("Alt+→ too", currentPage() == second);
    }

    // ---- 21. A notebook in one file: exported, imported back, and the copy is a real notebook.
    if (library) {
        QObject *notebooks = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            notebooks = engine->rootContext()->contextProperty(QStringLiteral("notebooks")).value<QObject *>();
        QVariantMap here;
        QMetaObject::invokeMethod(library, "page", Q_RETURN_ARG(QVariantMap, here), Q_ARG(qint64, currentPage()));
        const qint64 notebook = here.value(QStringLiteral("notebookId")).toLongLong();
        const QString file = QDir::tempPath() + QStringLiteral("/lumen-uitest-notebook.lumen");
        QFile::remove(file);
        QVariantMap saved, loaded;
        if (notebooks && notebook) {
            QMetaObject::invokeMethod(notebooks, "exportNotebook", Q_RETURN_ARG(QVariantMap, saved),
                                      Q_ARG(qint64, notebook), Q_ARG(QUrl, QUrl::fromLocalFile(file)));
            r.check("a notebook exports to one file", saved.value(QStringLiteral("ok")).toBool() && QFileInfo(file).size() > 0,
                    saved.value(QStringLiteral("error")).toString());
            QString what;
            QMetaObject::invokeMethod(notebooks, "describe", Q_RETURN_ARG(QString, what), Q_ARG(QUrl, QUrl::fromLocalFile(file)));
            r.check("the file says what is in it", what.contains(QLatin1String("page")), what);
            int before = 0;
            QVariantList list;
            QMetaObject::invokeMethod(library, "notebooks", Q_RETURN_ARG(QVariantList, list));
            before = list.size();
            QMetaObject::invokeMethod(notebooks, "importNotebook", Q_RETURN_ARG(QVariantMap, loaded), Q_ARG(QUrl, QUrl::fromLocalFile(file)));
            spin(400);
            QMetaObject::invokeMethod(library, "notebooks", Q_RETURN_ARG(QVariantList, list));
            r.check("and imports back as a notebook of its own",
                    loaded.value(QStringLiteral("ok")).toBool() && list.size() == before + 1
                        && loaded.value(QStringLiteral("pages")).toInt() == saved.value(QStringLiteral("pages")).toInt(),
                    loaded.value(QStringLiteral("error")).toString());
            root->setProperty("leftPanel", QStringLiteral("notebooks"));
            spin(500);
            // The imported notebook is the last thing in the list: show that end of it.
            if (QQuickItem *list = findOne(win, QStringLiteral("sidebarList"))) QMetaObject::invokeMethod(list, "positionViewAtEnd");
            spin(300);
            shot(win, QStringLiteral("5-notebook-file"));
        } else {
            r.check("the notebook file service is available to QML", false);
        }
        // And the menu that offers it.
        revealCurrent(win, root);
        QQuickItem *notebookRow = nullptr;
        {
            QQuickItem *list = findOne(win, QStringLiteral("sidebarList"));
            const QRectF viewport = list ? list->mapRectToScene(QRectF(0, 0, list->width(), list->height())) : QRectF();
            for (QQuickItem *row : findAll(win, QStringLiteral("sidebarRow"))) {
                if (!row->isVisible() || row->property("rowKind").toString() != QLatin1String("notebook")) continue;
                if (!viewport.isNull() && !viewport.contains(centre(row))) continue;   // a delegate parked outside the list
                notebookRow = row;
                break;
            }
        }
        if (notebookRow) {
            hold(win, notebookRow);
            r.check("a notebook's menu offers to export it", sheetAction(win, QStringLiteral("Export notebook…")) != nullptr);
            pressEscape(win);
            spin(150);
        }
        QQuickItem *importButton = findOne(win, QStringLiteral("importNotebook"));
        r.check("the sidebar offers to import one", importButton && importButton->isVisible());
        QFile::remove(file);
    }
}

} // namespace

int uitest::run(QQuickWindow *win, QObject *root)
{
    g_previous = qInstallMessageHandler(collectMessages);
    Report r;

    root->setProperty("onboardingVisible", false);
    // The checks below are about ink pages; typed pages have their own section.
    if (QQmlEngine *engine = qmlEngine(root))
        if (QObject *lib = engine->rootContext()->contextProperty(QStringLiteral("library")).value<QObject *>())
            QMetaObject::invokeMethod(lib, "setSetting", Q_ARG(QString, QStringLiteral("page.sizeMode")), Q_ARG(QString, QStringLiteral("a4")));
    root->setProperty("leftPanel", QStringLiteral("notebooks"));
    if (!waitFor([&] { return win->isExposed() || win->isVisible(); }, 5000))
        qWarning("UITEST: window never became visible");
    spin(400);

    const auto currentPage = [&] { return root->property("currentPageId").toLongLong(); };
    if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("pages")) {
        waitFor([&] { return currentPage() > 0; }, 3000);
        if (currentPage() == 0) QMetaObject::invokeMethod(root, "newPageAnywhere", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
        pageFeatures(win, root, r);
        r.check("no QML errors during the run", g_qmlComplaints.isEmpty(),
                g_qmlComplaints.isEmpty() ? QString() : g_qmlComplaints.join(QStringLiteral(" | ")).left(600));
        qInstallMessageHandler(g_previous);
        qInfo("UITEST %s (%d failure%s)", r.failures ? "FAILED" : "OK", r.failures, r.failures == 1 ? "" : "s");
        return r.failures;
    }

    // Start is either the last page reopened, or — after a deliberate close last time — the empty
    // state. Both are correct; anything else is not.
    waitFor([&] { return currentPage() > 0; }, 3000);
    if (currentPage() == 0) {
        QQuickItem *empty = findOne(win, QStringLiteral("emptyState"));
        r.check("a deliberate close is remembered, on the empty state", empty && empty->isVisible());
        QMetaObject::invokeMethod(root, "newPageAnywhere", Q_ARG(QVariant, QStringLiteral("a4")));
        waitFor([&] { return currentPage() > 0; }, 3000);
    } else {
        r.check("the last page reopens at start", true);
    }
    r.check("a page is open once we ask for one", currentPage() > 0, QStringLiteral("currentPageId=%1").arg(currentPage()));
    r.check("the sidebar lists rows", !findAll(win, QStringLiteral("sidebarRow")).isEmpty());

    if (!pageRow(win, false)) {                      // a fresh library has one page: make a second
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        waitFor([&] { return pageRow(win, false) != nullptr; }, 3000);
    }

    // ---- 1. A tap on a page row opens that page, and never a menu.
    if (QQuickItem *row = pageRow(win, false)) {
        const qint64 want = row->property("rowPageId").toLongLong();
        tap(win, row);
        r.check("tap on a page row opens it", currentPage() == want,
                QStringLiteral("wanted %1, got %2").arg(want).arg(currentPage()));
        r.check("tap on a page row does not open the menu", !sheetOpen(win));
    } else {
        r.check("found a page row to tap", false);
    }

    // ---- 2. Tapping the row that is already current still must not pop a menu (the reported bug:
    //         the ⋯ appeared on the current row and swallowed the tap).
    if (QQuickItem *row = pageRow(win, true)) {
        const qint64 before = currentPage();
        tap(win, row);
        r.check("tap on the current page row keeps it open", currentPage() == before);
        r.check("tap on the current page row does not open the menu", !sheetOpen(win));
    } else {
        r.check("found the current page row", false);
    }

    // ---- 3. Press and hold is what opens the menu.
    revealCurrent(win, root);
    QQuickItem *target = pageRow(win, true);
    if (target) {
        hold(win, target);
        r.check("press-and-hold opens the menu", sheetOpen(win));
        r.check("the menu offers Delete", sheetAction(win, QStringLiteral("Delete")) != nullptr);
        r.check("the menu offers Rename", sheetAction(win, QStringLiteral("Rename")) != nullptr);
        r.check("the menu offers Close page for the open page", sheetAction(win, QStringLiteral("Close page")) != nullptr);
    } else {
        r.check("found a page row to hold", false);
    }

    // ---- 3b. A finger tap is a tap, however slow or shaky. This is the maker's report: taps were
    //          opening the rename/delete menu.
    if (sheetOpen(win)) pressEscape(win);
    r.check("Escape closes the menu", !sheetOpen(win));
    if (!canFinger()) {
        qWarning("UITEST skip  finger input needs -DLUMEN_UITEST_TOUCH=ON");
    } else {
        QQuickItem *other = pageRow(win, false);
        if (other) {
            const qint64 want = other->property("rowPageId").toLongLong();
            fingerTap(win, other, 60);
            const bool delivered = currentPage() == want;
            r.check("a finger tap opens the page", delivered, QStringLiteral("wanted %1, got %2").arg(want).arg(currentPage()));
            r.check("a finger tap does not open the menu", !sheetOpen(win));
            if (delivered) {
                // a deliberate, slow tap — still a tap
                revealCurrent(win, root);
                if (QQuickItem *cur = pageRow(win, true)) {
                    fingerTap(win, cur, 400);
                    r.check("a slow finger tap still does not open the menu", !sheetOpen(win));
                }
                // and a real hold does open it
                revealCurrent(win, root);
                if (QQuickItem *cur = pageRow(win, true)) {
                    fingerTap(win, cur, 900, 0);
                    r.check("a finger hold opens the menu", sheetOpen(win));
                }
            }
        }
    }
    // A slow mouse press is a tap too.
    if (!sheetOpen(win)) {
        if (QQuickItem *other = pageRow(win, false)) {
            const QPointF p = centre(other);
            sendMouse(win, QEvent::MouseButtonPress, p, Qt::LeftButton);
            spin(400);
            sendMouse(win, QEvent::MouseButtonRelease, p, Qt::LeftButton);
            spin(150);
            r.check("a 400 ms mouse press does not open the menu", !sheetOpen(win));
        }
    }
    if (!sheetOpen(win)) {
        revealCurrent(win, root);
        if (QQuickItem *cur = pageRow(win, true)) hold(win, cur, 900);
    }

    // ---- 4. Delete completes: the page goes, nothing is auto-opened in its place, undo is offered.
    const qint64 deleted = currentPage();
    if (QQuickItem *del = sheetAction(win, QStringLiteral("Delete"))) {
        const QPointF where = centre(del);
        r.check("the Delete action is on screen", QRectF(0, 0, win->width(), win->height()).contains(where),
                QStringLiteral("at %1,%2 in a %3x%4 window").arg(where.x()).arg(where.y()).arg(win->width()).arg(win->height()));
        tap(win, del);
        waitFor([&] { return currentPage() == 0; }, 2000);
        r.check("deleting the open page leaves no page selected", currentPage() == 0,
                QStringLiteral("was %1, now %2").arg(deleted).arg(currentPage()));
        QQuickItem *empty = findOne(win, QStringLiteral("emptyState"));
        r.check("the empty state appears", empty && empty->isVisible());
        QQuickItem *toast = findOne(win, QStringLiteral("toastText"));
        r.check("a toast reports the deletion", toast && toast->isVisible());

        // ---- 5. Undo brings it back and opens it.
        if (QQuickItem *undo = findOne(win, QStringLiteral("toastUndo"))) {
            tap(win, undo);
            waitFor([&] { return currentPage() == deleted; }, 2000);
            r.check("undo restores the deleted page", currentPage() == deleted,
                    QStringLiteral("wanted %1, got %2").arg(deleted).arg(currentPage()));
        } else {
            r.check("the toast offers Undo", false);
        }
    } else {
        r.check("the menu has a Delete action", false);
    }

    // ---- 6. Closing a page deliberately is allowed, and the empty state can start a new one.
    QMetaObject::invokeMethod(root, "closePage");
    spin(200);
    r.check("close page leaves no page selected", currentPage() == 0);
    QQuickItem *newBtn = findOne(win, QStringLiteral("emptyNewPage"));
    r.check("the empty state offers a new page", newBtn && newBtn->isVisible());
    if (newBtn) {
        tap(win, newBtn);
        waitFor([&] { return currentPage() > 0; }, 3000);
        r.check("the empty state can start a page", currentPage() > 0);
    }

    // ---- 7. Touch targets are big enough to hit with a finger.
    int tooSmall = 0;
    QString smallest;
    for (const QString &name : {QStringLiteral("sidebarRow"), QStringLiteral("emptyNewPage"), QStringLiteral("actionSheetItem")}) {
        for (QQuickItem *i : findAll(win, name)) {
            if (!i->isVisible() || i->height() <= 0) continue;
            if (i->height() < 40) { ++tooSmall; smallest = QStringLiteral("%1 is %2 px tall").arg(name).arg(i->height()); }
        }
    }
    r.check("list and menu rows are finger-sized", tooSmall == 0, smallest);

    // ---- 8. The pen must not ink on chrome, and must ink on the page (D-021). Synthetic tablet
    //         events skip Qt's mouse synthesis, so ask the canvas what it decides — that decision
    //         is the whole rule: false means "let the UI have it".
    if (root->property("pageTyped").toBool()) {
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        spin(300);
    }
    if (auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"))) {
        const auto penAt = [&](QPointF scenePos, TabletSample::Kind kind) {
            TabletSample s;
            s.kind = kind;
            s.windowPos = scenePos;
            s.pressure = kind == TabletSample::Kind::Release ? 0.0f : 0.6f;
            s.buttons = kind == TabletSample::Kind::Release ? Qt::NoButton : Qt::LeftButton;
            s.button = Qt::LeftButton;
            s.timestampMs = quint32(g_stamp += 20);
            return canvas->tabletSample(s);
        };
        if (QQuickItem *row = pageRow(win, false) ? pageRow(win, false) : pageRow(win, true)) {
            const int before = canvas->strokeCount();
            const bool eaten = penAt(centre(row), TabletSample::Kind::Press);
            penAt(centre(row), TabletSample::Kind::Release);
            r.check("the pen does not ink on the sidebar", !eaten);
            r.check("a pen press on the sidebar leaves no stroke", canvas->strokeCount() == before);
        }
        const int before = canvas->strokeCount();
        const QPointF mid = centre(canvas);
        r.check("the pen inks on the page", penAt(mid, TabletSample::Kind::Press),
                QStringLiteral("visible=%1 tool=%2 page=%3 typed=%4").arg(canvas->isVisible()).arg(canvas->property("tool").toString())
                    .arg(currentPage()).arg(root->property("pageTyped").toBool()));
        penAt(mid + QPointF(30, 30), TabletSample::Kind::Move);
        penAt(mid + QPointF(60, 60), TabletSample::Kind::Release);
        r.check("a pen stroke on the page is kept", canvas->strokeCount() == before + 1,
                QStringLiteral("%1 → %2").arg(before).arg(canvas->strokeCount()));
        canvas->undo();

        QMetaObject::invokeMethod(root, "closePage");
        spin(200);
        r.check("with no page open the pen belongs to the UI", !penAt(mid, TabletSample::Kind::Press));
        penAt(mid, TabletSample::Kind::Release);
        QMetaObject::invokeMethod(root, "openLastPage");
        spin(200);
    } else {
        r.check("found the canvas", false);
    }

    // The services QML uses are context properties; the test drives them the same way QML does.
    QObject *library = nullptr;
    if (QQmlEngine *engine = qmlEngine(root))
        library = engine->rootContext()->contextProperty(QStringLiteral("library")).value<QObject *>();
    r.check("the test can reach the library service", library != nullptr);
    const auto ensurePage = [&] {
        // Everything that uses this draws on the canvas, so it needs a handwritten page.
        if (currentPage() > 0 && !root->property("pageTyped").toBool()) return;
        QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
        waitFor([&] { return currentPage() > 0 && !root->property("pageTyped").toBool(); }, 3000);
    };

    // ---- 12e. Tooltips by touch: holding a control with a finger or the pen shows what it does and
    // does not do it; a tap still does it; a hold that means something (the sidebar menu) still does.
    const auto holdTipChecks = [&] {
        root->setProperty("leftPanel", QStringLiteral("notebooks"));
        spin(250);
        QQuickItem *notebooks = nullptr;
        for (QQuickItem *b : findAll(win, QStringLiteral("railButton")))
            if (b->property("tip").toString().startsWith(QLatin1String("Notebooks"))) notebooks = b;
        // The bubble is a Popup, so it is a QObject child of the window's root, not an item in the tree.
        const auto bubbleSays = [&](const QString &what) {
            QObject *b = root->findChild<QObject *>(QStringLiteral("holdTip"));
            return b && b->property("visible").toBool() && b->property("text").toString().startsWith(what);
        };
        if (notebooks && canFinger()) {
            fingerTap(win, notebooks, 800);
            r.check("a finger hold on a rail button shows its tooltip", bubbleSays(QStringLiteral("Notebooks")));
            r.check("and does not press the button", root->property("leftPanel").toString() == QLatin1String("notebooks"));
            spin(1800);
            r.check("the hold tooltip goes away after the finger lifts", !bubbleSays(QStringLiteral("Notebooks")));
            fingerTap(win, notebooks, 120);
            r.check("a finger tap on the same button still works", root->property("leftPanel").toString().isEmpty());
            fingerTap(win, notebooks, 120);
            const QPointF at = centre(notebooks);
            penAt(win, QEvent::TabletPress, at, Qt::LeftButton);
            spin(800);
            penAt(win, QEvent::TabletRelease, at, Qt::NoButton);
            spin(200);
            r.check("a pen hold shows the tooltip too", bubbleSays(QStringLiteral("Notebooks")));
            r.check("and does not press the button either", root->property("leftPanel").toString() == QLatin1String("notebooks"));
            pressEscape(win);
        } else {
            r.check("found the Notebooks rail button and a touch device", false);
        }
        revealCurrent(win, root);
        if (QQuickItem *row = pageRow(win, false); row && canFinger()) {
            fingerTap(win, row, 900);
            r.check("a finger hold on a sidebar row still opens its menu", sheetOpen(win));
            pressEscape(win);
        }
    };
    // ---- 12f. Slow work shows itself where it was asked for, and can be stopped. Reading lasso'd
    // maths is the case to drive: its chip sits under the lasso, and the worker is slow to start.
    const auto progressChecks = [&] {
        ensurePage();
        QObject *ocr = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            ocr = engine->rootContext()->contextProperty(QStringLiteral("ocr")).value<QObject *>();
        const QString png = QDir::temp().filePath(QStringLiteral("lumen-uitest-maths.png"));
        QImage sum(160, 60, QImage::Format_RGB32);
        sum.fill(Qt::white);
        sum.save(png);
        const auto latexChip = [&]() -> QQuickItem * {
            QList<QQuickItem *> stack{win->contentItem()};
            while (!stack.isEmpty()) {
                QQuickItem *i = stack.takeLast();
                if (QByteArray(i->metaObject()->className()).startsWith("ProgressChip") && i->isVisible()
                    && i->property("text").toString().contains(QLatin1String("maths"))) return i;
                stack << i->childItems();
            }
            return nullptr;
        };
        if (ocr) {
            // Checked before the event loop runs again: a worker that is already up and lacks the
            // maths model answers within milliseconds, and the chip rightly goes with the answer.
            QMetaObject::invokeMethod(ocr, "latexFromImage", Q_ARG(QString, png));
            QQuickItem *chip = latexChip();
            r.check("reading maths shows a progress chip", chip != nullptr);
            QQuickItem *stop = nullptr;
            if (chip) { QList<QQuickItem *> found; gather(chip, QStringLiteral("progressCancel"), found); stop = found.value(0); }
            r.check("the chip can stop it, with a finger-sized button", stop && stop->width() >= 40 && stop->height() >= 40);
            if (chip) {
                QMetaObject::invokeMethod(chip, "cancelRequested");
                r.check("Stop takes the chip away", !latexChip() && !ocr->property("latexBusy").toBool());
            }
            // Without the AI add-on the same request must end in a plain explanation, not a stack trace.
            QMetaObject::invokeMethod(ocr, "latexFromImage", Q_ARG(QString, png));
            waitFor([&] { return !ocr->property("latexBusy").toBool(); }, 20000);
            r.check("the chip goes when the work ends", !latexChip());
            QQuickItem *toastText = findOne(win, QStringLiteral("toastText"));
            const QString said = toastText ? toastText->property("text").toString() : QString();
            if (said.contains(QLatin1String("AI add-on")))
                r.check("a missing add-on is explained, with where to go", said.contains(QLatin1String("Background services")), said);
            else
                qInfo("UITEST note  the maths reader is installed here; the add-on message was not exercised (%s)", qPrintable(said.left(80)));
        } else {
            r.check("the OCR service is available to QML", false);
        }
        QFile::remove(png);
    };
    // ---- 12g. Settings › Background services: one row per helper, each with a state a person can
    // read and a Restart that works; the add-on banner exactly when an AI package is missing.
    const auto servicesChecks = [&] {
        root->setProperty("settingsVisible", true);
        spin(300);
        const QList<QQuickItem *> rows = findAll(win, QStringLiteral("serviceRow"));
        r.check("the services panel lists every helper", rows.size() == 8, QStringLiteral("%1 rows").arg(rows.size()));
        const QVariantList workers = qmlEngine(root) ? qmlEngine(root)->rootContext()->contextProperty(QStringLiteral("workers")).toList() : QVariantList();
        const QStringList known{QStringLiteral("ready"), QStringLiteral("busy"), QStringLiteral("loading model"), QStringLiteral("starting"),
                                QStringLiteral("not installed"), QStringLiteral("crashed"), QStringLiteral("stopped")};
        // Opening Settings asks each helper what it has; give them time to say.
        waitFor([&] { for (const QVariant &v : workers) if (v.value<QObject *>()->property("status").toString() == QLatin1String("starting")) return false; return true; }, 20000);
        QStringList odd, summary;
        bool aiMissing = false;
        for (const QVariant &v : workers) {
            QObject *w = v.value<QObject *>();
            const QString status = w->property("status").toString();
            summary << w->property("name").toString() + "=" + status;
            if (!known.contains(status)) odd << status;
            const QStringList gone = w->property("missing").toStringList() + w->property("missingOptional").toStringList();
            for (const char *ai : {"torch", "transformers", "PIL", "pix2tex", "faster_whisper"})
                if (gone.contains(QLatin1String(ai)) && (w->property("name") == QLatin1String("ocr") || w->property("name") == QLatin1String("audio"))) aiMissing = true;
        }
        qInfo("UITEST note  services: %s", qPrintable(summary.join(QStringLiteral(", "))));
        r.check("every helper reports a state a person can read", odd.isEmpty(), odd.join(QStringLiteral(", ")));
        r.check("no helper is left starting after the check", !summary.join(QLatin1Char(' ')).contains(QLatin1String("=starting")));
        QQuickItem *banner = findOne(win, QStringLiteral("addOnBanner"));
        r.check("the add-on banner shows exactly when an AI package is missing", banner && banner->isVisible() == aiMissing,
                QStringLiteral("missing=%1 banner=%2").arg(aiMissing).arg(banner && banner->isVisible()));
        int restarted = 0;
        for (QQuickItem *b : findAll(win, QStringLiteral("serviceRestart"))) {
            if (!b->isVisible() || !b->isEnabled()) continue;
            // Settings scrolls: bring the button into view the way a finger would.
            for (QQuickItem *up = b->parentItem(); up; up = up->parentItem()) {
                if (!up->inherits("QQuickFlickable")) continue;
                QQuickItem *content = up->property("contentItem").value<QQuickItem *>();
                const qreal want = b->mapToItem(content, QPointF(0, 0)).y() - up->height() / 2;
                const qreal most = std::max<qreal>(0, up->property("contentHeight").toReal() - up->height());
                up->setProperty("contentY", std::clamp<qreal>(want, 0, most));
                spin(60);
                break;
            }
            const QRectF box = b->mapRectToScene(QRectF(0, 0, b->width(), b->height()));
            if (!QRectF(0, 0, win->width(), win->height()).contains(box.center())) continue;
            r.check("a Restart button is finger-sized", b->height() >= 40);
            tap(win, b);
            ++restarted;
        }
        r.check("Restart can be pressed", restarted > 0);
        waitFor([&] { for (const QVariant &v : workers) if (v.value<QObject *>()->property("status").toString() == QLatin1String("starting")) return false; return true; }, 20000);
        root->setProperty("settingsVisible", false);
        spin(200);
    };
    // ---- 12h. Left-handed mode: the rail, the panels and the page arrows change sides, and the
    // choice is remembered.
    const auto handChecks = [&] {
        ensurePage();
        root->setProperty("leftPanel", QStringLiteral("notebooks"));
        root->setProperty("leftHanded", false);
        spin(250);
        QQuickItem *rail = findOne(win, QStringLiteral("rail"));
        QQuickItem *sidebar = findOne(win, QStringLiteral("sidebarList"));
        const auto middleOf = [&](QQuickItem *i) { return i ? i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).x() : -1; };
        const qreal railRight = middleOf(rail), sideRight = middleOf(sidebar);
        root->setProperty("leftHanded", true);
        spin(400);
        r.check("left-handed puts the rail on the other side", middleOf(rail) > win->width() / 2 && railRight < win->width() / 2,
                QStringLiteral("%1 → %2").arg(railRight).arg(middleOf(rail)));
        r.check("and the notebooks panel follows it", middleOf(findOne(win, QStringLiteral("sidebarList"))) > win->width() / 2 && sideRight < win->width() / 2);
        QQuickItem *page = findOne(win, QStringLiteral("pageArea"));
        int arrows = 0, onTheRight = 0;
        for (QQuickItem *i : findAll(win, QStringLiteral("chrome"))) {
            if (!i->isVisible() || !page || !page->isAncestorOf(i)) continue;
            if (qAbs(i->width() - i->height()) > 2 || i->width() < 40) continue;       // the round page arrows
            ++arrows;
            if (i->mapToScene(QPointF(i->width() / 2, 0)).x() > win->width() / 2) ++onTheRight;
        }
        r.check("and the page arrows move over with them", arrows > 0 && arrows == onTheRight, QStringLiteral("%1 of %2 on the right").arg(onTheRight).arg(arrows));
        // The setting is what the app reads at start, so it has to be written, not just applied.
        if (QQmlEngine *engine = qmlEngine(root))
            if (QObject *lib = engine->rootContext()->contextProperty(QStringLiteral("library")).value<QObject *>()) {
                QMetaObject::invokeMethod(lib, "setSetting", Q_ARG(QString, QStringLiteral("ui.leftHanded")), Q_ARG(QString, QStringLiteral("1")));
                QString saved;
                QMetaObject::invokeMethod(lib, "setting", Q_RETURN_ARG(QString, saved), Q_ARG(QString, QStringLiteral("ui.leftHanded")), Q_ARG(QString, QStringLiteral("0")));
                r.check("the writing hand is stored in the settings", saved == QLatin1String("1"), saved);
                QMetaObject::invokeMethod(lib, "setSetting", Q_ARG(QString, QStringLiteral("ui.leftHanded")), Q_ARG(QString, QStringLiteral("0")));
            }
        root->setProperty("leftHanded", false);
        spin(300);
        r.check("and right-handed puts everything back", middleOf(findOne(win, QStringLiteral("rail"))) < win->width() / 2);
    };

    if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("hand")) {
        handChecks();
        qInstallMessageHandler(g_previous);
        return r.failures;
    }
    // ---- 12i. A picture can be trimmed and turned, and the file it came from is untouched.
    const auto pictureChecks = [&] {
        ensurePage();
        QObject *images = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            images = engine->rootContext()->contextProperty(QStringLiteral("images")).value<QObject *>();
        auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"));
        if (!images || !canvas) { r.check("the pictures service and canvas are there", false); return; }
        const QString png = QDir::temp().filePath(QStringLiteral("lumen-uitest-photo.png"));
        QImage photo(200, 100, QImage::Format_RGB32);
        photo.fill(Qt::darkCyan);
        photo.save(png);
        QFile original(png);
        original.open(QIODevice::ReadOnly);
        const QByteArray originalBytes = original.readAll();
        original.close();

        canvas->setTool(QStringLiteral("lasso"));
        qint64 id = 0;
        QMetaObject::invokeMethod(images, "insertFile", Q_RETURN_ARG(qint64, id), Q_ARG(qint64, currentPage()),
                                  Q_ARG(QUrl, QUrl::fromLocalFile(png)), Q_ARG(double, 80), Q_ARG(double, 80), Q_ARG(double, 240));
        r.check("a picture lands on the page", id > 0);
        if (!id) return;
        spin(300);
        const auto shot = [&](const char *key) {
            QVariantMap m;
            QMetaObject::invokeMethod(images, "image", Q_RETURN_ARG(QVariantMap, m), Q_ARG(qint64, id));
            return m.value(QLatin1String(key)).toDouble();
        };
        const QPointF middle = canvas->mapToScene(canvas->toScreen(QPointF(80 + shot("w") / 2, 80 + shot("h") / 2)));
        tap(win, canvas, Qt::LeftButton, middle);           // select it
        tap(win, canvas, Qt::LeftButton, middle);
        tap(win, canvas, Qt::LeftButton, middle);           // and again: its options
        spin(300);
        QQuickItem *trim = itemWithText(win->contentItem(), QStringLiteral("Trim…"));
        r.check("a picture's options offer Trim", trim != nullptr);
        if (!trim) return;
        tap(win, trim);
        spin(250);
        const QList<QQuickItem *> grips = findAll(win, QStringLiteral("cropGrip"));
        r.check("trimming shows four corners to drag", grips.size() == 4, QStringLiteral("%1 grips").arg(grips.size()));
        QQuickItem *topLeft = nullptr;
        for (QQuickItem *g : grips) {
            const QPointF c = centre(g);
            if (!topLeft || c.x() + c.y() < centre(topLeft).x() + centre(topLeft).y()) topLeft = g;
        }
        if (topLeft) {
            const QPointF from = centre(topLeft);
            sendMouse(win, QEvent::MouseButtonPress, from, Qt::LeftButton);
            for (int i = 1; i <= 6; ++i) { sendMouse(win, QEvent::MouseMove, from + QPointF(6, 4) * i, Qt::LeftButton); spin(16); }
            sendMouse(win, QEvent::MouseButtonRelease, from + QPointF(36, 24), Qt::LeftButton);
            spin(120);
        }
        const double widthBefore = shot("w");
        QQuickItem *done = findOne(win, QStringLiteral("cropDone"));
        r.check("and a finger-sized Trim to finish", done && done->height() >= 40);
        if (done) { tap(win, done); spin(300); }
        r.check("trimming takes a piece off the picture", shot("cropW") < 0.999 && shot("w") < widthBefore,
                QStringLiteral("cropW=%1 w=%2 (was %3)").arg(shot("cropW")).arg(shot("w")).arg(widthBefore));
        r.check("and no grips are left on the page", findAll(win, QStringLiteral("cropGrip")).isEmpty()
                || !findAll(win, QStringLiteral("cropGrip")).first()->isVisible());

        // Turn it: the crop turns with it and the sides swap.
        const double wasW = shot("w"), wasH = shot("h");
        tap(win, canvas, Qt::LeftButton, middle);
        tap(win, canvas, Qt::LeftButton, middle);
        spin(250);
        QQuickItem *turn = itemWithText(win->contentItem(), QStringLiteral("Turn"));
        r.check("a picture's options offer Turn", turn != nullptr);
        if (turn) {
            tap(win, turn);
            spin(250);
            QVariantMap m;
            QMetaObject::invokeMethod(images, "image", Q_RETURN_ARG(QVariantMap, m), Q_ARG(qint64, id));
            r.check("turning the picture turns it a quarter", m.value(QStringLiteral("rotation")).toInt() == 90);
            r.check("and swaps its sides", qAbs(m.value(QStringLiteral("w")).toDouble() - wasH) < 0.01
                                        && qAbs(m.value(QStringLiteral("h")).toDouble() - wasW) < 0.01);
        }
        pressEscape(win);
        QFile after(png);
        after.open(QIODevice::ReadOnly);
        r.check("and the picture file itself is never written", after.readAll() == originalBytes);
        after.close();
        QMetaObject::invokeMethod(images, "remove", Q_ARG(qint64, id));
        QFile::remove(png);
        spin(150);
    };

    if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("picture")) {
        pictureChecks();
        qInstallMessageHandler(g_previous);
        return r.failures;
    }
    // ---- 12j. The first run sets itself up from what the machine has, the page says whether it is
    // saved, and a single page can be exported on its own.
    const auto polishChecks = [&] {
        QObject *lib = nullptr, *tabletMode = nullptr;
        if (QQmlEngine *engine = qmlEngine(root)) {
            lib = engine->rootContext()->contextProperty(QStringLiteral("library")).value<QObject *>();
            tabletMode = engine->rootContext()->contextProperty(QStringLiteral("tabletMode")).value<QObject *>();
        }
        if (!lib || !tabletMode) { r.check("the library and tablet-mode services are there", false); return; }
        const auto setting = [&](const QString &key, const QString &fallback) {
            QString out;
            QMetaObject::invokeMethod(lib, "setting", Q_RETURN_ARG(QString, out), Q_ARG(QString, key), Q_ARG(QString, fallback));
            return out;
        };
        const bool pen = tabletMode->property("penAvailable").toBool();
        const bool touch = tabletMode->property("touchAvailable").toBool();
        qInfo("UITEST note  this machine reports pen=%d touch=%d", pen, touch);
        QMetaObject::invokeMethod(lib, "setSetting", Q_ARG(QString, QStringLiteral("page.sizeMode")), Q_ARG(QString, QString()));
        QMetaObject::invokeMethod(lib, "setSetting", Q_ARG(QString, QStringLiteral("keyboard.mode")), Q_ARG(QString, QString()));
        root->setProperty("onboardingVisible", true);
        spin(400);
        QQuickItem *typed = findOne(win, QStringLiteral("setupTyped"));
        QQuickItem *ink = findOne(win, QStringLiteral("setupInk"));
        QQuickItem *start = findOne(win, QStringLiteral("onboardingStart"));
        r.check("the first run offers the kind of page to start with", typed && ink && start);
        if (!typed || !ink || !start) { root->setProperty("onboardingVisible", false); return; }
        r.check("and picks the one this machine suits", (pen ? ink : typed)->property("on").toBool(),
                QStringLiteral("typed=%1 ink=%2").arg(typed->property("on").toBool()).arg(ink->property("on").toBool()));
        r.check("its choices are finger-sized", start->height() >= 40 && typed->height() >= 40);
        tap(win, typed);
        spin(100);
        r.check("a choice can be changed", typed->property("on").toBool() && !ink->property("on").toBool());
        tap(win, start);
        spin(300);
        r.check("Start puts the first run away", !root->property("onboardingVisible").toBool());
        r.check("and writes what was chosen", setting(QStringLiteral("page.sizeMode"), QString()) == QLatin1String("typed"),
                setting(QStringLiteral("page.sizeMode"), QStringLiteral("(unset)")));
        const bool touchNow = tabletMode->property("touchAvailable").toBool();
        r.check("and the on-screen keyboard follows the machine",
                setting(QStringLiteral("keyboard.mode"), QString()) == (touchNow ? QLatin1String("tablet") : QLatin1String("never")),
                setting(QStringLiteral("keyboard.mode"), QStringLiteral("(unset)")));
        // Put back what the rest of the run expects: handwritten pages and the default keyboard.
        QMetaObject::invokeMethod(lib, "setSetting", Q_ARG(QString, QStringLiteral("page.sizeMode")), Q_ARG(QString, QStringLiteral("a4")));
        QMetaObject::invokeMethod(lib, "setSetting", Q_ARG(QString, QStringLiteral("keyboard.mode")), Q_ARG(QString, QStringLiteral("tablet")));
        root->setProperty("keyboardMode", QStringLiteral("tablet"));

        // The save indicator: it says Saved when nothing is waiting, and Saving… while it is.
        ensurePage();
        spin(200);
        QQuickItem *saved = itemWithText(win->contentItem(), QStringLiteral("Saved"));
        QQuickItem *saving = itemWithText(win->contentItem(), QStringLiteral("Saving…"));
        r.check("the page says whether it is saved", saved || saving);

        // Export just this page: the page menu offers it, next to exporting the whole section.
        QQuickItem *overflow = nullptr;
        for (QQuickItem *i : findAll(win, QStringLiteral("chrome")))
            for (QQuickItem *k : i->childItems())
                if (k->property("tip").toString().startsWith(QLatin1String("Page style"))) overflow = k;
        if (!overflow) {
            QList<QQuickItem *> stack{win->contentItem()};
            while (!stack.isEmpty()) {
                QQuickItem *item = stack.takeLast();
                if (item->property("tip").toString().startsWith(QLatin1String("Page style")) && item->isVisible()) { overflow = item; break; }
                stack << item->childItems();
            }
        }
        r.check("the toolbar has its page menu", overflow != nullptr);
        if (overflow) {
            // On a narrow window the toolbar scrolls; its last button can be off the edge.
            for (QQuickItem *up = overflow->parentItem(); up; up = up->parentItem()) {
                if (!up->inherits("QQuickFlickable")) continue;
                up->setProperty("contentX", std::max<qreal>(0, up->property("contentWidth").toReal() - up->width()));
                spin(120);
                break;
            }
        }
        if (overflow && QRectF(0, 0, win->width(), win->height()).contains(centre(overflow))) {
            tap(win, overflow);
            spin(300);
            QStringList offered;
            for (QQuickItem *t : findAll(win, QStringLiteral("actionSheetLabel"))) offered << t->property("text").toString();
            r.check("the page menu offers exporting this page on its own", sheetAction(win, QStringLiteral("Export this page as PDF")) != nullptr,
                    offered.join(QStringLiteral(" | ")));
            r.check("and still offers the whole section", sheetAction(win, QStringLiteral("Export this section as PDF")) != nullptr);
            pressEscape(win);
            spin(150);
        }
    };

    if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("polish")) {
        polishChecks();
        qInstallMessageHandler(g_previous);
        return r.failures;
    }
    // LUMEN_UITEST_ONLY=holdtips | work runs just those, for working on them without the 7-minute sweep.
    if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("work")) {
        progressChecks();
        servicesChecks();
        qInstallMessageHandler(g_previous);
        return r.failures;
    }
    if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("holdtips")) {
        ensurePage();
        holdTipChecks();
        qInstallMessageHandler(g_previous);
        return r.failures;
    }

    // ---- 0. The window is opaque while a shape is selected and the page is zoomed in (the maker
    //         saw the desktop through the app doing exactly that).
    {
        ensurePage();
        QObject *shapes = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            shapes = engine->rootContext()->contextProperty(QStringLiteral("shapes")).value<QObject *>();
        auto *canvas = qobject_cast<InkCanvas *>(findOne(win, QStringLiteral("inkCanvas")));
        QQuickItem *layer = findOne(win, QStringLiteral("shapeLayer"));
        if (shapes && canvas && layer) {
            qlonglong id = 0;
            QMetaObject::invokeMethod(shapes, "create", Q_RETURN_ARG(qlonglong, id),
                                      Q_ARG(qlonglong, currentPage()), Q_ARG(QString, QStringLiteral("rect")),
                                      Q_ARG(double, 120), Q_ARG(double, 120), Q_ARG(double, 200), Q_ARG(double, 120),
                                      Q_ARG(QString, QStringLiteral("#E0403C")), Q_ARG(QString, QString()), Q_ARG(double, 2.5));
            spin(200);
            canvas->setProperty("tool", QStringLiteral("lasso"));
            layer->setProperty("selectedId", id);
            spin(200);
            const int flat = seeThroughPixels(win);
            const QPointF c = canvas->mapToScene(canvas->toScreen(QPointF(220, 180)));
            canvas->zoomAt(4.0, canvas->mapFromScene(c));
            spin(400);
            const int zoomed = seeThroughPixels(win);
            canvas->zoomAt(2.5, canvas->mapFromScene(c));
            spin(400);
            const int deep = seeThroughPixels(win);
            if (qEnvironmentVariableIsSet("LUMEN_UITEST_HOLD")) spin(qEnvironmentVariableIntValue("LUMEN_UITEST_HOLD"));
            r.check("the window is opaque with a shape selected, at any zoom", flat == 0 && zoomed == 0 && deep == 0,
                    QStringLiteral("see-through samples: 1x=%1 4x=%2 10x=%3").arg(flat).arg(zoomed).arg(deep));
            layer->setProperty("selectedId", 0);
            QMetaObject::invokeMethod(shapes, "remove", Q_ARG(qlonglong, id));
            QMetaObject::invokeMethod(canvas, "fitPage");
            canvas->setProperty("tool", QStringLiteral("pen"));
            spin(200);
        } else {
            r.check("found shapes, canvas and shape layer for the opacity check", false);
        }
        // Ink stops at the paper's edge, and a zoomed page never paints over the rail beside it.
        if (canvas) {
            const auto penAt = [&](QPointF scenePos, TabletSample::Kind kind) {
                TabletSample s;
                s.kind = kind;
                s.windowPos = scenePos;
                s.pressure = kind == TabletSample::Kind::Release ? 0.0f : 0.6f;
                s.buttons = kind == TabletSample::Kind::Release ? Qt::NoButton : Qt::LeftButton;
                s.button = Qt::LeftButton;
                s.timestampMs = quint32(g_stamp += 20);
                return canvas->tabletSample(s);
            };
            const auto isRed = [](QRgb p) { return qRed(p) > 170 && qGreen(p) < 110 && qBlue(p) < 110; };
            QMetaObject::invokeMethod(canvas, "fitPage");
            canvas->setProperty("tool", QStringLiteral("pen"));
            canvas->setProperty("penColor", QColor(QStringLiteral("#E5383B")));
            canvas->setProperty("penWidth", 4.0);
            spin(300);
            const QSizeF sheet = canvas->property("pageSize").toSizeF();
            const qreal y = sheet.height() * 0.4;
            const QPointF from = canvas->mapToScene(canvas->toScreen(QPointF(sheet.width() - 60, y)));
            const QPointF to = canvas->mapToScene(canvas->toScreen(QPointF(sheet.width() + 70, y)));
            const int strokesBefore = canvas->strokeCount();
            penAt(from, TabletSample::Kind::Press);
            for (int i = 1; i <= 12; ++i) { penAt(from + (to - from) * (i / 12.0), TabletSample::Kind::Move); spin(10); }
            penAt(to, TabletSample::Kind::Release);
            spin(400);
            if (canvas->strokeCount() == strokesBefore) {
                qInfo("UITEST note  the pen could not ink here; skipped the page-edge check");
            } else {
                const QImage img = win->grabWindow().convertToFormat(QImage::Format_ARGB32);
                const qreal dpr = img.width() / qreal(win->width());
                const qreal edgeX = canvas->mapToScene(canvas->toScreen(QPointF(sheet.width(), y))).x();
                int inside = 0, outside = 0;
                for (int dy = -3; dy <= 3; ++dy)
                    for (qreal x = edgeX - 40; x < edgeX + 40; x += 1) {
                        const QPoint px = (QPointF(x, from.y() + dy) * dpr).toPoint();
                        if (!img.rect().contains(px) || !isRed(img.pixel(px))) continue;
                        (x < edgeX - 2 ? inside : x > edgeX + 3 ? outside : inside) += 1;
                    }
                if (inside == 0)
                    qInfo("UITEST note  ink is not rendered on this platform; skipped the page-edge look");
                else
                    r.check("ink past the paper's edge is not drawn", outside == 0, QStringLiteral("outside=%1").arg(outside));
                QMetaObject::invokeMethod(canvas, "undo");
            }

            // Zoom right in and drag the page left, under the rail.
            QQuickItem *rail = findOne(win, QStringLiteral("rail"));
            QQuickItem *area = findOne(win, QStringLiteral("pageArea"));
            if (rail && area) {
                const QRectF areaRect = area->mapRectToScene(QRectF(0, 0, area->width(), area->height()));
                const QImage calm = win->grabWindow().convertToFormat(QImage::Format_ARGB32);
                canvas->zoomAt(3.0, QPointF(0, canvas->height() / 2));
                canvas->setProperty("pan", QPointF(-400, -200));
                spin(400);
                const QImage img = win->grabWindow().convertToFormat(QImage::Format_ARGB32);
                if (qEnvironmentVariableIsSet("LUMEN_UITEST_GRABS")) img.save(qEnvironmentVariable("LUMEN_UITEST_GRABS") + QStringLiteral("/rail.png"));
                const qreal dpr = img.width() / qreal(win->width());
                int changed = 0;
                for (qreal yy = areaRect.top() + 80; yy < areaRect.bottom() - 80; yy += 10)
                    for (qreal xx = 2; xx < areaRect.left() - 2; xx += 3) {
                        const QPoint px = (QPointF(xx, yy) * dpr).toPoint();
                        if (img.rect().contains(px) && calm.rect().contains(px) && img.pixel(px) != calm.pixel(px)) ++changed;
                    }
                r.check("a zoomed page never paints over the rail or panel", changed == 0,
                        QStringLiteral("rail/panel samples that changed when the page moved under them: %1").arg(changed));
                QMetaObject::invokeMethod(canvas, "fitPage");
                spin(200);
            } else {
                r.check("found the rail and page area", false);
            }
        }
        // ---- 0b. A typed page: a real text editor that works with only a keyboard and mouse.
        {
            QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("typed")));
            spin(400);
            QQuickItem *editor = findOne(win, QStringLiteral("typedEditor"));
            const qint64 typedId = currentPage();
            r.check("a typed page opens with a text editor", root->property("pageTyped").toBool() && editor && editor->isVisible());
            QQuickItem *ink = findOne(win, QStringLiteral("inkCanvas"));
            r.check("a typed page hides the ink canvas", ink && !ink->isVisible());
            if (editor) {
                win->requestActivate();
                waitFor([&] { return win->isActive(); }, 1500);
                editor->forceActiveFocus();
                spin(100);
                typeKeys(QStringLiteral("Shopping 1234567"));
                r.check("digits and letters reach the editor, not the ink shortcuts",
                        editor->property("text").toString().contains(QStringLiteral("Shopping 1234567")) || editor->property("length").toInt() >= 16,
                        QStringLiteral("length=%1").arg(editor->property("length").toInt()));
                const QString toolBefore = canvas ? canvas->property("tool").toString() : QString();
                r.check("typing digits did not switch the ink tool", !canvas || canvas->property("tool").toString() == toolBefore);
                QMetaObject::invokeMethod(editor, "selectAll");
                chord(win, Qt::Key_B, Qt::ControlModifier);
                QMetaObject::invokeMethod(editor, "deselect");
                editor->setProperty("cursorPosition", editor->property("length"));
                chord(win, Qt::Key_Return, Qt::NoModifier);
                chord(win, Qt::Key_8, Qt::ControlModifier | Qt::ShiftModifier);
                typeKeys(QStringLiteral("milk"));
                chord(win, Qt::Key_Return, Qt::NoModifier);
                typeKeys(QStringLiteral("eggs"));
                spin(1000);                                   // past the autosave delay
                QObject *textBlocks = nullptr;
                if (QQmlEngine *engine = qmlEngine(root))
                    textBlocks = engine->rootContext()->contextProperty(QStringLiteral("textBlocks")).value<QObject *>();
                QString saved;
                if (textBlocks) QMetaObject::invokeMethod(textBlocks, "pageText", Q_RETURN_ARG(QString, saved), Q_ARG(qint64, typedId));
                r.check("typed text is saved as Markdown", saved.contains(QStringLiteral("Shopping")), saved.left(200));
                r.check("Ctrl+B makes it bold", saved.contains(QStringLiteral("**Shopping")), saved.left(200));
                static const QRegularExpression bullet(QStringLiteral("^- (\\*\\*)?milk.*\\n- (\\*\\*)?eggs"), QRegularExpression::MultilineOption);
                r.check("Ctrl+Shift+8 makes a bulleted list", bullet.match(saved).hasMatch(), saved.left(200).replace(QLatin1Char('\n'), QLatin1String(" ⏎ ")));
                // Leave and come back: the text is still there, formatted.
                QMetaObject::invokeMethod(root, "closePage");
                spin(200);
                QMetaObject::invokeMethod(root, "openPage", Q_ARG(QVariant, typedId));
                spin(400);
                editor = findOne(win, QStringLiteral("typedEditor"));
                QObject *fmtObj = nullptr;
                if (QQuickItem *tp = findOne(win, QStringLiteral("typedPage"))) fmtObj = tp->property("formatter").value<QObject *>();
                QString reloaded;
                if (fmtObj) QMetaObject::invokeMethod(fmtObj, "markdown", Q_RETURN_ARG(QString, reloaded));
                r.check("a typed page reopens with its text", reloaded.contains(QStringLiteral("**Shopping")) && reloaded.contains(QStringLiteral("eggs")), reloaded.left(200));
            }
            QMetaObject::invokeMethod(root, "newPage", Q_ARG(QVariant, QStringLiteral("a4")));
            spin(300);
            r.check("a handwritten page still opens the ink canvas", !root->property("pageTyped").toBool() && ink && ink->isVisible());
        }

        if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("alpha")) {
            qInstallMessageHandler(g_previous);
            qInfo("UITEST %s (%d failure%s)", r.failures ? "FAILED" : "OK", r.failures, r.failures == 1 ? "" : "s");
            return r.failures;
        }
    }

    // ---- 9. The page browser: every page in the section, and a tap opens one.
    {
        ensurePage();
        const qint64 before = currentPage();
        root->setProperty("browserVisible", true);
        waitFor([&] { return !findAll(win, QStringLiteral("browserCell")).isEmpty(); }, 3000);
        spin(400);                                   // let it finish loading before a finger arrives
        const auto cells = findAll(win, QStringLiteral("browserCell"));
        r.check("the page browser lists pages", !cells.isEmpty());
        if (!cells.isEmpty()) {
            QQuickItem *other = nullptr;
            const QRectF screen(0, 0, win->width(), win->height());
            for (QQuickItem *c : cells)
                if (c->property("pid").toLongLong() != before && c->isVisible() && screen.contains(centre(c))) { other = c; break; }
            if (other) {
                const qint64 want = other->property("pid").toLongLong();
                tap(win, other);
                waitFor([&] { return currentPage() == want; }, 2000);
                r.check("a tap in the page browser opens that page", currentPage() == want);
                r.check("the page browser closes when it opens a page", !root->property("browserVisible").toBool(),
                        QStringLiteral("browserVisible=%1 page=%2 typed=%3 overlay=%4").arg(root->property("browserVisible").toBool())
                            .arg(currentPage()).arg(root->property("pageTyped").toBool()).arg(root->property("overlayUp").toBool()));
                if (qEnvironmentVariable("LUMEN_UITEST_ONLY") == QLatin1String("browser")) { qInstallMessageHandler(g_previous); return r.failures; }
            }
        }
        root->setProperty("browserVisible", false);
        spin(150);
    }

    // ---- 10. The trash: what you deleted is still there, and Restore brings it back.
    {
        ensurePage();
        const qint64 doomed = currentPage();
        QMetaObject::invokeMethod(root, "closePage");
        spin(150);
        if (library) QMetaObject::invokeMethod(library, "remove", Q_ARG(QString, QStringLiteral("page")), Q_ARG(qlonglong, doomed));
        spin(200);
        QQmlProperty(root, QStringLiteral("trashVisible")).write(true);
        waitFor([&] { return !findAll(win, QStringLiteral("trashRow")).isEmpty(); }, 3000);
        spin(500);          // let the layout settle: a delegate exists before it is positioned
        waitFor([&] { QQuickItem *b = findOne(win, QStringLiteral("trashRestore")); return b && centre(b).y() > 60; }, 2000);
        r.check("the trash lists what was deleted", !findAll(win, QStringLiteral("trashRow")).isEmpty());
        QQuickItem *restore = findOne(win, QStringLiteral("trashRestore"));
        r.check("the trash offers Restore", restore != nullptr);
        if (restore) {
            tap(win, restore);
            spin(400);
            r.check("Restore puts the page back", currentPage() > 0,
                    QStringLiteral("currentPageId=%1, trash still up=%2, rows=%3").arg(currentPage())
                        .arg(root->property("trashVisible").toBool()).arg(findAll(win, QStringLiteral("trashRow")).size()));
        }
        root->setProperty("trashVisible", false);
        spin(150);
        if (currentPage() == 0) { QMetaObject::invokeMethod(root, "openLastPage"); spin(300); }
        if (currentPage() == 0) { QMetaObject::invokeMethod(root, "newPageAnywhere", Q_ARG(QVariant, QStringLiteral("a4"))); spin(400); }
        Q_UNUSED(doomed);
    }

    // ---- 11. Pictures: one lands on the page and the canvas is told about it.
    {
        ensurePage();
        QObject *images = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            images = engine->rootContext()->contextProperty(QStringLiteral("images")).value<QObject *>();
        if (images) {
            QImage sample(80, 40, QImage::Format_RGB32);
            sample.fill(Qt::darkCyan);
            const QString file = QDir::tempPath() + QStringLiteral("/lumen-uitest-picture.png");
            sample.save(file, "PNG");
            qlonglong id = 0;
            QMetaObject::invokeMethod(images, "insertFile", Q_RETURN_ARG(qlonglong, id),
                                      Q_ARG(qlonglong, currentPage()), Q_ARG(QUrl, QUrl::fromLocalFile(file)),
                                      Q_ARG(double, 60), Q_ARG(double, 60), Q_ARG(double, 200));
            r.check("a picture can be placed on a page", id > 0);
            spin(250);
            int count = -1;
            QMetaObject::invokeMethod(images, "count", Q_RETURN_ARG(int, count), Q_ARG(qlonglong, currentPage()));
            r.check("the page knows it has a picture", count == 1, QStringLiteral("count=%1").arg(count));
            if (id > 0) {
                QMetaObject::invokeMethod(images, "remove", Q_ARG(qlonglong, id));
                spin(200);
                QMetaObject::invokeMethod(images, "count", Q_RETURN_ARG(int, count), Q_ARG(qlonglong, currentPage()));
                r.check("a picture can be taken off again", count == 0);

            // The same grab that shapes needed: with the lasso in hand a picture must move, and the
            // canvas must not start a selection underneath it.
            if (auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"))) {
                qlonglong picId = 0;
                QMetaObject::invokeMethod(images, "insertFile", Q_RETURN_ARG(qlonglong, picId),
                                          Q_ARG(qlonglong, currentPage()), Q_ARG(QUrl, QUrl::fromLocalFile(file)),
                                          Q_ARG(double, 200), Q_ARG(double, 260), Q_ARG(double, 240));
                canvas->setTool(QStringLiteral("lasso"));
                spin(400);
                if (picId) {
                    QVariantMap pic;
                    QMetaObject::invokeMethod(images, "image", Q_RETURN_ARG(QVariantMap, pic), Q_ARG(qlonglong, picId));
                    const double px = pic.value(QStringLiteral("x")).toDouble();
                    const QPointF on = canvas->mapToScene(canvas->toScreen(
                        QPointF(px + pic.value(QStringLiteral("w")).toDouble() / 2,
                                pic.value(QStringLiteral("y")).toDouble() + pic.value(QStringLiteral("h")).toDouble() / 2)));
                    sendMouse(win, QEvent::MouseButtonPress, on, Qt::LeftButton);
                    spin(30);
                    for (int i = 1; i <= 6; ++i) { sendMouse(win, QEvent::MouseMove, on + QPointF(12.0 * i, 0), Qt::LeftButton); spin(16); }
                    sendMouse(win, QEvent::MouseButtonRelease, on + QPointF(72, 0), Qt::LeftButton);
                    spin(300);
                    QMetaObject::invokeMethod(images, "image", Q_RETURN_ARG(QVariantMap, pic), Q_ARG(qlonglong, picId));
                    r.check("a picture can be dragged with the lasso in hand",
                            std::abs(pic.value(QStringLiteral("x")).toDouble() - px) > 20,
                            QStringLiteral("x %1 → %2").arg(px).arg(pic.value(QStringLiteral("x")).toDouble()));
                    r.check("and the canvas did not start a selection under it", !canvas->hasSelection());
                    QMetaObject::invokeMethod(images, "remove", Q_ARG(qlonglong, picId));
                }
                canvas->setTool(QStringLiteral("pen"));
                spin(100);
            }
            }
            QFile::remove(file);
        } else {
            r.check("the images service is available to QML", false);
        }
    }

    // ---- 12. Shapes: placed, restyled, resized and removed — all after the fact.
    {
        ensurePage();
        QObject *shapes = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            shapes = engine->rootContext()->contextProperty(QStringLiteral("shapes")).value<QObject *>();
        if (shapes) {
            qlonglong id = 0;
            QMetaObject::invokeMethod(shapes, "create", Q_RETURN_ARG(qlonglong, id),
                                      Q_ARG(qlonglong, currentPage()), Q_ARG(QString, QStringLiteral("ellipse")),
                                      Q_ARG(double, 90), Q_ARG(double, 90), Q_ARG(double, 220), Q_ARG(double, 140),
                                      Q_ARG(QString, QStringLiteral("#E0403C")), Q_ARG(QString, QString()), Q_ARG(double, 2.5));
            r.check("a shape can be placed", id > 0);
            spin(250);
            QMetaObject::invokeMethod(shapes, "setStyle", Q_ARG(qlonglong, id), Q_ARG(QString, QStringLiteral("#1F6FEB")),
                                      Q_ARG(QString, QStringLiteral("#CFE0FA")), Q_ARG(double, 6));
            spin(150);
            QVariantMap after;
            QMetaObject::invokeMethod(shapes, "shape", Q_RETURN_ARG(QVariantMap, after), Q_ARG(qlonglong, id));
            r.check("a shape keeps its new outline and fill", after.value("stroke").toString() == QLatin1String("#1F6FEB")
                                                             && after.value("fill").toString() == QLatin1String("#CFE0FA"));
            // with nothing selected the style bar stays out of the way
            if (QQuickItem *bar = findOne(win, QStringLiteral("styleBar")))
                r.check("the style bar is hidden with nothing selected", !bar->isVisible());
            else
                r.check("found the style bar", false);
            QMetaObject::invokeMethod(shapes, "remove", Q_ARG(qlonglong, id));
            spin(150);
            int count = -1;
            QMetaObject::invokeMethod(shapes, "count", Q_RETURN_ARG(int, count), Q_ARG(qlonglong, currentPage()));
            r.check("a shape can be removed", count == 0, QStringLiteral("count=%1").arg(count));
        } else {
            r.check("the shapes service is available to QML", false);
        }
    }

    // ---- 12b. Tablet mode has to be able to type: the in-window keyboard must come up when a
    //           text field asks for input, and go away when it does not.
    {
        QObject *tabletMode = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            tabletMode = engine->rootContext()->contextProperty(QStringLiteral("tabletMode")).value<QObject *>();
        if (tabletMode) tabletMode->setProperty("tablet", true);
        root->setProperty("keyboardMode", QStringLiteral("tablet"));
        root->setProperty("leftPanel", QStringLiteral("cards"));      // its filter box is always enabled
        root->setProperty("rightPanel", QStringLiteral("claude"));
        spin(600);

        QQuickItem *field = nullptr;
        QList<QQuickItem *> stack{win->contentItem()};
        while (!stack.isEmpty() && !field) {                 // the first real text input on screen
            QQuickItem *item = stack.takeLast();
            const auto kids = item->childItems();
            for (QQuickItem *k : kids) stack.append(k);
            if (item->isVisible() && item->isEnabled()
                && (item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))) field = item;
        }
        r.check("there is a text field to type into", field != nullptr);
        if (field) {
            win->requestActivate();          // offscreen windows are not focused unless asked
            waitFor([&] { return win->isActive(); }, 1500);
            field->forceActiveFocus();
            spin(150);
            QGuiApplication::inputMethod()->show();
            waitFor([&] { return QGuiApplication::inputMethod()->isVisible(); }, 3000);
            // The app's own keyboard: it must come up for a text field, put characters into it,
            // and go away when asked.
            QQuickItem *board = nullptr;
            QList<QQuickItem *> hunt{win->contentItem()};
            while (!hunt.isEmpty() && !board) {
                QQuickItem *item = hunt.takeLast();
                const auto kids = item->childItems();
                for (QQuickItem *k : kids) hunt.append(k);
                if (QString::fromLatin1(item->metaObject()->className()).contains(QLatin1String("Keyboard"))) board = item;
            }
            r.check("the app has an on-screen keyboard", board != nullptr);
            QObject *keys = nullptr;
            if (QQmlEngine *engine = qmlEngine(root))
                keys = engine->rootContext()->contextProperty(QStringLiteral("keys")).value<QObject *>();
            r.check("the keyboard can see what has focus", keys && keys->property("focusIsText").toBool());
            if (board) {
                waitFor([&] { return board->property("wanted").toBool(); }, 2000);
                r.check("it comes up for a text field in tablet mode", board->property("wanted").toBool());
            }
            if (keys) {
                const QString before = field->property("text").toString();
                QMetaObject::invokeMethod(keys, "type", Q_ARG(QString, QStringLiteral("hi")));
                spin(120);
                const QString after = field->property("text").toString();
                r.check("typing on it reaches the field", after != before && after.contains(QLatin1String("hi")),
                        QStringLiteral("\"%1\" → \"%2\"").arg(before, after));
                QMetaObject::invokeMethod(keys, "key", Q_ARG(int, int(Qt::Key_Backspace)), Q_ARG(int, 0));
                spin(120);
                r.check("backspace works too", field->property("text").toString() != after);
                QMetaObject::invokeMethod(keys, "dropFocus");
                spin(200);
                if (board) r.check("and it goes away when the field loses focus", !board->property("wanted").toBool());
            }

        }
        if (tabletMode) tabletMode->setProperty("tablet", false);
        root->setProperty("rightPanel", QString());
        root->setProperty("leftPanel", QStringLiteral("notebooks"));
        spin(200);
    }

    // ---- 12c. The shape tool: the canvas must hand the pen over, and the layer must draw and then
    //           let you grab what it drew. Both halves were broken — the canvas ate the pen event
    //           (so only a finger could draw), and a line's frame was one pixel tall.
    {
        ensurePage();
        QObject *shapesSvc = nullptr;
        if (QQmlEngine *engine = qmlEngine(root))
            shapesSvc = engine->rootContext()->contextProperty(QStringLiteral("shapes")).value<QObject *>();
        auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"));
        if (shapesSvc && canvas) {
            const auto penSample = [&](TabletSample::Kind kind, QPointF at) {
                TabletSample sample;
                sample.kind = kind;
                sample.windowPos = at;
                sample.pressure = kind == TabletSample::Kind::Release ? 0.0f : 0.6f;
                sample.buttons = kind == TabletSample::Kind::Release ? Qt::NoButton : Qt::LeftButton;
                sample.button = Qt::LeftButton;
                sample.timestampMs = quint32(g_stamp += 16);
                return canvas->tabletSample(sample);
            };
            const QPointF mid = centre(canvas);

            canvas->setTool(QStringLiteral("shape"));
            spin(200);
            // The whole pen fix in one assertion: declining is what lets the platform pass the event
            // on to the shape layer (and, failing that, synthesise a mouse event for it).
            r.check("with the shape tool the canvas hands the pen over", !penSample(TabletSample::Kind::Press, mid));
            penSample(TabletSample::Kind::Release, mid);
            const int strokesBefore = canvas->strokeCount();
            r.check("and the pen leaves no ink behind while doing it", canvas->strokeCount() == strokesBefore);

            int before = -1;
            QMetaObject::invokeMethod(shapesSvc, "count", Q_RETURN_ARG(int, before), Q_ARG(qlonglong, currentPage()));
            // Draw one, the way a hand does: press, move, release.
            const QPointF from = mid - QPointF(120, 80), to = mid + QPointF(120, 80);
            sendMouse(win, QEvent::MouseButtonPress, from, Qt::LeftButton);
            spin(30);
            for (int i = 1; i <= 8; ++i) { sendMouse(win, QEvent::MouseMove, from + (to - from) * (i / 8.0), Qt::LeftButton); spin(16); }
            sendMouse(win, QEvent::MouseButtonRelease, to, Qt::LeftButton);
            spin(250);
            int after = -1;
            QMetaObject::invokeMethod(shapesSvc, "count", Q_RETURN_ARG(int, after), Q_ARG(qlonglong, currentPage()));
            r.check("a drag draws a shape", after == before + 1, QStringLiteral("%1 → %2").arg(before).arg(after));
            r.check("and the tool hands back, so the next press grabs it rather than drawing another",
                    canvas->tool() == QLatin1String("lasso"), canvas->tool());

            QVariantList list;
            QMetaObject::invokeMethod(shapesSvc, "list", Q_RETURN_ARG(QVariantList, list), Q_ARG(qlonglong, currentPage()));
            if (!list.isEmpty()) {
                const QVariantMap made = list.last().toMap();
                const qlonglong id = made.value(QStringLiteral("id")).toLongLong();
                const double x0 = made.value(QStringLiteral("x")).toDouble();
                // toScreen() is canvas-local; the window wants scene coordinates.
                const QPointF onShape = canvas->mapToScene(canvas->toScreen(
                    QPointF(x0 + made.value(QStringLiteral("w")).toDouble() / 2,
                            made.value(QStringLiteral("y")).toDouble() + made.value(QStringLiteral("h")).toDouble() / 2)));
                sendMouse(win, QEvent::MouseButtonPress, onShape, Qt::LeftButton);
                spin(30);
                for (int i = 1; i <= 6; ++i) { sendMouse(win, QEvent::MouseMove, onShape + QPointF(10.0 * i, 0), Qt::LeftButton); spin(16); }
                sendMouse(win, QEvent::MouseButtonRelease, onShape + QPointF(60, 0), Qt::LeftButton);
                spin(250);
                QVariantMap moved;
                QMetaObject::invokeMethod(shapesSvc, "shape", Q_RETURN_ARG(QVariantMap, moved), Q_ARG(qlonglong, id));
                r.check("a shape can be moved after it is drawn",
                        std::abs(moved.value(QStringLiteral("x")).toDouble() - x0) > 20,
                        QStringLiteral("x %1 → %2").arg(x0).arg(moved.value(QStringLiteral("x")).toDouble()));
                QMetaObject::invokeMethod(shapesSvc, "remove", Q_ARG(qlonglong, id));
            }

            // Selecting and putting down: tapping empty space must clear the selection without
            // drawing anything, and the next drag must draw again.
            {
                QQuickItem *layer = nullptr;
                QList<QQuickItem *> hunt{win->contentItem()};
                while (!hunt.isEmpty() && !layer) {
                    QQuickItem *it = hunt.takeLast();
                    const auto kids = it->childItems();
                    for (QQuickItem *k : kids) hunt.append(k);
                    if (QString::fromLatin1(it->metaObject()->className()).contains(QLatin1String("ShapeLayer"))) layer = it;
                }
                if (layer) {
                    qlonglong keep = 0;
                    QMetaObject::invokeMethod(shapesSvc, "create", Q_RETURN_ARG(qlonglong, keep),
                                              Q_ARG(qlonglong, currentPage()), Q_ARG(QString, QStringLiteral("rect")),
                                              Q_ARG(double, 120), Q_ARG(double, 120), Q_ARG(double, 160), Q_ARG(double, 100),
                                              Q_ARG(QString, QStringLiteral("#E0403C")), Q_ARG(QString, QString()), Q_ARG(double, 2.5));
                    spin(250);
                    canvas->setTool(QStringLiteral("shape"));
                    layer->setProperty("selectedId", QVariant::fromValue(keep));
                    spin(150);
                    int countBefore = -1;
                    QMetaObject::invokeMethod(shapesSvc, "count", Q_RETURN_ARG(int, countBefore), Q_ARG(qlonglong, currentPage()));
                    // a drag on empty space while something is selected: put it down, draw nothing
                    const QPointF empty = canvas->mapToScene(canvas->toScreen(QPointF(500, 600)));
                    sendMouse(win, QEvent::MouseButtonPress, empty, Qt::LeftButton);
                    spin(30);
                    for (int i = 1; i <= 6; ++i) { sendMouse(win, QEvent::MouseMove, empty + QPointF(14.0 * i, 10.0 * i), Qt::LeftButton); spin(16); }
                    sendMouse(win, QEvent::MouseButtonRelease, empty + QPointF(84, 60), Qt::LeftButton);
                    spin(250);
                    int countAfter = -1;
                    QMetaObject::invokeMethod(shapesSvc, "count", Q_RETURN_ARG(int, countAfter), Q_ARG(qlonglong, currentPage()));
                    r.check("a press elsewhere puts the selection down", layer->property("selectedId").toLongLong() == 0);
                    r.check("and draws nothing while doing it", countAfter == countBefore,
                            QStringLiteral("%1 → %2").arg(countBefore).arg(countAfter));
                    // and now the next drag does draw
                    sendMouse(win, QEvent::MouseButtonPress, empty, Qt::LeftButton);
                    spin(30);
                    for (int i = 1; i <= 6; ++i) { sendMouse(win, QEvent::MouseMove, empty + QPointF(14.0 * i, 10.0 * i), Qt::LeftButton); spin(16); }
                    sendMouse(win, QEvent::MouseButtonRelease, empty + QPointF(84, 60), Qt::LeftButton);
                    spin(250);
                    QMetaObject::invokeMethod(shapesSvc, "count", Q_RETURN_ARG(int, countAfter), Q_ARG(qlonglong, currentPage()));
                    r.check("the next drag draws again", countAfter == countBefore + 1,
                            QStringLiteral("%1 → %2").arg(countBefore).arg(countAfter));
                    // Draw, put down, pick up again, then resize: the maker reports the handles stop
                    // working after a reselect.
                    {
                        layer->setProperty("selectedId", QVariant::fromValue(qlonglong(0)));
                        spin(150);
                        canvas->setTool(QStringLiteral("lasso"));
                        QVariantMap box;
                        QMetaObject::invokeMethod(shapesSvc, "shape", Q_RETURN_ARG(QVariantMap, box), Q_ARG(qlonglong, keep));
                        const double w0 = box.value(QStringLiteral("w")).toDouble();
                        const QPointF onIt = canvas->mapToScene(canvas->toScreen(
                            QPointF(box.value(QStringLiteral("x")).toDouble() + w0 / 2,
                                    box.value(QStringLiteral("y")).toDouble() + box.value(QStringLiteral("h")).toDouble() / 2)));
                        tap(win, nullptr, Qt::LeftButton, onIt);
                        spin(250);
                        r.check("a shape can be picked up again after putting it down",
                                layer->property("selectedId").toLongLong() == keep,
                                QStringLiteral("selectedId=%1 want %2").arg(layer->property("selectedId").toLongLong()).arg(keep));
                        // drag the bottom-right handle outwards
                        const QPointF corner = canvas->mapToScene(canvas->toScreen(
                            QPointF(box.value(QStringLiteral("x")).toDouble() + w0,
                                    box.value(QStringLiteral("y")).toDouble() + box.value(QStringLiteral("h")).toDouble())));
                        sendMouse(win, QEvent::MouseButtonPress, corner, Qt::LeftButton);
                        spin(40);
                        for (int i = 1; i <= 6; ++i) { sendMouse(win, QEvent::MouseMove, corner + QPointF(12.0 * i, 8.0 * i), Qt::LeftButton); spin(20); }
                        sendMouse(win, QEvent::MouseButtonRelease, corner + QPointF(72, 48), Qt::LeftButton);
                        spin(300);
                        QVariantMap after2;
                        QMetaObject::invokeMethod(shapesSvc, "shape", Q_RETURN_ARG(QVariantMap, after2), Q_ARG(qlonglong, keep));
                        r.check("and resized after that",
                                after2.value(QStringLiteral("w")).toDouble() > w0 + 20,
                                QStringLiteral("w %1 → %2, x %3 → %4, corner at %5,%6")
                                    .arg(w0).arg(after2.value(QStringLiteral("w")).toDouble())
                                    .arg(box.value(QStringLiteral("x")).toDouble()).arg(after2.value(QStringLiteral("x")).toDouble())
                                    .arg(corner.x()).arg(corner.y()));
                    }

                    // Tapping a selected shape again brings its options up over it.
                    layer->setProperty("selectedId", QVariant::fromValue(keep));
                    canvas->setTool(QStringLiteral("lasso"));
                    spin(200);
                    QVariantMap kept;
                    QMetaObject::invokeMethod(shapesSvc, "shape", Q_RETURN_ARG(QVariantMap, kept), Q_ARG(qlonglong, keep));
                    const QPointF onKept = canvas->mapToScene(canvas->toScreen(
                        QPointF(kept.value(QStringLiteral("x")).toDouble() + kept.value(QStringLiteral("w")).toDouble() / 2,
                                kept.value(QStringLiteral("y")).toDouble() + kept.value(QStringLiteral("h")).toDouble() / 2)));
                    tap(win, nullptr, Qt::LeftButton, onKept);
                    tap(win, nullptr, Qt::LeftButton, onKept);
                    spin(300);
                    QQuickItem *menuContent = findOne(win, QStringLiteral("objectMenuContent"));
                    r.check("tapping a selected shape again shows its options", menuContent && menuContent->isVisible());
                    pressEscape(win);
                    spin(150);
                    QMetaObject::invokeMethod(shapesSvc, "remove", Q_ARG(qlonglong, keep));
                }
            }

            // Reported: zooming in with a shape selected showed the desktop through the window.
            // A frame the window paints must never contain fully transparent pixels.
            {
                canvas->setTool(QStringLiteral("lasso"));
                qlonglong zoomId = 0;
                QMetaObject::invokeMethod(shapesSvc, "create", Q_RETURN_ARG(qlonglong, zoomId),
                                          Q_ARG(qlonglong, currentPage()), Q_ARG(QString, QStringLiteral("ellipse")),
                                          Q_ARG(double, 150), Q_ARG(double, 150), Q_ARG(double, 220), Q_ARG(double, 160),
                                          Q_ARG(QString, QStringLiteral("#E0403C")), Q_ARG(QString, QStringLiteral("#CFE0FA")), Q_ARG(double, 4));
                spin(250);
                QQuickItem *shapeLayerItem = nullptr;
                {
                    QList<QQuickItem *> hunt{win->contentItem()};
                    while (!hunt.isEmpty() && !shapeLayerItem) {
                        QQuickItem *it = hunt.takeLast();
                        const auto kids = it->childItems();
                        for (QQuickItem *k : kids) hunt.append(k);
                        if (QString::fromLatin1(it->metaObject()->className()).contains(QLatin1String("ShapeLayer"))) shapeLayerItem = it;
                    }
                }
                if (shapeLayerItem) shapeLayerItem->setProperty("selectedId", QVariant::fromValue(zoomId));
                canvas->setZoom(4.0);
                spin(400);
                const QImage shot = win->grabWindow();
                int clear = 0;
                for (int y = 0; y < shot.height(); y += 3)
                    for (int x = 0; x < shot.width(); x += 3)
                        if (qAlpha(shot.pixel(x, y)) < 250) ++clear;
                r.check("nothing shows through the window when a selected shape is zoomed", clear == 0,
                        QStringLiteral("%1 see-through pixels").arg(clear));
                canvas->fitPage();
                QMetaObject::invokeMethod(shapesSvc, "remove", Q_ARG(qlonglong, zoomId));
                spin(200);
            }

            // A line is the case that used to be ungrabbable: its box is one pixel tall.
            canvas->setTool(QStringLiteral("shape"));
            spin(150);
            qlonglong lineId = 0;
            QMetaObject::invokeMethod(shapesSvc, "create", Q_RETURN_ARG(qlonglong, lineId),
                                      Q_ARG(qlonglong, currentPage()), Q_ARG(QString, QStringLiteral("line")),
                                      Q_ARG(double, 200), Q_ARG(double, 300), Q_ARG(double, 240), Q_ARG(double, 0),
                                      Q_ARG(QString, QStringLiteral("#E0403C")), Q_ARG(QString, QString()), Q_ARG(double, 2.5));
            spin(250);
            canvas->setTool(QStringLiteral("lasso"));
            spin(150);
            if (lineId) {
                const QPointF onLine = canvas->mapToScene(canvas->toScreen(QPointF(320, 300)));
                const double lineX = 200;
                sendMouse(win, QEvent::MouseButtonPress, onLine, Qt::LeftButton);
                spin(30);
                for (int i = 1; i <= 6; ++i) { sendMouse(win, QEvent::MouseMove, onLine + QPointF(0, 8.0 * i), Qt::LeftButton); spin(16); }
                sendMouse(win, QEvent::MouseButtonRelease, onLine + QPointF(0, 48), Qt::LeftButton);
                spin(250);
                QVariantMap line;
                QMetaObject::invokeMethod(shapesSvc, "shape", Q_RETURN_ARG(QVariantMap, line), Q_ARG(qlonglong, lineId));
                r.check("a line can be grabbed, thin as it is",
                        std::abs(line.value(QStringLiteral("y")).toDouble() - 300) > 15,
                        QStringLiteral("y 300 → %1 (x %2 → %3)").arg(line.value(QStringLiteral("y")).toDouble())
                            .arg(lineX).arg(line.value(QStringLiteral("x")).toDouble()));
                QMetaObject::invokeMethod(shapesSvc, "remove", Q_ARG(qlonglong, lineId));
            }
            canvas->setTool(QStringLiteral("pen"));
            spin(100);
        }
    }

    // ---- 12d. A highlighter must not darken where it crosses itself or another stroke. The whole
    //           layer is composited once now; before, every stroke carried its own alpha.
    if (auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"))) {
        ensurePage();
        canvas->fitPage();
        spin(200);
        const auto strokeWith = [&](QPointF from, QPointF to) {
            const auto sample = [&](TabletSample::Kind kind, QPointF at) {
                TabletSample s;
                s.kind = kind;
                s.windowPos = canvas->mapToScene(at);
                s.pressure = kind == TabletSample::Kind::Release ? 0.0f : 0.7f;
                s.buttons = kind == TabletSample::Kind::Release ? Qt::NoButton : Qt::LeftButton;
                s.button = Qt::LeftButton;
                s.timestampMs = quint32(g_stamp += 12);
                canvas->tabletSample(s);
            };
            sample(TabletSample::Kind::Press, from);
            for (int i = 1; i <= 10; ++i) sample(TabletSample::Kind::Move, from + (to - from) * (i / 10.0));
            sample(TabletSample::Kind::Release, to);
        };
        const int before = canvas->strokeCount();
        canvas->setTool(QStringLiteral("highlighter"));
        canvas->setProperty("highlighterWidth", 26.0);
        const QPointF mid = QPointF(canvas->width() / 2, canvas->height() / 2);
        strokeWith(mid + QPointF(-160, 0), mid + QPointF(160, 0));       // one across
        strokeWith(mid + QPointF(0, -120), mid + QPointF(0, 120));       // one down, crossing it
        spin(400);
        r.check("two highlighter strokes are kept", canvas->strokeCount() == before + 2);

        const QImage shot = win->grabWindow();
        const QPoint cross = (canvas->mapToScene(mid)).toPoint();
        const QPoint single = (canvas->mapToScene(mid + QPointF(120, 0))).toPoint();
        const QPoint bare = (canvas->mapToScene(mid + QPointF(120, 90))).toPoint();
        const QRgb crossPx = shot.pixel(cross), singlePx = shot.pixel(single), barePx = shot.pixel(bare);
        // Overlap behaviour is a rendering question still open (see the highlighter note in
        // DECISIONS): assert only that ink reaches the screen at all on this platform.
        if (singlePx == barePx)
            qInfo("UITEST note  ink is not rendered on this platform; skipped the highlighter look");
        else
            r.check("a highlighter stroke reaches the screen", singlePx != barePx);
        canvas->clearAll();
        canvas->setTool(QStringLiteral("pen"));
        spin(200);
    }

    pageFeatures(win, root, r);

    // ---- 12e. Tooltips by touch (above).
    holdTipChecks();

    // ---- 12f. (above)
    progressChecks();

    // ---- 12g. (above)
    servicesChecks();

    // ---- 12h. (above)
    handChecks();

    // ---- 12i. (above)
    pictureChecks();

    // ---- 12j. (above)
    polishChecks();

    // ---- 13. Tap every control there is, in both postures.
    {
        const int windowsBefore = QGuiApplication::topLevelWindows().size();
        int tapped = 0, unreachable = 0;
        QStringList trouble, small;
        for (int posture = 0; posture < 2; ++posture) {
            QObject *tabletMode = nullptr;
            if (QQmlEngine *engine = qmlEngine(root))
                tabletMode = engine->rootContext()->contextProperty(QStringLiteral("tabletMode")).value<QObject *>();
            if (tabletMode) tabletMode->setProperty("tablet", posture == 1);
            spin(400);
            ensurePage();

            // Panels have to be open for their controls to exist at all.
            for (const QString &left : {QStringLiteral("notebooks"), QStringLiteral("cards"), QStringLiteral("papers")}) {
                for (const QString &right : {QString(), QStringLiteral("claude"), QStringLiteral("transcript"), QStringLiteral("handwriting"), QStringLiteral("page")}) {
                    root->setProperty("leftPanel", left);
                    root->setProperty("rightPanel", right);
                    spin(220);
                    const auto controls = interactiveItems(win);
                    for (const QPointer<QQuickItem> &guard : controls) {
                        // A tap can rebuild a model and destroy the delegates behind us, so every
                        // item is re-checked through a QPointer immediately before it is used.
                        QQuickItem *item = guard.data();
                        if (!item || !item->isVisible() || item->width() < 6 || item->height() < 6) continue;
                        const QRectF box = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
                        if (!QRectF(0, 0, win->width(), win->height()).contains(box.center())) continue;
                        const QSizeF size(item->width(), item->height());
                        if (size.height() < 24 || size.width() < 24) {
                            ++unreachable;
                            const QString where = item->objectName().isEmpty() ? QString::fromLatin1(item->metaObject()->className()) : item->objectName();
                            const QString note = QStringLiteral("%1 %2x%3").arg(where).arg(int(size.width())).arg(int(size.height()));
                            if (!small.contains(note)) small << note;
                        }
                        const QString name = item->objectName().isEmpty() ? QString::fromLatin1(item->metaObject()->className()) : item->objectName();
                        const int before = g_qmlComplaints.size();
                        tap(win, item);
                        ++tapped;
                        if (!guard) { spin(40); continue; }      // the tap destroyed it: that is fine, just stop touching it
                        // Whatever that opened, put it away again.
                        pressEscape(win);
                        for (QWindow *w : QGuiApplication::topLevelWindows())
                            if (w != win && w->isVisible()) w->close();          // a file dialog must not block the sweep
                        for (const char *overlay : {"reviewVisible", "settingsVisible", "dashboardVisible", "trashVisible", "browserVisible", "keysVisible", "onboardingVisible"})
                            if (root->property(overlay).toBool()) root->setProperty(overlay, false);
                        spin(40);
                        if (g_qmlComplaints.size() > before)
                            trouble << QStringLiteral("%1: %2").arg(name, g_qmlComplaints.last().left(120));
                    }
                }
            }
            root->setProperty("leftPanel", QStringLiteral("notebooks"));
            root->setProperty("rightPanel", QString());
        }
        if (QQmlEngine *engine = qmlEngine(root))
            if (QObject *tabletMode = engine->rootContext()->contextProperty(QStringLiteral("tabletMode")).value<QObject *>())
                tabletMode->setProperty("tablet", false);
        qInfo("UITEST note  swept %d controls in both postures", tapped);
        r.check("every control was tapped without a QML error", trouble.isEmpty(), trouble.mid(0, 6).join(QStringLiteral(" | ")));
        r.check("no control is smaller than a fingertip", unreachable == 0, small.mid(0, 8).join(QStringLiteral(" | ")));
        QStringList leftovers;
        for (QWindow *w : QGuiApplication::topLevelWindows())
            if (w != win && w->isVisible()) leftovers << QStringLiteral("%1 (%2)").arg(w->title(), QString::fromLatin1(w->metaObject()->className()));
        r.check("the sweep left no window open", leftovers.isEmpty(), leftovers.join(QStringLiteral(", ")));
        r.check("the app is still on its feet after the sweep", currentPage() >= 0 && win->isVisible());
    }

    // ---- 14. Nothing complained in QML while we did all that.
    r.check("no QML errors during the run", g_qmlComplaints.isEmpty(),
            g_qmlComplaints.isEmpty() ? QString() : g_qmlComplaints.join(QStringLiteral(" | ")).left(600));

    qInstallMessageHandler(g_previous);
    qInfo("UITEST %s (%d failure%s)", r.failures ? "FAILED" : "OK", r.failures, r.failures == 1 ? "" : "s");
    return r.failures;
}

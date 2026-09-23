#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QTimer>
#include "canvas/inkcanvas.h"
#include "input/keyinjector.h"
#include "input/penprobeitem.h"
#include "input/tableteventfilter.h"
#include "pdf/pdfservice.h"
#include "ui/themeiconprovider.h"
#include "ui/lateximageprovider.h"
#include "text/textblocks.h"
#include "audio/audioservice.h"
#include "ai/claudeservice.h"
#include "cards/cards.h"
#include "ocr/ocrservice.h"
#include "papers/papersservice.h"
#include "ui/tabletmode.h"
#include "ui/backuptool.h"
#include "media/images.h"
#include "maths/mathsservice.h"
#include "media/shapes.h"
#include "ui/thumbnails.h"
#include "ui/uitest.h"
#include "storage/backup.h"
#include "storage/database.h"
#include "storage/library.h"
#include "storage/pagestore.h"
#include "storage/paths.h"
#include "storage/schema.h"
#include <QLoggingCategory>
#include <QDir>
#include <QFileInfo>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <csignal>
#include <atomic>
#endif
#include "workers/pingmeter.h"
#include "workers/workersupervisor.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("lumen"));
    QCoreApplication::setApplicationName(QStringLiteral("lumen"));
    QCoreApplication::setApplicationVersion(QStringLiteral(LUMEN_VERSION));

    // D-002, refined (D-021): the ink canvas consumes every tablet event over it, so the pen is
    // never a mouse *on the page*. Events over UI chrome (toolbar, sidebar, dialogs) are left
    // unhandled on purpose and Qt turns them into mouse events, so buttons and lists work with the pen.
    QGuiApplication::setAttribute(Qt::AA_SynthesizeMouseForUnhandledTabletEvents, true);

    // D-003: 4x MSAA for the ink ribbons, set before any window exists.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setSamples(4);
    // No alpha channel on the window surface. With one, anything that writes zero alpha — a shape
    // drawn with blending off, a driver quirk at high zoom — makes the compositor show the desktop
    // through the app, which is exactly what the maker saw when zooming with a shape selected.
    fmt.setAlphaBufferSize(0);
    QSurfaceFormat::setDefaultFormat(fmt);

    QGuiApplication app(argc, argv);
    // Wayland app_id = the desktop file name: this is what lets Plasma match the window to
    // lumen.desktop, show its icon in the task manager, and pin it.
    QGuiApplication::setDesktopFileName(QStringLiteral("lumen-notes"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Lumen"));
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("lumen-notes"), QIcon(QStringLiteral(":/icons/lumen-notes.svg"))));
    const QStringList args = app.arguments();

    paths::ensureDirs();
    Database db;
    if (!db.open(paths::databasePath()) || ensureSchema(db) == 0) {
        fprintf(stderr, "lumen: cannot open %s: %s\n", qPrintable(paths::databasePath()), qPrintable(db.lastError()));
        return 1;
    }
    Library library(db);
    library.tidyAutomaticTitles();
    library.purgeDeleted(30);
    library.seedDefaults();

    if (args.contains(QStringLiteral("--backup"))) {
        const backup::Result r = backup::run(paths::dataDir(), library.setting("backup.dir", paths::backupDir()), library.setting("backup.keep", "7").toInt());
        if (r.ok) printf("backup written: %s (%lld bytes, %d old removed)\n", qPrintable(r.path), (long long)r.bytes, r.removed);
        else fprintf(stderr, "backup failed: %s\n", qPrintable(r.error));
        return r.ok ? 0 : 1;
    }
    PageStore pageStore(db, paths::journalDir());

    WorkerSupervisor pingWorker(QStringLiteral("ping"));
    PingMeter pingMeter(&pingWorker);
    WorkerSupervisor pdfWorker(QStringLiteral("pdf"));
    PdfService pdf(db, library, &pdfWorker);
    WorkerSupervisor latexWorker(QStringLiteral("latex"));
    TextBlocks textBlocks(db, library);
    WorkerSupervisor audioWorker(QStringLiteral("audio"));
    AudioService audio(db, library, &audioWorker);
    WorkerSupervisor claudeWorker(QStringLiteral("claude"));
    const QByteArray promptsEnv = qgetenv("LUMEN_PROMPTS_DIR");
    QString promptsDir = promptsEnv.isEmpty() ? QStringLiteral(LUMEN_PROMPTS_SOURCE_DIR) : QString::fromLocal8Bit(promptsEnv);
#ifdef Q_OS_WIN
    // Flat Windows install: prompts/ next to lumen.exe (see workersDir() for the same split).
    const QString installedPrompts = QCoreApplication::applicationDirPath() + QStringLiteral("/prompts");
#else
    const QString installedPrompts = QCoreApplication::applicationDirPath() + QStringLiteral("/../share/lumen/prompts");
#endif
    if (promptsEnv.isEmpty() && QFileInfo::exists(installedPrompts + "/ask.md"))
        promptsDir = QDir::cleanPath(installedPrompts);
    ClaudeService claude(db, library, textBlocks, audio, &claudeWorker, promptsDir);
    WorkerSupervisor cardsWorker(QStringLiteral("cards"));
    Cards cards(db, library, &cardsWorker);
    WorkerSupervisor ocrWorker(QStringLiteral("ocr"));
    WorkerSupervisor mathsWorker(QStringLiteral("maths"));
    OcrService ocr(db, library, &ocrWorker);
    QObject::connect(&pageStore, &PageStore::saved, &ocr, &OcrService::schedule);
    PapersService papers(db, library, pdf, &pdfWorker);
    Images images(db);
    Shapes shapes(db);
    MathsService maths(mathsWorker);
    TabletMode tabletMode;
    BackupTool backupTool(library);
    Thumbnails thumbnails(db);
    QObject::connect(&pageStore, &PageStore::saved, &thumbnails, &Thumbnails::refresh);
    TabletEventFilter tabletFilter;
    KeyInjector keys;

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("theme"), new ThemeIconProvider);
    engine.addImageProvider(QStringLiteral("latex"), new LatexImageProvider(&latexWorker));
    engine.rootContext()->setContextProperty(QStringLiteral("pingWorker"), &pingWorker);
    engine.rootContext()->setContextProperty(QStringLiteral("pingMeter"), &pingMeter);
    engine.rootContext()->setContextProperty(QStringLiteral("library"), &library);
    engine.rootContext()->setContextProperty(QStringLiteral("pageStore"), &pageStore);
    engine.rootContext()->setContextProperty(QStringLiteral("pdf"), &pdf);
    engine.rootContext()->setContextProperty(QStringLiteral("textBlocks"), &textBlocks);
    engine.rootContext()->setContextProperty(QStringLiteral("audio"), &audio);
    engine.rootContext()->setContextProperty(QStringLiteral("claude"), &claude);
    engine.rootContext()->setContextProperty(QStringLiteral("cards"), &cards);
    engine.rootContext()->setContextProperty(QStringLiteral("ocr"), &ocr);
    engine.rootContext()->setContextProperty(QStringLiteral("papers"), &papers);
    engine.rootContext()->setContextProperty(QStringLiteral("tabletMode"), &tabletMode);
    engine.rootContext()->setContextProperty(QStringLiteral("backupTool"), &backupTool);
    engine.rootContext()->setContextProperty(QStringLiteral("thumbnails"), &thumbnails);
    engine.rootContext()->setContextProperty(QStringLiteral("images"), &images);
    engine.rootContext()->setContextProperty(QStringLiteral("shapes"), &shapes);
    engine.rootContext()->setContextProperty(QStringLiteral("maths"), &maths);
    engine.rootContext()->setContextProperty(QStringLiteral("keys"), &keys);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    const bool probeMode = app.arguments().contains(QStringLiteral("--probe"));
    engine.setInitialProperties({{QStringLiteral("probeMode"), probeMode}});
    engine.loadFromModule("Lumen", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
        // Mesa gives the window an alpha channel even though the format above asks for none, so a
        // pixel any item leaves below full alpha shows the desktop through the app. Seal alpha to 1
        // at the end of every pass. Only OpenGL surfaces are composited with their alpha.
        QObject::connect(win, &QQuickWindow::afterRenderPassRecording, win, [win] {
            if (win->rendererInterface()->graphicsApi() != QSGRendererInterface::OpenGL) return;
            QOpenGLContext *ctx = QOpenGLContext::currentContext();
            if (!ctx) return;
            win->beginExternalCommands();
            QOpenGLFunctions *gl = ctx->functions();
            gl->glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            gl->glClearColor(0, 0, 0, 1);
            gl->glClear(GL_COLOR_BUFFER_BIT);
            gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            win->endExternalCommands();
        }, Qt::DirectConnection);
        if (args.contains(QStringLiteral("--fullscreen"))) win->showFullScreen();
        else if (args.contains(QStringLiteral("--maximized"))) win->showMaximized();
        tabletFilter.attachWindow(win);
        if (probeMode) {
            if (auto *probe = win->findChild<PenProbeItem *>(QStringLiteral("penProbe")))
                tabletFilter.setSink(probe);
        } else if (auto *canvas = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas"))) {
            tabletFilter.setSink(canvas);
            pageStore.attach(canvas->document());
            QMetaObject::invokeMethod(win, "openLastPage");
        }
    }
    pingWorker.start();
    pdfWorker.start();
    audioWorker.start();
    for (WorkerSupervisor *w : {&latexWorker, &claudeWorker, &ocrWorker, &cardsWorker, &mathsWorker}) w->setAutoStart(true);   // start on first use
    if (!args.contains(QStringLiteral("--smoke"))) QTimer::singleShot(15000, &ocr, [&ocr] { ocr.prepare(); ocr.scanStale(); });
    if (!args.contains(QStringLiteral("--smoke"))) QTimer::singleShot(4000, &audio, &AudioService::prepareModels);

    // Nightly backup timer (D-014): never in tests (they point LUMEN_DATA_DIR elsewhere). On Linux
    // a systemd --user timer runs `lumen --backup`, installed once. Elsewhere there is no systemd
    // equivalent, so an in-app timer checks hourly and runs the backup itself when one is due.
#ifdef Q_OS_LINUX
    if (qgetenv("LUMEN_DATA_DIR").isEmpty() && !args.contains(QStringLiteral("--smoke")) && backup::timerNeedsUpdate(QCoreApplication::applicationFilePath())) {
        QString err;
        if (!backup::installUserTimer(QCoreApplication::applicationFilePath(), &err)) qWarning("backup timer: %s", qPrintable(err));
    }
#else
    if (qgetenv("LUMEN_DATA_DIR").isEmpty() && !args.contains(QStringLiteral("--smoke"))) {
        auto runIfDue = [&library] {
            const QString dest = library.setting("backup.dir", paths::backupDir());
            if (backup::dueForNightlyBackup(dest)) backup::run(paths::dataDir(), dest, library.setting("backup.keep", "7").toInt());
        };
        auto *backupTimer = new QTimer(&app);
        backupTimer->setInterval(3600000);   // checked hourly; dueForNightlyBackup gates the actual run
        QObject::connect(backupTimer, &QTimer::timeout, &app, runIfDue);
        backupTimer->start();
        QTimer::singleShot(30000, &app, runIfDue);   // also catch a backup overdue at launch, once things settle
    }
#endif

    if (args.contains(QStringLiteral("--smoke")))
        QTimer::singleShot(2500, &app, &QCoreApplication::quit);

    // --uitest: drive the real window with synthetic input and check the flows a person uses.
    if (args.contains(QStringLiteral("--uitest"))) {
        QTimer::singleShot(1200, &app, [&engine] {
            auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
            const int failures = win ? uitest::run(win, win) : 1;
            QCoreApplication::exit(failures ? 2 : 0);
        });
    }
    // --screenshot <png>: grab the window after 3.5 s and quit (headless UI checks).
    // SIGUSR1 → screenshot of the live window to $LUMEN_SHOT (default /tmp/lumen-shot.png), no quit.
    // Unix-only: Windows has no SIGUSR1, and this is a dev convenience, not a feature to replace.
#ifdef Q_OS_UNIX
    {
        static QQmlApplicationEngine *engineForShot = &engine;
        static std::atomic<int> shotRequests{0};
        ::signal(SIGUSR1, [](int) { shotRequests.fetch_add(1); });
        auto *poll = new QTimer(&app);
        poll->setInterval(150);
        QObject::connect(poll, &QTimer::timeout, &app, [] {
            if (shotRequests.exchange(0) == 0) return;
            const QByteArray env = qgetenv("LUMEN_SHOT");
            const QString path = env.isEmpty() ? QStringLiteral("/tmp/lumen-shot.png") : QString::fromLocal8Bit(env);
            if (auto *win = qobject_cast<QQuickWindow *>(engineForShot->rootObjects().constFirst())) { win->grabWindow().save(path); qInfo("screenshot saved: %s", qPrintable(path)); }
        });
        poll->start();
    }
#endif
    const int shotIdx = args.indexOf(QStringLiteral("--screenshot"));
    if (shotIdx >= 0 && shotIdx + 1 < args.size()) {
        const QString shotPath = args.at(shotIdx + 1);
        QTimer::singleShot(3500, &app, [&engine, shotPath] {
            if (auto *win = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst())) {
                if (auto *c = win->findChild<InkCanvas *>(QStringLiteral("inkCanvas")))
                    qInfo("screenshot: canvas %gx%g zoom %g pan %g,%g strokes %d", c->width(), c->height(), c->zoom(), c->pan().x(), c->pan().y(), c->strokeCount());
                win->grabWindow().save(shotPath);
            }
            QCoreApplication::quit();
        });
    }

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &pageStore, &PageStore::unload);
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &audio, &AudioService::finishForQuit);
    const int rc = app.exec();
    tabletFilter.setSink(nullptr);
    pageStore.unload();
    cardsWorker.stop();
    mathsWorker.stop();
    ocrWorker.stop();
    claudeWorker.stop();
    audioWorker.stop();
    latexWorker.stop();
    pdfWorker.stop();
    pingWorker.stop();
    return rc;
}

// The iPad half of Platform (platform.h). Compiled with ARC (app/CMakeLists.txt).
#include "platform.h"
#include "storage/paths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QWindow>
#import <UIKit/UIKit.h>

namespace {

QString actionName(UIPencilPreferredAction action)
{
    if (action == UIPencilPreferredActionSwitchEraser) return QStringLiteral("switchEraser");
    if (action == UIPencilPreferredActionSwitchPrevious) return QStringLiteral("switchPrevious");
    if (action == UIPencilPreferredActionShowColorPalette) return QStringLiteral("showColorPalette");
    if (@available(iOS 16.0, *)) {
        if (action == UIPencilPreferredActionShowInkAttributes) return QStringLiteral("showInkAttributes");
    }
    if (@available(iOS 17.5, *)) {
        if (action == UIPencilPreferredActionShowContextualPalette) return QStringLiteral("showContextualPalette");
    }
    return QStringLiteral("ignore");    // also "run a system shortcut", which the app never hears about
}

UIView *viewOf(QWindow *window)
{
    return window ? (__bridge UIView *)reinterpret_cast<void *>(window->winId()) : nil;
}

} // namespace

@interface LumenPencilDelegate : NSObject <UIPencilInteractionDelegate>
@property (nonatomic, assign) Platform *platform;
@end

@implementation LumenPencilDelegate

// Up to iOS 17.4; from 17.5 the system calls the method below instead.
- (void)pencilInteractionDidTap:(UIPencilInteraction *)interaction
{
    Q_UNUSED(interaction);
    emit self.platform->pencilTapped(actionName(UIPencilInteraction.preferredTapAction));
}

- (void)pencilInteraction:(UIPencilInteraction *)interaction didReceiveTap:(UIPencilInteractionTap *)tap API_AVAILABLE(ios(17.5))
{
    Q_UNUSED(interaction);
    Q_UNUSED(tap);
    emit self.platform->pencilTapped(actionName(UIPencilInteraction.preferredTapAction));
}

// Apple Pencil Pro. Acted on when the squeeze ends, so holding it does nothing twice.
- (void)pencilInteraction:(UIPencilInteraction *)interaction didReceiveSqueeze:(UIPencilInteractionSqueeze *)squeeze API_AVAILABLE(ios(17.5))
{
    Q_UNUSED(interaction);
    if (squeeze.phase != UIPencilInteractionPhaseEnded) return;
    emit self.platform->pencilSqueezed(actionName(UIPencilInteraction.preferredSqueezeAction));
}

@end

void Platform::attach(QWindow *window)
{
    m_window = window;
    UIView *view = viewOf(window);
    if (!view) return;
    static LumenPencilDelegate *delegate = [[LumenPencilDelegate alloc] init];   // lives as long as the app
    delegate.platform = this;
    UIPencilInteraction *interaction = [[UIPencilInteraction alloc] init];
    interaction.delegate = delegate;
    [view addInteraction:interaction];
}

bool Platform::canShare() const { return true; }

bool Platform::share(const QUrl &file, const QRectF &anchor)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    UIView *view = viewOf(m_window);
    if (!view || !QFileInfo::exists(path)) return false;
    UIViewController *top = view.window.rootViewController;
    while (top.presentedViewController) top = top.presentedViewController;
    if (!top) return false;
    NSURL *url = [NSURL fileURLWithPath:path.toNSString()];
    UIActivityViewController *sheet = [[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil];
    // On an iPad the sheet is a popover, and a popover with nothing to point at is an exception.
    UIPopoverPresentationController *popover = sheet.popoverPresentationController;
    if (popover) {
        popover.sourceView = view;
        popover.sourceRect = anchor.isEmpty() ? CGRectMake(view.bounds.size.width / 2, 60, 1, 1) : anchor.toCGRect();
    }
    [top presentViewController:sheet animated:YES completion:nil];
    return true;
}

// A file opened from the Files app is lent, not given: read it inside its security scope, into a
// copy of our own that the importers can take their time over.
QUrl Platform::readableCopy(const QUrl &url) const
{
    if (!url.isLocalFile()) return url;
    NSURL *ns = url.toNSURL();
    const BOOL scoped = [ns startAccessingSecurityScopedResource];
    const QString dir = paths::cacheDir() + QStringLiteral("/opened");
    QDir().mkpath(dir);
    const QString copy = dir + QLatin1Char('/') + QFileInfo(url.toLocalFile()).fileName();
    QFile::remove(copy);
    const bool copied = QFile::copy(url.toLocalFile(), copy);
    if (scoped) [ns stopAccessingSecurityScopedResource];
    return copied ? QUrl::fromLocalFile(copy) : url;
}

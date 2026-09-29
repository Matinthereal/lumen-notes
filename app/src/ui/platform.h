#pragma once
#include <QObject>
#include <QRectF>
#include <QUrl>

class QWindow;

// What the system offers beyond Qt. On an iPad (platform_ios.mm): Apple Pencil's double-tap and
// squeeze, reported as the action the iPad's Pencil settings ask for; the share sheet, which is
// how a file leaves the app there (Qt's iOS save dialog is not implemented); and files handed
// over from the Files app. Elsewhere the Pencil signals never fire and canShare is false, so the
// file dialogs do the job.
class Platform : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool canShare READ canShare CONSTANT)
public:
    explicit Platform(QObject *parent = nullptr);
    void attach(QWindow *window);
    bool canShare() const;
    Q_INVOKABLE QUrl shareTarget(const QString &fileName) const;   // where to write a file before share()
    Q_INVOKABLE bool share(const QUrl &file, const QRectF &anchor);   // anchor: scene rect the sheet points at
    // A file handed over from outside, the way the system hands one over: "Open with Lumen" on a
    // desktop passes it on the command line, or to the Lumen already running (SingleInstance).
    void openFile(const QUrl &file);

signals:
    // switchEraser · switchPrevious · showColorPalette · showInkAttributes · showContextualPalette · ignore
    void pencilTapped(const QString &action);
    void pencilSqueezed(const QString &action);
    void fileOpened(const QUrl &file);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QUrl readableCopy(const QUrl &url) const;
    QWindow *m_window = nullptr;
};

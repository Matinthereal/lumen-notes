#pragma once
class QQuickWindow;
class QObject;

// A scripted battle test of the real UI: drives the actual window with synthetic input and
// asserts the flows a person uses. Run with `lumen --uitest` (offscreen in CI). Returns the
// number of failures; any QML warning during the run counts as one.
namespace uitest {
int run(QQuickWindow *window, QObject *root);
}

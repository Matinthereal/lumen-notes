package io.github.matinthereal.lumen;

import android.app.Activity;
import android.content.Context;
import android.view.ActionMode;
import android.view.KeyEvent;
import android.view.KeyboardShortcutGroup;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.SearchEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.view.accessibility.AccessibilityEvent;
import java.util.List;

// Qt 6.10 hands a stylus to the app with its position and pressure only (QtInputDelegate
// .sendTouchEvent), so the pen's lean never arrives. This sits in front of the activity's window,
// tells the native side (app/src/input/stylustilt.cpp) the tilt and orientation of each stylus
// event under the event time Qt also passes on, and then lets the event go on as before.
public final class StylusTilt {
    private StylusTilt() {}

    private static native void sample(long timeMs, float tilt, float orientation);

    public static void install(Context context) {
        if (!(context instanceof Activity)) return;
        final Activity activity = (Activity) context;
        activity.runOnUiThread(() -> {
            final Window window = activity.getWindow();
            final Window.Callback inner = window.getCallback();
            if (inner != null && !(inner instanceof Hook)) window.setCallback(new Hook(inner));
        });
    }

    private static final class Hook implements Window.Callback {
        private final Window.Callback inner;

        Hook(Window.Callback inner) { this.inner = inner; }

        @Override public boolean dispatchTouchEvent(MotionEvent e) {
            // Qt takes pointer 0 and decides pen or finger by its tool type; so does this.
            final int tool = e.getToolType(0);
            if (tool == MotionEvent.TOOL_TYPE_STYLUS || tool == MotionEvent.TOOL_TYPE_ERASER)
                sample(e.getEventTime(), e.getAxisValue(MotionEvent.AXIS_TILT, 0), e.getAxisValue(MotionEvent.AXIS_ORIENTATION, 0));
            return inner.dispatchTouchEvent(e);
        }

        // Everything else goes straight to the activity.
        @Override public boolean dispatchKeyEvent(KeyEvent e) { return inner.dispatchKeyEvent(e); }
        @Override public boolean dispatchKeyShortcutEvent(KeyEvent e) { return inner.dispatchKeyShortcutEvent(e); }
        @Override public boolean dispatchTrackballEvent(MotionEvent e) { return inner.dispatchTrackballEvent(e); }
        @Override public boolean dispatchGenericMotionEvent(MotionEvent e) { return inner.dispatchGenericMotionEvent(e); }
        @Override public boolean dispatchPopulateAccessibilityEvent(AccessibilityEvent e) { return inner.dispatchPopulateAccessibilityEvent(e); }
        @Override public View onCreatePanelView(int featureId) { return inner.onCreatePanelView(featureId); }
        @Override public boolean onCreatePanelMenu(int featureId, Menu menu) { return inner.onCreatePanelMenu(featureId, menu); }
        @Override public boolean onPreparePanel(int featureId, View view, Menu menu) { return inner.onPreparePanel(featureId, view, menu); }
        @Override public boolean onMenuOpened(int featureId, Menu menu) { return inner.onMenuOpened(featureId, menu); }
        @Override public boolean onMenuItemSelected(int featureId, MenuItem item) { return inner.onMenuItemSelected(featureId, item); }
        @Override public void onWindowAttributesChanged(WindowManager.LayoutParams attrs) { inner.onWindowAttributesChanged(attrs); }
        @Override public void onContentChanged() { inner.onContentChanged(); }
        @Override public void onWindowFocusChanged(boolean hasFocus) { inner.onWindowFocusChanged(hasFocus); }
        @Override public void onAttachedToWindow() { inner.onAttachedToWindow(); }
        @Override public void onDetachedFromWindow() { inner.onDetachedFromWindow(); }
        @Override public void onPanelClosed(int featureId, Menu menu) { inner.onPanelClosed(featureId, menu); }
        @Override public boolean onSearchRequested() { return inner.onSearchRequested(); }
        @Override public boolean onSearchRequested(SearchEvent e) { return inner.onSearchRequested(e); }
        @Override public ActionMode onWindowStartingActionMode(ActionMode.Callback callback) { return inner.onWindowStartingActionMode(callback); }
        @Override public ActionMode onWindowStartingActionMode(ActionMode.Callback callback, int type) { return inner.onWindowStartingActionMode(callback, type); }
        @Override public void onActionModeStarted(ActionMode mode) { inner.onActionModeStarted(mode); }
        @Override public void onActionModeFinished(ActionMode mode) { inner.onActionModeFinished(mode); }
        @Override public void onProvideKeyboardShortcuts(List<KeyboardShortcutGroup> data, Menu menu, int deviceId) { inner.onProvideKeyboardShortcuts(data, menu, deviceId); }
        @Override public void onPointerCaptureChanged(boolean hasCapture) { inner.onPointerCaptureChanged(hasCapture); }
    }
}

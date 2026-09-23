import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import Lumen

Window {
    id: root
    width: 1600
    height: 900
    visible: true
    title: probeMode ? "Lumen — pen probe" : "Lumen"

    property bool probeMode: false
    property bool showStats: false
    property var currentPageId: 0
    property string pageLabel: ""
    property string pagePaper: ""
    property string pageSizeMode: ""
    // A typed page is a text document, not a sheet of paper: no ink tools, no canvas.
    readonly property bool pageTyped: currentPageId > 0 && pageSizeMode === "typed"
    // Charcoal by default because that is what the maker's existing pages were written on; each
    // page can carry its own colour, so ink never becomes invisible when the theme moves.
    readonly property string paperColour: pagePaper.length ? pagePaper : library.setting("page.paper", "#22262B")
    readonly property bool paperIsDark: {
        const c = Qt.color(root.paperColour)
        return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) < 0.45
    }
    property bool reviewVisible: false
    property bool dashboardVisible: false
    property string dashboardSubject: ""
    property bool settingsVisible: false
    property bool trashVisible: false
    property bool keysVisible: false
    property bool browserVisible: false
    property bool onboardingVisible: library.setting("onboarded", "0") !== "1"
    readonly property bool tablet: tabletMode.tablet
    // Panels: one on the left (notebooks | cards | papers), one on the right (claude | transcript | handwriting).
    property string leftPanel: "notebooks"
    property string rightPanel: ""

    SystemPalette { id: pal }
    color: pal.window
    Binding { target: Ui; property: "tablet"; value: root.tablet }
    Binding { target: Ui; property: "dark"; value: (0.299 * pal.window.r + 0.587 * pal.window.g + 0.114 * pal.window.b) < 0.5 }

    function applySavedSettings() {
        canvas.pressureCeiling = Number(library.setting("pen.ceiling", "0.85"))
        canvas.smoothing = Number(library.setting("pen.smoothing", "0"))
        const pm = Number(library.setting("pen.predictionMs", "16.7")); canvas.predictionMs = pm; canvas.predictionEnabled = pm > 0
        canvas.shapeSnap = library.setting("pen.shapeSnap", "1") === "1"
        canvas.highlighterOpacity = Number(library.setting("pen.highlighterOpacity", "0.35"))
        canvas.palmRejectMs = Number(library.setting("touch.palmMs", "500"))
        canvas.eraserRadius = Number(library.setting("pen.eraserRadius", "12"))
        canvas.penStyle = library.setting("pen.style", "classic")
        if (library.setting("tablet.forced", "") === "1") tabletMode.tablet = true
    }
    Component.onCompleted: applySavedSettings()
    onTabletChanged: { library.setSetting("tablet.forced", tablet ? "1" : "0"); if (tablet) leftPanel = "" }

    function openPage(pageId) {
        if (!pageId) return
        const info = library.page(pageId)
        if (!info.id) return
        canvas.resetHistory()
        canvas.clearBackground()
        pageStore.load(pageId)
        pagePaper = info.paper || ""
        pageSizeMode = info.sizeMode || ""
        canvas.pageStyle = info.style
        canvas.infinite = info.sizeMode === "infinite"
        canvas.pageSize = Qt.size(info.width, info.height)
        canvas.fitPage()
        currentPageId = pageId
        pageLabel = info.notebookName + " › " + info.sectionName + (info.title.length ? " › " + info.title.replace("\u200b", "") : "")
        library.setSetting("lastPage", String(pageId))
        library.touchPage(pageId)
        if (sidebar && sidebar.revealPage) sidebar.revealPage(pageId)
        if (pdf.pageHasPdf(pageId)) { pdf.requestRender(pageId, Math.min(canvas.zoom, 1.0)); pdf.requestWords(pageId) }
        else if (canvas.tool === "text") canvas.tool = "pen"
    }
    function openLastPage() {
        const saved = library.setting("lastPage", "")
        if (saved === "0") return            // the maker closed the page last time: stay on the empty state
        let id = Number(saved || "0")
        if (!id || !library.page(id).id) id = library.firstPageId()
        openPage(id)
    }
    // No page open is a real, supported state — nothing is auto-opened to fill the hole.
    function closePage() {
        pageStore.unload()
        canvas.resetHistory(); canvas.clearBackground(); canvas.setWords([])
        currentPageId = 0
        pageLabel = ""
        pagePaper = ""
        pageSizeMode = ""
        renderedScale = 0
        library.setSetting("lastPage", "0")
        if (sidebar && sidebar.clearCurrent) sidebar.clearCurrent()
    }
    // "New page" with nothing open: land in the section last used, else the first one that exists.
    function newPageAnywhere(kind) {
        let sectionId = library.page(Number(library.setting("lastPage", "0"))).sectionId || 0
        if (!sectionId) {
            const nb = library.notebooks()[0]
            const sec = nb ? library.sections(nb.id)[0] : null
            sectionId = sec ? sec.id : (nb ? library.createSection(nb.id, "Notes")
                                           : library.createSection(library.createNotebook("Notes", "#1F3A93", ""), "Notes"))
        }
        openPage(library.createPage(sectionId, "", kind || ""))
    }
    function newPage(kind) {
        const info = library.page(currentPageId); if (!info.id) { newPageAnywhere(kind); return }
        const idx = library.pages(info.sectionId).findIndex(p => p.id === currentPageId)
        openPage(library.createPage(info.sectionId, "", kind || "", idx))
    }
    function newSection() {
        const info = library.page(currentPageId); if (!info.id) return
        openPage(library.createPage(library.createSection(info.notebookId, "New section")))
    }
    function stepPage(delta) { const id = library.nextPageId(currentPageId, delta); if (id) openPage(id) }
    function toggleLeft(name) { leftPanel = leftPanel === name ? "" : name }
    function toggleRight(name) { rightPanel = rightPanel === name ? "" : name }

    // ---- PDF plumbing
    property real renderedScale: 0
    Connections {
        target: pdf
        function onRendered(pageId, scale, file, w, h) {
            if (pageId !== root.currentPageId) return
            canvas.setBackground(file, scale); root.renderedScale = scale
            const next = library.nextPageId(pageId, 1), prev = library.nextPageId(pageId, -1)
            if (next) pdf.prerender(next, scale); if (prev) pdf.prerender(prev, scale)
        }
        function onWords(pageId, words) { if (pageId === root.currentPageId) canvas.setWords(words) }
        function onImported(sectionId, firstPageId, token) { if (firstPageId) root.openPage(firstPageId) }
        function onExported(file, pages) { toast.show("Exported " + pages + " page" + (pages === 1 ? "" : "s") + " to " + file, null) }
        function onFailed(message) { toast.show(message, null) }
    }
    Timer { id: rerender; interval: 250; onTriggered: { if (!pdf.pageHasPdf(root.currentPageId)) return; const want = Math.min(Math.max(canvas.zoom, 0.5), 4.0); if (Math.abs(want - root.renderedScale) / Math.max(root.renderedScale, 0.01) > 0.2) pdf.requestRender(root.currentPageId, want) } }
    FileDialog {
        id: pictureDialog
        title: "Add a picture to this page"
        nameFilters: ["Pictures (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.tif *.tiff)"]
        onAccepted: imageLayer.insertFile(selectedFile)
    }
    FileDialog { id: importDialog; property var notebookId: 0; title: "Import PDF as a section"; nameFilters: ["PDF files (*.pdf)"]; onAccepted: pdf.importAsSection(selectedFile, notebookId, "") }
    FileDialog { id: exportDialog; title: "Export section as PDF"; fileMode: FileDialog.SaveFile; nameFilters: ["PDF files (*.pdf)"]; defaultSuffix: "pdf"; onAccepted: { const info = library.page(root.currentPageId); if (info.id) pdf.exportPages("section", info.sectionId, selectedFile) } }
    SearchPalette {
        id: search; parent: Overlay.overlay
        onOpenResult: (pageId, kind, refId) => { root.openPage(pageId); if (kind === "ocr") Qt.callLater(() => { for (const r of ocr.results(pageId)) if (r.id === refId) canvas.flashRect(Qt.rect(r.x, r.y, r.w, r.h)) }) }
    }

    Loader { id: probeLoader; anchors.fill: parent; active: root.probeMode; sourceComponent: ProbePage {} }

    // ================================================================ layout
    RowLayout {
        anchors.fill: parent
        visible: !root.probeMode
        spacing: 0

        Rail {
            id: rail
            objectName: "rail"
            Layout.fillHeight: true
            leftPanel: root.leftPanel
            rightPanel: root.rightPanel
            tablet: root.tablet
            onLeftPanelChanged: root.leftPanel = leftPanel
            onRightPanelChanged: root.rightPanel = rightPanel
            onSearchRequested: search.open()
            onBrowserRequested: if (root.currentPageId) root.browserVisible = true
            onTrashRequested: root.trashVisible = true
            onSettingsRequested: root.settingsVisible = !root.settingsVisible
            onTabletRequested: tabletMode.tablet = !tabletMode.tablet
        }
        Binding { target: rail; property: "leftPanel"; value: root.leftPanel }
        Binding { target: rail; property: "rightPanel"; value: root.rightPanel }

        // Left panel (docked in desktop mode; overlays the page in tablet mode)
        Item {
            id: leftSlot
            Layout.fillHeight: true
            Layout.bottomMargin: Ui.keyboardInset
            Layout.preferredWidth: root.tablet ? 0 : (root.leftPanel.length ? Ui.panel : 0)
            visible: !root.tablet && root.leftPanel.length > 0
            Loader { anchors.fill: parent; sourceComponent: leftPanelComponent; active: !root.tablet && root.leftPanel.length > 0 }
        }
        Rectangle { visible: leftSlot.visible; Layout.fillHeight: true; implicitWidth: 1; color: Qt.alpha(pal.text, 0.14) }

        // Page
        Item {
            id: page
            objectName: "pageArea"
            Layout.fillWidth: true
            Layout.fillHeight: true
            // A zoomed page must stay inside its own area: unclipped, it painted over the rail and
            // the side panel while input there still went to them, so you saw paper you could not write on.
            clip: true

            Rectangle { id: desk; anchors.fill: parent
                        color: root.currentPageId === 0 ? pal.window
                             : (canvas.infinite ? canvas.paperColor : (Ui.dark ? Qt.darker(pal.window, 1.35) : Qt.darker(pal.window, 1.08))) }
            InkCanvas {
                id: canvas
                objectName: "inkCanvas"
                anchors.fill: parent
                visible: root.currentPageId > 0 && !root.pageTyped
                enabled: visible
                topInset: (toolbar.visible ? toolbar.height + 20 : 16)
                bottomInset: (audioBar.visible ? audioBar.height : 0) + (paperBar.visible ? paperBar.height : 0) + 16 + Ui.keyboardInset
                onTopInsetChanged: if (root.currentPageId) fitPage()
                // The paper has its own colour so that ink written under one theme stays visible
                // under the other. pal.base only supplies the fallback for the app default.
                paperColor: root.paperColour
                dotColor: Qt.alpha(root.paperIsDark ? "#FFFFFF" : "#000000", 0.18)
                frameColor: Qt.alpha(root.paperIsDark ? "#FFFFFF" : "#000000", 0.55)   // the page's edge is load-bearing: WCAG 1.4.11 asks 3:1
                penColor: root.paperIsDark ? "#F2F2F2" : "#1A1A1A"
                accentColor: pal.highlight
                Component.onCompleted: fitPage()
                onViewChanged: rerender.restart()
            }
            ImageLayer {
                id: imageLayer
                anchors.fill: parent
                board: canvas
                pageId: root.currentPageId
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
                onOptionsAsked: (where) => {
                    objectMenu.subject = "picture"
                    objectMenu.info = ({})
                    objectMenu.openOver(imageLayer.mapToItem(objectMenu.parent, where.x, where.y, where.width, where.height))
                }
            }
            ShapeLayer {
                id: shapeLayer
                objectName: "shapeLayer"
                anchors.fill: parent
                visible: !root.pageTyped
                board: canvas
                pageId: root.currentPageId
                strokeColour: canvas.penColor        // a new shape uses the pen in your hand
                strokeWidth: Math.max(1.5, canvas.penWidth)
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
                onOptionsAsked: (where) => {
                    objectMenu.subject = "shape"
                    objectMenu.info = shapeLayer.selected()
                    objectMenu.openOver(shapeLayer.mapToItem(objectMenu.parent, where.x, where.y, where.width, where.height))
                }
            }
            TextLayer {
                id: textLayer; anchors.fill: parent; canvas: canvas; pageId: root.currentPageId
                visible: !root.pageTyped
                recordingT: function() { return audio.recording ? audio.nowMs() : 0 }
            }
            TypedPage {
                id: typedPage
                anchors { fill: parent; bottomMargin: (audioBar.visible ? audioBar.height : 0) + Ui.keyboardInset }
                visible: root.pageTyped
                pageId: root.pageTyped ? root.currentPageId : 0
            }
            Connections { target: canvas; function onTapped(page) { textLayer.addAt(page); canvas.tool = "pen" } }
            // Touching the page itself puts down whatever object was selected.
            Connections {
                target: canvas
                // Any press the canvas itself owns — the page, the desk beside it, with any tool —
                // puts the selection down. Presses on an object never reach here.
                function onPagePressed() { shapeLayer.selectedId = 0; imageLayer.selectedId = 0 }
            }
            Binding { target: canvas; property: "recordingId"; value: audio.recording ? audio.recordingId : 0 }
            Binding { target: canvas; property: "recordingEpochMs"; value: audio.recordingEpochMs }
            Binding { target: textLayer; property: "recordingId"; value: audio.recording ? audio.recordingId : 0 }

            // Tablet mode: panels overlay the page; tapping the page dismisses them.
            TapHandler {
                // Both overlays are children of `page`, and a rail-style button inside one takes
                // only a passive grab — so without the bounds test a tap on any of them reached
                // here too and shut the panel you were using.
                enabled: root.tablet && (root.leftPanel.length > 0 || root.rightPanel.length > 0)
                onTapped: (ev) => {
                    const p = ev.position
                    if (leftOverlay.visible && leftOverlay.contains(leftOverlay.mapFromItem(page, p))) return
                    if (rightOverlay.visible && rightOverlay.contains(rightOverlay.mapFromItem(page, p))) return
                    root.leftPanel = ""; root.rightPanel = ""
                }
            }
            Loader {
                id: rightOverlay
                // Both tablet overlays live inside `page`: at Window level they were siblings of the
                // whole page and painted over the toolbar, the arrows, the style bar and the toast.
                visible: root.tablet && !root.probeMode && root.rightPanel.length > 0; active: visible
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom; margins: 8
                          topMargin: toolbar.visible ? toolbar.height + 22 : 8
                          bottomMargin: audioBar.height + 12 + Ui.keyboardInset }
                width: Ui.rightPanel
                sourceComponent: rightPanelComponent
                z: 5
            }
            Loader {
                id: leftOverlay
                visible: root.tablet && root.leftPanel.length > 0; active: visible
                anchors { left: parent.left; top: parent.top; bottom: parent.bottom; margins: 8; bottomMargin: audioBar.height + 12 + Ui.keyboardInset }
                width: Ui.panel
                sourceComponent: leftPanelComponent
                z: 5
            }

            // Touch page navigation: big translucent arrows at the bottom corners and a counter.
            property int libraryTick: 0
            Connections { target: library; function onChanged() { page.libraryTick++ } }
            Connections { target: papers; function onChanged() { page.libraryTick++ } }
            function paperOf(tick, pageId) { return papers.paperForPage(pageId) }
            function pagesOf(tick, pageId) { return library.pages(library.page(pageId).sectionId || 0) }   // tick makes the binding follow library.changed()
            readonly property var pageList: page.pagesOf(page.libraryTick, root.currentPageId)
            readonly property int pageIndex: { const l = page.pageList; for (let i = 0; i < l.length; ++i) if (l[i].id === root.currentPageId) return i; return -1 }
            component NavArrow: Rectangle {
                property string icon; property bool enabledLook: true; signal clicked()
                width: Ui.target + 8; height: Ui.target + 8; radius: width / 2
                color: Qt.alpha(pal.window, 0.85); border.color: Qt.alpha(pal.text, 0.18); border.width: 1
                // hidden, not dimmed: a visible "chrome" item is a hole the pen cannot draw through
                visible: enabledLook && !canvas.inking
                opacity: canvas.inking ? 0 : 0.9
                Behavior on opacity { NumberAnimation { duration: 120 } }
                Icon { anchors.centerIn: parent; name: "" + parent.icon; implicitWidth: Ui.icon + 2; implicitHeight: Ui.icon + 2 }
                TapHandler { enabled: parent.enabledLook && parent.opacity > 0.5; onTapped: parent.clicked() }
            }
            NavArrow {
                objectName: "chrome"; icon: "go-previous"; z: 6
                enabledLook: root.currentPageId > 0 && page.pageIndex > 0
                // Off the bottom corners: that is where the writing hand sits and where a right-hander
                // occludes the screen (Vogel et al., CHI 2009). Mid-height on the free-hand side.
                anchors { left: parent.left; verticalCenter: parent.verticalCenter; margins: 14; verticalCenterOffset: -34 }
                onClicked: root.stepPage(-1)
            }
            NavArrow {
                objectName: "chrome"; icon: "go-next"; z: 6
                enabledLook: root.currentPageId > 0 && page.pageIndex >= 0 && page.pageIndex < page.pageList.length - 1
                anchors { left: parent.left; verticalCenter: parent.verticalCenter; margins: 14; verticalCenterOffset: 34 }
                onClicked: root.stepPage(1)
            }
            Rectangle {
                objectName: "chrome"
                visible: root.currentPageId > 0 && page.pageList.length > 1 && !canvas.inking
                anchors { horizontalCenter: parent.horizontalCenter; bottom: audioBar.top; bottomMargin: paperBar.visible ? paperBar.height + 16 : 10 }
                width: counter.implicitWidth + 22; height: Ui.target - 10; radius: height / 2; z: 6
                color: Qt.alpha(pal.window, 0.85); border.color: Qt.alpha(pal.text, 0.18); border.width: 1
                Text { id: counter; anchors.centerIn: parent; text: (page.pageIndex + 1) + " / " + page.pageList.length; color: pal.text; font.pixelSize: Ui.small + 1 }
                TapHandler { onTapped: root.newPage() }
                ToolTip.visible: ch.hovered; ToolTip.delay: 600; ToolTip.text: "Page " + (page.pageIndex + 1) + " of " + page.pageList.length + " — tap for a new page after this one"
                HoverHandler { id: ch }
            }

            Toolbar {
                id: toolbar
                visible: root.currentPageId > 0 && !root.pageTyped
                canvas: canvas
                pageId: root.currentPageId
                tablet: root.tablet
                anchors { top: root.tablet ? undefined : parent.top; horizontalCenter: root.tablet ? undefined : parent.horizontalCenter; topMargin: 10 }
                x: root.tablet ? (page.width - width) / 2 : 0
                y: root.tablet ? 10 : 0
                opacity: canvas.inking ? 0 : 1
                Behavior on opacity { NumberAnimation { duration: 120 } }
                enabled: visible && opacity > 0.5
                onExportRequested: exportDialog.open()
                onPictureRequested: pictureDialog.open()
                onToast: (m) => toast.show(m, null)
                onLatexRequested: if (canvas.hasSelection) ocr.latexFromImage(canvas.renderSelectionToPng())
                onShapeKindChosen: (kind) => { shapeLayer.kind = kind; canvas.tool = "shape"; toast.show("Drag to draw a " + kind, null) }
                onStyleChosen: (style) => {
                    if (!root.currentPageId) return
                    canvas.pageStyle = style
                    library.setPageStyle(root.currentPageId, style)
                    toast.show("Page style: " + style, null)
                }
                onPaperChosen: (colour) => {
                    if (!root.currentPageId) return
                    library.setPagePaper(root.currentPageId, colour)
                    root.pagePaper = colour
                    toast.show(colour.length ? "Paper colour set for this page" : "Paper back to the app default", null)
                }
                // Bounded, and remembered: an unbounded drag could put the toolbar off-screen for good.
                DragHandler {
                    enabled: root.tablet
                    margin: 6
                    xAxis.minimum: 0; xAxis.maximum: Math.max(0, page.width - toolbar.width)
                    yAxis.minimum: 0; yAxis.maximum: Math.max(0, page.height - toolbar.height - 80)
                    onActiveChanged: if (!active) library.setSetting("toolbar.pos", toolbar.x + "," + toolbar.y)
                }
                z: 6
            }

            AudioPanel {
                id: audioBar
                visible: root.currentPageId > 0
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: Ui.keyboardInset }
                canvas: canvas
                sectionId: library.page(root.currentPageId).sectionId || 0
                pageId: root.currentPageId
                z: 6
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
                onTranscriptRequested: root.toggleRight("transcript")
            }
            PaperBar {
                id: paperBar
                enabled: !root.pageTyped
                opacity: root.pageTyped ? 0 : 1
                anchors { left: parent.left; right: parent.right; bottom: audioBar.top; leftMargin: 8; rightMargin: 8; bottomMargin: 6 }
                canvas: canvas
                pageId: root.currentPageId
                paperId: page.paperOf(page.libraryTick, root.currentPageId)
                onOpenPage: (pid) => root.openPage(pid)
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
                z: 6
            }
            LatexPopup {
                id: latexPopup; parent: Overlay.overlay; x: 80; y: 90
                onInsert: (latex, where) => { const id = textBlocks.create(root.currentPageId, where.x + where.width + 12, where.y, 320, 0, 0); textBlocks.setMarkdown(id, "$$" + latex + "$$", 0); canvas.selectNone() }
                onInsertAnswer: (text, where) => {
                    const id = textBlocks.create(root.currentPageId, where.x + where.width + 16, where.y, 260, 0, 0)
                    textBlocks.setMarkdown(id, "**= " + text + "**", 0)
                    canvas.selectNone()
                    toast.show("Answer written on the page", null)
                }
                onImprove: (png, draft) => claude.improveLatex(png, draft)
            }
            // Reading lasso'd maths takes a few seconds the first time: show it under the lasso,
            // where the LaTeX will appear.
            ProgressChip {
                id: latexProgress
                objectName: "chrome"
                visible: ocr.latexBusy && root.currentPageId > 0
                readonly property rect box: visible ? canvas.selectionBounds() : Qt.rect(0, 0, 0, 0)
                readonly property point under: Qt.point(box.x * canvas.zoom + canvas.pan.x, (box.y + box.height) * canvas.zoom + canvas.pan.y)
                x: Math.max(8, Math.min(page.width - width - 8, under.x))
                y: Math.max(8, Math.min((styleBar.visible ? styleBar.y : audioBar.y) - height - 8, under.y + 12))   // never under the style bar
                z: 19
                text: "Reading the maths…"
                cancelTip: "Stop reading the maths"
                onCancelRequested: ocr.cancelLatex()
            }
            Connections {
                target: ocr
                function onLatexReady(latex, png) { latexPopup.latex = latex; latexPopup.png = png; latexPopup.anchorRect = canvas.selectionBounds(); latexPopup.open() }
                function onFailed(m) { toast.show(m, null) }
            }

            // Where you are, top-left of the desk: out of the corner a right hand covers, off the
            // page itself, and never longer than the space it has.
            Text {
                anchors { left: parent.left; top: parent.top; leftMargin: 14; topMargin: 14 }
                width: Math.min(implicitWidth, page.width * 0.4)
                elide: Text.ElideMiddle
                text: root.pageLabel + (pageStore.dirty ? "  ·  saving…" : "")
                color: Qt.alpha(pal.windowText, 0.5); font.pixelSize: Ui.small
                visible: root.currentPageId > 0 && !canvas.inking
            }
            Text {
                anchors { left: parent.left; bottom: audioBar.top; margins: 8 }
                visible: root.showStats
                text: canvas.stats + "  ·  touch ignored " + canvas.touchIgnored + "  ·  zoom " + Math.round(canvas.zoom * 100) + "%"
                color: pal.text; font.pixelSize: 12; font.family: "monospace"
            }

            // ---- Nothing open. A real state with its own way back, not a blank screen.
            Item {
                id: emptyState
                objectName: "chrome"
                anchors.fill: parent
                visible: root.currentPageId === 0 && !root.probeMode
                z: 15
                property var recents: []
                property var starred: []
                function refreshRecents() { recents = library.recentPages(5); starred = library.starredPages(6) }
                Component.onCompleted: refreshRecents()      // it is already visible at startup, so onVisibleChanged never fires
                onVisibleChanged: if (visible) refreshRecents()
                Connections { target: library; function onChanged() { if (emptyState.visible) emptyState.refreshRecents() } }

                MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true }   // nothing behind this is live

                component EmptyAction: Rectangle {
                    property string label: ""
                    property string icon: ""
                    property bool primary: false
                    signal clicked()
                    implicitWidth: lbl.implicitWidth + (ico.visible ? Ui.icon + 34 : 34)
                    implicitHeight: Ui.target + 8
                    radius: 10
                    color: primary ? pal.highlight : "transparent"
                    // The theme's highlightedText measured 1.96:1 on this highlight; pick the
                    // readable one from the accent's own luminance instead (WCAG 1.4.3 wants 4.5).
                    readonly property color onAccent: (0.299 * pal.highlight.r + 0.587 * pal.highlight.g + 0.114 * pal.highlight.b) > 0.5 ? pal.window : pal.windowText
                    border.color: primary ? "transparent" : Qt.alpha(pal.text, 0.25)
                    border.width: primary ? 0 : 1
                    opacity: tap.pressed ? 0.75 : 1
                    Row {
                        anchors.centerIn: parent; spacing: 8
                        Icon { id: ico; visible: parent.parent.icon.length > 0; name: "" + parent.parent.icon
                               width: Ui.icon; height: Ui.icon
                               anchors.verticalCenter: parent.verticalCenter }
                        Text { id: lbl; text: parent.parent.label; anchors.verticalCenter: parent.verticalCenter
                               color: parent.parent.primary ? parent.parent.onAccent : pal.windowText
                               font.pixelSize: Ui.text + 1; font.weight: Font.DemiBold }
                    }
                    TapHandler { id: tap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: parent.clicked() }
                }

                ColumnLayout {
                    objectName: "emptyState"
                    anchors.centerIn: parent
                    width: Math.min(parent.width - 48, 460)
                    spacing: 12

                    Text {
                        text: "No page open"
                        color: pal.windowText; font.pixelSize: Ui.title; font.weight: Font.DemiBold
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Text {
                        text: "Nothing is selected. Start a new page, or pick one from your notebooks."
                        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true; Layout.bottomMargin: 6
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 10
                        EmptyAction { objectName: "emptyNewPage"; label: "New typed page"; icon: "document-new"; primary: true; onClicked: root.newPageAnywhere("typed") }
                        EmptyAction { objectName: "emptyNewInkPage"; label: "Handwritten"; icon: "draw-freehand"; onClicked: root.newPageAnywhere("a4") }
                        EmptyAction { objectName: "emptyNotebooks"; label: "Notebooks"; icon: "folder"; onClicked: { root.leftPanel = "notebooks" } }
                    }
                    Text {
                        visible: emptyState.starred.length > 0
                        text: "Starred"
                        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; font.capitalization: Font.AllUppercase
                        Layout.topMargin: 14; Layout.leftMargin: 4
                    }
                    Repeater {
                        model: emptyState.starred
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: Ui.target + 2
                            radius: 8
                            color: stap.pressed ? Qt.alpha(pal.text, 0.12) : (shov.hovered ? Qt.alpha(pal.text, 0.06) : "transparent")
                            RowLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 12 }
                                spacing: 10
                                Text { text: "★"; color: pal.highlight; font.pixelSize: Ui.text }
                                Text {
                                    text: (modelData.title || "").length ? modelData.title.replace("\u200b", "") : "Untitled page"
                                    color: pal.windowText; font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true
                                }
                                Text {
                                    text: modelData.notebookName + " › " + modelData.sectionName
                                    color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight
                                }
                            }
                            HoverHandler { id: shov }
                            TapHandler { id: stap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: root.openPage(modelData.id) }
                        }
                    }
                    Text {
                        visible: emptyState.recents.length > 0
                        text: "Recent"
                        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; font.capitalization: Font.AllUppercase
                        Layout.topMargin: 14; Layout.leftMargin: 4
                    }
                    Repeater {
                        model: emptyState.recents
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: Ui.target + 6
                            radius: 8
                            color: rtap.pressed ? Qt.alpha(pal.text, 0.12) : (rhov.hovered ? Qt.alpha(pal.text, 0.06) : "transparent")
                            RowLayout {
                                anchors { fill: parent; leftMargin: 10; rightMargin: 12 }
                                spacing: 10
                                Rectangle { implicitWidth: 4; implicitHeight: parent.height - 14; radius: 2; color: modelData.colour || pal.mid }
                                ColumnLayout {
                                    spacing: 0; Layout.fillWidth: true
                                    Text {
                                        text: (modelData.title || "").length ? modelData.title.replace("\u200b", "") : "Untitled page"
                                        color: pal.windowText; font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                    Text {
                                        text: modelData.notebookName + " › " + modelData.sectionName
                                        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                }
                            }
                            HoverHandler { id: rhov }
                            TapHandler { id: rtap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: root.openPage(modelData.id) }
                        }
                    }
                }
            }

            ObjectMenu {
                id: objectMenu
                parent: Overlay.overlay
                Connections { target: root; function onCurrentPageIdChanged() { objectMenu.close() } }
                onOutlineChosen: (c) => { if (subject === "shape") shapeLayer.restyle(c, undefined, 0); info = subject === "shape" ? shapeLayer.selected() : ({}) }
                onFillChosen: (c) => { shapeLayer.restyle("", c, 0); info = shapeLayer.selected() }
                onWidthChosen: (w) => { shapeLayer.restyle("", undefined, w); info = shapeLayer.selected() }
                onDuplicateAsked: subject === "shape" ? shapeLayer.duplicateSelected() : toast.show("Add the picture again to duplicate it", null)
                onDeleteAsked: subject === "shape" ? shapeLayer.removeSelected() : imageLayer.removeSelected()
            }
            StyleBar {
                id: styleBar
                objectName: "chrome"          // the pen must not ink through it
                board: canvas
                shapeLayer: shapeLayer
                imageLayer: imageLayer
                z: 18
                anchors { horizontalCenter: parent.horizontalCenter; bottom: audioBar.top; bottomMargin: toast.visible ? toast.height + 22 : 14 }
                width: implicitWidth
                onToast: (m) => toast.show(m, null)
            }
            Rectangle {
                id: toast
                objectName: "chrome"
                property var undoFn: null
                property string actionLabel: "Undo"
                function show(message, fn, label) { toastText.text = message; undoFn = fn; actionLabel = label || "Undo"; visible = true; toastTimer.restart() }
                anchors { horizontalCenter: parent.horizontalCenter; bottom: audioBar.top; bottomMargin: 14 }
                width: Math.min(page.width - 40, toastRow.implicitWidth + 28); height: Ui.target + 2; radius: 10
                color: pal.text; visible: false; z: 20
                Timer { id: toastTimer; interval: 8000; onTriggered: if (!toastHover.hovered && !undoTap.pressed) toast.visible = false; else restart() }
                HoverHandler { id: toastHover }
                RowLayout { id: toastRow; anchors.centerIn: parent; spacing: 16
                    Text { id: toastText; objectName: "toastText"; color: pal.base; font.pixelSize: Ui.text; elide: Text.ElideMiddle; Layout.maximumWidth: page.width - 200 }
                    Rectangle {
                        objectName: "toastUndo"
                        visible: toast.undoFn !== null
                        implicitWidth: undoLabel.implicitWidth + 24; implicitHeight: Ui.target - 6
                        radius: 8; color: Qt.alpha(pal.base, undoTap.pressed ? 0.35 : 0.16)
                        Text { id: undoLabel; anchors.centerIn: parent; text: toast.actionLabel; color: pal.base; font.pixelSize: Ui.text; font.weight: Font.DemiBold }
                        TapHandler { id: undoTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { if (toast.undoFn) toast.undoFn(); toast.visible = false } }
                    }
                }
            }
        }

        Rectangle { visible: rightSlot.visible; Layout.fillHeight: true; implicitWidth: 1; color: Qt.alpha(pal.text, 0.14) }
        Item {
            id: rightSlot
            Layout.fillHeight: true
            Layout.bottomMargin: Ui.keyboardInset
            Layout.preferredWidth: root.rightPanel.length && !root.tablet ? Ui.rightPanel : 0
            visible: !root.tablet && root.rightPanel.length > 0
            Loader { anchors.fill: parent; sourceComponent: rightPanelComponent; active: !root.tablet && root.rightPanel.length > 0 }
        }
    }

    Component {
        id: leftPanelComponent
        Item {
            Sidebar {
                id: sidebarInst
                anchors.fill: parent
                visible: root.leftPanel === "notebooks"
                onOpenPage: (pageId) => { root.openPage(pageId); if (root.tablet) root.leftPanel = "" }
                onImportPdf: (notebookId) => { importDialog.notebookId = notebookId; importDialog.open() }
                onClosePage: root.closePage()
                property var infoBeforeDelete: ({})
                onDeleting: (kind, id) => { infoBeforeDelete = library.page(root.currentPageId) }
                onDeleted: (kind, id, name) => {
                    const wasCurrent = root.currentPageId
                    const info = infoBeforeDelete || ({})
                    const gone = (kind === "page" && id === wasCurrent) || (kind === "section" && info.sectionId === id) || (kind === "notebook" && info.notebookId === id)
                    if (gone) root.closePage()          // no page is a fine place to be; the toast offers it back
                    toast.show(kind + " “" + name + "” deleted", function() { library.restore(kind, id); if (kind === "page") root.openPage(id) })
                }
                Component.onCompleted: { root.sidebar = sidebarInst; revealPage(root.currentPageId) }
                Component.onDestruction: if (root.sidebar === sidebarInst) root.sidebar = null
            }
            CardsPanel {
                anchors.fill: parent; visible: root.leftPanel === "cards"; pageId: root.currentPageId
                onReview: root.reviewVisible = true
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
            }
            PapersPanel {
                anchors.fill: parent; visible: root.leftPanel === "papers"
                onOpenPage: (pid) => root.openPage(pid)
                onOpenDashboard: (s) => { root.dashboardSubject = s; root.dashboardVisible = true }
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
            }
        }
    }
    property var sidebar: null
    Connections { target: pageStore; function onStorageError(message) { toast.show(message, null) } }
    Component {
        id: rightPanelComponent
        Item {
            AiPanel {
                anchors.fill: parent; visible: root.rightPanel === "claude"; canvas: canvas; pageId: root.currentPageId
                sectionId: audioBar.sectionId; recordingId: audioBar.currentRecordingId
                onOpenPage: (pid) => root.openPage(pid)
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
            }
            TranscriptPanel {
                anchors.fill: parent; visible: root.rightPanel === "transcript"; recordingId: audioBar.currentRecordingId
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
            }
            OcrPanel {
                anchors.fill: parent; visible: root.rightPanel === "handwriting"; canvas: canvas; pageId: root.currentPageId
                onInsertText: (text, x, y) => {
                    if (!root.currentPageId) return
                    const id = textBlocks.create(root.currentPageId, x, y, 420, 0, 0)
                    textBlocks.setMarkdown(id, text, 0)
                }
                onToast: (m) => toast.show(m, null)
                onToastAction: (m, label, fn) => toast.show(m, fn, label)
            }
        }
    }

    // full-screen pages
    ReviewPage {
        visible: root.reviewVisible; anchors.fill: parent; z: 30
        onClosed: root.reviewVisible = false
        onToast: (m) => toast.show(m, null)
    }
    DashboardPage { visible: root.dashboardVisible; anchors.fill: parent; subject: root.dashboardSubject; z: 30; onClosed: root.dashboardVisible = false }
    SettingsPage {
        visible: root.settingsVisible; anchors.fill: parent; canvas: canvas; z: 30
        onClosed: { root.settingsVisible = false; root.keyboardMode = library.setting("keyboard.mode", "tablet") }
        onToast: (m) => toast.show(m, null)
    }
    // ---- The on-screen keyboard. It appears when a text field asks for input and the app is in
    // tablet mode (or you have set it to always), and it never covers the field it is filling.
    property string keyboardMode: library.setting("keyboard.mode", "tablet")   // tablet | always | never
    readonly property bool keyboardWanted: keyboardMode === "always" || (keyboardMode === "tablet" && root.tablet)
    Keyboard {
        id: keyboard
        objectName: "chrome"
        // It must live in the overlay, not in the window: the overlay sits above everything at
        // z ≈ 1e6, so as a plain window child the keyboard was covered by (and, for a modal, inert
        // under) every popup — which is every text field in the app (D-064).
        parent: Overlay.overlay
        z: 100
        anchors { left: parent.left; right: parent.right }
        // Parked off the bottom until a text field wants it. Ui.keyboardInset tells the rest of the
        // app how much of the bottom it has taken, so nothing it fills ends up underneath it.
        property bool wanted: root.keyboardWanted && keys.focusIsText && !keyboard.dismissed
        property bool dismissed: false
        y: wanted ? parent.height - height : parent.height
        visible: y < parent.height
        Behavior on y { NumberAnimation { duration: Ui.quick; easing.type: Easing.OutCubic } }
        onHidden: { dismissed = true; keys.dropFocus() }
        Binding { target: Ui; property: "keyboardInset"; value: keyboard.visible ? keyboard.height : 0 }
        Connections { target: keys; function onFocusChanged() { if (keys.focusIsText) keyboard.dismissed = false } }
    }
    ShortcutsPage {
        visible: root.keysVisible; anchors.fill: parent; z: 30
        onClosed: root.keysVisible = false
    }
    TrashPage {
        visible: root.trashVisible; anchors.fill: parent; z: 30
        onClosed: root.trashVisible = false
        onToast: (m) => toast.show(m, null)
        onOpenPage: (pageId) => { root.trashVisible = false; root.openPage(pageId) }
    }
    PageBrowser {
        id: pageBrowser
        visible: root.browserVisible; anchors.fill: parent; z: 30
        sectionId: library.page(root.currentPageId).sectionId || 0
        currentPageId: root.currentPageId
        onClosed: root.browserVisible = false
        onToast: (m) => toast.show(m, null)
        onOpenPage: (pageId) => { root.browserVisible = false; root.openPage(pageId) }
    }
    HoldTip { parent: Overlay.overlay }
    Onboarding {
        visible: root.onboardingVisible; anchors.fill: parent; z: 40
        onDone: { root.onboardingVisible = false; library.setSetting("onboarded", "1") }
    }

    // ================================================================ shortcuts (Goodnotes-shaped)
    // While a full-screen surface is up, the page behind it is not what the keys mean.
    readonly property bool overlayUp: reviewVisible || settingsVisible || dashboardVisible || onboardingVisible || trashVisible || browserVisible || keysVisible
    // Keys that mean something only to the ink canvas must never fire while you are typing.
    readonly property bool pageKeys: !overlayUp && !keys.focusIsText
    readonly property bool inkKeys: pageKeys && !pageTyped
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+Z"; onActivated: canvas.undo() }
    Shortcut { enabled: root.inkKeys; sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]; onActivated: canvas.redo() }
    Shortcut { enabled: root.inkKeys; sequence: "1"; onActivated: canvas.tool = "pen" }
    Shortcut { enabled: root.inkKeys; sequence: "2"; onActivated: canvas.tool = "highlighter" }
    Shortcut { enabled: root.inkKeys; sequence: "3"; onActivated: canvas.tool = "eraser" }
    Shortcut { enabled: root.inkKeys; sequence: "4"; onActivated: canvas.tool = "lasso" }
    Shortcut { enabled: root.inkKeys; sequence: "5"; onActivated: if (canvas.wordCount > 0) canvas.tool = "text" }
    Shortcut { enabled: root.inkKeys; sequence: "6"; onActivated: canvas.tool = "textblock" }
    Shortcut { enabled: root.inkKeys; sequence: "7"; onActivated: canvas.tool = "shape" }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+E"; onActivated: canvas.tool = "eraser" }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+L"; onActivated: canvas.tool = "lasso" }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+H"; onActivated: canvas.tool = "highlighter" }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+T"; onActivated: canvas.tool = "textblock" }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+A"; onActivated: canvas.selectAll() }
    // Qt matches a shortcut before it delivers the key, and a text item only claims ShortcutOverride
    // for keys below Escape — so while you are typing this used to swallow Escape and TextLayer's
    // Keys.onEscapePressed never ran.
    Shortcut { enabled: !keys.focusIsText; sequence: "Escape"; onActivated: {
        if (objectMenu.opened) { objectMenu.close(); return }
        if (root.onboardingVisible) { root.onboardingVisible = false; return }
        if (root.reviewVisible) { root.reviewVisible = false; return }
        if (root.settingsVisible) { root.settingsVisible = false; return }
        if (root.dashboardVisible) { root.dashboardVisible = false; return }
        if (root.browserVisible) { root.browserVisible = false; return }
        if (root.trashVisible) { root.trashVisible = false; return }
        if (root.keysVisible) { root.keysVisible = false; return }
        canvas.selectNone(); if (root.tablet) { root.leftPanel = ""; root.rightPanel = "" }
    } }
    Shortcut { enabled: root.inkKeys; sequences: ["Delete", "Backspace"]; onActivated: canvas.deleteSelection() }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+C"; onActivated: canvas.hasTextSelection ? canvas.copyText() : canvas.copySelection() }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+X"; onActivated: canvas.cutSelection() }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+V"; onActivated: { if (!imageLayer.pasteClipboard()) canvas.paste() } }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+Shift+G"; onActivated: pictureDialog.open() }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+P"; onActivated: if (root.currentPageId) root.browserVisible = true }
    Shortcut { enabled: !root.overlayUp && audio.recording; sequence: "Ctrl+Shift+M"; onActivated: { audio.addMark(audio.recordingId, audio.nowMs()); toast.show("Marked", null) } }
    Shortcut { enabled: root.pageKeys; sequence: "Ctrl+B"; onActivated: if (root.currentPageId) { const on = !library.isStarred(root.currentPageId); library.setStarred(root.currentPageId, on); toast.show(on ? "Page starred" : "Star removed", null) } }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: root.trashVisible = !root.trashVisible }
    Shortcut { sequences: ["Ctrl+/", "Ctrl+?"]; onActivated: root.keysVisible = !root.keysVisible }
    Shortcut { enabled: root.pageKeys; sequence: "Ctrl+D"; onActivated: { const id = library.duplicatePage(root.currentPageId); if (id) { root.openPage(id); toast.show("Page duplicated", null) } } }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+W"; onActivated: root.closePage() }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+0"; onActivated: canvas.fitPage() }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+1"; onActivated: canvas.fitWidth() }
    Shortcut { enabled: root.inkKeys; sequences: ["Ctrl+=", "Ctrl++"]; onActivated: canvas.zoomAt(1.2, Qt.point(canvas.width / 2, canvas.height / 2)) }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+-"; onActivated: canvas.zoomAt(1 / 1.2, Qt.point(canvas.width / 2, canvas.height / 2)) }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+N"; onActivated: root.newPage() }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+Shift+N"; onActivated: root.newSection() }
    Shortcut { enabled: !root.overlayUp; sequence: "PgDown"; onActivated: root.stepPage(1) }
    Shortcut { enabled: !root.overlayUp; sequence: "PgUp"; onActivated: root.stepPage(-1) }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+S"; onActivated: pageStore.flush() }
    Shortcut { sequence: "Ctrl+K"; onActivated: search.open() }
    Shortcut { sequence: "Ctrl+\\"; onActivated: root.toggleLeft("notebooks") }
    Shortcut { sequence: "Ctrl+J"; onActivated: root.toggleRight("claude") }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+Shift+H"; onActivated: root.toggleRight("handwriting") }
    Shortcut { sequence: "Ctrl+Shift+C"; onActivated: root.toggleLeft("cards") }
    Shortcut { sequence: "Ctrl+Shift+P"; onActivated: root.toggleLeft("papers") }
    Shortcut { sequence: "Ctrl+Shift+R"; onActivated: root.reviewVisible = !root.reviewVisible }
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: tabletMode.tablet = !tabletMode.tablet }
    Shortcut { sequence: "Ctrl+,"; onActivated: root.settingsVisible = !root.settingsVisible }
    Shortcut { enabled: !root.overlayUp && root.currentPageId > 0; sequence: "Ctrl+R"; onActivated: { if (audio.recording) audio.stopRecording(); else audioBar.startRecording() } }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+M"; onActivated: if (canvas.hasSelection) ocr.latexFromImage(canvas.renderSelectionToPng()) }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+Shift+E"; onActivated: exportDialog.open() }
    Shortcut { enabled: !root.overlayUp; sequence: "Ctrl+Shift+O"; onActivated: { const info = library.page(root.currentPageId); if (info.id) { importDialog.notebookId = info.notebookId; importDialog.open() } } }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+Shift+B"; onActivated: canvas.addBenchmarkStrokes(10000) }
    Shortcut { enabled: root.inkKeys; sequence: "Ctrl+Shift+I"; onActivated: canvas.infinite = !canvas.infinite }
    Shortcut { sequence: "F12"; onActivated: root.showStats = !root.showStats }
    Shortcut { sequence: "F11"; onActivated: root.visibility = root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
    Shortcut { sequence: "F1"; onActivated: root.onboardingVisible = true }
}

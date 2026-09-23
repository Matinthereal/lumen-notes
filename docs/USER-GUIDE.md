# Lumen — user guide

For a pen, a touchscreen, a keyboard, or all three. Everything works offline except the Claude
features. Transcription, handwriting recognition and PDF import use the optional Python helpers
(see the README); the rest of the app needs nothing else.

## Start

```
lumen                             # Linux: the AppImage, or after scripts/install.sh
scripts/run.sh                    # from a build tree
```
On Windows, Start menu → Lumen.

First run looks at what the machine has (pen, touch, keyboard) and picks whether new pages start
typed or handwritten; F1 brings those tips back. A notebook called *My notebook* is ready, with a
typed welcome page.

## Writing

| Do | How |
|---|---|
| Pen, highlighter, eraser, lasso | keys 1–4, or the toolbar |
| Temporary eraser | hold the pen's side button |
| Undo | double-press the pen's button, or Ctrl+Z |
| Straight line, arrow, circle, box | draw it, then hold the pen still for half a second |
| Pan / zoom | one finger / pinch (touch is ignored while the pen is near) |
| Pen colour, width, style | toolbar; "classic" is the default pen |
| Page style | toolbar pill: dotted, grid, lined, Cornell, plain |
| New page / section | Ctrl+N / Ctrl+Shift+N; PgUp/PgDn or the on-screen arrows move between pages; tap the page counter for a new page |
| Fit page / fit width | Ctrl+0 / Ctrl+1 (or tap / long-press the zoom pill) |
| Undo with fingers | two-finger double-tap |

## Typing without a keyboard (tablet mode)

Fold the screen back and the app draws its own keyboard: it appears whenever a text box asks for
input — renaming a page, searching, asking Claude — and hides when you tap **Hide** or tap away.
Hold ⌫ or an arrow to repeat. Settings → Tablet mode chooses when it appears: in tablet mode
(the default), always, or never.

## Pictures, paper and page styles

| Do | How |
|---|---|
| Add a picture | Ctrl+Shift+G, the toolbar's picture button, or Ctrl+V with an image on the clipboard |
| Move or resize a picture | switch to the lasso (4) — handles appear; with a pen in hand you write *over* pictures instead |
| Paper colour | the "paper" pill: white, cream, cool grey or charcoal, per page. The page keeps its own colour, so ink written under one theme stays readable under the other |
| Page style | the style pill: dotted, lined, squared, **2 mm graph paper**, **isometric**, **music staves**, Cornell, plain |
| Cross something out | scribble over it with the pen — a real back-and-forth rubs out what it crosses (a zig-zag drawing is left alone) |
| Straight line | hold **Shift** while drawing: the stroke snaps to the nearest 45° |
| Recolour or re-weight ink | lasso it, then tap a colour or a width |
| Favourite pens | the three slots after the widths: tap to take one up, hold to store the pen you are using |
| Trim or turn a picture | select it, then **Trim…** or **Turn** in its menu; **Original** brings back the whole picture. The file itself is never changed |

## Notebooks, pages, and getting things back

| Do | How |
|---|---|
| Open a page | tap it in the sidebar — a tap never opens a menu |
| Rename, delete, close, duplicate a page | **press and hold** the row (half a second — a line fills along it as you hold), or right-click |
| New section / new page | the **+** on a notebook or section row |
| Close the page you are on | Ctrl+W, or "Close page" in the row's menu |
| Nothing open | that is a normal state: the page area offers New page, Notebooks, and your recent pages. It is remembered, so the app starts there next time |
| Undo a delete | the toast at the bottom — it waits while you hover or hold it. Pages, sections, notebooks, cards and papers all come back |
| Audio you deleted | goes to the data folder's `trash` for 30 days; the toast's Undo restores it |
| Things that ask first | deleting a recording entirely, and deleting a paper question — those two cannot be undone |
| Everything in this section | Ctrl+P — thumbnails; tap to open, hold for duplicate/move/delete, select several to move or bin them together |
| Duplicate a page | Ctrl+D (ink, typed blocks, pictures and any PDF backing come with it) |
| Star a page | Ctrl+B — starred pages sit at the top of the empty state and of search |
| Recently deleted | Ctrl+Shift+D — restore anything from the last thirty days, or delete it for good |
| Every key | Ctrl+/ |

## Typing, maths, search

- Tool 6 (Ctrl+T) then tap: a Markdown block. `$x^2$` and `$$\int_0^1 x\,dx$$` render as maths.
- Lasso handwritten maths → **∑ LaTeX** (Ctrl+M): recognised locally; *Improve with Claude* sends
  the picture only when you press it.
- Ctrl+K searches typed text, recognised handwriting, transcripts and PDFs.
- Ctrl+Shift+H shows what was read from your handwriting; edit a line to correct it.

## Links, tags and finding your way back

- Type `[[` in typed text and pick a page: the link follows the page, not its name, so renaming
  never breaks it. Tap a link to go there.
- **This page** (Ctrl+Shift+L, or its rail button) shows what links here, the page's tags, and an
  outline of its headings; tap a heading to jump to it.
- Tags: add them in *This page*, or lasso some handwriting and tap **# Tag** to make it one. The
  page browser and Ctrl+K filter by tag, and `#tag` in a search does too.
- Alt+← and Alt+→ (or the arrows beside the page's name) step back and forward through the pages
  you have opened.

## Two pages, presenting, sharing

- **Split view**: Ctrl+Shift+S, or *Open beside* in a page's menu, puts a second page next to this
  one (a PDF beside your notes, say). Each side has its own tools, zoom and undo; drag the divider,
  close either side.
- **Present**: F5 shows the section full screen, one page at a time. Arrow keys, space, a tap or a
  swipe move on; the pen becomes a laser that fades and never writes. Esc leaves.
- **Share a notebook**: hold a notebook in the sidebar → *Export notebook…* writes one `.lumen`
  file with its ink, text, links, pictures and PDFs. *Import a notebook* (beside the sidebar's +)
  opens one as a new notebook. Recordings, flashcards and papers stay behind.

## Slides and PDFs

- Sidebar → the import icon on a notebook, or Ctrl+Shift+O: the PDF becomes a section, one page per slide.
- Tool 5 selects PDF text (Ctrl+C copies). The highlighter snaps to lines of text.
- Ctrl+Shift+E exports the current section as a PDF with your ink baked in.

## Lessons and audio

- **Hold** the red button (or press Ctrl+R) to record; a tap only shows the hint, and a resting palm cannot start it. The bar turns red with REC and a timer while recording; tap the button to stop. *Test mic* shows the level for eight seconds.
- Later: **Tap to hear** plays the moment a stroke was written; **Replay ink** redraws in time.
- While recording, **Ctrl+Shift+M** (or the Mark button) drops a mark; marks show on the playback bar and a tap jumps there.
- The **1×** chip cycles playback speed: 1 · 1.25 · 1.5 · 2 · 0.75.
- The handwriting panel's **Insert as text** puts the page's recognised writing on the page as a typed block, and the ↧ on a line inserts just that line. Your ink is never replaced.
- The transcript panel's ⋯ menu: re-transcribe, delete the transcript (keep audio), delete the audio (keep transcript), or delete the recording.
- Untitled pages take their name from the first heading you type or the first line read from your handwriting (shown in italics); rename to make it stick. Ctrl+K with nothing typed lists recent pages.

## Claude (Ctrl+J)

Notes from a recording, flashcards to tick and add, explain a selection, ask your notes. The exact
prompt is shown first; nothing is sent until you press Send. Audio never leaves the machine.
*Online* allows web search (e.g. an exam spec).

## Flashcards

Ctrl+Shift+C lists cards (add by hand, import/export Anki `.apkg`); Ctrl+Shift+R reviews:
Space flips, 1–4 rates. Scheduling is FSRS.

## Past papers

Ctrl+Shift+P → *Import pair* (paper + mark scheme). Questions are detected from the numbering;
the paper bar lets you fix marks and topics, run an attempt (timed or not), and enter marks per
question with an error type. *Dashboard* shows the weakest topics, where marks go, and the trend.

## Tablet mode and settings

Ctrl+Shift+T (or the tablet icon): bigger targets, a floating toolbar you can drag, and the app's
own keyboard for text fields. Plasma switches tablet mode by itself on machines that report being
folded; anywhere else, use the switch. Holding any button shows what it does without doing it.

Ctrl+, opens Settings: pen, touch, **writing hand** (left moves the rail and panels to the other
side), page defaults, **background services** (what each Python helper is doing, and a restart),
transcription and OCR models, backups (*Back up now*; nightly at 03:00 on Linux, once a day while
Lumen is open on Windows), Claude status.

## Where things live

Linux: `~/.local/share/lumen/` — database, journals, attachments, models, Claude log.
Windows: `%LOCALAPPDATA%\lumen\`. Backups go to a `Backups/lumen` folder in your home folder.
Set `LUMEN_DATA_DIR` to keep a whole library (and its cache) somewhere else.
Logs: `QT_FORCE_STDERR_LOGGING=1 lumen`, or `journalctl --user` on Linux.

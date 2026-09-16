# Lumen — user guide

Built for one machine: the Galaxy Book5 360 with the S Pen on Fedora/KDE. Everything works offline
except the Claude features.

## Start

```
~/lumen/scripts/run.sh            # from the build tree
lumen                             # once installed (RPM)
```
First run shows four tips (F1 brings them back). Three notebooks are ready: Maths (Edexcel),
Physics (AQA), Computer Science (OCR H446), each with sections from the spec.

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

## Notebooks, pages, and getting things back

| Do | How |
|---|---|
| Open a page | tap it in the sidebar — a tap never opens a menu |
| Rename, delete, close, duplicate a page | **press and hold** the row (half a second — a line fills along it as you hold), or right-click |
| New section / new page | the **+** on a notebook or section row |
| Close the page you are on | Ctrl+W, or "Close page" in the row's menu |
| Nothing open | that is a normal state: the page area offers New page, Notebooks, and your recent pages. It is remembered, so the app starts there next time |
| Undo a delete | the toast at the bottom — it waits while you hover or hold it. Pages, sections, notebooks, cards and papers all come back |
| Audio you deleted | goes to `~/.local/share/lumen/trash` for 30 days; the toast's Undo restores it |
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

Ctrl+Shift+T (or the tablet icon): bigger targets, a floating toolbar you can drag, the on-screen
keyboard for text fields once `maliit-keyboard` is installed and enabled in Plasma. Rotation is set in Settings (this laptop exposes no hinge sensor to
Linux). Ctrl+, opens Settings: pen, touch, page defaults, transcription models, OCR model,
backups (nightly at 03:00, *Back up now*), Claude status.

## Where things live

`~/.local/share/lumen/` — database, journals, attachments, models, Claude log.
`~/Backups/lumen/` — nightly zips. Logs: `QT_FORCE_STDERR_LOGGING=1 lumen` or `journalctl --user`.

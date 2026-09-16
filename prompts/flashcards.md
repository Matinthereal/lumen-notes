Make spaced-repetition flashcards from the material below for a student ({{subject}}).
Return ONLY a JSON array, no prose, of 8–20 objects with this shape:
{"kind": "basic" | "cloze", "front": "...", "back": "...", "tags": ["topic", ...]}

Rules: one fact per card; "basic" cards ask a question on the front and answer on the back;
"cloze" cards put the sentence with the hidden part as {{c1::…}} in "front" and leave "back" empty;
use $…$ for maths; prefer definitions, formulae, conditions, and "why" questions over trivia.

MATERIAL:
{{material}}

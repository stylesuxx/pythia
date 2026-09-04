PYTHIA// user files

settings.json holds the preferences and names the theme and the layout in
use. themes/ holds a folder per theme, each with a theme.json inside, and
layouts/ a file per layout. The built-in ones are themes/neon/theme.json
and layouts/default.json; the terminal writes them again whenever they are
missing, so there is always something to copy from. Edit any file, then
eject the drive: the terminal reads them at once and switches over. A file
it cannot accept is refused and the previous one stays; STATUS.txt says
what was applied and why a file was refused.

settings.json: "theme" and "layout" name a folder under themes/ and a file
under layouts/, in lower-case letters, digits, - and _. "display_rotated"
turns the display half a turn, "haptics" switches the cues, "reverse_knob"
swaps the turning direction, "sleep_after" is the seconds without input
before the screen sleeps (0 never sleeps), and "brightness" is a per cent
ceiling on the light, 1 to 100. A key left out means its default.

themes/<name>/theme.json: every colour takes "#RRGGBB" and every key is
optional. "colors" holds the general roles; each screen has a section of
its own (boot, list, caption, numbers, oracle, coin) whose keys win over the
roles they follow. A key left out follows its role; a role left out keeps
the built-in value. Copy the neon folder under a name of your own and put
that name in settings.json.

layouts/<name>.json: "dice" lists the dice in the order the knob browses
them, one to sixteen. Each has a "name", as wide as the rim caption allows,
drawn with the characters the built-in faces carry (digits and the letters
of the built-in names); a "kind" of numeric, coin, d66 or oracle; "sides"
from 2 to 100 for a numeric die; and optionally an "effect", slide or tear,
for how its result arrives. "default_effect" at the top is what the rest
take.

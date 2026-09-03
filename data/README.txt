PYTHIA// user files

theme.json on this drive is the look in use and layout.json the dice the
knob offers. Edit either, then eject the drive: the terminal reads them at
once and switches over. A file it cannot accept is refused and the previous
one stays; STATUS.txt says what was applied and why a file was refused.
Delete a file and eject to go back to the built-in one; it is written again
from that.

theme.json: every colour takes "#RRGGBB" and every key is optional. "colors"
holds the general roles; each screen has a section of its own (boot, list,
caption, numbers, oracle, coin) whose keys win over the roles they follow. A
key left out follows its role; a role left out keeps the built-in value.

layout.json: "dice" lists the dice in the order the knob browses them, one
to sixteen. Each has a "name", as wide as the rim caption allows, drawn
with the characters the built-in faces carry (digits and the letters of the
built-in names); a "kind" of numeric, coin, d66 or oracle; "sides" from 2 to
100 for a numeric die; and optionally an "effect", slide or tear, for how its
result arrives. "default_effect" at the top is what the rest take.

# Working on shrub

Use the installed Nool CLI for semantic context, proposals, and saved changes.
Start with `nool status --json` and
`nool context "<task>" --budget 2000 --cite`. Read the cited source before editing.
Consult `nool <command> --help` before using an unfamiliar command.

Save changes with `nool propose --all --intent "<change>" --test-note "<validation>"`
and `nool solidify --candidate <id> --full`. Use the candidate ID returned by the
proposal so only your own work is saved. Run `nool doctor` before handing off.
Do not push unless the user requests it.

Build the game with `make`. On Debian/Ubuntu the build dependencies are
`build-essential`, `libncurses-dev`, and `emacs-bin-common` (for `etags`).
Run `TERM=xterm-256color ./shrub` in an interactive terminal. The main game loop
has no quit key; use Ctrl-C to stop it.

There is no complete automated test suite: the Makefile's `utest` target refers
to a missing `utest.c`. For setup changes, verify the build and a short terminal
smoke test. For simulation changes, validate the affected behavior directly.

The main executable uses `game.c` and `worldview.c` for the terminal interface;
`shrub.c`, `genome.c`, `items.c`, `perlin.c`, and `roulette.c` implement the
simulation. `serialize.c` handles partial genome persistence. `display.c`,
`menu1.c`, `bg.c`, and `perf.c` are outside the default build.

Keep generated binaries, TAGS, runtime databases, caches, and checkout-specific
agent hooks ignored. Retain Nool's canonical artifacts and configuration.

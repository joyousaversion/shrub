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

The game, regression suites, process-isolated test runner, and terminal smoke
helper are written in C. Keep verification in C; no Python runner is needed.
Run `make test`, `make test-sanitize`, and `make test-smoke`. The repaired
suites pass 68/68 cases in both ordinary and sanitized modes. The sanitized
historical baseline at `742229a` had 4 passing and 17 failing cases; the
per-suite counts and evidence are recorded in `docs/VERIFICATION.md`.
Read the implemented contracts there before changing test expectations.
Use `.build/test_biology_sanitize <case>` to isolate a case, or a suite's
`--list` and `--all --report <path>` options to inspect it and save results.
The legacy `utest` target refers to a missing `utest.c`.
The CI workflow runs the game build, both regression modes, and terminal smoke.

The main executable uses `game.c` and `worldview.c` for the terminal interface;
`shrub.c`, `genome.c`, `items.c`, `perlin.c`, and `roulette.c` implement the
simulation. `serialize.c` handles partial genome persistence. `display.c`,
`menu1.c`, `bg.c`, and `perf.c` are outside the default build.

Keep generated binaries, TAGS, runtime databases, caches, and checkout-specific
agent hooks ignored. Retain Nool's canonical artifacts and configuration.

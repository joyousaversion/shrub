# shrub 

Shrub was created by Matthew Charles Baker in
[fimbaz/shrub](https://github.com/fimbaz/shrub). This repository adds biological
verification and repairs to that project.

![Shrub](shrub2.gif)
### shrub is a TUI pet plant simulator currently featuring
- big scrolly worlds
- water
- wind
- dirt
- plants
- pollen
- seeds
- genes
- reproduction 
- death
- decay

Controls:
- hjkl <- move cursor
- HJKL <- jump cursor
- s    <- plant seed @ cursor
- d    <- cut plant
- 0..6 <- resource view

Interesting things:
- Male flowers are pink
- female flowers are purple
- pollen is '.'
- new seeds are usually '&'


Known limitations:
- no saving
- limited UI
- may segfault with certain libc
- the world is a dense data structure, so everything is n^2 with worldsize

Helpful hints:
- Shrub was designed for xterm-256color and requires ncurses-dev to build

Installation guide:
 1. observe common params in game.c
 2. make
 3. ./shrub

### Development with Nool

This project is set up for the Nool CLI. On Debian/Ubuntu, install the build
dependencies with `sudo apt-get install build-essential libncurses-dev emacs-bin-common`.

```sh
nool init
nool status --json
nool context "plant growth and the simulation loop" --budget 2000 --cite
make
TERM=xterm-256color ./shrub
```

Use Ctrl-C to stop the game. Save changes through Nool:

```sh
nool propose --all --intent "Describe the change" --test-note "Describe validation"
nool solidify --candidate <id-from-propose> --full
nool doctor
```

`nool architecture review` lists recovered architecture candidates for review.
See [AGENTS.md](AGENTS.md) for the source layout and agent workflow.

### Biological verification

The game and its verification tools are written in C. Run the deterministic
regressions, AddressSanitizer/UndefinedBehaviorSanitizer checks, and terminal
smoke test with:

```sh
make
make test
make test-sanitize
make test-smoke
```

Each regression case runs in a separate process with a 15-second timeout.
The terminal helper starts the game in a pseudo-terminal, sends a short input
sequence, checks rendered terminal output, and stops it with SIGINT.
GitHub Actions runs these same commands on Linux for pushes and pull requests.

The [verification record](docs/VERIFICATION.md) describes the implemented
inheritance, nutrient, germination, and tissue rules, test evidence, and
remaining coverage limits. The repaired suite passes **68/68 cases** in both
ordinary and sanitized modes. The historical **4/21 passing** sanitized
baseline describes the original engine before repairs.

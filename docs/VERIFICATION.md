# Plant biology verification

The verification work repairs genetics, inheritance, weighted selection,
growth, germination, and tissue differentiation against explicit behavioral
contracts. The game, headless suites, test runner, and terminal smoke helper
are written in C. Tests link the simulation sources without the terminal UI.

The original engine at Git commit `742229a` had **4 of 21 cases passing under
AddressSanitizer and UndefinedBehaviorSanitizer, with 17 failing**. The ordinary
build passed 5 of 21 because an array overflow escaped its output assertion.
Those are historical failing baselines, separate from the repaired suite.
The repaired suite passes **68/68 cases in both ordinary and sanitized modes**.

## Implemented contracts

Comments, data structures, sample configuration, and `docs/cell` informed the
contracts. Some choices clarify ambiguous behavior; their tests establish the
chosen behavior and do not retroactively prove that the original game promised
it. The preserved rules and corrective policy decisions are identified below.

### Genetics, inheritance, and species identity

`init_sample_run` clears both the experiment and genome before applying sample
defaults. All declared fields initialize independently of prior memory contents.
`apply_genome` maps each tissue and nutrient field to its corresponding
phenotype without changing its inputs.

Crossover preserves the existing cut range **0 through 15**, selected from the
low four bits of a random draw. Each bit of each child gets its own mutation
draw and toggles when `rand() % 1024 == 0`. This chosen 1/1024 policy includes
both zero and nonzero multiples of 1024; child two mutates its own output.

| Genome fields | Inheritance behavior |
| --- | --- |
| `evolve_threshold[THC][TC]` | Recombine only the declared `THC` axes; truncate the 16-bit result to the stored 8-bit threshold. |
| `transfer_bias[NC][TC]` | Recombine every nutrient channel, including WATER, into separate child outputs. |
| `reproduction_bias[TC]` | Recombine into separate child outputs. |
| `min`, `max`, `evolve_type`, `max_offspring`, `flux_cost`, `initial_flux`, `min_seed_nutrients`, `name`, `remarks` | Child one copies its respective first parent; child two copies its respective second parent. |
| `parents` | Child one records the first then second stored parent genome ID; child two records the reverse order. |
| `genome_id` | Clear both offspring IDs before registration; registration assigns the stored array entry's ID. |

Both children retain the existing flower safeguards: male-flower tip threshold
and female-flower root threshold are 100. The safeguards apply symmetrically.
Parents remain unchanged by inheritance.

Species identity compares the declared genotype fields, including seed nutrient
requirements. It excludes names, remarks, assigned IDs, and ancestry metadata.
Registering the same genotype reuses its existing species and retains the
**first registration's ancestry**. This chosen identity policy provides a
canonical genotype record, not a complete pedigree graph. New species have
zeroed statistics, and random-species templates remain unchanged even when a
stored template's genome array grows during registration.

### Weighted selection and breeding

Roulette tickets cover integer intervals starting at zero, with exactly the
configured weight assigned to each entry. Zero-weight entries never win.
Totals use a wide unsigned accumulator; totals above `UINT_MAX` and `RAND_MAX`
are supported. Random draws expand to the required range and reject incomplete
modulo tails to avoid selection bias.

Roulette returns `-1` for null input/output pointers, zero count or spins, or
zero total weight, without changing winners. Breeding returns `-1` for invalid
world/zone/diversity inputs, invalid occupied species IDs, and extinct selected
cohorts before registering offspring. Diversity must be positive, even, and
small enough to select actual species rather than reserved entry zero.

The breeding cohort is the latest `diversity` registered species. Winners are
relative indices into that cohort and are translated back to global species
indices before reading parent genomes. Biomass in older species cannot revive
an extinct selected cohort. Recorded ancestry uses the stored parents' IDs.

### Growth and nutrient accounting

Growth admission and debit use the **offspring tissue's reproduction costs for
all `NC` channels**, including nonzero WATER costs. This is a chosen corrective
policy; the sample configuration's WATER birth cost remains zero. The same
costs gate and charge growth, preventing unsigned nutrient balances wrapping.

Escrow transfers into nutrients once, up to the byte storage capacity. Any
excess remains in escrow for a later pass; it is neither discarded nor added
again after consumption. Escrow survives transplanting into the next tick's
board. Nutrient transfer uses two bits per weighted ticket and treats a
recipient already at or above its configured maximum as having zero room;
recipient balances include both nutrients and escrow. These integration
repairs prevent invalid wheel shifts and unsigned capacity underflow.

Growth eligibility observes offspring capacity,
crowding, matching substrate, direction weights, occupancy, and floaty state.
The richest eligible neighbor wins; only equally rich eligible cells enter the
weighted tie selection. Eight neighboring candidates fit within bounded
storage, including defensive malformed-graph fixtures.

A new module inherits species and plant identity, receives the offspring
phenotype's initial flux, and gets reciprocal parent/offspring links. Its two
`NEW` markers block reproduction again during the same growth pass. Ordinary
germination creates the circular root/shoot anchor and remains eligible for
later stages, including growth, in the same `tick_CA` call. The existing tick
order is preserved.

### Planting and germination

Planting rejects invalid species, coordinates, substrate, exhausted plant-ID
capacity, and occupied or reserved root/shoot cells on either tick board.
Failed placement returns `-1` without overwriting modules or registering a
plant. Successful placement initializes module state and reciprocal circular
anchor links while retaining cell resources, wind, and unrelated items.

Germination checks the planting cell and all eight neighbors on both boards.
It finds a seed in an item list, consumes that seed only after successful
placement, and preserves unrelated items. Blocked or invalid seeds remain
available. Failed germination does not create a plant.

### Tissue differentiation

Tip distance counts modules, with a terminal tip equal to **one**. A circular
root/shoot anchor without descendants also has tip distance one. The anchor's
paired half does not lengthen its own descendant subtree. Root distance counts
parent-link steps to the circular seed anchor, whose distance is **zero**.

Tip thresholds preserve the inclusive comparison `distance >= threshold`.
Root thresholds preserve the strict comparison `distance > threshold`.
Both rules read the **original tissue's** phenotype for that pass; when both
fire, the root rule takes precedence. Target `NONE` disables its rule, as does
an invalid target tissue. A zero threshold is active when its target is valid.

Bounded iterative traversal returns distance zero for malformed paths, invalid
coordinates/directions, cycles, and inconsistent plant links. Tissue lookup
rejects invalid species or tissue IDs. Differentiation changes tissue while
retaining plant/species identity and structural links. The distance-axis enums
now agree on tip and root indexing.

### Pollination and seed production

These are chosen corrective contracts for the flower lifecycle, verified
alongside the inherited traits and graph rules:

- A male flower attempts pollen creation with the existing 50% chance, checked
  before creation. It must have one PHOTO, one HUMMUS, and one WATER available.
  Each successful pollen item packages and debits one unit of all three.
  Failed or skipped creation does not debit those nutrients.
- Fertilization validates the flower's parent and pollen's species before
  reading genomes. Successful inheritance consumes pollen and marks the
  female for reaping even if its offspring deduplicates to the parent's
  genotype. Species equality is not a fertilization marker.
- Reaping a fertilized female produces its seed genome. An unfertilized female
  produces a FRAGMENT on death rather than a viable seed.

The historical male-flower code created pollen before the random gate and
charged only two nutrients on half the attempts. Its female lifecycle inferred
fertilization from changed species identity. The corrected contracts make
creation, payment, and fertilization explicit.

## Running verification

On Debian/Ubuntu the build dependencies are `build-essential`,
`libncurses-dev`, and `emacs-bin-common` for `etags`.

```sh
make
make test
make test-sanitize
make test-smoke
```

Each regression binary uses `tests/test_runner.h` to run cases in separate
processes with a 15-second timeout. Scripted `rand()` values verify exact cuts,
mutations, and selection tickets. Seeded scenarios exercise repeated behavior.
The sanitized build enables AddressSanitizer, UndefinedBehaviorSanitizer, leak
checks, and fail-fast sanitizer reporting. Compiler checks include
`-Wall -Werror`.

Each suite supports listing, an isolated case, and a JSON status report:

```sh
.build/test_biology_sanitize --list
.build/test_biology_sanitize inheritance
.build/test_biology_sanitize --all --report .build/biology-results.json
.build/test_genetics_extra_sanitize --all --report .build/genetics-results.json
```

The terminal smoke helper runs the real game in a pseudo-terminal, sends
`hl3s0`, checks that the process remains alive and emits terminal escape
sequences, then sends intentional SIGINT. It verifies startup, rendering, and
survival during scripted input. It does not verify the meaning of the UI's
responses to those keys. Interactive play uses `TERM=xterm-256color ./shrub`;
use Ctrl-C to stop it. The legacy `make utest` target still references a missing
source.

`.github/workflows/verify.yml` is configured to run all four commands on
Ubuntu 24.04 for pushes, pull requests, and manual runs. Checkout is pinned to the verified
[actions/checkout v7.0.1 release](https://github.com/actions/checkout/releases/tag/v7.0.1).
The same commands are required locally; Nool is used to record and consolidate
changes rather than as a CI dependency.

## Integrated validation

The complete local runs of `make test` and `make test-sanitize` both pass
**68/68 cases**:

| C suite | Ordinary | ASan/UBSan |
| --- | --- | --- |
| Biology and original regression cases | 29/29 | 29/29 |
| Genetics and weighted selection extras | 8/8 | 8/8 |
| Growth, germination, and nutrient allocation extras | 13/13 | 13/13 |
| Differentiation and graph safety extras | 8/8 | 8/8 |
| Flower lifecycle | 7/7 | 7/7 |
| Connected tick integration | 3/3 | 3/3 |
| **Total** | **68/68** | **68/68** |

`make` and `make test-smoke` also pass after the resource fixes. The recorded
smoke run rendered **3,985 bytes**, survived the scripted input sequence, and
stopped on intentional SIGINT.

The historical 4/21 result above describes the original engine and should not
be used as the current expected result. Hosted CI results are separate from
these local checks and become available when the workflow runs on GitHub.

The connected lifecycle fixture uses seed **1729**. It invokes actual male
pollen creation, female fertilization, and inherited seed production, then
places the emitted seed in a fresh zone and advances it with `tick_CA` for
**12 ticks**. Pollen and seed delivery are explicit fixture handoffs; wind
transport is outside this scenario. Each tick checks reciprocal graph links,
species/plant identity, germination, growth, and tissue differentiation.

This fixture has no rain, maintenance deaths, or photosynthetic input. PHOTO
and WATER birth costs are zero, and HUMMUS birth cost is one. It checks
`HUMMUS + biomass == 12`, zero PHOTO/WATER balances, and semantic state hashes
at every tick across two independently initialized runs with the same seed.
Separate tick fixtures check escrow carry and recipients above configured
nutrient maxima. All three pass under ASan/UBSan after the integration repairs.
The tested lifecycle run ends with biomass **11** and semantic snapshot
`78320311c9079be4` in both runs; the hash is a local reproducibility record,
not a portable promise about every libc's random-number sequence.

## Historical baseline and repair evidence

The four sanitized baseline passes were `phenotype_mapping`, `crossover_cuts`,
`differentiation_thresholds`, and `differentiation_precedence`. They remain
regression guards. Grouped cases stop at their first failure, so 17 failing
cases did not establish 17 distinct production bugs.

| Baseline failure at `742229a` | Observed issue | Fix task |
| --- | --- | --- |
| `initialization` | Initialized data depended on prior fixture contents. | `68b283d5` |
| `crossover_mutations` | A forced second-child mutation changed the first child. | `68b283d5` |
| `inheritance` | Threshold index 2 exceeded the two-axis array; later assertions were blocked. | `bd7b055e`, `68b283d5` |
| `species_identity` | Metadata affected identity; genotype-only identity required a policy decision. | `8567e629`, `68b283d5` |
| `random_species` | Stored genome IDs did not identify their array entries. | `68b283d5` |
| `roulette_edges` | A zero-weight entry won the first ticket. | `aed4c345` |
| `breeding` | Odd diversity read beyond the winners; relative/global indexing was also incorrect. | `bd7b055e`, `aed4c345` |
| `growth_cost` | Admission used child costs while debit used parent costs, wrapping balances to 252 and 250. | `8567e629`, `2bc0d432` |
| `growth_gates` | WATER cost did not gate growth. | `8567e629`, `2bc0d432` |
| `growth_selection` | An ineligible floaty candidate prevented growth into the richest eligible cell. | `2bc0d432` |
| `growth_weighted_ties` | Enumerated tickets did not produce the configured 1:3 weights. | `aed4c345` |
| `growth_eight_neighbors` | Sanitizers detected an eight-entry array's ninth write in a malformed fixture. | `bd7b055e` |
| `newborn_and_escrow` | Consumed escrow remained and could be credited again. | `2bc0d432` |
| `planting` | A second seed overwrote occupied modules. | `5b8b36e6` |
| `germination` | An invalid seed was consumed after unsuccessful placement. | `5b8b36e6` |
| `differentiation_distances` | An anchor with no descendant returned zero while ordinary tips returned one. | `8567e629`, `239d3802` |
| `differentiation_invalid_paths` | An out-of-board parent link crashed under ASan. | `bd7b055e`, `239d3802` |

All work items belong to parent Nool task `e8da8af1` in thread
`plant-verification`. Task `8567e629` records contracts; `bd7b055e` covers
memory bounds; `68b283d5`, `aed4c345`, `2bc0d432`, `5b8b36e6`, and `239d3802`
cover the respective repairs; `2b273142` covers lifecycle and CI integration.

Independent repair suites establish before/after evidence under
ASan/UBSan:

| Suite | Before relevant repairs | After relevant repairs |
| --- | --- | --- |
| Genetics and selection extras | 0/8 | 8/8 |
| Growth, germination, and nutrient allocation extras | 0/13 | 13/13 |
| Flower lifecycle | 2/7 | 7/7 |
| Full-tick integration | 0/3 | 3/3 |

The three full-tick regressions passed **0/3 before their integration repairs**:
the nutrient wheel shifted by four rather than two bits per ticket (UBSan
detected a shift by 32), transplanting dropped escrow, and unsigned recipient
room underflow caused transfers into saturated tissue. The integrated
validation table includes these three repaired regressions.

The genetics comparison compiled the original `genome.c` and `roulette.c`
with their original headers against the current independent fixtures. Those
checks cover nonzero mutation multiples on both children, copied traits and
threshold truncation, every
integer ticket across all 1,023 nonzero five-entry weight combinations in
0..3, wide totals, invalid inputs, mixed global parentage, extinct cohorts,
and stored-template allocation growth. The randomized template fixture uses
seed **1729**. The biology suite retains all 21 original case names and splits
growth gates into independent WATER, PHOTO, HUMMUS, offspring-capacity,
crowding, substrate, and floaty cases, plus a zero-cost birth case. Splitting
checks prevents one failed gate from hiding the next. Local status reports are
kept under ignored `.build/` paths.

## Coverage limits

Finite deterministic and seeded tests verify their stated scenarios. They do
not establish long-term stochastic population stability, balance ecological
parameters, exhaust every world shape, or prove every simulation subsystem.

The suite does not exhaust allocation failures, maximum-size worlds, item
transport under all wind/water patterns, persistence round trips, or every
interactive UI path. The CI gate covers Linux with GCC and ncurses; other
platforms and libc implementations need their own runs. Canonical genotype
ancestry remains a first-registration record rather than a complete pedigree.
Keep these limits explicit when adding future behavior or changing contracts.

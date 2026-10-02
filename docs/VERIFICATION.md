# Proposal: verify plant biology against its stated behavior

Establish executable contracts for genetics, inheritance, growth, and tissue
differentiation. Use the comments, data structures, sample configuration, and
the growth description in `docs/cell` as evidence. Distinguish a documented
promise from an inferred design choice; document ambiguous rules before
encoding them as expected results.

The baseline is Git commit `742229a`. The game builds with `make` and renders
in a short terminal smoke test, but these checks do not establish biological
correctness. There is no working automated suite: `make utest` references a
missing source file.

## Coverage and acceptance criteria

| Path | Entry points | Required verification |
| --- | --- | --- |
| Genetics and phenotype | `init_sample_run`, `apply_genome`, `create_random_species`, `create_species` | Defined fields initialize deterministically; distinct per-tissue and per-nutrient fixture values reach the right phenotype fields; inputs remain unchanged; generated genomes satisfy documented bounds; species identity and deduplication rules are explicit. |
| Inheritance | `crossover`, `combine_genomes`, `tick_flowers`, `auto_breed_plants` | Exact crossover cuts and mutation draws exercise both children; parents remain unchanged; heritable loci and any exclusions are enumerated, including WATER; ancestry IDs identify the actual selected parents; flowering consumes pollen and produces viable seed genomes; zero-fitness and odd population sizes have defined outcomes. |
| Growth | `plant_seed`, `tick_germinate`, `tick_reproduction` | Boundary placement, occupied cells, available nutrients, offspring limits, crowding, substrate, and directional bias are tested; the richest eligible neighbor wins; nutrient admission and debit use the same rule without wraparound; child identity and reciprocal tree links are correct; newborn modules cannot grow again during the same tick. |
| Tissue differentiation | `longest_dist_to_edge`, `longest_dist_to_sprout`, `transform` | Hand-built chains, branching plants, and the circular seed/root connection have known distances; transitions are checked below, at, and above thresholds; zero and disabled thresholds are covered; simultaneous rules have explicit precedence; tissue changes preserve plant/species identity and structural links. |

## Reproduced discrepancies

Small headless probes were compiled against the unchanged engine, using a
scripted `rand()` and UndefinedBehaviorSanitizer. These are baseline failures,
not passing suite coverage. The probe source is retained locally at
`.nool/tmp/verification-probes.c`.

| Case | Evidence | Observed result |
| --- | --- | --- |
| Mutation targeting | `genome.c:22` | A forced mutation in the second child's loop changes child 1: outputs are `1,0` instead of `0,1`. |
| Threshold array bounds | `genome.c:71`, `typedefs.h:6` | `combine_genomes` iterates three threshold forms while `THC` is two. UBSan reports index 2 out of bounds at `genome.c:74`. |
| Growth cost mismatch | `shrub.c:1237`, `shrub.c:1332` | A connected root/shoot fixture with one PHOTO and HUMMUS passes the child-cost check, then subtracts parent costs of five and seven. Balances wrap to 252 and 250. |
| Growth candidate bounds | `shrub.c:1248`, `shrub.c:1296` | An adversarial fixture with eight eligible neighbors writes `win_rng[8]` into an eight-element array; UBSan reports the overflow. This fixture deliberately supplies a disconnected module to test defensive bounds. |

Source inspection also identifies inheritance issues to turn into regressions:
`combine_genomes` passes the same child-2 output address twice for transfer and
reproduction bias, excludes WATER from its transfer loop, and masks inherited
thresholds to the single bit `0x08`. The mutation expression `!rand()%1024`
tests whether the draw itself is zero; the intended mutation probability
needs an explicit contract. Breeding selects relative winner indices but uses
them directly as global genome IDs; ancestry selection needs its own fixture.

For differentiation, establish the intended distance units, the `>=` versus
`>` threshold boundaries, and precedence when both rules fire. Two enums give
different names/orders for the distance axes. Characterize the current
behavior separately from the chosen contract.

## Implementation sequence

1. Build a headless C harness that links the simulation sources without
   `game.c`, `worldview.c`, or ncurses. Use small initialized world fixtures and
   scripted random draws; report each failing assertion independently.
2. Turn the reproduced failures into regressions, then cover the contract
   matrix. Include parent swaps, equivalent fixtures, and resource/structure
   invariants so assertions do not merely duplicate the implementation.
3. Resolve and document the mutation, heritable-locus, growth-cost, distance,
   and rule-precedence contracts. Fix demonstrated violations with focused
   changes and evidence showing each regression fails before its fix.
4. Add `make test` and `make test-sanitize` with AddressSanitizer and UBSan,
   run them in CI, and add a bounded seeded multi-tick scenario covering
   germination, growth, differentiation, pollination, and inheritance.
5. Record commands, random seeds, coverage by contract, and remaining gaps
   in Nool. Completion requires the deterministic suite, sanitizer checks,
   original game build, and terminal smoke test to pass. Any unresolved
   biological rule must remain explicitly open.

Keep simulation changes tied to a failing contract test. UI work, balancing,
performance redesign, and unrelated persistence changes are outside this
proposal.

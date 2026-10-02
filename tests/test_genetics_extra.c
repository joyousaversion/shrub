#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "genome.h"
#include "roulette.h"
#include "test_runner.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); return 1; \
} } while (0)

extern int __real_rand(void);
static int draws[4096], fallback = 1, use_real_random;
static uint draw_count, draw_position;

int __wrap_rand(void) {
    if (use_real_random) return __real_rand();
    return draw_position < draw_count ? draws[draw_position++] : fallback;
}

static void random_reset(int value) {
    draw_count = draw_position = 0;
    fallback = value;
    use_real_random = 0;
}

static void crossover_script(int cut, int first_bit, int second_bit) {
    draws[draw_count++] = cut;
    for (int bit = 0; bit < 16; bit++) draws[draw_count++] = bit == first_bit ? 1024 : 1023;
    for (int bit = 0; bit < 16; bit++) draws[draw_count++] = bit == second_bit ? 2048 : 1025;
}

static int mutation_multiples(void) {
    for (int bit = 0; bit < 16; bit++) {
        unsigned short first, second;
        random_reset(1);
        crossover_script(15, bit, (bit + 1) % 16);
        crossover(0, 0, &first, &second);
        CHECK(first == (1u << bit));
        CHECK(second == (1u << ((bit + 1) % 16)));
        CHECK(draw_position == 33);
    }
    return 0;
}

static int inheritance_copied_traits(void) {
    Experiment experiment;
    Genome mother, father, first, second;
    CHECK(init_sample_run(&experiment, &mother) == 0);
    father = mother;
    mother.genome_id = 19; father.genome_id = 31;
    strcpy(mother.name, "mother"); strcpy(father.name, "father");
    strcpy(mother.remarks, "first source"); strcpy(father.remarks, "second source");
    for (int t = 0; t < TC; t++) {
        mother.reproduction_bias[t] = 0x4000 + t;
        father.reproduction_bias[t] = 0x8000 + t;
        mother.flux_cost[t] = t + 0.25f; father.flux_cost[t] = t + 0.75f;
        mother.initial_flux[t] = t + 1.25f; father.initial_flux[t] = t + 1.75f;
        mother.max_offspring[t] = t % 3; father.max_offspring[t] = (t + 1) % 3;
        for (int n = 0; n < NC; n++) {
            mother.transfer_bias[n][t] = 0x4000 + n * TC + t;
            father.transfer_bias[n][t] = 0x8000 + n * TC + t;
            mother.min[n][t] = n + t; father.min[n][t] = n + t + 1;
            mother.max[n][t] = 60 + n + t; father.max[n][t] = 80 + n + t;
        }
        for (int axis = 0; axis < THC; axis++) {
            mother.evolve_threshold[axis][t] = 0xab;
            father.evolve_threshold[axis][t] = 0x55;
            mother.evolve_type[axis][t] = LEAF;
            father.evolve_type[axis][t] = ROOT;
        }
    }
    for (int n = 0; n < NC; n++) {
        mother.min_seed_nutrients[n] = n + 1;
        father.min_seed_nutrients[n] = n + 4;
    }
    const Genome original_mother = mother, original_father = father;
    random_reset(1);
    /* Mutated bit 8 survives in the biases and truncates out of byte thresholds. */
    for (int locus = 0; locus < TC * (THC + NC + 1); locus++) crossover_script(0, 8, 8);
    combine_genomes(&mother, &father, &first, &second);
    for (int t = 0; t < TC; t++) {
        CHECK(first.reproduction_bias[t] == (father.reproduction_bias[t] ^ 0x100));
        CHECK(second.reproduction_bias[t] == (mother.reproduction_bias[t] ^ 0x100));
        for (int n = 0; n < NC; n++) {
            CHECK(first.transfer_bias[n][t] == (father.transfer_bias[n][t] ^ 0x100));
            CHECK(second.transfer_bias[n][t] == (mother.transfer_bias[n][t] ^ 0x100));
        }
        for (int axis = 0; axis < THC; axis++) {
            if ((axis == 0 && t == FLOWER_MALE) || (axis == 1 && t == FLOWER_FEMALE)) {
                CHECK(first.evolve_threshold[axis][t] == 100 && second.evolve_threshold[axis][t] == 100);
            } else {
                CHECK(first.evolve_threshold[axis][t] == 0x55);
                CHECK(second.evolve_threshold[axis][t] == 0xab);
            }
        }
    }
#define COPIED(field) do { \
    CHECK(memcmp(&first.field, &mother.field, sizeof mother.field) == 0); \
    CHECK(memcmp(&second.field, &father.field, sizeof father.field) == 0); \
} while (0)
    COPIED(min); COPIED(max); COPIED(evolve_type); COPIED(max_offspring);
    COPIED(flux_cost); COPIED(initial_flux); COPIED(min_seed_nutrients);
    COPIED(name); COPIED(remarks);
#undef COPIED
    CHECK(first.parents[0] == 19 && first.parents[1] == 31);
    CHECK(second.parents[0] == 31 && second.parents[1] == 19);
    CHECK(first.genome_id == 0 && second.genome_id == 0);
    CHECK(memcmp(&mother, &original_mother, sizeof mother) == 0);
    CHECK(memcmp(&father, &original_father, sizeof father) == 0);
    CHECK(draw_position == draw_count);
    return 0;
}

static int roulette_ticket_enumeration(void) {
    /* Enumerate every ticket for all five-entry weights from zero through three. */
    for (uint encoded = 1; encoded < 1024; encoded++) {
        uint weights[5], counts[5] = {0}, total = 0;
        for (uint i = 0; i < 5; i++) {
            weights[i] = (encoded >> (2 * i)) & 3;
            total += weights[i];
        }
        for (uint ticket = 0; ticket < total; ticket++) {
            uint winner = UINT_MAX;
            random_reset(ticket);
            CHECK(roulette(weights, &winner, 1, 5) == 0);
            CHECK(winner < 5 && weights[winner] != 0);
            counts[winner]++;
        }
        CHECK(memcmp(weights, counts, sizeof weights) == 0);
    }
    return 0;
}

static void wide_ticket_script(uint64_t ticket, uint64_t total) {
    const uint64_t base = (uint64_t)RAND_MAX + 1;
    uint64_t span = base;
    uint digits = 1;
    while (span < total) { span *= base; digits++; }
    const uint first = draw_count;
    draw_count += digits;
    for (uint i = digits; i > 0; i--) {
        draws[first + i - 1] = ticket % base;
        ticket /= base;
    }
}

static int roulette_wide_totals(void) {
    const uint weights[4] = {UINT_MAX, 0, UINT_MAX, 1};
    const uint64_t total = 2 * (uint64_t)UINT_MAX + 1;
    const uint64_t tickets[] = {0, UINT_MAX - 1u, UINT_MAX, 2 * (uint64_t)UINT_MAX - 1, total - 1};
    const uint expected[] = {0, 0, 2, 2, 3};
    for (uint i = 0; i < sizeof tickets / sizeof tickets[0]; i++) {
        uint winner = UINT_MAX;
        random_reset(1); wide_ticket_script(tickets[i], total);
        CHECK(roulette(weights, &winner, 1, 4) == 0);
        CHECK(winner == expected[i] && draw_position == draw_count);
    }
    const uint small_weights[2] = {1, 2};
    if (((uint64_t)RAND_MAX + 1) % 3 != 0) {
        uint winner;
        random_reset(1); draws[draw_count++] = RAND_MAX; draws[draw_count++] = 2;
        CHECK(roulette(small_weights, &winner, 1, 2) == 0);
        CHECK(winner == 1 && draw_position == 2);
    }
    return 0;
}

static int roulette_invalid_inputs(void) {
    uint weights[3] = {0}, winners[3] = {71, 72, 73};
    const uint original[3] = {71, 72, 73};
    random_reset(1);
    CHECK(roulette(weights, winners, 3, 3) == -1);
    weights[1] = 1;
    CHECK(roulette(NULL, winners, 3, 3) == -1);
    CHECK(roulette(weights, NULL, 3, 3) == -1);
    CHECK(roulette(weights, winners, 3, 0) == -1);
    CHECK(roulette(weights, winners, 0, 3) == -1);
    CHECK(memcmp(winners, original, sizeof winners) == 0);
    CHECK(draw_position == 0);
    return 0;
}

static World *breeding_world(uint *second, uint *third) {
    Experiment experiment;
    Genome genome;
    init_sample_run(&experiment, &genome);
    World *world = create_world(&experiment);
    create_species(world, &genome);
    genome.reproduction_bias[LEAF] = 0xaaaa;
    *second = create_species(world, &genome);
    genome.reproduction_bias[LEAF] = 0x5555;
    *third = create_species(world, &genome);
    alloc_CA(world, 3, 3);
    return world;
}

static void dispose_world(World *world) {
    for (uint i = 1; i < world->max_species_id; i++) {
        free(world->plantae[i].tissues);
        free(world->plantae[i].plant_ids);
    }
    dealloc_world(world);
    free(world);
}

static int breeding_mixed_parentage(void) {
    uint second, third;
    World *world = breeding_world(&second, &third);
    Cell *first_cell = &world->zones[0].new[1][1], *second_cell = &world->zones[0].new[2][1];
    first_cell->species_id = second; first_cell->parent = BOTTOM;
    second_cell->species_id = third; second_cell->parent = TOP;
    const uint before = world->max_species_id;
    random_reset(1); draws[draw_count++] = 0; draws[draw_count++] = 1;
    for (int locus = 0; locus < TC * (THC + NC + 1); locus++) crossover_script(4, -1, -1);
    CHECK(auto_breed_plants(world, 0, 2) == 0);
    CHECK(world->max_species_id == before + 2);
    CHECK(world->genomes[before].parents[0] == second && world->genomes[before].parents[1] == third);
    CHECK(world->genomes[before + 1].parents[0] == third && world->genomes[before + 1].parents[1] == second);
    CHECK(world->genomes[before].reproduction_bias[LEAF] == 0x555a);
    CHECK(world->genomes[before + 1].reproduction_bias[LEAF] == 0xaaa5);
    CHECK(first_cell->species_id == second && second_cell->species_id == third);
    CHECK(world->genomes[before].genome_id == before && world->genomes[before + 1].genome_id == before + 1);
    dispose_world(world);
    return 0;
}

static int breeding_invalid_and_extinct(void) {
    uint second, third;
    World *world = breeding_world(&second, &third);
    Cell *cell = &world->zones[0].new[1][1];
    cell->species_id = 1; cell->parent = TOP;
    const World original_world = *world;
    Genome original_genomes[4];
    memcpy(original_genomes, world->genomes, sizeof original_genomes);
    random_reset(1);
    CHECK(auto_breed_plants(NULL, 0, 2) == -1);
    CHECK(auto_breed_plants(world, 1, 2) == -1);
    CHECK(auto_breed_plants(world, 0, 0) == -1);
    CHECK(auto_breed_plants(world, 0, 1) == -1);
    CHECK(auto_breed_plants(world, 0, 3) == -1);
    CHECK(auto_breed_plants(world, 0, 4) == -1);
    CHECK(auto_breed_plants(world, 0, UINT_MAX) == -1);
    CHECK(auto_breed_plants(world, 0, 2) == -1); /* Older biomass cannot revive the latest cohort. */
    const short invalid_ids[] = {0, -1, 4};
    for (uint i = 0; i < sizeof invalid_ids / sizeof invalid_ids[0]; i++) {
        cell->species_id = invalid_ids[i];
        CHECK(auto_breed_plants(world, 0, 2) == -1);
        CHECK(cell->species_id == invalid_ids[i]);
    }
    cell->species_id = 1;
    CHECK(memcmp(world, &original_world, sizeof *world) == 0);
    CHECK(memcmp(world->genomes, original_genomes, sizeof original_genomes) == 0);
    CHECK(draw_position == 0);
    dispose_world(world);
    return 0;
}

static int random_stored_template_expansion(void) {
    uint second, third;
    World *world = breeding_world(&second, &third);
    const Genome before = world->genomes[1];
    use_real_random = 1; srand(1729);
    create_random_species(world, &world->genomes[1], 120);
    CHECK(world->max_species_id > 100);
    CHECK(memcmp(&world->genomes[1], &before, sizeof before) == 0);
    for (uint i = 4; i < world->max_species_id; i++) {
        CHECK(world->genomes[i].genome_id == i);
        CHECK(world->genomes[i].parents[0] == 0 && world->genomes[i].parents[1] == 0);
        CHECK(world->plantae[i].stats.population == 0);
    }
    dispose_world(world);
    return 0;
}

int main(int argc, char **argv) {
    static const TestCase cases[] = {
        {"mutation_multiples", mutation_multiples},
        {"inheritance_copied_traits", inheritance_copied_traits},
        {"roulette_ticket_enumeration", roulette_ticket_enumeration},
        {"roulette_wide_totals", roulette_wide_totals},
        {"roulette_invalid_inputs", roulette_invalid_inputs},
        {"breeding_mixed_parentage", breeding_mixed_parentage},
        {"breeding_invalid_and_extinct", breeding_invalid_and_extinct},
        {"random_stored_template_expansion", random_stored_template_expansion}
    };
    return test_main(argc, argv, cases, sizeof cases / sizeof cases[0]);
}

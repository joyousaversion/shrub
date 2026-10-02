#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shrub.h"
#include "perlin.h"
#include "test_runner.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); return 1; \
} } while (0)

extern void tick_flowers(CA *, uint, uint);

typedef struct Fixture { World *world; CA *ca; uint species; } Fixture;

static Fixture fixture(unsigned seed, byte nutrient_max, int growing) {
    /* Warm the lazy Perlin initializer before reseeding each independent run. */
    double origin[2] = {0, 0};
    (void)noise2(origin);
    srand(seed);
    perlin_init();
    Experiment experiment = {0};
    Genome genome = {0};
    init_sample_run(&experiment, &genome);
    for (uint tissue = 0; tissue < TC; tissue++) {
        genome.initial_flux[tissue] = 100;
        genome.flux_cost[tissue] = 0;
        genome.max_offspring[tissue] = growing && tissue != ROOT ? 2 : 0;
        genome.reproduction_bias[tissue] = 0xffff;
        experiment.photosynthetic[tissue] = 0;
        for (uint nutrient = 0; nutrient < NC; nutrient++) {
            genome.max[nutrient][tissue] = nutrient_max;
            genome.min[nutrient][tissue] = 0;
            genome.transfer_bias[nutrient][tissue] = 0xffff;
            experiment.repro_cost[nutrient][tissue] = growing && nutrient == HUMMUS ? 1 : 0;
        }
        for (uint axis = 0; axis < THC; axis++) {
            genome.evolve_type[axis][tissue] = NONE;
            genome.evolve_threshold[axis][tissue] = 100;
        }
    }
    if (growing) {
        genome.evolve_type[LONGEST_DISTANCE_TO_TIP][LEAF] = BRANCH;
        genome.evolve_threshold[LONGEST_DISTANCE_TO_TIP][LEAF] = 2;
        genome.evolve_type[LONGEST_DISTANCE_TO_TIP][SHOOT] = TRUNK;
        genome.evolve_threshold[LONGEST_DISTANCE_TO_TIP][SHOOT] = 3;
    }
    Fixture f;
    f.world = create_world(&experiment);
    f.species = create_species(f.world, &genome);
    f.ca = &f.world->zones[alloc_CA(f.world, 10, 9)];
    f.world->plantae[f.species].max_crowding = 8;
    f.ca->rain_level = 0;
    for (uint y = 0; y < f.ca->ys; y++)
        for (uint x = 0; x < f.ca->xs; x++)
            f.ca->new[y][x].substrate_type = f.ca->old[y][x].substrate_type = y < 7 ? AIR : GROUND;
    return f;
}

static void dispose(Fixture *f) {
    for (uint zone = 0; zone < f->world->zone_count; zone++) {
        CA *ca = &f->world->zones[zone];
        for (uint y = 0; y < ca->ys; y++)
            for (uint x = 0; x < ca->xs; x++) {
                Item *item = ca->new[y][x].items;
                while (item) { Item *next = item->next; free(item); item = next; }
                item = ca->old[y][x].items;
                while (item) { Item *next = item->next; free(item); item = next; }
            }
    }
    for (uint species = 1; species < f->world->max_species_id; species++) {
        free(f->world->plantae[species].tissues);
        free(f->world->plantae[species].plant_ids);
    }
    dealloc_world(f->world);
    free(f->world);
}

static uint nutrient_total(Fixture *f, Nutrient nutrient) {
    uint result = 0;
    for (uint y = 0; y < f->ca->ys; y++)
        for (uint x = 0; x < f->ca->xs; x++) {
            Cell *cell = &f->ca->new[y][x];
            if (cell->parent != SELF) result += cell->nutrients[nutrient] + cell->escrow[nutrient];
        }
    return result;
}

static int verify_graph(Fixture *f, uint plant_id) {
    const Neighbor reverse[9] = {SELF, BOTTOM, BOTTOM_LEFT, LEFT, TOP_LEFT,
                                 TOP, TOP_RIGHT, RIGHT, BOTTOM_RIGHT};
    for (uint y = 0; y < f->ca->ys; y++)
        for (uint x = 0; x < f->ca->xs; x++) {
            Cell *cell = &f->ca->new[y][x];
            if (cell->parent == SELF) continue;
            CHECK(cell->species_id == f->species && cell->plant_id == plant_id);
            CHECK(get_tissue(f->ca, cell->species_id, cell->tissue_id) != NULL);
            CHECK(cell->parent >= TOP && cell->parent <= TOP_LEFT);
            Cell *parent = neighbor(f->ca, cell->parent, y, x);
            CHECK(parent != NULL && parent->parent != SELF);
            CHECK(parent->species_id == cell->species_id && parent->plant_id == cell->plant_id);
            CHECK(parent->offspring[0] == reverse[cell->parent] || parent->offspring[1] == reverse[cell->parent]);
            for (uint slot = 0; slot < 2; slot++) {
                Neighbor direction = cell->offspring[slot];
                if (direction == SELF || direction == NEW) continue;
                CHECK(direction >= TOP && direction <= TOP_LEFT);
                Cell *child = neighbor(f->ca, direction, y, x);
                CHECK(child != NULL && child->parent == reverse[direction]);
                CHECK(child->species_id == cell->species_id && child->plant_id == cell->plant_id);
            }
            CHECK((cell->offspring[0] == NEW) == (cell->offspring[1] == NEW));
        }
    return 0;
}

static uint64_t hash_integer(uint64_t hash, uint64_t value) {
    for (uint byte = 0; byte < 8; byte++) {
        hash ^= value & 255u;
        hash *= UINT64_C(1099511628211);
        value >>= 8;
    }
    return hash;
}

/* Hash semantic fields, excluding pointers, padding, and wall-clock durations. */
static uint64_t state_hash(Fixture *f) {
    uint64_t hash = UINT64_C(14695981039346656037);
    hash = hash_integer(hash, f->ca->tick);
    hash = hash_integer(hash, f->world->max_plant_id);
    hash = hash_integer(hash, f->world->max_species_id);
    for (uint y = 0; y < f->ca->ys; y++)
        for (uint x = 0; x < f->ca->xs; x++) {
            Cell *cell = &f->ca->new[y][x];
            hash = hash_integer(hash, cell->species_id);
            hash = hash_integer(hash, cell->plant_id);
            hash = hash_integer(hash, cell->tissue_id);
            hash = hash_integer(hash, cell->parent);
            hash = hash_integer(hash, cell->offspring[0]);
            hash = hash_integer(hash, cell->offspring[1]);
            hash = hash_integer(hash, cell->circ);
            hash = hash_integer(hash, cell->resource);
            hash = hash_integer(hash, cell->water);
            hash = hash_integer(hash, cell->substrate_type);
            hash = hash_integer(hash, cell->damage);
            uint32_t flux;
            memcpy(&flux, &cell->flux, sizeof flux);
            hash = hash_integer(hash, flux);
            for (uint nutrient = 0; nutrient < NC; nutrient++) {
                hash = hash_integer(hash, cell->nutrients[nutrient]);
                hash = hash_integer(hash, cell->escrow[nutrient]);
            }
            for (Item *item = cell->items; item; item = item->next) {
                hash = hash_integer(hash, item->type);
                hash = hash_integer(hash, item->item.species_id);
            }
            hash = hash_integer(hash, UINT64_MAX); /* End of this cell's item list. */
        }
    return hash;
}

static int inherited_seed_start(Fixture *f) {
    uint mother = f->species;
    Genome paternal = f->world->genomes[mother];
    for (uint tissue = 0; tissue < TC; tissue++) paternal.transfer_bias[PHOTO][tissue] ^= 0xffff;
    uint father = create_species(f->world, &paternal);
    CHECK(father != mother);
    Genome saved_mother = f->world->genomes[mother], saved_father = f->world->genomes[father];
    CHECK(plant_seed(f->ca, mother, 7, 4) == 0);
    CHECK(plant_seed(f->ca, father, 7, 1) == 0);
    Cell *female = &f->ca->new[6][4], *male = &f->ca->new[6][1];
    female->tissue_id = FLOWER_FEMALE;
    male->tissue_id = FLOWER_MALE;
    for (uint nutrient = 0; nutrient < NC; nutrient++) male->nutrients[nutrient] = 3;
    for (uint y = 6; y <= 7; y++) {
        f->ca->old[y][4] = f->ca->new[y][4];
        f->ca->old[y][1] = f->ca->new[y][1];
    }
    for (uint attempt = 0; attempt < 16 && male->items == NULL; attempt++) tick_flowers(f->ca, 6, 1);
    CHECK(male->items && male->items->type == POLLEN && male->items->item.species_id == father);
    for (uint nutrient = 0; nutrient < NC; nutrient++) CHECK(male->nutrients[nutrient] == 2);
    /* Deliver the emitted pollen; wind transport is outside this biological fixture. */
    female->items = male->items;
    male->items = NULL;
    tick_flowers(f->ca, 6, 4);
    CHECK(female->reap && female->items == NULL);
    uint child = female->species_id;
    CHECK(child != mother && child != father && child < f->world->max_species_id);
    Genome *inherited = &f->world->genomes[child];
    CHECK((inherited->parents[0] == mother && inherited->parents[1] == father) ||
          (inherited->parents[0] == father && inherited->parents[1] == mother));
    CHECK(memcmp(&saved_mother, &f->world->genomes[mother], sizeof saved_mother) == 0);
    CHECK(memcmp(&saved_father, &f->world->genomes[father], sizeof saved_father) == 0);
    kill_cell(f->ca, 6, 4);
    Item *seed = female->items;
    CHECK(seed && seed->type == SEED && seed->item.species_id == child && seed->next == NULL);
    female->items = NULL;
    uint zone = alloc_CA(f->world, 10, 9);
    f->ca = &f->world->zones[zone];
    f->species = child;
    f->world->plantae[child].max_crowding = 8;
    for (uint y = 0; y < f->ca->ys; y++)
        for (uint x = 0; x < f->ca->xs; x++)
            f->ca->new[y][x].substrate_type = f->ca->old[y][x].substrate_type = y < 7 ? AIR : GROUND;
    f->ca->new[6][4].items = seed;
    return 0;
}

static int lifecycle_run(uint64_t hashes[12], uint *last_biomass) {
    Fixture f = fixture(1729, 255, 1);
    CHECK(inherited_seed_start(&f) == 0);
    Genome carried_genome = f.world->genomes[f.species];
    uint plant_id = f.world->max_plant_id, previous = 2;
    uint differentiated = 0;
    for (uint tick = 0; tick < 12; tick++) {
        tick_CA(f.ca);
        CHECK(f.ca->tick == tick + 1 && total_items(f.ca) == 0);
        CHECK(f.world->plantae[f.species].count == 1 && f.world->max_plant_id == plant_id + 1);
        uint biomass = total_biomass(f.ca);
        CHECK(biomass >= previous && biomass <= 12);
        CHECK(nutrient_total(&f, HUMMUS) + biomass == 12);
        CHECK(nutrient_total(&f, PHOTO) == 0 && nutrient_total(&f, WATER) == 0);
        CHECK(verify_graph(&f, plant_id) == 0);
        CHECK(memcmp(&carried_genome, &f.world->genomes[f.species], sizeof carried_genome) == 0);
        hashes[tick] = state_hash(&f);
        previous = biomass;
        for (uint y = 0; y < f.ca->ys; y++)
            for (uint x = 0; x < f.ca->xs; x++)
                differentiated += f.ca->new[y][x].tissue_id == BRANCH || f.ca->new[y][x].tissue_id == TRUNK;
    }
    CHECK(previous > 2 && differentiated > 0);
    *last_biomass = previous;
    dispose(&f); return 0;
}

static int tick_seeded_lifecycle(void) {
    uint64_t first[12], second[12];
    uint first_biomass, second_biomass;
    CHECK(lifecycle_run(first, &first_biomass) == 0);
    CHECK(lifecycle_run(second, &second_biomass) == 0);
    CHECK(first_biomass == second_biomass && memcmp(first, second, sizeof first) == 0);
    CHECK(first[0] != first[11]);
    printf("seed1729: biomass=%u snapshot=%016llx\n", first_biomass, (unsigned long long)first[11]);
    return 0;
}

static int tick_escrow_carry(void) {
    Fixture f = fixture(1729, 255, 0);
    CHECK(plant_seed(f.ca, f.species, 7, 4) == 0);
    for (uint tissue = LEAF; tissue <= ROOT; tissue++)
        for (uint nutrient = 0; nutrient < NC; nutrient++)
            get_tissue(f.ca, f.species, tissue)->min[nutrient] = 255;
    Cell *root = &f.ca->new[7][4];
    root->nutrients[PHOTO] = 250; root->escrow[PHOTO] = 10;
    root->nutrients[HUMMUS] = 255; root->escrow[HUMMUS] = 255;
    root->nutrients[WATER] = 255; root->escrow[WATER] = 17;
    uint balances[NC];
    for (uint nutrient = 0; nutrient < NC; nutrient++) balances[nutrient] = nutrient_total(&f, nutrient);
    for (uint tick = 0; tick < 2; tick++) {
        tick_CA(f.ca);
        root = &f.ca->new[7][4];
        CHECK(root->nutrients[PHOTO] == 255 && root->escrow[PHOTO] == 5);
        CHECK(root->nutrients[HUMMUS] == 255 && root->escrow[HUMMUS] == 255);
        CHECK(root->nutrients[WATER] == 255 && root->escrow[WATER] == 17);
        for (uint nutrient = 0; nutrient < NC; nutrient++) CHECK(nutrient_total(&f, nutrient) == balances[nutrient]);
    }
    root->nutrients[PHOTO] -= 10;
    tick_CA(f.ca);
    root = &f.ca->new[7][4];
    CHECK(root->nutrients[PHOTO] == 250 && root->escrow[PHOTO] == 0);
    CHECK(nutrient_total(&f, PHOTO) == balances[PHOTO] - 10);
    CHECK(total_biomass(f.ca) == 2);
    dispose(&f); return 0;
}

static int tick_saturated_transfer(void) {
    Fixture f = fixture(1729, 3, 0);
    CHECK(plant_seed(f.ca, f.species, 7, 4) == 0);
    Cell *root = &f.ca->new[7][4], *shoot = &f.ca->new[6][4];
    shoot->nutrients[HUMMUS] = 3;
    /* Seed roots start with ten units even when their configured maximum is three. */
    CHECK(root->nutrients[HUMMUS] == 10);
    tick_CA(f.ca);
    root = &f.ca->new[7][4]; shoot = &f.ca->new[6][4];
    CHECK(root->flux == 100 && shoot->flux == 100);
    CHECK(root->nutrients[HUMMUS] == 10 && shoot->nutrients[HUMMUS] == 3);
    CHECK(nutrient_total(&f, HUMMUS) == 13);
    dispose(&f); return 0;
}

static const TestCase tests[] = {
    {"tick_seeded_lifecycle", tick_seeded_lifecycle},
    {"tick_escrow_carry", tick_escrow_carry},
    {"tick_saturated_transfer", tick_saturated_transfer}
};

int main(int argc, char **argv) {
    return test_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "genome.h"
#include "roulette.h"
#include "test_runner.h"

extern short unsigned int crossover(unsigned short, unsigned short, unsigned short *, unsigned short *);
extern void tick_flowers(CA *, uint, uint);
extern int tick_reproduction(CA *);
extern uint longest_dist_to_sprout(CA *, uint, uint);
extern void unmark_cell(CA *, uint, uint);
extern void kill_cell(CA *, uint, uint);

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); return 1; \
} } while (0)

extern int __real_rand(void);
static int random_values[4096];
static uint random_count, random_position;
static int random_default = 1, use_real_random;

int __wrap_rand(void) {
    if (use_real_random) return __real_rand();
    if (random_position < random_count) return random_values[random_position++];
    return random_default;
}

static void scripted_random(int value) {
    random_count = random_position = 0;
    random_default = value;
    use_real_random = 0;
}

static void crossover_draws(int cut, int first_mutation, int second_mutation) {
    random_values[random_count++] = cut;
    for (int bit = 0; bit < 16; bit++)
        random_values[random_count++] = bit == first_mutation ? 0 : 1;
    for (int bit = 0; bit < 16; bit++)
        random_values[random_count++] = bit == second_mutation ? 0 : 1;
}

typedef struct Fixture { World *world; CA *ca; uint species; } Fixture;

static Fixture fixture(uint rows, uint columns) {
    Experiment experiment = {0};
    Genome genome = {0};
    init_sample_run(&experiment, &genome);
    Fixture f;
    f.world = create_world(&experiment);
    f.species = create_species(f.world, &genome);
    f.ca = &f.world->zones[alloc_CA(f.world, rows, columns)];
    for (uint y = 0; y < rows; y++)
        for (uint x = 0; x < columns; x++)
            f.ca->new[y][x].substrate_type = f.ca->old[y][x].substrate_type = AIR;
    return f;
}

/* The legacy deallocator does not own species allocations or the World itself. */
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
    for (uint i = 1; i < f->world->max_species_id; i++) {
        free(f->world->plantae[i].tissues);
        free(f->world->plantae[i].plant_ids);
    }
    dealloc_world(f->world);
    free(f->world);
}

static Cell *module(Fixture *f, uint y, uint x, TissueId tissue, Neighbor parent) {
    Cell *cell = &f->ca->new[y][x];
    cell->species_id = f->species;
    cell->tissue_id = tissue;
    cell->parent = parent;
    cell->plant_id = 42;
    cell->flux = 100;
    return cell;
}

static Cell *growth_fixture(Fixture *f) {
    *f = fixture(5, 5);
    Cell *shoot = module(f, 2, 2, SHOOT, BOTTOM);
    Cell *root = module(f, 3, 2, ROOT, TOP);
    shoot->circ = root->circ = 1;
    shoot->offspring[0] = BOTTOM;
    root->offspring[0] = TOP;
    shoot->nutrients[PHOTO] = shoot->nutrients[HUMMUS] = 10;
    shoot->nutrients[WATER] = 10;
    f->world->plantae[f->species].tissues[ROOT].max_offspring = 0;
    f->world->plantae[f->species].tissues[LEAF].max_offspring = 0;
    return shoot;
}

static int initialization(void) {
    Experiment a, b;
    Genome ga, gb;
    memset(&a, 0xa5, sizeof a); memset(&b, 0x5a, sizeof b);
    memset(&ga, 0xa5, sizeof ga); memset(&gb, 0x5a, sizeof gb);
    init_sample_run(&a, &ga); init_sample_run(&b, &gb);
    CHECK(memcmp(&a, &b, sizeof a) == 0);
    CHECK(memcmp(&ga, &gb, sizeof ga) == 0);
    CHECK(a.photosynthetic[LEAF] == 1 && a.photosynthetic[SHOOT] == 1);
    CHECK(a.photosynthetic[ROOT] == 0 && ga.parents[0] == 0);
    return 0;
}

static int phenotype_mapping(void) {
    Experiment experiment = {0}, original_experiment;
    Genome genome = {0}, original_genome;
    Tissue tissues[TC];
    Species species = {.tissues = tissues};
    memset(tissues, 0xa5, sizeof tissues);
    for (int t = 0; t < TC; t++) {
        for (int n = 0; n < NC; n++) {
            experiment.repro_cost[n][t] = 1 + n * TC + t;
            genome.transfer_bias[n][t] = 1000 + n * TC + t;
            genome.max[n][t] = 70 + n * TC + t;
            genome.min[n][t] = n * TC + t;
        }
        for (int axis = 0; axis < THC; axis++) {
            genome.evolve_type[axis][t] = (t + axis) % (ROOT + 1);
            genome.evolve_threshold[axis][t] = 10 + axis * TC + t;
        }
        genome.reproduction_bias[t] = 500 + t;
        genome.max_offspring[t] = t % 3;
        genome.flux_cost[t] = t + 0.25f;
        genome.initial_flux[t] = t + 0.5f;
        experiment.max_damage[t] = 30 + t;
        experiment.fg_color[t] = 100 + t;
        experiment.bg_color[t] = 110 + t;
        experiment.visible[t] = t % 2;
        experiment.photosynthetic[t] = (t + 1) % 2;
        experiment.decay_rate[t] = 1000 + t;
        experiment.offspring_type[t] = (t + 1) % (ROOT + 1);
    }
    original_genome = genome; original_experiment = experiment;
    apply_genome(&experiment, &genome, &species);
    for (int t = 0; t < TC; t++) {
        for (int n = 0; n < NC; n++) {
            CHECK(tissues[t].transfer_bias[n] == genome.transfer_bias[n][t]);
            CHECK(tissues[t].repro_cost[n] == experiment.repro_cost[n][t]);
            CHECK(tissues[t].min[n] == genome.min[n][t]);
            CHECK(tissues[t].max[n] == genome.max[n][t]);
        }
        for (int axis = 0; axis < THC; axis++) {
            CHECK(tissues[t].evolve_type[axis] == genome.evolve_type[axis][t]);
            CHECK(tissues[t].evolve_threshold[axis] == genome.evolve_threshold[axis][t]);
        }
        CHECK(tissues[t].reproduction_bias == genome.reproduction_bias[t]);
        CHECK(tissues[t].max_offspring == genome.max_offspring[t]);
        CHECK(tissues[t].flux_cost == genome.flux_cost[t]);
        CHECK(tissues[t].initial_flux == genome.initial_flux[t]);
        CHECK(tissues[t].max_damage == experiment.max_damage[t]);
        CHECK(tissues[t].fg_color == experiment.fg_color[t]);
        CHECK(tissues[t].bg_color == experiment.bg_color[t]);
        CHECK(tissues[t].visible == experiment.visible[t]);
        CHECK(tissues[t].photosynthetic == experiment.photosynthetic[t]);
        CHECK(tissues[t].decay_rate == experiment.decay_rate[t]);
        CHECK(tissues[t].offspring_type == experiment.offspring_type[t]);
    }
    CHECK(memcmp(&genome, &original_genome, sizeof genome) == 0);
    CHECK(memcmp(&experiment, &original_experiment, sizeof experiment) == 0);
    return 0;
}

static int crossover_cuts(void) {
    for (int cut = 0; cut < 16; cut++) {
        unsigned short first, second;
        scripted_random(1); crossover_draws(cut, -1, -1);
        crossover(0xffff, 0, &first, &second);
        CHECK(first == (1u << cut) - 1);
        CHECK((unsigned short)(first ^ second) == 0xffff);
        CHECK((first & second) == 0);
    }
    return 0;
}

static int crossover_mutations(void) {
    for (int bit = 0; bit < 16; bit++) {
        unsigned short first, second;
        scripted_random(1); crossover_draws(8, bit, -1);
        crossover(0, 0, &first, &second);
        CHECK(first == (1u << bit) && second == 0);
        scripted_random(1); crossover_draws(8, -1, bit);
        crossover(0, 0, &first, &second);
        CHECK(first == 0 && second == (1u << bit));
    }
    return 0;
}

static int inheritance(void) {
    Genome mother = {0}, father = {0}, first, second, copy_mother, copy_father;
    mother.genome_id = 11; father.genome_id = 22;
    mother.min[PHOTO][LEAF] = 3; father.min[PHOTO][LEAF] = 4;
    for (int t = 0; t < TC; t++) {
        mother.reproduction_bias[t] = 0xffff;
        for (int n = 0; n < NC; n++) mother.transfer_bias[n][t] = 0xffff;
        for (int axis = 0; axis < THC; axis++) mother.evolve_threshold[axis][t] = 0xff;
    }
    copy_mother = mother; copy_father = father;
    scripted_random(1);
    for (int i = 0; i < TC * (THC + NC + 1); i++) crossover_draws(4, -1, -1);
    combine_genomes(&mother, &father, &first, &second);
    for (int t = 0; t < TC; t++) {
        CHECK(first.reproduction_bias[t] == 0x000f);
        CHECK(second.reproduction_bias[t] == 0xfff0);
        for (int n = 0; n < NC; n++) {
            CHECK(first.transfer_bias[n][t] == 0x000f);
            CHECK(second.transfer_bias[n][t] == 0xfff0);
        }
        for (int axis = 0; axis < THC; axis++) {
            if ((axis == 0 && t == FLOWER_MALE) || (axis == 1 && t == FLOWER_FEMALE)) continue;
            CHECK(first.evolve_threshold[axis][t] == 0x0f);
            CHECK(second.evolve_threshold[axis][t] == 0xf0);
        }
    }
    CHECK(first.evolve_threshold[0][FLOWER_MALE] == 100 && second.evolve_threshold[0][FLOWER_MALE] == 100);
    CHECK(first.evolve_threshold[1][FLOWER_FEMALE] == 100 && second.evolve_threshold[1][FLOWER_FEMALE] == 100);
    CHECK(first.parents[0] == 11 && first.parents[1] == 22);
    CHECK(second.parents[0] == 22 && second.parents[1] == 11);
    CHECK(first.genome_id == 0 && second.genome_id == 0);
    CHECK(first.min[PHOTO][LEAF] == 3 && second.min[PHOTO][LEAF] == 4);
    CHECK(memcmp(&mother, &copy_mother, sizeof mother) == 0);
    CHECK(memcmp(&father, &copy_father, sizeof father) == 0);
    return 0;
}

static int species_identity(void) {
    Fixture f = fixture(3, 3);
    Genome genome = f.world->genomes[f.species];
    strcpy(genome.name, "renamed"); strcpy(genome.remarks, "annotation");
    genome.parents[0] = 100; genome.genome_id = 900;
    CHECK(create_species(f.world, &genome) == (int)f.species);
    genome.min_seed_nutrients[WATER] = 3;
    CHECK(create_species(f.world, &genome) != (int)f.species);
    CHECK(f.world->genomes[f.species].genome_id == f.species);
    dispose(&f); return 0;
}

static int random_species(void) {
    Fixture f = fixture(3, 3);
    Genome template = f.world->genomes[f.species], before = template;
    use_real_random = 1; srand(1729);
    create_random_species(f.world, &template, 120);
    CHECK(memcmp(&template, &before, sizeof template) == 0);
    CHECK(f.world->max_species_id > 100);
    for (uint i = 1; i < f.world->max_species_id; i++) {
        CHECK(f.world->genomes[i].genome_id == i);
        CHECK(f.world->plantae[i].stats.population == 0);
        for (int t = 0; t < TC; t++) {
            CHECK(f.world->genomes[i].max_offspring[t] <= 2);
            for (int n = 0; n < NC; n++) CHECK(f.world->genomes[i].min[n][t] <= f.world->genomes[i].max[n][t]);
        }
    }
    dispose(&f); return 0;
}

static int roulette_edges(void) {
    uint weights[3] = {0, 1, 3}, winners[4] = {99, 99, 99, 99};
    scripted_random(0); random_values[0] = 0; random_values[1] = 1;
    random_values[2] = 2; random_values[3] = 3; random_count = 4;
    CHECK(roulette(weights, winners, 4, 3) == 0);
    CHECK(winners[0] == 1 && winners[1] == 2 && winners[2] == 2 && winners[3] == 2);
    weights[1] = weights[2] = 0;
    CHECK(roulette(weights, winners, 1, 3) == -1);
    CHECK(roulette(weights, winners, 1, 0) == -1);
    return 0;
}

static int breeding(void) {
    Fixture f = fixture(4, 4);
    Genome genome = f.world->genomes[f.species];
    genome.reproduction_bias[LEAF] = 0xaaaa;
    uint second_parent = create_species(f.world, &genome);
    genome.reproduction_bias[LEAF] = 0x5555;
    uint third_parent = create_species(f.world, &genome);
    Cell *cell = module(&f, 2, 2, LEAF, BOTTOM);
    cell->species_id = third_parent;
    cell = module(&f, 3, 2, ROOT, TOP); cell->species_id = third_parent;
    uint before = f.world->max_species_id;
    CHECK(auto_breed_plants(f.world, 0, 1) == -1);
    CHECK(auto_breed_plants(f.world, 1, 2) == -1);
    CHECK(auto_breed_plants(f.world, 0, before) == -1);
    CHECK(f.world->max_species_id == before);
    scripted_random(0); random_values[0] = 0; random_values[1] = 0; random_count = 2;
    random_default = 1024; /* Mutation guarantees a new genotype. */
    CHECK(auto_breed_plants(f.world, 0, 2) == 0);
    CHECK(f.world->max_species_id > before);
    for (uint i = before; i < f.world->max_species_id; i++) {
        CHECK(f.world->genomes[i].parents[0] == third_parent);
        CHECK(f.world->genomes[i].parents[1] == third_parent);
        CHECK(f.world->genomes[i].parents[0] != second_parent);
    }
    memset(f.ca->new[2], 0, sizeof(Cell) * f.ca->xs);
    memset(f.ca->new[3], 0, sizeof(Cell) * f.ca->xs);
    CHECK(auto_breed_plants(f.world, 0, 2) == -1);
    dispose(&f); return 0;
}

static int growth_cost(void) {
    Fixture f; Cell *shoot = growth_fixture(&f);
    Tissue *parent = get_tissue(f.ca, f.species, SHOOT), *child = get_tissue(f.ca, f.species, LEAF);
    parent->reproduction_bias = 3;
    parent->repro_cost[PHOTO] = 5; parent->repro_cost[HUMMUS] = 7;
    child->repro_cost[PHOTO] = child->repro_cost[HUMMUS] = 1;
    child->repro_cost[WATER] = 2;
    shoot->nutrients[PHOTO] = shoot->nutrients[HUMMUS] = 1;
    shoot->nutrients[WATER] = 2;
    tick_reproduction(f.ca);
    CHECK(shoot->nutrients[PHOTO] == 0 && shoot->nutrients[HUMMUS] == 0 && shoot->nutrients[WATER] == 0);
    Cell *born = &f.ca->new[1][2];
    CHECK(born->parent == BOTTOM && shoot->offspring[1] == TOP);
    CHECK(born->tissue_id == LEAF && born->species_id == shoot->species_id);
    CHECK(born->plant_id == shoot->plant_id && born->flux == child->initial_flux);
    dispose(&f); return 0;
}

static int growth_gate(int mode) {
        Fixture f; Cell *shoot = growth_fixture(&f);
        Tissue *t = get_tissue(f.ca, f.species, SHOOT);
        t->reproduction_bias = 3;
        if (mode == 0) shoot->nutrients[PHOTO] = 0;
        if (mode == 1) shoot->nutrients[HUMMUS] = 0;
        if (mode == 2) { get_tissue(f.ca, f.species, LEAF)->repro_cost[WATER] = 1; shoot->nutrients[WATER] = 0; }
        if (mode == 3) t->max_offspring = 1;
        if (mode == 4) t->max_offspring = 0;
        if (mode == 5) f.world->plantae[f.species].max_crowding = 0;
        if (mode == 6) f.ca->new[1][2].substrate_type = GROUND;
        if (mode == 7) f.ca->new[1][2].floaty = 1;
        uint before = total_biomass(f.ca);
        byte nutrients[NC];
        memcpy(nutrients, shoot->nutrients, sizeof nutrients);
        tick_reproduction(f.ca);
        CHECK(total_biomass(f.ca) == (int)before);
        CHECK(memcmp(nutrients, shoot->nutrients, sizeof nutrients) == 0);
        dispose(&f);
    return 0;
}
/* Keep the historical case name for the WATER regression; isolate other gates. */
static int growth_gates(void) { return growth_gate(2); }
static int growth_photo_gate(void) { return growth_gate(0); }
static int growth_hummus_gate(void) { return growth_gate(1); }
static int growth_one_offspring_gate(void) { return growth_gate(3); }
static int growth_disabled_gate(void) { return growth_gate(4); }
static int growth_crowding_gate(void) { return growth_gate(5); }
static int growth_substrate_gate(void) { return growth_gate(6); }
static int growth_floaty_gate(void) { return growth_gate(7); }

static int growth_zero_cost(void) {
    Fixture f; Cell *shoot = growth_fixture(&f);
    get_tissue(f.ca, f.species, SHOOT)->reproduction_bias = 3;
    Tissue *child = get_tissue(f.ca, f.species, LEAF);
    memset(child->repro_cost, 0, sizeof child->repro_cost);
    memset(shoot->nutrients, 0, sizeof shoot->nutrients);
    CHECK(tick_reproduction(f.ca) == 0);
    CHECK(total_biomass(f.ca) == 3);
    CHECK(shoot->offspring[1] == TOP && f.ca->new[1][2].parent == BOTTOM);
    for (int n = 0; n < NC; n++) CHECK(shoot->nutrients[n] == 0);
    dispose(&f); return 0;
}

static int growth_selection(void) {
    Fixture f; Cell *shoot = growth_fixture(&f);
    Tissue *t = get_tissue(f.ca, f.species, SHOOT);
    t->reproduction_bias = 0xffff;
    f.ca->new[1][2].resource = 5;
    f.ca->new[2][3].resource = 4;
    f.ca->new[1][3].resource = 7; f.ca->new[1][3].floaty = 1;
    tick_reproduction(f.ca);
    CHECK(shoot->offspring[1] == TOP && f.ca->new[1][2].parent == BOTTOM);
    dispose(&f); return 0;
}

static int growth_weighted_ties(void) {
    uint counts[2] = {0};
    for (int ticket = 0; ticket < 4; ticket++) {
        Fixture f; Cell *shoot = growth_fixture(&f);
        Tissue *t = get_tissue(f.ca, f.species, SHOOT);
        t->reproduction_bias = 1 | (3 << ((RIGHT - 1) * 2));
        scripted_random(ticket);
        tick_reproduction(f.ca);
        CHECK(shoot->offspring[1] == TOP || shoot->offspring[1] == RIGHT);
        counts[shoot->offspring[1] == RIGHT]++;
        dispose(&f);
    }
    CHECK(counts[0] == 1 && counts[1] == 3);
    return 0;
}

static int growth_eight_neighbors(void) {
    Fixture f = fixture(3, 3);
    Cell *shoot = module(&f, 1, 1, SHOOT, BOTTOM);
    shoot->nutrients[PHOTO] = shoot->nutrients[HUMMUS] = 2;
    get_tissue(f.ca, f.species, LEAF)->max_offspring = 0;
    tick_reproduction(f.ca);
    CHECK(total_biomass(f.ca) == 2);
    dispose(&f); return 0;
}

static int newborn_and_escrow(void) {
    Fixture f; Cell *shoot = growth_fixture(&f);
    get_tissue(f.ca, f.species, SHOOT)->reproduction_bias = 3 << ((RIGHT - 1) * 2);
    Tissue *leaf = get_tissue(f.ca, f.species, LEAF);
    leaf->max_offspring = 2;
    leaf->repro_cost[PHOTO] = leaf->repro_cost[HUMMUS] = 0;
    shoot->nutrients[PHOTO] = 8;
    shoot->escrow[PHOTO] = 2;
    tick_reproduction(f.ca);
    CHECK(total_biomass(f.ca) == 3);
    CHECK(shoot->escrow[PHOTO] == 0 && shoot->nutrients[PHOTO] == 10);
    Cell *child = &f.ca->new[2][3];
    CHECK(child->offspring[0] == NEW && child->offspring[1] == NEW);
    unmark_cell(f.ca, 2, 3);
    CHECK(child->offspring[0] == SELF && child->offspring[1] == SELF);
    dispose(&f); return 0;
}

static int planting(void) {
    Fixture f = fixture(4, 4);
    f.ca->new[2][2].substrate_type = f.ca->old[2][2].substrate_type = GROUND;
    CHECK(plant_seed(f.ca, f.species, 2, 2) == 0);
    Cell *root = &f.ca->new[2][2], *shoot = &f.ca->new[1][2];
    CHECK(root->parent == TOP && shoot->parent == BOTTOM);
    CHECK(root->offspring[0] == TOP && shoot->offspring[0] == BOTTOM);
    CHECK(root->circ && shoot->circ && root->plant_id == shoot->plant_id);
    CHECK(root->tissue_id == ROOT && shoot->tissue_id == SHOOT);
    CHECK(root->species_id == f.species && shoot->species_id == f.species);
    CHECK(plant_seed(f.ca, f.species, 2, 2) == -1);
    CHECK(f.world->plantae[f.species].count == 1);
    CHECK(plant_seed(f.ca, 0, 2, 1) == -1);
    CHECK(plant_seed(f.ca, f.world->max_species_id, 2, 1) == -1);
    CHECK(plant_seed(f.ca, f.species, 4, 1) == -1);
    f.ca->new[0][1].substrate_type = f.ca->old[0][1].substrate_type = GROUND;
    CHECK(plant_seed(f.ca, f.species, 0, 1) == -1);
    dispose(&f); return 0;
}

static int germination(void) {
    Fixture f = fixture(5, 5);
    f.ca->new[3][2].substrate_type = f.ca->old[3][2].substrate_type = GROUND;
    Item seed = {.type = SEED}; seed.item.species_id = f.species;
    create_item(f.ca, 2, 2, &seed);
    tick_germinate(f.ca, 2, 2);
    CHECK(f.ca->new[2][2].items == NULL && total_biomass(f.ca) == 2);
    seed.item.species_id = 0;
    create_item(f.ca, 2, 1, &seed);
    f.ca->new[3][1].substrate_type = f.ca->old[3][1].substrate_type = GROUND;
    tick_germinate(f.ca, 2, 1);
    CHECK(f.ca->new[2][1].items != NULL && total_biomass(f.ca) == 2);
    dispose(&f); return 0;
}

static void chain(Fixture *f) {
    *f = fixture(6, 5);
    Cell *root = module(f, 4, 2, ROOT, TOP), *shoot = module(f, 3, 2, SHOOT, BOTTOM);
    Cell *branch = module(f, 2, 2, BRANCH, BOTTOM), *tip = module(f, 1, 2, LEAF, BOTTOM);
    root->circ = shoot->circ = 1;
    root->offspring[0] = TOP; shoot->offspring[0] = BOTTOM;
    shoot->offspring[1] = TOP; branch->offspring[0] = TOP;
    tip->offspring[0] = tip->offspring[1] = SELF;
}

static int differentiation_distances(void) {
    Fixture f; chain(&f);
    CHECK(longest_dist_to_edge(f.ca, 1, 2) == 1);
    CHECK(longest_dist_to_edge(f.ca, 2, 2) == 2);
    CHECK(longest_dist_to_edge(f.ca, 3, 2) == 3);
    CHECK(longest_dist_to_sprout(f.ca, 3, 2) == 0);
    CHECK(longest_dist_to_sprout(f.ca, 2, 2) == 1);
    CHECK(longest_dist_to_sprout(f.ca, 1, 2) == 2);
    f.ca->new[3][2].offspring[1] = SELF;
    CHECK(longest_dist_to_edge(f.ca, 3, 2) == 1);
    dispose(&f); return 0;
}

static int differentiation_thresholds(void) {
    for (int axis = 0; axis < THC; axis++)
        for (int threshold = 0; threshold <= 3; threshold++) {
            Fixture f; chain(&f);
            for (int t = 1; t <= ROOT; t++) {
                Tissue *tissue = get_tissue(f.ca, f.species, t);
                tissue->evolve_type[0] = tissue->evolve_type[1] = NONE;
            }
            Tissue *leaf = get_tissue(f.ca, f.species, LEAF);
            leaf->evolve_type[axis] = FLOWER_FEMALE;
            leaf->evolve_threshold[axis] = threshold;
            transform(f.ca);
            int fires = axis == 0 ? 1 >= threshold : 2 > threshold;
            CHECK(f.ca->new[1][2].tissue_id == (fires ? FLOWER_FEMALE : LEAF));
            CHECK(f.ca->new[1][2].plant_id == 42 && f.ca->new[1][2].species_id == f.species);
            CHECK(f.ca->new[1][2].parent == BOTTOM);
            dispose(&f);
        }
    return 0;
}

static int differentiation_precedence(void) {
    Fixture f; chain(&f);
    for (int t = 1; t <= ROOT; t++) {
        Tissue *tissue = get_tissue(f.ca, f.species, t);
        tissue->evolve_type[0] = tissue->evolve_type[1] = NONE;
    }
    Tissue *leaf = get_tissue(f.ca, f.species, LEAF);
    leaf->evolve_type[0] = SHOOT; leaf->evolve_type[1] = FLOWER_MALE;
    leaf->evolve_threshold[0] = leaf->evolve_threshold[1] = 0;
    transform(f.ca);
    CHECK(f.ca->new[1][2].tissue_id == FLOWER_MALE);
    CHECK(f.ca->new[2][2].tissue_id == BRANCH);
    dispose(&f); return 0;
}

static int differentiation_invalid_paths(void) {
    Fixture f = fixture(3, 3);
    Cell *cell = module(&f, 0, 1, LEAF, TOP);
    CHECK(longest_dist_to_sprout(f.ca, 0, 1) == 0);
    cell->offspring[0] = TOP;
    CHECK(longest_dist_to_edge(f.ca, 0, 1) == 0);
    cell->parent = RIGHT; module(&f, 0, 2, LEAF, LEFT);
    CHECK(longest_dist_to_sprout(f.ca, 0, 1) == 0);
    cell->offspring[0] = RIGHT; f.ca->new[0][2].offspring[0] = LEFT;
    CHECK(longest_dist_to_edge(f.ca, 0, 1) == 0);
    CHECK(get_tissue(f.ca, f.world->max_species_id, LEAF) == NULL);
    CHECK(get_tissue(f.ca, USHRT_MAX, LEAF) == NULL);
    dispose(&f); return 0;
}

static const TestCase tests[] = {
    {"initialization", initialization}, {"phenotype_mapping", phenotype_mapping},
    {"crossover_cuts", crossover_cuts}, {"crossover_mutations", crossover_mutations},
    {"inheritance", inheritance}, {"species_identity", species_identity},
    {"random_species", random_species}, {"roulette_edges", roulette_edges},
    {"breeding", breeding}, {"growth_cost", growth_cost}, {"growth_gates", growth_gates},
    {"growth_photo_gate", growth_photo_gate}, {"growth_hummus_gate", growth_hummus_gate},
    {"growth_one_offspring_gate", growth_one_offspring_gate},
    {"growth_disabled_gate", growth_disabled_gate}, {"growth_crowding_gate", growth_crowding_gate},
    {"growth_substrate_gate", growth_substrate_gate}, {"growth_floaty_gate", growth_floaty_gate},
    {"growth_zero_cost", growth_zero_cost},
    {"growth_selection", growth_selection}, {"growth_weighted_ties", growth_weighted_ties},
    {"growth_eight_neighbors", growth_eight_neighbors}, {"newborn_and_escrow", newborn_and_escrow},
    {"planting", planting}, {"germination", germination},
    {"differentiation_distances", differentiation_distances},
    {"differentiation_thresholds", differentiation_thresholds},
    {"differentiation_precedence", differentiation_precedence},
    {"differentiation_invalid_paths", differentiation_invalid_paths}
};

int main(int argc, char **argv) {
    return test_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}

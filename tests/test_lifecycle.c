#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "genome.h"
#include "test_runner.h"

extern void tick_flowers(CA *, uint, uint);
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #c); return 1; } } while (0)

static int draw = 1;
static int selected_child = -1, calls;
int __wrap_rand(void) {
    if (selected_child >= 0 && calls++ == TC * (THC + NC + 1) * 33)
        return selected_child;
    return draw;
}

typedef struct { World *world; CA *ca; int mother, father; } Fixture;
static Fixture fixture(void) {
    Experiment experiment;
    Genome genome;
    init_sample_run(&experiment, &genome);
    Fixture f;
    f.world = create_world(&experiment);
    f.mother = create_species(f.world, &genome);
    genome.reproduction_bias[LEAF] ^= 0xffff;
    genome.transfer_bias[WATER][ROOT] ^= 0xffff;
    f.father = create_species(f.world, &genome);
    f.ca = &f.world->zones[alloc_CA(f.world, 5, 5)];
    Cell *flower = &f.ca->new[2][2];
    flower->parent = BOTTOM;
    flower->species_id = f.mother;
    flower->tissue_id = FLOWER_FEMALE;
    flower->plant_id = 1;
    Cell *parent = &f.ca->old[3][2];
    parent->parent = BOTTOM;
    parent->species_id = f.mother;
    parent->tissue_id = SHOOT;
    parent->plant_id = 1;
    draw = 1;
    selected_child = -1;
    calls = 0;
    return f;
}
static void dispose(Fixture *f) {
    for (uint y = 0; y < f->ca->ys; y++) for (uint x = 0; x < f->ca->xs; x++) {
        Item *item = f->ca->new[y][x].items;
        while (item) { Item *next = item->next; free(item); item = next; }
        item = f->ca->old[y][x].items;
        while (item) { Item *next = item->next; free(item); item = next; }
    }
    for (uint i = 1; i < f->world->max_species_id; i++) {
        free(f->world->plantae[i].tissues);
        free(f->world->plantae[i].plant_ids);
    }
    dealloc_world(f->world);
    free(f->world);
}
static void pollen(Fixture *f, int sid) {
    Item item = {0};
    item.type = POLLEN;
    item.item.species_id = sid;
    create_item(f->ca, 2, 2, &item);
}
static int pollination_inheritance(void) {
    for (int child = 0; child < 2; child++) {
        Fixture f = fixture();
        Genome first, second;
        combine_genomes(&f.world->genomes[f.mother], &f.world->genomes[f.father], &first, &second);
        Genome *expected = child ? &first : &second;
        pollen(&f, f.father);
        selected_child = child;
        calls = 0;
        tick_flowers(f.ca, 2, 2);
        Cell *flower = &f.ca->new[2][2];
        CHECK(flower->items == NULL && flower->reap);
        CHECK(flower->species_id > 0 && (uint)flower->species_id < f.world->max_species_id);
        Genome *result = &f.world->genomes[flower->species_id];
        CHECK(result->parents[0] == expected->parents[0] && result->parents[1] == expected->parents[1]);
        CHECK(memcmp(result->transfer_bias, expected->transfer_bias, sizeof first.transfer_bias) == 0);
        CHECK(memcmp(result->reproduction_bias, expected->reproduction_bias, sizeof first.reproduction_bias) == 0);
        CHECK(flower->parent == BOTTOM && flower->plant_id == 1 && flower->tissue_id == FLOWER_FEMALE);
        dispose(&f);
    }
    return 0;
}
static int identical_pollination(void) {
    Fixture f = fixture();
    pollen(&f, f.mother);
    tick_flowers(f.ca, 2, 2);
    Cell *flower = &f.ca->new[2][2];
    CHECK(flower->species_id == f.mother && flower->items == NULL && flower->reap);
    CHECK(f.world->max_species_id == 3);
    dispose(&f);
    return 0;
}
static int invalid_pollination(void) {
    int bad_ids[] = {-1, 0, INT_MAX, 3};
    for (size_t i = 0; i < sizeof bad_ids / sizeof bad_ids[0]; i++) {
        Fixture f = fixture();
        pollen(&f, bad_ids[i]);
        Cell before = f.ca->new[2][2];
        tick_flowers(f.ca, 2, 2);
        CHECK(memcmp(&before, &f.ca->new[2][2], sizeof before) == 0);
        dispose(&f);
    }
    Fixture f = fixture();
    pollen(&f, f.father);
    f.ca->new[2][2].parent = 255;
    Cell before = f.ca->new[2][2];
    tick_flowers(f.ca, 2, 2);
    CHECK(memcmp(&before, &f.ca->new[2][2], sizeof before) == 0);
    dispose(&f);
    return 0;
}
static int fertilized_seed(void) {
    Fixture f = fixture();
    pollen(&f, f.father);
    tick_flowers(f.ca, 2, 2);
    Cell *flower = &f.ca->new[2][2];
    int child_sid = flower->species_id;
    for (int n = 0; n < NC; n++) flower->nutrients[n] = n + 2;
    kill_cell(f.ca, 2, 2);
    Item *seed = flower->items;
    CHECK(seed && seed->next == NULL && seed->type == SEED);
    CHECK(seed->item.species_id == child_sid && seed->decay_rate == 0);
    for (int n = 0; n < NC; n++) CHECK(seed->nutrients[n] == n + 2 && flower->nutrients[n] == 0);
    CHECK(flower->parent == SELF && flower->species_id == 0);
    dispose(&f);
    return 0;
}
static int unfertilized_fragment(void) {
    Fixture f = fixture();
    kill_cell(f.ca, 2, 2);
    CHECK(f.ca->new[2][2].items && f.ca->new[2][2].items->type == FRAGMENT);
    dispose(&f);
    return 0;
}
static int male_pollen_cost(void) {
    Fixture f = fixture();
    Cell *male = &f.ca->new[2][2];
    male->tissue_id = FLOWER_MALE;
    for (int n = 0; n < NC; n++) male->nutrients[n] = 3;
    tick_flowers(f.ca, 2, 2);
    CHECK(male->items && male->items->type == POLLEN && male->items->item.species_id == f.mother);
    for (int n = 0; n < NC; n++) CHECK(male->nutrients[n] == 2 && male->items->nutrients[n] == 1);
    tick_flowers(f.ca, 2, 2);
    for (int n = 0; n < NC; n++) CHECK(male->nutrients[n] == 2);
    CHECK(male->items->next == NULL);
    dispose(&f);
    return 0;
}
static int male_pollen_gates(void) {
    for (int absent = 0; absent <= NC; absent++) {
        Fixture f = fixture();
        Cell *male = &f.ca->new[2][2];
        male->tissue_id = FLOWER_MALE;
        for (int n = 0; n < NC; n++) male->nutrients[n] = n == absent ? 0 : 3;
        draw = absent == NC ? 0 : 1;
        Cell before = *male;
        tick_flowers(f.ca, 2, 2);
        CHECK(memcmp(&before, male, sizeof before) == 0);
        dispose(&f);
    }
    return 0;
}
int main(int argc, char **argv) {
    const TestCase cases[] = {
        {"pollination_inheritance", pollination_inheritance},
        {"identical_pollination", identical_pollination},
        {"invalid_pollination", invalid_pollination},
        {"fertilized_seed", fertilized_seed},
        {"unfertilized_fragment", unfertilized_fragment},
        {"male_pollen_cost", male_pollen_cost},
        {"male_pollen_gates", male_pollen_gates}
    };
    return test_main(argc, argv, cases, sizeof cases / sizeof cases[0]);
}

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shrub.h"
#include "test_runner.h"

extern void allocate_nutrients(const uint *, uint[3], uint, uint[3]);

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); return 1; \
} } while (0)

static int random_draw = 1;
int __wrap_rand(void) { return random_draw; }

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

static void dispose(Fixture *f) {
    for (uint y = 0; y < f->ca->ys; y++)
        for (uint x = 0; x < f->ca->xs; x++) {
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

static Cell *module(Fixture *f, uint y, uint x, TissueId tissue, Neighbor parent) {
    Cell *cell = &f->ca->new[y][x];
    cell->species_id = f->species;
    cell->tissue_id = tissue;
    cell->parent = parent;
    cell->plant_id = 42;
    cell->flux = 100;
    for (int n = 0; n < NC; n++) cell->nutrients[n] = 20;
    return cell;
}

static void ground(Fixture *f, uint y, uint x) {
    f->ca->new[y][x].substrate_type = f->ca->old[y][x].substrate_type = GROUND;
}

static int growth_escrow_capacity(void) {
    Fixture f = fixture(5, 5);
    Cell *cell = module(&f, 3, 2, ROOT, TOP);
    get_tissue(f.ca, f.species, ROOT)->max_offspring = 0;
    cell->nutrients[PHOTO] = 250; cell->escrow[PHOTO] = 10;
    cell->nutrients[HUMMUS] = 0; cell->escrow[HUMMUS] = 20;
    cell->nutrients[WATER] = 255; cell->escrow[WATER] = 255;
    uint balances[NC];
    for (int n = 0; n < NC; n++) balances[n] = cell->nutrients[n] + cell->escrow[n];
    CHECK(tick_reproduction(f.ca) == 0);
    for (int n = 0; n < NC; n++)
        CHECK(cell->nutrients[n] + cell->escrow[n] == balances[n]);
    CHECK(cell->nutrients[PHOTO] == 255 && cell->escrow[PHOTO] == 5);
    CHECK(cell->nutrients[HUMMUS] == 20 && cell->escrow[HUMMUS] == 0);
    CHECK(cell->nutrients[WATER] == 255 && cell->escrow[WATER] == 255);
    Cell before = *cell;
    CHECK(tick_reproduction(f.ca) == 0);
    CHECK(memcmp(cell, &before, sizeof before) == 0);
    for (int n = 0; n < NC; n++) cell->nutrients[n] = 0;
    CHECK(tick_reproduction(f.ca) == 0);
    CHECK(cell->nutrients[PHOTO] == 5 && cell->nutrients[WATER] == 255);
    for (int n = 0; n < NC; n++) CHECK(cell->escrow[n] == 0);
    dispose(&f); return 0;
}

static int growth_direction_tickets(void) {
    const uint weights[9] = {0, 0, 1, 2, 3, 0, 3, 2, 1};
    const int dy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};
    const int dx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
    uint counts[9] = {0}, tickets = 0;
    unsigned short bias = 0;
    for (uint n = TOP; n <= TOP_LEFT; n++) {
        bias |= weights[n] << ((n - 1) * 2);
        tickets += weights[n];
    }
    for (uint ticket = 0; ticket < tickets; ticket++) {
        Fixture f = fixture(5, 5);
        /* Disconnected parent deliberately leaves all eight directions available. */
        Cell *parent = module(&f, 2, 2, SHOOT, TOP);
        get_tissue(f.ca, f.species, SHOOT)->reproduction_bias = bias;
        get_tissue(f.ca, f.species, LEAF)->max_offspring = 0;
        for (uint n = TOP; n <= TOP_LEFT; n++)
            f.ca->new[2 + dy[n]][2 + dx[n]].resource = weights[n] ? 3 : 15;
        random_draw = ticket;
        CHECK(tick_reproduction(f.ca) == 0);
        Neighbor selected = parent->offspring[0];
        CHECK(selected >= TOP && selected <= TOP_LEFT && weights[selected] != 0);
        counts[selected]++;
        Cell *child = &f.ca->new[2 + dy[selected]][2 + dx[selected]];
        CHECK(child->tissue_id == LEAF && child->species_id == parent->species_id);
        CHECK(child->plant_id == parent->plant_id);
        CHECK(neighbor(f.ca, child->parent, 2 + dy[selected], 2 + dx[selected]) == parent);
        CHECK(child->offspring[0] == NEW && child->offspring[1] == NEW);
        dispose(&f);
    }
    for (uint n = TOP; n <= TOP_LEFT; n++) CHECK(counts[n] == weights[n]);
    random_draw = 1;
    return 0;
}

static int growth_richest_eligible(void) {
    Fixture f = fixture(5, 5);
    Cell *parent = module(&f, 2, 2, SHOOT, BOTTOM);
    Cell *root = module(&f, 3, 2, ROOT, TOP);
    parent->offspring[0] = BOTTOM; root->offspring[0] = TOP;
    parent->circ = root->circ = 1;
    get_tissue(f.ca, f.species, ROOT)->max_offspring = 0;
    get_tissue(f.ca, f.species, LEAF)->max_offspring = 0;
    get_tissue(f.ca, f.species, SHOOT)->reproduction_bias = 0x3fff;
    f.ca->new[1][2].floaty = 1; f.ca->new[1][2].resource = 15;
    f.ca->new[1][3].substrate_type = GROUND; f.ca->new[1][3].resource = 15;
    f.ca->new[2][3].parent = TOP; f.ca->new[2][3].resource = 15;
    f.ca->new[3][3].offspring[0] = LEFT; f.ca->new[3][3].resource = 15;
    f.ca->new[1][1].resource = 15; /* Zero directional weight. */
    f.ca->new[2][1].resource = 10; f.ca->new[3][1].resource = 9;
    CHECK(tick_reproduction(f.ca) == 0);
    CHECK(parent->offspring[1] == LEFT && f.ca->new[2][1].parent == RIGHT);
    CHECK(f.ca->new[1][2].parent == SELF && f.ca->new[1][3].parent == SELF);
    CHECK(f.ca->new[3][3].parent == SELF && f.ca->new[3][3].offspring[0] == LEFT);
    dispose(&f); return 0;
}

static int planting_occupancy(void) {
    for (uint board = 0; board < 2; board++)
        for (uint slot = 0; slot < 2; slot++)
            for (uint kind = 0; kind < 3; kind++) {
                Fixture f = fixture(5, 5);
                ground(&f, 3, 2);
                Cell *cells[] = {&f.ca->new[3][2], &f.ca->new[2][2],
                                 &f.ca->old[3][2], &f.ca->old[2][2]};
                Cell *blocked = cells[2 * board + slot];
                if (kind == 0) blocked->parent = RIGHT;
                if (kind == 1) blocked->offspring[0] = RIGHT;
                if (kind == 2) blocked->floaty = 1;
                Cell saved[4];
                for (uint i = 0; i < 4; i++) saved[i] = *cells[i];
                uint plant_id = f.world->max_plant_id;
                CHECK(plant_seed(f.ca, f.species, 3, 2) == -1);
                CHECK(f.world->max_plant_id == plant_id && f.world->plantae[f.species].count == 0);
                for (uint i = 0; i < 4; i++) CHECK(memcmp(cells[i], &saved[i], sizeof saved[i]) == 0);
                dispose(&f);
            }
    return 0;
}

static int planting_ids_and_storage(void) {
    Fixture f = fixture(3, 25);
    uint first_id = f.world->max_plant_id;
    for (uint x = 0; x < f.ca->xs; x++) {
        ground(&f, 2, x);
        CHECK(plant_seed(f.ca, f.species, 2, x) == 0);
        CHECK(f.ca->new[2][x].plant_id == first_id + x && f.ca->new[1][x].plant_id == first_id + x);
        CHECK(f.world->plantae[f.species].plant_ids[x] == first_id + x);
        CHECK(f.world->plantae[f.species].count <= f.world->plantae[f.species].allocd);
    }
    CHECK(f.world->plantae[f.species].count == 25 && f.world->max_plant_id == first_id + 25);
    dispose(&f);

    f = fixture(3, 5);
    ground(&f, 2, 0); ground(&f, 2, 1);
    CHECK(plant_seed(f.ca, f.species, UINT_MAX, 0) == -1);
    CHECK(plant_seed(f.ca, f.species, 2, UINT_MAX) == -1);
    uint actual_species_bound = f.world->max_species_id;
    f.world->max_species_id = SHRT_MAX + 2u;
    CHECK(plant_seed(f.ca, SHRT_MAX + 1u, 2, 0) == -1);
    f.world->max_species_id = actual_species_bound;
    f.world->max_plant_id = USHRT_MAX;
    CHECK(plant_seed(f.ca, f.species, 2, 0) == 0);
    CHECK(f.ca->new[2][0].plant_id == USHRT_MAX && f.ca->new[1][0].plant_id == USHRT_MAX);
    CHECK(f.world->max_plant_id == USHRT_MAX + 1u);
    CHECK(plant_seed(f.ca, f.species, 2, 1) == -1);
    CHECK(f.world->max_plant_id == USHRT_MAX + 1u && f.ca->new[2][1].parent == SELF);
    dispose(&f); return 0;
}

static int planting_preserves_cell_state(void) {
    Fixture f = fixture(4, 4);
    ground(&f, 2, 2);
    Cell *root = &f.ca->new[2][2], *shoot = &f.ca->new[1][2];
    root->resource = 11; root->water = 3; root->wind[0] = 1.75;
    shoot->resource = 4; shoot->wind[1] = -2.25;
    root->nutrients[WATER] = 100; shoot->escrow[PHOTO] = 80;
    Item mote = {.type = FLOATY, .shape = '*'};
    create_item(f.ca, 1, 2, &mote);
    Item *item = shoot->items;
    CHECK(plant_seed(f.ca, f.species, 2, 2) == 0);
    CHECK(root->resource == 11 && root->water == 3 && root->wind[0] == 1.75);
    CHECK(shoot->resource == 4 && shoot->wind[1] == -2.25 && shoot->items == item);
    CHECK(root->nutrients[WATER] == 0 && shoot->escrow[PHOTO] == 0);
    dispose(&f); return 0;
}

static int germination_neighbors(void) {
    const int dy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};
    const int dx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
    for (uint board = 0; board < 2; board++)
        for (uint n = SELF; n <= TOP_LEFT; n++) {
            Fixture f = fixture(6, 5);
            ground(&f, 3, 2);
            Item seed = {.type = SEED}; seed.item.species_id = f.species;
            create_item(f.ca, 2, 2, &seed);
            Item *item = f.ca->new[2][2].items;
            Cell **cells = board ? f.ca->new : f.ca->old;
            cells[3 + dy[n]][2 + dx[n]].parent = RIGHT;
            uint plant_id = f.world->max_plant_id;
            tick_germinate(f.ca, 2, 2);
            CHECK(f.ca->new[2][2].items == item && total_items(f.ca) == 1);
            CHECK(f.world->plantae[f.species].count == 0 && f.world->max_plant_id == plant_id);
            dispose(&f);
        }
    return 0;
}

static int germination_item_list(void) {
    Fixture f = fixture(4, 5);
    ground(&f, 3, 2);
    Item mote = {.type = FLOATY, .shape = '*'};
    Item seed = {.type = SEED}; seed.item.species_id = f.species;
    Item pollen = {.type = POLLEN, .shape = '.'};
    create_item(f.ca, 2, 2, &mote); create_item(f.ca, 2, 2, &seed);
    create_item(f.ca, 2, 2, &pollen);
    Item *first = f.ca->new[2][2].items, *last = first->next->next;
    tick_germinate(f.ca, 2, 2);
    CHECK(f.world->plantae[f.species].count == 1);
    CHECK(f.ca->new[2][2].items == first && first->next == last && last->next == NULL);
    CHECK(first->type == FLOATY && last->type == POLLEN && total_items(f.ca) == 2);
    CHECK(f.ca->new[3][2].parent == TOP && f.ca->new[2][2].parent == BOTTOM);
    dispose(&f); return 0;
}

static int germination_invalid_seed(void) {
    for (uint kind = 0; kind < 3; kind++) {
        Fixture f = fixture(4, 5);
        ground(&f, 3, 2);
        Item seed = {.type = SEED};
        seed.item.species_id = kind == 0 ? 0 : kind == 1 ? f.world->max_species_id : -1;
        create_item(f.ca, 2, 2, &seed);
        Item *item = f.ca->new[2][2].items;
        tick_germinate(f.ca, 2, 2);
        CHECK(f.ca->new[2][2].items == item && f.ca->new[2][2].parent == SELF);
        CHECK(f.ca->new[3][2].parent == SELF && f.world->plantae[f.species].count == 0);
        dispose(&f);
    }
    return 0;
}

static int nutrient_allocation_tickets(void) {
    const uint maxes[3] = {10, 10, 10};
    uint weights[3] = {3, 3, 3}, counts[3] = {0};
    for (uint ticket = 0; ticket < 9; ticket++) {
        uint outs[3] = {0};
        random_draw = ticket;
        allocate_nutrients(maxes, weights, 1, outs);
        CHECK(outs[0] + outs[1] + outs[2] == 1);
        for (uint recipient = 0; recipient < 3; recipient++) counts[recipient] += outs[recipient];
    }
    for (uint recipient = 0; recipient < 3; recipient++) CHECK(counts[recipient] == 3);
    memset(counts, 0, sizeof counts);
    weights[0] = 2; weights[1] = 0; weights[2] = 3;
    for (uint ticket = 0; ticket < 5; ticket++) {
        uint outs[3] = {0};
        random_draw = ticket;
        allocate_nutrients(maxes, weights, 1, outs);
        CHECK(outs[1] == 0 && outs[0] + outs[2] == 1);
        for (uint recipient = 0; recipient < 3; recipient++) counts[recipient] += outs[recipient];
    }
    CHECK(counts[0] == 2 && counts[1] == 0 && counts[2] == 3);
    random_draw = 1;
    return 0;
}

static int nutrient_allocation_capacity(void) {
    const uint maxes[3] = {1, 2, 0};
    uint weights[3] = {3, 3, 3}, original_weights[3], outs[3] = {0};
    memcpy(original_weights, weights, sizeof weights);
    random_draw = 0;
    allocate_nutrients(maxes, weights, 10, outs);
    CHECK(outs[0] == 1 && outs[1] == 2 && outs[2] == 0);
    CHECK(memcmp(weights, original_weights, sizeof weights) == 0);
    weights[0] = weights[1] = weights[2] = 0;
    allocate_nutrients(maxes, weights, 1, outs);
    CHECK(outs[0] == 0 && outs[1] == 0 && outs[2] == 0);
    random_draw = 1;
    return 0;
}

static int nutrient_invalid_neighbor(void) {
    Fixture f = fixture(3, 5);
    Cell *cell = module(&f, 0, 2, SHOOT, TOP);
    Cell before = *cell;
    redistribute_nutrients(f.ca);
    CHECK(memcmp(cell, &before, sizeof before) == 0);
    dispose(&f); return 0;
}

static int nutrient_duplicate_links(void) {
    Fixture f = fixture(3, 5);
    Cell *source = module(&f, 1, 2, SHOOT, RIGHT);
    Cell *target = module(&f, 1, 3, ROOT, LEFT);
    source->offspring[0] = source->offspring[1] = RIGHT;
    for (uint nutrient = 0; nutrient < NC; nutrient++) source->nutrients[nutrient] = target->nutrients[nutrient] = 0;
    source->nutrients[HUMMUS] = 10;
    target->nutrients[HUMMUS] = target->escrow[HUMMUS] = 1;
    get_tissue(f.ca, f.species, ROOT)->max[HUMMUS] = 3;
    get_tissue(f.ca, f.species, ROOT)->min[HUMMUS] = 3;
    redistribute_nutrients(f.ca);
    CHECK(source->nutrients[HUMMUS] == 9);
    CHECK(target->nutrients[HUMMUS] == 1 && target->escrow[HUMMUS] == 2);
    CHECK(target->flux == 101 && source->flux == 100);
    dispose(&f); return 0;
}

static const TestCase tests[] = {
    {"growth_escrow_capacity", growth_escrow_capacity},
    {"growth_direction_tickets", growth_direction_tickets},
    {"growth_richest_eligible", growth_richest_eligible},
    {"planting_occupancy", planting_occupancy},
    {"planting_ids_and_storage", planting_ids_and_storage},
    {"planting_preserves_cell_state", planting_preserves_cell_state},
    {"germination_neighbors", germination_neighbors},
    {"germination_item_list", germination_item_list},
    {"germination_invalid_seed", germination_invalid_seed},
    {"nutrient_allocation_tickets", nutrient_allocation_tickets},
    {"nutrient_allocation_capacity", nutrient_allocation_capacity},
    {"nutrient_invalid_neighbor", nutrient_invalid_neighbor},
    {"nutrient_duplicate_links", nutrient_duplicate_links}
};

int main(int argc, char **argv) {
    return test_main(argc, argv, tests, sizeof tests / sizeof tests[0]);
}

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shrub.h"
#include "test_runner.h"

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); return 1; \
} } while (0)

typedef struct Fixture {
    World world;
    CA ca;
    Species species[2];
    Tissue tissues[TC];
} Fixture;

static void fixture_init(Fixture *f, uint rows, uint columns) {
    memset(f, 0, sizeof *f);
    f->world.plantae = f->species;
    f->world.max_species_id = 2;
    f->species[1].tissues = f->tissues;
    f->ca.world = &f->world;
    f->ca.ys = rows;
    f->ca.xs = columns;
    f->ca.new = calloc(rows, sizeof *f->ca.new);
    f->ca.old = calloc(rows, sizeof *f->ca.old);
    if (!f->ca.new || !f->ca.old) abort();
    for (uint y = 0; y < rows; y++) {
        f->ca.new[y] = calloc(columns, sizeof *f->ca.new[y]);
        f->ca.old[y] = calloc(columns, sizeof *f->ca.old[y]);
        if (!f->ca.new[y] || !f->ca.old[y]) abort();
    }
}

static void fixture_dispose(Fixture *f) {
    for (uint y = 0; y < f->ca.ys; y++) {
        free(f->ca.new[y]);
        free(f->ca.old[y]);
    }
    free(f->ca.new);
    free(f->ca.old);
}

static Cell *module(Fixture *f, uint y, uint x, TissueId tissue, Neighbor parent) {
    Cell *cell = &f->ca.new[y][x];
    cell->species_id = 1;
    cell->tissue_id = tissue;
    cell->parent = parent;
    cell->plant_id = 42;
    return cell;
}

static int direction_helpers(void) {
    Fixture f; fixture_init(&f, 3, 3);
    const int dy[9] = {0,-1,-1,0,1,1,1,0,-1};
    const int dx[9] = {0,0,1,1,1,0,-1,-1,-1};
    for (int direction = SELF; direction <= TOP_LEFT; direction++) {
        CHECK(neighbor(&f.ca, direction, 1, 1) == &f.ca.new[1+dy[direction]][1+dx[direction]]);
        CHECK(o_neighbor(&f.ca, direction, 1, 1) == &f.ca.old[1+dy[direction]][1+dx[direction]]);
        for (uint quantity = 0; quantity <= 3; quantity++) {
            uint encoded = set_shares_by_direction(quantity, direction);
            if (direction == SELF) {
                CHECK(encoded == 0);
                CHECK(get_shares_by_direction(encoded, direction) == 1);
            } else {
                CHECK(get_shares_by_direction(encoded, direction) == quantity);
                for (int other = TOP; other <= TOP_LEFT; other++)
                    if (other != direction) CHECK(get_shares_by_direction(encoded, other) == 0);
            }
        }
        CHECK(set_shares_by_direction(4, direction) == 0);
    }
    const int invalid[] = {-1, NEW, NEW+1, 255, INT_MAX};
    for (uint i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
        CHECK(neighbor(&f.ca, invalid[i], 1, 1) == NULL);
        CHECK(o_neighbor(&f.ca, invalid[i], 1, 1) == NULL);
        CHECK(get_shares_by_direction(0xffff, invalid[i]) == 0);
        CHECK(set_shares_by_direction(3, invalid[i]) == 0);
    }
    CHECK(neighbor(&f.ca, TOP, 0, 0) == NULL);
    CHECK(neighbor(&f.ca, LEFT, 0, 0) == NULL);
    CHECK(neighbor(&f.ca, SELF, UINT_MAX, 0) == NULL);
    CHECK(o_neighbor(&f.ca, SELF, 0, UINT_MAX) == NULL);
    CHECK(neighbor(NULL, SELF, 0, 0) == NULL);
    CHECK(o_neighbor(NULL, SELF, 0, 0) == NULL);
    fixture_dispose(&f); return 0;
}

static int tissue_bounds(void) {
    Fixture f; fixture_init(&f, 1, 1);
    for (int tissue = LEAF; tissue <= ROOT; tissue++)
        CHECK(get_tissue(&f.ca, 1, tissue) == &f.tissues[tissue]);
    CHECK(get_tissue(NULL, 1, LEAF) == NULL);
    CHECK(get_tissue(&f.ca, 0, LEAF) == NULL);
    CHECK(get_tissue(&f.ca, 2, LEAF) == NULL);
    CHECK(get_tissue(&f.ca, USHRT_MAX, LEAF) == NULL);
    CHECK(get_tissue(&f.ca, 1, NONE) == NULL);
    CHECK(get_tissue(&f.ca, 1, -1) == NULL);
    CHECK(get_tissue(&f.ca, 1, ROOT+1) == NULL);
    CHECK(get_tissue(&f.ca, 1, TC) == NULL);
    f.species[1].tissues = NULL;
    CHECK(get_tissue(&f.ca, 1, LEAF) == NULL);
    f.ca.world = NULL;
    CHECK(get_tissue(&f.ca, 1, LEAF) == NULL);
    fixture_dispose(&f); return 0;
}

static int branching_distances(void) {
    Fixture f; fixture_init(&f, 6, 5);
    Cell *shoot = module(&f, 3, 2, SHOOT, BOTTOM);
    Cell *root = module(&f, 4, 2, ROOT, TOP);
    shoot->circ = root->circ = 1;
    shoot->offspring[0] = BOTTOM; root->offspring[0] = TOP;
    root->offspring[1] = BOTTOM;
    module(&f, 5, 2, ROOT, TOP);
    /* A longer subtree on the other seed half does not lengthen this half. */
    CHECK(longest_dist_to_edge(&f.ca, 3, 2) == 1);
    CHECK(longest_dist_to_edge(&f.ca, 4, 2) == 2);
    CHECK(longest_dist_to_sprout(&f.ca, 3, 2) == 0);
    CHECK(longest_dist_to_sprout(&f.ca, 4, 2) == 0);
    Cell *branch = module(&f, 2, 2, BRANCH, BOTTOM);
    module(&f, 1, 2, LEAF, BOTTOM);
    module(&f, 2, 3, LEAF, LEFT);
    shoot->offspring[1] = TOP;
    branch->offspring[0] = TOP; branch->offspring[1] = RIGHT;
    CHECK(longest_dist_to_edge(&f.ca, 2, 2) == 2);
    CHECK(longest_dist_to_edge(&f.ca, 3, 2) == 3);
    CHECK(longest_dist_to_sprout(&f.ca, 2, 3) == 2);
    CHECK(longest_dist_to_sprout(&f.ca, 5, 2) == 1);
    fixture_dispose(&f); return 0;
}

static int newborn_distance_markers(void) {
    Fixture f; fixture_init(&f, 2, 1);
    Cell *cell = module(&f, 0, 0, LEAF, BOTTOM);
    cell->offspring[0] = cell->offspring[1] = NEW;
    CHECK(longest_dist_to_edge(&f.ca, 0, 0) == 1);
    cell->offspring[0] = SELF;
    CHECK(longest_dist_to_edge(&f.ca, 0, 0) == 1);
    cell->circ = 1; cell->offspring[0] = BOTTOM;
    CHECK(longest_dist_to_edge(&f.ca, 0, 0) == 1);
    CHECK(longest_dist_to_sprout(&f.ca, 0, 0) == 0);
    cell->circ = 0; cell->parent = NEW;
    CHECK(longest_dist_to_sprout(&f.ca, 0, 0) == 0);
    fixture_dispose(&f); return 0;
}

static int malformed_distances(void) {
    Fixture f; fixture_init(&f, 3, 3);
    CHECK(longest_dist_to_edge(&f.ca, 1, 1) == 0);
    CHECK(longest_dist_to_sprout(&f.ca, 1, 1) == 0);
    Cell *center = module(&f, 1, 1, LEAF, 255);
    CHECK(longest_dist_to_sprout(&f.ca, 1, 1) == 0);
    center->offspring[0] = 255;
    CHECK(longest_dist_to_edge(&f.ca, 1, 1) == 0);
    center->circ = 1;
    CHECK(longest_dist_to_edge(&f.ca, 1, 1) == 0);
    center->circ = 0;
    Cell *child = module(&f, 0, 1, LEAF, BOTTOM);
    center->offspring[0] = TOP; center->offspring[1] = 255;
    CHECK(longest_dist_to_edge(&f.ca, 1, 1) == 0);
    center->offspring[1] = SELF; child->plant_id = 43;
    CHECK(longest_dist_to_edge(&f.ca, 1, 1) == 0);
    center->parent = TOP; child->circ = 1;
    CHECK(longest_dist_to_sprout(&f.ca, 1, 1) == 0);
    child->plant_id = 42; child->species_id = 2;
    CHECK(longest_dist_to_edge(&f.ca, 1, 1) == 0);
    CHECK(longest_dist_to_sprout(&f.ca, 1, 1) == 0);
    child->species_id = 1; child->circ = 0;
    child->offspring[0] = BOTTOM;
    CHECK(longest_dist_to_edge(&f.ca, 1, 1) == 0);
    CHECK(longest_dist_to_sprout(&f.ca, 1, 1) == 0);
    CHECK(longest_dist_to_edge(NULL, 0, 0) == 0);
    CHECK(longest_dist_to_sprout(NULL, 0, 0) == 0);
    CHECK(longest_dist_to_edge(&f.ca, UINT_MAX, 0) == 0);
    CHECK(longest_dist_to_sprout(&f.ca, 0, UINT_MAX) == 0);
    fixture_dispose(&f); return 0;
}

static int deep_chain_distances(void) {
    const uint length = 20000;
    Fixture f; fixture_init(&f, length, 1);
    for (uint y = 0; y < length; y++) {
        Cell *cell = module(&f, y, 0, SHOOT, y == 0 ? BOTTOM : TOP);
        if (y+1 < length) cell->offspring[y == 0 ? 1 : 0] = BOTTOM;
    }
    f.ca.new[0][0].circ = 1;
    f.ca.new[0][0].offspring[0] = BOTTOM;
    CHECK(longest_dist_to_edge(&f.ca, 0, 0) == length);
    CHECK(longest_dist_to_sprout(&f.ca, length-1, 0) == length-1);
    /* Closing a long malformed loop returns zero without recursive overflow. */
    f.ca.new[0][0].circ = 0;
    f.ca.new[0][0].offspring[1] = SELF;
    f.ca.new[length-1][0].offspring[0] = TOP;
    CHECK(longest_dist_to_edge(&f.ca, 0, 0) == 0);
    CHECK(longest_dist_to_sprout(&f.ca, length-1, 0) == 0);
    fixture_dispose(&f); return 0;
}

static int transformation_rules(void) {
    Fixture f; fixture_init(&f, 2, 1);
    Cell *anchor = module(&f, 1, 0, ROOT, TOP);
    anchor->circ = 1; anchor->offspring[0] = TOP;
    Cell *leaf = module(&f, 0, 0, LEAF, BOTTOM);
    f.tissues[LEAF].evolve_type[DIST_TO_LEAF] = SHOOT;
    f.tissues[LEAF].evolve_type[DIST_TO_ROOT] = FLOWER_MALE;
    f.tissues[SHOOT].evolve_type[DIST_TO_ROOT] = TRUNK;
    Cell before = *leaf;
    transform(&f.ca);
    CHECK(leaf->tissue_id == FLOWER_MALE);
    before.tissue_id = FLOWER_MALE;
    CHECK(memcmp(&before, leaf, sizeof before) == 0);
    CHECK(anchor->tissue_id == ROOT);
    /* A transformed phenotype cannot supply a second rule during this pass. */
    leaf->tissue_id = LEAF;
    f.tissues[LEAF].evolve_type[DIST_TO_ROOT] = NONE;
    transform(&f.ca);
    CHECK(leaf->tissue_id == SHOOT);
    /* Invalid target IDs are ignored independently of the other rule. */
    leaf->tissue_id = LEAF;
    f.tissues[LEAF].evolve_type[DIST_TO_LEAF] = TC;
    f.tissues[LEAF].evolve_type[DIST_TO_ROOT] = TC;
    transform(&f.ca);
    CHECK(leaf->tissue_id == LEAF);
    f.tissues[LEAF].evolve_type[DIST_TO_ROOT] = FLOWER_FEMALE;
    transform(&f.ca);
    CHECK(leaf->tissue_id == FLOWER_FEMALE);
    transform(NULL);
    fixture_dispose(&f); return 0;
}

static int transformation_deep_storage(void) {
    const uint length = 64;
    Fixture f; fixture_init(&f, length, 1);
    Cell *anchor = module(&f, 0, 0, ROOT, BOTTOM);
    anchor->circ = 1;
    anchor->offspring[0] = anchor->offspring[1] = BOTTOM;
    for (uint y = 1; y < length; y++) {
        Cell *leaf = module(&f, y, 0, LEAF, TOP);
        if (y+1 < length) leaf->offspring[0] = BOTTOM;
    }
    f.tissues[LEAF].evolve_type[DIST_TO_LEAF] = SHOOT;
    f.tissues[LEAF].evolve_threshold[DIST_TO_LEAF] = 3;
    f.tissues[LEAF].evolve_type[DIST_TO_ROOT] = FLOWER_MALE;
    f.tissues[LEAF].evolve_threshold[DIST_TO_ROOT] = 50;
    transform(&f.ca);
    for (uint y = 1; y < length; y++) {
        TissueId expected = y > 50 ? FLOWER_MALE : length-y >= 3 ? SHOOT : LEAF;
        CHECK(f.ca.new[y][0].tissue_id == expected);
        CHECK(f.ca.new[y][0].species_id == 1 && f.ca.new[y][0].plant_id == 42);
        CHECK(f.ca.new[y][0].parent == TOP);
    }
    CHECK(anchor->tissue_id == ROOT);
    fixture_dispose(&f); return 0;
}

static const TestCase cases[] = {
    {"direction_helpers", direction_helpers},
    {"tissue_bounds", tissue_bounds},
    {"branching_distances", branching_distances},
    {"newborn_distance_markers", newborn_distance_markers},
    {"malformed_distances", malformed_distances},
    {"deep_chain_distances", deep_chain_distances},
    {"transformation_rules", transformation_rules},
    {"transformation_deep_storage", transformation_deep_storage},
};

int main(int argc, char **argv) {
    return test_main(argc, argv, cases, sizeof cases / sizeof cases[0]);
}

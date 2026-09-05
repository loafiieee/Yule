#include "../native_room_reset.h"
#include <assert.h>
#include <stdio.h>

typedef struct {
    int started, old_room, countdown;
    int swords, hazards, resets;
} Fixture;

static void reset_room(Fixture *f) {
    if (!native_room_reset_is_duplicate_initial(9, f->started,
                                                 f->old_room, f->countdown)) {
        ++f->swords;
        ++f->hazards;
        ++f->resets;
    }
}

static void start(Fixture *f) {
    f->countdown = 90;
    reset_room(f);
    f->started = 1;
}

static void enter_room(Fixture *f, int room) {
    if (f->old_room != room) {
        if (f->started) reset_room(f);
        f->old_room = room;
    }
    if (f->countdown) --f->countdown;
}

int main(void) {
    Fixture immediate = {0, -1, 0, 0, 0, 0};
    Fixture delayed = immediate;
    Fixture restored;
    start(&immediate); /* native GAME enter / online direct game_start */
    restored = immediate; /* authoritative frame zero / rollback restore */
    enter_room(&immediate, 4);
    enter_room(&restored, 4);
    assert(immediate.swords == 1 && immediate.hazards == 1);
    assert(restored.resets == 1 && restored.countdown == 89);
    enter_room(&delayed, 4); /* unstarted native update before GAME enter */
    start(&delayed);
    enter_room(&delayed, 4);
    assert(delayed.swords == 1 && delayed.hazards == 1);
    enter_room(&immediate, 5);
    enter_room(&immediate, 4);
    assert(immediate.resets == 3); /* normal room revisits still respawn */
    immediate = (Fixture){0, -1, 0, 0, 0, 0}; /* in-game native restart */
    start(&immediate);
    enter_room(&immediate, 4);
    assert(immediate.resets == 1);
    assert(!native_room_reset_is_duplicate_initial(2, 1, -1, 90));
    assert(!native_room_reset_is_duplicate_initial(9, 0, -1, 90));
    assert(!native_room_reset_is_duplicate_initial(9, 1, -1, 89));
    assert(!native_room_reset_is_duplicate_initial(9, 1, 4, 90));
    puts("native room reset lifecycle tests: OK");
    return 0;
}

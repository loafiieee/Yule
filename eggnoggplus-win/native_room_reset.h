#ifndef NATIVE_ROOM_RESET_H
#define NATIVE_ROOM_RESET_H

/* game_start_countdown resets the initial room before setting game_started.
 * The first game_update repeats that reset while old_active_room is still -1,
 * before decrementing the 90-tick countdown. Keep the room-entry bookkeeping,
 * but do not dispatch its reset actions a second time. All inputs are native
 * rollback state; no unsnapshotted once-per-frame flag is involved. */
static inline int native_room_reset_is_duplicate_initial(int mode,
                                                         int game_started,
                                                         int old_active_room,
                                                         int countdown) {
    return mode == 9 && game_started != 0 && old_active_room == -1 &&
           countdown == 90;
}

#endif

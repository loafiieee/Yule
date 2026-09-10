#ifndef OPPONENT_SPAWN_H
#define OPPONENT_SPAWN_H

/* Only respawning the nonleader is configurable. The native end-room rule is
 * separate from actual match completion; score/victory guards always survive. */
static int opponent_spawn_gate(int policy, int native_blocked,
                              int has_leader, int is_opponent,
                              int score_target, int score0, int score1,
                              int victory_countdown, int respawn) {
    if (!has_leader || (respawn && !is_opponent)) return native_blocked;
    if (respawn && policy == 2) return 1;
    if (policy != 1) return native_blocked;
    return victory_countdown != 0 ||
        (score_target != 0 && (score0 >= score_target || score1 >= score_target));
}
#endif

#ifndef OPPONENT_SPAWN_H
#define OPPONENT_SPAWN_H

/* Room-entry spawning of the trailing fighter is configurable. Combat and
 * loser-removal gates must stay open for a fighter who was already alive;
 * suppressing a new spawn must never masquerade as killing that fighter. */
static int opponent_spawn_gate(int policy, int native_blocked,
                              int has_leader, int is_opponent,
                              int score_target, int score0, int score1,
                              int victory_countdown, int respawn) {
    if (!has_leader || (respawn && !is_opponent)) return native_blocked;
    if (policy != 1 && policy != 2) return native_blocked;
    if (victory_countdown != 0 ||
        (score_target != 0 && (score0 >= score_target || score1 >= score_target)))
        return 1;
    if (policy == 2) return respawn ? 1 : 0;
    return 0;
}
#endif

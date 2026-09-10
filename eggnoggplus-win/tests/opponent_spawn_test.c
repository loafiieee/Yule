#include "../opponent_spawn.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    int policy, native, leader, opponent, respawn;
    /* No leader means ordinary death/reset. The leader's own respawn must also
     * retain native behavior, even in a room that suppresses opponents. */
    for (policy = 0; policy < 4; ++policy)
    for (native = 0; native < 2; ++native)
    for (respawn = 0; respawn < 2; ++respawn) {
        assert(opponent_spawn_gate(policy,native,0,1,0,0,0,0,respawn) == native);
        assert(opponent_spawn_gate(policy,native,1,0,0,0,0,0,1) == native);
    }
    for (leader = 0; leader < 2; ++leader)
    for (opponent = 0; opponent < 2; ++opponent)
    for (native = 0; native < 2; ++native) {
        assert(opponent_spawn_gate(0,native,leader,opponent,0,0,0,0,1) == native);
        assert(opponent_spawn_gate(3,native,leader,opponent,0,0,0,0,1) == native);
    }
    for (native = 0; native < 2; ++native) {
        assert(opponent_spawn_gate(2,native,1,1,0,0,0,0,1) == 1);
        assert(opponent_spawn_gate(2,native,1,1,0,0,0,0,0) == native);
        for (respawn = 0; respawn < 2; ++respawn) {
            assert(opponent_spawn_gate(1,native,1,1,0,0,0,0,respawn) == 0);
            assert(opponent_spawn_gate(1,native,1,1,5,4,4,0,respawn) == 0);
            assert(opponent_spawn_gate(1,native,1,1,5,5,0,0,respawn) == 1);
            assert(opponent_spawn_gate(1,native,1,1,5,0,6,0,respawn) == 1);
            assert(opponent_spawn_gate(1,native,1,1,0,0,0,1,respawn) == 1);
            assert(opponent_spawn_gate(1,native,1,1,0,0,0,-1,respawn) == 1);
        }
    }
    puts("PASS: opponent spawn policy preserves initial/leader respawns and match completion");
    return 0;
}

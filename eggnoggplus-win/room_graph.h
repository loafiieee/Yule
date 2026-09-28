#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROOM_GRAPH_MAX_NODES 64
#define ROOM_GRAPH_MAX_CONNECTIONS 256
#define ROOM_GRAPH_MAX_SOURCES 64
#define ROOM_GRAPH_ID_CAP 64
#define ROOM_GRAPH_COORD_LIMIT 4096

typedef enum RoomGraphSide {
    ROOM_GRAPH_SIDE_LEFT = 0,
    ROOM_GRAPH_SIDE_RIGHT = 1,
    ROOM_GRAPH_SIDE_TOP = 2,
    ROOM_GRAPH_SIDE_BOTTOM = 3
} RoomGraphSide;

typedef enum RoomGraphAppearance {
    ROOM_GRAPH_APPEARANCE_PRIMARY = 0,
    ROOM_GRAPH_APPEARANCE_MIRROR = 1
} RoomGraphAppearance;

typedef enum RoomGraphPlayerPolicy {
    ROOM_GRAPH_PLAYERS_BOTH = 0,
    ROOM_GRAPH_PLAYERS_PLAYER1 = 1,
    ROOM_GRAPH_PLAYERS_PLAYER2 = 2,
    ROOM_GRAPH_PLAYERS_GO = 3
} RoomGraphPlayerPolicy;

typedef enum RoomGraphFocusPolicy {
    ROOM_GRAPH_FOCUS_GO = 0,
    ROOM_GRAPH_FOCUS_CROSSING = 1
} RoomGraphFocusPolicy;

typedef struct RoomGraphSource {
    int width;
    int height;
} RoomGraphSource;

typedef struct RoomGraphNode {
    char id[ROOM_GRAPH_ID_CAP];
    int source_room;
    int x;
    int y;
    int mirror_x;
    int appearance;
} RoomGraphNode;

typedef struct RoomGraphConnection {
    int from_node;
    int from_side;
    int from_offset;
    int to_node;
    int to_side;
    int to_offset;
    int span;
    int one_way;
    int player_policy;
    int focus_policy;
} RoomGraphConnection;

typedef struct RoomGraph {
    int node_count;
    int connection_count;
    int start_node;
    RoomGraphNode nodes[ROOM_GRAPH_MAX_NODES];
    RoomGraphConnection connections[ROOM_GRAPH_MAX_CONNECTIONS];
} RoomGraph;

typedef enum RoomGraphError {
    ROOM_GRAPH_ERROR_NONE = 0,
    ROOM_GRAPH_ERROR_SOURCE_COUNT,
    ROOM_GRAPH_ERROR_NODE_COUNT,
    ROOM_GRAPH_ERROR_CONNECTION_COUNT,
    ROOM_GRAPH_ERROR_START,
    ROOM_GRAPH_ERROR_NODE_ID,
    ROOM_GRAPH_ERROR_DUPLICATE_NODE_ID,
    ROOM_GRAPH_ERROR_SOURCE,
    ROOM_GRAPH_ERROR_COORDINATE,
    ROOM_GRAPH_ERROR_MIRROR,
    ROOM_GRAPH_ERROR_APPEARANCE,
    ROOM_GRAPH_ERROR_OMITTED_SOURCE,
    ROOM_GRAPH_ERROR_OVERLAP,
    ROOM_GRAPH_ERROR_CONNECTION_NODE,
    ROOM_GRAPH_ERROR_SELF_CONNECTION,
    ROOM_GRAPH_ERROR_SIDE,
    ROOM_GRAPH_ERROR_OPPOSITE_SIDE,
    ROOM_GRAPH_ERROR_CONNECTION_RANGE,
    ROOM_GRAPH_ERROR_ALIGNMENT,
    ROOM_GRAPH_ERROR_ONE_WAY,
    ROOM_GRAPH_ERROR_PLAYER_POLICY,
    ROOM_GRAPH_ERROR_FOCUS_POLICY,
    ROOM_GRAPH_ERROR_AMBIGUOUS_EXIT,
    ROOM_GRAPH_ERROR_UNREACHABLE
} RoomGraphError;

typedef struct RoomGraphValidation {
    int error_count;
    RoomGraphError first_error;
    int node_index;
    int connection_index;
    int bounds_x;
    int bounds_y;
    int bounds_width;
    int bounds_height;
} RoomGraphValidation;

typedef struct RoomGraphExit {
    int connection_index;
    int from_node;
    int from_side;
    int from_offset;
    int to_node;
    int to_side;
    int to_offset;
    int span;
    int reversed;
} RoomGraphExit;

/* Validates the resolved, pointer-free native graph. Source dimensions come
 * from the already validated data.map rooms, so nodes cannot forge geometry.
 * Returns 1 when valid and 0 otherwise. */
int room_graph_validate(const RoomGraph* graph,
                        const RoomGraphSource* sources,
                        int source_count,
                        RoomGraphValidation* out_validation);

/* Expands the legacy center-outward source order into the same explicit graph
 * produced by Greggnogg. Returns 1 on success without allocating memory. */
int room_graph_from_mirrored(const RoomGraphSource* sources,
                             int source_count,
                             RoomGraph* out_graph,
                             RoomGraphValidation* out_validation);

/* Resolves one cell along an authored room edge to its traversable exit. The
 * edge offset is measured from the top of left/right edges or the left of
 * top/bottom edges. A successful result includes the equivalent offset on the
 * destination edge. One-way connections are only traversable from their
 * declared from endpoint. Returns 1 for an exit, 0 for a closed edge cell, and
 * -1 for invalid or ambiguous input. */
int room_graph_find_exit(const RoomGraph* graph,
                         int current_node,
                         int side,
                         int edge_offset,
                         RoomGraphExit* out_exit);

/* Finds the unique room rectangle containing a world-space tile cell. Returns
 * 1 and writes the node index, 0 when the cell is outside every room, and -1
 * when the inputs are invalid or malformed overlapping nodes are encountered. */
int room_graph_node_at_cell(const RoomGraph* graph,
                            const RoomGraphSource* sources,
                            int source_count,
                            int world_x,
                            int world_y,
                            int* out_node);

const char* room_graph_error_code(RoomGraphError error);

#ifdef __cplusplus
}
#endif

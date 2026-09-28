#include <stdio.h>
#include <string.h>

#include "../room_graph.h"

static int failures = 0;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        failures++; \
    } \
} while (0)

static void set_node(RoomGraphNode* node, const char* id, int source,
                     int x, int y, int mirror, int appearance) {
    memset(node, 0, sizeof(*node));
    snprintf(node->id, sizeof(node->id), "%s", id);
    node->source_room = source;
    node->x = x;
    node->y = y;
    node->mirror_x = mirror;
    node->appearance = appearance;
}

static void set_horizontal(RoomGraphConnection* connection,
                           int from, int to, int span) {
    memset(connection, 0, sizeof(*connection));
    connection->from_node = from;
    connection->from_side = ROOM_GRAPH_SIDE_RIGHT;
    connection->to_node = to;
    connection->to_side = ROOM_GRAPH_SIDE_LEFT;
    connection->span = span;
}

static RoomGraph mirrored_graph(void) {
    RoomGraph graph;
    memset(&graph, 0, sizeof(graph));
    graph.node_count = 3;
    graph.connection_count = 2;
    graph.start_node = 1;
    set_node(&graph.nodes[0], "left_1", 1, 0, 0, 0,
             ROOM_GRAPH_APPEARANCE_MIRROR);
    set_node(&graph.nodes[1], "center", 0, 12, 0, 0,
             ROOM_GRAPH_APPEARANCE_PRIMARY);
    set_node(&graph.nodes[2], "right_1", 1, 59, 0, 1,
             ROOM_GRAPH_APPEARANCE_PRIMARY);
    set_horizontal(&graph.connections[0], 0, 1, 7);
    set_horizontal(&graph.connections[1], 1, 2, 7);
    return graph;
}

static void test_mirrored_conversion_shape(void) {
    const RoomGraphSource sources[] = {{47, 18}, {12, 7}};
    RoomGraph graph = mirrored_graph();
    RoomGraph generated;
    RoomGraphValidation result;
    CHECK(room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.error_count == 0);
    CHECK(result.bounds_x == 0 && result.bounds_y == 0);
    CHECK(result.bounds_width == 71 && result.bounds_height == 18);
    CHECK(room_graph_from_mirrored(sources, 2, &generated, &result));
    CHECK(generated.node_count == graph.node_count &&
          generated.connection_count == graph.connection_count &&
          generated.start_node == graph.start_node);
    CHECK(memcmp(generated.nodes, graph.nodes,
                 sizeof(RoomGraphNode) * (size_t)graph.node_count) == 0);
    CHECK(memcmp(generated.connections, graph.connections,
                 sizeof(RoomGraphConnection) * (size_t)graph.connection_count) == 0);
    {
        const RoomGraphSource invalid[] = {{0, 12}};
        CHECK(!room_graph_from_mirrored(invalid, 1, &generated, &result));
        CHECK(result.first_error == ROOM_GRAPH_ERROR_SOURCE);
    }
    {
        const RoomGraphSource oversized[] = {{4097, 12}};
        CHECK(!room_graph_from_mirrored(oversized, 1, &generated, &result));
        CHECK(result.first_error == ROOM_GRAPH_ERROR_COORDINATE);
    }
}

static void test_geometry_and_references_fail_closed(void) {
    const RoomGraphSource sources[] = {{47, 18}, {12, 7}};
    RoomGraphValidation result;
    RoomGraph graph = mirrored_graph();

    graph.nodes[2].x = 58;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_OVERLAP);

    graph = mirrored_graph();
    graph.nodes[2].x = 60;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_ALIGNMENT);

    graph = mirrored_graph();
    graph.connections[0].span = 8;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_CONNECTION_RANGE);

    graph = mirrored_graph();
    graph.nodes[2].source_room = 2;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_SOURCE);

    graph = mirrored_graph();
    snprintf(graph.nodes[2].id, sizeof(graph.nodes[2].id), "%s", "center");
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_DUPLICATE_NODE_ID);
}

static void test_connections_and_reachability(void) {
    const RoomGraphSource sources[] = {{47, 18}, {12, 7}};
    RoomGraphValidation result;
    RoomGraph graph = mirrored_graph();

    graph.connection_count = 0;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_UNREACHABLE);

    graph = mirrored_graph();
    graph.connections[2] = graph.connections[0];
    graph.connection_count = 3;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_AMBIGUOUS_EXIT);

    memset(&graph, 0, sizeof(graph));
    graph.node_count = 2;
    graph.connection_count = 1;
    graph.start_node = 0;
    set_node(&graph.nodes[0], "top", 0, 0, 0, 0,
             ROOM_GRAPH_APPEARANCE_PRIMARY);
    set_node(&graph.nodes[1], "bottom", 1, 10, 18, 0,
             ROOM_GRAPH_APPEARANCE_PRIMARY);
    graph.connections[0].from_node = 1;
    graph.connections[0].from_side = ROOM_GRAPH_SIDE_TOP;
    graph.connections[0].to_node = 0;
    graph.connections[0].to_side = ROOM_GRAPH_SIDE_BOTTOM;
    graph.connections[0].to_offset = 10;
    graph.connections[0].span = 4;
    graph.connections[0].one_way = 1;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_UNREACHABLE);
    graph.connections[0].one_way = 0;
    CHECK(room_graph_validate(&graph, sources, 2, &result));

    graph.connections[0].player_policy = ROOM_GRAPH_PLAYERS_PLAYER2;
    graph.connections[0].focus_policy = ROOM_GRAPH_FOCUS_CROSSING;
    CHECK(room_graph_validate(&graph, sources, 2, &result));
    graph.connections[0].player_policy = 4;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_PLAYER_POLICY);
    graph.connections[0].player_policy = ROOM_GRAPH_PLAYERS_BOTH;
    graph.connections[0].focus_policy = 2;
    CHECK(!room_graph_validate(&graph, sources, 2, &result));
    CHECK(result.first_error == ROOM_GRAPH_ERROR_FOCUS_POLICY);
}

static void test_negative_world_coordinates(void) {
    const RoomGraphSource sources[] = {{12, 7}};
    RoomGraph graph;
    RoomGraphValidation result;
    memset(&graph, 0, sizeof(graph));
    graph.node_count = 1;
    graph.start_node = 0;
    set_node(&graph.nodes[0], "only", 0, -12, -7, 0,
             ROOM_GRAPH_APPEARANCE_PRIMARY);
    CHECK(room_graph_validate(&graph, sources, 1, &result));
    CHECK(result.bounds_x == -12 && result.bounds_y == -7);
    CHECK(result.bounds_width == 12 && result.bounds_height == 7);
}

static void test_exit_lookup(void) {
    RoomGraph graph = mirrored_graph();
    RoomGraphExit exit;
    int result;

    result = room_graph_find_exit(&graph, 0, ROOM_GRAPH_SIDE_RIGHT, 0, &exit);
    CHECK(result == 1);
    CHECK(exit.connection_index == 0 && exit.from_node == 0 &&
          exit.from_side == ROOM_GRAPH_SIDE_RIGHT && exit.from_offset == 0 &&
          exit.to_node == 1 && exit.to_side == ROOM_GRAPH_SIDE_LEFT &&
          exit.to_offset == 0 && exit.span == 7 && !exit.reversed);

    result = room_graph_find_exit(&graph, 1, ROOM_GRAPH_SIDE_LEFT, 6, &exit);
    CHECK(result == 1);
    CHECK(exit.connection_index == 0 && exit.from_node == 1 &&
          exit.to_node == 0 && exit.to_side == ROOM_GRAPH_SIDE_RIGHT &&
          exit.to_offset == 6 && exit.reversed);
    CHECK(room_graph_find_exit(&graph, 1, ROOM_GRAPH_SIDE_LEFT, 7, &exit) == 0);
    CHECK(room_graph_find_exit(&graph, 1, ROOM_GRAPH_SIDE_TOP, 0, &exit) == 0);

    graph.connections[0].from_offset = 2;
    graph.connections[0].to_offset = 4;
    graph.connections[0].span = 3;
    CHECK(room_graph_find_exit(&graph, 0, ROOM_GRAPH_SIDE_RIGHT, 3, &exit) == 1);
    CHECK(exit.to_offset == 5);
    CHECK(room_graph_find_exit(&graph, 1, ROOM_GRAPH_SIDE_LEFT, 6, &exit) == 1);
    CHECK(exit.to_offset == 4 && exit.reversed);

    graph.connections[0].one_way = 1;
    CHECK(room_graph_find_exit(&graph, 1, ROOM_GRAPH_SIDE_LEFT, 5, &exit) == 0);
    CHECK(room_graph_find_exit(&graph, 0, ROOM_GRAPH_SIDE_RIGHT, 2, &exit) == 1);

    graph.connections[2] = graph.connections[0];
    graph.connection_count = 3;
    CHECK(room_graph_find_exit(&graph, 0, ROOM_GRAPH_SIDE_RIGHT, 2, &exit) == -1);
    CHECK(room_graph_find_exit(&graph, -1, ROOM_GRAPH_SIDE_RIGHT, 0, &exit) == -1);
    CHECK(room_graph_find_exit(&graph, 0, 99, 0, &exit) == -1);
    CHECK(room_graph_find_exit(&graph, 0, ROOM_GRAPH_SIDE_RIGHT, -1, &exit) == -1);
}

static void test_node_lookup(void) {
    const RoomGraphSource sources[] = {{47, 18}, {12, 7}};
    RoomGraph graph = mirrored_graph();
    int node = -1;
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 0, 0, &node) == 1 &&
          node == 0);
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 11, 6, &node) == 1 &&
          node == 0);
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 12, 0, &node) == 1 &&
          node == 1);
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 58, 17, &node) == 1 &&
          node == 1);
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 59, 0, &node) == 1 &&
          node == 2);
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 71, 0, &node) == 0 &&
          node == -1);
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 0, 7, &node) == 0);

    graph.nodes[2].x = 58;
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 58, 0, &node) == -1);
    graph.nodes[2].source_room = 2;
    CHECK(room_graph_node_at_cell(&graph, sources, 2, 60, 0, &node) == -1);
}

int main(void) {
    test_mirrored_conversion_shape();
    test_geometry_and_references_fail_closed();
    test_connections_and_reachability();
    test_negative_world_coordinates();
    test_exit_lookup();
    test_node_lookup();
    CHECK(strcmp(room_graph_error_code(ROOM_GRAPH_ERROR_ALIGNMENT),
                 "alignment") == 0);
    if (failures) {
        fprintf(stderr, "%d room graph test(s) failed\n", failures);
        return 1;
    }
    puts("room_graph_test: all checks passed");
    return 0;
}

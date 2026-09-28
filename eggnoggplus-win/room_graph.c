#include "room_graph.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void graph_error(RoomGraphValidation* result, RoomGraphError error,
                        int node_index, int connection_index) {
    result->error_count++;
    if (result->first_error == ROOM_GRAPH_ERROR_NONE) {
        result->first_error = error;
        result->node_index = node_index;
        result->connection_index = connection_index;
    }
}

static int valid_node_id(const char id[ROOM_GRAPH_ID_CAP]) {
    size_t length;
    size_t index;
    if (!id || !memchr(id, '\0', ROOM_GRAPH_ID_CAP)) return 0;
    length = strlen(id);
    if (length == 0 || length >= ROOM_GRAPH_ID_CAP) return 0;
    if (!((id[0] >= 'A' && id[0] <= 'Z') ||
          (id[0] >= 'a' && id[0] <= 'z') ||
          (id[0] >= '0' && id[0] <= '9'))) return 0;
    for (index = 1; index < length; ++index) {
        char ch = id[index];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '.' || ch == '_' || ch == '-'))
            return 0;
    }
    return 1;
}

static int valid_side(int side) {
    return side >= ROOM_GRAPH_SIDE_LEFT && side <= ROOM_GRAPH_SIDE_BOTTOM;
}

static int opposite_side(int left, int right) {
    return (left == ROOM_GRAPH_SIDE_LEFT && right == ROOM_GRAPH_SIDE_RIGHT) ||
           (left == ROOM_GRAPH_SIDE_RIGHT && right == ROOM_GRAPH_SIDE_LEFT) ||
           (left == ROOM_GRAPH_SIDE_TOP && right == ROOM_GRAPH_SIDE_BOTTOM) ||
           (left == ROOM_GRAPH_SIDE_BOTTOM && right == ROOM_GRAPH_SIDE_TOP);
}

static int edge_length(const RoomGraphSource* source, int side) {
    return side == ROOM_GRAPH_SIDE_LEFT || side == ROOM_GRAPH_SIDE_RIGHT
        ? source->height : source->width;
}

static int connection_aligned(const RoomGraphNode* from,
                              const RoomGraphSource* from_source,
                              int from_side, int from_offset,
                              const RoomGraphNode* to,
                              const RoomGraphSource* to_source,
                              int to_side, int to_offset) {
    int64_t from_axis;
    int64_t to_axis;
    if (from_side == ROOM_GRAPH_SIDE_RIGHT && to_side == ROOM_GRAPH_SIDE_LEFT) {
        if ((int64_t)from->x + from_source->width != (int64_t)to->x) return 0;
        from_axis = (int64_t)from->y + from_offset;
        to_axis = (int64_t)to->y + to_offset;
    } else if (from_side == ROOM_GRAPH_SIDE_LEFT && to_side == ROOM_GRAPH_SIDE_RIGHT) {
        if ((int64_t)from->x != (int64_t)to->x + to_source->width) return 0;
        from_axis = (int64_t)from->y + from_offset;
        to_axis = (int64_t)to->y + to_offset;
    } else if (from_side == ROOM_GRAPH_SIDE_BOTTOM && to_side == ROOM_GRAPH_SIDE_TOP) {
        if ((int64_t)from->y + from_source->height != (int64_t)to->y) return 0;
        from_axis = (int64_t)from->x + from_offset;
        to_axis = (int64_t)to->x + to_offset;
    } else if (from_side == ROOM_GRAPH_SIDE_TOP && to_side == ROOM_GRAPH_SIDE_BOTTOM) {
        if ((int64_t)from->y != (int64_t)to->y + to_source->height) return 0;
        from_axis = (int64_t)from->x + from_offset;
        to_axis = (int64_t)to->x + to_offset;
    } else {
        return 0;
    }
    return from_axis == to_axis;
}

static int ranges_overlap(int first_offset, int first_span,
                          int second_offset, int second_span) {
    return (int64_t)first_offset < (int64_t)second_offset + second_span &&
           (int64_t)first_offset + first_span > (int64_t)second_offset;
}

static int endpoints_overlap(int first_node, int first_side,
                             int first_offset, int first_span,
                             int second_node, int second_side,
                             int second_offset, int second_span) {
    return first_node == second_node && first_side == second_side &&
           ranges_overlap(first_offset, first_span, second_offset, second_span);
}

int room_graph_validate(const RoomGraph* graph,
                        const RoomGraphSource* sources,
                        int source_count,
                        RoomGraphValidation* out_validation) {
    RoomGraphValidation local;
    RoomGraphValidation* result = out_validation ? out_validation : &local;
    unsigned char node_geometry_valid[ROOM_GRAPH_MAX_NODES];
    unsigned char connection_valid[ROOM_GRAPH_MAX_CONNECTIONS];
    unsigned char visited[ROOM_GRAPH_MAX_NODES];
    int queue[ROOM_GRAPH_MAX_NODES];
    int min_x = INT_MAX, min_y = INT_MAX, max_x = INT_MIN, max_y = INT_MIN;
    int node_count;
    int connection_count;
    int index;

    memset(result, 0, sizeof(*result));
    result->node_index = -1;
    result->connection_index = -1;
    memset(node_geometry_valid, 0, sizeof(node_geometry_valid));
    memset(connection_valid, 0, sizeof(connection_valid));
    memset(visited, 0, sizeof(visited));

    if (!graph || !sources || source_count < 1 || source_count > ROOM_GRAPH_MAX_SOURCES) {
        graph_error(result, ROOM_GRAPH_ERROR_SOURCE_COUNT, -1, -1);
        return 0;
    }
    node_count = graph->node_count;
    connection_count = graph->connection_count;
    if (node_count < 1 || node_count > ROOM_GRAPH_MAX_NODES) {
        graph_error(result, ROOM_GRAPH_ERROR_NODE_COUNT, -1, -1);
        return 0;
    }
    if (connection_count < 0 || connection_count > ROOM_GRAPH_MAX_CONNECTIONS) {
        graph_error(result, ROOM_GRAPH_ERROR_CONNECTION_COUNT, -1, -1);
        return 0;
    }
    if (graph->start_node < 0 || graph->start_node >= node_count)
        graph_error(result, ROOM_GRAPH_ERROR_START, -1, -1);

    for (index = 0; index < node_count; ++index) {
        const RoomGraphNode* node = &graph->nodes[index];
        int source_valid = node->source_room >= 0 && node->source_room < source_count;
        int dimensions_valid = source_valid && sources[node->source_room].width > 0 &&
            sources[node->source_room].height > 0;
        int64_t right = dimensions_valid
            ? (int64_t)node->x + sources[node->source_room].width : node->x;
        int64_t bottom = dimensions_valid
            ? (int64_t)node->y + sources[node->source_room].height : node->y;
        int other;
        if (!valid_node_id(node->id))
            graph_error(result, ROOM_GRAPH_ERROR_NODE_ID, index, -1);
        else for (other = 0; other < index; ++other) {
            if (valid_node_id(graph->nodes[other].id) &&
                strcmp(node->id, graph->nodes[other].id) == 0) {
                graph_error(result, ROOM_GRAPH_ERROR_DUPLICATE_NODE_ID, index, -1);
                break;
            }
        }
        if (!source_valid || !dimensions_valid) {
            graph_error(result, ROOM_GRAPH_ERROR_SOURCE, index, -1);
        }
        if (!dimensions_valid || node->x < -ROOM_GRAPH_COORD_LIMIT ||
            node->y < -ROOM_GRAPH_COORD_LIMIT || right > ROOM_GRAPH_COORD_LIMIT ||
            bottom > ROOM_GRAPH_COORD_LIMIT) {
            graph_error(result, ROOM_GRAPH_ERROR_COORDINATE, index, -1);
        } else {
            node_geometry_valid[index] = 1;
            if (node->x < min_x) min_x = node->x;
            if (node->y < min_y) min_y = node->y;
            if (right > max_x) max_x = (int)right;
            if (bottom > max_y) max_y = (int)bottom;
        }
        if (node->mirror_x != 0 && node->mirror_x != 1)
            graph_error(result, ROOM_GRAPH_ERROR_MIRROR, index, -1);
        if (node->appearance != ROOM_GRAPH_APPEARANCE_PRIMARY &&
            node->appearance != ROOM_GRAPH_APPEARANCE_MIRROR)
            graph_error(result, ROOM_GRAPH_ERROR_APPEARANCE, index, -1);
    }

    /* Source rooms are reusable designs. An unused design is harmless and
     * lets editors create it before placing an instance on the graph. */

    for (index = 0; index < node_count; ++index) {
        int other;
        const RoomGraphNode* first;
        const RoomGraphSource* first_source;
        if (!node_geometry_valid[index]) continue;
        first = &graph->nodes[index];
        first_source = &sources[first->source_room];
        for (other = index + 1; other < node_count; ++other) {
            const RoomGraphNode* second;
            const RoomGraphSource* second_source;
            if (!node_geometry_valid[other]) continue;
            second = &graph->nodes[other];
            second_source = &sources[second->source_room];
            if (first->x < second->x + second_source->width &&
                first->x + first_source->width > second->x &&
                first->y < second->y + second_source->height &&
                first->y + first_source->height > second->y)
                graph_error(result, ROOM_GRAPH_ERROR_OVERLAP, other, -1);
        }
    }

    for (index = 0; index < connection_count; ++index) {
        const RoomGraphConnection* connection = &graph->connections[index];
        int nodes_valid = connection->from_node >= 0 && connection->from_node < node_count &&
            connection->to_node >= 0 && connection->to_node < node_count;
        int sides_valid = valid_side(connection->from_side) && valid_side(connection->to_side);
        int values_valid = connection->from_offset >= 0 && connection->to_offset >= 0 &&
            connection->span > 0;
        int ranges_valid = 0;
        int aligned = 0;
        int previous;
        if (!nodes_valid) {
            graph_error(result, ROOM_GRAPH_ERROR_CONNECTION_NODE, -1, index);
        } else if (connection->from_node == connection->to_node) {
            graph_error(result, ROOM_GRAPH_ERROR_SELF_CONNECTION, -1, index);
        }
        if (!sides_valid) graph_error(result, ROOM_GRAPH_ERROR_SIDE, -1, index);
        else if (!opposite_side(connection->from_side, connection->to_side))
            graph_error(result, ROOM_GRAPH_ERROR_OPPOSITE_SIDE, -1, index);
        if (!values_valid) {
            graph_error(result, ROOM_GRAPH_ERROR_CONNECTION_RANGE, -1, index);
        } else if (nodes_valid && sides_valid) {
            const RoomGraphNode* from = &graph->nodes[connection->from_node];
            const RoomGraphNode* to = &graph->nodes[connection->to_node];
            if (node_geometry_valid[connection->from_node] &&
                node_geometry_valid[connection->to_node] &&
                (int64_t)connection->from_offset + connection->span <=
                    edge_length(&sources[from->source_room], connection->from_side) &&
                (int64_t)connection->to_offset + connection->span <=
                    edge_length(&sources[to->source_room], connection->to_side)) {
                ranges_valid = 1;
            } else {
                graph_error(result, ROOM_GRAPH_ERROR_CONNECTION_RANGE, -1, index);
            }
        }
        if (nodes_valid && sides_valid &&
            opposite_side(connection->from_side, connection->to_side) && ranges_valid) {
            const RoomGraphNode* from = &graph->nodes[connection->from_node];
            const RoomGraphNode* to = &graph->nodes[connection->to_node];
            aligned = connection_aligned(from, &sources[from->source_room],
                                         connection->from_side, connection->from_offset,
                                         to, &sources[to->source_room],
                                         connection->to_side, connection->to_offset);
            if (!aligned) graph_error(result, ROOM_GRAPH_ERROR_ALIGNMENT, -1, index);
        }
        if (connection->one_way != 0 && connection->one_way != 1)
            graph_error(result, ROOM_GRAPH_ERROR_ONE_WAY, -1, index);
        if (connection->player_policy < ROOM_GRAPH_PLAYERS_BOTH ||
            connection->player_policy > ROOM_GRAPH_PLAYERS_GO)
            graph_error(result, ROOM_GRAPH_ERROR_PLAYER_POLICY, -1, index);
        if (connection->focus_policy < ROOM_GRAPH_FOCUS_GO ||
            connection->focus_policy > ROOM_GRAPH_FOCUS_CROSSING)
            graph_error(result, ROOM_GRAPH_ERROR_FOCUS_POLICY, -1, index);
        if (nodes_valid && sides_valid && ranges_valid) {
            for (previous = 0; previous < index; ++previous) {
                const RoomGraphConnection* old = &graph->connections[previous];
                if (!connection_valid[previous]) continue;
                if (endpoints_overlap(connection->from_node, connection->from_side,
                                      connection->from_offset, connection->span,
                                      old->from_node, old->from_side,
                                      old->from_offset, old->span) ||
                    endpoints_overlap(connection->from_node, connection->from_side,
                                      connection->from_offset, connection->span,
                                      old->to_node, old->to_side,
                                      old->to_offset, old->span) ||
                    endpoints_overlap(connection->to_node, connection->to_side,
                                      connection->to_offset, connection->span,
                                      old->from_node, old->from_side,
                                      old->from_offset, old->span) ||
                    endpoints_overlap(connection->to_node, connection->to_side,
                                      connection->to_offset, connection->span,
                                      old->to_node, old->to_side,
                                      old->to_offset, old->span)) {
                    graph_error(result, ROOM_GRAPH_ERROR_AMBIGUOUS_EXIT, -1, index);
                    break;
                }
            }
        }
        if (nodes_valid && connection->from_node != connection->to_node &&
            sides_valid && opposite_side(connection->from_side, connection->to_side) &&
            ranges_valid && aligned && (connection->one_way == 0 || connection->one_way == 1) &&
            connection->player_policy >= ROOM_GRAPH_PLAYERS_BOTH &&
            connection->player_policy <= ROOM_GRAPH_PLAYERS_GO &&
            connection->focus_policy >= ROOM_GRAPH_FOCUS_GO &&
            connection->focus_policy <= ROOM_GRAPH_FOCUS_CROSSING)
            connection_valid[index] = 1;
    }

    if (graph->start_node >= 0 && graph->start_node < node_count) {
        int head = 0;
        int tail = 0;
        queue[tail++] = graph->start_node;
        visited[graph->start_node] = 1;
        while (head < tail) {
            int current = queue[head++];
            for (index = 0; index < connection_count; ++index) {
                const RoomGraphConnection* connection;
                int next = -1;
                if (!connection_valid[index]) continue;
                connection = &graph->connections[index];
                if (connection->from_node == current) next = connection->to_node;
                else if (!connection->one_way && connection->to_node == current)
                    next = connection->from_node;
                if (next >= 0 && !visited[next]) {
                    visited[next] = 1;
                    queue[tail++] = next;
                }
            }
        }
        for (index = 0; index < node_count; ++index)
            if (!visited[index]) graph_error(result, ROOM_GRAPH_ERROR_UNREACHABLE, index, -1);
    }

    if (min_x != INT_MAX) {
        result->bounds_x = min_x;
        result->bounds_y = min_y;
        result->bounds_width = max_x - min_x;
        result->bounds_height = max_y - min_y;
    }
    return result->error_count == 0;
}

int room_graph_from_mirrored(const RoomGraphSource* sources,
                             int source_count,
                             RoomGraph* out_graph,
                             RoomGraphValidation* out_validation) {
    int source;
    int node = 0;
    int cursor = 0;
    int64_t total_width = 0;
    if (!sources || !out_graph || source_count < 1 ||
        source_count > ROOM_GRAPH_MAX_SOURCES ||
        source_count * 2 - 1 > ROOM_GRAPH_MAX_NODES) {
        if (out_validation) {
            memset(out_validation, 0, sizeof(*out_validation));
            out_validation->first_error = source_count < 1 || source_count > ROOM_GRAPH_MAX_SOURCES
                ? ROOM_GRAPH_ERROR_SOURCE_COUNT : ROOM_GRAPH_ERROR_NODE_COUNT;
            out_validation->error_count = 1;
            out_validation->node_index = -1;
            out_validation->connection_index = -1;
        }
        return 0;
    }
    for (source = source_count - 1; source >= 1; --source) {
        if (sources[source].width <= 0 || sources[source].height <= 0) {
            if (out_validation) {
                memset(out_validation, 0, sizeof(*out_validation));
                out_validation->first_error = ROOM_GRAPH_ERROR_SOURCE;
                out_validation->error_count = 1;
                out_validation->node_index = source_count - 1 - source;
                out_validation->connection_index = -1;
            }
            return 0;
        }
        total_width += sources[source].width;
    }
    if (sources[0].width <= 0 || sources[0].height <= 0) {
        if (out_validation) {
            memset(out_validation, 0, sizeof(*out_validation));
            out_validation->first_error = ROOM_GRAPH_ERROR_SOURCE;
            out_validation->error_count = 1;
            out_validation->node_index = source_count - 1;
            out_validation->connection_index = -1;
        }
        return 0;
    }
    total_width = total_width * 2 + sources[0].width;
    if (total_width > ROOM_GRAPH_COORD_LIMIT) {
        if (out_validation) {
            memset(out_validation, 0, sizeof(*out_validation));
            out_validation->first_error = ROOM_GRAPH_ERROR_COORDINATE;
            out_validation->error_count = 1;
            out_validation->node_index = -1;
            out_validation->connection_index = -1;
        }
        return 0;
    }
    memset(out_graph, 0, sizeof(*out_graph));
    out_graph->node_count = source_count * 2 - 1;
    out_graph->connection_count = out_graph->node_count - 1;
    out_graph->start_node = source_count - 1;
    for (source = source_count - 1; source >= 1; --source) {
        RoomGraphNode* output = &out_graph->nodes[node++];
        snprintf(output->id, sizeof(output->id), "left_%d", source);
        output->source_room = source;
        output->x = cursor;
        output->appearance = ROOM_GRAPH_APPEARANCE_MIRROR;
        cursor += sources[source].width;
    }
    {
        RoomGraphNode* output = &out_graph->nodes[node++];
        snprintf(output->id, sizeof(output->id), "%s", "center");
        output->source_room = 0;
        output->x = cursor;
        output->appearance = ROOM_GRAPH_APPEARANCE_PRIMARY;
        cursor += sources[0].width;
    }
    for (source = 1; source < source_count; ++source) {
        RoomGraphNode* output = &out_graph->nodes[node++];
        snprintf(output->id, sizeof(output->id), "right_%d", source);
        output->source_room = source;
        output->x = cursor;
        output->mirror_x = 1;
        output->appearance = ROOM_GRAPH_APPEARANCE_PRIMARY;
        cursor += sources[source].width;
    }
    for (node = 0; node < out_graph->connection_count; ++node) {
        RoomGraphConnection* connection = &out_graph->connections[node];
        const RoomGraphNode* left = &out_graph->nodes[node];
        const RoomGraphNode* right = &out_graph->nodes[node + 1];
        connection->from_node = node;
        connection->from_side = ROOM_GRAPH_SIDE_RIGHT;
        connection->to_node = node + 1;
        connection->to_side = ROOM_GRAPH_SIDE_LEFT;
        connection->span = sources[left->source_room].height <
                sources[right->source_room].height
            ? sources[left->source_room].height
            : sources[right->source_room].height;
    }
    return room_graph_validate(out_graph, sources, source_count, out_validation);
}

int room_graph_find_exit(const RoomGraph* graph,
                         int current_node,
                         int side,
                         int edge_offset,
                         RoomGraphExit* out_exit) {
    RoomGraphExit found;
    int found_count = 0;
    int index;
    if (out_exit) memset(out_exit, 0, sizeof(*out_exit));
    if (!graph || !out_exit || graph->node_count < 1 ||
        graph->node_count > ROOM_GRAPH_MAX_NODES ||
        graph->connection_count < 0 ||
        graph->connection_count > ROOM_GRAPH_MAX_CONNECTIONS ||
        current_node < 0 || current_node >= graph->node_count ||
        !valid_side(side) || edge_offset < 0) return -1;

    memset(&found, 0, sizeof(found));
    for (index = 0; index < graph->connection_count; ++index) {
        const RoomGraphConnection* connection = &graph->connections[index];
        int endpoint_offset;
        int reversed;
        if (connection->from_node == current_node && connection->from_side == side) {
            endpoint_offset = connection->from_offset;
            reversed = 0;
        } else if (!connection->one_way && connection->to_node == current_node &&
                   connection->to_side == side) {
            endpoint_offset = connection->to_offset;
            reversed = 1;
        } else {
            continue;
        }
        if (connection->span <= 0 || edge_offset < endpoint_offset ||
            (int64_t)edge_offset >= (int64_t)endpoint_offset + connection->span)
            continue;
        if (connection->from_node < 0 || connection->from_node >= graph->node_count ||
            connection->to_node < 0 || connection->to_node >= graph->node_count ||
            !valid_side(connection->from_side) || !valid_side(connection->to_side) ||
            !opposite_side(connection->from_side, connection->to_side) ||
            connection->from_offset < 0 || connection->to_offset < 0 ||
            (connection->one_way != 0 && connection->one_way != 1)) return -1;
        if (++found_count > 1) return -1;
        found.connection_index = index;
        found.from_node = current_node;
        found.from_side = side;
        found.from_offset = endpoint_offset;
        found.span = connection->span;
        found.reversed = reversed;
        if (!reversed) {
            found.to_node = connection->to_node;
            found.to_side = connection->to_side;
            found.to_offset = connection->to_offset +
                (edge_offset - connection->from_offset);
        } else {
            found.to_node = connection->from_node;
            found.to_side = connection->from_side;
            found.to_offset = connection->from_offset +
                (edge_offset - connection->to_offset);
        }
    }
    if (!found_count) return 0;
    *out_exit = found;
    return 1;
}

int room_graph_node_at_cell(const RoomGraph* graph,
                            const RoomGraphSource* sources,
                            int source_count,
                            int world_x,
                            int world_y,
                            int* out_node) {
    int found = -1;
    int index;
    if (out_node) *out_node = -1;
    if (!graph || !sources || !out_node || source_count < 1 ||
        source_count > ROOM_GRAPH_MAX_SOURCES || graph->node_count < 1 ||
        graph->node_count > ROOM_GRAPH_MAX_NODES) return -1;
    for (index = 0; index < graph->node_count; ++index) {
        const RoomGraphNode* node = &graph->nodes[index];
        const RoomGraphSource* source;
        int64_t right;
        int64_t bottom;
        if (node->source_room < 0 || node->source_room >= source_count) return -1;
        source = &sources[node->source_room];
        if (source->width <= 0 || source->height <= 0) return -1;
        right = (int64_t)node->x + source->width;
        bottom = (int64_t)node->y + source->height;
        if ((int64_t)world_x < node->x || (int64_t)world_x >= right ||
            (int64_t)world_y < node->y || (int64_t)world_y >= bottom) continue;
        if (found >= 0) return -1;
        found = index;
    }
    if (found < 0) return 0;
    *out_node = found;
    return 1;
}

const char* room_graph_error_code(RoomGraphError error) {
    static const char* const codes[] = {
        "none", "source_count", "node_count", "connection_count", "start",
        "node_id", "duplicate_node_id", "source", "coordinate", "mirror",
        "appearance", "omitted_source", "overlap", "connection_node",
        "self_connection", "side", "opposite_side", "connection_range",
        "alignment", "one_way", "player_policy", "focus_policy",
        "ambiguous_exit", "unreachable"
    };
    if (error < ROOM_GRAPH_ERROR_NONE || error > ROOM_GRAPH_ERROR_UNREACHABLE)
        return "unknown";
    return codes[(int)error];
}

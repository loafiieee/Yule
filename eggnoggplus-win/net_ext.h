#pragma once
#include <stddef.h>
#include <stdint.h>
/* net_ext.h – simple non-blocking TCP client used by eggnogg-online mod.
   All functions are thread-unsafe; call only from the main game thread.
   Slots are 0-based integer handles returned by net_connect(). */

/* Open a non-blocking TCP connection.  Returns a slot index (>= 0) on
   success, or -1 if no slot is free or the name could not be resolved. */
int  net_connect      (const char *host, int port);

/* Poll an in-progress async connect.
   Returns  1 = now connected,  0 = still pending,  -1 = failed/closed. */
int  net_check_connect(int slot);

/* Copy one logical message into a bounded output queue, then drain whatever
   Winsock currently accepts.  Returns len when the complete message was
   accepted, 0 when local backpressure leaves insufficient room, or -1 on
   invalid input/socket failure.  A positive result never means a partial
   message, and the caller may reuse/free data immediately. */
int  net_send         (int slot, const char *data, int len);

/* Receive available bytes into buf (non-blocking).
   Returns > 0: byte count,  0: no data yet,  -1: error or remote close. */
int  net_recv         (int slot, char *buf, int maxlen);

int  net_connected    (int slot);
int  net_connecting   (int slot);
void net_close        (int slot);

/* Best-effort local LAN IPv4 address for P2P hints. Returns 1 if filled. */
int  net_local_ipv4    (char *buf, int buflen);

typedef struct NetNetworkProfile {
    char primary_ipv4[64];
    char primary_adapter[128];
    int active_ipv4_adapters;
    int vpn_suspected;
    int primary_vpn_suspected;
    int route_matched;
    int primary_is_private;
    int primary_is_cgnat;
} NetNetworkProfile;

/* Best-effort local adapter evidence. VPN detection is heuristic; CGNAT can
   be called likely only when Windows itself owns a 100.64/10 address. When a
   target is supplied, the primary adapter is the route Windows chose for it. */
int net_network_profile(const char *target_host, NetNetworkProfile *out);

typedef struct NetUdpProbeResult {
    char observed_host[64];
    uint16_t observed_port;
    uint32_t round_trip_ms;
} NetUdpProbeResult;

/* One process-wide, non-blocking UDP reachability probe against the online
   server's udp_ping endpoint. poll: 0 pending, 1 complete, -1 failed. */
int net_udp_probe_start(const char *host, uint16_t port, uint32_t sequence,
                        uint32_t timeout_ms, char *error, size_t error_cap);
int net_udp_probe_poll(NetUdpProbeResult *out, char *error, size_t error_cap);
int net_udp_probe_active(void);
void net_udp_probe_cancel(void);

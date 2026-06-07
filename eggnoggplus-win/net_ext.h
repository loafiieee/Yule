#pragma once
/* net_ext.h – simple non-blocking TCP client used by eggnogg-online mod.
   All functions are thread-unsafe; call only from the main game thread.
   Slots are 0-based integer handles returned by net_connect(). */

/* Open a non-blocking TCP connection.  Returns a slot index (>= 0) on
   success, or -1 if no slot is free or the name could not be resolved. */
int  net_connect      (const char *host, int port);

/* Poll an in-progress async connect.
   Returns  1 = now connected,  0 = still pending,  -1 = failed/closed. */
int  net_check_connect(int slot);

/* Send raw bytes.  Returns bytes queued, 0 if send buffer full, -1 on error. */
int  net_send         (int slot, const char *data, int len);

/* Receive available bytes into buf (non-blocking).
   Returns > 0: byte count,  0: no data yet,  -1: error or remote close. */
int  net_recv         (int slot, char *buf, int maxlen);

int  net_connected    (int slot);
int  net_connecting   (int slot);
void net_close        (int slot);

/* Best-effort local LAN IPv4 address for P2P hints. Returns 1 if filled. */
int  net_local_ipv4    (char *buf, int buflen);

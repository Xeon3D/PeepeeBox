"""Apply the remote-switch (nrswitch) implementation to src/network/net_switch.c."""
import os, sys
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'network', 'net_switch.c')
s = open(p, encoding='utf-8').read()

def rep(a, b, count=1):
    global s
    assert s.count(a) == count, (a[:70], s.count(a))
    s = s.replace(a, b)

# 1. struct: remote mode fields
rep("""    uint8_t        promisc;
    uint8_t        secret_enabled;
    uint8_t        secret_hash[32];
""", """    uint8_t        promisc;
    uint8_t        secret_enabled;
    uint8_t        secret_hash[32];
    uint8_t        remote;      /* NET_TYPE_NRSWITCH: one unicast peer instead of the local multicast switch */
    net_switch_sockaddr_t remote_addr;
""")

# 2. remote resolver + keepalive, placed before net_switch_thread
rep("""static void
net_switch_thread(void *priv)
{""", r"""/* Remote switch: resolve "host[:port]" (default port 8086) into one unicast peer.
   Frames go to it from the receive socket itself, so that replies -- and the NAT
   mapping on the way -- come back to us. */
static int
net_switch_add_remote(net_switch_t *netswitch, const char *spec)
{
    char host[128];
    char port[8] = "8086";
    strncpy(host, spec, sizeof(host) - 1);
    host[sizeof(host) - 1] = '\0';
    char *colon = strrchr(host, ':');
    if (colon && colon[1]) {
        strncpy(port, colon + 1, sizeof(port) - 1);
        port[sizeof(port) - 1] = '\0';
        *colon = '\0';
    }

    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port, &hints, &res) != 0 || !res)
        return 0;

    net_switch_hostaddr_t *hostaddr = (net_switch_hostaddr_t *) calloc(1, sizeof(net_switch_hostaddr_t));
    memcpy(&hostaddr->addr_tx.sin, res->ai_addr, sizeof(struct sockaddr_in));
    memcpy(&netswitch->remote_addr.sin, res->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    hostaddr->socket_tx = netswitch->socket_rx; /* shared; not closed separately */
    netswitch->hostaddrs = hostaddr;
    netswitch->remote    = 1;

#ifdef ENABLE_SWITCH_LOG
    char buf[INET_ADDRSTRLEN];
    buf[0] = '\0';
    inet_ntop(AF_INET, &hostaddr->addr_tx.sin.sin_addr.s_addr, buf, sizeof(buf));
    netswitch_log("Network Switch: remote switch %s -> %s:%d\n", spec, buf, ntohs(hostaddr->addr_tx.sin.sin_port));
#endif
    return 1;
}

/* Remote switch keep-alive: an empty datagram, which every receiver ignores,
   just to keep the NAT mapping alive between the cabinet's transmissions. */
static void
net_switch_keepalive(net_switch_t *netswitch)
{
    if (netswitch->remote && netswitch->hostaddrs)
        sendto(netswitch->socket_rx, "", 0, 0, &netswitch->hostaddrs->addr_tx.sa, sizeof(netswitch->hostaddrs->addr_tx.sa));
}

static void
net_switch_thread(void *priv)
{""")

# 3. thread loop: timeouts for keep-alive
rep("""    int packets;
    ssize_t len;
#ifdef _WIN32
    uint8_t run = 1;
    while (run) {
        int ret = WaitForMultipleObjects(NET_EVENT_MAX, events, FALSE, INFINITE);
        switch (ret - WAIT_OBJECT_0) {
            case NET_EVENT_STOP:
                run = 0;
#else
    while (1) {
        poll(pfd, NET_EVENT_MAX, -1);
        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
#endif""", """    int packets;
    ssize_t len;
    net_switch_sockaddr_t from;
    socklen_t             from_len;
#define SWITCH_KEEPALIVE_MS 20000
#ifdef _WIN32
    uint8_t run = 1;
    while (run) {
        int ret = WaitForMultipleObjects(NET_EVENT_MAX, events, FALSE, netswitch->remote ? SWITCH_KEEPALIVE_MS : INFINITE);
        if (ret == WAIT_TIMEOUT) {
            net_switch_keepalive(netswitch);
            continue;
        }
        switch (ret - WAIT_OBJECT_0) {
            case NET_EVENT_STOP:
                run = 0;
#else
    while (1) {
        if (poll(pfd, NET_EVENT_MAX, netswitch->remote ? SWITCH_KEEPALIVE_MS : -1) == 0) {
            net_switch_keepalive(netswitch);
            continue;
        }
        if (pfd[NET_EVENT_STOP].revents & POLLIN) {
#endif""")

# 4. rx: recvfrom + source filter (both branches)
rep("""            if (netswitch->secret_enabled) {
                len = recv(netswitch->socket_rx, (char *) netswitch->pkt.data, NET_MAX_FRAME + sizeof(netswitch->secret_hash), 0);
                if (len < (sizeof(netswitch->secret_hash) + 12)) {""", """            from_len = sizeof(from);
            if (netswitch->secret_enabled) {
                len = recvfrom(netswitch->socket_rx, (char *) netswitch->pkt.data, NET_MAX_FRAME + sizeof(netswitch->secret_hash), 0, &from.sa, &from_len);
                if (netswitch->remote && (len < 0 ||
                    from.sin.sin_addr.s_addr != netswitch->remote_addr.sin.sin_addr.s_addr ||
                    from.sin.sin_port != netswitch->remote_addr.sin.sin_port))
                    continue; /* not from our switch */
                if (len < (sizeof(netswitch->secret_hash) + 12)) {""")
rep("""            } else {
                len = recv(netswitch->socket_rx, (char *) netswitch->pkt.data, NET_MAX_FRAME, 0);
                if (len < 12) {""", """            } else {
                len = recvfrom(netswitch->socket_rx, (char *) netswitch->pkt.data, NET_MAX_FRAME, 0, &from.sa, &from_len);
                if (netswitch->remote && (len < 0 ||
                    from.sin.sin_addr.s_addr != netswitch->remote_addr.sin.sin_addr.s_addr ||
                    from.sin.sin_port != netswitch->remote_addr.sin.sin_port))
                    continue; /* not from our switch */
                if (len < 12) {""")

# 5. init: bind + peer setup
rep("""    netswitch->port_out = htons(SWITCH_MULTICAST_PORT);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_ANY) },
        .sin_port = netswitch->port_out
    };
    if (bind(netswitch->socket_rx, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        snprintf(netdrv_errbuf, NET_DRV_ERRBUF_SIZE, "Could not bind to port %d\\n", (int) addr.sin_port);
        goto fail;
    }

    /* Add host interfaces. */
    net_switch_update_hostaddrs(netswitch);
    if (!netswitch->hostaddrs) {
        strncpy(netdrv_errbuf, "Could not add any interfaces\\n", NET_DRV_ERRBUF_SIZE);
        goto fail;
    }
""", """    netswitch->port_out = htons(SWITCH_MULTICAST_PORT);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr = { .s_addr = htonl(INADDR_ANY) },
        .sin_port = (netcard->net_type == NET_TYPE_NRSWITCH) ? 0 : netswitch->port_out /* remote: any port, replies come back to it */
    };
    if (bind(netswitch->socket_rx, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        snprintf(netdrv_errbuf, NET_DRV_ERRBUF_SIZE, "Could not bind to port %d\\n", (int) ntohs(addr.sin_port));
        goto fail;
    }

    if (netcard->net_type == NET_TYPE_NRSWITCH) {
        /* Remote switch: one unicast peer, no host interface discovery. */
        if (!netcard->nrs_hostname[0]) {
            strncpy(netdrv_errbuf, "No remote switch host configured\\n", NET_DRV_ERRBUF_SIZE);
            goto fail;
        }
        if (!net_switch_add_remote(netswitch, netcard->nrs_hostname)) {
            snprintf(netdrv_errbuf, NET_DRV_ERRBUF_SIZE, "Could not resolve remote switch host %s\\n", netcard->nrs_hostname);
            goto fail;
        }
        net_switch_keepalive(netswitch); /* open the NAT mapping right away */
    } else {
        /* Add host interfaces. */
        net_switch_update_hostaddrs(netswitch);
        if (!netswitch->hostaddrs) {
            strncpy(netdrv_errbuf, "Could not add any interfaces\\n", NET_DRV_ERRBUF_SIZE);
            goto fail;
        }
    }
""")

# 6. close: shared socket
rep("""    net_switch_hostaddr_t *hostaddr = netswitch->hostaddrs;
    while (hostaddr) {
        if (hostaddr->socket_tx >= 0)
            close(hostaddr->socket_tx);""", """    net_switch_hostaddr_t *hostaddr = netswitch->hostaddrs;
    while (hostaddr) {
        if (hostaddr->socket_tx >= 0 && hostaddr->socket_tx != netswitch->socket_rx)
            close(hostaddr->socket_tx);""")

open(p, 'w', encoding='utf-8', newline='').write(s)
print("patched", p)

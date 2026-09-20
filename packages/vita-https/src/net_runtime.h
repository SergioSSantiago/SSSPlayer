#ifndef VITA_HTTPS_NET_RUNTIME_H
#define VITA_HTTPS_NET_RUNTIME_H

int vita_https_net_init(void);
void vita_https_net_term(void);
int vita_https_net_is_connected(void);
int vita_https_net_wifi_signal_percent(int *percent);

#endif

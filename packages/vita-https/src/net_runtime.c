#include "net_runtime.h"

#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

static char s_net_memory[1024 * 1024];
static int s_module_loaded;
static int s_net_initialized;
static int s_netctl_initialized;

int vita_https_net_init(void) {
	int result = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	if (result < 0) return result;
	s_module_loaded = 1;
	SceNetInitParam parameters = {
		.memory = s_net_memory,
		.size = sizeof(s_net_memory),
		.flags = 0
	};
	result = sceNetInit(&parameters);
	if (result < 0) goto fail;
	s_net_initialized = 1;
	result = sceNetCtlInit();
	if (result < 0) goto fail;
	s_netctl_initialized = 1;
	return 0;

fail:
	vita_https_net_term();
	return result;
}

int vita_https_net_is_connected(void) {
	if (!s_netctl_initialized) return 0;
	int state = 0;
	return sceNetCtlInetGetState(&state) >= 0 &&
	       state == SCE_NETCTL_STATE_CONNECTED;
}

int vita_https_net_wifi_signal_percent(int *percent) {
	if (!s_netctl_initialized || !percent) return -1;
	SceNetCtlInfo info;
	int result = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE,
	                                  &info);
	if (result < 0) return result;
	*percent = info.rssi_percentage;
	return 0;
}

void vita_https_net_term(void) {
	if (s_netctl_initialized) {
		s_netctl_initialized = 0;
		sceNetCtlTerm();
	}
	if (s_net_initialized) {
		sceNetTerm();
		s_net_initialized = 0;
	}
	if (s_module_loaded) {
		sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
		s_module_loaded = 0;
	}
}

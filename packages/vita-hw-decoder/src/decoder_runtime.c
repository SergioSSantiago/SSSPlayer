#include "decoder_runtime.h"

#include <psp2/kernel/modulemgr.h>

#include "module_log.h"

static SceUID g_runtime_module = -1;

int vita_hw_decoder_runtime_prepare(void) {
	if (g_runtime_module >= 0) return 0;
	int status = 0;
	SceUID module = sceKernelLoadStartModule(
	    "app0:modules/reAvPlayer.suprx", 0, NULL, 0, NULL, &status);
	log_printf("vita_hw_decoder: load runtime -> 0x%08X status=0x%08X",
	           (unsigned)module, (unsigned)status);
	if (module < 0) return module;
	g_runtime_module = module;
	return 0;
}

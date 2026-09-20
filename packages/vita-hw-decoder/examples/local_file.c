#include <psp2/kernel/processmgr.h>
#include <vita2d.h>

#include "vita_hw_decoder.h"

/* This function assumes that the host application has initialized vita2d and
 * packaged reAvPlayer.suprx at app0:modules/reAvPlayer.suprx. */
int play_local_h264_aac_file(const char *path) {
	VitaHwDecoderStreamFactory stream;
	vita_hw_decoder_file_stream_factory(path, &stream);

	VitaHwDecoderPlayer *player = vita_hw_decoder_create();
	if (!player) return -1;
	VitaHwDecoderPlayerConfig config = {
		.stream = stream,
		.volume_percent = 100
	};
	int result = vita_hw_decoder_open(player, &config);
	while (result == 0) {
		VitaHwDecoderPlayerStatus status;
		vita_hw_decoder_get_status(player, &status);
		if (status.error || status.eof) break;

		vita2d_start_drawing();
		vita2d_clear_screen();
		vita_hw_decoder_present(player, 0);
		vita2d_end_drawing();
		vita2d_wait_rendering_done();
		vita_hw_decoder_render_complete(player);
		vita2d_swap_buffers();
		sceKernelDelayThread(1000);
	}
	vita_hw_decoder_destroy(player);
	return result;
}

/*
 * SSSPlayer – decoder_m4a.c
 * AAC-in-MP4 (.m4a / .aac) via libavformat demux + Vita sceAudiodec.
 * Used for YouTube audio downloads (AAC/M4A) so the music library can play them.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>

#include "decoder.h"
#include "audio_engine.h"
#include "internal/vita_aac_decoder.h"

#define M4A_PCM_CAP (SCE_AUDIODEC_AAC_MAX_SAMPLES * 2)

typedef struct {
	AVFormatContext *fmt;
	AVPacket *packet;
	VitaAacDecoder aac;
	int stream_index;
	int channels_src;   /* 1 or 2 from container */
	int sample_rate;
	int aac_ready;
	int eof;
	/* Leftover stereo PCM from the last AAC frame */
	int16_t pcm_pending[M4A_PCM_CAP];
	uint32_t pending_frames;
	uint32_t pending_read;
} M4aState;

static int m4a_open(Decoder *dec, const char *filepath);
static void m4a_close(Decoder *dec);
static int m4a_decode_frames(Decoder *dec, int16_t *out,
                             uint32_t frames_req, uint32_t *frames_decoded);
static int m4a_seek(Decoder *dec, uint64_t position_ms);
static void m4a_reset(Decoder *dec);

static const DecoderVtable m4a_vtable = {
	m4a_open,
	m4a_close,
	m4a_decode_frames,
	m4a_seek,
	m4a_reset
};

int decoder_m4a_open(Decoder *dec, const char *filepath)
{
	return m4a_open(dec, filepath);
}

static int find_aac_stream(AVFormatContext *fmt)
{
	unsigned i;
	for (i = 0; i < fmt->nb_streams; i++) {
		AVCodecParameters *par = fmt->streams[i]->codecpar;
		if (par && par->codec_type == AVMEDIA_TYPE_AUDIO &&
		    par->codec_id == AV_CODEC_ID_AAC)
			return (int)i;
	}
	return -1;
}

static void write_stereo(int16_t *dst, const int16_t *src, uint32_t frames,
                         int src_channels)
{
	uint32_t i;
	if (src_channels >= 2) {
		memcpy(dst, src, frames * 2 * sizeof(int16_t));
		return;
	}
	for (i = 0; i < frames; i++) {
		dst[i * 2] = src[i];
		dst[i * 2 + 1] = src[i];
	}
}

static int refill_pending(M4aState *st)
{
	VitaAacDecodedAudio decoded;
	int ret;

	st->pending_frames = 0;
	st->pending_read = 0;
	if (st->eof) return 1;

	for (;;) {
		av_packet_unref(st->packet);
		ret = av_read_frame(st->fmt, st->packet);
		if (ret == AVERROR_EOF) {
			st->eof = 1;
			return 1;
		}
		if (ret < 0) return ret;
		if (st->packet->stream_index != st->stream_index) continue;
		if (!st->packet->data || st->packet->size <= 0) continue;

		ret = vita_hw_aac_decoder_decode(&st->aac, st->packet->data,
		                                 (size_t)st->packet->size, &decoded);
		if (ret < 0) return ret;
		if (ret == 0) continue; /* no PCM this packet */

		if (decoded.frames > M4A_PCM_CAP / 2)
			decoded.frames = M4A_PCM_CAP / 2;
		write_stereo(st->pcm_pending, decoded.pcm, decoded.frames,
		             (int)decoded.channels);
		st->pending_frames = decoded.frames;
		st->pending_read = 0;
		return 0;
	}
}

static int m4a_open(Decoder *dec, const char *filepath)
{
	M4aState *st;
	AVCodecParameters *par;
	uint32_t channels;
	uint32_t sample_rate;
	int ret;

	st = (M4aState *)calloc(1, sizeof(M4aState));
	if (!st) return -1;

	st->stream_index = -1;
	ret = avformat_open_input(&st->fmt, filepath, NULL, NULL);
	if (ret < 0) {
		free(st);
		return -2;
	}
	ret = avformat_find_stream_info(st->fmt, NULL);
	if (ret < 0) {
		avformat_close_input(&st->fmt);
		free(st);
		return -3;
	}

	st->stream_index = find_aac_stream(st->fmt);
	if (st->stream_index < 0) {
		avformat_close_input(&st->fmt);
		free(st);
		return -4;
	}

	par = st->fmt->streams[st->stream_index]->codecpar;
	channels = par->ch_layout.nb_channels > 0
	               ? (uint32_t)par->ch_layout.nb_channels : 0;
	sample_rate = par->sample_rate > 0 ? (uint32_t)par->sample_rate : 0;
	if (channels == 0 || channels > 2 || sample_rate == 0) {
		avformat_close_input(&st->fmt);
		free(st);
		return -5;
	}

	st->packet = av_packet_alloc();
	if (!st->packet) {
		avformat_close_input(&st->fmt);
		free(st);
		return -6;
	}

	/* MP4/M4A usually has AudioSpecificConfig extradata (raw AUs).
	 * Raw .aac ADTS streams have no extradata — tell sceAudiodec. */
	ret = vita_hw_aac_decoder_init_ex(&st->aac, channels, sample_rate,
	                                  par->extradata_size <= 0 ? 1 : 0);
	if (ret < 0) {
		av_packet_free(&st->packet);
		avformat_close_input(&st->fmt);
		free(st);
		return ret;
	}
	st->aac_ready = 1;
	st->channels_src = (int)channels;
	st->sample_rate = (int)sample_rate;

	dec->info.sample_rate = sample_rate;
	dec->info.channels = 2; /* always present stereo to the audio engine */
	dec->info.bit_depth = 16;
	dec->info.bitrate =
	    par->bit_rate > 0 ? (uint32_t)(par->bit_rate / 1000) : 0;
	{
		int64_t dur = st->fmt->duration;
		if (dur <= 0 && st->fmt->streams[st->stream_index]->duration > 0) {
			AVRational tb = st->fmt->streams[st->stream_index]->time_base;
			dur = av_rescale_q(st->fmt->streams[st->stream_index]->duration,
			                   tb, AV_TIME_BASE_Q);
		}
		if (dur > 0) {
			dec->info.duration_ms = (uint64_t)(dur / (AV_TIME_BASE / 1000));
			dec->info.total_frames =
			    (uint64_t)((dec->info.duration_ms * sample_rate) / 1000ULL);
		}
	}

	dec->internal = st;
	dec->vtable = m4a_vtable;
	dec->state = DECODER_STATE_OPEN;
	return 0;
}

static void m4a_close(Decoder *dec)
{
	M4aState *st;
	if (!dec || !dec->internal) return;
	st = (M4aState *)dec->internal;
	if (st->aac_ready) {
		vita_hw_aac_decoder_term(&st->aac);
		st->aac_ready = 0;
	}
	if (st->packet) av_packet_free(&st->packet);
	if (st->fmt) avformat_close_input(&st->fmt);
	free(st);
	dec->internal = NULL;
	dec->state = DECODER_STATE_IDLE;
}

static int m4a_decode_frames(Decoder *dec, int16_t *out, uint32_t frames_req,
                             uint32_t *frames_decoded)
{
	M4aState *st;
	uint32_t written = 0;

	if (frames_decoded) *frames_decoded = 0;
	if (!dec || !dec->internal || !out || frames_req == 0) return -1;
	st = (M4aState *)dec->internal;

	while (written < frames_req) {
		uint32_t avail;
		uint32_t take;
		if (st->pending_read >= st->pending_frames) {
			int r = refill_pending(st);
			if (r == 1) {
				dec->state = DECODER_STATE_EOF;
				if (frames_decoded) *frames_decoded = written;
				return written > 0 ? 0 : 1;
			}
			if (r < 0) {
				dec->state = DECODER_STATE_ERROR;
				return r;
			}
		}
		avail = st->pending_frames - st->pending_read;
		take = frames_req - written;
		if (take > avail) take = avail;
		memcpy(out + written * 2,
		       st->pcm_pending + st->pending_read * 2,
		       take * 2 * sizeof(int16_t));
		st->pending_read += take;
		written += take;
	}

	dec->state = DECODER_STATE_DECODING;
	if (frames_decoded) *frames_decoded = written;
	return 0;
}

static int m4a_seek(Decoder *dec, uint64_t position_ms)
{
	M4aState *st;
	int64_t ts;
	int ret;
	if (!dec || !dec->internal) return -1;
	st = (M4aState *)dec->internal;
	if (st->stream_index < 0 || !st->fmt) return -1;

	ts = (int64_t)position_ms * (AV_TIME_BASE / 1000);
	ret = av_seek_frame(st->fmt, -1, ts, AVSEEK_FLAG_BACKWARD);
	if (ret < 0)
		ret = avformat_seek_file(st->fmt, -1, INT64_MIN, ts, INT64_MAX, 0);
	if (ret < 0) return ret;

	avformat_flush(st->fmt);
	st->pending_frames = 0;
	st->pending_read = 0;
	st->eof = 0;
	dec->state = DECODER_STATE_OPEN;
	return 0;
}

static void m4a_reset(Decoder *dec)
{
	m4a_seek(dec, 0);
}

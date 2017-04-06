#include "opensles_output.h"
#include "opensles_player.h"
#include "opensles_engine.h"
#include "opensles_outputmix.h"
#include "audio_output.h"
#include <string.h>
#include <math.h>

static int opensles_close(void* p)
{
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;
	opensles_player_destroy(player);
	opensles_outputmix_destroy(player);
	opensles_engine_destroy(player);

	if (player->ptr)
	{
		free(player->ptr);
		player->ptr = NULL;
	}
	free(player);
}

static void* opensles_open(int channels, int bits_per_samples, int samples_per_seconds)
{
	int r;
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)malloc(sizeof(*player));
	if (NULL == player)
		return NULL;
	memset(player, 0, sizeof(struct opensles_player_t));
	player->channels = channels;
	player->sample_bits = bits_per_samples;
	player->sample_rate = samples_per_seconds;
	player->bytes_per_sample = player->channels * player->sample_bits / 8;
	player->samples_per_buffer = player->sample_rate * OPENSLES_TIME / 1000;
	player->ptr = (sl_uint8_t*)malloc(OPENSLES_BUFFERS * player->samples_per_buffer * player->bytes_per_sample);

	if (NULL == player->ptr
		|| 0 != opensles_engine_create(player)
		|| 0 != opensles_outputmix_create(player)
		|| 0 != opensles_player_create(player, channels, bits_per_samples, samples_per_seconds))
	{
		opensles_close(player);
		return NULL;
	}

	return player;
}

static int opensles_write(void* p, const void* samples, int count)
{
	int i, free;
	SLresult ret;
	const sl_uint8_t* src;
	struct opensles_player_t* player;
	SLAndroidSimpleBufferQueueState state;

	src = (const sl_uint8_t*)samples;
	player = (struct opensles_player_t*)p;
	ret = (*player->bufferQ)->GetState(player->bufferQ, &state);
	if (SL_RESULT_SUCCESS != ret)
		return ret;

	assert(state.count <= OPENSLES_BUFFERS);
	if (state.count >= OPENSLES_BUFFERS)
		return -1; // no more space to write data

	for (i = 0; i < count && state.count < OPENSLES_BUFFERS; ++state.count)
	{
		free = player->samples_per_buffer - (player->offset % player->samples_per_buffer);
		if (count - i >= free)
		{
			memcpy(player->ptr + player->offset * player->bytes_per_sample, src + i * player->bytes_per_sample, free * player->bytes_per_sample);
			ret = (*player->bufferQ)->Enqueue(player->bufferQ, player->ptr + (player->offset-(player->offset % player->samples_per_buffer)) * player->bytes_per_sample, player->bytes_per_sample * player->samples_per_buffer);
			if (SL_RESULT_SUCCESS != ret)
				return ret > 0 ? -ret : ret;

			i += free;
			player->offset = (player->offset + free) % (player->samples_per_buffer * OPENSLES_BUFFERS);
		}
		else
		{
			memcpy(player->ptr + player->offset * player->bytes_per_sample, src + i * player->bytes_per_sample, (count - i) * player->bytes_per_sample);
			player->offset += count - i;
			i = count;
			assert(player->offset < player->samples_per_buffer * OPENSLES_BUFFERS);
			break;
		}
	}

	return i;
}

static int opensles_play(void* p)
{
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;

	if (player->player)
	{
		return (*player->player)->SetPlayState(player->player, SL_PLAYSTATE_PLAYING);
	}
	return -1;
}

static int opensles_pause(void* p)
{
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;

	if (player->player)
	{
		return (*player->player)->SetPlayState(player->player, SL_PLAYSTATE_PAUSED);
	}
	return -1;
}

static int opensles_flush(void* p)
{
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;

	if (player->bufferQ)
	{
		return (*player->bufferQ)->Clear(player->bufferQ);
	}
	return -1;
}

static int opensles_get_info(void *p, int *channels, int *bits_per_sample, int *samples_per_second)
{
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;
	*channels = player->channels;
	*bits_per_sample = player->sample_bits;
	*samples_per_second = player->sample_rate;
	return 0;
}

static int opensles_get_buffer_size(void* p)
{
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;
	return OPENSLES_BUFFERS * player->samples_per_buffer;
}

static int opensles_get_samples(void* p)
{
	struct opensles_player_t* player;
	SLAndroidSimpleBufferQueueState state;

	player = (struct opensles_player_t*)p;
	if (SL_RESULT_SUCCESS == (*player->bufferQ)->GetState(player->bufferQ, &state))
	{
		return state.count * player->samples_per_buffer;
	}

	return 0;
}

static int opensles_get_volume(void* p, int* volume)
{
	SLresult r;
	SLmillibel level = 0;
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;

	if (player->volume)
	{
		r = (*player->volume)->GetVolumeLevel(player->volume, &level);
		if (r == SL_RESULT_SUCCESS)
		{
			*volume = pow(level * 1.0f, 10) / 20;
			return 0;
		}
	}
	return -1;
}

static int opensles_set_volume(void* p, int volume)
{
	SLmillibel level;
	struct opensles_player_t* player;
	player = (struct opensles_player_t*)p;

	if (player->volume)
	{
		level = 20 * log10(volume / 65535.0f) * 0xFFFF - SL_MILLIBEL_MAX;
		level = level < SL_MILLIBEL_MIN ? SL_MILLIBEL_MIN : level;
		level = level > SL_MILLIBEL_MAX ? SL_MILLIBEL_MAX : level;
		return (*player->volume)->SetVolumeLevel(player->volume, level);
	}
	return -1;
}

int opensles_player_register()
{
	static audio_output_t ao;
	memset(&ao, 0, sizeof(ao));
	ao.open = opensles_open;
	ao.close = opensles_close;
	ao.write = opensles_write;
	ao.play = opensles_play;
	ao.pause = opensles_pause;
	ao.reset = opensles_flush;
	ao.get_info = opensles_get_info;
	ao.get_buffer_size = opensles_get_buffer_size;
	ao.get_available_sample = opensles_get_samples;
	ao.get_volume = opensles_get_volume;
	ao.set_volume = opensles_set_volume;
	return audio_output_register("opensles", &ao);
}
#include <iostream>
#include <assert.h>
#include <iostream>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
//#include <libpostproc/postprocess.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}
#include "SDL2/SDL.h"
#include "SDL2/SDL_thread.h"
#include "SDL2/SDL_syswm.h"
#include "SDL2/SDL_render.h"
#include "SDL2/SDL_audio.h"

#include <vector>

#define SDL_MAIN_HANDLED
#define ERROR_SIZE 128
#define FORMATO AV_PIX_FMT_YUVJ420P
#define SDL_AUDIO_BUFFER_SIZE 1024;
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

struct file_video
{
	std::string filename;
};

struct ffmpeg_video
{
	struct SwrContext *swrCtx = NULL;
	AVCodecContext *pCodecAudioCtx = NULL;
	int videoStream = 0;
	int audioStream = 0;

	//
	AVFormatContext *pFormatCtx = NULL;
	//
	AVCodecParameters *pCodecParameters = NULL;
	AVCodecParameters *pCodecAudioParameters = NULL;
	//
	AVCodec *pCodec = NULL;
	AVCodec *pAudioCodec = NULL;
	AVCodecContext *pCodecCtx = NULL;
};

#define window_size_s 4
struct filter_info
{
	int frame_index = 0;
	uint8_t* data;

	float start_w = 0;
	float end_w = 1;
	float start_h = 0;
	float end_h = 1;

	uint8_t *tmpFrame = new uint8_t[1920 * 1080];
	//AVFrame *pFrameRGB = NULL;

	uint8_t *buffer = (uint8_t *)av_malloc(1920 * 1080 * 3 * sizeof(uint8_t));;
	//uint8_t *tmp_buffer = NULL;
	uint32_t b_width;
	uint32_t b_height;
	uint32_t b_offset;


	uint8_t window_size = window_size_s;
	uint32_t window_values[window_size_s][window_size_s];

	float variance[2][window_size_s*2][window_size_s * 2];
	float pixel_variance[2][window_size_s * 2][window_size_s * 2];
	float avg_value[1920][1080];

	uint64_t frame_n = 0;
};


struct audio_packet
{
	AVPacketList *first, *last;
	int nb_packets, size;
	SDL_mutex *mutex;
	SDL_cond *cond;
};

struct ffmpeg_frame
{
	audio_packet audio_p;
	AVFrame *pFrame = NULL;
	double frame_in_seconds = 0;
};

struct filter_D
{
	file_video info_file;
	ffmpeg_video info_ffmpeg;
	SDL_AudioSpec wanted_spec = { 0 }, audio_spec = { 0 };
	audio_packet audio_p;
	AVFrame wanted_frame;

	ffmpeg_frame frame;
	AVPacket packet;

	filter_info finfo;

};


static int getAudioPacket(audio_packet* q, AVPacket* pkt, int block) {

	AVPacketList* pktl;
	int ret;

	SDL_LockMutex(q->mutex);

	while (1)
	{
		pktl = q->first;
		if (pktl)
		{
			q->first = pktl->next;
			if (!q->first)
				q->last = NULL;

			q->nb_packets--;
			q->size -= pktl->pkt.size;

			*pkt = pktl->pkt;
			av_free(pktl);
			ret = 1;
			break;
		}
		else if (!block)
		{
			ret = 0;
			break;
		}
		else
		{
			SDL_CondWait(q->cond, q->mutex);
		}
	}

	SDL_UnlockMutex(q->mutex);

	return ret;

}
static int audio_decode_frame(AVCodecContext* aCodecCtx, uint8_t* audio_buf, int buf_size, audio_packet& audio_p, AVFrame& wanted_frame) {

	static AVPacket pkt;
	static uint8_t* audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	static AVFrame frame;

	int len1;
	int data_size = 0;

	SwrContext* swr_ctx = NULL;

	while (1)
	{
		while (audio_pkt_size > 0)
		{
			int got_frame = 0;

			avcodec_send_packet(aCodecCtx, &pkt);
			avcodec_receive_frame(aCodecCtx, &frame);

			len1 = frame.pkt_size;
			if (len1 < 0)
			{
				audio_pkt_size = 0;
				break;
			}

			audio_pkt_data += len1;
			audio_pkt_size -= len1;
			data_size = 0;
			if (got_frame)
			{
				int linesize = 1;
				data_size = av_samples_get_buffer_size(&linesize, aCodecCtx->channels, frame.nb_samples, aCodecCtx->sample_fmt, 1);
				assert(data_size <= buf_size);
				memcpy(audio_buf, frame.data[0], data_size);
			}

			if (frame.channels > 0 && frame.channel_layout == 0)
				frame.channel_layout = av_get_default_channel_layout(frame.channels);
			else if (frame.channels == 0 && frame.channel_layout > 0)
				frame.channels = av_get_channel_layout_nb_channels(frame.channel_layout);

			if (swr_ctx)
			{
				swr_free(&swr_ctx);
				swr_ctx = NULL;
			}

			swr_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout, (AVSampleFormat)wanted_frame.format, wanted_frame.sample_rate,
				frame.channel_layout, (AVSampleFormat)frame.format, frame.sample_rate, 0, NULL);

			if (!swr_ctx || swr_init(swr_ctx) < 0)
			{
				std::cout << "swr_init failed" << std::endl;
				break;
			}

			int dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, frame.sample_rate) + frame.nb_samples,
				wanted_frame.sample_rate, wanted_frame.format, AV_ROUND_INF);
			int len2 = swr_convert(swr_ctx, &audio_buf, dst_nb_samples,
				(const uint8_t**)frame.data, frame.nb_samples);
			if (len2 < 0)
			{
				std::cout << "swr_convert failed" << std::endl;
				break;
			}

			return wanted_frame.channels * len2 * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

			if (data_size <= 0)
				continue;

			return data_size;
		}

		if (pkt.data)
			av_packet_unref(&pkt);

		if (getAudioPacket(&audio_p, &pkt, 1) < 0)
			return -1;

		audio_pkt_data = pkt.data;
		audio_pkt_size = pkt.size;
	}

}
//, audio_packet& audio_p, AVFrame& wanted_frame
static void audio_callback(void* userdata, Uint8* stream, int len) {

	AVCodecContext* aCodecCtx = (AVCodecContext*)userdata;
	int len1, audio_size;

	static uint8_t audio_buff[192000 * 3 / 2];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;

	SDL_memset(stream, 0, len);

	while (len > 0)
	{
		if (audio_buf_index >= audio_buf_size)
		{
			audio_size = 0;// audio_decode_frame(aCodecCtx, audio_buff, sizeof(audio_buff), audio_p, wanted_frame);
			if (audio_size < 0)
			{
				audio_buf_size = 1024;
				memset(audio_buff, 0, audio_buf_size);
			}
			else
				audio_buf_size = audio_size;

			audio_buf_index = 0;
		}
		len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;

		SDL_MixAudio(stream, audio_buff + audio_buf_index, len, SDL_MIX_MAXVOLUME);


		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}
}
static void show_error(int erro) {

	char errobuf[ERROR_SIZE];
	av_strerror(erro, errobuf, ERROR_SIZE);
	std::cout << "Error = " << errobuf << std::endl;

}
static void init_audio_packet(audio_packet *q)
{
	q->last = NULL;
	q->first = NULL;
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}
static int init_ffmpeg(const file_video& info_file, ffmpeg_video& info_ffmpeg, SDL_AudioSpec& wantedSpec, SDL_AudioSpec& audioSpec, audio_packet& audio_p, AVFrame& wanted_frame)
{
	av_register_all();

	info_ffmpeg.audioStream = -1;

	//open video
	int res = avformat_open_input(&info_ffmpeg.pFormatCtx, info_file.filename.c_str(), NULL, NULL);

	/*for (int i = 0; i < 1920; i++)
	{
		for (int j = 0; j < 1080; j++)
		{
			variance[0][i][j] = 0;
			variance[1][i][j] = 0;
		}
	}*/

	//check video opened
	if (res != 0) {
		show_error(res);
		exit(-1);
	}

	//get video info
	res = avformat_find_stream_info(info_ffmpeg.pFormatCtx, NULL);
	if (res < 0) {
		show_error(res);
		exit(-1);
	}

	//get video stream
	//info_ffmpeg.videoStream = get_parameters();
	for (unsigned int i = 0; i < info_ffmpeg.pFormatCtx->nb_streams; i++) {
		if (info_ffmpeg.pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) info_ffmpeg.videoStream = i;
		if (info_ffmpeg.pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) info_ffmpeg.audioStream = i;
	}
	if (info_ffmpeg.videoStream == -1) {
		std::cout << "" << std::endl;
		exit(-1);
	}
	info_ffmpeg.pCodecParameters = info_ffmpeg.pFormatCtx->streams[info_ffmpeg.videoStream]->codecpar;
	if (info_ffmpeg.audioStream != -1) info_ffmpeg.pCodecAudioParameters = info_ffmpeg.pFormatCtx->streams[info_ffmpeg.audioStream]->codecpar;

	if (info_ffmpeg.videoStream == -1) {
		std::cout << "Error opening your video using AVCodecParameters, does not have codecpar_type type AVMEDIA_TYPE_VIDEO" << std::endl;
		exit(-1);
	}

	info_ffmpeg.pCodec = avcodec_find_decoder(info_ffmpeg.pCodecParameters->codec_id);
	info_ffmpeg.pAudioCodec = avcodec_find_decoder(info_ffmpeg.pCodecAudioParameters->codec_id);

	if (info_ffmpeg.pCodec == NULL) {
		std::cout << "" << std::endl;
		return -1; // Codec not found
	}

	if (info_ffmpeg.pAudioCodec == NULL) {
		std::cout << "" << std::endl;
		return -1; // Codec not found
	}

	info_ffmpeg.pCodecCtx = avcodec_alloc_context3(info_ffmpeg.pCodec);
	if (info_ffmpeg.pCodecCtx == NULL) {
		std::cout << "" << std::endl;
		exit(-1);
	}

	info_ffmpeg.pCodecAudioCtx = avcodec_alloc_context3(info_ffmpeg.pAudioCodec);
	if (info_ffmpeg.pCodecAudioCtx == NULL) {
		std::cout << "" << std::endl;
		exit(-1);
	}

	res = avcodec_parameters_to_context(info_ffmpeg.pCodecCtx, info_ffmpeg.pCodecParameters);
	if (res < 0) {
		std::cout << "Failed to get video codec" << std::endl;
		avformat_close_input(&info_ffmpeg.pFormatCtx);
		avcodec_free_context(&info_ffmpeg.pCodecCtx);
		exit(-1);
	}
	res = avcodec_parameters_to_context(info_ffmpeg.pCodecAudioCtx, info_ffmpeg.pCodecAudioParameters);

	if (res < 0) {
		std::cout << "Failed to get audio codec" << std::endl;
		avformat_close_input(&info_ffmpeg.pFormatCtx);
		avcodec_free_context(&info_ffmpeg.pCodecCtx);
		avcodec_free_context(&info_ffmpeg.pCodecAudioCtx);
		exit(-1);
	}


	res = avcodec_open2(info_ffmpeg.pCodecCtx, info_ffmpeg.pCodec, NULL);
	if (res < 0) {
		std::cout << "Failed to open video codec" << std::endl;
		exit(-1);
	}
	res = avcodec_open2(info_ffmpeg.pCodecAudioCtx, info_ffmpeg.pAudioCodec, NULL);

	if (res < 0) {
		std::cout << "Failed to open audio codec" << std::endl;
		exit(-1);
	}

	info_ffmpeg.swrCtx = swr_alloc();
	if (info_ffmpeg.swrCtx == NULL) {
		std::cout << "Failed to load audio" << std::endl;
		exit(-1);
	}

	//audio context
	av_opt_set_channel_layout(info_ffmpeg.swrCtx, "in_channel_layout", info_ffmpeg.pCodecAudioCtx->channel_layout, 0);
	av_opt_set_channel_layout(info_ffmpeg.swrCtx, "out_channel_layout", info_ffmpeg.pCodecAudioCtx->channel_layout, 0);
	av_opt_set_int(info_ffmpeg.swrCtx, "in_sample_rate", info_ffmpeg.pCodecAudioCtx->sample_rate, 0);
	av_opt_set_int(info_ffmpeg.swrCtx, "out_sample_rate", info_ffmpeg.pCodecAudioCtx->sample_rate, 0);
	av_opt_set_sample_fmt(info_ffmpeg.swrCtx, "in_sample_fmt", info_ffmpeg.pCodecAudioCtx->sample_fmt, 0);
	av_opt_set_sample_fmt(info_ffmpeg.swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);

	res = swr_init(info_ffmpeg.swrCtx);

	if (res != 0) {
		std::cout << "Failed to initialize audio" << std::endl;
		//exit(-1);
	}

	//SDL Audio Spec
	wantedSpec.channels = info_ffmpeg.pCodecAudioCtx->channels;
	wantedSpec.freq = info_ffmpeg.pCodecAudioCtx->sample_rate;
	wantedSpec.format = AUDIO_S16SYS;
	wantedSpec.silence = 0;
	wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;
	wantedSpec.userdata = info_ffmpeg.pCodecAudioCtx;
	wantedSpec.callback = audio_callback;
	wantedSpec.padding = 0;
	wantedSpec.size = 0;

	if (SDL_OpenAudio(&wantedSpec, &audioSpec) < 0) {
		std::cout << "" << std::endl;
		exit(-1);
	}
	wanted_frame.format = AV_SAMPLE_FMT_S16;
	wanted_frame.sample_rate = audioSpec.freq;
	wanted_frame.channel_layout = av_get_default_channel_layout(audioSpec.channels);
	wanted_frame.channels = audioSpec.channels;

	init_audio_packet(&audio_p);
	SDL_PauseAudio(0);


	AVPixelFormat pixFormat;
	switch (info_ffmpeg.pCodecCtx->pix_fmt)
	{
	case AV_PIX_FMT_YUVJ420P:
		pixFormat = AV_PIX_FMT_YUV420P;
		break;
	case AV_PIX_FMT_YUVJ422P:
		pixFormat = AV_PIX_FMT_YUV422P;
		break;
	case AV_PIX_FMT_YUVJ444P:
		pixFormat = AV_PIX_FMT_YUV444P;
		break;
	case AV_PIX_FMT_YUVJ440P:
		pixFormat = AV_PIX_FMT_YUV440P;
		break;
	default:
		pixFormat = info_ffmpeg.pCodecCtx->pix_fmt;
	}
	// initialize SWS context for software scaling
	SwsContext *swsCtx = sws_getContext(info_ffmpeg.pCodecCtx->width, info_ffmpeg.pCodecCtx->height, pixFormat, info_ffmpeg.pCodecCtx->width, info_ffmpeg.pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
	// change the range of input data by first reading the current color space and then setting it's range as yuvj.
	int dummy[4];
	int srcRange, dstRange;
	int brightness, contrast, saturation;
	sws_getColorspaceDetails(swsCtx, (int**)&dummy, &srcRange, (int**)&dummy, &dstRange, &brightness, &contrast, &saturation);
	const int* coefs = sws_getCoefficients(SWS_CS_DEFAULT);
	srcRange = 1; // this marks that values are according to yuvj
	sws_setColorspaceDetails(swsCtx, coefs, srcRange, coefs, dstRange,
		brightness, contrast, saturation);
}
static int init_frame(ffmpeg_frame& frame)
{
	frame.pFrame = av_frame_alloc();
	if (frame.pFrame == NULL) {
		std::cout << "" << std::endl;
		return -1;
	}



	
}
static int putAudioPacket(audio_packet *q, AVPacket *pkt)
{
	AVPacketList *pktl;
	AVPacket *newPkt;
	newPkt = (AVPacket*)av_mallocz_array(1, sizeof(AVPacket));
	if (av_packet_ref(newPkt, pkt) < 0)
		return -1;

	pktl = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!pktl)
		return -1;

	pktl->pkt = *newPkt;
	pktl->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last)
		q->first = pktl;
	else
		q->last->next = pktl;

	q->last = pktl;

	q->nb_packets++;
	q->size += newPkt->size;

	SDL_CondSignal(q->cond);
	SDL_UnlockMutex(q->mutex);

	return 0;
}
static int ffmpeg_step(ffmpeg_video& info_ffmpeg, ffmpeg_frame& frame, AVPacket& packet, audio_packet& audio_p)
{
	for (int i = 0; i < info_ffmpeg.pFormatCtx->nb_streams; i++)
	{
		if (info_ffmpeg.pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			frame.frame_in_seconds = 1 / av_q2d(info_ffmpeg.pFormatCtx->streams[i]->r_frame_rate);
			break;
		}
	}

	if (av_read_frame(info_ffmpeg.pFormatCtx, &packet) >= 0) {


		//if (frame_index > 2000) break;
		while (packet.stream_index == info_ffmpeg.audioStream) {
			putAudioPacket(&audio_p, &packet);
			if (av_read_frame(info_ffmpeg.pFormatCtx, &packet) < 0)
			{
				return 2;
			}
		}

		if (packet.stream_index == info_ffmpeg.videoStream) {


			int res = avcodec_send_packet(info_ffmpeg.pCodecCtx, &packet);
			if (res < 0) {
				show_error(res);
				return -1;
			}

			res = avcodec_receive_frame(info_ffmpeg.pCodecCtx, frame.pFrame);
			if (res < 0) {
				//show_error(res);
				return -2;
			}
		}

		return 2;
	}
	else
	{
		return 1;

	}
	/*uint64_t flying_frame_counter = 0;
	std::ofstream fd_output_file("f_d_" + info_file.filename + ".txt");
	std::ofstream time_output_file("f_t_" + info_file.filename + ".txt");
	for (int i = 0; i < diffs.size(); ++i)
	{
		fd_output_file << diffs[i] << std::endl;
		if (diffs[i] > 999999999) flying_frame_counter++;
	}

	float flying_time_in_seconds = (float)flying_frame_counter * (float)frame.frame_in_seconds;
	time_output_file << flying_time_in_seconds << std::endl;
	std::cout << flying_time_in_seconds << std::endl;
	fd_output_file.close();
	time_output_file.close();
	return 1;*/
}
static int filter_step(filter_info& finfo, std::vector<uint64_t>& diffs)
{
	finfo.frame_index++;

	int actual_index = finfo.frame_index % 2;
	int prev_index = (finfo.frame_index + 1) % 2;

	for (int i = 0; i < finfo.b_height; i++)
	{
		for (int j = 0; j < finfo.b_width; j++)
		{
			finfo.tmpFrame[i * finfo.b_width + j] = finfo.data[i * finfo.b_width + j];
		}
	}
	//uint64_t number_of_frames = info_ffmpeg.pFormatCtx->streams[info_ffmpeg.videoStream]->nb_frames;
	uint64_t accumulation = 0;
	uint64_t frame_diff = 0;
	//diffs.resize(number_of_frames);

	/**/


	for (int i = finfo.b_height*finfo.start_h + finfo.window_size; i < finfo.b_height*finfo.end_h - finfo.window_size; i++)
	{
		accumulation = 0;
		if (finfo.frame_index > 1)
		{
			//for (int ie = i - finfo.window_size; ie <= i + finfo.window_size; ie++)
			for (int ie = 0; ie <= window_size_s * 2; ie++)
			{
				for (int je = 0; je <= window_size_s * 2; je++)
				{
					/*if (ie == info_ffmpeg.pCodecCtx->height*start_h && )
					{

					}*/
					uint64_t tmp = std::abs((int)finfo.buffer[(int)(ie+ i - finfo.window_size)* finfo.b_width + (int)(je + finfo.b_width*finfo.start_w)] -
						(int)finfo.tmpFrame[(int)(ie+ i - finfo.window_size)* finfo.b_width + (int)(je + finfo.b_width*finfo.start_w)]);
					//uint64_t tmp = std::abs((int)prevFrame->data[0][(int)(i + info_ffmpeg.pCodecCtx->height*start_h) * prevFrame->linesize[0] + (int)(j + info_ffmpeg.pCodecCtx->width*start_w)]
					//								- (int)pFrame->data[0][(int)(i + info_ffmpeg.pCodecCtx->height*start_h) * b_width + (int)(j + info_ffmpeg.pCodecCtx->width*start_w)]);

					if (tmp > 0)
					{
						finfo.pixel_variance[actual_index][(int)(je )][ie] = finfo.pixel_variance[prev_index][(int)(je )][ie] + 1;// = pixel_diff;
						finfo.variance[actual_index][(int)(je)][ie] = finfo.variance[prev_index][(int)(je )][ie] + tmp;
						//ocurrences[j][i]++;
					}

					if ((finfo.pixel_variance[prev_index][(int)(je )][ie] / (float)(finfo.frame_n + 1)) > 0.5 && (finfo.variance[prev_index][(int)(je )][ie] / (float)(finfo.frame_n + 1)) * 6 > tmp) tmp = 0;

					accumulation += tmp;

				}
			}
		}
		for (int j = finfo.b_width*finfo.start_w + finfo.window_size; j < finfo.b_width * finfo.end_w - finfo.window_size; j++)
		{
			if (finfo.frame_index > 1)
			{
				int64_t pixel_diff = 0;
				uint8_t neig = 0;
				uint64_t window_pix_diff = 0;

				uint32_t wwi, wj;

				wwi = (i - (uint32_t)(finfo.b_height*finfo.start_h + finfo.window_size)) % finfo.window_size;
				wj = (j - (uint32_t)(finfo.b_width*finfo.start_w + finfo.window_size)) % finfo.window_size;
				
				finfo.avg_value[j][i] = finfo.avg_value[j][i] * finfo.frame_n + (float)finfo.data[i * finfo.b_width + j];
				finfo.avg_value[j][i] /= (finfo.frame_n + 1);

				pixel_diff = std::abs((int)finfo.avg_value[j][i] - (int)finfo.data[i * finfo.b_width + j]);
				pixel_diff = std::abs((int)finfo.buffer[i * finfo.b_width + j] - (int)finfo.data[i * finfo.b_width + j]);

				//pixel_diff = (pixel_diff + pixel_diff2) / 2;

				if (pixel_diff > 0)
				{
					//pixel_variance[j][i]++;// = pixel_diff;
					//variance[j][i] +=pixel_diff;
										   //ocurrences[j][i]++;
				}

				if ((finfo.pixel_variance[prev_index][wj][wwi] / (float)(finfo.frame_n + 1)) > 0.5 && (finfo.variance[prev_index][wj][wwi] / (float)(finfo.frame_n + 1)) * 7 > pixel_diff) pixel_diff = 0;


				//pixel_diff -= (variance[j][i] / (float)ocurrences[j][i]);

				//if (pixel_diff < 0) pixel_diff = 0;
				float new_pixel_diff = std::abs(pixel_diff - (finfo.pixel_variance[prev_index][wj][wwi] / (float)(finfo.frame_n + 1)));



				if (j > finfo.b_width*finfo.start_w + finfo.window_size)
				{
					//if (pixel_diff < 10) pixel_diff = 0;
					for (int wi = i - finfo.window_size; wi <= i + finfo.window_size; wi++)
					{
						//for (int wj = j - window_size; wj <= j + window_size; wj++)
						{
							int new_wj = j + finfo.window_size;
							int old_wj = j - finfo.window_size - 1;

							uint32_t owj, nwj;

							owj = (new_wj - (uint32_t)(finfo.b_width*finfo.start_w + finfo.window_size)) % finfo.window_size;
							nwj = (old_wj - (uint32_t)(finfo.b_width*finfo.start_w + finfo.window_size)) % finfo.window_size;

							uint32_t wwi;
							wwi = (wi - (uint32_t)(finfo.b_height*finfo.start_h + finfo.window_size)) % finfo.window_size;

							float new_wij_var = (finfo.pixel_variance[prev_index][nwj][wwi] / (float)(finfo.frame_n + 1));
							float old_wij_var = (finfo.pixel_variance[prev_index][owj][wwi] / (float)(finfo.frame_n + 1));
							//if (wi != i || wj != j)
							{
								uint32_t dist_pix = finfo.window_size - std::abs(wi - i) + finfo.window_size - std::abs(new_wj - j);

								int64_t tmp_p = std::abs((int)finfo.buffer[wi * finfo.b_width + new_wj] - (int)finfo.tmpFrame[wi * finfo.b_width + new_wj]);
								int64_t old_tmp_p = std::abs((int)finfo.buffer[wi * finfo.b_width + old_wj] - (int)finfo.tmpFrame[wi * finfo.b_width + old_wj]);

								if (tmp_p > 0)
								{
									finfo.pixel_variance[actual_index][nwj][wwi] = finfo.pixel_variance[prev_index][nwj][wwi] + 1;// = pixel_diff;
									finfo.variance[actual_index][nwj][wwi] = finfo.variance[prev_index][nwj][wwi] + tmp_p;
									//ocurrences[j][i]++;
								}

								/*if (old_tmp_p > 0)
								{
									pixel_variance[old_wj][wi]++;// = pixel_diff;
									variance[old_wj][wi] += old_tmp_p;
									//ocurrences[j][i]++;
								}*/

								if ((finfo.pixel_variance[prev_index][nwj][wwi] / (float)(finfo.frame_n + 1)) > 0.5 && (finfo.variance[prev_index][nwj][wwi] / (float)(finfo.frame_n + 1)) * 6 > tmp_p) tmp_p = 0;
								if ((finfo.pixel_variance[prev_index][owj][wwi] / (float)(finfo.frame_n + 1)) > 0.5 && (finfo.variance[prev_index][owj][wwi] / (float)(finfo.frame_n + 1)) * 6 > old_tmp_p) old_tmp_p = 0;

								accumulation += tmp_p;

								if (old_tmp_p > accumulation)
								{
									int a = 42;
								}

								accumulation -= old_tmp_p;

								//Update center value



								//if ((pixel_variance[wj][wi] / (float)(finfo.frame_n + 1)) > 0.5 && (variance[wj][wi] / (float)(finfo.frame_n + 1)) * 7 > tmp_p) tmp_p = 0;
								//if (tmp_p < 255) tmp_p = 0;
								if (tmp_p >= 255)
								{
									//tmp_p = 1;
								}
								window_pix_diff += tmp_p;//std::pow(tmp_p, dist_pix); //dist_pix * tmp_p;// std::pow(2, dist_pix);
								//if (tmp_p > 120) neig++;
							}
						}
					}/**/
				}

				//variance[j][i] += window_pix_diff;
				//ocurrences[j][i]++;

				//if (neig > 6)
				{
					float new_diff = accumulation;// pow(window_pix_diff, 2) * pow(2, neig);
					if (frame_diff < new_diff)
					{
						frame_diff = new_diff;
					}
				}
				float nnp = pixel_diff;// std::abs(pixel_diff - (pixel_variance[j][i] / (float)(finfo.frame_n + 1)));
				if ((finfo.pixel_variance[prev_index][j][i] / (float)(finfo.frame_n + 1)) > 0.5 && (finfo.variance[prev_index][j][i] / (float)(finfo.frame_n + 1)) * 7 > pixel_diff) nnp = 0;
				//frame_diff += nnp;
				finfo.data[i * finfo.b_width + j] = nnp * 5;
			}
		}
	}/**/
	diffs.push_back(frame_diff);
	std::cout << frame_diff << std::endl;
	/*for (int i = 0; i < info_ffmpeg.pCodecCtx->height; i++)
	{
		for (int j = 0; j < info_ffmpeg.pCodecCtx->width; j++)
		{
			frame.pFrame->data[0][i * b_width + j] = tmframe.pFrame[i * b_width + j];
		}
	}/**/
	finfo.frame_n++;
	//if (video_output)
	{
		/*SDL_UpdateYUVTexture(bmp, NULL, pFrame->data[0], b_width,
			pFrame->data[1], pFrame->linesize[1],
			pFrame->data[2], pFrame->linesize[2]);*/
	}
	//*prevFrame = *pFrame;

	for (int i = 0; i < finfo.b_height; i++)
	{
		for (int j = 0; j < finfo.b_width; j++)
		{
			finfo.data[i * finfo.b_width + j] = finfo.tmpFrame[i * finfo.b_width + j];

			finfo.buffer[i * finfo.b_width + j] = finfo.tmpFrame[i * finfo.b_width + j];
		}
	}

	/*if (video_output)
	{
		SDL_RenderCopy(renderer, bmp, NULL, NULL);
		SDL_RenderPresent(renderer);
		SDL_UpdateWindowSurface(screen);
		SDL_Delay(1000 / 30);
	}*/
	//}
	/*if (video_output)
	{
		SDL_PollEvent(&evt);
	}*/

	return 2;

	//f_d = frame_diff
	//f_t = flying_time


}

#include <iostream>
#include <assert.h>

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

#define SDL_MAIN_HANDLED
#define ERROR_SIZE 128
#define FORMATO AV_PIX_FMT_RGB24
#define SDL_AUDIO_BUFFER_SIZE 1024;
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

typedef struct _AudioPacket
	{
		AVPacketList *first, *last;
		int nb_packets, size;
  		SDL_mutex *mutex;
  		SDL_cond *cond;
	} AudioPacket;

class Player {

public:
	
	float start_w = 0;
	float end_w = 1;
	float start_h = 0;
	float end_h = 1;

	std::string filename;

	bool video_output;

	float variance[2][1920][1080];
	float pixel_variance[2][1920][1080];
	float avg_value[1920][1080];
	uint32_t ocurrences[1920][1080];
	uint64_t frame_n = 0;

	uint64_t accumulation = 0;
	uint64_t center_value = 0;

#define window_size_s 4
	uint8_t window_size = window_size_s;
	uint32_t window_values[window_size_s][window_size_s];

	Player(std::string afilename, float astart_w, float aend_w, float astart_h, float aend_h, bool avo) : video_output(avo), start_w(astart_w), end_w(aend_w), start_h(astart_h), end_h(aend_h) {

		audioStream = -1;

		//init ffmpeg
		av_register_all();

		filename = afilename;

		//open video
		int res = avformat_open_input(&pFormatCtx, filename.c_str(), NULL, NULL);

		for (int i = 0; i < 1920; i++)
		{
			for (int j = 0; j < 1080; j++)
			{
				variance[0][i][j] = 0;
				variance[1][i][j] = 0;
			}
		}

		//check video opened
		if (res!=0){
			show_error(res);
			exit(-1);
		}

		//get video info
		res = avformat_find_stream_info(pFormatCtx, NULL);
		if (res < 0) {
			show_error(res);
			exit(-1);
		}

		//get video stream
		videoStream = get_parameters();
		if (videoStream == -1) {
			std::cout << "Error opening your video using AVCodecParameters, does not have codecpar_type type AVMEDIA_TYPE_VIDEO" << std::endl;
			exit(-1);
		}

		if (lerCodecVideo() < 0) exit(-1);

	}

	~Player(void) {

		av_free(buffer);
		av_free(pFrameRGB);

		// Free the YUV frame
		av_free(pFrame);

		// Close the codecs
		avcodec_close(pCodecCtx);

		// Close the video file
		avformat_close_input(&pFormatCtx);

	}

	
	void video_info(void);
	int alloc_memory(void);
	int lerFramesVideo(void);
	int create_window(void);
	
	static int getAudioPacket(AudioPacket*, AVPacket*, int);

private:
	
	void memsetAudioPacket(AudioPacket * pq);
	int videoStream;

	int audioStream;

	AVFormatContext *pFormatCtx = NULL;

	AVCodecParameters *pCodecParameters = NULL;

	AVCodecParameters *pCodecAudioParameters = NULL;

	AVCodecContext *pCodecCtx = NULL;

	AVCodecContext *pCodecAudioCtx = NULL;

	SDL_AudioSpec wantedSpec = { 0 }, audioSpec = { 0 };

	AVCodec *pCodec = NULL;

	AVCodec *pAudioCodec = NULL;

	AVFrame *pFrame = NULL;
	AVFrame *prevFrame = NULL;
	uint8_t *tmpFrame = NULL;
	AVFrame *pFrameRGB = NULL;

	uint8_t *buffer = NULL;

	struct SwsContext *sws_ctx = NULL;

	SDL_Window *screen;

	SDL_Renderer *renderer;

	SDL_Texture* bmp;
	
	void show_error(int erro);

	int get_parameters(void);

	int lerCodecVideo(void);

	
	void initAudioPacket(AudioPacket *); 

	int putAudioPacket(AudioPacket *, AVPacket *); 

};
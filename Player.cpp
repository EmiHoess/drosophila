#include "Player.hpp"
#include <iostream>
#include <fstream>
#include <vector>


using namespace std;


struct SwrContext *swrCtx = NULL;
AVFrame wanted_frame;

AudioPacket audioq;
void audio_callback(void*, Uint8*, int);

void Player::video_info(void) {

	av_dump_format(pFormatCtx, 0, pFormatCtx->filename, 0);

}

void Player::show_error(int erro) {

	char errobuf[ERROR_SIZE];
	av_strerror(erro, errobuf, ERROR_SIZE);
	cout << "Error = " << errobuf<<endl;

}

int Player::get_parameters(void) {


	int videoStream = -1;
	for (unsigned int i = 0; i<pFormatCtx->nb_streams; i++){
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) videoStream = i;
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) audioStream = i;
	}
	if (videoStream == -1) {
		cout << "" << endl;
		return -1;
	}
	pCodecParameters = pFormatCtx->streams[videoStream]->codecpar;
	if(audioStream != -1) pCodecAudioParameters = pFormatCtx->streams[audioStream]->codecpar;
	return videoStream;

}

int Player::lerCodecVideo(void) {


	pCodec = avcodec_find_decoder(pCodecParameters->codec_id);
	pAudioCodec = avcodec_find_decoder(pCodecAudioParameters->codec_id);

	if (pCodec == NULL) {
		cout << "" << endl;
		return -1; // Codec not found
	}

	if (pAudioCodec == NULL) {
		cout << "" << endl;
		return -1; // Codec not found
	}

	pCodecCtx = avcodec_alloc_context3(pCodec);
	if(pCodecCtx == NULL){
		cout<<""<<endl;
		exit(-1);
	}

	pCodecAudioCtx = avcodec_alloc_context3(pAudioCodec);
	if(pCodecAudioCtx == NULL){
		cout<<""<<endl;
		exit(-1);
	}

	int res = avcodec_parameters_to_context(pCodecCtx, pCodecParameters);
	if(res < 0){
		cout<<"Failed to get video codec"<<endl;
		avformat_close_input(&pFormatCtx);
		avcodec_free_context(&pCodecCtx);
		exit(-1);
	}
	res = avcodec_parameters_to_context(pCodecAudioCtx, pCodecAudioParameters);

	if (res < 0) {
		cout<<"Failed to get audio codec"<<endl;
		avformat_close_input(&pFormatCtx);
		avcodec_free_context(&pCodecCtx);
		avcodec_free_context(&pCodecAudioCtx);
		exit(-1);
	}


	res = avcodec_open2(pCodecCtx, pCodec, NULL);
	if(res < 0){
		cout<<"Failed to open video codec"<<endl;
		exit(-1);
	}
	res = avcodec_open2(pCodecAudioCtx, pAudioCodec, NULL);

	if (res < 0) {
		cout<<"Failed to open audio codec"<<endl;
		exit(-1);
	}
	return 1;
}

int Player::alloc_memory(void) {

	swrCtx = swr_alloc();
	if(swrCtx == NULL){
		cout<<"Failed to load audio"<<endl;
		exit(-1);
	}

	//audio context
	av_opt_set_channel_layout(swrCtx, "in_channel_layout", pCodecAudioCtx->channel_layout, 0);
	av_opt_set_channel_layout(swrCtx, "out_channel_layout", pCodecAudioCtx->channel_layout, 0);
	av_opt_set_int(swrCtx, "in_sample_rate", pCodecAudioCtx->sample_rate, 0);
	av_opt_set_int(swrCtx, "out_sample_rate", pCodecAudioCtx->sample_rate, 0);
	av_opt_set_sample_fmt(swrCtx, "in_sample_fmt", pCodecAudioCtx->sample_fmt, 0);
	av_opt_set_sample_fmt(swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
	
	int res = swr_init(swrCtx);

	if(res != 0){
		cout<<"Failed to initialize audio"<<endl;
		//exit(-1);
	}

	memset(&wantedSpec, 0, sizeof(wantedSpec));
	wantedSpec.channels = pCodecAudioCtx->channels;
	wantedSpec.freq = pCodecAudioCtx->sample_rate;
	wantedSpec.format = AUDIO_S16SYS;
	wantedSpec.silence = 0;
	wantedSpec.samples = SDL_AUDIO_BUFFER_SIZE;
	wantedSpec.userdata = pCodecAudioCtx;
	wantedSpec.callback = audio_callback;
	
	if (SDL_OpenAudio(&wantedSpec, &audioSpec) < 0) {
		cout<<""<<endl;
		exit(-1);
	}
	wanted_frame.format = AV_SAMPLE_FMT_S16;	
	wanted_frame.sample_rate = audioSpec.freq;
	wanted_frame.channel_layout = av_get_default_channel_layout(audioSpec.channels);
	wanted_frame.channels = audioSpec.channels;
	
	initAudioPacket(&audioq);
	SDL_PauseAudio(0);

	pFrame = av_frame_alloc();
	if (pFrame == NULL) {
		cout << "" << endl;
		return -1;
	}

	prevFrame = av_frame_alloc();
	tmpFrame = new uint8_t[1920 * 1080];

	pFrameRGB = av_frame_alloc();
	if (pFrameRGB == NULL) {
		cout << "" << endl;
		return -1;
	}


	int numBytes = av_image_get_buffer_size(FORMATO, pCodecCtx->width, pCodecCtx->height,1);
	cout << "qtd bytes="<<numBytes << endl;


	buffer = (uint8_t *)av_malloc(numBytes*sizeof(uint8_t));


	res = av_image_fill_arrays(pFrameRGB->data, pFrameRGB->linesize, buffer, FORMATO, pCodecCtx->width, pCodecCtx->height, 1);
	if (res < 0) {
		show_error(res);
		return res;
	}
	return 1;
}

void Player::initAudioPacket(AudioPacket *q) 
{
    q->last = NULL;
    q->first = NULL;
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int Player::putAudioPacket(AudioPacket *q, AVPacket *pkt) 
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

int Player::getAudioPacket(AudioPacket* q, AVPacket* pkt, int block){

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

int audio_decode_frame(AVCodecContext* aCodecCtx, uint8_t* audio_buf, int buf_size){

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

            swr_ctx = swr_alloc_set_opts(NULL, wanted_frame.channel_layout, (AVSampleFormat) wanted_frame.format, wanted_frame.sample_rate,
                frame.channel_layout, (AVSampleFormat) frame.format, frame.sample_rate, 0, NULL);

            if (!swr_ctx || swr_init(swr_ctx) < 0)
            {
                cout<<"swr_init failed"<<endl;
                break;
            }

            int dst_nb_samples = (int)av_rescale_rnd(swr_get_delay(swr_ctx, frame.sample_rate) + frame.nb_samples,
                wanted_frame.sample_rate, wanted_frame.format, AV_ROUND_INF);
            int len2 = swr_convert(swr_ctx, &audio_buf, dst_nb_samples,
                (const uint8_t**)frame.data, frame.nb_samples);
            if (len2 < 0)
            {
                cout<<"swr_convert failed"<<endl;
                break;
            }

            return wanted_frame.channels * len2 * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);

            if (data_size <= 0)
                continue;

            return data_size;
        }

        if (pkt.data)
            av_packet_unref(&pkt);

        if (Player::getAudioPacket(&audioq, &pkt, 1) < 0)
            return -1;

        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }

}

void audio_callback(void* userdata, Uint8* stream, int len){
	
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
            audio_size = audio_decode_frame(aCodecCtx, audio_buff, sizeof(audio_buff));
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

int Player::lerFramesVideo(void) {

	AVPacket packet;

	sws_ctx = sws_getContext(pCodecCtx->width,
		pCodecCtx->height,
		pCodecCtx->pix_fmt,
		pCodecCtx->width,
		pCodecCtx->height,
		FORMATO,
		SWS_BILINEAR,
		NULL,
		NULL,
		NULL
		);
	SDL_Event evt;
	int ret = 0;
	uint64_t total_diff = 0;
	uint8_t pixel_diff = 0;
	uint64_t frame_diff = 0;
	int frame_index = 0;
	std::vector<uint64_t> diffs;

	while (av_read_frame(pFormatCtx, &packet) >= 0) {

		frame_index++;
		//if (frame_index > 2000) break;
		if (packet.stream_index == audioStream) {
			putAudioPacket(&audioq, &packet);
		}

		if (packet.stream_index == videoStream) {


			int res = avcodec_send_packet(pCodecCtx, &packet);
			if (res < 0) {
				show_error(res);
				continue;
			}

			res = avcodec_receive_frame(pCodecCtx, pFrame);
			if (res < 0) {
				//show_error(res);
				continue;
			}
			

			for (int i = 0; i < pCodecCtx->height; i++)
			{
				for (int j = 0; j < pCodecCtx->width; j++)
				{
					tmpFrame[i * pFrame->linesize[0] + j] = pFrame->data[0][i * pFrame->linesize[0] + j];
				}
			}
			uint64_t number_of_frames = pFormatCtx->streams[videoStream]->nb_frames;
			//diffs.resize(number_of_frames);
			frame_diff = 0;
			for (int i = pCodecCtx->height*start_h; i < pCodecCtx->height*end_h; i++)
			{
				for (int j = pCodecCtx->width*start_w; j < pCodecCtx->width * end_w; j++)
				{
					if (frame_index > 1)
					{
						pixel_diff = std::abs((int)prevFrame->data[0][i * pFrame->linesize[0] + j] - (int)pFrame->data[0][i * pFrame->linesize[0] + j]);
						
						if (pixel_diff < 30) pixel_diff = 0;
						frame_diff += pixel_diff;
						pFrame->data[0][i * pFrame->linesize[0] + j] = pixel_diff*5;
					}

				}
			}/**/
			diffs.push_back(frame_diff);
			//std::cout << frame_diff << std::endl;
			/*for (int i = 0; i < pCodecCtx->height; i++)
			{
				for (int j = 0; j < pCodecCtx->width; j++)
				{
					pFrame->data[0][i * pFrame->linesize[0] + j] = tmpFrame[i * pFrame->linesize[0] + j];
				}
			}/**/

			if (video_output)
			{
				SDL_UpdateYUVTexture(bmp, NULL, pFrame->data[0], pFrame->linesize[0],
					pFrame->data[1], pFrame->linesize[1],
					pFrame->data[2], pFrame->linesize[2]);
			}
				*prevFrame = *pFrame;

				for (int i = 0; i < pCodecCtx->height; i++)
				{
					for (int j = 0; j < pCodecCtx->width; j++)
					{
						prevFrame->data[0][i * pFrame->linesize[0] + j] = tmpFrame[i * pFrame->linesize[0] + j];
					}
				}
			
			if (video_output)
			{
				SDL_RenderCopy(renderer, bmp, NULL, NULL);
				SDL_RenderPresent(renderer);
				SDL_UpdateWindowSurface(screen);
				SDL_Delay(1000 / 30);
			}
		}
		if (video_output)
		{
			SDL_PollEvent(&evt);
		}
	}
	//f_d = frame_diff
	//f_t = flying_time
	uint64_t flying_frame_counter = 0;
	std::ofstream fd_output_file("f_d_"+filename+".txt");
	std::ofstream time_output_file("f_t_"+filename+".txt");
	for (int i = 0; i < diffs.size(); ++i)
	{
		fd_output_file << diffs[i] << std::endl;
		if (diffs[i] > 500) flying_frame_counter++;
	}

	float flying_time_in_seconds = (float)flying_frame_counter * (float)pCodecCtx->bits_per_coded_sample;
	time_output_file << flying_time_in_seconds << std::endl;
	std::cout << flying_time_in_seconds << std::endl;
	fd_output_file.close();
	time_output_file.close();
	return 1;

}

int Player::create_window(void) {

	if (video_output)
	{

		screen = SDL_CreateWindow("Pretty Fly",
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			pCodecCtx->width * 0.5f, pCodecCtx->height * 0.5f,
			SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

		if (!screen) {
			cout << "" << endl;
			return -1;
		}

		renderer = SDL_CreateRenderer(screen, -1, 0);

		bmp = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_YV12, SDL_TEXTUREACCESS_STATIC, pCodecCtx->width, pCodecCtx->height);
	}
	return 1;
}
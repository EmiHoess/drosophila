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
	int64_t pixel_diff = 0;
	int64_t pixel_diff2 = 0;
	uint64_t frame_diff = 0;
	int frame_index = 0;
	std::vector<uint64_t> diffs;

	uint64_t expp = 2;

	double frame_in_seconds = 0;

	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			frame_in_seconds = 1/av_q2d(pFormatCtx->streams[i]->r_frame_rate);
			break;
		}
	}

	while (av_read_frame(pFormatCtx, &packet) >= 0) {

		
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
			frame_index++;

			int actual_index = frame_index % 2;
			int prev_index = (frame_index + 1) % 2;

			for (int i = 0; i < pCodecCtx->height; i++)
			{
				for (int j = 0; j < pCodecCtx->width; j++)
				{
					tmpFrame[i * pFrame->linesize[0] + j] = pFrame->data[0][i * pFrame->linesize[0] + j];
				}
			}
			uint64_t number_of_frames = pFormatCtx->streams[videoStream]->nb_frames;
			accumulation = 0;
			//diffs.resize(number_of_frames);

			/**/
			
			frame_diff = 0;
			for (int i = pCodecCtx->height*start_h + window_size; i < pCodecCtx->height*end_h - window_size; i++)
			{
				accumulation = 0;
				if (frame_index > 1)
				{
					for (int ie = i - window_size; ie <= i + window_size; ie++)
					{
						for (int je = 0; je <= window_size_s * 2; je++)
						{
							/*if (ie == pCodecCtx->height*start_h && )
							{

							}*/
							uint64_t tmp = std::abs((int)prevFrame->data[0][(int)(ie ) * pFrame->linesize[0] + (int)(je + pCodecCtx->width*start_w)] -
								(int)tmpFrame[(int)(ie ) * pFrame->linesize[0] + (int)(je + pCodecCtx->width*start_w)]);
							//uint64_t tmp = std::abs((int)prevFrame->data[0][(int)(i + pCodecCtx->height*start_h) * prevFrame->linesize[0] + (int)(j + pCodecCtx->width*start_w)]
							//								- (int)pFrame->data[0][(int)(i + pCodecCtx->height*start_h) * pFrame->linesize[0] + (int)(j + pCodecCtx->width*start_w)]);

							if (tmp > 0)
							{
								pixel_variance[actual_index][(int)(je + pCodecCtx->width*start_w)][ie] = pixel_variance[prev_index][(int)(je + pCodecCtx->width*start_w)][ie]+1;// = pixel_diff;
								variance[actual_index][(int)(je + pCodecCtx->width*start_w)][ie] = variance[prev_index][(int)(je + pCodecCtx->width*start_w)][ie] + tmp;
								//ocurrences[j][i]++;
							}

							if ((pixel_variance[prev_index][(int)(je + pCodecCtx->width*start_w)][ie] / (float)(frame_n + 1)) > 0.5 && (variance[prev_index][(int)(je + pCodecCtx->width*start_w)][ie] / (float)(frame_n + 1)) * 6 > tmp) tmp = 0;

							accumulation += tmp;

						}
					}
				}
				for (int j = pCodecCtx->width*start_w + window_size; j < pCodecCtx->width * end_w - window_size; j++)
				{
					if (frame_index > 1)
					{
						pixel_diff = 0;
						uint8_t neig = 0;
						uint64_t window_pix_diff = 0;

						avg_value[j][i] = avg_value[j][i] * frame_n + (float)pFrame->data[0][i * pFrame->linesize[0] + j];
						avg_value[j][i] /= (frame_n + 1);

						pixel_diff = std::abs((int)avg_value[j][i] - (int)pFrame->data[0][i * pFrame->linesize[0] + j]);
						pixel_diff = std::abs((int)prevFrame->data[0][i * prevFrame->linesize[0] + j] - (int)pFrame->data[0][i * pFrame->linesize[0] + j]);
						
						//pixel_diff = (pixel_diff + pixel_diff2) / 2;

						if (pixel_diff > 0) 
						{
							//pixel_variance[j][i]++;// = pixel_diff;
							//variance[j][i] +=pixel_diff;
												   //ocurrences[j][i]++;
						}

						if ((pixel_variance[prev_index][j][i] / (float)(frame_n + 1)) > 0.5 && (variance[prev_index][j][i] / (float)(frame_n + 1)) * 7 > pixel_diff) pixel_diff = 0;
						

						//pixel_diff -= (variance[j][i] / (float)ocurrences[j][i]);

						//if (pixel_diff < 0) pixel_diff = 0;
						//float new_pixel_diff = std::abs(pixel_diff - (pixel_variance[prev_index][j][i] / (float)(frame_n + 1)));

						

						if (j > pCodecCtx->width*start_w + window_size)
						{
							//if (pixel_diff < 10) pixel_diff = 0;
							for (int wi = i - window_size; wi <= i + window_size; wi++)
							{
								//for (int wj = j - window_size; wj <= j + window_size; wj++)
								{
									int new_wj = j + window_size;
									int old_wj = j - window_size - 1;


									float new_wij_var = (pixel_variance[prev_index][new_wj][wi] / (float)(frame_n + 1));
									float old_wij_var = (pixel_variance[prev_index][old_wj][wi] / (float)(frame_n + 1));
									//if (wi != i || wj != j)
									{
										uint32_t dist_pix = window_size - std::abs(wi - i) + window_size - std::abs(new_wj - j);

										int64_t tmp_p = std::abs((int)prevFrame->data[0][wi * pFrame->linesize[0] + new_wj] - (int)tmpFrame[wi * pFrame->linesize[0] + new_wj]);
										int64_t old_tmp_p = std::abs((int)prevFrame->data[0][wi * pFrame->linesize[0] + old_wj] - (int)tmpFrame[wi * pFrame->linesize[0] + old_wj]);
										
										if (tmp_p > 0)
										{
											pixel_variance[actual_index][new_wj][wi] = pixel_variance[prev_index][new_wj][wi]+1;// = pixel_diff;
											variance[actual_index][new_wj][wi] = variance[prev_index][new_wj][wi] + tmp_p;
											//ocurrences[j][i]++;
										}

										/*if (old_tmp_p > 0)
										{
											pixel_variance[old_wj][wi]++;// = pixel_diff;
											variance[old_wj][wi] += old_tmp_p;
											//ocurrences[j][i]++;
										}*/

										if ((pixel_variance[prev_index][new_wj][wi] / (float)(frame_n + 1)) > 0.5 && (variance[prev_index][new_wj][wi] / (float)(frame_n + 1)) * 6 > tmp_p) tmp_p = 0;
										if ((pixel_variance[prev_index][old_wj][wi] / (float)(frame_n + 1)) > 0.5 && (variance[prev_index][old_wj][wi] / (float)(frame_n + 1)) * 6 > old_tmp_p) old_tmp_p = 0;

										accumulation += tmp_p;

										if (old_tmp_p > accumulation)
										{
											int a = 42;
										}

										accumulation -= old_tmp_p;

										//Update center value



										//if ((pixel_variance[wj][wi] / (float)(frame_n + 1)) > 0.5 && (variance[wj][wi] / (float)(frame_n + 1)) * 7 > tmp_p) tmp_p = 0;
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
						ocurrences[j][i]++;
						
						//if (neig > 6)
						{
							float new_diff = accumulation;// pow(window_pix_diff, 2) * pow(2, neig);
							if (frame_diff < new_diff)
							{
								frame_diff = new_diff;
							}
						}
						float nnp = pixel_diff;// std::abs(pixel_diff - (pixel_variance[j][i] / (float)(frame_n + 1)));
						if ((pixel_variance[prev_index][j][i] / (float)(frame_n + 1)) > 0.5 && (variance[prev_index][j][i] / (float)(frame_n + 1))*7 > pixel_diff) nnp = 0;
						//frame_diff += nnp;
						pFrame->data[0][i * pFrame->linesize[0] + j] = nnp *5 ;
					}
				}
			}/**/
			diffs.push_back(frame_diff);
			std::cout << frame_diff << std::endl;
			/*for (int i = 0; i < pCodecCtx->height; i++)
			{
				for (int j = 0; j < pCodecCtx->width; j++)
				{
					pFrame->data[0][i * pFrame->linesize[0] + j] = tmpFrame[i * pFrame->linesize[0] + j];
				}
			}/**/
			frame_n++;
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
		if (diffs[i] > 999999999) flying_frame_counter++;
	}

	float flying_time_in_seconds = (float)flying_frame_counter * (float)frame_in_seconds;
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
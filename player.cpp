#include<stdio.h>
#define __STDC_CONSTANT_MACROS
extern "C"
{
#include "SDL2/SDL.h"
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
FILE __iob_func[3] = { *stdin,*stdout,*stderr };
}
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)

int thread_exit = 0;

int sfp_refresh_thread(void *opaque) {
	while (thread_exit == 0) {
		SDL_Event event;
		event.type = SFM_REFRESH_EVENT;
		SDL_PushEvent(&event);
		SDL_Delay(20);
	}
	return 0;
}


int main(int argc, char* argv[])
{
	AVFormatContext    *pFormatCtx;
	int                i, videoindex;
	AVCodecContext    *pCodecCtx;
	AVCodec            *pCodec;
	AVFrame    *pFrame, *pFrameYUV;
	uint8_t *out_buffer;
	AVPacket *packet;
	int ret, got_picture;
	int screen_w, screen_h;
	//SDL
	SDL_Window *screen;
	SDL_Renderer* sdlRenderer;
	SDL_Texture* sdlTexture;
	SDL_Rect sdlRect;
	SDL_Thread *video_tid;
	SDL_Event event;

	struct SwsContext *img_convert_ctx;
	char filepath[] = "rtsp://192.168.1.205:554/av_0";

	av_register_all();//注册所有组件
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	//打开输入视频文件
	if (avformat_open_input(&pFormatCtx, filepath, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");///////////////////////
		return -1;
	}
	//获取视频文件信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
	{
		printf("Couldn't find stream information.\n");
		return -1;
	}
	videoindex = -1;
	//找到视频流
	for (i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videoindex = i;
			break;
		}
	if (videoindex == -1)
	{
		printf("Didn't find a video stream.\n");
		return -1;
	}
	pCodecCtx = pFormatCtx->streams[videoindex]->codec;//得到AVCodecContext
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);//查找解码器
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		printf("Could not open codec.\n");
		return -1;
	}
	pFrame = av_frame_alloc();
	pFrameYUV = av_frame_alloc();
	out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));//根据像素格式和视频分辨率获得picture存储大小。
	//用ptr中的内容根据文件格式（YUV…） 和分辨率填充picture。这里由于是在初始化阶段，所以填充的可能全是零。
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	//Output Info-----------------------------
	printf("---------------- File Information ---------------\n");
	av_dump_format(pFormatCtx, 0, filepath, 0);//检查下初始化过程中设置的参数是否符合规范
	printf("-------------------------------------------------\n");
	//剔除无效像素
	img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
		pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	screen_w = pCodecCtx->width;
	screen_h = pCodecCtx->height;
	//创建窗口
	screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h, SDL_WINDOW_OPENGL);
	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}
	sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pCodecCtx->width, pCodecCtx->height);

	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;
	packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	
	video_tid = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);
	for (;;) {
		//Wait
		SDL_WaitEvent(&event);
		if (event.type == SFM_REFRESH_EVENT) {
			//------------------------------
			//从输入文件读取一帧压缩数据。
			if (av_read_frame(pFormatCtx, packet) >= 0) {
				if (packet->stream_index == videoindex) {
					ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);//解码一帧压缩数据。
					if (ret < 0) {
						printf("Decode Error.\n");
						return -1;
					}
					if (got_picture) {
						sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
						//SDL---------------------------
						//设置纹理的数据 
						SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
						SDL_RenderClear(sdlRenderer);
						//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
						//将纹理的数据拷贝给渲染器
						SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
						//显示
						SDL_RenderPresent(sdlRenderer);
						//SDL End-----------------------
					}
				}
				av_free_packet(packet);
			}
			else {
				//Exit Thread
				thread_exit = 1;
				break;
			}
		}
		else if (event.type == SDL_QUIT) {
			thread_exit = 1;
			break;
		}

	}

	sws_freeContext(img_convert_ctx);

	SDL_Quit();
	//--------------
	av_frame_free(&pFrameYUV);
	av_frame_free(&pFrame);
	avcodec_close(pCodecCtx); //关闭解码器。
	avformat_close_input(&pFormatCtx);//关闭输入视频文件

	return 0;
}
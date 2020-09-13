#include <stdio.h>
 
#define __STDC_CONSTANT_MACROS
 
#ifdef _WIN32
//Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "SDL2/SDL.h"
};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
};
#endif
#endif
 
//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
 
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)
 
int thread_exit=0;
int thread_pause=0;
 
int sfp_refresh_thread(void *opaque){
    thread_exit=0;
    thread_pause=0;
 
    while (!thread_exit) {
        if(!thread_pause){
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(40);// B9. 控制帧率为25FPS，此处不够准确，未考虑解码消耗的时间
    }
    thread_exit=0;
    thread_pause=0;
    //Break
    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);
 
    return 0;
}
 
 
int main(int argc, char* argv[])
{
 
    AVFormatContext *pFormatCtx;
    int             i, videoindex;
    AVCodecContext  *pCodecCtx;
    AVCodec         *pCodec;
    AVFrame *pFrame,*pFrameYUV;
    unsigned char *out_buffer;
    AVPacket *packet;
    int ret, got_picture;
 
    //------------SDL----------------
    int screen_w,screen_h;
    SDL_Window *screen;
    SDL_Renderer* sdlRenderer;
    SDL_Texture* sdlTexture;
    SDL_Rect sdlRect;
    SDL_Thread *video_tid;
    SDL_Event event;
 
    struct SwsContext *img_convert_ctx;
 
    if (argc < 2)
   {
       printf("Please provide a movie file\n");
       return -1;
   }
    
 
    av_register_all();// 初始化libavformat(所有格式)，注册所有复用器/解复用器
    avformat_network_init();
    pFormatCtx = avformat_alloc_context();
 
    // A1. 打开视频文件：读取文件头，将文件格式信息存储在"pFormatCtx"中
    if(avformat_open_input(&pFormatCtx, argv[1],NULL,NULL)!=0){
        printf("Couldn't open input stream.\n");
        return -1;
    }
    // A2. 搜索流信息：读取一段视频文件数据，尝试解码，将取到的流信息填入pFormatCtx->streams
    if(avformat_find_stream_info(pFormatCtx,NULL)<0){
        printf("Couldn't find stream information.\n");
        return -1;
    }
    videoindex=-1;
     //  pFormatCtx->streams是一个指针数组，数组大小是pFormatCtx->nb_streams
     // A3. 查找第一个视频流
    for(i=0; i<pFormatCtx->nb_streams; i++)
        if(pFormatCtx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO){
            videoindex=i;
            break;
        }
    //未找到
    if(videoindex==-1){
        printf("Didn't find a video stream.\n");
        return -1;
    }
    // A5. 为视频流构建解码器AVCodecContext

    // A5.1 获取解码器参数AVCodecParameters
    pCodecCtx=pFormatCtx->streams[videoindex]->codec;
    // A5.2 获取解码器
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL){
        printf("Codec not found.\n");
        return -1;
    }
    // A5.3.3 pCodecCtx初始化：使用pCodec初始化pCodecCtx，初始化完成
    if(avcodec_open2(pCodecCtx, pCodec,NULL)<0){
        printf("Could not open codec.\n");
        return -1;
    }
    // A6. 分配AVFrame
    // A6.1 分配AVFrame结构，注意并不分配data buffer(即AVFrame.*data[])
    pFrame=av_frame_alloc();
    pFrameYUV=av_frame_alloc();
 
    // A6.2 为AVFrame.*data[]手工分配缓冲区，用于存储sws_scale()中目的帧视频数据
    //     pFrame的data_buffer由av_read_frame()分配，因此不需手工分配
    //     pFrameYUV的data_buffer无处分配，因此在此处手工分配
    // out_buffer将作为p_frm_yuv的视频数据缓冲区
    out_buffer=(unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P,  pCodecCtx->width, pCodecCtx->height,1));
    // 使用给定参数设定pFrameYUV->data和pFrameYUV->inesize
    av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize,out_buffer,
        AV_PIX_FMT_YUV420P,pCodecCtx->width, pCodecCtx->height,1);
 
    //Output Info-----------------------------
    printf("---------------- File Information ---------------\n");
    av_dump_format(pFormatCtx,0, argv[1],0);
    printf("-------------------------------------------------\n");
    
    // A7. 初始化SWS context，用于后续图像转换
    //     此处第6个参数使用的是FFmpeg中的像素格式，对比参考注释B4
    //     FFmpeg中的像素格式AV_PIX_FMT_YUV420P对应SDL中的像素格式SDL_PIXELFORMAT_IYUV
    //     如果解码后得到图像的不被SDL支持，不进行图像转换的话，SDL是无法正常显示图像的
    //     如果解码后得到图像的能被SDL支持，则不必进行图像转换
    //     这里为了编码简便，统一转换为SDL支持的格式AV_PIX_FMT_YUV420P==>SDL_PIXELFORMAT_IYUV
    img_convert_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt,
        pCodecCtx->width, pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);
    
 
     // B1. 初始化SDL子系统：缺省(事件处理、文件IO、线程)、视频、音频、定时器
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        printf( "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    // B2. 创建SDL窗口，SDL 2.0支持多窗口
    //     SDL_Window即运行程序后弹出的视频窗口，同SDL 1.x中的SDL_Surface
    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;
    screen = SDL_CreateWindow("ffmpeg player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        screen_w, screen_h,SDL_WINDOW_OPENGL);
 
    if(!screen) {
        printf("SDL: could not create window - exiting:%s\n",SDL_GetError());
        return -1;
    }
    
    // B3. 创建SDL_Renderer
    //     SDL_Renderer：渲染器
    sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    // B4. 创建SDL_Texture
    //     一个SDL_Texture对应一帧YUV数据，同SDL 1.x中的SDL_Overlay
    //     此处第2个参数使用的是SDL中的像素格式，对比参考注释A7
    //     FFmpeg中的像素格式AV_PIX_FMT_YUV420P对应SDL中的像素格式SDL_PIXELFORMAT_IYUV
    sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,pCodecCtx->width,pCodecCtx->height);
 
    sdlRect.x=0;
    sdlRect.y=0;
    sdlRect.w=screen_w;
    sdlRect.h=screen_h;
 
    packet=(AVPacket *)av_malloc(sizeof(AVPacket));
    // A8. 从视频文件中读取一个packet
   //     packet可能是视频帧、音频帧或其他数据，解码器只会解码视频帧或音频帧，非音视频数据并不会被
   //     扔掉、从而能向解码器提供尽可能多的信息
   //     对于视频来说，一个packet只包含一个frame
   //     对于音频来说，若是帧长固定的格式则一个packet可包含整数个frame，
   //                   若是帧长可变的格式则一个packet只包含一个frame
 
    video_tid = SDL_CreateThread(sfp_refresh_thread,NULL,NULL);
    //------------SDL End------------
    //Event Loop
    
    for (;;) {
        SDL_WaitEvent(&event);
        if(event.type==SFM_REFRESH_EVENT){
            while(1){
                if(av_read_frame(pFormatCtx, packet)<0)
                    thread_exit=1;
 
                if(packet->stream_index==videoindex)
                    break;
            }
            ret = avcodec_decode_video2(pCodecCtx, pFrame, &got_picture, packet);
            if(ret < 0){
                printf("Decode Error.\n");
                return -1;
            }
            if(got_picture){
                // A10. 图像转换：p_frm_raw->data ==> p_frm_yuv->data
                           // 将源图像中一片连续的区域经过处理后更新到目标图像对应区域，处理的图像区域必须逐行连续
                           // plane: 如YUV有Y、U、V三个plane，RGB有R、G、B三个plane
                           // slice: 图像中一片连续的行，必须是连续的，顺序由顶部到底部或由底部到顶部
                           // stride/pitch: 一行图像所占的字节数，Stride=BytesPerPixel*Width+Padding，注意对齐
                           // AVFrame.*data[]: 每个数组元素指向对应plane
                           // AVFrame.linesize[]: 每个数组元素表示对应plane中一行图像所占的字节数
                sws_scale(img_convert_ctx, (const unsigned char* const*)pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
                //SDL---------------------------
                 // B5. 使用新的YUV像素数据更新SDL_Rect
                SDL_UpdateTexture( sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0] );
                // B6. 使用特定颜色清空当前渲染目标
                SDL_RenderClear( sdlRenderer );
                //SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );
                 // B7. 使用部分图像数据(texture)更新当前渲染目标
                SDL_RenderCopy( sdlRenderer, sdlTexture, NULL, NULL);
                // B8. 执行渲染，更新屏幕显示
                SDL_RenderPresent( sdlRenderer );
                //SDL End-----------------------
            }
            av_free_packet(packet);
        }else if(event.type==SDL_KEYDOWN){
            //Pause
            if(event.key.keysym.sym==SDLK_SPACE)
                thread_pause=!thread_pause;
        }else if(event.type==SDL_QUIT){
            thread_exit=1;
        }else if(event.type==SFM_BREAK_EVENT){
            break;
        }
 
    }
 
    sws_freeContext(img_convert_ctx);
 
    SDL_Quit();
    //--------------
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
 
    return 0;
}


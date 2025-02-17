#pragma once
#include "av_queue.h"
#include <algorithm>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

#define MAX_AUDIO_FRAME_SIZE 192000

class AvProcessor{
private:
    int is_quit = 0;
    AVFormatContext *fmt_ctx = nullptr;
    enum ERRNO{ // 错误码
        GET_BYTES_FAILED = 1,
        AV_MALLOC_FAILED,
        AVCODEC_SEND_PKT_FAILED,
        CREAT_DAUDIO_THREAD_FAILED,
        CREAT_DVIDEO_THREAD_FAILED,
        SWR_GETCONTEXT_FAILED,
        SWS_GETCONTEXT_FAILED,
        A_FRAME_ALLOC_FAILED,
        V_FRAME_ALLOC_FAILED,
        A_PACKET_ALLOC_FAILED,
        V_PACKET_ALLOC_FAILED,
        A_CODEC_OPEN_FAILED,
        READ_A_PARA_FAILED,
        V_CODEC_OPEN_FAILED,
        READ_V_PARA_FAILED,
        A_CODEC_NOT_FOUND,
        V_CODEC_NOT_FOUND,
        FIND_BEST_A_STREAM_FAILED,
        FIND_BEST_V_STREAM_FAILED,
        FIND_STREAM_FAILED,
        OPEN_INPUT_FAILED,
    };
    // audio
    const AVCodec *a_codec = nullptr;
    AVCodecContext *a_codec_ctx = nullptr;
    AVPacket * a_pkt = nullptr;
    AVFrame * a_frame = nullptr;
    struct SwrContext *swr_ctx = nullptr;   // 用于音频格式转换
    int a_index;
    AvQueue<AVPacket*> a_pkt_queue{100};    // 音频编码数据包队列
    AvBufferQueue<uint8_t> audio_chunk{MAX_AUDIO_FRAME_SIZE};    // 音频帧队列, 用于存放解码后的PCM数据, 给声卡播放
    int64_t next_pts; // 下一音频帧的pts, 用于计算当前帧的时间戳
    // video
    int h, w;
    const AVCodec *v_codec = nullptr;
    AVCodecContext *v_codec_ctx = nullptr;
    AVPacket * v_pkt = nullptr;
    AVFrame * v_frame = nullptr;
    struct SwsContext *sws_ctx = nullptr;   // 用于视频格式转换
    int v_index;
    AvQueue<AVPacket*> v_pkt_queue{100};    // 视频编码数据包队列
    AvQueue<AVFrame*> v_frame_queue{100};   // 视频帧队列
    // 功能-快进快退
    int64_t seek_pos;   // 快进快退的目标位置，秒 * AV_TIME_BASE
    int seek_flag = 0;  // 0为正常播放, 1为快进, -1为快退
    SDL_mutex *seek_mutex;
public:
    int invalid = 0;  // 错误处理, 0: valid, >0: invalid
    AvProcessor(const char *src);
    ~AvProcessor();
    static int demux_thread(void* data){ // 静态成员函数作为创建线程的入口
        return ((AvProcessor*)data)->demux();
    }
    static int decode_video_thread(void* data){ // 静态成员函数作为创建线程的入口
        return ((AvProcessor*)data)->decode_video();
    }
    static int decode_audio_thread(void* data){ // 静态成员函数作为创建线程的入口
        return ((AvProcessor*)data)->decode_audio();
    }
    int demux();            // 解复用线程主体
    int decode_video();     // 视频解码线程主体
    int decode_audio();     // 音频解码线程主体
    double get_video_clock(AVFrame* frame); // 计算视频时钟
    double get_audio_clock();               // 计算音频时钟
    void audio_chunk_pop(uint8_t *stream, int len); // 从音频帧队列中取出PCM数据
    AVFrame* video_frame_pop();                     // 从视频帧队列中取出视频帧
    void stop(){    // 停止线程
        this->is_quit = 1;
        this->a_pkt_queue.stop();
        this->v_pkt_queue.stop();
        this->v_frame_queue.stop();
        this->audio_chunk.stop();
    }
    // 获取private属性值
    int get_h(){ return this->h; }
    int get_w(){ return this->w; }
    int get_channels(){ return this->a_codec_ctx->ch_layout.nb_channels; }
    int get_sample_rate(){ return this->a_codec_ctx->sample_rate; }
    void set_seek_flag(int flag, double pos_time){
        SDL_LockMutex(this->seek_mutex);
        this->seek_flag = flag;
        this->seek_pos = (int64_t)(std::max(0., pos_time) * AV_TIME_BASE);
        av_log(nullptr, AV_LOG_DEBUG, "seek_pos %lld, pos_time %lf, seek_flag %d\n", this->seek_pos, pos_time, this->seek_flag);
        SDL_UnlockMutex(this->seek_mutex);
    }
};

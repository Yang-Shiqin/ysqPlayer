# 基于FFMpeg+SDL的视频播放器
基于FFMpeg+SDL的简易音视频播放器实现，支持暂停继续、快进快退等功能。

## 依赖
- ffmpeg库 7.0及以上
- SDL库

## 运行
编译：
```bash
rm -rf build/
cmake -B build
cmake --build build
```

运行：
```bash
./build/BasicAvPlayer <your_video_file_path>
```
- 空格：暂停/播放
- 左键：快退3秒
- 右键：快进3秒
- 退出键：关闭视频

## 播放器模型
基本组件模型（5个）：
- **音视频解复用组件**：将音视频解复用，视频放入 *视频编码数据包队列*，音频放入 *音频编码数据包队列*
- **视频解码组件**：解码视频，从 *视频编码数据包队列* 中取出packet数据，解码frame后存入 *视频帧队列*
- **音频解码组件**：解码音频，从 *音频编码数据包队列* 中取出packet数据，解码frame后存入 *音频帧队列*
- **视频渲染组件**：从 *视频帧队列* 取出数据进行渲染
- **音频渲染组件**：从 *音频帧队列* 取出数据进行播放

数据队列（4个）：
- **视频编码数据包队列**：存视频编码数据包，如 `.h264`
- **音频编码数据包队列**：存音频编码数据包，如 `.aac`
- **视频帧队列**：存视频解码数据帧，如 `.yuv`
- **音频帧队列**：存音频解码数据帧，如 `.pcm`

处理流程：
![](https://img2020.cnblogs.com/blog/2063669/202008/2063669-20200815121549742-1557938920.png)
线程设计（4个）：
- **主线程**：主循环，事件处理-退出、视频渲染、音频播放：对应视频渲染组件和音频渲染组件
	- **定时器回调函数**发送视频渲染信号，定时渲染视频(类型为用户事件的事件)
	- **音频回调函数**在声卡需要数据时，从音频帧队列取出数据自动播放
- **解复用线程**：对应音视频解复用组件
- **视频解码线程**：对应视频解码组件
- **音频解码线程**：对应音频解码组件

## 附加功能说明
键盘事件判断：
- 空格：播放暂停
- 左右键：快进快退
- 退出：退出程序
```cpp
int Player::play(){
    ...
    int running = 1;    // 第1位是是否播放, 第2位是是否暂停
    while(running){
        SDL_WaitEvent(&this->event);
        switch (this->event.type)
        {
        case SDL_QUIT:  // 退出事件
            this->processor->stop();
            SDL_WaitThread(demux_tid, nullptr);
            running = 0;
            break;
        case SDL_KEYDOWN:   // 键盘事件
            switch (event.key.keysym.sym){
            case SDLK_SPACE:
                running ^= 2;   // 暂停
                SDL_PauseAudio(running & 2);    // 暂停音频
                break;
            case SDLK_LEFT:     // 增加键盘事件中的左键事件处理-快退3s
                SDL_PauseAudio(1);  // 播放音频(非0是暂停, 0是播放)
                this->processor->set_seek_flag(-1, this->processor->get_audio_clock()-3);
                break;
            case SDLK_RIGHT:    // 增加键盘事件中的右键事件处理-快进3s
                SDL_PauseAudio(1);  // 播放音频(非0是暂停, 0是播放)
                this->processor->set_seek_flag(1, this->processor->get_audio_clock()+3);
                break;
            }
            break;
        case SDL_USEREVENT: // 视频定时播放事件
            this->timer_video_display();
            break;
        default:
            break;
        }
    }
    ...
}
```
### 同步到音频
`av_processor` 增加获取音视频时间戳的函数
```cpp
// 计算视频时钟, 单位为s
double AvProcessor::get_video_clock(AVFrame* frame){
    return frame->pts*av_q2d(this->fmt_ctx->streams[this->v_index]->time_base);
}

// 计算音频时钟, 单位为s
double AvProcessor::get_audio_clock(){
    // 公式: 当前帧实际时间 = pts x time_base - 已解码未播放字节/(channels x 样本位深 x sample_rate)
    double audio_clock = this->next_pts * av_q2d(this->fmt_ctx->streams[this->a_index]->time_base); // 下一音频包的pts
    audio_clock -= (double)(this->audio_chunk.size()) / 
        (double)(this->a_codec_ctx->channels*this->a_codec_ctx->sample_rate*
        av_get_bytes_per_sample(this->a_codec_ctx->sample_fmt));
    return audio_clock;
}
```

`av_SDL` 增加根据时间戳设置delay时长的函数
```cpp
// 原本调用video_display的地方改为调用timer_video_display

int Player::timer_video_display(){
    static double first_delay = 0;  // 用来同步音视频第一个帧所需的延迟
    static int flag = 0;
    // 1. 从队列中取出视频帧
    if (this->frame==nullptr)
        this->frame = this->processor->video_frame_pop();
    if (!this->frame) {
        av_log(NULL, AV_LOG_ERROR, "frame is NULL\n");
        return (this->invalid = VIDEO_FRAME_BROKE);
    }
    // 2. 计算视频同步到音频需要的延迟(下一视频帧时间戳-音频帧时间戳, <0则说明视频慢了, 应加速播放)
    double delay = this->processor->get_video_clock(this->frame) - this->processor->get_audio_clock()-first_delay;
    if (!flag){     // 只记录第一次的延迟
        first_delay = delay;
        flag = 1;
    }
    av_log(NULL, AV_LOG_DEBUG, "delay: %f\n", delay);
    
    if (delay <= 0){    // 视频慢了
        this->video_display(this->frame);   // 显示视频
        this->frame = nullptr;
        SDL_AddTimer(1, video_timer, this->processor);  // 1ms后再次调用timer_video_display
    }else{  // 视频快了
        // delay是s为单位, 而SDL_AddTimer是ms为单位, 所以delay*1000, +0.5是向上取整, 防止下次调用没到时间又重复delay了很小一个时间
        SDL_AddTimer((int)(delay*1000+0.5), video_timer, this->processor);  // 延迟快了的时间后再次调用timer_video_display
    }
    return 0;
}

// 播放一帧视频
int Player::video_display(AVFrame* frame){
    // 2. 更新纹理
    SDL_UpdateYUVTexture(this->texture, NULL, frame->data[0], frame->linesize[0], 
        frame->data[1], frame->linesize[1], frame->data[2], frame->linesize[2]);
    // 3. 清空渲染器
    SDL_RenderClear(this->renderer);
    // 4. 拷贝纹理到渲染器
    SDL_RenderCopy(this->renderer, this->texture, NULL, NULL);
    // 5. 显示
    SDL_RenderPresent(this->renderer);
    // 6. 释放帧
    av_freep(&frame);
    return 0;
}
```

更详细的说明见笔记《基于FFMpeg+SDL的视频播放器》的“方案2：视频同步到音频”章节

### 暂停继续
因为已经实现较为精细的音视频同步，所以视频也会相应暂停和继续
```cpp
int Player::play(){
    ...
    int running = 1;    // 第1位是是否播放, 第2位是是否暂停
    while(running){
        SDL_WaitEvent(&this->event);
        switch (this->event.type)
        {
        case SDL_QUIT:  // 退出事件
            this->processor->stop();
            SDL_WaitThread(demux_tid, nullptr);
            running = 0;
            break;
        case SDL_KEYDOWN:   // 键盘事件
            switch (event.key.keysym.sym){
            case SDLK_SPACE:                // 增加键盘事件中的空格事件处理
                running ^= 2;   // 暂停
                SDL_PauseAudio(running & 2);    // 暂停音频
                break;
            }
            break;
        case SDL_USEREVENT: // 视频定时播放事件
            this->timer_video_display();
            break;
        default:
            break;
        }
    }
    ...
}
```
### 快进快退
通过 `av_seek_frame()` 实现视频的快进快退，一个简单的例子:

```cpp
// 快退(其中ex_yuv为媒体处理类)
// 1. 获取当前视频时钟(单位为秒, double)并-3s, 时基转换: AV_TIME_BASE->视频编码时基
int64_t seek_pos = av_rescale_q(std::max(0., ex_yuv.get_video_clock()-3)*AV_TIME_BASE, 
                AV_TIME_BASE_Q, ex_yuv.fmt_ctx->streams[ex_yuv.video_index]->time_base);
// 2. 跳转到对应时刻帧
av_seek_frame(ex_yuv.fmt_ctx, ex_yuv.video_index, seek_pos, AVSEEK_FLAG_BACKWARD);
// 清空缓存
avcodec_flush_buffers(ex_yuv.codec_ctx);
```

具体修改：
- `av_SDL.cc` 增加快进快退事件处理，音视频同步时间差太大放弃同步
- `av_queue.h` 增加清空队列操作 `clear()` 和非阻塞push `try_push()`
- `av_processor` 增加快进快退属性, `demux()` 增加快进快退处理

`av_SDL.cc`:
```cpp
int Player::play(){
    ...
            case SDLK_LEFT:     // 快退3s
                SDL_PauseAudio(1);  // 播放音频(非0是暂停, 0是播放)
                this->processor->set_seek_flag(-1, this->processor->get_audio_clock()-3);
                break;
            case SDLK_RIGHT:    // 快进3s
                SDL_PauseAudio(1);  // 播放音频(非0是暂停, 0是播放)
                this->processor->set_seek_flag(1, this->processor->get_audio_clock()+3);
                break;
    ...
}

int Player::timer_video_display(){
    ...
    if (flag && abs(delay)>1){  // 差太大，快进快退模式
        av_freep(&frame);
        this->frame = nullptr;
        SDL_AddTimer(1, video_timer, this->processor);
    }else if (delay <= 0){    // 视频慢了
        直接显示;
    }else{  // 视频快了
        delay差值;
    }
    ...
}
```

`av_queue.h`:
```cpp
template <typename T>
class AvQueue{
public:
    bool try_push(T element){
        SDL_LockMutex(this->mutex);
        if (!this->running || this->q.size() >= this->q_len){
            SDL_UnlockMutex(this->mutex);
            return false;
        }
        this->q.push(element);
        SDL_CondBroadcast(this->cond);
        SDL_UnlockMutex(this->mutex);
        return true;
    }

    void clear(void(*callback)(void*)){ // 放个回调函数是为了以用户定义的方式释放资源(因为不同资源的释放方式不同)
        SDL_LockMutex(this->mutex);
        if (callback){
            while (!this->q.empty()){
                callback(&(this->q.front()));
                this->q.pop();
            }
        }else{
            while (!this->q.empty()){
                this->q.pop();
            }
        }
        SDL_CondBroadcast(this->cond);
        SDL_UnlockMutex(this->mutex);
    }
}

class AvBufferQueue{
public:
    void clear(){
        SDL_LockMutex(this->mutex);
        this->q_size = 0;
        this->head = 0;
        this->tail = 0;
        SDL_CondBroadcast(this->cond);  // 因为相当于pop all, 还是要唤醒大家
        SDL_UnlockMutex(this->mutex);
    }
}
```
释放资源调用:
```cpp
void free_packet(void* packet){ av_packet_unref(*(AVPacket**)packet); }

this->v_pkt_queue.clear((void(*)(void*))free_packet);
this->a_pkt_queue.clear((void(*)(void*))free_packet);
this->v_frame_queue.clear((void(*)(void*))av_frame_free); // av_frame_free(**)是因为还要设置指针为nullptr
```

`av_processor.h`:
```cpp
class AvProcessor{
private:
    ...
    // 功能-快进快退
    int seek_flag = 0;  // 0为正常播放, 1为快进, -1为快退
    int64_t seek_pos;   // 快进快退的目标位置，秒 * AV_TIME_BASE
    SDL_mutex *seek_mutex;
    ...
public:
    ...
    // 设置快进快退标志位和目标位置，pos_time单位为秒
    void set_seek_flag(int flag, double pos_time){
        SDL_LockMutex(this->seek_mutex);
        this->seek_flag = flag;
        this->seek_pos = (int64_t)(std::max(0., pos_time) * AV_TIME_BASE);
        SDL_UnlockMutex(this->seek_mutex);
    }
}
```

`av_processor.cc`:
```cpp
int AvProcessor::demux(){
    ...
    while (1) {
        if (this->is_quit){...}
        if (this->seek_flag!=0){
            SDL_PauseAudio(-1); // 暂停音频
            // 计算时间戳(时基AV_TIME_BASE->对应流的time_base)
            int64_t seek_pos = av_rescale_q(this->seek_pos, AV_TIME_BASE_Q, this->fmt_ctx->streams[this->v_index]->time_base);
            // 跳转目标时间戳, av_seek_frame默认同时跳转音视频到目标帧
            if (av_seek_frame(this->fmt_ctx, this->v_index, seek_pos, (1-this->seek_flag)/2) < 0){  // 快退AVSEEK_FLAG_BACKWARD是1
                av_log(nullptr, AV_LOG_ERROR, "av_seek_frame failed\n");
            } else {
                // 清空队列
                avcodec_flush_buffers(this->v_codec_ctx);
                avcodec_flush_buffers(this->a_codec_ctx);
                this->v_pkt_queue.clear((void(*)(void*))free_packet);
                this->a_pkt_queue.clear((void(*)(void*))free_packet);
                this->v_frame_queue.clear((void(*)(void*))av_frame_free);
                this->audio_chunk.clear();
                av_log(nullptr, AV_LOG_DEBUG, "aframe queue size: %d\n", this->audio_chunk.size());
                av_log(nullptr, AV_LOG_DEBUG, "vpkt size: %d\n", this->v_pkt_queue.size());
            }
            // 清空状态位
            this->set_seek_flag(0, 0);
            SDL_PauseAudio(0);  // 继续音频
        }
        ...
        if (av_read_frame(this->fmt_ctx, pkt) < 0){...}
        else{
            if (pkt->stream_index == this->v_index){    // 视频流
                while(!this->v_pkt_queue.try_push(pkt)){    // 改成非阻塞，防止阻塞造成快进快退被卡住，但轮询性能低
                    if (this->is_quit) break;
                    if (this->seek_flag) break;
                }
            }else if (pkt->stream_index == this->a_index){  // 音频流
                while(!this->a_pkt_queue.try_push(pkt)){
                    if (this->is_quit) break;
                    if (this->seek_flag) break;
                }
            }
        }
    }
    return 0;
}
```

更详细的说明见笔记《基于FFMpeg+SDL的视频播放器》的“快进快退”章节


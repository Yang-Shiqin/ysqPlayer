/* 16播放器(能够播放音视频、退出、音视频同步、暂停继续)基础上, 实现快进快退功能 */

// make
// ./BasicAvPlayer ../../data/human.mp4

/*
 * [ ] TODO: 
 * - [ ] 视频pts无效时没有处理, 而且继续用来计算
 * - [ ] 多次快进快退会segmentation fault, 可能还有些小bug
 */

#include "av_SDL.h"

int main(int argc, char *argv[]){
    av_log_set_level(AV_LOG_INFO);  // 设置日志级别
    // 0. 命令行参数解析
    if (argc < 2) {  // 错误处理
        av_log(NULL, AV_LOG_ERROR, "usage: %s <input>\n", argv[0]);
        return 1;
    }
    char *src = argv[1];
    if (!src) {
        av_log(NULL, AV_LOG_ERROR, "input is NULL\n");
        return 1;
    }

    // 1. 创建AvProcessor对象
    AvProcessor processor(src);

    // 2. 初始化SDL播放器
    Player player(&processor);

    // 3. 播放
    player.play();

    return 0;
}
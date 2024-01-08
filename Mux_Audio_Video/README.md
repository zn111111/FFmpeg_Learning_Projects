# 1 基于新版FFmpeg（FFmpeg 6.1）的音视频复用（不涉及编解码）

## 1.1 项目中使用的FFmpeg函数介绍

[FFmpeg库常用函数介绍-CSDN博客](https://blog.csdn.net/m0_51496461/article/details/135315126?spm=1001.2014.3001.5502)

### 1.1.1 av_compare_ts

作用：比较两个以不同时间基表示的时间戳，一般用于将音视频封装于封装格式中进行时间戳比较，以决定先写入哪个帧。

函数原型：

```c++
int av_compare_ts(int64_t ts_a, AVRational tb_a, int64_t ts_b, AVRational tb_b);
```

ts_a：以时间基tb_a表示的时间戳；

tb_a：时间基；

ts_b：以时间基tb_b表示的时间戳；

tb_b：时间戳；

返回值：-1表示时间戳ts_a早于ts_b，0表示两个时间戳相等，1表示时间戳ts_a滞后于ts_b。

### 1.1.2 av_q2d

作用：将AVRational类型的分数转换为double类型，转换前后仍然基于同一时间基。

函数原型：

```c++
static inline double av_q2d(AVRational a);
```

a：AVRational类型的分数；

返回值：转换后的double类型数值。

## 1.2 介绍

这篇文章介绍的是基于新版FFmpeg（FFmpeg 6.1）的音视频复用器的实现，可以实现音频和视频文件复用为一个视频文件，具体功能如下表所示。

| **输入视频文件** | **输入音频文件** | **输出视频文件**             |
| ---------------- | ---------------- | ---------------------------- |
| input.h264       | input.aac        | output.mp4 (avi、mkv、wmv等) |
| input.h264       | input.mp3        |                              |
| input.mp4        | input.mp3        |                              |
| input.mp4        | input.aac        |                              |
| input.mp4        | input.mp4        |                              |
| …等等…           |                  |                              |

## 1.3 代码逻辑

1、 根据输出文件的格式选择是否开始比特流过滤器（AAC_ADTS_TO_ASC和H264_AVCC_TO_ANNEXB宏）。例如，输出格式为avi，就需要开启H264_AVCC_TO_ANNEXB（置为1）；

2、 打开输入音视频文件，创建并初始化输入AVFormatContext，创建输出AVFormatContext；

3、 根据输入视频文件的视频流创建输出文件的视频流，拷贝编解码器参数；

4、 根据输入音频文件的音频流创建输出文件的音频流，拷贝编解码器参数；

5、 打开输出文件，写入文件头；

6、 根据过滤器的开启情况创建并初始化对应比特流过滤器；

7、 根据av_compare_ts的输出判断先读取音频还是视频文件，然后读取帧；

8、 时间戳转换、送入过滤器过滤、交错写入；

9、 所有帧写完后写入文件尾；

## 1.4 问题汇总

### 1.4.1 没有pts

有的码流没有pts，例如原始的H.264码流，因此需要自己手动设置pts。pts是以输入流时间基表示的ffmpeg内部时间。以输入流时间基表示的意思是有几个输入流时间基。ffmpeg内部时间是AV_TIME_BASE (1000000)，换算关系是1s = 1000000。

计算过程是首先计算出ffmpeg内部时间表示的两帧之间的间隔：

```c++
int frame_duration = AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
```

**1 / av_q2d(in_stream->r_frame_rate)**表示的是以秒表示的间隔，**AV_TIME_BASE / av_q2d(in_stream->r_frame_rate)**表示的是ffmpeg内部时间表示的间隔。

接着就是算出真正的pts，也就是以输入流时间基表示的ffmpeg内部时间。

```c++
pkt.pts = frame_index * frame_duration / (av_q2d(in_stream->time_base) * AV_TIME_BASE);
```

**frame_index \* frame_duration**表示当前帧以ffmpeg内部时间表示的显示时间。**(av_q2d(in_stream->time_base) \* AV_TIME_BASE)**表示输入流时间基以ffmpeg内部时间表示的结果。**二者相除**表示以输入流时间基表示的ffmpeg内部时间，也就是真正的pts。

### 1.4.2 二倍速问题

写完代码后，使用没有pts的码流进行测试，发现画面变成了二倍速，并且视频长度也减半了，猜测是pts的设置有问题。最终将pts乘以2解决了问题，但是目前还不知道原理是什么。

//解决2倍速问题...

```c++
pkt.pts *= 2;
```

### 1.4.3 packet里的stream_index的设置

输出文件的音视频流来自不同的文件，因此packet中流的索引与输出文件中流的索引可能不匹配，可能出现packet中音频帧和视频帧所对应的stream_index是一样的的情况。因此将packet中的音频或视频帧与输出流的音视频流的索引匹配上。

```c++
pkt.stream_index = out_stream->index;
```
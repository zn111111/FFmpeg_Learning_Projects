# 1 项目介绍

这是一个使用FFmpeg进行视频转码的小项目，为了简单起见，项目中的音视频的编码标准使用原编码标准。需要注意的是，由于不同音频编码标准对于一帧音频的样本数（nb_samples）不同，例如，AAC一般为1024，MP3一般为1152。因此，如果需要转换音频的编码标准，则需要进行重采样。

# 2 项目中使用的FFmpeg函数介绍

[FFmpeg库常用函数介绍（一）-CSDN博客](https://blog.csdn.net/m0_51496461/article/details/135315126?spm=1001.2014.3001.5502)

[FFmpeg库常用函数介绍（二）-CSDN博客](https://blog.csdn.net/m0_51496461/article/details/135585184?spm=1001.2014.3001.5502)

[FFmpeg库常用函数介绍（三）-CSDN博客](https://blog.csdn.net/m0_51496461/article/details/135585321?spm=1001.2014.3001.5502)

# 3 视频转码流程

## 3.1 打开输入文件

打开输入文件的主要任务如下：

1) 初始化输入文件的AVFormatContext以及寻找流信息这些获取输入文件信息的一些操作。
2) 遍历输入流，寻找音视频的解码器，创建解码器上下文，初始化解码器上下文的参数，打开解码器。

![img](file:///C:/Users/ZouNan/AppData/Local/Temp/msohtmlclip1/01/clip_image002.png)

在初始化解码器上下文AVCodecContext参数时，有一个需要注意的点。AVCodecParameters中没有pkt_timebase字段，因此AVCodecContext中的该字段只有默认值，需要找到输入流中对应的时间基给它赋值，以便后面创建buffer/abuffer滤镜器实例传递参数时使用。

![img](file:///C:/Users/ZouNan/AppData/Local/Temp/msohtmlclip1/01/clip_image004.jpg)

## 3.2 打开输出文件

打开输出文件的主要任务如下：

1) 分配输出文件的AVFormatContext，创建输出音视频流。
2) 寻找音视频的编码器，创建编码器上下文，初始化编码器上下文的参数，打开编码器，将AVCodecContext的参数拷贝给AVCodecParameters。
3) 打开输出文件，写入文件头。

![img](file:///C:/Users/ZouNan/AppData/Local/Temp/msohtmlclip1/01/clip_image006.png)

在初始化编码器上下文AVCodecContext参数时，有几个需要注意的点：

1) 初始化解码器上下文时可以将输入流的AVCodecParameters拷贝给解码器上下文AVCodecContext，因为输入流的AVCodecParameters里存储的就是编码器的相关信息。但是转码时使用的可能是不同的编码器，因此初始化时就需要一个个成员赋值。
2) 需要通过判断输出格式是否需要全局头来确定AVCodecContext里的flags的值。如果需要全局头，就需要或上AV_CODEC_FLAG_GLOBAL_HEADER来让FFmpeg在编码时自动加上全局头。

if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)

enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

## 3.3 初始化滤镜

之所以需要滤镜是因为需要将解码后的视频帧的像素格式转换为输出格式支持的像素格式，以及将解码后的音频帧的采样格式转换为输出格式支持的采样格式，然后再进行编码。

初始化滤镜的主要任务如下：

1) **对于视频**，创建null滤镜（假滤镜），因为并不需要使用滤镜，只是改变像素格式。创建buffer滤镜（滤镜图的输入节点，用于缓存原始的视频帧供滤镜图读取）和buffersink滤镜（滤镜图的输出节点，用于缓存滤镜图处理后的视频帧）。创建buffer滤镜和buffersink滤镜实例（创建buffer滤镜实例时需要提供参数，参数包含视频分辨率、像素格式、时间基以及像素纵横比），并将其添加到滤镜图中去。设置buffersink滤镜实例的参数pix_fmts。
2) **对于音频**，创建anull滤镜（假滤镜）。创建abuffer滤镜和abuffersink滤镜。创建abuffer滤镜和abuffersink滤镜实例（创建abuffer滤镜时需要提供参数，参数包含音频的时间基、采样率、采样格式以及通道布局），并将其添加到滤镜图中去。设置abuffersink滤镜实例的参数sample_fmts、ch_layouts以及sample_rates。
3) 创建并初始化buffer/abuffer和buffersink/abuffersink滤镜实例的引脚AVFilterInOut。解析字符串描述的滤镜图null/anull，并将其添加到现有的滤镜图中去。最后将滤镜图中的滤镜连接起来。

![img](file:///C:/Users/ZouNan/AppData/Local/Temp/msohtmlclip1/01/clip_image008.png)

## 3.4 转码

转码的主要任务如下：

1) **解码**。读取编码帧，将编码帧送入解码器中，接收解码后的帧。
2) **使用滤镜进行处理**。将解码后的帧送入滤镜图进行处理，接收经滤镜图处理后的音视频帧。
3) **编码写入**。将滤镜图处理后的音视频帧送入编码器，接收编码后的音视频帧，将编码后的音视频帧写入输出文件。
4) **刷新解码器、滤镜器和编码器**。该操作的作用是清空解码器、滤镜器和编码器中的缓冲数据。**刷新解码器：**将输入数据置为NULL刷新解码器，接收解码后的数据，然后送入滤镜器处理，然后进行编码写入。**刷新滤镜器：**将输入数据置为NULL刷新滤镜器，接收滤镜器处理后的数据，然后进行编码写入。**刷新编码器：**将输入数据置为NULL刷新编码器，然后将编码后的音视频帧写入输出文件。
5) **写入文件尾**。

在进行编码时，有一个需要注意的点。需要对pts进行转换，将pts转换为以编码器时间基表示的pts。但是解码后的帧和滤镜器处理后的帧（AVFrame）里的时间基都是默认值0。因此要对解码后和滤镜器处理后的帧里的时间基进行赋值，以便后面转换pts使用。

在将编码后的帧写入输出文件时有一个需要注意的点。需要将以编码器时间基表示的时间戳pts、dts以及duration转换为以输出格式时间基表示。

# 4 问题汇总

## 4.1 段错误

问题描述：调用avcodec_receive_packet函数接收编码后的音视频帧时，出现段错误。

原因：用于存储编码后的音视频帧的AVPacket结构体没有初始化。

解决办法：对AVPacket结构体进行初始化。

## 4.2 打开视频编码器失败

问题描述：调用avcodec_open2打开视频编码器失败。

错误信息：[libx264 @ 0x56217c499880] The encoder timebase is not set.

原因：编码器的时间基为{0, 1}。因为输入视频流里的参数里的framerate为{0, 1}，导致设置的time_base也为{0, 1}。

解决办法：调用av_guess_frame_rate对视频流的帧率进行设置。

## 4.3 写入编码的帧到输出文件中失败

问题描述：调用av_interleaved_write_frame函数将编码后的音视频帧写入到输出文件中失败。

原因：待写入的帧的pts、dts以及duration时间戳有问题，值为0或者负值。因为调用av_packet_rescale_ts函数进行时间戳转换时原宿时间基写错了，导致AVPacket中帧的时间戳有问题。

解决办法：调用av_packet_rescale_ts函数，对编码后的音视频帧的时间戳进行转换。源时间基是编码器的时间基，宿时间基是输出格式里对应音频或者视频流的时间基。

## 4.4 使用libx264进行编码时出现警告

问题描述：使用libx264进行编码时出现警告，内容是强制的帧类型5被更改为帧类型3。

错误信息：[libx264 @ 0x563e9de51540] forced frame type (5) at 134 was changed to frame type (3)

原因：根据警告信息可以看出，原本是强制的帧类型（AVFrame里的pict_type）5被更改为了帧类型3。

解决办法：将AVFrame里的pict_type字段的值改为AV_PICTURE_TYPE_NONE。

## 4.5 刷新编码器时，编码器里的编码的帧写入输出文件失败

问题描述：刷新编码器时，编码器里有缓存的数据，将缓存的编码的帧写入输出文件时失败。

原因：pts、dts和duration时间戳有问题（不是AV_NOPTS_VALUE，只是值不对）。使用av_packet_rescale_ts进行时间戳转换时源宿时间基写错了，源时间基应该是编码器的时间基，宿时间基应该是输出格式里对应音频或者视频流的时间基。

解决办法：将源宿时间基修改为正确的值。

## 4.6 转码后的视频文件只有一个视频流

问题描述：转码后的视频文件只有一个视频流，并且无法播放。

原因：编码后的AVPacket里的流索引（stream_index）并不会被设置，仍然保持默认值0，导致编码的音视频帧都被写入到视频流里去了，所以只有一个视频流。

解决办法：手动设置编码后的音视频帧的AVPacket里的stream_index。

## 4.7 编码的帧写入输出文件时，报错时间戳不是单调递增的

问题描述：将编码的音视频帧写入输出文件时报错，内容为提供的dts时间戳不是单调递增的。

错误信息：[mp4 @ 0x555df2a72800] Application provided invalid, non monotonically increasing dts to muxer in stream 0: 176640 >= -3686400

原因：和上面只有一个视频流的原因一样，是因为AVPacket里的stream_index没有设置，无论音频帧还是视频帧，stream_index的值都为默认值0。因此音视频帧被写入到一个流里，导致时间戳非单调递增。

解决办法：手动设置编码后的音视频帧的AVPacket里的stream_index。

## 4.8 转码后的视频文件，声音断断续续

问题描述：播放转码后的视频文件，声音断断续续不连贯。

原因：音频帧的时间戳有问题。AVPacket里的stream_index还没有设置，默认值为0，在编码的音频帧到来时，直接使用stream_index来获取流的时间基，获取到的是视频流的时间基。将编码的以编码器时间基表示的音频帧的时间戳转换为视频流时间基表示的时间戳，显然有问题。

解决办法：使用正确的流索引获取正确的流的时间基。

## 4.9 转码后的视频文件，画面卡住不动

问题描述：播放转码后的视频文件，画面卡住不动，很久才会切换到下一个画面，并且视频时常变长了几百倍。

原因：编码后的视频帧的pts时间戳有问题。经滤镜器处理后的视频帧的pts时间戳是没有问题的，但是经过编码的时间戳却有问题。因为经滤镜器处理后的pts时间戳在送入编码器处理前没有进行转换。

解决办法：应该将由buffersink滤镜器时间基表示的pts时间戳转换为由编码器表示的pts时间戳，然后再送入编码器进行编码。

## 4.10 转码后的视频文件时长变长了近一半

问题描述：视频转码完成后，时长变长了近一半，前面和原来的视频一样，也可以正常播放，后面多出来的没有内容。

原因：刷新编码器时，音频帧的时间戳有问题。AVPacket里的stream_index还没有设置，默认值为0，在获取到编码器缓冲区中编码的音频帧后，直接使用stream_index来获取流的时间基，获取到的是视频流的时间基。将编码的以编码器时间基表示的音频帧的时间戳转换为视频流时间基表示的时间戳，显然有问题。

解决办法：使用正确的流索引获取正确的流的时间基。
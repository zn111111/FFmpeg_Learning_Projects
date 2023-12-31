这是一个使用FFmpeg对视频文件进行转封装，但不进行转码的小项目，可以实现mp4转flv、avi、wmv、mkv以及mov。

项目中使用的函数的讲解见[FFmpeg库常用函数介绍-CSDN博客](https://blog.csdn.net/m0_51496461/article/details/135315126?spm=1001.2014.3001.5502)

整个转封装的代码流程如下：

![img](file:///C:/Users/ZouNan/AppData/Local/Temp/msohtmlclip1/01/clip_image002.png)

首先是打开输入流，创建并初始化输入AVFormatContext；然后是寻找流的编解码信息；然后是创建并初始化输出AVFormatContext；然后遍历所有输入流，创建输出流并拷贝编解码器参数；由于不同封装格式码流格式不同，所以要将codec_tag设为0，这样ffmpeg会自动选择和封装格式匹配的码流格式。

然后根据上下文是否依赖输入输出文件来确定是否打开输出文件；然后写入文件头。

然后循环的读取音视频帧，如果到达文件尾，则退出循环，然后写入文件尾，整个转换过程结束。如果没有到达文件尾，则对时间戳进行转换。如果输出格式为avi且是视频流，还需要对码流进行过滤，将avcC码流转换成annexB码流。否则交错写入帧信息。

具体转换码流流程是，首先获取比特流过滤器，然后创建AVBSFContext，然后拷贝编解码器参数，然后初始化AVBSFContext，然后将数据送入过滤器，如果av_bsf_send_packet返回值为AVERROR(EAGAIN)，则说明单个packet不足以完成过滤，需要继续送入数据，则执行continue。否则获取过滤后的数据，如果到达文件尾，则退出循环。否则交错写入帧信息。然后根据av_bsf_receive_packet返回值是否等于AVERROR(EAGAIN)来判断是否继续执行循环。
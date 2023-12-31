#include <string>
#include <stdio.h>
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/bsf.h"
};

//释放所有上下文信息
void release_context(AVFormatContext **in, AVFormatContext *out, AVIOContext *ctx, AVBSFContext **bsf_ctx_)
{
    //关闭输入
    avformat_close_input(in);
    if (!((out->oformat->flags) & AVFMT_NOFILE))
    {
        //关闭输出
        if (avio_close(ctx) < 0)
        {
            printf("failed to close output file\n");
            return;
        }
    }
    //释放输出AVFormatContext
    avformat_free_context(out);
    //释放AVBSFContext
    av_bsf_free(bsf_ctx_);
}

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        printf("argument error, caller should pass two filename as arguments, for example, \"./main src.mp4 dst.avi\"\n");
        return -1;
    }
    const char *in_filename = argv[1];
    AVFormatContext *in_fmt_ctx = NULL;
    //打开输入流，创建并初始化输入AVFormatContext
    if (avformat_open_input(&in_fmt_ctx, in_filename, NULL, NULL) < 0)
    {
        printf("failed to open input file\n");
        release_context(&in_fmt_ctx, NULL, NULL, NULL);
        return -1;
    }
    //格式化输出输入流信息
    av_dump_format(in_fmt_ctx, 0, in_filename, 0);
    //寻找流的信息以及编解码信息
    if (avformat_find_stream_info(in_fmt_ctx, NULL) < 0)
    {
        printf("failed to find media stream info\n");
        release_context(&in_fmt_ctx, NULL, NULL, NULL);
        return -1;
    }
    AVFormatContext *out_fmt_ctx = NULL;
    const char *out_filename = argv[2];
    //创建并初始化输出AVFormatContext
    //不指定AVOutputFormat，ffmpeg会根据格式名或者文件名猜出上下文信息并初始化输出AVFormatContext
    if (avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, out_filename) < 0)
    {
        printf("failed to create output format context\n");
        release_context(&in_fmt_ctx, out_fmt_ctx, NULL, NULL);
        return -1;
    }
    //遍历所有输入流
    for (int i = 0; i < in_fmt_ctx->nb_streams; i++)
    {
        AVStream *in_stream = in_fmt_ctx->streams[i];
        //创建输出流
        AVStream *out_stream = avformat_new_stream(out_fmt_ctx, NULL);
        if (!out_stream)
        {
            printf("failed to create new stream\n");
            release_context(&in_fmt_ctx, out_fmt_ctx, NULL, NULL);
            return -1;
        }
        //拷贝编解码器的参数设置
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0)
        {
            printf("failed to copy codec parameters from src to dst\n");
            release_context(&in_fmt_ctx, out_fmt_ctx, NULL, NULL);
            return -1;
        }
        //不同封装格式码流格式不同，所以要将codec_tag设为0
        //这样ffmpeg会自动选择和封装格式匹配的码流格式
        out_stream->codecpar->codec_tag = 0;
    }
    //格式化输出输出流信息
    av_dump_format(out_fmt_ctx, 0, out_filename, 1);
    //判断该上下文是否依赖于输入输出，1：不依赖，0：依赖
    if (!((out_fmt_ctx->oformat->flags) & AVFMT_NOFILE))
    {
        //打开输出文件
        if (avio_open2(&out_fmt_ctx->pb, out_filename, AVIO_FLAG_WRITE, NULL, NULL) < 0)
        {
            printf("failed to open output file\n");
            release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, NULL);
            return -1;
        }
    }
    //写入文件头
    if (avformat_write_header(out_fmt_ctx, NULL) < 0)
    {
        printf("failed to write header\n");
        release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, NULL);
        return -1;
    }
    AVBSFContext *bsf_ctx = NULL;
    //写入流信息
    while (true)
    {
        AVPacket pkt;
        memset(&pkt, 0, sizeof(pkt));
        //读取一帧视频或者几帧音频
        int ret = av_read_frame(in_fmt_ctx, &pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                printf("end of file\n");
                //取消对缓缓区的引用，将packet其他字段置为默认值
                av_packet_unref(&pkt);
                break;
            }
            printf("failed to read frame\n");
            av_packet_unref(&pkt);
            release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, NULL);
            return -1;
        }
        AVStream *in_stream = in_fmt_ctx->streams[pkt.stream_index];
        AVStream *out_stream = out_fmt_ctx->streams[pkt.stream_index];
        //将输入时间基表示的时间戳转换为输出时间基表示
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_PASS_MINMAX | AV_ROUND_NEAR_INF));
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_PASS_MINMAX | AV_ROUND_NEAR_INF));
        pkt.duration = av_rescale_q_rnd(pkt.duration, in_stream->time_base, out_stream->time_base, (AVRounding)AV_ROUND_NEAR_INF);
        /**
         * h264有两种码流格式，一种是avcc，另一种是annexb
         * map4支持的是avcc，avcc的NALU前面有NALU的长度
         * 而annexb的NALU前面是起始码，有的是4字节的00 00 00 01，有的是3字节的00 00 01
         * 而不同封装格式对h264码流格式的支持也不一样，因此需要使用过滤器进行转换
         * 将avcc的码流格式转换为annexb
        */
       //**********************过滤开始****************************
       std::string out_filename_str(out_filename);
       //限定为输出格式为avi且是视频流
       if (out_filename_str.find(".avi") != std::string::npos && out_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
       {
            //获取比特流过滤器
            const AVBitStreamFilter *filter = av_bsf_get_by_name("h264_mp4toannexb");
            if (!filter)
            {
                printf("failed to get bit stream filter\n");
                av_packet_unref(&pkt);
                release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, NULL);
                return -1;
            }
            //创建AVBSFContext
            if (av_bsf_alloc(filter, &bsf_ctx) < 0)
            {
                printf("failed to alloc AVBSFContext\n");
                av_packet_unref(&pkt);
                release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
                return -1;
            }
            //拷贝编解码器参数
            if (avcodec_parameters_copy(bsf_ctx->par_in, out_stream->codecpar) < 0)
            {
                printf("failed to copy codec parameters from src to dst\n");
                av_packet_unref(&pkt);
                release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
                return -1;
            }
            //初始化AVBSFContext
            if (av_bsf_init(bsf_ctx) < 0)
            {
                printf("failed to init AVBSFContext\n");
                av_packet_unref(&pkt);
                release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
                return -1;
            }
            //将数据送入过滤器
            ret = av_bsf_send_packet(bsf_ctx, &pkt);
            if (ret < 0)
            {
                //单个packet不足以完成过滤，需要继续送入数据
                if (ret == AVERROR(EAGAIN))
                {
                    av_packet_unref(&pkt);
                    continue;
                }
                printf("failed to send pakcet to bit stream filter\n");
                av_packet_unref(&pkt);
                release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
                return -1;
            }
            //接收过滤后的数据
            //由于单个输入packet可能产生多个输出packet，因此需要使用循环
            int ans = 0;
            do
            {
                AVPacket filtered_pkt;
                //初始化AVPacket
                memset(&pkt, 0, sizeof(filtered_pkt));
                //获取过滤后的数据
                ans = av_bsf_receive_packet(bsf_ctx, &filtered_pkt);
                if (ans < 0)
                {
                    if (ans == AVERROR_EOF)
                    {
                        av_packet_unref(&pkt);
                        av_packet_unref(&filtered_pkt);
                        break;
                    }
                    if (ans != AVERROR(EAGAIN))
                    {
                        printf("failed to reveive filtered packet\n");
                        av_packet_unref(&pkt);
                        av_packet_unref(&filtered_pkt);
                        release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
                        return -1;
                    }
                }
                //交错写入
                if (av_interleaved_write_frame(out_fmt_ctx, &filtered_pkt) < 0)
                {
                    printf("failed to write frame\n");
                    av_packet_unref(&pkt);
                    av_packet_unref(&filtered_pkt);
                    release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
                    return -1;
                }
                av_packet_unref(&filtered_pkt);
            } while (ans == AVERROR(EAGAIN));
            //**********************过滤结束****************************
       }
       else
       {
            //交错写入
            if (av_interleaved_write_frame(out_fmt_ctx, &pkt) < 0)
            {
                printf("failed to write frame\n");
                av_packet_unref(&pkt);
                release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
                return -1;
            }
       }
    }
    //写入文件尾
    if (av_write_trailer(out_fmt_ctx) != 0)
    {
        printf("failed to write tail\n");
        release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);
        return -1;
    }
    release_context(&in_fmt_ctx, out_fmt_ctx, out_fmt_ctx->pb, &bsf_ctx);

    return 0;
}

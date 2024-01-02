#include <stdio.h>
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/bsf.h"
};

//手动控制是否需要将h264的avcC码流转换为annexB码流
//1：avcC码流转换为annexB码流
//0：保持avcC码流不转换
#define H264_AnnexB 0

void release_context(AVFormatContext *in_fmt_ctx, AVFormatContext *out_fmt_ctx1, AVFormatContext *out_fmt_ctx2, AVPacket *pkt1, AVPacket *pkt2, AVBSFContext *bsf_ctx)
{
    if (out_fmt_ctx1 && !(out_fmt_ctx1->oformat->flags & AVFMT_NOFILE))
    {
        avio_close(out_fmt_ctx1->pb);
    }
    if (out_fmt_ctx2 && !(out_fmt_ctx2->oformat->flags & AVFMT_NOFILE))
    {
        avio_close(out_fmt_ctx2->pb);
    }
    avformat_close_input(&in_fmt_ctx);
    avformat_free_context(out_fmt_ctx1);
    avformat_free_context(out_fmt_ctx2);
    av_packet_unref(pkt1);
    av_packet_unref(pkt2);
    av_bsf_free(&bsf_ctx);
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("argument error, caller should pass a filename as argument, for example, \"./main src.mp4\"\n");
        return -1;
    }

    //输入文件
    const char *in_filename = argv[1];
    //输出分离的音视频
    const char *out_filename_video = "output.h264";
    const char *out_filename_audio = "output.aac";
    //输入文件、输出视频码流以及输出音频码流的AVFormatContext
    AVFormatContext *in_fmt_ctx = NULL, *out_fmt_ctx_video = NULL, *out_fmt_ctx_audio = NULL;
    AVPacket pkt, filtered_pkt;
    AVBSFContext *av_bsf_ctx = NULL;
    memset(&pkt, 0, sizeof(pkt));
    memset(&filtered_pkt, 0, sizeof(filtered_pkt));

    //打开输入
    if (avformat_open_input(&in_fmt_ctx, in_filename, NULL, NULL) < 0)
    {
        printf("failed to open input file\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }
    //寻找流信息以及编解码信息
    if (avformat_find_stream_info(in_fmt_ctx, NULL) < 0)
    {
        printf("failed to find stream info\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }

    //分配输出AVFormatContext
    if (avformat_alloc_output_context2(&out_fmt_ctx_video, NULL, NULL, out_filename_video) < 0)
    {
        printf("failed to alloc video AVFormatContext\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }
    if (avformat_alloc_output_context2(&out_fmt_ctx_audio, NULL, NULL, out_filename_audio) < 0)
    {
        printf("failed to alloc audio AVFormatContext\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }

    //创建输出流，拷贝编解码器参数
    int stream_index_video = -1, stream_index_audio = -1;
    for (int i = 0; i < in_fmt_ctx->nb_streams; i++)
    {
        AVStream *in_stream = NULL, *out_stream = NULL;
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            //获取视频流的索引
            stream_index_video = i;
            in_stream = in_fmt_ctx->streams[i];
            //创建输出视频流
            out_stream = avformat_new_stream(out_fmt_ctx_video, NULL);
            if (!out_stream)
            {
                printf("failed to create new stream for video AVFormatContext\n");
                release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                return -1;
            }
        }
        else if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            //获取音频流的索引
            stream_index_audio = i;
            in_stream = in_fmt_ctx->streams[i];
            //创建输出音频流
            out_stream = avformat_new_stream(out_fmt_ctx_audio, NULL);
            if (!out_stream)
            {
                printf("failed to create new stream for audio AVFormatContext\n");
                release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                return -1;
            }
        }
        //拷贝编解码器参数
        if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0)
        {
            printf("failed to copy codec parameters from input to output\n");
            release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
            return -1;
        }
        //根据输出格式自动选择匹配的码流类型
        out_stream->codecpar->codec_tag = 0;
    }

    //打开输出视频文件
    if (!(out_fmt_ctx_video->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open2(&out_fmt_ctx_video->pb, out_filename_video, AVIO_FLAG_WRITE, NULL, NULL) < 0)
        {
            printf("failed to open output video file\n");
            release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
            return -1;
        }
    }
    //打开输出音频文件
    if (!(out_fmt_ctx_audio->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open2(&out_fmt_ctx_audio->pb, out_filename_audio, AVIO_FLAG_WRITE, NULL, NULL) < 0)
        {
            printf("failed to open output audio file\n");
            release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
            return -1;
        }
    }

    //写入文件头
    if (avformat_write_header(out_fmt_ctx_video, NULL) < 0)
    {
        printf("failed to write header to the output video file\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }
    if (avformat_write_header(out_fmt_ctx_audio, NULL) < 0)
    {
        printf("failed to write header to the output audio file\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }

    //读取帧，时间戳转换，视频流帧的码流转换，写入帧
    while (true)
    {
        //读取帧
        int ans = av_read_frame(in_fmt_ctx, &pkt);
        if (ans < 0)
        {
            if (ans == AVERROR_EOF)
            {
                break;
            }
            printf("failed to read frame from input\n");
            release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
            return -1;
        }

        AVStream *in_stream = in_fmt_ctx->streams[pkt.stream_index];
        AVStream *out_stream = NULL;
        if (pkt.stream_index == stream_index_video)
        {
            out_stream = out_fmt_ctx_video->streams[0];
        }
        else if(pkt.stream_index == stream_index_audio)
        {
            out_stream = out_fmt_ctx_audio->streams[0];
        }
        //packet中的帧在流中的索引
        //视频流还是音频流
        int pkt_in_stream_index = pkt.stream_index;
        //将输入时间基表示的时间戳转换为输出时间基表示
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding )(AV_ROUND_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding )(AV_ROUND_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q_rnd(pkt.duration, in_stream->time_base, out_stream->time_base, (AVRounding)AV_ROUND_INF);
        //因为输出视频和音频的AVFormatContext只有一个流，所以置为0
        pkt.stream_index = 0;
        if (pkt_in_stream_index == stream_index_video)
        {
            #if H264_AnnexB
                //获取比特流过滤器
                const AVBitStreamFilter *av_bsf = av_bsf_get_by_name("h264_mp4toannexb");
                if (!av_bsf)
                {
                    printf("failed to get bit stream filter\n");
                    release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                    return -1;
                }
                //分配AVBSFContext
                if (av_bsf_alloc(av_bsf, &av_bsf_ctx) < 0)
                {
                    printf("failed to alloc AVBSFContext\n");
                    release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                    return -1;
                }
                //拷贝编解码器参数
                if (avcodec_parameters_copy(av_bsf_ctx->par_in, in_fmt_ctx->streams[stream_index_video]->codecpar) < 0)
                {
                    printf("failed to copy codec parameters from src to dst\n");
                    release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                    return -1;
                }
                //初始化AVBSFContext
                if (av_bsf_init(av_bsf_ctx) < 0)
                {
                    printf("failed to init AVBSFContext\n");
                    release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                    return -1;
                }

                //将packet送入过滤器
                int res = av_bsf_send_packet(av_bsf_ctx, &pkt);
                if (res < 0)
                {
                    //一个packet不足以完成过滤，继续读取下一个packet
                    if (res == AVERROR(EAGAIN))
                    {
                        continue;
                    }
                    printf("failed to send packet to filter\n");
                    release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                    return -1;
                }

                //一个packet过滤后可能生成多个输出packet，需要循环读取
                do
                {
                    //获取过滤后的packet
                    res = av_bsf_receive_packet(av_bsf_ctx, &filtered_pkt);
                    if (res < 0)
                    {
                        if (res == AVERROR_EOF)
                        {
                            break;
                        }
                        if (res != AVERROR(EAGAIN))
                        {
                            printf("failed to receive packet from filter\n");
                            release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                            return -1;
                        }
                    }
                    //写入过滤后的帧
                    if (av_interleaved_write_frame(out_fmt_ctx_video, &filtered_pkt) < 0)
                    {
                        printf("failed to write frame to the output video\n");
                        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                        return -1;
                    }
                    av_packet_unref(&filtered_pkt);
                } while (res == AVERROR(EAGAIN));
            #else
                //写入帧
                if (av_interleaved_write_frame(out_fmt_ctx_video, &pkt) < 0)
                {
                    printf("failed to write frame to the output video\n");
                    release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                    return -1;
                }
            #endif
        }
        else if (pkt_in_stream_index == stream_index_audio)
        {
            //写入帧
            if (av_interleaved_write_frame(out_fmt_ctx_audio, &pkt) < 0)
            {
                printf("failed to write frame to the output audio\n");
                release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
                return -1;
            }
        }
        av_packet_unref(&pkt);
    }

    //写入文件尾
    if (av_write_trailer(out_fmt_ctx_audio) < 0)
    {
        printf("failed to write tail to the output audio\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }
    if (av_write_trailer(out_fmt_ctx_video) < 0)
    {
        printf("failed to write tail to the output video\n");
        release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);
        return -1;
    }    
    
    release_context(in_fmt_ctx, out_fmt_ctx_video, out_fmt_ctx_audio, &pkt, &filtered_pkt, av_bsf_ctx);

    return 0;
}
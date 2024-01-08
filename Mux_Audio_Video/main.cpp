#include <stdio.h>
extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/bsf.h"
};

#define AAC_ADTS_TO_ASC 0
#define H264_AVCC_TO_ANNEXB 0

void release_context(AVFormatContext *in_fmt_ctx1, AVFormatContext *in_fmt_ctx2, AVFormatContext *out_fmt_ctx, AVPacket *pkt1, AVPacket *pkt2,
AVBSFContext *bsf_ctx1, AVBSFContext *bsf_ctx2)
{
    if (out_fmt_ctx && !((out_fmt_ctx->oformat->flags) & AVFMT_NOFILE))
    {
        avio_close(out_fmt_ctx->pb);
    }

    avformat_close_input(&in_fmt_ctx1);
    avformat_close_input(&in_fmt_ctx2);
    avformat_free_context(out_fmt_ctx);
    av_packet_unref(pkt1);
    av_packet_unref(pkt2);
    av_bsf_free(&bsf_ctx1);
    av_bsf_free(&bsf_ctx2);
}

int main(int argc, char *argv[])
{
    if (argc < 4)
    {
        printf("argument error, caller should pass 3 filenames as arguments, for example, \"./main input_video.h264 input_audio.aac output_video.mp4\"\n");
        return -1;
    }

    const char *in_filename_video = argv[1];
    const char *in_filename_audio = argv[2];
    const char *out_filename_video = argv[3];

    AVFormatContext *in_fmt_ctx_video = NULL, *in_fmt_ctx_audio = NULL, *out_fmt_ctx = NULL;
    AVPacket pkt, pkt_filtered;
    memset(&pkt, 0, sizeof(pkt));
    memset(&pkt_filtered, 0, sizeof(pkt_filtered));
    AVBSFContext *bsf_ctx_video = NULL, *bsf_ctx_audio = NULL;

    int64_t ts_video = 0, ts_audio = 0;
    AVRational time_base_in_video, time_base_in_audio;

    int in_video_index = -1, in_audio_index = -1, out_video_index = -1, out_audio_index = -1;

    //打开输入视频，初始化AVFormatContext
    if (avformat_open_input(&in_fmt_ctx_video, in_filename_video, NULL, NULL) < 0)
    {
        printf("failed to open input video file\n");
        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
        return -1;
    }
    //寻找输入视频流信息
    if (avformat_find_stream_info(in_fmt_ctx_video, NULL) < 0)
    {
        printf("failed to find input video stream info\n");
        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
        return -1;
    }

    //格式化输出输入流信息
    av_dump_format(in_fmt_ctx_video, 0, in_filename_video, 0);

    //打开输入音频，初始化AVFormatContext
    if (avformat_open_input(&in_fmt_ctx_audio, in_filename_audio, NULL, NULL) < 0)
    {
        printf("failed to open input audio file\n");
        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
        return -1;
    }
    //寻找输入音频流信息
    if (avformat_find_stream_info(in_fmt_ctx_audio, NULL) < 0)
    {
        printf("failed to find input audio stream info\n");
        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
        return -1;
    }

    //格式化输出输入流信息
    av_dump_format(in_fmt_ctx_audio, 0, in_filename_audio, 0);

    //分配输出AVFormatContext
    if (avformat_alloc_output_context2(&out_fmt_ctx, NULL, NULL, out_filename_video) < 0)
    {
        printf("failed to alloc output AVFormatContext\n");
        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
        return -1;
    }

    //遍历输入视频，寻找视频流，为输出AVFormatContext创建新流，拷贝编解码器参数
    for (int i = 0; i < in_fmt_ctx_video->nb_streams; i++)
    {
        if (in_fmt_ctx_video->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            AVStream *in_stream = in_fmt_ctx_video->streams[i];
            time_base_in_video = in_stream->time_base;
            //创建流
            AVStream *out_stream = avformat_new_stream(out_fmt_ctx, NULL);
            if (!out_stream)
            {
                printf("failed to create new stream for output AVFormatContext\n");
                release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                return -1;
            }
            //拷贝编解码器参数
            if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0)
            {
                printf("failed to copy codec parameters form input video to output video\n");
                release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                return -1;
            }
            //自动选择符合输出格式的码流类型
            out_stream->codecpar->codec_tag = 0;
            //输入视频流索引
            in_video_index = i;
            //输出视频流索引
            out_video_index = 0;
            break;
        }
    }

    //遍历输入音频，寻找音频流，为输出AVFormatContext创建新流，拷贝编解码器参数
    for (int i = 0; i < in_fmt_ctx_audio->nb_streams; i++)
    {
        if (in_fmt_ctx_audio->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            AVStream *in_stream = in_fmt_ctx_audio->streams[i];
            time_base_in_audio = in_stream->time_base;
            //创建流
            AVStream *out_stream = avformat_new_stream(out_fmt_ctx, NULL);
            if (!out_stream)
            {
                printf("failed to create new stream for output AVFormatContext\n");
                release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                return -1;
            }
            //拷贝编解码器参数
            if (avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar) < 0)
            {
                printf("failed to copy codec parameters form input audio to output video\n");
                release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                return -1;
            }
            //自动选择符合输出格式的码流类型
            out_stream->codecpar->codec_tag = 0;
            //输入音频流索引
            in_audio_index = i;
            //输出音频流索引
            out_audio_index = 1;
            break;
        }
    }

    //格式化输出输出音视频流信息
    av_dump_format(out_fmt_ctx, 0, out_filename_video, 1);

    //打开输出文件
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open2(&out_fmt_ctx->pb, out_filename_video, AVIO_FLAG_WRITE, NULL, NULL) < 0)
        {
            printf("failed to open output file\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }
    }

    //写入文件头
    if (avformat_write_header(out_fmt_ctx, NULL) < 0)
    {
        printf("failed to write header to the output file\n");
        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
        return -1;
    }

    #if AAC_ADTS_TO_ASC
        //获取比特流过滤器
        const AVBitStreamFilter *bsf_audio = av_bsf_get_by_name("aac_adtstoasc");
    #endif

    #if H264_AVCC_TO_ANNEXB
        //获取比特流过滤器
        const AVBitStreamFilter *bsf_video = av_bsf_get_by_name("h264_mp4toannexb");
    #endif

    #if AAC_ADTS_TO_ASC
        //分配比特流过滤器上下文AVBSFContext
        if (av_bsf_alloc(bsf_audio, &bsf_ctx_audio) < 0)
        {
            printf("failed to alloc AVBSFContext\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }
        //拷贝编解码器参数
        if (avcodec_parameters_copy(bsf_ctx_audio->par_in, in_fmt_ctx_audio->streams[in_audio_index]->codecpar) < 0)
        {
            printf("failed to copy codec parameters from input audio to the bi stream filter context\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }
        //初始化AVBSFContext
        if (av_bsf_init(bsf_ctx_audio) < 0)
        {
            printf("failed to init AVBSFContext\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }
    #endif

    #if H264_AVCC_TO_ANNEXB
        //分配比特流过滤器上下文AVBSFContext
        if (av_bsf_alloc(bsf_video, &bsf_ctx_video) < 0)
        {
            printf("failed to alloc AVBSFContext\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }
        //拷贝编解码器参数
        if (avcodec_parameters_copy(bsf_ctx_video->par_in, in_fmt_ctx_video->streams[in_video_index]->codecpar) < 0)
        {
            printf("failed to copy codec parameters from input video to the bit stream filter context\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }
        //初始化AVBSFContext
        if (av_bsf_init(bsf_ctx_video) < 0)
        {
            printf("failed to init AVBSFContext\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }
    #endif

    int frame_index = 0;
    AVFormatContext *fmt_ctx_v_or_a = NULL;
    while (true)
    {
        AVStream *in_stream = NULL, *out_stream = NULL;
        //比较时间戳，以判断先处理并写入音频还是视频帧
        int ret = av_compare_ts(ts_video, time_base_in_video, ts_audio, time_base_in_audio);
        switch (ret)
        {
            case -1:
            case 0:
            {
                fmt_ctx_v_or_a = in_fmt_ctx_video;
                in_stream = in_fmt_ctx_video->streams[in_video_index];
                out_stream = out_fmt_ctx->streams[out_video_index];
                break;
            }
            case 1:
            {
                fmt_ctx_v_or_a = in_fmt_ctx_audio;
                in_stream = in_fmt_ctx_audio->streams[in_audio_index];
                out_stream = out_fmt_ctx->streams[out_audio_index];
                break;
            }
            default:
            {
                printf("undefined result\n");
                release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                return -1;
            }
        }

        //读取音视频帧
        ret = av_read_frame(fmt_ctx_v_or_a, &pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                break;
            }

            printf("failed to read frame from input file\n");
            release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
            return -1;
        }

        //读取的是输入的视频文件
        //如果读取到的帧不是视频帧则重新读取
        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (pkt.stream_index != in_video_index)
            {
                av_packet_unref(&pkt);
                continue;
            }
        }
        //读取的是输入的音频文件
        //如果读取到的不是音频帧则重新读取
        else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if (pkt.stream_index != in_audio_index)
            {
                av_packet_unref(&pkt);
                continue;
            }
        }

        //有的码流没有pts，例如原始的H.264码流
        //因此需要自己手动设置pts
        if (pkt.pts == AV_NOPTS_VALUE)
        {
            //两帧之间的间隔
            int frame_duration = AV_TIME_BASE / av_q2d(in_stream->r_frame_rate);
            //计算pts以输入流时间基表示的ffmpeg内部时间
            pkt.pts = frame_index * frame_duration / (av_q2d(in_stream->time_base) * AV_TIME_BASE);
            //解决2倍速问题...
            pkt.pts *= 2;
            //计算duration以输入流时间基表示的ffmpeg内部时间
            pkt.duration = frame_duration / (av_q2d(in_stream->time_base) * AV_TIME_BASE);
            pkt.dts = pkt.pts;
            frame_index++;
        }

        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            ts_video = pkt.pts;
        }
        else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            ts_audio = pkt.pts;
        }

        //时间戳转换
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q_rnd(pkt.duration, in_stream->time_base, out_stream->time_base, (AVRounding)AV_ROUND_INF);
        //输出文件的音视频流来自不同的文件，因此packet中流的索引与输出文件中流的索引可能不匹配，可能出现packet中音频帧和视频帧所对应的stream_index是一样的的情况
        //因此将packet中的音频或视频帧与输出流的音视频流的索引匹配上
        pkt.stream_index = out_stream->index;

        AVBSFContext *bsf_ctx = NULL;

        #if AAC_ADTS_TO_ASC
            if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                bsf_ctx = bsf_ctx_audio;
            }
        #endif

        #if H264_AVCC_TO_ANNEXB
            if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                bsf_ctx = bsf_ctx_video;
            }
        #endif

        if ((AAC_ADTS_TO_ASC && in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) || (H264_AVCC_TO_ANNEXB && in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
        {                
            //将packet送入过滤器
            int ans = av_bsf_send_packet(bsf_ctx, &pkt);
            if (ans < 0)
            {
                //需要多个packet才能过滤
                if (ans == AVERROR(EAGAIN))
                {
                    av_packet_unref(&pkt);
                    continue;
                }
                printf("failed to send packet to filter\n");
                release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                return -1;
            }

            //一个输入packet可能产生多个输出packet
            do
            {
                ans = av_bsf_receive_packet(bsf_ctx, &pkt_filtered);
                if (ans < 0 && ans != AVERROR(EAGAIN))
                {
                    if (ans == AVERROR_EOF)
                    {
                        break;
                    }
                    else
                    {
                        printf("failed to receive packet from filter\n");
                        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                        return -1;
                    }
                }
                //交错写入
                if (av_interleaved_write_frame(out_fmt_ctx, &pkt_filtered) < 0)
                {
                    printf("failed to write frame to the output file\n");
                    release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                    return -1;                    
                }
                av_packet_unref(&pkt_filtered);
            } while (ans == AVERROR(EAGAIN));
        }
        else
        {
            //交错写入
            if (av_interleaved_write_frame(out_fmt_ctx, &pkt) < 0)
            {
                printf("failed to write frame to the output file\n");
                release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
                return -1;
            }
        }

        av_packet_unref(&pkt);
    }

    //写入文件尾
    if (av_write_trailer(out_fmt_ctx) < 0)
    {
        printf("failed to write tail to the output file\n");
        release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
        return -1;
    }

    release_context(in_fmt_ctx_video, in_fmt_ctx_audio, out_fmt_ctx, &pkt, &pkt_filtered, bsf_ctx_video, bsf_ctx_audio);
    return 0;
}
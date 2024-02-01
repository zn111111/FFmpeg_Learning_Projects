extern "C"
{
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavfilter/avfilter.h"
    #include "libavutil/opt.h"
    #include "libavfilter/buffersrc.h"
    #include "libavfilter/buffersink.h"
};

//打开输入文件
int open_input_file(int &audio_stream_index, int &video_stream_index, const char *filename, AVFormatContext **in_fmt_ctx,
AVCodecContext **audio_dec_ctx, AVCodecContext **video_dec_ctx)
{
    //初始化输入AVFormatContext
    if (avformat_open_input(in_fmt_ctx, filename, NULL, NULL) < 0)
    {
        printf("failed to alloc input AVFormatContext\n");
        return -1;
    }
    //寻找流信息
    if (avformat_find_stream_info(*in_fmt_ctx, NULL) < 0)
    {
        //destroy(in_fmt_ctx);
        printf("failed to find stream information\n");
        return -1;
    }
    //格式化输出输入文件信息
    av_dump_format(*in_fmt_ctx, 0, filename, 0);

    for (int i = 0; i < (*in_fmt_ctx)->nb_streams; i++)
    {
        //视频流
        if ((*in_fmt_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video_stream_index = i;
            //寻找视频解码器
            const AVCodec *video_codec = avcodec_find_decoder((*in_fmt_ctx)->streams[i]->codecpar->codec_id);
            if (!video_codec)
            {
                printf("failed to find video decoder\n");
                return -1;
            }
            //分配视频AVCodecContext
            *video_dec_ctx = avcodec_alloc_context3(video_codec);
            if (!(*video_dec_ctx))
            {
                printf("failed to alloc video decoder AVCodecContext\n");
                return -1;
            }
            //拷贝AVCodecParameters给AVCodecContext
            if (avcodec_parameters_to_context(*video_dec_ctx, (*in_fmt_ctx)->streams[i]->codecpar) < 0)
            {
                printf("failed to copy AVCodecParameters to AVCodecContext\n");
                return -1;
            }
            (*video_dec_ctx)->pkt_timebase = (*in_fmt_ctx)->streams[i]->time_base;
            //打开视频解码器
            if (avcodec_open2(*video_dec_ctx, video_codec, NULL) < 0)
            {
                printf("failed to open video decoder AVCodecContext\n");
                return -1;
            }
        }
        //音频流
        else if ((*in_fmt_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio_stream_index = i;
            //寻找音频解码器
            const AVCodec *audio_codec = avcodec_find_decoder((*in_fmt_ctx)->streams[i]->codecpar->codec_id);
            if (!audio_codec)
            {
                printf("failed to find audio decoder\n");
                return -1;
            }
            //分配音频AVCodecContext
            *audio_dec_ctx = avcodec_alloc_context3(audio_codec);
            if (!(*audio_dec_ctx))
            {
                printf("failed to alloc audio decoder AVCodecContext\n");
                return -1;
            }
            //拷贝AVCodecParameters给AVCodecContext
            if (avcodec_parameters_to_context(*audio_dec_ctx, (*in_fmt_ctx)->streams[i]->codecpar) < 0)
            {
                printf("failed to copy AVCodecParameters to AVCodecContext\n");
                return -1;
            }
            (*audio_dec_ctx)->pkt_timebase = (*in_fmt_ctx)->streams[i]->time_base;
            //打开音频解码器
            if (avcodec_open2(*audio_dec_ctx, audio_codec, NULL) < 0)
            {
                printf("falied to open audio decoder AVCodecContext\n");
                return -1;
            }
        }
    }

    return 0;
}

//打开输出文件
int open_output_file(const char *filename, AVFormatContext **in_fmt_ctx, AVFormatContext **out_fmt_ctx, AVCodecContext **audio_enc_ctx, AVCodecContext **video_enc_ctx)
{
    //分配输出AVFormatContext
    if (avformat_alloc_output_context2(out_fmt_ctx, NULL, NULL, filename) < 0)
    {
        printf("failed to alloc output AVFormatContext\n");
        return -1;
    }
    
    for (int i = 0; i < (*in_fmt_ctx)->nb_streams; i++)
    {
        if ((*in_fmt_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            //创建输出流
            AVStream *out_stream = avformat_new_stream(*out_fmt_ctx, NULL);
            if (!out_stream)
            {
                printf("failed to create output stream\n");
                return -1;
            }

            //寻找编码器
            const AVCodec *video_codec = avcodec_find_encoder((*in_fmt_ctx)->streams[i]->codecpar->codec_id);
            if (!video_codec)
            {
                printf("failed to find video encoder\n");
                return -1;
            }
            //分配视频AVCodecContext
            *video_enc_ctx = avcodec_alloc_context3(video_codec);
            if (!(*video_enc_ctx))
            {
                printf("failed to alloc video encoder AVCodecContext\n");
                return -1;
            }
            
            //初始化视频AVCodecContext
            (*video_enc_ctx)->height = (*in_fmt_ctx)->streams[i]->codecpar->height;
            (*video_enc_ctx)->width = (*in_fmt_ctx)->streams[i]->codecpar->width;
            (*in_fmt_ctx)->streams[i]->codecpar->framerate = av_guess_frame_rate(*in_fmt_ctx, (*in_fmt_ctx)->streams[i], NULL);
            //视频流的时间基是1 / 帧率
            (*video_enc_ctx)->time_base = av_inv_q((*in_fmt_ctx)->streams[i]->codecpar->framerate);
            (*video_enc_ctx)->sample_aspect_ratio = (*in_fmt_ctx)->streams[i]->codecpar->sample_aspect_ratio;
            if (video_codec->pix_fmts)
            {
                (*video_enc_ctx)->pix_fmt = video_codec->pix_fmts[0];
            }

            // //全局头
            if ((*out_fmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
            {
                (*video_enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            //打开视频编码器
            if (avcodec_open2(*video_enc_ctx, video_codec, NULL) < 0)
            {
                printf("failed to open video AVCodecContext\n");
                return -1;
            }

            //拷贝AVCodecContext给AVCodecParameters
            if (avcodec_parameters_from_context((*out_fmt_ctx)->streams[i]->codecpar, *video_enc_ctx) < 0)
            {
                printf("failed to copy AVCodecContext to AVCodecParameters\n");
                return -1;
            }
        }
        else if ((*in_fmt_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            //创建输出流
            AVStream *out_stream = avformat_new_stream(*out_fmt_ctx, NULL);
            if (!out_stream)
            {
                printf("failed to create output stream\n");
                return -1;
            }
            //寻找编码器
            const AVCodec *audio_codec = avcodec_find_encoder((*in_fmt_ctx)->streams[i]->codecpar->codec_id);
            //分配音频AVCodecContext
            *audio_enc_ctx = avcodec_alloc_context3(audio_codec);
            if (!(*audio_enc_ctx))
            {
                printf("failed to alloc audio encoder AVCodecContext\n");
                return -1;
            }

            //初始化音频AVCodecContext
            //音频流的时间基是1 / 采样率
            (*audio_enc_ctx)->time_base = {1, (*in_fmt_ctx)->streams[i]->codecpar->sample_rate};
            if (audio_codec->sample_fmts)
            {
                (*audio_enc_ctx)->sample_fmt = audio_codec->sample_fmts[0];
            }
            (*audio_enc_ctx)->sample_rate = (*in_fmt_ctx)->streams[i]->codecpar->sample_rate;
            //立体声
            (*audio_enc_ctx)->channel_layout = AV_CH_LAYOUT_STEREO;
            //拷贝通道布局
            if (av_channel_layout_copy(&(*audio_enc_ctx)->ch_layout, &(*audio_enc_ctx)->ch_layout) < 0)
            {
                printf("failed to copy channel layout\n");
                return -1;
            }

            //全局头
            if ((*out_fmt_ctx)->oformat->flags & AVFMT_GLOBALHEADER)
            {
                (*audio_enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            //打开音频编码器
            if (avcodec_open2(*audio_enc_ctx, audio_codec, NULL) < 0)
            {
                printf("failed to open audio AVCodecContext\n");
                return -1;
            }

            //拷贝AVCodecContext给AVCodecParameters
            if (avcodec_parameters_from_context((*out_fmt_ctx)->streams[i]->codecpar, *audio_enc_ctx) < 0)
            {
                printf("failed to copy AVCodecContext to AVCodecParameters\n");
                return -1;
            }
        }
    }

    if (!((*out_fmt_ctx)->oformat->flags & AVFMT_NOFILE))
    {
        //打开输出文件
        if (avio_open2(&(*out_fmt_ctx)->pb, filename, AVIO_FLAG_WRITE, NULL, NULL) < 0)
        {
            printf("failed to open output file\n");
            return -1;
        }
    }

    //写入文件头
    if (avformat_write_header(*out_fmt_ctx, NULL) < 0)
    {
        printf("failed to write header\n");
        return -1;
    }

    //格式化输出输出文件信息
    av_dump_format(*out_fmt_ctx, 0, filename, 1);

    return 0;
}

//初始化滤镜
int init_filters(AVFormatContext **in_fmt_ctx, AVCodecContext **audio_enc_ctx, AVCodecContext **video_enc_ctx,
AVCodecContext **audio_dec_ctx, AVCodecContext **video_dec_ctx,
AVFilterGraph **audio_filter_graph, AVFilterGraph **video_filter_graph, AVFilterContext **audio_abuffer_filter_ctx,
AVFilterContext **audio_abuffersink_filter_ctx, AVFilterContext **video_buffer_filter_ctx, AVFilterContext **video_buffersink_filter_ctx,
AVFilterInOut **audio_output_port, AVFilterInOut **audio_input_port, AVFilterInOut **video_output_port, AVFilterInOut **video_input_port)
{
    for (int i = 0; i < (*in_fmt_ctx)->nb_streams; i++)
    {
        if ((*in_fmt_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            //分配视频滤镜图
            *video_filter_graph = avfilter_graph_alloc();
            if (!(*video_filter_graph))
            {
                printf("failed to alloc video filter graph\n");
                return -1;
            }
            //获取视频buffer滤镜
            const AVFilter *video_buffer_filter = avfilter_get_by_name("buffer");
            if (!video_buffer_filter)
            {
                printf("failed to get buffer filter\n");
                return -1;
            }
            //获取视频buffersink滤镜
            const AVFilter *video_buffersink_filter = avfilter_get_by_name("buffersink");
            if (!video_buffersink_filter)
            {
                printf("failed to get buffersink filter\n");
                return -1;
            }
            char args[4096] = {0};
            snprintf(args, sizeof(args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
            (*video_dec_ctx)->width, (*video_dec_ctx)->height, (*video_dec_ctx)->pix_fmt,
            (*video_dec_ctx)->pkt_timebase.num, (*video_dec_ctx)->pkt_timebase.den,
            (*video_dec_ctx)->sample_aspect_ratio.num, (*video_dec_ctx)->sample_aspect_ratio.den);
            //分配buffer AVFilterContext并将其添加到滤镜图中去
            if (avfilter_graph_create_filter(video_buffer_filter_ctx, video_buffer_filter, "in", args, NULL, *video_filter_graph) < 0)
            {
                printf("failed to alloc video buffer filter context\n");
                return -1;
            }
            //分配buffersink AVFilterContext并将其添加到滤镜图中去
            if (avfilter_graph_create_filter(video_buffersink_filter_ctx, video_buffersink_filter, "out", NULL, NULL, *video_filter_graph) < 0)
            {
                printf("failed to alloc video buffersink filter context\n");
                return -1;
            }
            //设置pix_fmt选项的值
            if (av_opt_set_bin(*video_buffersink_filter_ctx, "pix_fmts", (uint8_t *)&(*video_enc_ctx)->pix_fmt, sizeof(AVPixelFormat), AV_OPT_SEARCH_CHILDREN) < 0)
            {
                printf("failed to set video buffersink filter context option\n");
                return -1;
            }
            //分配buffer滤镜的输出引脚
            *video_output_port = avfilter_inout_alloc();
            if (!(*video_output_port))
            {
                printf("failed to alloc buffer output AVFilterInOut\n");
                return -1;
            }
            //初始化引脚
            (*video_output_port)->filter_ctx = *video_buffer_filter_ctx;
            //连接到字符串描述的滤镜图的第一个滤镜的输入端，该滤镜的输入端默认为"in"
            //字符串常量存储在只读数据段，同样的值只能存储一份
            //又有不只一个指针指向"in"和"out",因此需要拷贝
            (*video_output_port)->name = av_strdup("in");
            (*video_output_port)->next = NULL;
            (*video_output_port)->pad_idx =0;
            //分配buffersink滤镜的输入引脚
            *video_input_port = avfilter_inout_alloc();
            if (!(*video_input_port))
            {
                printf("failed to alloc buffersink input AVFilterInOut\n");
                return -1;
            }
            //初始化引脚
            (*video_input_port)->filter_ctx = *video_buffersink_filter_ctx;
            //连接到字符串描述的滤镜图的最后一个滤镜的输出端，该滤镜的输出端默认为"out"
            (*video_input_port)->name = av_strdup("out");
            (*video_input_port)->next = NULL;
            (*video_input_port)->pad_idx = 0;
            //将字符串描述的滤镜图插入到现存的滤镜图中去
            if (avfilter_graph_parse_ptr(*video_filter_graph, "null", video_input_port, video_output_port, NULL) < 0)
            {
                printf("failed to insert the filter graph described by string to the existed filter graph\n");
                return -1;
            }
            //为滤镜图中的滤镜建立连接
            if (avfilter_graph_config(*video_filter_graph, NULL) < 0)
            {
                printf("failed to link all the filter\n");
                return -1;
            }
        }
        else if ((*in_fmt_ctx)->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            //分配音频滤镜图
            *audio_filter_graph = avfilter_graph_alloc();
            if (!(*audio_filter_graph))
            {
                printf("failed to alloc audio filter graph\n");
                return -1;
            }
            //获取音频abuffer滤镜
            const AVFilter *audio_abuffer_filter = avfilter_get_by_name("abuffer");
            if (!audio_abuffer_filter)
            {
                printf("failed to get abuffer filter\n");
                return -1;
            }
            //获取音频abuffersink滤镜
            const AVFilter *audio_abuffersink_filter = avfilter_get_by_name("abuffersink");
            if (!audio_abuffersink_filter)
            {
                printf("failed to get abuffersink filter\n");
                return -1;
            }
            char ch_layout_desc[1028] = {0};
            //获取通道布局描述
            if (av_channel_layout_describe(&(*audio_dec_ctx)->ch_layout, ch_layout_desc, sizeof(ch_layout_desc)) < 0)
            {
                printf("failed to get channel layout description\n");
                return -1;
            }
            char args[4096] = {0};
            snprintf(args, sizeof(args), "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
            (*audio_dec_ctx)->time_base.num, (*audio_dec_ctx)->time_base.den, (*audio_dec_ctx)->sample_rate,
            av_get_sample_fmt_name((*audio_dec_ctx)->sample_fmt), ch_layout_desc);
            //分配abuffer AVFilterContext并将其添加到滤镜图中去
            if (avfilter_graph_create_filter(audio_abuffer_filter_ctx, audio_abuffer_filter, "in", args, NULL, *audio_filter_graph) < 0)
            {
                printf("failed to alloc audio abuffer filter context\n");
                return -1;
            }
            //分配abuffersink AVFilterContext并将其添加到滤镜图中去
            if (avfilter_graph_create_filter(audio_abuffersink_filter_ctx, audio_abuffersink_filter, "out", NULL, NULL, *audio_filter_graph) < 0)
            {
                printf("failed to alloc audio abuffersink filter context\n");
                return -1;
            }
            //设置sample_rates选项的值
            if (av_opt_set_bin(*audio_abuffersink_filter_ctx, "sample_rates", (uint8_t *)&(*audio_enc_ctx)->sample_rate, sizeof(int), AV_OPT_SEARCH_CHILDREN) < 0)
            {
                printf("failed to set audio abuffersink filter context option\n");
                return -1;
            }
            //设置sample_fmts选项的值
            if (av_opt_set_bin(*audio_abuffersink_filter_ctx, "sample_fmts", (uint8_t *)&(*audio_enc_ctx)->sample_fmt, sizeof(AVSampleFormat), AV_OPT_SEARCH_CHILDREN) < 0)
            {
                printf("failed to set audio abuffersink filter context option\n");
                return -1;
            }
            memset(args, 0, sizeof(args));
            if (av_channel_layout_describe(&(*audio_enc_ctx)->ch_layout, args, sizeof(args)) < 0)
            {
                printf("failed to get channel layout description\n");
                return -1;
            }
            //设置ch_layouts选项的值
            if (av_opt_set(*audio_abuffersink_filter_ctx, "ch_layouts", args, AV_OPT_SEARCH_CHILDREN) < 0)
            {
                printf("failed to set audio abuffersink filter context option\n");
                return -1;
            }
            //分配abuffer滤镜的输出引脚
            *audio_output_port = avfilter_inout_alloc();
            if (!(*audio_output_port))
            {
                printf("failed to alloc abuffer output AVFilterInOut\n");
                return -1;
            }
            //初始化引脚
            (*audio_output_port)->filter_ctx = *audio_abuffer_filter_ctx;
            (*audio_output_port)->name = av_strdup("in");
            (*audio_output_port)->next = NULL;
            (*audio_output_port)->pad_idx = 0;
            //分配abuffersink滤镜的输入引脚
            *audio_input_port = avfilter_inout_alloc();
            if (!(*audio_input_port))
            {
                printf("failed to alloc abuffersink input AVFilterInOut\n");
                return -1;
            }
            //初始化引脚
            (*audio_input_port)->filter_ctx = *audio_abuffersink_filter_ctx;
            (*audio_input_port)->name = av_strdup("out");
            (*audio_input_port)->next = NULL;
            (*audio_input_port)->pad_idx = 0;
            //将字符串描述的滤镜图插入到现存的滤镜图中去
            if (avfilter_graph_parse_ptr(*audio_filter_graph, "anull", audio_input_port, audio_output_port, NULL) < 0)
            {
                printf("failed to insert the filter graph described by string to the existed filter graph\n");
                return -1;
            }
            //为滤镜图中的滤镜建立连接
            if (avfilter_graph_config(*audio_filter_graph, NULL) < 0)
            {
                printf("failed to link all the filter\n");
                return -1;
            }
        }
    }

    return 0;
}

//编码，交错写入
int encode_write(AVFrame *filtered_frame, AVCodecContext *enc_ctx, AVFormatContext *out_fmt_ctx, int stream_id)
{
    filtered_frame->pts = av_rescale_q_rnd(filtered_frame->pts, filtered_frame->time_base, enc_ctx->time_base, (AVRounding)AV_ROUND_NEAR_INF);
    //将滤镜器处理后的音视频帧送入编码器进行编码
    if (avcodec_send_frame(enc_ctx, filtered_frame) < 0)
    {
        printf("failed to send frame to the encoder\n");
        return -1;
    }

    int ret = 0;
    AVPacket encoded_pkt;
    memset(&encoded_pkt, 0, sizeof(encoded_pkt));
    while (ret >= 0)
    {
        //接收编码后的音视频帧
        ret = avcodec_receive_packet(enc_ctx, &encoded_pkt);
        if (ret < 0)
        {
            if ((ret == AVERROR(EAGAIN)) || (ret == AVERROR_EOF))
            {
                continue;
            }

            printf("failed to receive encoded packet from the encoder\n");
            av_packet_unref(&encoded_pkt);
            return -1;
        }

        //对packet中的音视频帧的时间戳进行转换
        av_packet_rescale_ts(&encoded_pkt, enc_ctx->time_base, out_fmt_ctx->streams[stream_id]->time_base);
        //交错写入
        encoded_pkt.stream_index = stream_id;
        if (av_interleaved_write_frame(out_fmt_ctx, &encoded_pkt) < 0)
        {
            printf("failed to write encoded frame\n");
            av_packet_unref(&encoded_pkt);
            return -1;
        }

        av_packet_unref(&encoded_pkt);
    }

    return 0;
}

//将解码的音视频帧送入滤镜器处理，编码，交错写入
int filter_encode_write(AVFrame *decoded_frame, AVFilterContext *buffer_filter_ctx, AVFilterContext *buffersink_filter_ctx, AVCodecContext *enc_ctx,
AVFormatContext *out_fmt_ctx, int stream_id)
{
    AVFrame filtered_frame;
    memset(&filtered_frame, 0, sizeof(filtered_frame));
    //将解码的音视频帧送入buffer/abuffer滤镜器
    if (av_buffersrc_add_frame_flags(buffer_filter_ctx, decoded_frame, 0) < 0)
    {
        printf("failed to send frame to the buffersrc filter\n");
        return -1;
    }

    int ret = 0;
    while (ret >= 0)
    {
        //获取滤镜器处理后的帧
        ret = av_buffersink_get_frame(buffersink_filter_ctx, &filtered_frame);
        if (ret < 0)
        {
            if ((ret == AVERROR(EAGAIN)) || (ret == AVERROR_EOF))
            {
                continue;
            }

            printf("failed to filter frame\n");
            av_frame_unref(&filtered_frame);
            return -1;
        }

        filtered_frame.pict_type = AV_PICTURE_TYPE_NONE;
        filtered_frame.time_base = av_buffersink_get_time_base(buffersink_filter_ctx);
        //编码，交错写入
        if (encode_write(&filtered_frame, enc_ctx, out_fmt_ctx, stream_id) < 0)
        {
            av_frame_unref(&filtered_frame);
            return -1;
        }

        av_frame_unref(&filtered_frame);
    }

    return 0;
}

//转码
int transcoding(int audio_stream_index, int video_stream_index, AVFormatContext *in_fmt_ctx, AVCodecContext *audio_dec_ctx, AVCodecContext *video_dec_ctx,
AVFormatContext *out_fmt_ctx, AVCodecContext *audio_enc_ctx, AVCodecContext *video_enc_ctx,
AVFilterContext *audio_abuffer_filter_ctx, AVFilterContext *audio_abuffersink_filter_ctx,
AVFilterContext *video_buffer_filter_ctx, AVFilterContext *video_buffersink_filter_ctx)
{
    int stream_id = 0;
    AVPacket encoded_pkt;
    AVFrame decoded_frame;
    AVFrame filtered_frame;
    memset(&encoded_pkt, 0, sizeof(encoded_pkt));
    memset(&decoded_frame, 0, sizeof(decoded_frame));
    memset(&filtered_frame, 0, sizeof(filtered_frame));
    AVFilterContext *buffer_filter_ctx = NULL, *buffersink_filter_ctx = NULL;
    AVCodecContext *enc_ctx = NULL, *dec_ctx = NULL;
    //读取编码的帧
    while ((av_read_frame(in_fmt_ctx, &encoded_pkt) >= 0))
    {
        if (encoded_pkt.stream_index == audio_stream_index)
        {
            buffer_filter_ctx = audio_abuffer_filter_ctx;
            buffersink_filter_ctx = audio_abuffersink_filter_ctx;
            enc_ctx = audio_enc_ctx;
            dec_ctx = audio_dec_ctx;
            stream_id = audio_stream_index;
        }
        else if (encoded_pkt.stream_index == video_stream_index)
        {
            buffer_filter_ctx = video_buffer_filter_ctx;
            buffersink_filter_ctx = video_buffersink_filter_ctx;
            enc_ctx = video_enc_ctx;
            dec_ctx = video_dec_ctx;
            stream_id = video_stream_index;
        }
        
        if ((encoded_pkt.stream_index != audio_stream_index) && (encoded_pkt.stream_index != video_stream_index))
        {
            continue;
        }

        //将编码的音视频帧送入解码器
        if (avcodec_send_packet(dec_ctx, &encoded_pkt) < 0)
        {
            printf("failed to send encoded packet to the decoder\n");
            av_packet_unref(&encoded_pkt);
            return -1;
        }

        int ret = 0;
        while (ret >= 0)
        {
            //接收解码的音视频帧
            ret = avcodec_receive_frame(dec_ctx, &decoded_frame);
            if (ret < 0)
            {
                if ((ret ==  AVERROR(EAGAIN)) || (ret ==  AVERROR_EOF))
                {
                    continue;
                }

                printf("failed to decode packet\n");
                av_packet_unref(&encoded_pkt);
                av_frame_unref(&decoded_frame);
                return -1;
            }

            //使用滤镜器处理，编码，交错写入
            if (filter_encode_write(&decoded_frame, buffer_filter_ctx, buffersink_filter_ctx, enc_ctx, out_fmt_ctx, stream_id) < 0)
            {
                av_packet_unref(&encoded_pkt);
                av_frame_unref(&decoded_frame);
                return -1;
            }

            av_packet_unref(&encoded_pkt);
            av_frame_unref(&decoded_frame);
        }
    }

    for (int i = 0; i < in_fmt_ctx->nb_streams; i++)
    {
        if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            buffer_filter_ctx = audio_abuffer_filter_ctx;
            buffersink_filter_ctx = audio_abuffersink_filter_ctx;
            enc_ctx = audio_enc_ctx;
            dec_ctx = audio_dec_ctx;
            stream_id = i;
        }
        else if (in_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            buffer_filter_ctx = video_buffer_filter_ctx;
            buffersink_filter_ctx = video_buffersink_filter_ctx;
            enc_ctx = video_enc_ctx;
            dec_ctx = video_dec_ctx;
            stream_id = i;
        }

        //刷新解码器
        if (avcodec_send_packet(dec_ctx, NULL) < 0)
        {
            printf("failed to send null to flush decoder\n");
            return -1;
        }

        int ret = 0;
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(dec_ctx, &decoded_frame);
            if (ret < 0)
            {
                if ((ret == AVERROR(EAGAIN)) || (ret ==  AVERROR_EOF))
                {
                    continue;
                }

                printf("failed to receive flushed frame\n");
                av_frame_unref(&decoded_frame);
                return -1;
            }

            if (filter_encode_write(&decoded_frame, buffer_filter_ctx, buffersink_filter_ctx, enc_ctx, out_fmt_ctx, stream_id) < 0)
            {
                av_frame_unref(&decoded_frame);
                return -1;
            }

            av_frame_unref(&decoded_frame);
        }

        //刷新滤镜器
        if (av_buffersrc_add_frame_flags(buffer_filter_ctx, NULL, 0) < 0)
        {
            printf("failed to send null to flush filter\n");
            return -1;
        }

        ret = 0;
        while (ret >= 0)
        {
            ret = av_buffersink_get_frame(buffersink_filter_ctx, &filtered_frame);
            if (ret < 0)
            {
                if ((ret == AVERROR(EAGAIN)) || (ret ==  AVERROR_EOF))
                {
                    continue;
                }

                printf("failed to receive flushed frame\n");
                av_frame_unref(&filtered_frame);
                return -1;
            }

            if (encode_write(&filtered_frame, enc_ctx, out_fmt_ctx, stream_id) < 0)
            {
                av_frame_unref(&filtered_frame);
                return -1;
            }

            av_frame_unref(&filtered_frame);
        }

        //刷新编码器
        if (avcodec_send_frame(enc_ctx, NULL) < 0)
        {
            printf("failed to send null to flush encoder\n");
            return -1;
        }

        ret = 0;
        while (ret >= 0)
        {
            ret = avcodec_receive_packet(enc_ctx, &encoded_pkt);
            if (ret < 0)
            {
                if ((ret == AVERROR(EAGAIN)) || (ret ==  AVERROR_EOF))
                {
                    continue;
                }

                printf("failed to receive flushed packet\n");
                av_packet_unref(&encoded_pkt);
                return -1;
            }

            av_packet_rescale_ts(&encoded_pkt, enc_ctx->time_base, out_fmt_ctx->streams[stream_id]->time_base);
            encoded_pkt.stream_index = stream_id;

            if (av_interleaved_write_frame(out_fmt_ctx, &encoded_pkt) < 0)
            {
                printf("failed to write encoded frame\n");
                av_packet_unref(&encoded_pkt);
                return -1;
            }

            av_packet_unref(&encoded_pkt);
        }
    }

    //写入文件尾
    if (av_write_trailer(out_fmt_ctx) < 0)
    {
        printf("failed to write tail\n");
        return -1;
    }

    return 0;
}

void destroy(AVFormatContext **in_fmt_ctx, AVFormatContext **out_fmt_ctx, AVCodecContext **audio_dec_ctx, AVCodecContext **video_dec_ctx,
AVCodecContext **audio_enc_ctx, AVCodecContext **video_enc_ctx, AVFilterGraph **audio_filter_graph, AVFilterGraph **video_filter_graph)
{
    avformat_close_input(in_fmt_ctx);
    if (out_fmt_ctx && !((*out_fmt_ctx)->oformat->flags & AVFMT_NOFILE))
    {
        avio_close((*out_fmt_ctx)->pb);
    }
    avformat_free_context(*out_fmt_ctx);
    avcodec_free_context(audio_dec_ctx);
    avcodec_free_context(video_dec_ctx);
    avcodec_free_context(audio_enc_ctx);
    avcodec_free_context(video_enc_ctx);
    avfilter_graph_free(audio_filter_graph);
    avfilter_graph_free(video_filter_graph);
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("argument passing error\nUsage: ./main input_video.mp4 output_video.mp4\n");
        return 1;
    }

    int audio_stream_index = 0, video_stream_index = 0;
    AVFormatContext *in_fmt_ctx = NULL, *out_fmt_ctx = NULL;
    AVCodecContext *audio_dec_ctx = NULL, *video_dec_ctx = NULL;
    AVCodecContext *audio_enc_ctx = NULL, *video_enc_ctx = NULL;
    AVFilterGraph *audio_filter_graph = NULL, *video_filter_graph = NULL;
    AVFilterContext *audio_abuffer_filter_ctx = NULL, *audio_abuffersink_filter_ctx = NULL, *video_buffer_filter_ctx = NULL, *video_buffersink_filter_ctx = NULL;
    AVFilterInOut *audio_output_port = NULL, *audio_input_port = NULL, *video_output_port = NULL, *video_input_port = NULL;

    //打开输入文件
    if (open_input_file(audio_stream_index, video_stream_index, argv[1], &in_fmt_ctx, &audio_dec_ctx, &video_dec_ctx) < 0)
    {
        destroy(&in_fmt_ctx, &out_fmt_ctx, &audio_dec_ctx, &video_dec_ctx, &audio_enc_ctx, &video_enc_ctx, &audio_filter_graph, &video_filter_graph);
        return -1;
    }

    //打开输出文件
    if (open_output_file(argv[2], &in_fmt_ctx, &out_fmt_ctx, &audio_enc_ctx, &video_enc_ctx) < 0)
    {
        destroy(&in_fmt_ctx, &out_fmt_ctx, &audio_dec_ctx, &video_dec_ctx, &audio_enc_ctx, &video_enc_ctx, &audio_filter_graph, &video_filter_graph);
        return -1;
    }

    //初始化滤镜
    if (init_filters(&in_fmt_ctx, &audio_enc_ctx, &video_enc_ctx, &audio_dec_ctx, &video_dec_ctx, &audio_filter_graph, &video_filter_graph,
    &audio_abuffer_filter_ctx, &audio_abuffersink_filter_ctx, &video_buffer_filter_ctx, &video_buffersink_filter_ctx, &audio_output_port,
    &audio_input_port, &video_output_port, &video_input_port) < 0)
    {
        destroy(&in_fmt_ctx, &out_fmt_ctx, &audio_dec_ctx, &video_dec_ctx, &audio_enc_ctx, &video_enc_ctx, &audio_filter_graph, &video_filter_graph);
        return -1;
    }

    //转码
    if (transcoding(audio_stream_index, video_stream_index, in_fmt_ctx, audio_dec_ctx, video_dec_ctx, out_fmt_ctx, audio_enc_ctx, video_enc_ctx,
    audio_abuffer_filter_ctx, audio_abuffersink_filter_ctx, video_buffer_filter_ctx, video_buffersink_filter_ctx) < 0)
    {
        destroy(&in_fmt_ctx, &out_fmt_ctx, &audio_dec_ctx, &video_dec_ctx, &audio_enc_ctx, &video_enc_ctx, &audio_filter_graph, &video_filter_graph);
        return -1;
    }

    destroy(&in_fmt_ctx, &out_fmt_ctx, &audio_dec_ctx, &video_dec_ctx, &audio_enc_ctx, &video_enc_ctx, &audio_filter_graph, &video_filter_graph);
    return 0;
}
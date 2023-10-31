#include "EngineVideoManager.h"
#include <filesystem>

EngineVideoManager* EngineVideoManager::instance = nullptr;

EngineVideoManager::EngineVideoManager(){}

EngineVideoManager::~EngineVideoManager(){}

void EngineVideoManager::startCaptureVideo(int width, int height)
{
    isComputingVideo = true;
    videoWidth = width;
    videoHeight = height;
    initializeCodec("temp/tempvideo.h264");
}

void EngineVideoManager::addCaptureVideoFrame(wi::graphics::Texture& newFrame)
{
    encodeAndWriteVideo(newFrame);
}

void EngineVideoManager::stopCaptureVideo(std::string fileName)
{
    closeStream();
    reEncodeToMp4("temp/tempvideo.h264", fileName);
    std::filesystem::remove("temp/tempvideo.h264");
    isComputingVideo = false;
}

bool EngineVideoManager::isVideoInComputation()
{
    return this->isComputingVideo;
}

XMINT2 EngineVideoManager::getVideoSize()
{
    return XMINT2(videoWidth, videoHeight);
}

int EngineVideoManager::initializeCodec(std::string fileName)
{
    int valueReturn = 1;

#pragma warning (push)
#pragma warning (disable : 4996)
    av_register_all();
#pragma warning (pop)

    avformat_alloc_output_context2(&formatContext, nullptr, nullptr, fileName.c_str());

    if (formatContext == nullptr)
    {
        valueReturn = 0;
    }
    else
    {
        videoCodec = avcodec_find_encoder(AV_CODEC_ID_H264);

#pragma warning (push)
#pragma warning (disable : 4996)
        av_format_set_video_codec(formatContext, videoCodec);
#pragma warning (pop)

        outputFormat = formatContext->oformat;

        if (outputFormat->video_codec != AV_CODEC_ID_NONE)
        {
            addStream();
        }

        streamOut.nextPoint = 0;
        av_dump_format(formatContext, 0, fileName.c_str(), 1);

        openVideo();

        if (!(outputFormat->flags & AVFMT_NOFILE))
        {
            int ret = avio_open(&formatContext->pb, fileName.c_str(), AVIO_FLAG_WRITE);
            if (ret < 0)
            {
                //could not open file
                valueReturn = 0;
            }
            else
            {
                ret = avformat_write_header(formatContext, &videoDictionary);
                if (ret < 0)
                {
                    //Error occurred when writing file
                    valueReturn = 0;
                }
                else
                {
                    swsContext = sws_getContext(
                        streamOut.codecContext->width,
                        streamOut.codecContext->height,
                        FORMAT_PIXEL,
                        streamOut.codecContext->width,
                        streamOut.codecContext->height,
                        streamOut.codecContext->pix_fmt,
                        0, nullptr, nullptr, nullptr);
                }
            }
        }
    }
    return valueReturn;
}

void EngineVideoManager::addStream()
{
    streamOut.stream = avformat_new_stream(formatContext, nullptr);
    if (streamOut.stream != nullptr)
    {
        streamOut.stream->id = formatContext->nb_streams - 1;
        streamOut.codecContext = avcodec_alloc_context3(videoCodec);

        if (streamOut.codecContext != nullptr)
        {
            streamOut.codecContext->codec_id = outputFormat->video_codec;
            streamOut.codecContext->width = videoWidth;
            streamOut.codecContext->height = videoHeight;
            streamOut.stream->time_base = streamOut.codecContext->time_base = { 1, FRAMERATE };
            streamOut.codecContext->framerate = { FRAMERATE,1 };
            streamOut.codecContext->gop_size = 60;
            streamOut.codecContext->max_b_frames = 0;
            streamOut.codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
            streamOut.codecContext->codec_type = AVMEDIA_TYPE_VIDEO;

            if (streamOut.codecContext->codec_id == AV_CODEC_ID_H264)
            {
                av_opt_set(streamOut.codecContext->priv_data, "preset", "slow", 0);
            }

            if (formatContext->oformat->flags & AVFMT_GLOBALHEADER)
            {
                streamOut.codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }

            avcodec_parameters_from_context(streamOut.stream->codecpar, streamOut.codecContext);
        }
    }
}

void EngineVideoManager::closeStream()
{
    avcodec_free_context(&streamOut.codecContext);
    av_frame_free(&streamOut.frame);

    if (swsContext != nullptr)
    {
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }

    if (formatContext != nullptr)
    {
        if (!(outputFormat->flags & AVFMT_NOFILE))
        {
            auto ret = avio_closep(&formatContext->pb);
            if (ret < 0)
            {
                //avio close failed
            }
        }

        avformat_free_context(formatContext);
        formatContext = nullptr;
    }
}

void EngineVideoManager::openVideo()
{
    AVDictionary* dictionary = nullptr;

    av_dict_copy(&dictionary, videoDictionary, 0);
    int ret = avcodec_open2(streamOut.codecContext, videoCodec, &dictionary);
    av_dict_free(&dictionary);

    if (ret < 0)
    {
        //Could not open video codec
    }

    allocatePicture();
}

void EngineVideoManager::allocatePicture()
{
    streamOut.frame = av_frame_alloc();
    if (!streamOut.frame)
    {
        //av_frame_alloc failed
    }
    else
    {
        streamOut.frame->format = streamOut.codecContext->pix_fmt;
        streamOut.frame->width = videoWidth;
        streamOut.frame->height = videoHeight;

        if (av_frame_get_buffer(streamOut.frame, 32) < 0)
        {
            //Could not allocate frame data
        }
    }
}
int EngineVideoManager::writeFrame()
{
    av_packet_rescale_ts(&packet, streamOut.codecContext->time_base, streamOut.stream->time_base);
    packet.stream_index = streamOut.stream->index;
    return av_interleaved_write_frame(formatContext, &packet);
}

int EngineVideoManager::encodeFrame(AVFrame* frame)
{
    int valueReturn = 1;

    auto ret = avcodec_send_frame(streamOut.codecContext, frame);

    if (ret < 0 && ret != AVERROR_EOF)
    {
        //Error during sending frame
        valueReturn = 0;
    }
    else
    {
        ret = avcodec_receive_packet(streamOut.codecContext, &packet);

        if (ret < 0)
        {
            //Error during receiving frame
            av_packet_unref(&packet);
            valueReturn = 0;
        }
    }

    return valueReturn;
}

void EngineVideoManager::closeEncodeContext(AVFormatContext* ifmt_ctx, AVFormatContext* ofmt_ctx)
{
    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
    }
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) 
    {
        avio_closep(&ofmt_ctx->pb);
    }
    if (ofmt_ctx) {
        avformat_free_context(ofmt_ctx);
    }
}

void EngineVideoManager::encodeAndWriteVideo(wi::graphics::Texture& newFrame)
{
    packet = { nullptr };
    av_init_packet(&packet);
    packet.size = 0;
    packet.data = nullptr;

    AVFrame* frame = av_frame_alloc();
    frame->width = videoWidth;
    frame->height = videoHeight;
    frame->format = FORMAT_PIXEL;
    frame->pts = AV_NOPTS_VALUE;

    av_frame_get_buffer(frame, 32);
    
    //Convert R11G11B10 float to RGBA8
    wi::vector<uint8_t> imageSrc = {};
    wi::helper::saveTextureToMemory(newFrame, imageSrc);

    XMFLOAT3PK* dataSrc = (XMFLOAT3PK*)imageSrc.data();
    uint32_t* data32 = (uint32_t*)imageSrc.data();
    uint32_t data_count = newFrame.desc.width * newFrame.desc.height * newFrame.desc.depth;

    for (uint32_t i = 0; i < data_count; ++i)
    {
        XMFLOAT3PK pixel = dataSrc[i];
        XMVECTOR V = XMLoadFloat3PK(&pixel);
        XMFLOAT3 pixel3;
        XMStoreFloat3(&pixel3, V);
        float r = std::max(0.0f, std::min(pixel3.x, 1.0f));
        float g = std::max(0.0f, std::min(pixel3.y, 1.0f));
        float b = std::max(0.0f, std::min(pixel3.z, 1.0f));
        float a = 1;

        uint32_t rgba8 = 0;
        rgba8 |= (uint32_t)(r * 255.0f) << 0;
        rgba8 |= (uint32_t)(g * 255.0f) << 8;
        rgba8 |= (uint32_t)(b * 255.0f) << 16;
        rgba8 |= (uint32_t)(a * 255.0f) << 24;

        data32[i] = rgba8;
    }

    //Send the frame with right format
    frame->data[0] = (uint8_t*)&data32[0];

    //convert RGBA in frame to YUV to streamOut.frame
    sws_scale(swsContext, frame->data, frame->linesize, 0, frame->height, streamOut.frame->data, streamOut.frame->linesize);

    av_frame_free(&frame);

    streamOut.frame->pts = streamOut.nextPoint++;

    if (encodeFrame(streamOut.frame) < 0)
    {
        //Error encoding frame
    }
    else
    {
        int ret = writeFrame();
        if (ret < 0)
        {
            //Error while writing video frame
        }
    }
    av_packet_unref(&packet);
}

void EngineVideoManager::reEncodeToMp4(std::string h264File, std::string fileName)
{
    AVFormatContext* ifmt_ctx = NULL, * ofmt_ctx = NULL;
    int error = -1;

    if ((error = avformat_open_input(&ifmt_ctx, h264File.c_str(), 0, 0)) < 0) 
    {
        //Failed to open input file for remuxing
    }
    else
    {
        if ((error = avformat_find_stream_info(ifmt_ctx, 0)) < 0) 
        {
            //Failed to retrieve input stream information
        }
        else
        {
            if ((error = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, fileName.c_str())))
            {
                //Failed to allocate output context
            }
            else
            {
                AVStream* inVideoStream = ifmt_ctx->streams[0];
                AVStream* outVideoStream = avformat_new_stream(ofmt_ctx, NULL);
                if (!outVideoStream) {
                    //Failed to allocate output video stream
                }
                else
                {
                    outVideoStream->time_base = { 1, FRAMERATE };
                    avcodec_parameters_copy(outVideoStream->codecpar, inVideoStream->codecpar);
                    outVideoStream->codecpar->codec_tag = 0;

                    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) 
                    {
                        if ((error = avio_open(&ofmt_ctx->pb, fileName.c_str(), AVIO_FLAG_WRITE)) < 0) 
                        {
                            //Failed to open output file
                        }
                    }

                    if ((error = avformat_write_header(ofmt_ctx, 0)) < 0) {
                        //Failed to write header to output file
                    }
                    else
                    {
                        AVPacket videoPkt;
                        int ts = 0;
                        while (true) 
                        {
                            if ((error = av_read_frame(ifmt_ctx, &videoPkt)) < 0) 
                            {
                                break;
                            }
                            videoPkt.stream_index = outVideoStream->index;
                            videoPkt.pts = ts;
                            videoPkt.dts = ts;
                            videoPkt.duration = av_rescale_q(videoPkt.duration, inVideoStream->time_base, outVideoStream->time_base);
                            ts += (int)videoPkt.duration;
                            videoPkt.pos = -1;

                            if ((error = av_interleaved_write_frame(ofmt_ctx, &videoPkt)) < 0) 
                            {
                                //Failed to mux packet
                                av_packet_unref(&videoPkt);
                                break;
                            }
                            av_packet_unref(&videoPkt);
                        }

                        av_write_trailer(ofmt_ctx);
                    }
                }
            }
        }
    }
    closeEncodeContext(ifmt_ctx, ofmt_ctx);
}
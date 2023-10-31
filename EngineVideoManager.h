#pragma once

#include "WickedEngine.h"

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/opt.h>
}

class EngineVideoManager
{
public:

    EngineVideoManager();
    ~EngineVideoManager();

    static EngineVideoManager* getInstance()
    {
        if (instance == nullptr)
        {
            instance = new EngineVideoManager();
        }

        return instance;
    }

    static void removeInstance()
    {
        if (instance != 0)
        {
            delete instance;
            instance = nullptr;
        }
    }

    void startCaptureVideo(int width = 1920,int height = 1080);
    void addCaptureVideoFrame(wi::graphics::Texture& newFrame);
    void stopCaptureVideo(std::string fileName);
    bool isVideoInComputation();
    XMINT2 getVideoSize();

private:
    static EngineVideoManager* instance;
    
    const int FRAMERATE = 25;
    const AVPixelFormat FORMAT_PIXEL = AV_PIX_FMT_RGBA;

    bool isComputingVideo = false;
    int videoWidth = 1920;
    int videoHeight = 1080;

    int initializeCodec(std::string fileName);
    void addStream();
    void closeStream();
    void openVideo();
    void allocatePicture();
    int writeFrame();
    int encodeFrame(AVFrame* frame);
    void closeEncodeContext(AVFormatContext* ifmt_ctx, AVFormatContext* ofmt_ctx);
    void encodeAndWriteVideo(wi::graphics::Texture& newFrame);
    void reEncodeToMp4(std::string h264File, std::string fileName);

    struct OutputStream
    {
        AVStream* stream = nullptr;
        AVCodecContext* codecContext = nullptr;
        int64_t nextPoint;
        AVFrame* frame = nullptr;
        struct SwsContext* swsContext = nullptr;
    };

    AVFormatContext* formatContext = nullptr;
    AVOutputFormat* outputFormat = nullptr;
    AVCodec* videoCodec = nullptr;
    AVDictionary* videoDictionary = nullptr;
    SwsContext* swsContext = nullptr;
    AVPacket packet;
    OutputStream streamOut;
};
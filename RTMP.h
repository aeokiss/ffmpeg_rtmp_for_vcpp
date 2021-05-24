#pragma once

#pragma comment(lib, "strmiids")

#include <dshow.h>

// C++ 11
#include <string>
#include <vector>
#include <thread>
#include <mutex>

#define SET_FFMPEG_USE_FILTER

extern "C" {
#pragma warning (push)
#pragma warning (disable : 4819)
#	include <libavformat/avformat.h>
#	include <libavcodec/avcodec.h>
#	include <libavdevice/avdevice.h>
#	include <libswresample/swresample.h>
#	include <libswscale/swscale.h>
#	include <libavutil/opt.h>
#	include <libavutil/time.h>
#	include <libavutil/mathematics.h>
#	include <libavutil/audio_fifo.h>
#	include <libavutil/pixfmt.h>
#	if defined (SET_FFMPEG_USE_FILTER)
#		include <libavfilter/avfiltergraph.h>
#		include <libavfilter/buffersink.h>
#		include <libavfilter/buffersrc.h>
#	endif
#pragma warning (pop)
} // extern "C"

//#if defined (_DEBUG)
#	define SET_DIALOG_DEBUG_SHOW // 반드시 필요함
//#endif

#if defined (_DEBUG)
#	define SET_DEUBG_OUTPUT_FFMPEG_LOG		(1)
//#	define SET_DEUBG_FFMPEG_LOG_SAVE_FILE	(1)
//#	define SET_DEUBG_FFMPEG_LOG_SAVE_DIR	"log"
#endif

#include "Network.h"

typedef struct _Device {
	std::string deviceName; // This can be used to show the devices to the user (utf-8)
	std::string devicePath; // A unique string that identifies the device. (Video capture devices only.) (utf-8)
	int waveInID; // The identifier for an audio capture device. (Audio capture devices only.)
} Device;

typedef struct _DeviceVideoOptions {
	std::string pixel_format;
	std::string vcodec;
	int width;
	int height;
	double fps_min;
	double fps_max;
	double fps;
} DeviceVideoOptions;

class CRTMP
{
public:
	CRTMP (
		int setting_video_resolution_max_width,
		int setting_video_resolution_max_height,
		float setting_video_fps_max,
		int64_t setting_video_bitrate,
		int setting_video_x264_qmin,
		int setting_video_x264_qmax,
		int64_t setting_audio_bitrate
	);
	~CRTMP();

	typedef enum _ErrorCode {
		ERROR_NONE = 0,

		ERROR_INPUT_VIDEO_DEVICE_OPEN, // Couldn't open input video stream.
		ERROR_INPUT_AUDIO_DEVICE_OPEN, // Couldn't open input audio stream.
		ERROR_INPUT_VIDEO_DEVICE_INFORMATION, // Couldn't find video stream information.
		ERROR_INPUT_VIDEO_DEVICE_FIND_STREAM, // Couldn't find a video stream
		ERROR_INPUT_VIDEO_CODEC_FIND, // Could not find video codec
		ERROR_INPUT_VIDEO_CODEC_OPEN, // Could not open video codec
		ERROR_INPUT_AUDIO_DEVICE_INFORMATION, // Couldn't find audio stream information.
		ERROR_INPUT_AUDIO_DEVICE_FIND_STREAM, // Couldn't find a audio stream
		ERROR_INPUT_AUDIO_CODEC_FIND, // Could not find audio codec
		ERROR_INPUT_AUDIO_CODEC_OPEN, // Could not open audio codec

		ERROR_OUTPUT_RTMP_URL_NOT_SET, // rtmp url is not set.
		ERROR_OUTPUT_RTMP_URL_INVALID, // rtmp url is invalid.
		ERROR_OUTPUT_FORMAT_ALLOCATE, // Failed to allocate an output format.
		ERROR_OUTPUT_VIDEO_ENCODER_FIND, // Can not find output video encoder
		ERROR_OUTPUT_VIDEO_ENCODER_OPEN, // Failed to open output video encoder
		ERROR_OUTPUT_VIDEO_STREAM_ADD, // Could not add a new video stream
		ERROR_OUTPUT_AUDIO_ENCODER_FIND, // Can not find output audio encoder
		ERROR_OUTPUT_AUDIO_ENCODER_OPEN, // Failed to open output audio encoder
		ERROR_OUTPUT_AUDIO_STREAM_ADD, // Could not add a new audio stream
		ERROR_OUTPUT_STREAM_OPEN, // Failed to open output stream
		ERROR_OUTPUT_VIDEO_CONVERT_CONTEXT_ALLOCATE, // Failed to allocate video convert context
		ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_ALLOCATE, // Failed to allocate audio convert context
		ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_INITIALIZE, // Failed to initialize the resampler to be able to convert audio sample formats
		ERROR_OUTPUT_AUDIO_CONVERT_SAMPLE_POINTERS_ALLOCATE, // Could not allocate converted input sample pointers

		ERROR_STREAM_VIDEO_PACKET_ALLOCATE, // Failed to allocate video packet
		ERROR_STREAM_VIDEO_INPUT_FRAME_ALLOCATE, // Failed to allocate input video frame
		ERROR_STREAM_INPUT_VIDEO_DECODING, // Could not decode video frame
		ERROR_STREAM_INPUT_VIDEO_FRAME_READ, // Could not read video frame
		ERROR_STREAM_INPUT_AUDIO_DECODING, // Could not decode audio frame
		ERROR_STREAM_INPUT_AUDIO_FRAME_READ, // Could not read audio frame
		ERROR_STREAM_AUDIO_INPUT_FRAME_ALLOCATE, // Failed to allocate input audio frame
		ERROR_STREAM_AUDIO_CONVERT_SAMPLES_ALLOCATE, // Could not allocate converted input samples
		ERROR_STREAM_AUDIO_INPUT_SAMPLE_CONVERT, // Could not convert input samples
		ERROR_STREAM_AUDIO_FIFO_REALLOCATE, // Could not reallocate FIFO
		ERROR_STREAM_AUDIO_FIFO_WRITE, // Could not write data to FIFO
		ERROR_STREAM_AUDIO_OUTPUT_FRAME_ALLOCATE, // Failed to allocate input audio frame
		ERROR_STREAM_AUDIO_FRAME_SAMPLE_ALLOCATE, // Could not allocate output frame samples
		ERROR_STREAM_AUDIO_FIFO_READ, // Could not read data from FIFO
		ERROR_STREAM_AUDIO_FRAME_ENCODE, // Could not encode frame
		ERROR_STREAM_AUDIO_FRAME_WRITE, // Could not write audio frame
		ERROR_STREAM_VIDEO_FRAME_WRITE, // Could not write video frame
		ERROR_STREAM_VIDEO_ENCODER_FLUSHING, // Flushing video encoder failed
		ERROR_STREAM_AUDIO_ENCODER_FLUSHING, // Flushing audio encoder failed

		ERROR_STREAM_WHILE_FEEDING_THE_FILTERGRAPH, // Error while feeding the filtergraph
		ERROR_STREAM_FILTER_FRAME_ALLOCATE, // Failed to allocate filter frame
		ERROR_STREAM_FILTER_BUFFERSINK_GET_FRAME_FLAGE, // buffersink_get_frame_flags
		ERROR_STREAM_FILTER_APPLY, // Failed to apply watermark filter

		ERROR_LAST
	} ErrorCode;

	typedef enum _StatusCode {
		STATUS_READY = 0,

		STATUS_RUN_CAMERA_ONLY,
		STATUS_RUN_STREAMING_RTMP,

		STATUS_LAST
	} StatusCode;

	typedef enum _SyncStatus {
		SYNC_STATUS_NORMAL = 0,

		SYNC_STATUS_AUDIO_SLOW, // 1.25초 이상 차이
		SYNC_STATUS_AUDIO_VERY_SLOW, // 2.5초 이상 차이

		SYNC_STATUS_VIDEO_SLOW, // 1.25초 이상 차이
		SYNC_STATUS_VIDEO_VERY_SLOW, // 2.5초 이상 차이

		SYNC_STATUS_LAST
	} SyncStatus;

	// 카메라 리스트
	void getDevicesVideo (void);
	// 마이크 리스트
	void getDevicesAudio (void);

	// 카메라 상세 옵션 리스트
	void getDeviceVideoOptions (std::string device_name);
	void initDeviceVideoOptionSelected (void);

	// for streaming
	void initStream (void);
	bool setStreamInputDevices (void);
	bool setStreamOutput (void);
	void runStream (void);
	void stopStream (void);

	void closeStream (void);

	void setStreamURL (std::string url) { m_URL_Stream = url; }

	void logFFMPEG (void *ptr, int level, const char *fmt, va_list vl);

	bool getIsOutputScreen (void) { return m_IsOutputScreen; }
	void setIsOutputScreen (bool output) { m_IsOutputScreen = output; }

	bool getIsOutputRtmp (void) { return m_IsOutputRtmp; }
	void setIsOutputRtmp (bool output) { m_IsOutputRtmp = output; }

	std::string getDeviceVideoNameSelected (void) { return m_DeviceVideoName; }
	void setDeviceVideoNameSelected (std::string name) { m_DeviceVideoName = name; }

	std::string getDeviceAudioNameSelected (void) { return m_DeviceAudioName; }
	void setDeviceAudioNameSelected (std::string name) { m_DeviceAudioName = name; }

	DeviceVideoOptions *getDeviceVideoOptionSelected (void) { return &m_DeviceVideoOptionSelected; }

	bool checkValidDeviceNameVideo (std::string name);
	bool checkValidDeviceNameAudio (std::string name);

	std::vector<Device> *getDeviceVideoList (void) { return &m_DeviceVideo; }
	std::vector<Device> *getDeviceAudioList (void) { return &m_DeviceAudio; }
	std::vector<DeviceVideoOptions> *getDeviceVideoOptionList (void) { return &m_DeviceVideoOptions; }

	// 스크린 프레임(m_FrameScreen) 사용시 rtmp thread의 반드시 속도에 지장을 적게 주고 오류를 방지하기 위해 try_lock를 사용해야 합니다.
	// std::mutex *_mutex = getThreadMutex ();
	// if (_mutex.try_lock ()) {
	//     .. 실행 ...
	//     _mutex.unlock ();
	// }
	std::mutex *getThreadMutex (void) { return &m_ThreadMutex; }
	uint8_t *popScreenFrameToBitmap (void);

	//bool isRunStreamThread (void) { return m_ThreadRun.joinable (); }
	double getFrameRate (void) { return m_Framerate_InputVideo; }
	int getFrameScreenQueueCount (void) { return (int) m_FrameScreenQueue.size (); }

	StatusCode getStatusCode (void) { return m_StatusCode; }
	ErrorCode getErrorCode (void) { return m_LastErrorCode; }
	SyncStatus getSyncStatus (void) { return m_SyncStatus; }
	wchar_t *getErrorMessage (void);

	void setAudioMute (bool mute) { m_IsAudioMute = mute; }
	bool getAudioMute (void) { return m_IsAudioMute; }

#if defined (SET_FFMPEG_USE_FILTER)
	void setPathWaterMark (std::string &path) { m_PathWaterMark = path; }
#endif

#if defined (SET_DIALOG_DEBUG_SHOW)
	double _debug_fps;
#endif

	bool getCanStop (void) { return m_IsStopValid; }
	void setCanStop (bool flag) { m_IsStopValid = flag; }

	void setOutputAudioChannels (int channels) { m_SettingAudioChannel = channels; }
	int getOutputAudioChannels (void) { return m_SettingAudioChannel; }

#if defined (SET_FFMPEG_USE_FILTER)
	bool getMirrorStream (void) { return m_IsMirrorStream; }
	void setMirrorStream (bool mirror) { m_IsMirrorStream = mirror; }
	void resetFilter (void) { m_IsInitFilter = true; }
#endif

	bool getMirrorScreen (void) { return m_IsMirrorScreen; }
	void setMirrorScreen (bool mirror) { m_IsMirrorScreen = mirror; }

	void setVideoBitrate (int64_t bitrate) { m_SettingVideoBitrate = bitrate; }
	int64_t getVideoBitrate (void) { return m_SettingVideoBitrate; }

	void setAudioBitrate (int64_t bitrate) { m_SettingAudioBitrate = bitrate; }
	int64_t getAudioBitrate (void) { return m_SettingAudioBitrate; }

private:
	std::vector<Device> m_DeviceVideo; // 카메라 리스트
	std::vector<Device> m_DeviceAudio; // 마이크 리스트
	std::vector<DeviceVideoOptions> m_DeviceVideoOptions; // 카메라 옵션 리스트
	DeviceVideoOptions m_DeviceVideoOptionSelected; // 선택된 카메라 옵션

	// 디바이스 이름 (Device / deviceName)
	std::string m_DeviceVideoName;
	std::string m_DeviceAudioName;

	bool _log_enable_device_option;
	std::string _log_device_buffer;

	ErrorCode m_LastErrorCode;
	StatusCode m_StatusCode;
	SyncStatus m_SyncStatus;

	bool m_IsOutputScreen;
	bool m_IsOutputRtmp;

	bool m_IsStopValid;

	AVFormatContext *m_FormatContext_InputVideo;
	AVFormatContext *m_FormatContext_InputAudio;
	AVFormatContext *m_FormatContext_Output;
	int m_StreamIndex_InputVideo;
	int m_StreamIndex_InputAudio;
	double m_Framerate_InputVideo;
	AVStream *m_Stream_Video;
	AVStream *m_Stream_Audio;
	int m_StreamIndex_OutputVideo;
	int m_StreamIndex_OutputAudio;
	AVCodecContext *m_CodecContext_Video;
	AVCodecContext *m_CodecContext_Audio;
	struct SwsContext *m_ConvertContext_Video;
	struct SwrContext *m_ConvertContext_Audio;

	bool m_IsAudioMute;

	unsigned long long m_PacketSizeVideoWrited;
	unsigned long long m_PacketSizeAudioWrited;

#if defined (SET_FFMPEG_USE_FILTER)
	AVFilterContext *m_FilterContext_BufferSink;
	AVFilterContext *m_FilterContext_BufferSrc;
	AVFilterGraph *m_FilterGraph;
	AVFilter *m_Filter_BufferSink;
	AVFilter *m_Filter_BufferSrc;
	AVFrame *m_Frame_Filter;

	bool m_IsInitFilter;
	int setFilter (AVFormatContext *ifmt_ctx, bool use_watermark, bool use_mirror);
	std::string m_PathWaterMark;

	bool m_IsMirrorStream;
#endif
	bool m_IsMirrorScreen;

	std::string m_URL_Stream;

	std::thread m_ThreadRun;
	std::mutex m_ThreadMutex;
	bool m_ThreadBreak;

	//AVFrame *m_FrameScreen;
	std::vector<AVFrame *> m_FrameScreenQueue;

	void runStreamThread (void);
	int flushEncoderVideo (AVFormatContext *fmt_ctx_input_video, AVFormatContext *fmt_ctx_output, int64_t framecnt);
	int flushEncoderAudio (AVFormatContext *fmt_ctx_input_audio, AVFormatContext *fmt_ctx_output, int64_t pts_audio_input, int64_t nb_samples);

	void getDevices (const GUID deviceClass, std::vector<Device> &device);
	void logAVError (int errcode);

	void putScreenFrame (AVFrame *frame);
	void clearScreenFrame (void);

	//void copyScreenFrame (AVFrame *frame);
	//void freeScreenFrame (void);

#if defined (SET_DEUBG_FFMPEG_LOG_SAVE_FILE)
	std::string log_filename;
	FILE *fp_log;
#endif

	int m_SettingVideoResolutionMaxWidth;
	int m_SettingVideoResolutionMaxHeight;
	float m_SettingVideoFPSMax;
	int64_t m_SettingVideoBitrate;
	int m_SettingVideoX264Qmin;
	int m_SettingVideoX264Qmax;
	int64_t m_SettingAudioBitrate;
	int m_SettingAudioChannel;
};

#define SET_SCREENFRAME_MAX_QUEUE	(10)

//#define SET_RESOLUTION_MAX_WIDTH	(1920)
//#define SET_RESOLUTION_MAX_HEIGHT	(1080)
//#define SET_RESOLUTION_MAX_FPS		(30.0f)
//#define SET_DEVICE_AUDIO_CHANNEL	(1)
//#define SET_DEVICE_AUDIO_BITRATE	(32000 * 2)
//#define SET_DEVICE_VIDEO_BITRATE	(1024 * 1024 * 1)
#define SET_FFMPEG_RTBUFSIZE		"512000k"
// x264 encoding minimum quantizer
//#define SET_FFMPEG_X264_QMIN		(10)
//// x264 encoding maximum quantizer
//#define SET_FFMPEG_X264_QMAX		(51)

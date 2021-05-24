#include "stdafx.h"
#include "RTMP.h"
#include "MAINDlg.h"

#include "Support.h"

static CRTMP *_instance = nullptr;

static void _log_ffmpeg (void *ptr, int level, const char *fmt, va_list vl)
{
	if (!_instance)
		return;

	_instance->logFFMPEG (ptr, level, fmt, vl);
}

CRTMP::CRTMP (
	int setting_video_resolution_max_width,
	int setting_video_resolution_max_height,
	float setting_video_fps_max,
	int64_t setting_video_bitrate,
	int setting_video_x264_qmin,
	int setting_video_x264_qmax,
	int64_t setting_audio_bitrate
)
{
	_instance = this;

	_log_enable_device_option = false;

	m_IsOutputScreen = true;
	m_IsOutputRtmp = false;

#if defined (SET_FFMPEG_USE_FILTER)
	m_IsMirrorStream = false;
#endif
	m_IsMirrorScreen = true;

#if defined (SET_DEUBG_FFMPEG_LOG_SAVE_FILE)
	if (GetFileAttributes (CSupport::utf8_to_CString (SET_DEUBG_FFMPEG_LOG_SAVE_DIR)) == INVALID_FILE_ATTRIBUTES)
		CreateDirectory (CSupport::utf8_to_CString (SET_DEUBG_FFMPEG_LOG_SAVE_DIR), nullptr);
	log_filename = SET_DEUBG_FFMPEG_LOG_SAVE_DIR"\\log_" + CSupport::get_time_str () + ".log";
	fp_log = fopen (log_filename.c_str (), "a");
#endif

	m_DeviceVideoName = "";
	m_DeviceAudioName = "";

	initDeviceVideoOptionSelected ();

	//m_FrameScreen = nullptr;

	av_register_all ();
	avformat_network_init ();
	avdevice_register_all ();
#if defined (SET_FFMPEG_USE_FILTER)
	avfilter_register_all ();
	m_Filter_BufferSrc = avfilter_get_by_name ("buffer");
	m_Filter_BufferSink = avfilter_get_by_name ("buffersink");
#endif

	av_log_set_callback (&_log_ffmpeg);

#if defined (SET_DIALOG_DEBUG_SHOW)
	_debug_fps = 0.0f;
#endif

#if defined (_DEBUG)
	av_log_set_level (AV_LOG_TRACE);
	OutputDebugStringUTF8 ("[FFMPEG] avformat_configuration () : ");
	OutputDebugStringUTF8 (avformat_configuration ());
	OutputDebugStringUTF8 ("\n");

	OutputDebugStringUTF8 ("[FFMPEG] avdevice_configuration () : ");
	OutputDebugStringUTF8 (avdevice_configuration ());
	OutputDebugStringUTF8 ("\n");
#else
	av_log_set_level (AV_LOG_INFO);
#endif

	m_IsAudioMute = false;

	m_StatusCode = StatusCode::STATUS_READY;
	m_SyncStatus = SyncStatus::SYNC_STATUS_NORMAL;

	m_SettingVideoResolutionMaxWidth = setting_video_resolution_max_width;
	m_SettingVideoResolutionMaxHeight = setting_video_resolution_max_height;
	m_SettingVideoFPSMax = setting_video_fps_max;
	m_SettingVideoBitrate = setting_video_bitrate;
	m_SettingVideoX264Qmin = setting_video_x264_qmin;
	m_SettingVideoX264Qmax = setting_video_x264_qmax;
	m_SettingAudioBitrate = setting_audio_bitrate;
	m_SettingAudioChannel = 1;

	m_IsStopValid = true;

	initStream ();
}

CRTMP::~CRTMP()
{
	closeStream ();

	//freeScreenFrame ();
	clearScreenFrame ();

#if defined (SET_DEUBG_FFMPEG_LOG_SAVE_FILE)
	if (fp_log)
		fclose (fp_log);
	fp_log = nullptr;
#endif

	_instance = nullptr;
}

void CRTMP::getDevicesVideo (void)
{
	getDevices (CLSID_VideoInputDeviceCategory, m_DeviceVideo);
	if (m_DeviceVideoName.length () <= 0 && m_DeviceVideo.size () > 0)
		m_DeviceVideoName = m_DeviceVideo.begin ()->deviceName;
}

void CRTMP::getDevicesAudio (void)
{
	getDevices (CLSID_AudioInputDeviceCategory, m_DeviceAudio);
	if (m_DeviceAudioName.length () <= 0 && m_DeviceAudio.size () > 0)
		m_DeviceAudioName = m_DeviceAudio.begin ()->deviceName;
}

void CRTMP::getDeviceVideoOptions (std::string device_name)
{
	int _log_level = av_log_get_level ();

	av_log_set_level (AV_LOG_INFO);

	m_DeviceVideoOptions.clear ();

	_log_enable_device_option = true;

	std::string _url = "video=" + device_name;

	AVFormatContext *_format_context = avformat_alloc_context();

	AVInputFormat *_input_format = av_find_input_format ("dshow");

	AVDictionary* _dictionary = nullptr;
	av_dict_set (&_dictionary, "list_options", "true", 0);

	avformat_open_input (&_format_context, _url.c_str (), _input_format, &_dictionary);

	if (_format_context)
		avformat_free_context (_format_context);

	_log_enable_device_option = false;

	av_log_set_level (_log_level);
}

void CRTMP::initDeviceVideoOptionSelected (void)
{
	m_DeviceVideoOptionSelected.pixel_format.clear ();
	m_DeviceVideoOptionSelected.vcodec.clear ();
	m_DeviceVideoOptionSelected.width = 0;
	m_DeviceVideoOptionSelected.height = 0;
	m_DeviceVideoOptionSelected.fps_min = 0.0f;
	m_DeviceVideoOptionSelected.fps_max = 0.0f;
}

bool CRTMP::checkValidDeviceNameVideo (std::string name)
{
	for (std::vector<Device>::iterator _iter = m_DeviceVideo.begin (); _iter != m_DeviceVideo.end (); _iter++) {
		if (_iter->deviceName == name)
			return true;
	}
	return false;
}

bool CRTMP::checkValidDeviceNameAudio (std::string name)
{
	for (std::vector<Device>::iterator _iter = m_DeviceAudio.begin (); _iter != m_DeviceAudio.end (); _iter++) {
		if (_iter->deviceName == name)
			return true;
	}
	return false;
}

void CRTMP::initStream (void)
{
	m_FormatContext_InputVideo = nullptr;
	m_FormatContext_InputAudio = nullptr;
	m_FormatContext_Output = nullptr;
	m_StreamIndex_InputVideo = -1;
	m_StreamIndex_InputAudio = -1;
	m_URL_Stream = "";
	m_Stream_Video = nullptr;
	m_Stream_Audio = nullptr;
	m_StreamIndex_OutputVideo = -1;
	m_StreamIndex_OutputAudio = -1;
	m_CodecContext_Video = nullptr;
	m_CodecContext_Audio = nullptr;
	m_ConvertContext_Video = nullptr;
	m_ConvertContext_Audio = nullptr;
	m_IsAudioMute = false;
#if defined (SET_FFMPEG_USE_FILTER)
	m_FilterContext_BufferSink = nullptr;
	m_FilterContext_BufferSrc = nullptr;
	m_FilterGraph = nullptr;
	m_Frame_Filter = nullptr;
	m_IsInitFilter = true;
#endif
	m_Framerate_InputVideo = 0.0f;
	m_PacketSizeVideoWrited = 0;
	m_PacketSizeAudioWrited = 0;
}

bool CRTMP::setStreamInputDevices (void)
{
	AVInputFormat *input_fmt;

	int i;
	int ret;

	AVRational time_base_q = {1, AV_TIME_BASE};
	AVCodec *codec_input_video = nullptr;
	AVCodec *codec_input_audio = nullptr;

	input_fmt = av_find_input_format ("dshow");

#if defined (SET_DIALOG_DEBUG_SHOW)
	CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_RESOLUTION_WIDTH, m_DeviceVideoOptionSelected.width);
	CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_RESOLUTION_HEIGHT, m_DeviceVideoOptionSelected.height);
	LPARAM _lparam;
#	if defined (_X86_64)
	memcpy (&_lparam, &m_DeviceVideoOptionSelected.fps, sizeof (double));
#	else // 32bit
	float _float = (float) m_DeviceVideoOptionSelected.fps;
	memcpy (&_lparam, &_float, sizeof (float));
#	endif
	CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_DOUBLE, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_FPS_SETTING, _lparam);
#endif

	AVDictionary *device_param_video = nullptr;
	av_dict_set (&device_param_video, "rtbufsize", SET_FFMPEG_RTBUFSIZE, 0);
	av_dict_set (&device_param_video, "video_size", (std::to_string (m_DeviceVideoOptionSelected.width) + "x" + std::to_string (m_DeviceVideoOptionSelected.height)).c_str (), 0);
	av_dict_set (&device_param_video, "framerate", std::to_string (static_cast<int>(m_DeviceVideoOptionSelected.fps)).c_str (), 0);
	//av_dict_set (&device_param_video, "framerate", "30", 0);
	if (m_DeviceVideoOptionSelected.pixel_format.length () > 0)
		av_dict_set (&device_param_video, "pixel_format", m_DeviceVideoOptionSelected.pixel_format.c_str (), 0);
	if (m_DeviceVideoOptionSelected.vcodec.length () > 0)
		av_dict_set (&device_param_video, "vcodec", m_DeviceVideoOptionSelected.vcodec.c_str (), 0);

	if ((ret = avformat_open_input (&m_FormatContext_InputVideo, ("video=" + m_DeviceVideoName).c_str (), input_fmt, &device_param_video)) < 0) {
		logAVError (ret);
		m_LastErrorCode = ErrorCode::ERROR_INPUT_VIDEO_DEVICE_OPEN;
		return false;
	}

	if (m_IsOutputRtmp) {
		AVDictionary *device_param_audio = nullptr;
		//av_dict_set (&device_param_audio, "tune", "zerolatency", 0);
		//av_dict_set (&device_param_audio, "preset", "ultrafast", 0);

		if ((ret = avformat_open_input (&m_FormatContext_InputAudio, ("audio=" + m_DeviceAudioName).c_str (), input_fmt, &device_param_audio)) < 0) {
			logAVError (ret);
			m_LastErrorCode = ErrorCode::ERROR_INPUT_AUDIO_DEVICE_OPEN;
			return false;
		}
	}

	//input video initialize
	if ((ret = avformat_find_stream_info (m_FormatContext_InputVideo, nullptr)) < 0) {
		logAVError (ret);
		m_LastErrorCode = ErrorCode::ERROR_INPUT_VIDEO_DEVICE_INFORMATION;
		return false;
	}
	m_StreamIndex_InputVideo = -1;
	for (i = 0; i < (int) m_FormatContext_InputVideo->nb_streams; i++) {
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		if (m_FormatContext_InputVideo->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#pragma warning (pop)
			m_StreamIndex_InputVideo = i;
			break;
		}
	}
	if (m_StreamIndex_InputVideo == -1) {
		m_LastErrorCode = ErrorCode::ERROR_INPUT_VIDEO_DEVICE_FIND_STREAM;
		return false;
	}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	codec_input_video = avcodec_find_decoder (m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->codec_id);
#pragma warning (pop)
	if (!codec_input_video) {
		m_LastErrorCode = ErrorCode::ERROR_INPUT_VIDEO_CODEC_FIND;
		return false;
	}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	if ((ret = avcodec_open2 (m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec, codec_input_video, nullptr)) < 0) {
#pragma warning (pop)
		logAVError (ret);
		m_LastErrorCode = ErrorCode::ERROR_INPUT_VIDEO_CODEC_OPEN;
		return false;
	}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	// 일부 장치에서 framerate를 강제로 설정해도 적용되지 않는다. (예: Microsoft LifeCam HD-3000)
	// 따라서, 아웃풋 framerate도 장치를 따라가자...
	m_Framerate_InputVideo = av_q2d (m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->framerate);
#if defined (SET_DIALOG_DEBUG_SHOW)
#	if defined (_X86_64)
	memcpy (&_lparam, &m_Framerate_InputVideo, sizeof (double));
#	else // 32bit
	_float = (float) m_Framerate_InputVideo;
	memcpy (&_lparam, &_float, sizeof (float));
#	endif
	CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_DOUBLE, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_FPS_DEVICE, _lparam);
#endif
#pragma warning (pop)

	if (m_IsOutputRtmp) {
		if ((ret = avformat_find_stream_info (m_FormatContext_InputAudio, nullptr)) < 0) {
			logAVError (ret);
			m_LastErrorCode = ErrorCode::ERROR_INPUT_AUDIO_DEVICE_INFORMATION;
			return false;
		}
		m_StreamIndex_InputAudio = -1;
		for (i = 0; i < (int) m_FormatContext_InputAudio->nb_streams; i++) {
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
			if (m_FormatContext_InputAudio->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
#pragma warning (pop)
				m_StreamIndex_InputAudio = i;
				break;
			}
		}
		if (m_StreamIndex_InputAudio == -1) {
			m_LastErrorCode = ErrorCode::ERROR_INPUT_AUDIO_DEVICE_FIND_STREAM;
			return false;
		}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		codec_input_audio = avcodec_find_decoder (m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->codec_id);
#pragma warning (pop)
		if (!codec_input_audio) {
			m_LastErrorCode = ErrorCode::ERROR_INPUT_AUDIO_CODEC_FIND;
			return false;
		}
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		if ((ret = avcodec_open2 (m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec, codec_input_audio, nullptr)) < 0) {
#pragma warning (pop)
			logAVError (ret);
			m_LastErrorCode = ErrorCode::ERROR_INPUT_AUDIO_CODEC_OPEN;
			return false;
		}
	}
#if defined (SET_DIALOG_DEBUG_SHOW)
	if (codec_input_video)
		CMAINDlg::getInstance ()->m_DialogDebug->setData (CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_INPUT_CODEC, CSupport::utf8_to_CString ((char *) codec_input_video->name));
	if (codec_input_audio)
		CMAINDlg::getInstance ()->m_DialogDebug->setData (CDialogDebug::DebugType::DEBUG_TYPE_AUDIO_INPUT_CODEC, CSupport::utf8_to_CString ((char *) codec_input_audio->name));
#endif

	return true;
}

bool CRTMP::setStreamOutput (void)
{
	int ret;
	AVCodec *codec_video = nullptr;
	AVCodec *codec_audio = nullptr;

	if (m_URL_Stream.length () <= 0) {
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_RTMP_URL_NOT_SET;
		return false;
	}

	if (std::strncmp (m_URL_Stream.c_str (), "rtmp://", 7)) {
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_RTMP_URL_INVALID;
		return false;
	}

	if ((ret = avformat_alloc_output_context2 (&m_FormatContext_Output, nullptr, "flv", m_URL_Stream.c_str ())) < 0) {
		logAVError (ret);
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_FORMAT_ALLOCATE;
		return false;
	}
	//if ((ret = avformat_alloc_output_context2 (&m_FormatContext_Output, nullptr, "flv", "D:\\Projects\\win32\\XXXX\\work\\dump.flv")) < 0) {
	//	logAVError (ret);
	//	m_LastErrorCode = ErrorCode::ERROR_OUTPUT_FORMAT_ALLOCATE;
	//	return false;
	//}

	if (!(codec_video = avcodec_find_encoder (AV_CODEC_ID_H264))) {
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_VIDEO_ENCODER_FIND;
		return false;
	}

	m_CodecContext_Video = avcodec_alloc_context3 (codec_video);
	m_CodecContext_Video->pix_fmt = AV_PIX_FMT_YUV420P;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	m_CodecContext_Video->width = m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->width;
	m_CodecContext_Video->height = m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->height;
#pragma warning (pop)
	//const AVRational dst_fps = {static_cast<int>(m_DeviceVideoOptionSelected.fps), 1};
	const AVRational dst_fps = {static_cast<int>(m_Framerate_InputVideo), 1};
	m_CodecContext_Video->time_base = av_inv_q (dst_fps);
//#pragma warning (push)
//#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
//	m_CodecContext_Video->time_base = m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->framerate;
//#pragma warning (pop)
	m_CodecContext_Video->bit_rate = m_SettingVideoBitrate;
	//m_CodecContext_Video->bit_rate = SET_DEVICE_VIDEO_BITRATE;
	m_CodecContext_Video->gop_size = 30;
	/* Some formats want stream headers to be separate. */
	if (m_FormatContext_Output->oformat->flags & AVFMT_GLOBALHEADER)
		m_CodecContext_Video->flags |= CODEC_FLAG_GLOBAL_HEADER;

	//H264 codec param
	//codec_ctx_video->me_range = 16;
	//codec_ctx_video->max_qdiff = 4;
	//codec_ctx_video->qcompress = 0.6;
	m_CodecContext_Video->qmin = m_SettingVideoX264Qmin;
	m_CodecContext_Video->qmax = m_SettingVideoX264Qmax;
	//m_CodecContext_Video->qmin = SET_FFMPEG_X264_QMIN;
	//m_CodecContext_Video->qmax = SET_FFMPEG_X264_QMAX;
	//Optional Param
	m_CodecContext_Video->max_b_frames = 0;

	AVDictionary *param_video = nullptr;
	av_dict_set (&param_video, "profile", "high444", 0);
	// https://trac.ffmpeg.org/wiki/Encode/H.264
	// tune options
	// - film – use for high quality movie content; lowers deblocking
	// - animation – good for cartoons; uses higher deblocking and more reference frames
	// - grain – preserves the grain structure in old, grainy film material
	// - stillimage – good for slideshow - like content
	// - fastdecode – allows faster decoding by disabling certain filters
	// - zerolatency – good for fast encoding and low - latency streaming
	// - psnr – ignore this as it is only used for codec development
	// - ssim – ignore this as it is only used for codec development
	av_dict_set (&param_video, "tune", "zerolatency", 0); // zerolatency : 지연시간 최소
	// Preset options
	// A preset is a collection of options that will provide a certain encoding speed to compression ratio.
	// A slower preset will provide better compression (compression is quality per filesize).
	// This means that, for example, if you target a certain file size or constant bit rate, you will achieve better quality with a slower preset.Similarly, for constant quality encoding, you will simply save bitrate by choosing a slower preset.
	av_dict_set (&param_video, "preset", "ultrafast", 0); // ultrafast 로 갈수록 압축율이 낮아 용량이 커짐
	// crf
	//	The range of the CRF scale is 0–51, where 0 is lossless, 23 is the default, and 51 is worst quality possible.A lower value generally leads to higher quality, and a subjectively sane range is 17–28.Consider 17 or 18 to be visually lossless or nearly so; it should look the same or nearly the same as the input but it isn't technically lossless.
	//	The range is exponential, so increasing the CRF value + 6 results in roughly half the bitrate / file size, while - 6 leads to roughly twice the bitrate.
	//	Choose the highest CRF value that still provides an acceptable quality.If the output looks good, then try a higher value.If it looks bad, choose a lower value.
	//	Note: The 0–51 CRF quantizer scale mentioned on this page only applies to 8 - bit x264.When compiled with 10 - bit support, x264's quantizer scale is 0–63. You can see what you are using by referring to the ffmpeg console output during encoding (yuv420p or similar for 8-bit, and yuv420p10le or similar for 10-bit). 8-bit is more common among distributors.
	av_dict_set (&param_video, "crf", "23", 0);

	if ((ret = avcodec_open2 (m_CodecContext_Video, codec_video, &param_video)) < 0) {
		logAVError (ret);
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_VIDEO_ENCODER_OPEN;
		return false;
	}

	if ((m_Stream_Video = avformat_new_stream (m_FormatContext_Output, codec_video)) == nullptr) {
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_VIDEO_STREAM_ADD;
		return false;
	}
	m_StreamIndex_OutputVideo = m_Stream_Video->index;
	//m_Stream_Video->time_base.num = 1;
	//m_Stream_Video->time_base.den = 30;
	m_Stream_Video->time_base = av_inv_q (dst_fps);
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	//m_Stream_Video->time_base = m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->framerate;
	m_Stream_Video->codec = m_CodecContext_Video;
#pragma warning (pop)

	if (!(codec_audio = avcodec_find_encoder (AV_CODEC_ID_AAC))) {
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_AUDIO_ENCODER_FIND;
		return false;
	}
	m_CodecContext_Audio = avcodec_alloc_context3 (codec_audio);
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	m_CodecContext_Audio->channels = min (m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->channels, m_SettingAudioChannel);
#pragma warning (pop)
	//m_CodecContext_Audio->channels = SET_DEVICE_AUDIO_CHANNEL;
	m_CodecContext_Audio->channel_layout = av_get_default_channel_layout (m_CodecContext_Audio->channels);
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	m_CodecContext_Audio->sample_rate = m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->sample_rate; // 44100
#pragma warning (pop)
	m_CodecContext_Audio->sample_fmt = codec_audio->sample_fmts[0];
	m_CodecContext_Audio->bit_rate = m_SettingAudioBitrate;
	//m_CodecContext_Audio->bit_rate = SET_DEVICE_AUDIO_BITRATE;
	m_CodecContext_Audio->time_base.num = 1;
	m_CodecContext_Audio->time_base.den = m_CodecContext_Audio->sample_rate;
	/** Allow the use of the experimental AAC encoder */
	m_CodecContext_Audio->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
	/* Some formats want stream headers to be separate. */
	if (m_FormatContext_Output->oformat->flags & AVFMT_GLOBALHEADER)
		m_CodecContext_Audio->flags |= CODEC_FLAG_GLOBAL_HEADER;
	if ((ret = avcodec_open2 (m_CodecContext_Audio, codec_audio, nullptr)) < 0) {
		logAVError (ret);
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_AUDIO_ENCODER_OPEN;
		return false;
	}
#if defined (SET_DIALOG_DEBUG_SHOW)
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_AUDIO_INPUT_CHANNELS, (LPARAM) m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->channels);
#pragma warning (pop)
	CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_AUDIO_OUTPUT_CHANNELS, (LPARAM) m_CodecContext_Audio->channels);
#endif

	if ((m_Stream_Audio = avformat_new_stream (m_FormatContext_Output, codec_audio)) == nullptr) {
		logAVError (AVERROR (ENOMEM));
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_AUDIO_STREAM_ADD;
		return false;
	}
	m_StreamIndex_OutputAudio = m_Stream_Audio->index;
	m_Stream_Audio->time_base.num = 1;
	m_Stream_Audio->time_base.den = m_CodecContext_Audio->sample_rate;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	m_Stream_Audio->codec = m_CodecContext_Audio;
#pragma warning (pop)

	if ((ret = avio_open (&m_FormatContext_Output->pb, m_URL_Stream.c_str (), AVIO_FLAG_READ_WRITE)) < 0) {
		logAVError (ret);
		m_LastErrorCode = ErrorCode::ERROR_OUTPUT_STREAM_OPEN;
		return false;
	}

	// Show some Information
	av_dump_format (m_FormatContext_Output, 0, m_URL_Stream.c_str (), 1);

	// Write File Header
	avformat_write_header (m_FormatContext_Output, nullptr);

	return true;
}

void CRTMP::runStream (void)
{
	m_SyncStatus = SyncStatus::SYNC_STATUS_NORMAL;
	m_IsStopValid = false;
	m_ThreadRun = std::thread (&CRTMP::runStreamThread, this);
}

void CRTMP::stopStream (void)
{
	m_ThreadBreak = true;
	if (m_ThreadRun.joinable ())
		m_ThreadRun.join ();
	m_ThreadBreak = false;
}

void CRTMP::closeStream (void)
{
	if (m_Stream_Video)
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		avcodec_close (m_Stream_Video->codec);
#pragma warning (pop)
	if (m_Stream_Audio)
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		avcodec_close (m_Stream_Audio->codec);
#pragma warning (pop)

	if (m_FormatContext_Output) {
		avio_close (m_FormatContext_Output->pb);
		avformat_free_context (m_FormatContext_Output);
	}
	if (m_FormatContext_InputVideo) {
		avformat_close_input (&m_FormatContext_InputVideo); // close camera
		av_free (m_FormatContext_InputVideo);
	}
	if (m_FormatContext_InputAudio) {
		avformat_close_input (&m_FormatContext_InputAudio); // close mic
		av_free (m_FormatContext_InputAudio);
	}

	initStream ();
}

void CRTMP::logFFMPEG (void *ptr, int level, const char *fmt, va_list vl)
{
	char message[8192];
	const char *module = nullptr;
	bool _is_device_option = false;
	memset (message, 0x00, sizeof (message));

	if (ptr) {
		AVClass *avc = *(AVClass**) ptr;
		if (avc->item_name)
			module = avc->item_name (ptr);
	}
	vsnprintf_s (message, sizeof (message), sizeof (message), fmt, vl);

	//message[sizeof (message) - 1] = '\0';
#if defined (SET_DEUBG_FFMPEG_LOG_SAVE_FILE)
	if (fp_log) {
		static bool _is_newline = true;
		static std::string _file_message = "";
		if (_is_newline)
			_file_message = message;
		else
			_file_message = _file_message + message;
		if ((_is_newline = (message[strlen (message) - 1] == '\n')) && level == AV_LOG_INFO) {
		//if ((_is_newline = (message[strlen (message) - 1] == '\n'))) {
			char *_log_level = "";
			switch (level) {
				case AV_LOG_PANIC :
					_log_level = "PANIC";
					break;
				case AV_LOG_FATAL :
					_log_level = "FATAL";
					break;
				case AV_LOG_ERROR :
					_log_level = "ERROR";
					break;
				case AV_LOG_WARNING :
					_log_level = "WARNING";
					break;
				case AV_LOG_INFO :
					_log_level = "INFO";
					break;
				case AV_LOG_VERBOSE :
					_log_level = "VERBOSE";
					break;
				case AV_LOG_TRACE :
					_log_level = "TRACE";
					break;
				case AV_LOG_DEBUG :
					_log_level = "DEBUG";
					break;
				default :
					_log_level = "UNKNOWN";
					break;
			}
			fwrite ("[", 1, 1, fp_log);
			fwrite (_log_level, 1, strlen (_log_level), fp_log);
			fwrite ("] ", 1, 2, fp_log);
			fwrite (_file_message.c_str (), 1, _file_message.length (), fp_log);
		}
	}
#endif

	if (_log_enable_device_option && level == AV_LOG_INFO) {
		std::string _option (message);
#if defined (_DEBUG)
		OutputDebugStringUTF8 (_option.c_str ());
#endif
		CSupport::trim (_option);

		char _buffer[4][0xff];
		if (!_option.compare (0, strlen ("pixel_format="), "pixel_format=")) {
			_log_device_buffer = _option;
			_is_device_option = true;
		}
		else if (!_option.compare (0, strlen ("vcodec="), "vcodec=")) {
			_log_device_buffer = _option;
			_is_device_option = true;
		}
		else if (!_option.compare (0, strlen ("min s="), "min s=")) {
			DeviceVideoOptions _device_option;
			_device_option.pixel_format = "";
			_device_option.vcodec = "";
			if (!_log_device_buffer.compare (0, strlen ("pixel_format="), "pixel_format=")) {
				std::sscanf (_log_device_buffer.c_str (), "pixel_format=%s", _buffer[0]);
				_device_option.pixel_format = _buffer[0];
			}
			else if (!_log_device_buffer.compare (0, strlen ("vcodec="), "vcodec=")) {
				std::sscanf (_log_device_buffer.c_str (), "vcodec=%s", _buffer[0]);
				_device_option.vcodec = _buffer[0];
			}
			// min s=320x180 fps=5 max s=320x180 fps=30
			//std::sscanf (_buffer[0], "min s=%dx%d fps=%f max s=%dx%d fps=%f", &_size_min_w, &_size_min_h, &_fps_min, &_size_max_w, &_size_max_h, &_fps_max);
			std::sscanf (_option.c_str (), "min %s %s max %s %s", _buffer[0], _buffer[1], _buffer[2], _buffer[3]); // 한번에 짜르면 안된다.... 때문에 텍스트로 분류
			// XSplit 에서 min은 1x1로 잡힘 따라서 max를 사용하자
//			std::sscanf (_buffer[0], "s=%dx%d", &_device_option.width, &_device_option.height);
			std::sscanf (_buffer[2], "s=%dx%d", &_device_option.width, &_device_option.height);
			std::sscanf (_buffer[1], "fps=%lf", &_device_option.fps_min);
			//std::sscanf (_buffer[2], "s=%dx%d", &_size_max_w, &_size_max_h);
			std::sscanf (_buffer[3], "fps=%lf", &_device_option.fps_max);

			// 똑같은 옵션이 2개씩 들어올때가 많다. 따라서 이미 같은 옵션이 있는지 확인하고 추가하자
			bool _is_unique = true;
			for (std::vector<DeviceVideoOptions>::iterator _iter = m_DeviceVideoOptions.begin (); _iter != m_DeviceVideoOptions.end (); _iter++) {
				if (_iter->pixel_format == _device_option.pixel_format &&
					_iter->vcodec == _device_option.vcodec &&
					_iter->width == _device_option.width &&
					_iter->height == _device_option.height &&
					_iter->fps_min == _device_option.fps_min &&
					_iter->fps_max == _device_option.fps_max) {
					_is_unique = false;
					break;
				}
			}
			if (_is_unique && _device_option.width <= m_SettingVideoResolutionMaxWidth && _device_option.height <= m_SettingVideoResolutionMaxHeight) {
			//if (_is_unique && _device_option.width <= SET_RESOLUTION_MAX_WIDTH && _device_option.height <= SET_RESOLUTION_MAX_HEIGHT) {
				_device_option.fps = FFMIN (_device_option.fps_max, m_SettingVideoFPSMax);
				//_device_option.fps = FFMIN (_device_option.fps_max, SET_RESOLUTION_MAX_FPS);
				m_DeviceVideoOptions.push_back (_device_option);
				// 기본값 자동 세팅
				if ((_device_option.width > m_DeviceVideoOptionSelected.width && _device_option.width <= m_SettingVideoResolutionMaxWidth) ||
					(_device_option.height > m_DeviceVideoOptionSelected.height && _device_option.height <= m_SettingVideoResolutionMaxHeight) ||
				//if ((_device_option.width > m_DeviceVideoOptionSelected.width && _device_option.width <= SET_RESOLUTION_MAX_WIDTH) ||
				//	(_device_option.height > m_DeviceVideoOptionSelected.height && _device_option.height <= SET_RESOLUTION_MAX_HEIGHT) ||
					(
						(_device_option.width >= m_DeviceVideoOptionSelected.width && _device_option.width <= m_SettingVideoResolutionMaxWidth) &&
						(_device_option.height >= m_DeviceVideoOptionSelected.height && _device_option.height <= m_SettingVideoResolutionMaxHeight) &&
						//(_device_option.width >= m_DeviceVideoOptionSelected.width && _device_option.width <= SET_RESOLUTION_MAX_WIDTH) &&
						//(_device_option.height >= m_DeviceVideoOptionSelected.height && _device_option.height <= SET_RESOLUTION_MAX_HEIGHT) &&
						(_device_option.fps_max >= m_DeviceVideoOptionSelected.fps_max)
					)) {
					m_DeviceVideoOptionSelected.pixel_format = _device_option.pixel_format;
					m_DeviceVideoOptionSelected.vcodec = _device_option.vcodec;
					m_DeviceVideoOptionSelected.width = _device_option.width;
					m_DeviceVideoOptionSelected.height = _device_option.height;
					m_DeviceVideoOptionSelected.fps_min = _device_option.fps_min;
					m_DeviceVideoOptionSelected.fps_max = _device_option.fps_max;
					m_DeviceVideoOptionSelected.fps = _device_option.fps;
				}
			}
			_is_device_option = true;
		}
	}

#if defined (SET_DEUBG_OUTPUT_FFMPEG_LOG)
	if (!_is_device_option && (level == AV_LOG_PANIC || level == AV_LOG_FATAL || level == AV_LOG_ERROR || level == AV_LOG_INFO)) {
		OutputDebugStringUTF8 ("[FFMPEG | ");
		switch (level) {
			case AV_LOG_PANIC :
				OutputDebugStringUTF8 ("PANIC");
				break;
			case AV_LOG_FATAL :
				OutputDebugStringUTF8 ("FATAL");
				break;
			case AV_LOG_ERROR :
				OutputDebugStringUTF8 ("ERROR");
				break;
			case AV_LOG_WARNING :
				OutputDebugStringUTF8 ("WARNING");
				break;
			case AV_LOG_INFO :
				OutputDebugStringUTF8 ("INFO");
				break;
			case AV_LOG_VERBOSE :
				OutputDebugStringUTF8 ("VERBOSE");
				break;
			case AV_LOG_TRACE :
				OutputDebugStringUTF8 ("TRACE");
				break;
			case AV_LOG_DEBUG :
				OutputDebugStringUTF8 ("DEBUG");
				break;
			default :
				OutputDebugStringUTF8 ("UNKNOWN");
				break;
		}
		OutputDebugStringUTF8 ("] MODULE:");
		OutputDebugStringUTF8 (module);
		OutputDebugStringUTF8 (", MESSAGE:");
		std::string _message (message);
		CSupport::trim (_message);
		OutputDebugStringUTF8 (_message.c_str ());
		//OutputDebugStringA (_message.c_str ());
		OutputDebugStringUTF8 ("\n");
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////
// internal functions
void CRTMP::runStreamThread (void)
{
	m_LastErrorCode = ErrorCode::ERROR_NONE;
	m_StatusCode = (m_IsOutputRtmp) ? StatusCode::STATUS_RUN_STREAMING_RTMP : StatusCode::STATUS_RUN_CAMERA_ONLY;

	AVFrame *frame_input = nullptr;
	AVFrame *frame_output_video = nullptr;
	AVFrame *frame_output_audio = nullptr;

	AVPacket packet_decoding;
	AVPacket packet_encoding;

	m_ThreadBreak = false;

	int encode_video = 1;
	int encode_audio = 1;

	AVRational time_base_q = {1, AV_TIME_BASE};
	int64_t aud_next_pts = 0;
	int64_t vid_next_pts = 0;

	int got_frame_video_decoding;
	int got_frame_video_encoding;
	int got_frame_audio_decoding;
	int got_frame_audio_encoding;

	int64_t frame_count_video = 0;
	int64_t nb_samples = 0;

	int ret;

	uint8_t *out_buffer = nullptr;
	AVAudioFifo *fifo = nullptr;
	uint8_t **converted_input_samples = nullptr;

	if (!setStreamInputDevices ())
		goto cleanup_runstreamthread;

	if (m_IsOutputRtmp) {
		if (!setStreamOutput ())
			goto cleanup_runstreamthread;
	}

	if (m_IsOutputRtmp) {
#if defined (SET_FFMPEG_USE_FILTER)
#else
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		if (!(m_ConvertContext_Video = sws_getContext (m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->width, m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->height, m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->codec->pix_fmt, m_CodecContext_Video->width, m_CodecContext_Video->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr))) {
#pragma warning (pop)
			logAVError (AVERROR (ENOMEM));
			m_LastErrorCode = ErrorCode::ERROR_OUTPUT_VIDEO_CONVERT_CONTEXT_ALLOCATE;
			goto cleanup_runstreamthread;
		}
#endif

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		if (!(m_ConvertContext_Audio = swr_alloc_set_opts (nullptr, av_get_default_channel_layout (m_CodecContext_Audio->channels), m_CodecContext_Audio->sample_fmt, m_CodecContext_Audio->sample_rate, av_get_default_channel_layout (m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->channels), m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->sample_fmt, m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->sample_rate, 0, NULL))) {
#pragma warning (pop)
			logAVError (AVERROR (ENOMEM));
			m_LastErrorCode = ErrorCode::ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_ALLOCATE;
			goto cleanup_runstreamthread;
		}

		if ((ret = swr_init (m_ConvertContext_Audio)) < 0) {
			logAVError (ret);
			m_LastErrorCode = ErrorCode::ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_INITIALIZE;
			goto cleanup_runstreamthread;
		}

		frame_output_video = av_frame_alloc ();
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avpicture_get_size': deprecated
		out_buffer = (uint8_t *) av_malloc (avpicture_get_size (AV_PIX_FMT_YUV420P, m_CodecContext_Video->width, m_CodecContext_Video->height));
#pragma warning (pop)
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avpicture_fill': deprecated
		avpicture_fill ((AVPicture *) frame_output_video, out_buffer, AV_PIX_FMT_YUV420P, m_CodecContext_Video->width, m_CodecContext_Video->height);
#pragma warning (pop)

		fifo = av_audio_fifo_alloc (m_CodecContext_Audio->sample_fmt, m_CodecContext_Audio->channels, 1);
	}

	int64_t start_time = av_gettime ();

#if defined (SET_DIALOG_DEBUG_SHOW)
	int64_t _debug_pts_video_input_pts_base = -1;
	int64_t _debug_pts_video_output_pts_base = -1;
	int64_t _debug_pts_audio_input_pts_base = -1;
	int64_t _debug_pts_audio_output_pts_base = -1;

	int64_t _debug_pts_video_input_pts_diff = -1;
	int64_t _debug_pts_video_output_pts_diff = -1;
	int64_t _debug_pts_audio_input_pts_diff = -1;
	int64_t _debug_pts_audio_output_pts_diff = -1;

	int64_t _debug_frame_count = 0;
#endif

	int64_t pts_video_input_base = -1;
	int64_t pts_audio_input_base = -1;

	int64_t pts_audio_input = 0;

	while ((encode_video || encode_audio) && !m_ThreadBreak) {
		//std::chrono::system_clock::time_point _runtime_base = std::chrono::system_clock::now ();
		//std::chrono::milliseconds _runtime;
		//DWORD _runtime;

		if (encode_video && ((!encode_audio || av_compare_ts (vid_next_pts, time_base_q, aud_next_pts, time_base_q) <= 0) || !m_IsOutputRtmp)) {
			av_init_packet (&packet_decoding);
			packet_decoding.data = nullptr;
			packet_decoding.size = 0;
			if ((ret = av_read_frame (m_FormatContext_InputVideo, &packet_decoding)) >= 0) {
				if (!(frame_input = av_frame_alloc ())) {
					logAVError (AVERROR (ENOMEM));
					m_LastErrorCode = ErrorCode::ERROR_STREAM_VIDEO_INPUT_FRAME_ALLOCATE;
					goto cleanup_runstreamthread;
				}
				if (pts_video_input_base == -1)
					pts_video_input_base = packet_decoding.pts;

#if defined (SET_DIALOG_DEBUG_SHOW)
				if (_debug_pts_video_input_pts_base == -1)
					_debug_pts_video_input_pts_base = packet_decoding.pts;
				_debug_pts_video_input_pts_diff = (packet_decoding.pts - _debug_pts_video_input_pts_base) / (m_FormatContext_InputVideo->streams[packet_decoding.stream_index]->time_base.den / AV_TIME_BASE) / 1000;
#endif

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated, 'avcodec_decode_video2': deprecated
				if ((ret = avcodec_decode_video2 (m_FormatContext_InputVideo->streams[packet_decoding.stream_index]->codec, frame_input, &got_frame_video_decoding, &packet_decoding)) < 0) {
#pragma warning (pop)
					logAVError (ret);
					av_frame_free (&frame_input);
					m_LastErrorCode = ErrorCode::ERROR_STREAM_INPUT_VIDEO_DECODING;
					goto cleanup_runstreamthread;
				}

				if (got_frame_video_decoding) {
					///////////////////////////////////////////////////////////////////////
					if (m_IsOutputScreen) {
						m_ThreadMutex.lock ();
						putScreenFrame (frame_input);
						//freeScreenFrame ();
						//copyScreenFrame (frame_input);
						m_ThreadMutex.unlock ();
#if defined (SET_DIALOG_DEBUG_SHOW)
						int64_t _debug_runtime = av_gettime () - start_time;
						_debug_fps = (double) (++_debug_frame_count) / ((double) _debug_runtime / 1000000.0);
						LPARAM _lparam;
#	if defined (_X86_64)
						memcpy (&_lparam, &_debug_fps, sizeof (double));
#	else // 32bit
						float _float = (float) _debug_fps;
						memcpy (&_lparam, &_float, sizeof (float));
#	endif
						CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_DOUBLE, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_FPS_RUNTIME, _lparam);
						CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_TOTAL_FRAME, (LPARAM) _debug_frame_count);

#	if defined (_X86_64)
						double _speed = _debug_fps / getFrameRate ();
						memcpy (&_lparam, &_speed, sizeof (double));
#	else // 32bit
						float _speed = (float) (_debug_fps / getFrameRate ());
						memcpy (&_lparam, &_speed, sizeof (float));
#	endif
						CMAINDlg::getInstance ()->PostMessage (WM_STATUS_SPEED, (getFrameRate () > 0.0f) ? 1 : 0, _lparam);
#endif
					}
					/////////////////////////////////////////////////////////////////////////
					if (m_IsOutputRtmp) {
#if defined (SET_FFMPEG_USE_FILTER)
						frame_input->pts = av_frame_get_best_effort_timestamp (frame_input);

						if (m_IsInitFilter) {
							if ((ret = setFilter (m_FormatContext_InputVideo, true, m_IsMirrorStream))) {
								logAVError (ret);
								av_frame_free (&frame_input);
								m_LastErrorCode = ErrorCode::ERROR_STREAM_FILTER_APPLY;
								goto cleanup_runstreamthread;
							}
						}
						m_IsInitFilter = false;

						if ((ret = av_buffersrc_add_frame (m_FilterContext_BufferSrc, frame_input)) < 0) {
							logAVError (ret);
							av_frame_free (&frame_input);
							m_LastErrorCode = ErrorCode::ERROR_STREAM_WHILE_FEEDING_THE_FILTERGRAPH;
							goto cleanup_runstreamthread;
						}
						if (!(m_Frame_Filter = av_frame_alloc ())) {
							logAVError (AVERROR (ENOMEM));
							av_frame_free (&frame_input);
							m_LastErrorCode = ErrorCode::ERROR_STREAM_FILTER_FRAME_ALLOCATE;
							goto cleanup_runstreamthread;
						}

						// pull filtered pictures from the filtergraph
						while (1) {
							ret = av_buffersink_get_frame_flags (m_FilterContext_BufferSink, m_Frame_Filter, 0);
							if (ret == AVERROR (EAGAIN) || ret == AVERROR_EOF) {
								//logAVError (ret);
								break;
							}
							if (ret < 0) {
								logAVError (ret);
								av_frame_free (&frame_input);
								av_frame_unref (m_Frame_Filter);
								m_LastErrorCode = ErrorCode::ERROR_STREAM_FILTER_BUFFERSINK_GET_FRAME_FLAGE;
								goto cleanup_runstreamthread;
							}

							if (m_Frame_Filter) {
								if (!(m_ConvertContext_Video = sws_getContext (m_Frame_Filter->width, m_Frame_Filter->height, (AVPixelFormat) m_Frame_Filter->format, m_CodecContext_Video->width, m_CodecContext_Video->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL))) {
									logAVError (AVERROR (ENOMEM));
									av_frame_free (&frame_input);
									av_frame_unref (m_Frame_Filter);
									m_LastErrorCode = ErrorCode::ERROR_OUTPUT_VIDEO_CONVERT_CONTEXT_ALLOCATE;
									goto cleanup_runstreamthread;
								}

								sws_scale (m_ConvertContext_Video, (const uint8_t* const*) m_Frame_Filter->data, m_Frame_Filter->linesize, 0, m_CodecContext_Video->height, frame_output_video->data, frame_output_video->linesize);
								sws_freeContext (m_ConvertContext_Video);
								m_ConvertContext_Video = nullptr;
								frame_output_video->width = m_Frame_Filter->width;
								frame_output_video->height = m_Frame_Filter->height;
								frame_output_video->format = AV_PIX_FMT_YUV420P;
#else
								sws_scale (m_ConvertContext_Video, (const uint8_t* const*) frame_input->data, frame_input->linesize, 0, m_CodecContext_Video->height, frame_output_video->data, frame_output_video->linesize);
								frame_output_video->width = frame_input->width;
								frame_output_video->height = frame_input->height;
								frame_output_video->format = AV_PIX_FMT_YUV420P;
#endif
								packet_encoding.data = nullptr;
								packet_encoding.size = 0;
								av_init_packet (&packet_encoding);
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avcodec_encode_video2': deprecated
								ret = avcodec_encode_video2 (m_CodecContext_Video, &packet_encoding, frame_output_video, &got_frame_video_encoding);
#pragma warning (pop)
								if (got_frame_video_encoding == 1) {
									frame_count_video++;
									packet_encoding.stream_index = m_Stream_Video->index;

									//Write PTS
									AVRational time_base = m_FormatContext_Output->streams[m_StreamIndex_OutputVideo]->time_base;//{ 1, 1000 };
									AVRational r_framerate1 = m_FormatContext_InputVideo->streams[m_StreamIndex_InputVideo]->r_frame_rate;//{ 50, 2 }; 

									int64_t video_duration = (int64_t) ((double) AV_TIME_BASE / av_q2d (r_framerate1));
									int64_t video_runtime_pts = (int64_t) ((double) ((packet_decoding.pts - pts_video_input_base) * AV_TIME_BASE) * av_q2d (m_FormatContext_InputVideo->streams[packet_decoding.stream_index]->time_base)) + video_duration;

									int64_t now_time = av_gettime () - start_time;						

									packet_encoding.pts = av_rescale_q (video_runtime_pts, time_base_q, time_base);
									packet_encoding.dts = packet_encoding.pts;
									packet_encoding.duration = video_duration;
									packet_encoding.pos = -1;

#if defined (SET_DIALOG_DEBUG_SHOW)
									if (_debug_pts_video_output_pts_base == -1)
										_debug_pts_video_output_pts_base = packet_encoding.pts;
									_debug_pts_video_output_pts_diff = packet_encoding.pts - _debug_pts_video_output_pts_base;

#	if defined (_DEBUG)
									CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_PTS_INPUT, (LPARAM) _debug_pts_video_input_pts_diff);
									CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_PTS_OUTPUT, (LPARAM) _debug_pts_video_output_pts_diff);
									CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_PTS_DELAY, (LPARAM) (_debug_pts_video_input_pts_diff - _debug_pts_video_output_pts_diff));
									CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_BOTH_PTS_DELAY, (LPARAM) (_debug_pts_video_input_pts_diff - _debug_pts_audio_input_pts_diff));
#	endif
									int64_t _sync_diff = _debug_pts_video_input_pts_diff - _debug_pts_audio_input_pts_diff;
									if (_sync_diff < -2500)
										m_SyncStatus = SyncStatus::SYNC_STATUS_VIDEO_VERY_SLOW;
									else if (_sync_diff < -1250)
										m_SyncStatus = SyncStatus::SYNC_STATUS_VIDEO_SLOW;
									else if (_sync_diff > 2500)
										m_SyncStatus = SyncStatus::SYNC_STATUS_AUDIO_VERY_SLOW;
									else if (_sync_diff > 1250)
										m_SyncStatus = SyncStatus::SYNC_STATUS_AUDIO_SLOW;
									else
										m_SyncStatus = SyncStatus::SYNC_STATUS_NORMAL;
#endif

									vid_next_pts = (int64_t) (video_runtime_pts); //general timebase

									int64_t video_runtime_frame = (int64_t) (((double) (AV_TIME_BASE * frame_count_video)) / av_q2d (r_framerate1));

									int64_t pts_time = av_rescale_q (packet_encoding.pts, time_base, time_base_q);
									if ((pts_time > now_time) && ((vid_next_pts + pts_time - now_time) < aud_next_pts))
										av_usleep ((unsigned int) (pts_time - now_time));

									m_PacketSizeVideoWrited += packet_encoding.size;
#if defined (SET_DIALOG_DEBUG_SHOW)
									CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_VIDEO_SEND_PACKET_SIZE, (LPARAM) m_PacketSizeVideoWrited);
									CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_TOTAL_SEND_PACKET_SIZE, (LPARAM) (m_PacketSizeVideoWrited + m_PacketSizeAudioWrited));
#endif
									if ((ret = av_interleaved_write_frame (m_FormatContext_Output, &packet_encoding)) < 0) {
										av_packet_unref (&packet_encoding);
										av_packet_unref (&packet_decoding);
#if defined (SET_FFMPEG_USE_FILTER)
										av_frame_unref (m_Frame_Filter);
#endif
										av_frame_free (&frame_input);
										logAVError (ret);
										m_LastErrorCode = ErrorCode::ERROR_STREAM_VIDEO_FRAME_WRITE;
										goto cleanup_runstreamthread;
									}
								}
								av_packet_unref (&packet_encoding);
#if defined (SET_FFMPEG_USE_FILTER)
								av_frame_unref (m_Frame_Filter);
							}
						}
#endif
					}
				}
				av_frame_free (&frame_input);
			}
			else {
				if (ret == AVERROR_EOF) {
					encode_video = 0;
				}
				else {
					av_packet_unref (&packet_decoding);
					m_LastErrorCode = ErrorCode::ERROR_STREAM_INPUT_VIDEO_FRAME_READ;
					goto cleanup_runstreamthread;
				}
			}
			av_packet_unref (&packet_decoding);
		}
		// audio
		else {
			const int output_frame_size = m_CodecContext_Audio->frame_size;

			while (av_audio_fifo_size (fifo) < output_frame_size) {
				if (!(frame_input = av_frame_alloc ())) {
					logAVError (AVERROR (ENOMEM));
					m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_INPUT_FRAME_ALLOCATE;
					goto cleanup_runstreamthread;
				}

				av_init_packet (&packet_encoding);
				packet_encoding.data = nullptr;
				packet_encoding.size = 0;

				if ((ret = av_read_frame (m_FormatContext_InputAudio, &packet_encoding)) < 0) {
					av_frame_free (&frame_input);
					av_packet_unref (&packet_encoding);
					if (ret == AVERROR_EOF) {
						encode_audio = 0;
					}
					else {
						logAVError (ret);
						m_LastErrorCode = ErrorCode::ERROR_STREAM_INPUT_AUDIO_FRAME_READ;
						goto cleanup_runstreamthread;
					}
				}
				if (pts_audio_input_base == -1)
					pts_audio_input_base = packet_encoding.pts;

				// output pts를 만들기 위해.....
				// nb_samples 는 기존 fifo를 통해 남은 찌꺼기도 포함시키자
				nb_samples = -(av_audio_fifo_size (fifo) % output_frame_size);
				// pts를 만들기 위해 input pts를 이용하자.. (microseconds)
				pts_audio_input = (packet_encoding.pts - pts_audio_input_base) / (m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->time_base.den / AV_TIME_BASE); // microsecond
				//OutputDebugStringA ("sync");
				//OutputDebugStringA ((" pts_audio_input:" + std::to_string (pts_audio_input)).c_str ());
				//OutputDebugStringA ((", diff:" + std::to_string (packet_encoding.pts - pts_audio_input_base)).c_str ());
				//OutputDebugStringA ("\n");

#if defined (SET_DIALOG_DEBUG_SHOW)
				if (_debug_pts_audio_input_pts_base == -1)
					_debug_pts_audio_input_pts_base = packet_encoding.pts;
				_debug_pts_audio_input_pts_diff = (packet_encoding.pts - _debug_pts_audio_input_pts_base) / (m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->time_base.den / AV_TIME_BASE) / 1000;
				//av_log (nullptr, AV_LOG_INFO, ("audio input pts:" + std::to_string (packet_encoding.pts) + " duration:" + std::to_string (packet_encoding.duration) + "\n").c_str ());
#endif

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
				if ((ret = avcodec_decode_audio4 (m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec, frame_input, &got_frame_audio_decoding, &packet_encoding)) < 0) {
#pragma warning (pop)
					av_frame_free (&frame_input);
					av_packet_unref (&packet_encoding);
					logAVError (ret);
					m_LastErrorCode = ErrorCode::ERROR_STREAM_INPUT_AUDIO_DECODING;
					goto cleanup_runstreamthread;
				}
				av_packet_unref (&packet_encoding);

				if (got_frame_audio_decoding) {
					if (!(converted_input_samples = (uint8_t **) calloc (m_CodecContext_Audio->channels, sizeof (*converted_input_samples)))) {
						av_frame_free (&frame_input);
						logAVError (AVERROR (ENOMEM));
						m_LastErrorCode = ErrorCode::ERROR_OUTPUT_AUDIO_CONVERT_SAMPLE_POINTERS_ALLOCATE;
						goto cleanup_runstreamthread;
					}

					if ((ret = av_samples_alloc (converted_input_samples, nullptr, m_CodecContext_Audio->channels, frame_input->nb_samples, m_CodecContext_Audio->sample_fmt, 0)) < 0) {
						free (converted_input_samples);
						av_frame_free (&frame_input);
						logAVError (ret);
						m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_CONVERT_SAMPLES_ALLOCATE;
						goto cleanup_runstreamthread;
					}

					if ((ret = swr_convert (m_ConvertContext_Audio, converted_input_samples, frame_input->nb_samples, (const uint8_t **) frame_input->extended_data, frame_input->nb_samples)) < 0) {
						av_freep (&converted_input_samples[0]);
						free (converted_input_samples);
						av_frame_free (&frame_input);
						logAVError (ret);
						m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_INPUT_SAMPLE_CONVERT;
						goto cleanup_runstreamthread;
					}

					if ((ret = av_audio_fifo_realloc (fifo, av_audio_fifo_size (fifo) + frame_input->nb_samples)) < 0) {
						av_freep (&converted_input_samples[0]);
						free (converted_input_samples);
						av_frame_free (&frame_input);
						logAVError (ret);
						m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_FIFO_REALLOCATE;
						goto cleanup_runstreamthread;
					}

					if (av_audio_fifo_write (fifo, (void **) converted_input_samples, frame_input->nb_samples) < frame_input->nb_samples) {
						av_freep (&converted_input_samples[0]);
						free (converted_input_samples);
						av_frame_free (&frame_input);
						logAVError (ret);
						m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_FIFO_WRITE;
						goto cleanup_runstreamthread;
					}
					av_freep (&converted_input_samples[0]);
					free (converted_input_samples);

					//// AV meter
					//if (m_CodecContext_Audio->sample_fmt == AV_SAMPLE_FMT_FLTP) {
					//	int _plane_size;
					//	int _data_size = av_samples_get_buffer_size (&_plane_size, m_CodecContext_Audio->channels, frame_input->nb_samples, m_CodecContext_Audio->sample_fmt, 1);
					//	for (int _nb = 0; _nb < _plane_size / sizeof (float); _nb++) {
					//		for (int _ch = 0; _ch < m_CodecContext_Audio->channels; _ch++) {
					//			//float *_float_p = (float *) frame_input->extended_data[_ch];
					//			float *_float_p = (float *) (frame_input->extended_data + (sizeof (float) * _ch));
					//			float _float = _float_p[_nb];
					//			OutputDebugStringUTF8 (("### ((uint16_t *) frame_input->extended_data[" + std::to_string (_ch) + "])[" + std::to_string (_nb) + "] : " + std::to_string (_float) + "\n").c_str ());
					//		}
					//	}
					//}
				} // if (got_frame_audio_decoding) {

				av_frame_free (&frame_input);
			} // while (av_audio_fifo_size (fifo) < output_frame_size) {

			if (av_audio_fifo_size (fifo) >= output_frame_size) {
				if (!(frame_output_audio = av_frame_alloc ())) {
					logAVError (AVERROR (ENOMEM));
					m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_OUTPUT_FRAME_ALLOCATE;
					goto cleanup_runstreamthread;
				}

				const int frame_size = FFMIN (av_audio_fifo_size (fifo), m_CodecContext_Audio->frame_size);

				frame_output_audio->nb_samples = frame_size;
				frame_output_audio->channels = m_CodecContext_Audio->channels;
				frame_output_audio->channel_layout = m_CodecContext_Audio->channel_layout;
				frame_output_audio->format = m_CodecContext_Audio->sample_fmt;
				frame_output_audio->sample_rate = m_CodecContext_Audio->sample_rate;

				if ((ret = av_frame_get_buffer (frame_output_audio, 0)) < 0) {
					av_frame_free (&frame_output_audio);
					logAVError (ret);
					m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_FRAME_SAMPLE_ALLOCATE;
					goto cleanup_runstreamthread;
				}

				if (av_audio_fifo_read (fifo, (void **) frame_output_audio->data, frame_size) < frame_size) {
					av_frame_free (&frame_output_audio);
					m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_FIFO_READ;
					goto cleanup_runstreamthread;
				}

				// AVPacket output_packet;
				av_init_packet (&packet_encoding);
				packet_encoding.data = nullptr;
				packet_encoding.size = 0;

				nb_samples += frame_output_audio->nb_samples;

				// set audio mute
				if (m_IsAudioMute) {
					//memset (frame_output_audio->data[0], 0x00, frame_output_audio->linesize[0]);
					for (int i = 0; i < 8; i++) {
						if (frame_output_audio->buf[i]) {
							if (frame_output_audio->buf[i]->data && frame_output_audio->buf[i]->size > 0)
								memset (frame_output_audio->buf[i]->data, 0x00, frame_output_audio->buf[i]->size);
						}
					}
				}

				//for (int i = 0; i < 8; i++) {
				//	if (frame_output_audio->buf[i])
				//		OutputDebugStringUTF8 (("sound buffer[" + std::to_string (i) + "] size : " + std::to_string (frame_output_audio->buf[i]->size) + "\n").c_str ());
				//		//OutputDebugStringUTF8 (("sound buffer[" + std::to_string (i) + "] data : " + std::to_string (frame_output_audio->buf[i]->data) + "\n").c_str ());
				//}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avcodec_encode_audio2': deprecated
				if ((ret = avcodec_encode_audio2 (m_CodecContext_Audio, &packet_encoding, frame_output_audio, &got_frame_audio_encoding)) < 0) {
#pragma warning (pop)
					av_frame_free (&frame_output_audio);
					av_packet_unref (&packet_encoding);
					logAVError (ret);
					m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_FRAME_ENCODE;
					goto cleanup_runstreamthread;
				}

				if (got_frame_audio_encoding) {
					packet_encoding.stream_index = 1;

					AVRational time_base = m_FormatContext_Output->streams[m_StreamIndex_OutputAudio]->time_base;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
					AVRational r_framerate1 = {m_FormatContext_InputAudio->streams[m_StreamIndex_InputAudio]->codec->sample_rate, 1};// { 44100, 1};  
#pragma warning (pop)
					int64_t calc_duration = pts_audio_input + (int64_t) ((double) AV_TIME_BASE * nb_samples / av_q2d (r_framerate1));
					int64_t audio_duration = (int64_t) ((double) (AV_TIME_BASE * frame_output_audio->nb_samples) / av_q2d (r_framerate1));

					int64_t now_time = av_gettime () - start_time;
					//OutputDebugStringA (("Audio OUTPUT PTS:" + std::to_string (calc_duration)).c_str ());
					//OutputDebugStringA ("\n");

					packet_encoding.pts = av_rescale_q (calc_duration, time_base_q, time_base);
					packet_encoding.dts = packet_encoding.pts;
					packet_encoding.duration = audio_duration;

#if defined (SET_DIALOG_DEBUG_SHOW)
					if (_debug_pts_audio_output_pts_base == -1)
						_debug_pts_audio_output_pts_base = packet_encoding.pts;
					_debug_pts_audio_output_pts_diff = packet_encoding.pts - _debug_pts_audio_output_pts_base;

#	if defined (_DEBUG)
					CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_AUDIO_PTS_INPUT, (LPARAM) _debug_pts_audio_input_pts_diff);
					CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_AUDIO_PTS_OUTPUT, (LPARAM) _debug_pts_audio_output_pts_diff);
					CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_AUDIO_PTS_DELAY, (LPARAM) (_debug_pts_audio_input_pts_diff - _debug_pts_audio_output_pts_diff));
					CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_BOTH_PTS_DELAY, (LPARAM) (_debug_pts_video_input_pts_diff - _debug_pts_audio_input_pts_diff));
#	endif
					int64_t _sync_diff = _debug_pts_video_input_pts_diff - _debug_pts_audio_input_pts_diff;
					if (_sync_diff < -2500)
						m_SyncStatus = SyncStatus::SYNC_STATUS_VIDEO_VERY_SLOW;
					else if (_sync_diff < -1250)
						m_SyncStatus = SyncStatus::SYNC_STATUS_VIDEO_SLOW;
					else if (_sync_diff > 2500)
						m_SyncStatus = SyncStatus::SYNC_STATUS_AUDIO_VERY_SLOW;
					else if (_sync_diff > 1250)
						m_SyncStatus = SyncStatus::SYNC_STATUS_AUDIO_SLOW;
					else
						m_SyncStatus = SyncStatus::SYNC_STATUS_NORMAL;
#endif

					aud_next_pts = (int64_t) (calc_duration);

					int64_t pts_time = av_rescale_q (packet_encoding.pts, time_base, time_base_q);
					if ((pts_time > now_time) && ((aud_next_pts + pts_time - now_time) < vid_next_pts))
						av_usleep ((unsigned int) (pts_time - now_time));

					m_PacketSizeAudioWrited += packet_encoding.size;
#if defined (SET_DIALOG_DEBUG_SHOW)
					CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_AUDIO_SEND_PACKET_SIZE, (LPARAM) m_PacketSizeAudioWrited);
					CMAINDlg::getInstance ()->m_DialogDebug->PostMessage (WM_DEBUG_UPDATE_INT64, CDialogDebug::DebugType::DEBUG_TYPE_TOTAL_SEND_PACKET_SIZE, (LPARAM) (m_PacketSizeVideoWrited + m_PacketSizeAudioWrited));
#endif
					if ((ret = av_interleaved_write_frame (m_FormatContext_Output, &packet_encoding)) < 0) {
						av_frame_free (&frame_output_audio);
						av_packet_unref (&packet_encoding);
						logAVError (ret);
						m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_FRAME_WRITE;
						goto cleanup_runstreamthread;
					}
				}
				av_frame_free (&frame_output_audio);
				av_packet_unref (&packet_encoding);
			}
		}
		m_IsStopValid = true;
	}

	//Flush Encoder
	if (m_IsOutputRtmp) {
		if (av_compare_ts (vid_next_pts, time_base_q, aud_next_pts, time_base_q) <= 0) {
			if (encode_video) {
				if ((ret = flushEncoderVideo (m_FormatContext_InputVideo, m_FormatContext_Output, frame_count_video)) < 0) {
					logAVError (ret);
					//m_LastErrorCode = ErrorCode::ERROR_STREAM_VIDEO_ENCODER_FLUSHING;
					//goto cleanup_runstreamthread;
				}
			}
			if (encode_audio) {
				if ((ret = flushEncoderAudio (m_FormatContext_InputAudio, m_FormatContext_Output, pts_audio_input, nb_samples)) < 0) {
					logAVError (ret);
					//m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_ENCODER_FLUSHING;
					//goto cleanup_runstreamthread;
				}
			}
		}
		else {
			if (encode_audio) {
				if ((ret = flushEncoderAudio (m_FormatContext_InputAudio, m_FormatContext_Output, pts_audio_input, nb_samples)) < 0) {
					logAVError (ret);
					//m_LastErrorCode = ErrorCode::ERROR_STREAM_AUDIO_ENCODER_FLUSHING;
					//goto cleanup_runstreamthread;
				}
			}
			if (encode_video) {
				if ((ret = flushEncoderVideo (m_FormatContext_InputVideo, m_FormatContext_Output, frame_count_video)) < 0) {
					logAVError (ret);
					//m_LastErrorCode = ErrorCode::ERROR_STREAM_VIDEO_ENCODER_FLUSHING;
					//goto cleanup_runstreamthread;
				}
			}
		}
		//Write file trailer
		av_write_trailer (m_FormatContext_Output);
	}

cleanup_runstreamthread:
	
#if defined (SET_FFMPEG_USE_FILTER)
	if (m_FilterGraph)
		avfilter_graph_free (&m_FilterGraph);
#endif
	if (frame_output_video)
		av_frame_free (&frame_output_video);

	if (out_buffer)
		av_free (out_buffer);

	if (fifo)
		av_audio_fifo_free (fifo);

	closeStream ();
	m_IsStopValid = true;

	// set error callback
	if (m_LastErrorCode != ErrorCode::ERROR_NONE)
		CMAINDlg::getInstance ()->PostMessage (WM_RTMP_ERROR, 0, 0);

	m_StatusCode = StatusCode::STATUS_READY;
}

int CRTMP::flushEncoderVideo (AVFormatContext *fmt_ctx_input_video, AVFormatContext *fmt_ctx_output, int64_t framecnt)
{
	int ret;
	int got_frame;
	AVPacket packet_encode;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	if (!(fmt_ctx_output->streams[m_StreamIndex_OutputVideo]->codec->codec->capabilities & CODEC_CAP_DELAY))
#pragma warning (pop)
		return 0;

	while (1) {
		packet_encode.data = nullptr;
		packet_encode.size = 0;
		av_init_packet (&packet_encode);
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		if ((ret = avcodec_encode_video2 (fmt_ctx_output->streams[m_StreamIndex_OutputVideo]->codec, &packet_encode, nullptr, &got_frame)) < 0)
#pragma warning (pop)
			break;
		if (!got_frame) {
			ret = 0;
			break;
		}

		framecnt++;

		AVRational time_base = fmt_ctx_output->streams[m_StreamIndex_OutputVideo]->time_base;
		AVRational r_framerate1 = fmt_ctx_input_video->streams[m_StreamIndex_InputVideo]->r_frame_rate;
		AVRational time_base_q = { 1, AV_TIME_BASE };

		int64_t calc_duration = (int64_t) ((double) AV_TIME_BASE / av_q2d (r_framerate1));

		packet_encode.pts = av_rescale_q (framecnt * calc_duration, time_base_q, time_base);
		packet_encode.dts = packet_encode.pts;
		packet_encode.duration = av_rescale_q (calc_duration, time_base_q, time_base);

		packet_encode.pos = -1;

		m_PacketSizeVideoWrited += packet_encode.size;
		ret = av_interleaved_write_frame (fmt_ctx_output, &packet_encode);
		av_packet_unref (&packet_encode);
//#pragma warning (push)
//#pragma warning (disable : 4996) // warning C4996: 'av_free_packet': deprecated
//		av_free_packet (&enc_pkt);
//#pragma warning (pop)
		if (ret < 0)
			break;
	}
	return ret;
}

int CRTMP::flushEncoderAudio (AVFormatContext *fmt_ctx_input_audio, AVFormatContext *fmt_ctx_output, int64_t pts_audio_input, int64_t nb_samples)
{
	int ret;
	int got_frame;
	AVPacket packet_encode;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	if (!(fmt_ctx_output->streams[m_StreamIndex_OutputAudio]->codec->codec->capabilities & CODEC_CAP_DELAY))
#pragma warning (pop)
		return 0;
	while (1) {
		packet_encode.data = nullptr;
		packet_encode.size = 0;
		av_init_packet (&packet_encode);
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated, 'avcodec_encode_audio2': deprecated
		if ((ret = avcodec_encode_audio2 (fmt_ctx_output->streams[m_StreamIndex_OutputAudio]->codec, &packet_encode, nullptr, &got_frame)) < 0)
#pragma warning (pop)
			break;
		if (!got_frame) {
			ret = 0;
			break;
		}

		nb_samples += 1024;

		AVRational time_base = fmt_ctx_output->streams[m_StreamIndex_OutputAudio]->time_base;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
		AVRational r_framerate1 = {fmt_ctx_input_audio->streams[m_StreamIndex_InputAudio]->codec->sample_rate, 1};
#pragma warning (pop)
		AVRational time_base_q = {1, AV_TIME_BASE};

		//int64_t calc_duration = (int64_t) ((double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1)));
		int64_t calc_duration = pts_audio_input + (int64_t) ((double) AV_TIME_BASE * nb_samples / av_q2d (r_framerate1));
		int64_t audio_duration = (int64_t) ((double) (AV_TIME_BASE * 1024) / av_q2d (r_framerate1));

		packet_encode.pts = av_rescale_q (calc_duration, time_base_q, time_base);
		packet_encode.dts = packet_encode.pts;
		packet_encode.duration = audio_duration;
		//Parameters
		//enc_pkt.pts = av_rescale_q(nb_samples*calc_duration, time_base_q, time_base);
		//enc_pkt.dts = enc_pkt.pts;
		//enc_pkt.duration = 1024;

		packet_encode.pos = -1;

		m_PacketSizeAudioWrited += packet_encode.size;
		ret = av_interleaved_write_frame (fmt_ctx_output, &packet_encode);
		av_packet_unref (&packet_encode);
//#pragma warning (push)
//#pragma warning (disable : 4996) // warning C4996: 'av_free_packet': deprecated
//		av_free_packet (&enc_pkt);
//#pragma warning (pop)
		if (ret < 0)
			break;
	}
	return ret;
}

void CRTMP::getDevices (const GUID deviceClass, std::vector<Device> &device)
{
	device.clear ();

	HRESULT _hr = CoInitialize (nullptr);
	if (FAILED (_hr))
		return;

	ICreateDevEnum *_dev_enum;
	IEnumMoniker *_enum_moniker = nullptr;

	if (SUCCEEDED ((_hr = CoCreateInstance (CLSID_SystemDeviceEnum, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS (&_dev_enum))))) {
		if ((_hr = _dev_enum->CreateClassEnumerator (deviceClass, &_enum_moniker, 0)) == S_FALSE)
			_hr = VFW_E_NOT_FOUND;
		_dev_enum->Release ();
	}

	if (SUCCEEDED (_hr)) {
		// Fill the map with id and friendly device name
		IMoniker *_moniker = nullptr;
		while (_enum_moniker->Next (1, &_moniker, nullptr) == S_OK) {
			IPropertyBag *pPropBag;
			if (FAILED ((_hr = _moniker->BindToStorage (0, 0, IID_PPV_ARGS (&pPropBag))))) {
				_moniker->Release ();
				continue;
			}

			VARIANT _var;
			VariantInit (&_var);

			std::string deviceName;
			std::string devicePath;
			int waveInID;

			if (FAILED ((_hr = pPropBag->Read (_T ("Description"), &_var, 0))))
				_hr = pPropBag->Read (_T ("FriendlyName"), &_var, 0);

			if (FAILED (_hr)) {
				VariantClear (&_var);
				continue;
			}
			else {
				deviceName = CSupport::wchar_to_utf8 (_var.bstrVal);
			}

			VariantClear (&_var);

			if (deviceClass == CLSID_VideoInputDeviceCategory) {
				if (FAILED ((_hr = pPropBag->Read (_T ("DevicePath"), &_var, 0)))) {
					VariantClear (&_var);
					continue;
				}
				else {
					devicePath = CSupport::wchar_to_utf8 (_var.bstrVal);
				}
				waveInID = 0;
			}
			else if (deviceClass == CLSID_AudioInputDeviceCategory) {
				// version 0.9.10
				// 소프트웨어 마이크의 경우 WaveInID를 가지고 있지 않다 (예:XSplitBroadcaster)
				// 때문에 사용하지 않는 WaveInID는 무시..
				//if (FAILED ((_hr = pPropBag->Read (_T ("WaveInID"), &_var, 0)))) {
				//	VariantClear (&_var);
				//	continue;
				//}
				//else {
				//	waveInID = _var.iVal;
				//}
				waveInID = _var.iVal;

				devicePath = "";
			}

			Device _device;
			_device.deviceName = deviceName;
			_device.devicePath = devicePath;
			_device.waveInID = waveInID;
			device.push_back (_device);
		}
		_enum_moniker->Release ();
	}
	CoUninitialize ();
}

void CRTMP::logAVError (int errcode)
{
	char *errbuf = (char *) calloc (AV_ERROR_MAX_STRING_SIZE, sizeof (char));
	av_strerror (errcode, errbuf, AV_ERROR_MAX_STRING_SIZE);
	std::string _error_message = "code:" + std::to_string (errcode) + " message:" + errbuf;
	av_log (nullptr, AV_LOG_ERROR, _error_message.c_str ());
	delete [] errbuf;
}

void CRTMP::putScreenFrame (AVFrame *frame)
{
	if (!frame)
		return;

	AVFrame *_new_frame = av_frame_alloc ();
	if (!_new_frame)
		return;

	_new_frame->format = frame->format;
	_new_frame->width = frame->width;
	_new_frame->height = frame->height;
	_new_frame->channels = frame->channels;
	_new_frame->channel_layout = frame->channel_layout;
	_new_frame->nb_samples = frame->nb_samples;
	av_frame_get_buffer (_new_frame, 32);
	av_frame_copy (_new_frame, frame);
	av_frame_copy_props (_new_frame, frame);

	while (m_FrameScreenQueue.size () >= SET_SCREENFRAME_MAX_QUEUE) {
		av_frame_free (&(*m_FrameScreenQueue.begin ()));
		m_FrameScreenQueue.erase (m_FrameScreenQueue.begin ());
	}
	m_FrameScreenQueue.push_back (_new_frame);
}

void CRTMP::clearScreenFrame (void)
{
	while (m_FrameScreenQueue.size () > 0) {
		av_frame_free (&(*m_FrameScreenQueue.begin ()));
		m_FrameScreenQueue.erase (m_FrameScreenQueue.begin ());
	}
	m_FrameScreenQueue.clear ();
}

uint8_t *CRTMP::popScreenFrameToBitmap (void)
{
	if (m_FrameScreenQueue.size () < 1)
		return nullptr;

	AVPicture ac_pic;
	SwsContext *sws_ctx;
	BITMAPFILEHEADER bitmap_file_header;
	BITMAPINFOHEADER bitmap_info_header;

	int _ret;

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avpicture_alloc': deprecated
	if ((_ret = avpicture_alloc (&ac_pic, AV_PIX_FMT_BGR24, (*m_FrameScreenQueue.begin ())->width, (*m_FrameScreenQueue.begin ())->height)) < 0)
#pragma warning (pop)
		return nullptr;

	if (!(sws_ctx = sws_getContext ((*m_FrameScreenQueue.begin ())->width, (*m_FrameScreenQueue.begin ())->height, static_cast<AVPixelFormat>((*m_FrameScreenQueue.begin ())->format), (*m_FrameScreenQueue.begin ())->width, (*m_FrameScreenQueue.begin ())->height, AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr, nullptr, nullptr))) {
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avpicture_free': deprecated
		avpicture_free (&ac_pic);
#pragma warning (pop)
		return nullptr;
	}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVPicture::data': deprecated
	if ((_ret = sws_scale (sws_ctx, (*m_FrameScreenQueue.begin ())->data, (*m_FrameScreenQueue.begin ())->linesize, 0, (*m_FrameScreenQueue.begin ())->height, ac_pic.data, ac_pic.linesize)) < 0) {
#pragma warning (pop)
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avpicture_free': deprecated
		avpicture_free (&ac_pic);
#pragma warning (pop)
		sws_freeContext (sws_ctx);
		return nullptr;
	}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVPicture::linesize': deprecated
	//uint8_t pixel_size = ac_pic.linesize[0] * (*m_FrameScreenQueue.begin ())->height;
#pragma warning (pop)

	memset (&bitmap_file_header, 0x00, sizeof (BITMAPFILEHEADER));
	bitmap_file_header.bfType = 0x4D42;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVPicture::linesize': deprecated
	bitmap_file_header.bfSize = sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + (ac_pic.linesize[0] * (*m_FrameScreenQueue.begin ())->height);
#pragma warning (pop)
	bitmap_file_header.bfReserved1 = 0;
	bitmap_file_header.bfReserved2 = 0;
	bitmap_file_header.bfOffBits = sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER);

	memset (&bitmap_info_header, 0x00, sizeof (BITMAPINFOHEADER));
	bitmap_info_header.biSize = sizeof (BITMAPINFOHEADER);
	bitmap_info_header.biWidth = (*m_FrameScreenQueue.begin ())->width;
	bitmap_info_header.biHeight = 0 - (*m_FrameScreenQueue.begin ())->height;
	bitmap_info_header.biPlanes = 1;
	bitmap_info_header.biBitCount = 24;
	bitmap_info_header.biCompression = BI_RGB;
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVPicture::linesize': deprecated
	bitmap_info_header.biSizeImage = ac_pic.linesize[0] * (*m_FrameScreenQueue.begin ())->height;
#pragma warning (pop)
	bitmap_info_header.biXPelsPerMeter = 0;
	bitmap_info_header.biYPelsPerMeter = 0;
	bitmap_info_header.biClrUsed = 0;
	bitmap_info_header.biClrImportant = 0;

	uint8_t *result = new uint8_t[bitmap_file_header.bfSize];
	if (!result) {
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avpicture_free': deprecated
		avpicture_free (&ac_pic);
#pragma warning (pop)
		sws_freeContext (sws_ctx);

		return nullptr;
	}
	memcpy (result, &bitmap_file_header, sizeof (BITMAPFILEHEADER));
	memcpy (result + sizeof (BITMAPFILEHEADER), &bitmap_info_header, sizeof (BITMAPINFOHEADER));
#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVPicture::data': deprecated
	if (!m_IsMirrorScreen) {
		memcpy (result + sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER), ac_pic.data[0], (ac_pic.linesize[0] * (*m_FrameScreenQueue.begin ())->height));
	}
	else {
		int _mem_width = (ac_pic.linesize[0] / 3) * 3;
		uint8_t *_pixel_base_src, *_pixel_base_des;
		for (int y = 0; y < (*m_FrameScreenQueue.begin ())->height; y++) {
			for (int x = 0; x < ac_pic.linesize[0]; x += 3) {
				_pixel_base_des = result + sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + (y * ac_pic.linesize[0]) + x;
				_pixel_base_src = ac_pic.data[0] + (y * ac_pic.linesize[0]) + (_mem_width - x - 3);
				*_pixel_base_des = *_pixel_base_src;
				*(_pixel_base_des + 1) = *(_pixel_base_src + 1);
				*(_pixel_base_des + 2) = *(_pixel_base_src + 2);
				//memcpy (result + sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + (y * ac_pic.linesize[0]) + x, ac_pic.data[0] + (y * ac_pic.linesize[0]) + (_mem_width - x - 3), 3);
				//*(result + sizeof (BITMAPFILEHEADER) + sizeof (BITMAPINFOHEADER) + (y * ac_pic.linesize[0]) + x) = *(ac_pic.data[0] + (y * ac_pic.linesize[0]) + (ac_pic.linesize[0] - x));
			}
		}
	}
#pragma warning (pop)

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'avpicture_free': deprecated
	avpicture_free (&ac_pic);
#pragma warning (pop)
	sws_freeContext (sws_ctx);

	av_frame_free (&(*m_FrameScreenQueue.begin ()));
	m_FrameScreenQueue.erase (m_FrameScreenQueue.begin ());

	return result;
}

wchar_t *CRTMP::getErrorMessage (void)
{
	switch (m_LastErrorCode) {
		case ErrorCode::ERROR_NONE :
			return _T ("");
		case ErrorCode::ERROR_INPUT_VIDEO_DEVICE_OPEN :
			// Couldn't open input video stream.
			return _T ("영상 입력 장치(카메라)를 열 수 없습니다. 다른 앱이 사용중일 수 있습니다. 확인 후 다시 시도해 주세요.\n(error code : ERROR_INPUT_VIDEO_DEVICE_OPEN)");
		case ErrorCode::ERROR_INPUT_AUDIO_DEVICE_OPEN :
			// Couldn't open input audio stream.
			return _T ("소리 입력 장치(마이크)를 열 수 없습니다. 다른 앱이 사용중일 수 있습니다. 확인 후 다시 시도해 주세요.\n(error code : ERROR_INPUT_AUDIO_DEVICE_OPEN)");
		case ErrorCode::ERROR_INPUT_VIDEO_DEVICE_INFORMATION :
			// Couldn't find video stream information.
			return _T ("영상 입력 장치의 정보를 읽을 수 없습니다.\n(error code : ERROR_INPUT_VIDEO_DEVICE_INFORMATION)");
		case ErrorCode::ERROR_INPUT_VIDEO_DEVICE_FIND_STREAM :
			// Couldn't find a video stream
			return _T ("영상 입력 장치의 영상 스트림을 찾을 수 없습니다.\n(error code : ERROR_INPUT_VIDEO_DEVICE_FIND_STREAM)");
		case ErrorCode::ERROR_INPUT_VIDEO_CODEC_FIND :
			// Could not find video codec
			return _T ("입력된 영상의 코덱을 찾을 수 없습니다.\n(error code : ERROR_INPUT_VIDEO_CODEC_FIND)");
		case ErrorCode::ERROR_INPUT_VIDEO_CODEC_OPEN :
			// Could not open video codec
			return _T ("입력된 영상의 코덱을 열 수 없습니다.\n(error code : ERROR_INPUT_VIDEO_CODEC_OPEN)");
		case ErrorCode::ERROR_INPUT_AUDIO_DEVICE_INFORMATION :
			// Couldn't find audio stream information.
			return _T ("소리 입력 장치의 정보를 읽을 수 없습니다.\n(error code : ERROR_INPUT_AUDIO_DEVICE_INFORMATION)");
		case ErrorCode::ERROR_INPUT_AUDIO_DEVICE_FIND_STREAM :
			// Couldn't find a audio stream
			return _T ("소리 입력 장치의 영상 스트림을 찾을 수 없습니다.\n(error code : ERROR_INPUT_AUDIO_DEVICE_FIND_STREAM)");
		case ErrorCode::ERROR_INPUT_AUDIO_CODEC_FIND :
			// Could not find audio codec
			return _T ("입력된 소리의 코덱을 찾을 수 없습니다.\n(error code : ERROR_INPUT_AUDIO_CODEC_FIND)");
		case ErrorCode::ERROR_INPUT_AUDIO_CODEC_OPEN :
			// Could not open audio codec
			return _T ("입력된 소리의 코덱을 열 수 없습니다.\n(error code : ERROR_INPUT_AUDIO_CODEC_OPEN)");
		case ErrorCode::ERROR_OUTPUT_RTMP_URL_NOT_SET :
			// rtmp url is not set.
			return _T ("출력할 서버의 주소가 잘못되었거나 서버를 찾을 수 없습니다.\n(error code : ERROR_OUTPUT_RTMP_URL_NOT_SET)");
		case ErrorCode::ERROR_OUTPUT_RTMP_URL_INVALID :
			// rtmp url is invalid.
			return _T ("출력할 서버의 주소가 잘못되었거나 서버를 찾을 수 없습니다.\n(error code : ERROR_OUTPUT_RTMP_URL_INVALID)");
		case ErrorCode::ERROR_OUTPUT_FORMAT_ALLOCATE :
			// Failed to allocate an output format.
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_FORMAT_ALLOCATE)");
		case ErrorCode::ERROR_OUTPUT_VIDEO_ENCODER_FIND :
			// Can not find output video encoder
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_VIDEO_ENCODER_FIND)");
		case ErrorCode::ERROR_OUTPUT_VIDEO_ENCODER_OPEN :
			// Failed to open output video encoder
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_VIDEO_ENCODER_OPEN)");
		case ErrorCode::ERROR_OUTPUT_VIDEO_STREAM_ADD :
			// Could not add a new video stream
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_VIDEO_STREAM_ADD)");
		case ErrorCode::ERROR_OUTPUT_AUDIO_ENCODER_FIND :
			// Can not find output audio encoder
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_AUDIO_ENCODER_FIND)");
		case ErrorCode::ERROR_OUTPUT_AUDIO_ENCODER_OPEN :
			// Failed to open output audio encoder
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_AUDIO_ENCODER_OPEN)");
		case ErrorCode::ERROR_OUTPUT_AUDIO_STREAM_ADD :
			// Could not add a new audio stream
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_AUDIO_STREAM_ADD)");
		case ErrorCode::ERROR_OUTPUT_STREAM_OPEN :
			// Failed to open output stream
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_STREAM_OPEN)");
		case ErrorCode::ERROR_OUTPUT_VIDEO_CONVERT_CONTEXT_ALLOCATE :
			 // Failed to allocate video convert context
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_VIDEO_CONVERT_CONTEXT_ALLOCATE)");
		case ErrorCode::ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_ALLOCATE :
			 // Failed to allocate audio convert context
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_ALLOCATE)");
		case ErrorCode::ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_INITIALIZE :
			// Failed to initialize the resampler to be able to convert audio sample formats
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_AUDIO_CONVERT_CONTEXT_INITIALIZE)");
		case ErrorCode::ERROR_OUTPUT_AUDIO_CONVERT_SAMPLE_POINTERS_ALLOCATE :
			// Could not allocate converted input sample pointers
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_OUTPUT_AUDIO_CONVERT_SAMPLE_POINTERS_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_VIDEO_PACKET_ALLOCATE :
			// Failed to allocate video packet
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_VIDEO_PACKET_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_VIDEO_INPUT_FRAME_ALLOCATE :
			// Failed to allocate input video frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_VIDEO_INPUT_FRAME_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_INPUT_VIDEO_DECODING :
			// Could not decode video frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_INPUT_VIDEO_DECODING)");
		case ErrorCode::ERROR_STREAM_INPUT_VIDEO_FRAME_READ :
			// Could not read video frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_INPUT_VIDEO_FRAME_READ)");
		case ErrorCode::ERROR_STREAM_INPUT_AUDIO_DECODING :
			// Could not decode audio frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_INPUT_AUDIO_DECODING)");
		case ErrorCode::ERROR_STREAM_INPUT_AUDIO_FRAME_READ :
			// Could not read audio frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_INPUT_AUDIO_FRAME_READ)");
		case ErrorCode::ERROR_STREAM_AUDIO_INPUT_FRAME_ALLOCATE :
			// Failed to allocate input audio frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_INPUT_FRAME_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_AUDIO_CONVERT_SAMPLES_ALLOCATE :
			// Could not allocate converted input samples
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_CONVERT_SAMPLES_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_AUDIO_INPUT_SAMPLE_CONVERT :
			// Could not convert input samples
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_INPUT_SAMPLE_CONVERT)");
		case ErrorCode::ERROR_STREAM_AUDIO_FIFO_REALLOCATE :
			// Could not reallocate FIFO
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_FIFO_REALLOCATE)");
		case ErrorCode::ERROR_STREAM_AUDIO_FIFO_WRITE :
			// Could not write data to FIFO
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_FIFO_WRITE)");
		case ErrorCode::ERROR_STREAM_AUDIO_OUTPUT_FRAME_ALLOCATE :
			// Failed to allocate input audio frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_OUTPUT_FRAME_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_AUDIO_FRAME_SAMPLE_ALLOCATE :
			// Could not allocate output frame samples
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_FRAME_SAMPLE_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_AUDIO_FIFO_READ :
			// Could not read data from FIFO
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_FIFO_READ)");
		case ErrorCode::ERROR_STREAM_AUDIO_FRAME_ENCODE :
			// Could not encode frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_FRAME_ENCODE)");
		case ErrorCode::ERROR_STREAM_AUDIO_FRAME_WRITE :
			// Could not write audio frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_FRAME_WRITE)");
		case ErrorCode::ERROR_STREAM_VIDEO_FRAME_WRITE :
			// Could not write video frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_VIDEO_FRAME_WRITE)");
		case ErrorCode::ERROR_STREAM_VIDEO_ENCODER_FLUSHING :
			// Flushing video encoder failed
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_VIDEO_ENCODER_FLUSHING)");
		case ErrorCode::ERROR_STREAM_AUDIO_ENCODER_FLUSHING :
			// Flushing audio encoder failed
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_AUDIO_ENCODER_FLUSHING)");
		case ErrorCode::ERROR_STREAM_WHILE_FEEDING_THE_FILTERGRAPH :
			// Error while feeding the filtergraph
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_WHILE_FEEDING_THE_FILTERGRAPH)");
		case ErrorCode::ERROR_STREAM_FILTER_FRAME_ALLOCATE :
			// FFailed to allocate filter frame
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_FILTER_FRAME_ALLOCATE)");
		case ErrorCode::ERROR_STREAM_FILTER_BUFFERSINK_GET_FRAME_FLAGE :
			// buffersink_get_frame_flags
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_FILTER_BUFFERSINK_GET_FRAME_FLAGE)");
		case ErrorCode::ERROR_STREAM_FILTER_APPLY :
			// Failed to apply watermark filter
			return _T ("스트리밍 중 오류가 발생했습니다.\n(error code : ERROR_STREAM_FILTER_APPLY)");
	}
	return _T ("알수 없는 에러가 발생했습니다.\n(error code : ERROR_KNOWN)");
}

#if defined (SET_FFMPEG_USE_FILTER)
int CRTMP::setFilter (AVFormatContext *ifmt_ctx, bool use_watermark, bool use_mirror)
{
	char _args[512];
	int ret;
	AVFilterInOut *_filter_output, *_filter_input;

	if (!(_filter_output = avfilter_inout_alloc ())) {
		//printf("Cannot alloc output\n");
		return -1;
	}

	if (!(_filter_input = avfilter_inout_alloc())) {
		//printf("Cannot alloc input\n");
		return -1;
	}

	if (m_FilterGraph)
		avfilter_graph_free (&m_FilterGraph);

	if (!(m_FilterGraph = avfilter_graph_alloc ())) {
		//printf("Cannot create filter graph\n");
		return -1;
	}

#pragma warning (push)
#pragma warning (disable : 4996) // warning C4996: 'AVStream::codec': deprecated
	snprintf (_args, sizeof (_args), "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d", ifmt_ctx->streams[0]->codec->width, ifmt_ctx->streams[0]->codec->height, ifmt_ctx->streams[0]->codec->pix_fmt, ifmt_ctx->streams[0]->time_base.num, ifmt_ctx->streams[0]->time_base.den, ifmt_ctx->streams[0]->codec->sample_aspect_ratio.num, ifmt_ctx->streams[0]->codec->sample_aspect_ratio.den);
#pragma warning (pop)

	if ((ret = avfilter_graph_create_filter (&m_FilterContext_BufferSrc, m_Filter_BufferSrc, "in", _args, nullptr, m_FilterGraph)) < 0) {
		logAVError (ret);
		//printf("Cannot create buffer source\n");
		return ret;
	}

	if ((ret = avfilter_graph_create_filter (&m_FilterContext_BufferSink, m_Filter_BufferSink, "out", nullptr, nullptr, m_FilterGraph)) < 0) {
		logAVError (ret);
		//printf("Cannot create buffer sink\n");
		return ret;
	}

	_filter_output->name = av_strdup ("in");
	_filter_output->filter_ctx = m_FilterContext_BufferSrc;
	_filter_output->pad_idx = 0;
	_filter_output->next = nullptr;

	_filter_input->name = av_strdup ("out");
	_filter_input->filter_ctx = m_FilterContext_BufferSink;
	_filter_input->pad_idx = 0;
	_filter_input->next = nullptr;


	//const char *_filter = "movie=watermark.png [wm]; [in][wm] overlay=10:10 [out]"; // top left
	//const char *_filter = "movie=watermark.png [wm]; [in][wm] overlay=main_w-overlay_w-10:10 [out]"; // top right
	//const char *_filter = "movie=watermark.png [wm]; [in][wm] overlay=10:main_h-overlay_h-10 [out]"; // bottom left
	//const char *_filter = "movie=watermark.png [wm]; [in][wm] overlay=main_w-overlay_w-10:main_h-overlay_h-10 [out]"; // bottom right
	//const char *_filter = "movie=watermark.png [wm]; [in][wm] overlay=main_w/2-overlay_w/2:main_h/2-overlay_h/2  [out]"; // center ?
	std::string _filter = "null";
	if (use_watermark && use_mirror) 
		_filter = "[in] hflip [flip]; movie=\\'" + m_PathWaterMark + "\\' [wm]; [flip][wm] overlay=main_w-overlay_w-10:10 [out]";
		//_filter = "[in] edgedetect [edge]; [edge] hflip [flip]; movie=\\'" + m_PathWaterMark + "\\' [wm]; [flip][wm] overlay=main_w-overlay_w-10:10 [out]";
		//_filter = "[in] curves=vintage [vintage]; [vintage] hflip [flip]; movie=\\'" + m_PathWaterMark + "\\' [wm]; [flip][wm] overlay=main_w-overlay_w-10:10 [out]";
	else if (use_watermark && !use_mirror)
		_filter = "movie=\\'" + m_PathWaterMark + "\\' [wm]; [in][wm] overlay=main_w-overlay_w-10:10 [out]";
	else if (!use_watermark && use_mirror)
		_filter = "hflip";

	if ((ret = avfilter_graph_parse_ptr (m_FilterGraph, _filter.c_str (), &_filter_input, &_filter_output, NULL)) < 0) {
		logAVError (ret);
		return ret;
	}

	if ((ret = avfilter_graph_config (m_FilterGraph, NULL)) < 0) {
		logAVError (ret);
		return ret;
	}

	avfilter_inout_free (&_filter_input);
	avfilter_inout_free (&_filter_output);

	return 0;
}
#endif

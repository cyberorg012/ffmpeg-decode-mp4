#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>

#define log_inf(fmt, ...) printf("\033[1;37m%s %04u " fmt "\033[m", __FILE__, __LINE__, ##__VA_ARGS__)
#define log_imp(fmt, ...) printf("\033[0;32;32m%s %04u " fmt "\033[m", __FILE__, __LINE__, ##__VA_ARGS__)
#define log_wrn(fmt, ...) printf("\033[0;35m%s %04u " fmt "\033[m", __FILE__, __LINE__, ##__VA_ARGS__)
#define log_err(fmt, ...) printf("\033[0;32;31m%s %04u " fmt "\033[m", __FILE__, __LINE__, ##__VA_ARGS__)

enum CHECK_TYPE {
	check_bool = 0, check_pointer, check_eq_zero, check_gr_zero, check_gr_eq_zero, check_le_zero, check_le_eq_zero,
};

#define glk_check(x, t) \
	switch (t){\
	case check_bool:\
		if (!x) {\
			log_err("%s failed\n", #x); \
			goto end;\
		}\
		break;\
	case check_pointer:\
		if (!x) {\
			log_err("%s failed\n", #x); \
			goto end;\
		}\
		break;\
	case check_eq_zero:\
		if (x != 0) {\
			log_err("%s failed\n", #x); \
			goto end;\
		}\
		break;\
	case check_gr_zero:\
		if (x <= 0) {\
			log_err("%s failed\n", #x); \
			goto end;\
		}\
		break;\
	case check_gr_eq_zero:\
		if (x < 0) {\
			log_err("%s failed\n", #x); \
			goto end;\
		}\
		break;\
	case check_le_zero:\
		if (x >= 0) {\
			log_err("%s failed\n", #x); \
			goto end;\
		}\
		break;\
	case check_le_eq_zero:\
		if (x > 0) {\
			log_err("%s failed\n", #x); \
			goto end;\
		}\
		break;\
	}\
\

static AVFormatContext * format_cnt_init(char * file) {
	AVFormatContext * fmt_cnt = NULL;

	glk_check(file, check_pointer);

	av_register_all();
	glk_check(avformat_open_input(&fmt_cnt, file, NULL, NULL), check_eq_zero);
	glk_check(avformat_find_stream_info(fmt_cnt, NULL), check_gr_eq_zero);
	return fmt_cnt;
end: ;
	if (fmt_cnt) {
		avformat_close_input(&fmt_cnt);
	}
	return NULL;
}


static AVCodecContext * decoder_init(AVFormatContext * fmt_cnt, enum AVMediaType type, int *stream_num){
	AVCodec * decoder = NULL;
	AVCodecContext * decoder_cnt = NULL;

	glk_check(fmt_cnt, check_pointer);
	*stream_num = av_find_best_stream(fmt_cnt, type, -1, -1, NULL, 0);
	glk_check(*stream_num, check_gr_eq_zero);

	decoder_cnt = fmt_cnt->streams[*stream_num]->codec;
	decoder = avcodec_find_decoder(decoder_cnt->codec_id);
	glk_check(decoder, check_pointer);
	glk_check(avcodec_open2(decoder_cnt, decoder, NULL), check_eq_zero);

	return decoder_cnt;
end:;
	if (decoder_cnt){
		avcodec_close(decoder_cnt);
	}
	return NULL;
}


static int decode_file(char * file) {
	int ret = -1;
	AVFormatContext * fmt_cnt = NULL;
	AVCodecContext * video_codec_cnt = NULL, *audio_codec_cnt = NULL;
	AVPacket * pkt = NULL;
	AVFrame * frame = NULL;
	uint8_t *pointers[4];
	int linesizes[4];
	int image_size = 0, w = 0, h = 0;
	enum AVPixelFormat pix_fmt;
	FILE * video_fp = NULL, *audio_fp = NULL;
	char *video_name = "test.h264";
	char *audio_name = "test.pcm";
	int video_nb = 0, audio_nb = 0;

	remove(video_name);
	video_fp = fopen(video_name, "w+");
	glk_check(video_fp, check_pointer);

	remove(audio_name);
	audio_fp = fopen(audio_name, "w+");

	av_register_all();

	fmt_cnt = format_cnt_init(file);
	glk_check(fmt_cnt, check_pointer);

	video_codec_cnt = decoder_init(fmt_cnt, AVMEDIA_TYPE_VIDEO, &video_nb);
	glk_check(video_codec_cnt, check_pointer);

	audio_codec_cnt = decoder_init(fmt_cnt, AVMEDIA_TYPE_AUDIO, &audio_nb);
	glk_check(audio_codec_cnt, check_pointer);

	av_dump_format(fmt_cnt, 0, file, 0);

	pkt = av_packet_alloc();
	av_init_packet(pkt);
	pkt->data = NULL;
	pkt->size = 0;

	frame = av_frame_alloc();
	glk_check(frame, check_pointer);

	w = video_codec_cnt->width;
	h = video_codec_cnt->height;
	pix_fmt = video_codec_cnt->pix_fmt;
	image_size = av_image_alloc(pointers, linesizes, w, h, pix_fmt, 1);

	while (av_read_frame(fmt_cnt, pkt) == 0) {
		int got_frame = 0;
		int decode_size = 0;

		if (video_nb == pkt->stream_index) {
			do {
				decode_size = avcodec_decode_video2(video_codec_cnt, frame, &got_frame, pkt);
				if (decode_size < 0) {
					break;
				}

				if (got_frame) {
					av_image_copy(pointers, linesizes, (const uint8_t **) (frame->data), frame->linesize, pix_fmt, w, h);
					fwrite(pointers[0], image_size, 1, video_fp);
				}

				pkt->data += decode_size;
				pkt->size -= decode_size;
			} while (pkt->size > 0);
		} else if (audio_nb == pkt->stream_index) {
			do {
				decode_size = avcodec_decode_audio4(audio_codec_cnt, frame, &got_frame, pkt);
				if (decode_size < 0) {
					break;
				}

				decode_size = FFMIN(decode_size, pkt->size);
				if (got_frame) {
					size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
					fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_fp);
				}

				pkt->data += decode_size;
				pkt->size -= decode_size;
			} while (pkt->size > 0);
		}

		av_packet_unref(pkt);
	}

	log_imp("Play the output video file with the command:\n" "ffplay -f rawvideo -pix_fmt %s -video_size %dx%d %s\n", av_get_pix_fmt_name(pix_fmt), w, h, video_name);
	ret = 0;
end: ;
	if (video_fp) {
		fclose(video_fp);
	}

	if (video_codec_cnt) {
		avcodec_close(video_codec_cnt);
	}

	if (pkt) {
		av_packet_free(&pkt);
	}

	if (frame) {
		av_frame_free(&frame);
	}

	if (&pointers[0]) {
		av_freep(&pointers[0]);
	}

	if (fmt_cnt) {
		avformat_close_input(&fmt_cnt);
	}

	return ret;
}

int main(int argc, char **argv) {
	decode_file(argv[1]);
	return 0;
}

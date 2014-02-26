/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#           Flemming Frandsen <dren.dk@gmail.com>                               #
#                             Add VU meter OSD                                  #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

/*******************************************************************************#
#                                                                               #
#  Encoder library                                                                #
#                                                                               #
********************************************************************************/

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <assert.h>
/* support for internationalization - i18n */
#include <locale.h>
#include <libintl.h>

#include "../config.h"
#include "gviewencoder.h"
#include "encoder.h"
#include "stream_io.h"
#include "gview.h"

int verbosity = 0;

static int valid_video_codecs = 0;
static int valid_audio_codecs = 0;

static int64_t last_video_pts = 0;
static int64_t last_audio_pts = 0;

/*
 * set verbosity
 * args:
 *   value - verbosity value
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void encoder_set_verbosity(int value)
{
	verbosity = value;
}

/*
 * video encoder initialization
 * args:
 *   video_codec_ind - video codec list index
 *   video_width - video frame width
 *   video_height - video frame height
 *   fps_num - fps numerator
 *   fps_den - fps denominator
 *
 * asserts:
 *   none
 *
 * returns: pointer to encoder video context (NULL on none)
 */
static encoder_video_context_t *encoder_video_init(
	int video_codec_ind,
	int video_width,
	int video_height,
	int fps_num,
	int fps_den)
{
	if(video_codec_ind < 0)
	{
		if(verbosity > 0)
			printf("ENCODER: no video codec set\n");

		return NULL;
	}

	video_codec_t *video_defaults = encoder_get_video_codec_defaults(video_codec_ind);

	if(!video_defaults)
	{
		fprintf(stderr, "ENCODER: defaults for video codec index %i not found\n", video_codec_ind);
		return(NULL);
	}

	encoder_video_context_t *enc_video_ctx = calloc(1, sizeof(encoder_video_context_t));

	/*
	 * find the video encoder
	 *   try specific codec (by name)
	 */
	enc_video_ctx->codec = avcodec_find_encoder_by_name(video_defaults->codec_name);
	/*if it fails try any codec with matching AV_CODEC_ID*/
	if(!enc_video_ctx->codec)
		enc_video_ctx->codec = avcodec_find_encoder(video_defaults->codec_id);

	if(!enc_video_ctx->codec)
	{
		fprintf(stderr, "ENCODER: libav video codec (%i) not found\n",video_defaults->codec_id);
		free(enc_video_ctx);
		return(NULL);
	}

#if LIBAVCODEC_VER_AT_LEAST(53,6)

	enc_video_ctx->codec_context = avcodec_alloc_context3(enc_video_ctx->codec);

	avcodec_get_context_defaults3 (
			enc_video_ctx->codec_context,
			enc_video_ctx->codec);
#else
	enc_video_ctx->codec_context = avcodec_alloc_context();
#endif

	if(!enc_video_ctx->codec_context)
	{
		fprintf(stderr, "ENCODER: couldn't allocate video codec context\n");
		free(enc_video_ctx);
		return(NULL);
	}

	/*set codec defaults*/
	enc_video_ctx->codec_context->bit_rate = video_defaults->bit_rate;
	enc_video_ctx->codec_context->width = video_width;
	enc_video_ctx->codec_context->height = video_height;

	enc_video_ctx->codec_context->flags |= video_defaults->flags;
	if (video_defaults->num_threads > 0)
		enc_video_ctx->codec_context->thread_count = video_defaults->num_threads;
	/*
	 * mb_decision:
	 * 0 (FF_MB_DECISION_SIMPLE) Use mbcmp (default).
	 * 1 (FF_MB_DECISION_BITS)   Select the MB mode which needs the fewest bits (=vhq).
	 * 2 (FF_MB_DECISION_RD)     Select the MB mode which has the best rate distortion.
	 */
	enc_video_ctx->codec_context->mb_decision = video_defaults->mb_decision;
	/*use trellis quantization*/
	enc_video_ctx->codec_context->trellis = video_defaults->trellis;

	/*motion estimation method (epzs)*/
	enc_video_ctx->codec_context->me_method = video_defaults->me_method;

	enc_video_ctx->codec_context->dia_size = video_defaults->dia;
	enc_video_ctx->codec_context->pre_dia_size = video_defaults->pre_dia;
	enc_video_ctx->codec_context->pre_me = video_defaults->pre_me;
	enc_video_ctx->codec_context->me_pre_cmp = video_defaults->me_pre_cmp;
	enc_video_ctx->codec_context->me_cmp = video_defaults->me_cmp;
	enc_video_ctx->codec_context->me_sub_cmp = video_defaults->me_sub_cmp;
	enc_video_ctx->codec_context->me_subpel_quality = video_defaults->subq; //NEW
	enc_video_ctx->codec_context->refs = video_defaults->framerefs;         //NEW
	enc_video_ctx->codec_context->last_predictor_count = video_defaults->last_pred;

	enc_video_ctx->codec_context->mpeg_quant = video_defaults->mpeg_quant; //h.263
	enc_video_ctx->codec_context->qmin = video_defaults->qmin;             // best detail allowed - worst compression
	enc_video_ctx->codec_context->qmax = video_defaults->qmax;             // worst detail allowed - best compression
	enc_video_ctx->codec_context->max_qdiff = video_defaults->max_qdiff;
	enc_video_ctx->codec_context->max_b_frames = video_defaults->max_b_frames;

	enc_video_ctx->codec_context->qcompress = video_defaults->qcompress;
	enc_video_ctx->codec_context->qblur = video_defaults->qblur;
	enc_video_ctx->codec_context->strict_std_compliance = FF_COMPLIANCE_NORMAL;
	enc_video_ctx->codec_context->codec_id = video_defaults->codec_id;

#if !LIBAVCODEC_VER_AT_LEAST(53,0)
#define AVMEDIA_TYPE_VIDEO CODEC_TYPE_VIDEO
#endif
	enc_video_ctx->codec_context->codec_type = AVMEDIA_TYPE_VIDEO;

	enc_video_ctx->codec_context->pix_fmt = PIX_FMT_YUV420P; //only yuv420p available for mpeg
	if(video_defaults->fps)
		enc_video_ctx->codec_context->time_base = (AVRational){1, video_defaults->fps}; //use codec properties fps
	else if (fps_den >= 5)
		enc_video_ctx->codec_context->time_base = (AVRational){fps_num, fps_den}; //default fps (for gspca this is 1/1)
	else
		enc_video_ctx->codec_context->time_base = (AVRational){1,15}; //fallback to 15 fps (e.g gspca)

    if(video_defaults->gop_size > 0)
        enc_video_ctx->codec_context->gop_size = video_defaults->gop_size;
    else
        enc_video_ctx->codec_context->gop_size = enc_video_ctx->codec_context->time_base.den;

	if(video_defaults->codec_id == AV_CODEC_ID_H264)
	{
	   enc_video_ctx->codec_context->me_range = 16;
	    //the first compressed frame will be empty (1 frame out of sync)
	    //but avoids x264 warning on lookaheadless mb-tree
#if LIBAVCODEC_VER_AT_LEAST(53,6)
	    av_dict_set(&enc_video_ctx->private_options, "rc_lookahead", "1", 0);
#else
	    enc_video_ctx->codec_context->rc_lookahead=1;
#endif
	}

	/* open codec*/
#if LIBAVCODEC_VER_AT_LEAST(53,6)
	if (avcodec_open2(
		enc_video_ctx->codec_context,
		enc_video_ctx->codec,
		&enc_video_ctx->private_options) < 0)
#else
	if (avcodec_open(
		enc_video_ctx->codec_context,
		enc_video_ctx->codec) < 0)
#endif
	{
		fprintf(stderr, "ENCODER: could not open video codec (%s)\n", video_defaults->codec_name);
		free(enc_video_ctx->codec_context);
		free(enc_video_ctx);
		return(NULL);
	}

	enc_video_ctx->picture= avcodec_alloc_frame();
	enc_video_ctx->picture->pts = 0;

	//alloc tmpbuff (yuv420p)
	enc_video_ctx->tmpbuf = calloc((video_width * video_height * 3)/2, sizeof(uint8_t));
	//alloc outbuf
	enc_video_ctx->outbuf_size = 240000;//1792
	enc_video_ctx->outbuf = calloc(enc_video_ctx->outbuf_size, sizeof(uint8_t));

	enc_video_ctx->delayed_frames = 0;
	enc_video_ctx->index_of_df = -1;

	enc_video_ctx->flushed_buffers = 0;
	enc_video_ctx->flush_delayed_frames = 0;
	enc_video_ctx->flush_done = 0;

	return (enc_video_ctx);
}

/*
 * audio encoder initialization
 * args:
 *   audio_codec_ind - audio codec list index
 *   audio_channels - audio channels
 *   audio_samprate - audio sample rate
 *
 * asserts:
 *   none
 *
 * returns: pointer to encoder audio context (NULL on none)
 */
static encoder_audio_context_t *encoder_audio_init(
	int audio_codec_ind,
	int audio_channels,
	int audio_samprate)
{

	if(audio_codec_ind < 0)
	{
		if(verbosity > 0)
			printf("ENCODER: no audio codec set\n");

		return NULL;
	}

	audio_codec_t *audio_defaults = encoder_get_audio_codec_defaults(audio_codec_ind);

	if(!audio_defaults)
	{
		fprintf(stderr, "ENCODER: defaults for audio codec index %i not found\n", audio_codec_ind);
		return NULL;
	}

	encoder_audio_context_t *enc_audio_ctx = calloc(1, sizeof(encoder_audio_context_t));
	/*
	 * find the audio encoder
	 *   try specific codec (by name)
	 */
	enc_audio_ctx->codec = avcodec_find_encoder_by_name(audio_defaults->codec_name);
	/*if it fails try any codec with matching AV_CODEC_ID*/
	if(!enc_audio_ctx->codec)
		enc_audio_ctx->codec = avcodec_find_encoder(audio_defaults->codec_id);

	if(!enc_audio_ctx->codec)
	{
		fprintf(stderr, "ENCODER: audio codec (%i) not found\n",audio_defaults->codec_id);
		free(enc_audio_ctx);
		return NULL;
	}

#if LIBAVCODEC_VER_AT_LEAST(53,6)
	enc_audio_ctx->codec_context = avcodec_alloc_context3(enc_audio_ctx->codec);
	avcodec_get_context_defaults3 (enc_audio_ctx->codec_context, enc_audio_ctx->codec);
#else
	enc_audio_ctx->codec_context = avcodec_alloc_context();
#endif

	if(!enc_audio_ctx->codec_context)
	{
		fprintf(stderr, "ENCODER: couldn't allocate audio codec context\n");
		free(enc_audio_ctx);
		return(NULL);
	}

	/*defaults*/
	enc_audio_ctx->codec_context->bit_rate = audio_defaults->bit_rate;
	enc_audio_ctx->codec_context->profile = audio_defaults->profile; /*for AAC*/

	enc_audio_ctx->codec_context->flags |= audio_defaults->flags;

	enc_audio_ctx->codec_context->sample_rate = audio_samprate;
	enc_audio_ctx->codec_context->channels = audio_channels;

#ifdef AV_CH_LAYOUT_MONO
	if(audio_channels < 2)
		enc_audio_ctx->codec_context->channel_layout = AV_CH_LAYOUT_MONO;
	else
		enc_audio_ctx->codec_context->channel_layout = AV_CH_LAYOUT_STEREO;
#endif

	enc_audio_ctx->codec_context->cutoff = 0; /*automatic*/

	enc_audio_ctx->codec_context->sample_fmt = audio_defaults->sample_format;

    enc_audio_ctx->codec_context->codec_id = audio_defaults->codec_id;

#if !LIBAVCODEC_VER_AT_LEAST(53,0)
#define AVMEDIA_TYPE_AUDIO CODEC_TYPE_AUDIO
#endif
	enc_audio_ctx->codec_context->codec_type = AVMEDIA_TYPE_AUDIO;

	/* open codec*/
#if LIBAVCODEC_VER_AT_LEAST(53,6)
	if (avcodec_open2(
		enc_audio_ctx->codec_context,
		enc_audio_ctx->codec, NULL) < 0)
#else
	if (avcodec_open(
		enc_audio_ctx->codec_context,
		enc_audio_ctx->codec) < 0)
#endif
	{
		switch(audio_defaults->sample_format)
		{
			case AV_SAMPLE_FMT_S16:
				audio_defaults->sample_format = AV_SAMPLE_FMT_FLT;
				enc_audio_ctx->codec_context->sample_fmt = audio_defaults->sample_format;
				fprintf(stderr, "ENCODER: could not open audio codec...trying again with float sample format\n");
				break;
			case AV_SAMPLE_FMT_FLT:
				audio_defaults->sample_format = AV_SAMPLE_FMT_S16;
				enc_audio_ctx->codec_context->sample_fmt = audio_defaults->sample_format;
				fprintf(stderr, "ENCODER: could not open audio codec...trying again with int16 sample format\n");
				break;
		}
		#if LIBAVCODEC_VER_AT_LEAST(53,6)
		if (avcodec_open2(
			enc_audio_ctx->codec_context,
			enc_audio_ctx->codec, NULL) < 0)
		#else
		if (avcodec_open(
			enc_audio_ctx->codec_context,
			enc_audio_ctx->codec) < 0)
		#endif
		{
			fprintf(stderr, "ENCODER: could not open audio codec...giving up\n");
			free(enc_audio_ctx->codec_context);
			free(enc_audio_ctx);
			return NULL;
		}
	}

	/* the codec gives us the frame size, in samples */
	int frame_size = enc_audio_ctx->codec_context->frame_size;
	if(verbosity > 0)
		printf("ENCODER: Audio frame size is %d samples for selected codec\n", frame_size);

	enc_audio_ctx->monotonic_pts = audio_defaults->monotonic_pts;

	/*alloc outbuf*/
	enc_audio_ctx->outbuf_size = 240000;
	enc_audio_ctx->outbuf = calloc(enc_audio_ctx->outbuf_size, sizeof(uint8_t));

#if LIBAVCODEC_VER_AT_LEAST(53,34)
	enc_audio_ctx->frame= avcodec_alloc_frame();
	avcodec_get_frame_defaults(enc_audio_ctx->frame);
#endif

	return (enc_audio_ctx);
}

/*
 * encoder initaliztion
 * args:
 *    none
 *
 * asserts:
 *    none
 *
 * returns: none
 */
void encoder_init()
{

#if !LIBAVCODEC_VER_AT_LEAST(53,34)
	avcodec_init();
#endif
	/* register all the codecs (you can also register only the codec
	 * you wish to have smaller code)
	 */
	avcodec_register_all();

	valid_video_codecs = encoder_set_valid_video_codec_list ();
	valid_audio_codecs = encoder_set_valid_audio_codec_list ();

}

/*
 * get valid video codec count
 * args:
 *   none
 *
 * asserts:
 *    none
 *
 * returns: number of valid video codecs
 */
int encoder_get_valid_video_codecs()
{
	return valid_video_codecs;
}

/*
 * get valid audio codec count
 * args:
 *   none
 *
 * asserts:
 *    none
 *
 * returns: number of valid audio codecs
 */
int encoder_get_valid_audio_codecs()
{
	return valid_audio_codecs;
}

/*
 * initialize and get the encoder context
 * args:
 *   input_format - input v4l2 format (yuyv for encoding)
 *   video_codec_ind - video codec list index
 *   audio_codec_ind - audio codec list index
 *   muxer_id - file muxer:
 *        ENCODER_MUX_MKV; ENCODER_MUX_WEBM; ENCODER_MUX_AVI
 *   video_width - video frame width
 *   video_height - video frame height
 *   fps_num - fps numerator
 *   fps_den - fps denominator
 *   audio_channels- audio channels
 *   audio_samprate- audio sample rate
 *
 * asserts:
 *   none
 *
 * returns: pointer to encoder context (NULL on error)
 */
encoder_context_t *encoder_get_context(
	int input_format,
	int video_codec_ind,
	int audio_codec_ind,
	int muxer_id,
	int video_width,
	int video_height,
	int fps_num,
	int fps_den,
	int audio_channels,
	int audio_samprate)
{
	encoder_context_t *encoder_ctx = calloc(1, sizeof(encoder_context_t));

	if(!encoder_ctx)
	{
		fprintf(stderr, "ENCODER: couldn't allocate encoder context\n");
		return NULL;
	}

	encoder_ctx->input_format = input_format;

	encoder_ctx->muxer_id = muxer_id;

	encoder_ctx->video_width = video_width;
	encoder_ctx->video_height = video_height;

	encoder_ctx->fps_num = fps_num;
	encoder_ctx->fps_den = fps_den;

	encoder_ctx->audio_channels = audio_channels;
	encoder_ctx->audio_samprate = audio_samprate;

	/******************* video **********************/
	encoder_ctx->enc_video_ctx = encoder_video_init(
		video_codec_ind,
		video_width,
		video_height,
		fps_num,
		fps_den);

	/******************* audio **********************/
	encoder_ctx->enc_audio_ctx = encoder_audio_init(
		audio_codec_ind,
		audio_channels,
		audio_samprate);

	return encoder_ctx;
}

/*
 * encode video frame
 * args:
 *   encoder_ctx - pointer to encoder context
 *   yuv_frame - yuyv input frame
 *
 * asserts:
 *   encoder_ctx is not null
 *
 * returns: encoded buffer size
 */
int encoder_encode_video(encoder_context_t *encoder_ctx, void *yuv_frame)
{
	/*assertions*/
	assert(encoder_ctx != NULL);

	encoder_video_context_t *enc_video_ctx = encoder_ctx->enc_video_ctx;

	if(!enc_video_ctx)
	{
		if(verbosity > 1)
			printf("ENCODER: video encoder not set\n");
		return 0;
	}

	int outsize = 0 ;

	yuv422to420p(encoder_ctx, yuv_frame);

	if(!enc_video_ctx->monotonic_pts) //generate a real pts based on the frame timestamp
		enc_video_ctx->picture->pts += ((enc_video_ctx->pts - last_video_pts)/1000) * 90;
	else  /*generate a true monotonic pts based on the codec fps*/
		enc_video_ctx->picture->pts +=
			(enc_video_ctx->codec_context->time_base.num * 1000 / enc_video_ctx->codec_context->time_base.den) * 90;

	if(enc_video_ctx->flush_delayed_frames)
	{
		//pkt.size = 0;
		if(!enc_video_ctx->flushed_buffers)
		{
			avcodec_flush_buffers(enc_video_ctx->codec_context);
			enc_video_ctx->flushed_buffers = 1;
		}
 	}
#if LIBAVCODEC_VER_AT_LEAST(54,01)
	AVPacket pkt;
    int got_packet = 0;
    av_init_packet(&pkt);
	pkt.data = enc_video_ctx->outbuf;
	pkt.size = enc_video_ctx->outbuf_size;

    //if(enc_video_ctx->outbuf_size < FF_MIN_BUFFER_SIZE)
    //{
	//	av_log(avctx, AV_LOG_ERROR, "buffer smaller than minimum size\n");
    //    return -1;
    //}
 	int ret = 0;
 	if(!enc_video_ctx->flush_delayed_frames)
    	ret = avcodec_encode_video2(
			enc_video_ctx->codec_context,
			&pkt,
			enc_video_ctx->picture,
			&got_packet);
   	else
   		ret = avcodec_encode_video2(
			enc_video_ctx->codec_context,
			&pkt, NULL, /*NULL flushes the encoder buffers*/
			&got_packet);

    if (!ret && got_packet && enc_video_ctx->codec_context->coded_frame)
    {
		/* Do we really need to set this ???*/
    	enc_video_ctx->codec_context->coded_frame->pts = pkt.pts;
        enc_video_ctx->codec_context->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }

	enc_video_ctx->dts = pkt.dts;
	enc_video_ctx->flags = pkt.flags;
	enc_video_ctx->duration = pkt.duration;

    /* free any side data since we cannot return it */
    if (pkt.side_data_elems > 0)
    {
    	int i;
        for (i = 0; i < pkt.side_data_elems; i++)
        	av_free(pkt.side_data[i].data);
        av_freep(&pkt.side_data);
        pkt.side_data_elems = 0;
    }

    out_size = pkt.size;
#else
	if(!enc_video_ctx->flush_delayed_frames)
		outsize = avcodec_encode_video(
			enc_video_ctx->codec_context,
			enc_video_ctx->outbuf,
			enc_video_ctx->outbuf_size,
			enc_video_ctx->picture);
	else
		outsize = avcodec_encode_video(
			enc_video_ctx->codec_context,
			enc_video_ctx->outbuf,
			enc_video_ctx->outbuf_size,
			NULL); /*NULL flushes the encoder buffers*/

	enc_video_ctx->flags = 0;
	if (enc_video_ctx->codec_context->coded_frame->key_frame)
		enc_video_ctx->flags |= AV_PKT_FLAG_KEY;
	enc_video_ctx->dts = AV_NOPTS_VALUE;
	enc_video_ctx->duration = enc_video_ctx->pts - last_video_pts;
#endif

	last_video_pts = enc_video_ctx->pts;

	if(enc_video_ctx->flush_delayed_frames && outsize == 0)
    	enc_video_ctx->flush_done = 1;

	if(outsize == 0 && enc_video_ctx->index_of_df < 0)
	{
	    enc_video_ctx->delayed_pts[enc_video_ctx->delayed_frames] = enc_video_ctx->pts;
	    enc_video_ctx->delayed_frames++;
	    if(enc_video_ctx->delayed_frames > MAX_DELAYED_FRAMES)
	    {
	    	enc_video_ctx->delayed_frames = MAX_DELAYED_FRAMES;
	    	printf("ENCODER: Maximum of %i delayed video frames reached...\n", MAX_DELAYED_FRAMES);
	    }
	}
	else
	{
		if(enc_video_ctx->delayed_frames > 0)
		{
			if(enc_video_ctx->index_of_df < 0)
			{
				enc_video_ctx->index_of_df = 0;
				printf("ENCODER: video codec is using %i delayed video frames\n", enc_video_ctx->delayed_frames);
			}
			int64_t my_pts = enc_video_ctx->pts;
			enc_video_ctx->pts = enc_video_ctx->delayed_pts[enc_video_ctx->index_of_df];
			enc_video_ctx->delayed_pts[enc_video_ctx->index_of_df] = my_pts;
			enc_video_ctx->index_of_df++;
			if(enc_video_ctx->index_of_df >= enc_video_ctx->delayed_frames)
				enc_video_ctx->index_of_df = 0;
		}
	}
	return (outsize);
}

/*
 * encode audio
 * args:
 *   encoder_ctx - pointer to encoder context
 *   pcm - pointer to audio pcm data
 *
 * asserts:
 *   encoder_ctx is not null
 *
 * returns: encoded buffer size
 */
int encoder_encode_audio(encoder_context_t *encoder_ctx, void *pcm)
{
	/*assertions*/
	assert(encoder_ctx != NULL);

	encoder_audio_context_t *enc_audio_ctx = encoder_ctx->enc_audio_ctx;

	if(!enc_audio_ctx)
	{
		if(verbosity > 1)
			printf("ENCODER: audio encoder not set\n");
		return 0;
	}

	int out_size = 0;
	int ret = 0;

	/* encode the audio */
#if LIBAVCODEC_VER_AT_LEAST(53,34)
	AVPacket pkt;
	int got_packet;
	av_init_packet(&pkt);
	pkt.data = enc_audio_ctx->outbuf;
	pkt.size = enc_audio_ctx->outbuf_size;

	enc_audio_ctx->frame->nb_samples  = enc_audio_ctx->codec_context->frame_size;
	int samples_size = av_samples_get_buffer_size(
		NULL,
		enc_audio_ctx->codec_context->channels,
		enc_audio_ctx->frame->nb_samples,
		enc_audio_ctx->codec_context->sample_fmt,
		1);

    avcodec_fill_audio_frame(
		enc_audio_ctx->frame,
		enc_audio_ctx->codec_context->channels,
		enc_audio_ctx->codec_context->sample_fmt,
		(const uint8_t *) pcm, samples_size,
		1);

	if(!enc_audio_ctx->monotonic_pts) /*generate a real pts based on the frame timestamp*/
		enc_audio_ctx->frame->pts += ((enc_audio_ctx->pts - last_audio_pts)/1000) * 90;
	else  /*generate a true monotonic pts based on the codec fps*/
		enc_audio_ctx->frame->pts +=
			(enc_audio_ctx->codec_context->time_base.num*1000/enc_audio_ctx->codec_context->time_base.den) * 90;

	ret = avcodec_encode_audio2(
			enc_audio_ctx->codec_context,
			&pkt,
			enc_audio_ctx->frame,
			&got_packet);

	if (!ret && got_packet && enc_audio_ctx->codec_context->coded_frame)
    {
    	enc_audio_ctx->codec_context->coded_frame->pts = pkt.pts;
        enc_audio_ctx->codec_context->coded_frame->key_frame = !!(pkt.flags & AV_PKT_FLAG_KEY);
    }

	enc_audio_ctx->dts = pkt.dts;
	enc_audio_ctx->flags = pkt.flags;
	enc_audio_ctx->duration = pkt.duration;

	/* free any side data since we cannot return it */
	//ff_packet_free_side_data(&pkt);
	if (enc_audio_ctx->frame &&
		enc_audio_ctx->frame->extended_data != enc_audio_ctx->frame->data)
		av_freep(enc_audio_ctx->frame->extended_data);

	out_size = pkt.size;
#else
	out_size = avcodec_encode_audio(
			enc_audio_ctx->codec_context,
			enc_audio_ctx->outbuf,
			enc_audio_ctx->outbuf_size,
			pcm);

	enc_audio_ctx->dts = AV_NOPTS_VALUE;
	enc_audio_ctx->flags = 0;
	if (enc_audio_ctx->codec_context->coded_frame->key_frame)
		enc_audio_ctx->flags |= AV_PKT_FLAG_KEY;

	enc_audio_ctx->duration = enc_audio_ctx->pts - last_audio_pts;
#endif

	last_audio_pts = enc_audio_ctx->pts;

	return (out_size);
}

/*
 * close and clean encoder context
 * args:
 *   encoder_ctx - pointer to encoder context data
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void encoder_close(encoder_context_t *encoder_ctx)
{
	if(!encoder_ctx);
		return;

	encoder_video_context_t *enc_video_ctx = encoder_ctx->enc_video_ctx;
	encoder_audio_context_t *enc_audio_ctx = encoder_ctx->enc_audio_ctx;

	/*close video codec*/
	if(enc_video_ctx)
	{
		if(enc_video_ctx->priv_data)
			free(enc_video_ctx->priv_data);

		if(!(enc_video_ctx->flushed_buffers))
		{
			avcodec_flush_buffers(enc_video_ctx->codec_context);
			enc_video_ctx->flushed_buffers = 1;
		}

		avcodec_close(enc_video_ctx->codec_context);
		free(enc_video_ctx->codec_context);

#if LIBAVCODEC_VER_AT_LEAST(53,6)
		av_dict_free(&(enc_video_ctx->private_options));
#endif

		if(enc_video_ctx->tmpbuf)
			free(enc_video_ctx->tmpbuf);
		if(enc_video_ctx->outbuf)
			free(enc_video_ctx->outbuf);
		if(enc_video_ctx->picture)
			free(enc_video_ctx->picture);

		free(enc_video_ctx);
	}

	/*close audio codec*/
	if(enc_audio_ctx)
	{
		if(enc_audio_ctx->priv_data != NULL)
			free(enc_audio_ctx->priv_data);

		avcodec_flush_buffers(enc_audio_ctx->codec_context);

		avcodec_close(enc_audio_ctx->codec_context);
		free(enc_audio_ctx->codec_context);

		if(enc_audio_ctx->outbuf)
			free(enc_audio_ctx->outbuf);
		if(enc_audio_ctx->frame)
			free(enc_audio_ctx->frame);

		free(enc_audio_ctx);
	}

	free(encoder_ctx);
}
/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-lamemp3enc
 * @see_also: lame, mad, vorbisenc
 *
 * This element encodes raw integer audio into an MPEG-1 layer 3 (MP3) stream.
 * Note that <ulink url="http://en.wikipedia.org/wiki/MP3">MP3</ulink> is not
 * a free format, there are licensing and patent issues to take into
 * consideration. See <ulink url="http://www.vorbis.com/">Ogg/Vorbis</ulink>
 * for a royalty free (and often higher quality) alternative.
 *
 * <refsect2>
 * <title>Output sample rate</title>
 * If no fixed output sample rate is negotiated on the element's src pad,
 * the element will choose an optimal sample rate to resample to internally.
 * For example, a 16-bit 44.1 KHz mono audio stream encoded at 48 kbit will
 * get resampled to 32 KHz.  Use filter caps on the src pad to force a
 * particular sample rate.
 * </refsect2>
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v audiotestsrc wave=sine num-buffers=100 ! audioconvert ! lamemp3enc ! filesink location=sine.mp3
 * ]| Encode a test sine signal to MP3.
 * |[
 * gst-launch -v alsasrc ! audioconvert ! lamemp3enc target=bitrate bitrate=192 ! filesink location=alsasrc.mp3
 * ]| Record from a sound card using ALSA and encode to MP3 with an average bitrate of 192kbps
 * |[
 * gst-launch -v filesrc location=music.wav ! decodebin ! audioconvert ! audioresample ! lamemp3enc target=quality quality=0 ! id3v2mux ! filesink location=music.mp3
 * ]| Transcode from a .wav file to MP3 (the id3v2mux element is optional) with best VBR quality
 * |[
 * gst-launch -v cdda://5 ! audioconvert ! lamemp3enc target=bitrate cbr=true bitrate=192 ! filesink location=track5.mp3
 * ]| Encode Audio CD track 5 to MP3 with a constant bitrate of 192kbps
 * |[
 * gst-launch -v audiotestsrc num-buffers=10 ! audio/x-raw-int,rate=44100,channels=1 ! lamemp3enc target=bitrate cbr=true bitrate=48 ! filesink location=test.mp3
 * ]| Encode to a fixed sample rate
 * </refsect2>
 *
 * Since: 0.10.12
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "gstlamemp3enc.h"
#include <gst/gst-i18n-plugin.h>

/* lame < 3.98 */
#ifndef HAVE_LAME_SET_VBR_QUALITY
#define lame_set_VBR_quality(flags,q) lame_set_VBR_q((flags),(int)(q))
#endif

GST_DEBUG_CATEGORY_STATIC (debug);
#define GST_CAT_DEFAULT debug

/* elementfactory information */

/* LAMEMP3ENC can do MPEG-1, MPEG-2, and MPEG-2.5, so it has 9 possible
 * sample rates it supports */
static GstStaticPadTemplate gst_lamemp3enc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) " G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean) true, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

static GstStaticPadTemplate gst_lamemp3enc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) 3, "
        "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 }, "
        "channels = (int) [ 1, 2 ]")
    );

/********** Define useful types for non-programmatic interfaces **********/
enum
{
  LAMEMP3ENC_TARGET_QUALITY = 0,
  LAMEMP3ENC_TARGET_BITRATE
};

#define GST_TYPE_LAMEMP3ENC_TARGET (gst_lamemp3enc_target_get_type())
static GType
gst_lamemp3enc_target_get_type (void)
{
  static GType lame_target_type = 0;
  static GEnumValue lame_targets[] = {
    {LAMEMP3ENC_TARGET_QUALITY, "Quality", "quality"},
    {LAMEMP3ENC_TARGET_BITRATE, "Bitrate", "bitrate"},
    {0, NULL, NULL}
  };

  if (!lame_target_type) {
    lame_target_type =
        g_enum_register_static ("GstLameMP3EncTarget", lame_targets);
  }
  return lame_target_type;
}

enum
{
  LAMEMP3ENC_ENCODING_ENGINE_QUALITY_FAST = 0,
  LAMEMP3ENC_ENCODING_ENGINE_QUALITY_STANDARD,
  LAMEMP3ENC_ENCODING_ENGINE_QUALITY_HIGH
};

#define GST_TYPE_LAMEMP3ENC_ENCODING_ENGINE_QUALITY (gst_lamemp3enc_encoding_engine_quality_get_type())
static GType
gst_lamemp3enc_encoding_engine_quality_get_type (void)
{
  static GType lame_encoding_engine_quality_type = 0;
  static GEnumValue lame_encoding_engine_quality[] = {
    {0, "Fast", "fast"},
    {1, "Standard", "standard"},
    {2, "High", "high"},
    {0, NULL, NULL}
  };

  if (!lame_encoding_engine_quality_type) {
    lame_encoding_engine_quality_type =
        g_enum_register_static ("GstLameMP3EncEncodingEngineQuality",
        lame_encoding_engine_quality);
  }
  return lame_encoding_engine_quality_type;
}

/********** Standard stuff for signals and arguments **********/

enum
{
  ARG_0,
  ARG_TARGET,
  ARG_BITRATE,
  ARG_CBR,
  ARG_QUALITY,
  ARG_ENCODING_ENGINE_QUALITY,
  ARG_MONO
};

#define DEFAULT_TARGET LAMEMP3ENC_TARGET_QUALITY
#define DEFAULT_BITRATE 128
#define DEFAULT_CBR FALSE
#define DEFAULT_QUALITY 4
#define DEFAULT_ENCODING_ENGINE_QUALITY LAMEMP3ENC_ENCODING_ENGINE_QUALITY_STANDARD
#define DEFAULT_MONO FALSE

static gboolean gst_lamemp3enc_start (GstAudioEncoder * enc);
static gboolean gst_lamemp3enc_stop (GstAudioEncoder * enc);
static gboolean gst_lamemp3enc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_lamemp3enc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);
static void gst_lamemp3enc_flush (GstAudioEncoder * enc);

static void gst_lamemp3enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_lamemp3enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_lamemp3enc_setup (GstLameMP3Enc * lame, GstTagList ** tags);

GST_BOILERPLATE (GstLameMP3Enc, gst_lamemp3enc, GstAudioEncoder,
    GST_TYPE_AUDIO_ENCODER);

static void
gst_lamemp3enc_release_memory (GstLameMP3Enc * lame)
{
  if (lame->lgf) {
    lame_close (lame->lgf);
    lame->lgf = NULL;
  }
}

static void
gst_lamemp3enc_finalize (GObject * obj)
{
  gst_lamemp3enc_release_memory (GST_LAMEMP3ENC (obj));

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_lamemp3enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_lamemp3enc_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_lamemp3enc_sink_template));
  gst_element_class_set_details_simple (element_class, "L.A.M.E. mp3 encoder",
      "Codec/Encoder/Audio",
      "High-quality free MP3 encoder",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_lamemp3enc_class_init (GstLameMP3EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAudioEncoderClass *base_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  base_class = (GstAudioEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_lamemp3enc_set_property;
  gobject_class->get_property = gst_lamemp3enc_get_property;
  gobject_class->finalize = gst_lamemp3enc_finalize;

  base_class->start = GST_DEBUG_FUNCPTR (gst_lamemp3enc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_lamemp3enc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_lamemp3enc_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_lamemp3enc_handle_frame);
  base_class->flush = GST_DEBUG_FUNCPTR (gst_lamemp3enc_flush);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TARGET,
      g_param_spec_enum ("target", "Target",
          "Optimize for quality or bitrate", GST_TYPE_LAMEMP3ENC_TARGET,
          DEFAULT_TARGET,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate (kb/s)",
          "Bitrate in kbit/sec (Only valid if target is bitrate, for CBR one "
          "of 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, "
          "256 or 320)", 8, 320, DEFAULT_BITRATE,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CBR,
      g_param_spec_boolean ("cbr", "CBR", "Enforce constant bitrate encoding "
          "(Only valid if target is bitrate)", DEFAULT_CBR,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_QUALITY,
      g_param_spec_float ("quality", "Quality",
          "VBR Quality from 0 to 10, 0 being the best "
          "(Only valid if target is quality)", 0.0, 9.999,
          DEFAULT_QUALITY,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      ARG_ENCODING_ENGINE_QUALITY, g_param_spec_enum ("encoding-engine-quality",
          "Encoding Engine Quality", "Quality/speed of the encoding engine, "
          "this does not affect the bitrate!",
          GST_TYPE_LAMEMP3ENC_ENCODING_ENGINE_QUALITY,
          DEFAULT_ENCODING_ENGINE_QUALITY,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MONO,
      g_param_spec_boolean ("mono", "Mono", "Enforce mono encoding",
          DEFAULT_MONO,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_lamemp3enc_init (GstLameMP3Enc * lame, GstLameMP3EncClass * klass)
{
}

static gboolean
gst_lamemp3enc_start (GstAudioEncoder * enc)
{
  GstLameMP3Enc *lame = GST_LAMEMP3ENC (enc);

  GST_DEBUG_OBJECT (lame, "start");
  return TRUE;
}

static gboolean
gst_lamemp3enc_stop (GstAudioEncoder * enc)
{
  GstLameMP3Enc *lame = GST_LAMEMP3ENC (enc);

  GST_DEBUG_OBJECT (lame, "stop");

  gst_lamemp3enc_release_memory (lame);
  return TRUE;
}

static gboolean
gst_lamemp3enc_set_format (GstAudioEncoder * enc, GstAudioInfo * info)
{
  GstLameMP3Enc *lame;
  gint out_samplerate;
  gint version;
  GstCaps *othercaps;
  GstClockTime latency;
  GstTagList *tags = NULL;

  lame = GST_LAMEMP3ENC (enc);

  /* parameters already parsed for us */
  lame->samplerate = GST_AUDIO_INFO_RATE (info);
  lame->num_channels = GST_AUDIO_INFO_CHANNELS (info);

  /* but we might be asked to reconfigure, so reset */
  gst_lamemp3enc_release_memory (lame);

  GST_DEBUG_OBJECT (lame, "setting up lame");
  if (!gst_lamemp3enc_setup (lame, &tags))
    goto setup_failed;

  out_samplerate = lame_get_out_samplerate (lame->lgf);
  if (out_samplerate == 0)
    goto zero_output_rate;
  if (out_samplerate != lame->samplerate) {
    GST_WARNING_OBJECT (lame,
        "output samplerate %d is different from incoming samplerate %d",
        out_samplerate, lame->samplerate);
  }

  version = lame_get_version (lame->lgf);
  if (version == 0)
    version = 2;
  else if (version == 1)
    version = 1;
  else if (version == 2)
    version = 3;

  othercaps =
      gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, 1,
      "mpegaudioversion", G_TYPE_INT, version,
      "layer", G_TYPE_INT, 3,
      "channels", G_TYPE_INT, lame->mono ? 1 : lame->num_channels,
      "rate", G_TYPE_INT, out_samplerate, NULL);

  /* and use these caps */
  gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (enc), othercaps);
  gst_caps_unref (othercaps);

  /* base class feedback:
   * - we will handle buffers, just hand us all available
   * - report latency */
  latency = gst_util_uint64_scale_int (lame_get_framesize (lame->lgf),
      GST_SECOND, lame->samplerate);
  gst_audio_encoder_set_latency (enc, latency, latency);

  if (tags)
    gst_audio_encoder_merge_tags (enc, tags, GST_TAG_MERGE_REPLACE);

  return TRUE;

zero_output_rate:
  {
    if (tags)
      gst_tag_list_free (tags);
    GST_ELEMENT_ERROR (lame, LIBRARY, SETTINGS, (NULL),
        ("LAMEMP3ENC decided on a zero sample rate"));
    return FALSE;
  }
setup_failed:
  {
    GST_ELEMENT_ERROR (lame, LIBRARY, SETTINGS,
        (_("Failed to configure LAMEMP3ENC encoder. Check your encoding parameters.")), (NULL));
    return FALSE;
  }
}

/* <php-emulation-mode>three underscores for ___rate is really really really
 * private as opposed to one underscore<php-emulation-mode> */
/* call this MACRO outside of the NULL state so that we have a higher chance
 * of actually having a pipeline and bus to get the message through */

#define CHECK_AND_FIXUP_BITRATE(obj,param,rate)		 		  \
G_STMT_START {                                                            \
  gint ___rate = rate;                                                    \
  gint maxrate = 320;							  \
  gint multiplier = 64;							  \
  if (rate == 0) {                                                        \
    ___rate = rate;                                                       \
  } else if (rate <= 64) {				                  \
    maxrate = 64; multiplier = 8;                                         \
    if ((rate % 8) != 0) ___rate = GST_ROUND_UP_8 (rate); 		  \
  } else if (rate <= 128) {						  \
    maxrate = 128; multiplier = 16;                                       \
    if ((rate % 16) != 0) ___rate = GST_ROUND_UP_16 (rate);               \
  } else if (rate <= 256) {						  \
    maxrate = 256; multiplier = 32;                                       \
    if ((rate % 32) != 0) ___rate = GST_ROUND_UP_32 (rate);               \
  } else if (rate <= 320) { 						  \
    maxrate = 320; multiplier = 64;                                       \
    if ((rate % 64) != 0) ___rate = GST_ROUND_UP_64 (rate);               \
  }                                                                       \
  if (___rate != rate) {                                                  \
    GST_ELEMENT_WARNING (obj, LIBRARY, SETTINGS,			  \
      (_("The requested bitrate %d kbit/s for property '%s' "             \
       "is not allowed. "  					          \
       "The bitrate was changed to %d kbit/s."), rate,		          \
         param,  ___rate), 					          \
       ("A bitrate below %d should be a multiple of %d.", 		  \
          maxrate, multiplier));		  			  \
    rate = ___rate;                                                       \
  }                                                                       \
} G_STMT_END

static void
gst_lamemp3enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstLameMP3Enc *lame;

  lame = GST_LAMEMP3ENC (object);

  switch (prop_id) {
    case ARG_TARGET:
      lame->target = g_value_get_enum (value);
      break;
    case ARG_BITRATE:
      lame->bitrate = g_value_get_int (value);
      break;
    case ARG_CBR:
      lame->cbr = g_value_get_boolean (value);
      break;
    case ARG_QUALITY:
      lame->quality = g_value_get_float (value);
      break;
    case ARG_ENCODING_ENGINE_QUALITY:
      lame->encoding_engine_quality = g_value_get_enum (value);
      break;
    case ARG_MONO:
      lame->mono = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_lamemp3enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstLameMP3Enc *lame;

  lame = GST_LAMEMP3ENC (object);

  switch (prop_id) {
    case ARG_TARGET:
      g_value_set_enum (value, lame->target);
      break;
    case ARG_BITRATE:
      g_value_set_int (value, lame->bitrate);
      break;
    case ARG_CBR:
      g_value_set_boolean (value, lame->cbr);
      break;
    case ARG_QUALITY:
      g_value_set_float (value, lame->quality);
      break;
    case ARG_ENCODING_ENGINE_QUALITY:
      g_value_set_enum (value, lame->encoding_engine_quality);
      break;
    case ARG_MONO:
      g_value_set_boolean (value, lame->mono);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_lamemp3enc_flush_full (GstLameMP3Enc * lame, gboolean push)
{
  GstBuffer *buf;
  gint size;
  GstFlowReturn result = GST_FLOW_OK;

  if (!lame->lgf)
    return GST_FLOW_OK;

  buf = gst_buffer_new_and_alloc (7200);
  size = lame_encode_flush (lame->lgf, GST_BUFFER_DATA (buf), 7200);

  if (size > 0 && push) {
    GST_BUFFER_SIZE (buf) = size;
    GST_DEBUG_OBJECT (lame, "pushing final packet of %u bytes", size);
    result = gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (lame), buf, -1);
  } else {
    GST_DEBUG_OBJECT (lame, "no final packet (size=%d, push=%d)", size, push);
    gst_buffer_unref (buf);
    result = GST_FLOW_OK;
  }
  return result;
}

static void
gst_lamemp3enc_flush (GstAudioEncoder * enc)
{
  gst_lamemp3enc_flush_full (GST_LAMEMP3ENC (enc), FALSE);
}

static GstFlowReturn
gst_lamemp3enc_handle_frame (GstAudioEncoder * enc, GstBuffer * in_buf)
{
  GstLameMP3Enc *lame;
  guchar *mp3_data;
  gint mp3_buffer_size, mp3_size;
  GstBuffer *mp3_buf;
  GstFlowReturn result;
  gint num_samples;
  guint8 *data;
  guint size;

  lame = GST_LAMEMP3ENC (enc);

  /* squeeze remaining and push */
  if (G_UNLIKELY (in_buf == NULL))
    return gst_lamemp3enc_flush_full (lame, TRUE);

  data = GST_BUFFER_DATA (in_buf);
  size = GST_BUFFER_SIZE (in_buf);

  num_samples = size / 2;

  /* allocate space for output */
  mp3_buffer_size = 1.25 * num_samples + 7200;
  mp3_buf = gst_buffer_new_and_alloc (mp3_buffer_size);
  mp3_data = GST_BUFFER_DATA (mp3_buf);

  /* lame seems to be too stupid to get mono interleaved going */
  if (lame->num_channels == 1) {
    mp3_size = lame_encode_buffer (lame->lgf,
        (short int *) data,
        (short int *) data, num_samples, mp3_data, mp3_buffer_size);
  } else {
    mp3_size = lame_encode_buffer_interleaved (lame->lgf,
        (short int *) data,
        num_samples / lame->num_channels, mp3_data, mp3_buffer_size);
  }

  GST_LOG_OBJECT (lame, "encoded %d bytes of audio to %d bytes of mp3",
      size, mp3_size);

  if (G_LIKELY (mp3_size > 0)) {
    GST_BUFFER_SIZE (mp3_buf) = mp3_size;
    result = gst_audio_encoder_finish_frame (enc, mp3_buf, -1);
  } else {
    if (mp3_size < 0) {
      /* eat error ? */
      g_warning ("error %d", mp3_size);
    }
    result = GST_FLOW_OK;
    gst_buffer_unref (mp3_buf);
  }

  return result;
}

/* set up the encoder state */
static gboolean
gst_lamemp3enc_setup (GstLameMP3Enc * lame, GstTagList ** tags)
{
  gboolean res;

#define CHECK_ERROR(command) G_STMT_START {\
  if ((command) < 0) { \
    GST_ERROR_OBJECT (lame, "setup failed: " G_STRINGIFY (command)); \
    if (*tags) { \
      gst_tag_list_free (*tags); \
      *tags = NULL; \
    } \
    return FALSE; \
  } \
}G_STMT_END

  int retval;
  GstCaps *allowed_caps;

  GST_DEBUG_OBJECT (lame, "starting setup");

  lame->lgf = lame_init ();

  if (lame->lgf == NULL)
    return FALSE;

  *tags = gst_tag_list_new ();

  /* copy the parameters over */
  lame_set_in_samplerate (lame->lgf, lame->samplerate);

  /* let lame choose default samplerate unless outgoing sample rate is fixed */
  allowed_caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (lame));

  if (allowed_caps != NULL) {
    GstStructure *structure;
    gint samplerate;

    structure = gst_caps_get_structure (allowed_caps, 0);

    if (gst_structure_get_int (structure, "rate", &samplerate)) {
      GST_DEBUG_OBJECT (lame, "Setting sample rate to %d as fixed in src caps",
          samplerate);
      lame_set_out_samplerate (lame->lgf, samplerate);
    } else {
      GST_DEBUG_OBJECT (lame, "Letting lame choose sample rate");
      lame_set_out_samplerate (lame->lgf, 0);
    }
    gst_caps_unref (allowed_caps);
    allowed_caps = NULL;
  } else {
    GST_DEBUG_OBJECT (lame, "No peer yet, letting lame choose sample rate");
    lame_set_out_samplerate (lame->lgf, 0);
  }

  CHECK_ERROR (lame_set_num_channels (lame->lgf, lame->num_channels));
  CHECK_ERROR (lame_set_bWriteVbrTag (lame->lgf, 0));

  if (lame->target == LAMEMP3ENC_TARGET_QUALITY) {
    CHECK_ERROR (lame_set_VBR (lame->lgf, vbr_default));
    CHECK_ERROR (lame_set_VBR_quality (lame->lgf, lame->quality));
  } else {
    if (lame->cbr) {
      CHECK_AND_FIXUP_BITRATE (lame, "bitrate", lame->bitrate);
      CHECK_ERROR (lame_set_VBR (lame->lgf, vbr_off));
      CHECK_ERROR (lame_set_brate (lame->lgf, lame->bitrate));
    } else {
      CHECK_ERROR (lame_set_VBR (lame->lgf, vbr_abr));
      CHECK_ERROR (lame_set_VBR_mean_bitrate_kbps (lame->lgf, lame->bitrate));
    }
    gst_tag_list_add (*tags, GST_TAG_MERGE_REPLACE, GST_TAG_BITRATE,
        lame->bitrate * 1000, NULL);
  }

  if (lame->encoding_engine_quality == LAMEMP3ENC_ENCODING_ENGINE_QUALITY_FAST)
    CHECK_ERROR (lame_set_quality (lame->lgf, 7));
  else if (lame->encoding_engine_quality ==
      LAMEMP3ENC_ENCODING_ENGINE_QUALITY_HIGH)
    CHECK_ERROR (lame_set_quality (lame->lgf, 2));
  /* else default */

  if (lame->mono)
    CHECK_ERROR (lame_set_mode (lame->lgf, MONO));

  /* initialize the lame encoder */
  if ((retval = lame_init_params (lame->lgf)) >= 0) {
    /* FIXME: it would be nice to print out the mode here */
    GST_INFO
        ("lame encoder setup (target %s, quality %f, bitrate %d, %d Hz, %d channels)",
        (lame->target == LAMEMP3ENC_TARGET_QUALITY) ? "quality" : "bitrate",
        lame->quality, lame->bitrate, lame->samplerate, lame->num_channels);
    res = TRUE;
  } else {
    GST_ERROR_OBJECT (lame, "lame_init_params returned %d", retval);
    res = FALSE;
  }

  GST_DEBUG_OBJECT (lame, "done with setup");
  return res;
#undef CHECK_ERROR
}

gboolean
gst_lamemp3enc_register (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (debug, "lamemp3enc", 0, "lame mp3 encoder");

  if (!gst_element_register (plugin, "lamemp3enc", GST_RANK_PRIMARY,
          GST_TYPE_LAMEMP3ENC))
    return FALSE;

  return TRUE;
}

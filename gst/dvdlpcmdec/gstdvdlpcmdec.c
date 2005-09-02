/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2005> Jan Schmidt <jan@noraisin.net>
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
/* Element-Checklist-Version: TODO */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <string.h>

#include "gstdvdlpcmdec.h"

GST_DEBUG_CATEGORY_STATIC (dvdlpcm_debug);
#define GST_CAT_DEFAULT dvdlpcm_debug

/* elementfactory information */
static GstElementDetails gst_dvdlpcmdec_details =
GST_ELEMENT_DETAILS ("DVD LPCM Audio decoder",
    "Codec/Demuxer/Audio",
    "Decode DVD LPCM frames into standard PCM audio",
    "Jan Schmidt <jan@noraisin.net>\n" "Michael Smith <msmith@fluendo.com>");

static GstStaticPadTemplate gst_dvdlpcmdec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-private1-lpcm; "
        "audio/x-lpcm, "
        "width = (int) { 16, 20, 24 }, "
        "rate = (int) { 48000, 96000 }, "
        "channels = (int) [ 1, 8 ], "
        "dynamic_range = (int) [ 0, 255 ], "
        "emphasis = (boolean) { TRUE, FALSE }, "
        "mute = (boolean) { TRUE, FALSE } ")
    );

static GstStaticPadTemplate gst_dvdlpcmdec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) { 16, 24 }, "
        "rate = (int) { 48000, 96000 }, "
        "channels = (int) [ 1, 8 ], "
        "endianness = (int) { BIG_ENDIAN }, "
        "depth = (int) { 16, 24 }, " "signed = (boolean) { true }")
    );

/* DvdLpcmDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

static void gst_dvdlpcmdec_base_init (gpointer g_class);
static void gst_dvdlpcmdec_class_init (GstDvdLpcmDecClass * klass);
static void gst_dvdlpcmdec_init (GstDvdLpcmDec * dvdlpcmdec);

static GstFlowReturn gst_dvdlpcmdec_chain_raw (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_dvdlpcmdec_chain_dvd (GstPad * pad,
    GstBuffer * buffer);
static GstPadLinkReturn gst_dvdlpcmdec_setcaps (GstPad * pad, GstCaps * caps);

static GstElementStateReturn gst_dvdlpcmdec_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

GType
gst_dvdlpcmdec_get_type (void)
{
  static GType dvdlpcmdec_type = 0;

  if (!dvdlpcmdec_type) {
    static const GTypeInfo dvdlpcmdec_info = {
      sizeof (GstDvdLpcmDecClass),
      gst_dvdlpcmdec_base_init,
      NULL,
      (GClassInitFunc) gst_dvdlpcmdec_class_init,
      NULL,
      NULL,
      sizeof (GstDvdLpcmDec),
      0,
      (GInstanceInitFunc) gst_dvdlpcmdec_init,
    };

    dvdlpcmdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstDvdLpcmDec",
        &dvdlpcmdec_info, 0);
  }
  return dvdlpcmdec_type;
}

static void
gst_dvdlpcmdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvdlpcmdec_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_dvdlpcmdec_src_template));
  gst_element_class_set_details (element_class, &gst_dvdlpcmdec_details);
}

static void
gst_dvdlpcmdec_class_init (GstDvdLpcmDecClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_dvdlpcmdec_change_state;
}

static void
gst_dvdlpcm_reset (GstDvdLpcmDec * dvdlpcmdec)
{
  dvdlpcmdec->rate = 0;
  dvdlpcmdec->channels = 0;
  dvdlpcmdec->width = 0;
  dvdlpcmdec->out_width = 0;
  dvdlpcmdec->dynamic_range = 0;
  dvdlpcmdec->emphasis = FALSE;
  dvdlpcmdec->mute = FALSE;
  dvdlpcmdec->timestamp = 0;

  dvdlpcmdec->header = 0;

  GST_LOG_OBJECT (dvdlpcmdec, "Setting NULL caps on src pad");
  gst_pad_set_caps (dvdlpcmdec->srcpad, NULL);
}

static void
gst_dvdlpcmdec_init (GstDvdLpcmDec * dvdlpcmdec)
{
  dvdlpcmdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_dvdlpcmdec_sink_template), "sink");
  gst_pad_set_setcaps_function (dvdlpcmdec->sinkpad, gst_dvdlpcmdec_setcaps);
  gst_element_add_pad (GST_ELEMENT (dvdlpcmdec), dvdlpcmdec->sinkpad);

  dvdlpcmdec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_dvdlpcmdec_src_template), "src");
  gst_pad_use_fixed_caps (dvdlpcmdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dvdlpcmdec), dvdlpcmdec->srcpad);

  gst_dvdlpcm_reset (dvdlpcmdec);
}

static GstPadLinkReturn
gst_dvdlpcmdec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstStructure *structure;
  gboolean res = TRUE;
  GstDvdLpcmDec *dvdlpcmdec;
  GstCaps *src_caps;

  g_return_val_if_fail (caps != NULL, FALSE);
  g_return_val_if_fail (pad != NULL, FALSE);

  dvdlpcmdec = GST_DVDLPCMDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* If we have the DVD structured LPCM (including header) */
  if (gst_structure_has_name (structure, "audio/x-private1-lpcm")) {
    gst_pad_set_chain_function (dvdlpcmdec->sinkpad, gst_dvdlpcmdec_chain_dvd);
    goto done;
  }

  gst_pad_set_chain_function (dvdlpcmdec->sinkpad, gst_dvdlpcmdec_chain_raw);

  res &= gst_structure_get_int (structure, "rate", &dvdlpcmdec->rate);
  res &= gst_structure_get_int (structure, "channels", &dvdlpcmdec->channels);
  res &= gst_structure_get_int (structure, "width", &dvdlpcmdec->width);
  res &= gst_structure_get_int (structure, "dynamic_range",
      &dvdlpcmdec->dynamic_range);
  res &= gst_structure_get_boolean (structure, "emphasis",
      &dvdlpcmdec->emphasis);
  res &= gst_structure_get_boolean (structure, "mute", &dvdlpcmdec->mute);

  if (!res)
    goto caps_parse_error;

  /* Output width is the input width rounded up to the nearest byte */
  if (dvdlpcmdec->width == 20)
    dvdlpcmdec->out_width = 24;
  else
    dvdlpcmdec->out_width = dvdlpcmdec->width;

  /* Build caps to set on the src pad, which we know from the incoming caps */
  src_caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, dvdlpcmdec->rate,
      "channels", G_TYPE_INT, dvdlpcmdec->channels,
      "endianness", G_TYPE_INT, G_BIG_ENDIAN,
      "depth", G_TYPE_INT, dvdlpcmdec->out_width,
      "width", G_TYPE_INT, dvdlpcmdec->out_width,
      "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  GST_DEBUG_OBJECT (dvdlpcmdec, "Set rate %d, channels %d, width %d (out %d)",
      dvdlpcmdec->rate, dvdlpcmdec->channels, dvdlpcmdec->width,
      dvdlpcmdec->out_width);

  if (!gst_pad_set_caps (dvdlpcmdec->srcpad, src_caps)) {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Failed to set caps!");
    res = FALSE;
  } else {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Successfully set caps: %" GST_PTR_FORMAT,
        caps);
  }

  gst_caps_unref (src_caps);

done:
  gst_object_unref (dvdlpcmdec);

  return res;

  /* ERRORS */
caps_parse_error:
  {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Couldn't get parameters; missing caps?");
    res = FALSE;
    goto done;
  }
}

static void
update_timestamps (GstDvdLpcmDec * dvdlpcmdec, GstBuffer * buf, int samples)
{
  GST_BUFFER_DURATION (buf) = samples * GST_SECOND / dvdlpcmdec->rate;
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    /* Then leave it as-is, and save this timestamp */
    dvdlpcmdec->timestamp = GST_BUFFER_TIMESTAMP (buf);
  } else {
    dvdlpcmdec->timestamp += GST_BUFFER_DURATION (buf);
    GST_BUFFER_TIMESTAMP (buf) = dvdlpcmdec->timestamp;
  }

  GST_LOG_OBJECT (dvdlpcmdec, "Updated timestamp to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));
}

static void
parse_header (GstDvdLpcmDec * dec, guint32 header)
{
  /* We don't actually use 'dynamic range', 'mute', or 'emphasis' currently, 
   * but parse them out */
  dec->dynamic_range = header & 0xff;

  dec->mute = (header & 0x400000) != 0;
  dec->emphasis = (header & 0x800000) != 0;

  /* These two bits tell us the bit depth */
  switch (header & 0xC000) {
    case 0x8000:
      dec->width = 24;
      dec->out_width = 24;
      break;
    case 0x4000:
      dec->width = 20;
      dec->out_width = 24;
      break;
    default:
      dec->width = 16;
      dec->out_width = 16;
      break;
  }

  /* Only two sample rates supported */
  if (header & 0x1000)
    dec->rate = 96000;
  else
    dec->rate = 48000;

  /* And, of course, the number of channels (up to 8) */
  dec->channels = ((header >> 8) & 0x7) + 1;
}

static GstFlowReturn
gst_dvdlpcmdec_chain_dvd (GstPad * pad, GstBuffer * buf)
{
  GstDvdLpcmDec *dvdlpcmdec;
  guint8 *data;
  guint size;
  guint first_access;
  guint32 header;
  GstBuffer *subbuf;
  GstFlowReturn ret;
  gint off, len;

  dvdlpcmdec = GST_DVDLPCMDEC (gst_pad_get_parent (pad));

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);

  /* We have a 5 byte header, now.
   * The first two bytes are a (big endian) 16 bit offset into our buffer.
   * The buffer timestamp refers to this offset.
   *
   * The other three bytes are a (big endian) number in which the header is
   * encoded.
   */
  first_access = (data[0] << 8) | data[1];
  header = (data[2] << 16) | (data[3] << 8) | data[4];

  /* see if we have a new header */
  if (header != dvdlpcmdec->header) {
    GstCaps *src_caps;

    parse_header (dvdlpcmdec, header);

    /* Build caps to set on the src pad from what we've just parsed */
    src_caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dvdlpcmdec->rate,
        "channels", G_TYPE_INT, dvdlpcmdec->channels,
        "endianness", G_TYPE_INT, G_BIG_ENDIAN,
        "depth", G_TYPE_INT, dvdlpcmdec->out_width,
        "width", G_TYPE_INT, dvdlpcmdec->out_width,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);

    GST_DEBUG_OBJECT (dvdlpcmdec, "Set rate %d, channels %d, width %d",
        dvdlpcmdec->rate, dvdlpcmdec->channels, dvdlpcmdec->width);

    if (!gst_pad_set_caps (dvdlpcmdec->srcpad, src_caps))
      goto negotiation_failed;

    gst_caps_unref (src_caps);

    dvdlpcmdec->header = header;
  }

  GST_LOG_OBJECT (dvdlpcmdec, "first_access %d, buffer length %d", first_access,
      size);

  /* After first_access, we have an additional 3 bytes of data we've parsed and
   * don't want to handle; this is included within the value of first_access.
   * So a first_access value of between 1 and 3 is just broken, we treat that
   * the same as zero. first_access == 4 means we only need to create a single
   * sub-buffer, greater than that we need to create two. */

  /* skip access unit bytes and info */
  off = 5;

  if (first_access > 4) {
    /* length of first blength before access unit */
    len = first_access - 4;

    GST_LOG_OBJECT (dvdlpcmdec, "Creating first sub-buffer off %d, len %d", off,
        len);

    /* see if we need a subbuffer without timestamp */
    if (len > 0) {
      subbuf = gst_buffer_create_sub (buf, off, len);
      GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
      ret = gst_dvdlpcmdec_chain_raw (pad, subbuf);
      if (ret != GST_FLOW_OK)
        goto done;
    }

    /* then the buffer with new timestamp */
    off += len;
    len = size - off;

    GST_LOG_OBJECT (dvdlpcmdec, "Creating next sub-buffer off %d, len %d", off,
        len);

    subbuf = gst_buffer_create_sub (buf, off, len);
    GST_BUFFER_TIMESTAMP (subbuf) = GST_BUFFER_TIMESTAMP (buf);

    ret = gst_dvdlpcmdec_chain_raw (pad, subbuf);
  } else {
    GST_LOG_OBJECT (dvdlpcmdec, "Creating single sub-buffer off %d, len %d",
        off, size - off);
    /* We don't have a valid timestamp at all */
    subbuf = gst_buffer_create_sub (buf, off, size - off);
    GST_BUFFER_TIMESTAMP (subbuf) = GST_CLOCK_TIME_NONE;
    ret = gst_dvdlpcmdec_chain_raw (pad, subbuf);
  }

done:
  gst_buffer_unref (buf);
  gst_object_unref (dvdlpcmdec);

  return ret;

  /* ERRORS */
negotiation_failed:
  {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Failed to set caps!");
    GST_DEBUG_OBJECT (dvdlpcmdec, "Couldn't negotiate caps on src pad");
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static GstFlowReturn
gst_dvdlpcmdec_chain_raw (GstPad * pad, GstBuffer * buf)
{
  GstDvdLpcmDec *dvdlpcmdec;
  guchar *data;
  guint size;
  GstFlowReturn ret;
  guint samples = 0;

  dvdlpcmdec = GST_DVDLPCMDEC (gst_pad_get_parent (pad));

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);

  GST_LOG_OBJECT (dvdlpcmdec, "got buffer %p of size %d", buf, size);

  if (dvdlpcmdec->rate == 0)
    goto not_negotiated;

  if (!GST_PAD_IS_USABLE (dvdlpcmdec->srcpad))
    goto disabled;

  /* We don't currently do anything at all regarding emphasis, mute or 
   * dynamic_range - I'm not sure what they're for */
  switch (dvdlpcmdec->width) {
    case 16:
    {
      /* We can just pass 16-bits straight through intact, once we set 
       * appropriate things on the buffer */
      samples = size / dvdlpcmdec->channels / 2;
      buf = gst_buffer_make_writable (buf);
      break;
    }
    case 20:
    {
      /* Allocate a new buffer and copy 20-bit width to 24-bit */
      gint64 samples = size * 8 / 20;
      gint64 count = size / 10;
      gint64 i;
      guchar *src;
      guchar *dest;
      GstBuffer *outbuf;
      GstCaps *bufcaps = GST_PAD_CAPS (dvdlpcmdec->srcpad);

      ret = gst_pad_alloc_buffer (dvdlpcmdec->srcpad, 0,
          samples * 3, bufcaps, &outbuf);

      if (ret != GST_FLOW_OK)
        goto buffer_alloc_failed;

      gst_buffer_stamp (outbuf, buf);

      /* adjust samples so we can calc the new timestamp */
      samples = samples / dvdlpcmdec->channels;

      src = data;
      dest = GST_BUFFER_DATA (outbuf);

      /* Copy 20-bit LPCM format to 24-bit buffers, with 0x00 in the lowest 
       * nibble. Note that the first 2 bytes are already correct */
      for (i = 0; i < count; i++) {
        dest[0] = src[0];
        dest[1] = src[1];
        dest[2] = src[8] & 0xf0;
        dest[3] = src[2];
        dest[4] = src[3];
        dest[5] = (src[8] & 0x0f) << 4;
        dest[6] = src[4];
        dest[7] = src[5];
        dest[8] = src[9] & 0x0f;
        dest[9] = src[6];
        dest[10] = src[7];
        dest[11] = (src[9] & 0x0f) << 4;

        src += 10;
        dest += 12;
      }

      gst_buffer_unref (buf);
      buf = outbuf;
      break;
    }
    case 24:
    {
      /* Rearrange 24-bit LPCM format in-place. Note that the first 2
       * and last byte are already correct */
      guint count = size / 12;
      gint i;
      guint8 *src;

      samples = size / dvdlpcmdec->channels / 3;

      /* Ensure our output buffer is writable */
      buf = gst_buffer_make_writable (buf);

      src = GST_BUFFER_DATA (buf);
      for (i = 0; i < count; i++) {
        guchar temp[9];

        temp[0] = src[8];
        temp[1] = src[2];
        temp[2] = src[3];
        temp[3] = src[9];
        temp[4] = src[4];
        temp[5] = src[5];
        temp[6] = src[10];
        temp[7] = src[6];
        temp[8] = src[7];

        memcpy (src + 2, temp, 9);
        src += 12;
      }
      break;
    }
    default:
      goto invalid_width;
  }

  /* Set appropriate caps on it to pass downstream */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (dvdlpcmdec->srcpad));
  update_timestamps (dvdlpcmdec, buf, samples);

  ret = gst_pad_push (dvdlpcmdec->srcpad, buf);

done:
  gst_object_unref (dvdlpcmdec);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, FORMAT, (NULL),
        ("Buffer pushed before negotiation"));
    gst_buffer_unref (buf);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
disabled:
  {
    GST_DEBUG_OBJECT (dvdlpcmdec, "Discarding buffer on disabled pad");
    gst_buffer_unref (buf);
    ret = GST_FLOW_NOT_LINKED;
    goto done;
  }
buffer_alloc_failed:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, RESOURCE, FAILED, (NULL),
        ("Buffer allocation failed"));
    gst_buffer_unref (buf);
    goto done;
  }
invalid_width:
  {
    GST_ELEMENT_ERROR (dvdlpcmdec, STREAM, WRONG_TYPE, (NULL),
        ("Invalid sample width configured"));
    gst_buffer_unref (buf);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static GstElementStateReturn
gst_dvdlpcmdec_change_state (GstElement * element)
{
  GstDvdLpcmDec *dvdlpcmdec = GST_DVDLPCMDEC (element);
  GstElementStateReturn res;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      gst_dvdlpcm_reset (dvdlpcmdec);
      break;
    default:
      break;
  }

  res = parent_class->change_state (element);

  switch (GST_STATE_TRANSITION (element)) {
    default:
      break;
  }

  return res;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (dvdlpcm_debug, "dvdlpcmdec", 0, "DVD LPCM Decoder");

  if (!gst_element_register (plugin, "dvdlpcmdec", GST_RANK_PRIMARY,
          GST_TYPE_DVDLPCMDEC)) {
    return FALSE;
  }

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "dvdlpcmdec",
    "Decode DVD LPCM frames into standard PCM",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

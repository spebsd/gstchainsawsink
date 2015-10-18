/*-
 * Copyright (c) 2010-2015 Sebastien Petit <spebsd@gmail.com>
 *      The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * SECTION:element-chainsawsink
 *
 * FIXME:Describe chainsawsink here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m fakesrc ! chainsawsink ! fakesink silent=TRUE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

extern "C" {
#include <gst/gst.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <pthread.h>
pthread_mutex_t mutex_ffmpeg;
#include <string.h>
}

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include "gstchainsawsink.h"


#define DEFAULT_SYNC FALSE

GST_DEBUG_CATEGORY_STATIC (gst_chainsaw_sink_debug_category);
#define GST_CAT_DEFAULT gst_chainsaw_sink_debug_category

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_HLS_LOCATION,
  PROP_HLS_INDEXNAME,
  PROP_HLS_HTTPURL,
  PROP_GENERATE_ISMV,
  PROP_ISMV_LOCATION,
  PROP_ISMV_INDEXNAME,
  PROP_SEGMENT_DURATION,
  PROP_RECORD_BUFFER,
  PROP_ISMV_QUALITY,
  PROP_GENERATE_MANIFEST,
  PROP_AUDIO_ONLY,
  PROP_QUICKSTART_TARGET_DURATION,
  PROP_VARNISH_ENABLE,
  PROP_VARNISH_BASE_HTTP_URL,
  PROP_MEMCACHE_IP,
  PROP_DROPCONN_URL
};

gboolean initialized = FALSE;

static GstStaticPadTemplate chainsawsinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define gst_chainsaw_sink_parent_class parent_class
G_DEFINE_TYPE (GstChainsawSink, gst_chainsaw_sink, GST_TYPE_BASE_SINK);


static void gst_chainsaw_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_chainsaw_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_chainsaw_sink_finalize (GObject * obj);
static gboolean gst_chainsaw_sink_stop (GstBaseSink * bsink);
static gboolean gst_chainsaw_sink_start (GstBaseSink * bsink);
static void gst_chainsaw_sink_init (GstChainsawSink * chainsawsink);
int gst_chainsaw_sink_open_file(const char *path);
static gboolean gst_chainsaw_sink_event (GstBaseSink * bsink, GstEvent * event);
static GstFlowReturn gst_chainsaw_sink_render (GstBaseSink * bsink, GstBuffer * buffer);
static int gst_chainsaw_sink_write_buf(int fd, guint8 * buf, gsize size);

static void
gst_chainsaw_sink_class_init (GstChainsawSinkClass * klass) {
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbase_sink_class;

  signal(SIGPIPE, SIG_IGN);

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbase_sink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_chainsaw_sink_set_property;
  gobject_class->get_property = gst_chainsaw_sink_get_property;
  gobject_class->finalize = gst_chainsaw_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_SILENT,
                                   g_param_spec_boolean ("silent", "Silent", "Produce verbose output ?",
                                                         FALSE,
                                                         (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_RECORD_BUFFER,
                                   g_param_spec_double ("recordBuffer", "Record Buffer",
                                                        "Record Buffer in seconds", 0.100, 10800.0, 6.0,
                                                        (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_HLS_LOCATION,
                                   g_param_spec_string ("hlsLocation", "Location",
                                                        "Path location for writing media files",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_HLS_INDEXNAME,
                                   g_param_spec_string ("hlsIndexname", "IndexName",
                                                        "Name of the index file",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_HLS_HTTPURL,
                                   g_param_spec_string ("hlsHttpurl", "HTTP Url",
                                                        "HTTP base url for fetching TS chuncks",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_GENERATE_ISMV,
                                   g_param_spec_boolean ("generateIsmv", "GenreateIsmv", "Generate Ismv",
                                                         FALSE,
                                                         (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ISMV_LOCATION,
                                   g_param_spec_string ("ismvLocation", "Location",
                                                        "Path location for writing media files",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_ISMV_INDEXNAME,
                                   g_param_spec_string ("ismvIndexname", "IndexName",                                                        "Name of the index file",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_SEGMENT_DURATION,
                                   g_param_spec_double ("segmentDuration", "Segment Duration",
                                                        "Duration in seconds of TS chuncks", 0.100, 20.0, 10.0,
                                                        (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_QUICKSTART_TARGET_DURATION,
                                   g_param_spec_double ("quickstartTargetDuration", "quickstart target duration",
						 	"Set quickstart target duration",
							1.0, 20.0, 2.0,
                                                        (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS)));

  g_object_class_install_property (gobject_class, PROP_ISMV_QUALITY,
                                   g_param_spec_int ("ismvQuality", "ismv quality",
                                                     "bitrate", 128, 8192, 1024,
                                                     (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_GENERATE_MANIFEST,
                                   g_param_spec_boolean ("generateManifest", "generate manifest", "Manifest ism client manifest file",
                                                         FALSE,
                                                         (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_AUDIO_ONLY,
                                   g_param_spec_boolean ("audioOnly", "audio only", "generate stream without video",
                                                         FALSE,
                                                         (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VARNISH_ENABLE,
                                   g_param_spec_boolean ("varnishEnable", "varnish enable", "activate varnish for banning expired urls",
                                                         FALSE,
                                                         (GParamFlags)G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_VARNISH_BASE_HTTP_URL,
                                   g_param_spec_string ("varnishBaseHttpUrl", "varnish base url", "varnish base url (eg: http://192.168.0.1/)",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_MEMCACHE_IP,
                                   g_param_spec_string ("memcacheIp", "memcache internet address", "memcache ip address",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));

  g_object_class_install_property (gobject_class, PROP_DROPCONN_URL,
                                   g_param_spec_string ("dropConnectionUrl", "HTTP drop connection callback url", "HTTP drop connection callback url (eg: http://192.168.0.1/?drop=yes&id=1234)",
                                                        NULL,
                                                        (GParamFlags)(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | GST_PARAM_MUTABLE_READY)));


  gstbase_sink_class->stop = GST_DEBUG_FUNCPTR(gst_chainsaw_sink_stop);
  gstbase_sink_class->start = GST_DEBUG_FUNCPTR(gst_chainsaw_sink_start);
  gstbase_sink_class->event = GST_DEBUG_FUNCPTR (gst_chainsaw_sink_event);
  gstbase_sink_class->render = GST_DEBUG_FUNCPTR(gst_chainsaw_sink_render);

  gst_element_class_set_details_simple (gstelement_class,
    "chainsawsink",
    "??",
    "Apple http streaming plugin",
    "Sebastien Petit <spebsd@gmail.com>");
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&chainsawsinktemplate));

  GST_DEBUG_CATEGORY_INIT (gst_chainsaw_sink_debug_category,
                           "chainsawsink",
                           0,
                           "Template chainsawsink");
}

static void
gst_chainsaw_sink_init (GstChainsawSink * chainsawsink) {
  int fd = 0;

  chainsawsink->silent = FALSE;
  chainsawsink->tmpchainsaw = (char *)malloc(128);
  snprintf(chainsawsink->tmpchainsaw, 128, "/tmp/chainsaw.XXXXXXXX");

  fd = mkstemp(chainsawsink->tmpchainsaw);
  if (fd == -1) {
    fprintf(stderr, "Can't create temporary chainsaw file: %s\n", strerror(errno));
    return;
  }
  close(fd);

  GST_DEBUG("tmpchainsaw is '%s'", chainsawsink->tmpchainsaw);

  // XXX fixme ?
  chainsaw::ChainsawSink::init();

  gst_base_sink_set_sync (GST_BASE_SINK (chainsawsink), FALSE);
}

static void
gst_chainsaw_sink_finalize (GObject * obj) {
#if !GLIB_CHECK_VERSION(2,26,0)
  GstChainsawSink *sink = GST_FAKE_SINK (obj);
#endif

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_chainsaw_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec) {
  GstChainsawSink *chainsawsink;

  chainsawsink = GST_CHAINSAW_SINK (object);

  switch (prop_id) {
    case PROP_SILENT:
      chainsawsink->silent = g_value_get_boolean (value);
      break;
    case PROP_HLS_LOCATION:
      chainsawsink->hlsLocation = g_strdup(g_value_get_string (value));
      break;
    case PROP_HLS_INDEXNAME:
      chainsawsink->hlsIndexname = g_strdup(g_value_get_string (value));
      break;
    case PROP_HLS_HTTPURL:
      chainsawsink->hlsHttpurl = g_strdup(g_value_get_string (value));
      break;
    case PROP_GENERATE_ISMV:
      chainsawsink->generateIsmv = g_value_get_boolean (value);
      break;
    case PROP_ISMV_LOCATION:
      chainsawsink->ismvLocation = g_strdup(g_value_get_string (value));
      break;
    case PROP_ISMV_INDEXNAME:
      chainsawsink->ismvIndexname = g_strdup(g_value_get_string (value));
      break;
    case PROP_SEGMENT_DURATION:
      chainsawsink->segmentDuration = g_value_get_double (value);
      break;
    case PROP_QUICKSTART_TARGET_DURATION:
      chainsawsink->quickstartTargetDuration = g_value_get_double (value);
      break;
    case PROP_RECORD_BUFFER:
      chainsawsink->recordBuffer = g_value_get_double (value);
      break;
    case PROP_ISMV_QUALITY:
      chainsawsink->ismvQuality = g_value_get_int (value);
      break;
    case PROP_GENERATE_MANIFEST:
      chainsawsink->generateManifest = g_value_get_boolean (value);
      break;
    case PROP_AUDIO_ONLY:
      chainsawsink->audioOnly = g_value_get_boolean (value);
      break;
    case PROP_VARNISH_ENABLE:
      chainsawsink->varnishEnable = g_value_get_boolean (value);
      break;
    case PROP_VARNISH_BASE_HTTP_URL:
      chainsawsink->varnishBaseHttpUrl = g_strdup(g_value_get_string (value));
      break;
    case PROP_MEMCACHE_IP:
      chainsawsink->memcacheIp = g_strdup(g_value_get_string (value));
      break;
    case PROP_DROPCONN_URL:
      chainsawsink->dropConnectionUrl = g_strdup(g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_chainsaw_sink_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
  GstChainsawSink *chainsawsink;

  chainsawsink = GST_CHAINSAW_SINK (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, chainsawsink->silent);
      break;
    case PROP_HLS_LOCATION:
      g_value_set_string (value, chainsawsink->hlsLocation);
      break;
    case PROP_HLS_INDEXNAME:
      g_value_set_string (value, chainsawsink->hlsIndexname);
      break;
    case PROP_HLS_HTTPURL:
      g_value_set_string (value, chainsawsink->hlsHttpurl);
      break;
    case PROP_GENERATE_ISMV:
      g_value_set_boolean (value, chainsawsink->generateIsmv);
      break;
    case PROP_ISMV_LOCATION:
      g_value_set_string (value, chainsawsink->ismvLocation);
      break;
    case PROP_ISMV_INDEXNAME:
      g_value_set_string (value, chainsawsink->ismvIndexname);
      break;
    case PROP_SEGMENT_DURATION:
      g_value_set_double (value, chainsawsink->segmentDuration);
      break;
    case PROP_QUICKSTART_TARGET_DURATION:
      g_value_set_double (value, chainsawsink->quickstartTargetDuration);
      break;
    case PROP_RECORD_BUFFER:
      g_value_set_double (value, chainsawsink->recordBuffer);
      break;
    case PROP_ISMV_QUALITY:
      g_value_set_int (value, chainsawsink->ismvQuality);
      break;
    case PROP_GENERATE_MANIFEST:
      g_value_set_boolean (value, chainsawsink->generateManifest);
      break;
    case PROP_AUDIO_ONLY:
      g_value_set_boolean (value, chainsawsink->audioOnly);
      break;
    case PROP_VARNISH_ENABLE:
      g_value_set_boolean (value, chainsawsink->varnishEnable);
      break;
    case PROP_VARNISH_BASE_HTTP_URL:
      g_value_set_string (value, chainsawsink->varnishBaseHttpUrl);
      break;
    case PROP_MEMCACHE_IP:
      g_value_set_string (value, chainsawsink->memcacheIp);
      break;
    case PROP_DROPCONN_URL:
      g_value_set_string (value, chainsawsink->dropConnectionUrl);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_chainsaw_sink_event (GstBaseSink * bsink, GstEvent * event) {
  GstChainsawSink *chainsawsink = GST_CHAINSAW_SINK (bsink);

  GST_DEBUG("gst_chainsaw_sink_event event %d", GST_EVENT_TYPE(event));
  if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
    GST_INFO("gst_chainsaw_sink_event\n");
    if (! chainsawsink->tmpchainsaw) {
      GST_ERROR("Temporary chainsaw file is NULL, cannot continue...\n");
      return false;
    }

    int rc;

    /* Create a pipe for chainsaw sink processing */
    unlink(chainsawsink->tmpchainsaw);
    rc = mkfifo(chainsawsink->tmpchainsaw, 0600);
    if (rc < 0) {
      perror("cannot open fifo file");
      return FALSE;
    }

    if (chainsawsink->hlsHttpurl == NULL) {
      chainsawsink->hlsHttpurl = (char *)malloc(1);
      chainsawsink->hlsHttpurl[0] = '\0';
    }

    GST_DEBUG("creating chainsawSink object\n");
    GST_DEBUG("chainsawsink->tmpchainsaw : %p\nchainsawsink->hlsLocation: %p\nchainsawsink->hlsIndexname: %p\nchainsawsin->hlsHttpurl: %p\n,chainsawsink->segmentDuration: %f\nchainsawsink->recordBuffer: %f\nchainsawsink->audioOnly: %d\nchainsawsink->quickstartTargetDuration: %f\nchainsawsink->varnishEnable: %d\nchainsawsink->varnishBaseHttpUrl: %p\nchainsawsink->memcacheIp: %p\n", chainsawsink->tmpchainsaw, chainsawsink->hlsLocation, chainsawsink->hlsIndexname, chainsawsink->hlsHttpurl, chainsawsink->segmentDuration, chainsawsink->recordBuffer, chainsawsink->audioOnly, chainsawsink->quickstartTargetDuration, chainsawsink->varnishEnable, chainsawsink->varnishBaseHttpUrl, chainsawsink->memcacheIp);

    chainsawsink->chainsawSink = new chainsaw::ChainsawSink(chainsawsink->tmpchainsaw,
                                              chainsawsink->hlsLocation,
                                              chainsawsink->hlsIndexname,
                                              chainsawsink->hlsHttpurl,
                                              chainsawsink->segmentDuration,
                                              chainsawsink->recordBuffer,
                                              chainsawsink->audioOnly,
 					      chainsawsink->quickstartTargetDuration,
                                              chainsawsink->varnishEnable,
                                              chainsawsink->varnishBaseHttpUrl,
                                              chainsawsink->memcacheIp,
					      chainsawsink->dropConnectionUrl);

    GST_DEBUG("chainsawSink object created: %p\n", chainsawsink->chainsawSink);
    GST_DEBUG("creating thread for chainsawSink object\n");
    /* Create a thread for chainsaw sink processing */
    pthread_attr_init(&chainsawsink->attr);
    if (pthread_create(&chainsawsink->thread, &chainsawsink->attr, p_to_m_wrapper<chainsaw::ChainsawSink, &chainsaw::ChainsawSink::run>, (void *)chainsawsink->chainsawSink) != 0) {
      fprintf(stderr, "Can't create chainsawsink ffmpeg thread");
      return false;
    }

    // Opening tmpchainsaw descriptor
    chainsawsink->fd = gst_chainsaw_sink_open_file(chainsawsink->tmpchainsaw);
    if (chainsawsink->fd < 0) {
      GST_ERROR("cannot open file: %s", strerror(errno));
      return false;
    }

    chainsawsink->chainsawSink->setFileDescriptor(chainsawsink->fd);

    initialized = TRUE;
    GST_INFO("Chainsawsink thread ID %d is running", chainsawsink->thread);
  }

  if (GST_BASE_SINK_CLASS (parent_class)->event) {
    return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
  }
  else
    return TRUE;
}

static GstFlowReturn
gst_chainsaw_sink_render (GstBaseSink * bsink, GstBuffer * buf) {
  GstChainsawSink *chainsawsink = GST_CHAINSAW_SINK (bsink);

  if ((initialized == TRUE) && (chainsawsink->running == TRUE)) {
      GstMapInfo info;

      gst_buffer_map(buf, &info, GST_MAP_READ);
      if (gst_chainsaw_sink_write_buf(chainsawsink->fd, info.data, info.size) == -1) {
        GST_ERROR("cannot write on descriptor %d: %s\n", chainsawsink->fd, strerror(errno));
      }
      gst_buffer_unmap(buf, &info);
  }

  /* just push out the incoming buffer without touching it */
  return GST_FLOW_OK;
}

static gboolean
gst_chainsaw_sink_stop (GstBaseSink * bsink) {
  GstChainsawSink *chainsawsink = GST_CHAINSAW_SINK (bsink);

  GST_DEBUG("gst_chainsaw_sink_stop");
  chainsawsink->running = FALSE;
  if (chainsawsink->chainsawSink) {
    chainsawsink->chainsawSink->stop();
    GST_INFO("close filter %d", chainsawsink->fd);
    close(chainsawsink->fd);
    GST_DEBUG("joining thread ID %u", chainsawsink->thread);
    pthread_join(chainsawsink->thread, NULL);
    GST_DEBUG("Thread ID %u joined", chainsawsink->thread);
    //delete chainsawsink->chainsawSink;
    GST_DEBUG("chainsawsink->chainsawSink deleted");
    // XXX spe Join the thread ChainsawSink object and delete the object
  }
  if (chainsawsink->tmpchainsaw) {
    if (unlink(chainsawsink->tmpchainsaw))
      GST_ERROR("cannot unlink pipe %s", chainsawsink->tmpchainsaw);
    free(chainsawsink->tmpchainsaw);
  }
  free(chainsawsink->buffer);

  return TRUE;
}

static gboolean
gst_chainsaw_sink_start (GstBaseSink * bsink) {
  GstChainsawSink *chainsawsink = GST_CHAINSAW_SINK (bsink);

  GST_INFO("gst_chainsaw_sink_start");

  chainsawsink->buffer = (char *)malloc(65535);
  chainsawsink->bufferOffset = 0;
  chainsawsink->running = TRUE;

  return TRUE;
}

int
gst_chainsaw_sink_open_file(const char *path) {
  int fd;

  fd = open(path, O_WRONLY);
  GST_DEBUG("file open fd %d with path '%s'", fd, path);

  return fd;
}

static int
gst_chainsaw_sink_write_buf(int fd, guint8 * buf, gsize size) {
  return write(fd, buf, size);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "chainsawsink", GST_RANK_NONE,
          GST_TYPE_CHAINSAW_SINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    chainsawsink,
    "Template chainsawsink",
    plugin_init,
    VERSION,
    "BSD",
    "GStreamer",
    "http://gstreamer.net/"
)


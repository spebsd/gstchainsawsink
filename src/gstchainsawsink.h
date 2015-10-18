/*-
 * Copyright (c) 2010-2011 Sebastien Petit <spebsd@gmail.com>
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

#ifndef __GST_CHAINSAWSINK_H__
#define __GST_CHAINSAWSINK_H__

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "chainsaw/ChainsawSink.hh"

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_CHAINSAW_SINK                   \
  (gst_chainsaw_sink_get_type())
#define GST_CHAINSAW_SINK(obj)                                           \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_CHAINSAW_SINK,GstChainsawSink))
#define GST_CHAINSAW_SINK_CLASS(klass)                                   \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_CHAINSAW_SINK,GstChainsawSinkClass))
#define GST_IS_CHAINSAW_SINK(obj)                            \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_CHAINSAW_SINK))
#define GST_IS_CHAINSAW_SINK_CLASS(klass)                    \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_CHAINSAW_SINK))

typedef struct _GstChainsawSink      GstChainsawSink;
typedef struct _GstChainsawSinkClass GstChainsawSinkClass;

struct _GstChainsawSink
{
  GstBaseSink element;

  gboolean running;
  char *buffer;
  int bufferOffset;
  chainsaw::ChainsawSink * chainsawSink;
  gboolean silent;
  gchar* hlsLocation;
  gchar* hlsIndexname;
  gchar* hlsHttpurl;
  gboolean generateIsmv;
  gchar* ismvLocation;
  gchar* ismvIndexname;
  char * tmpchainsaw;
  pthread_t thread;
  pthread_attr_t attr;
  int fd;
  gdouble segmentDuration;
  gdouble recordBuffer;
  int ismvQuality;
  gboolean generateManifest;
  gboolean audioOnly;
  gdouble quickstartTargetDuration;
  gboolean varnishEnable;
  gchar *varnishBaseHttpUrl;
  gchar *memcacheIp;
  gchar *dropConnectionUrl;
  std::list<uint8_t *> buffer_list;
};

struct _GstChainsawSinkClass 
{
  GstBaseSinkClass parent_class;
};

GType gst_chainsaw_sink_get_type (void);

G_END_DECLS

#endif /* __GST_CHAINSAWSINK_H__ */

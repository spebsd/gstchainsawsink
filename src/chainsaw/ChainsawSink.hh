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

#ifndef __CHAINSAWSINK_HH__
#define __CHAINSAWSINK_HH__

#include <string>
#include <list>
#include <vector>
#include <pthread.h>
#include <libmemcached/memcached.h>

#include <gst/gst.h>

extern gboolean initialized;

struct AVStream;
struct AVFormatContext;

template<class T, void(T::*mem_fn)()>
void* p_to_m_wrapper(void* p)
{
  (static_cast<T*>(p)->*mem_fn)();
  return 0;
}

namespace chainsaw
{

class TsSegment
{
  public:
    unsigned long long index;
    gdouble duration;
};

class ChainsawSink
{
public:
  ChainsawSink(const char * tsIn,
               const char * hlsLocation,
               const char * hlsIndexname,
               const char * hlsHttpurl,
               double segmentDuration,
               double recordBuffer,
               bool audioOnly,
	       double quickstartTargetDuration,
               gboolean varnish_enable,
               const char * varnish_base_http_url,
               const char * memcache_ip,
               const char * drop_connection_callback_url);
  void run();
  void stop();
  bool isTerminate() const { return this->terminate; }
  static void init();
  static void monitorStream(ChainsawSink *);
  AVFormatContext **getInputFormatContextRef() { return &ctx; };
  AVFormatContext *getCtx() { return ctx; };
  time_t getLastVideoPacketTime() { return last_video_packet_time; };
  time_t getLastAudioPacketTime() { return last_audio_packet_time; };
  double getSegmentDuration() { return segmentDuration; };
  bool getEnd() { return end; };
  const std::string getHlsIndexname() { return hlsIndexname; };
  const int getFileDescriptor() { return fileDescriptor; }
  void setFileDescriptor(int fd) { fileDescriptor = fd; };
  int callHttpUrl(std::string);
  std::string getDropConnectionCallbackUrl() { return dropConnectionCallbackUrl; };

protected:
  int convert();
  int writeIndexQuickstartFiles();
  int writeIndexFile(bool);
  int varnishBanAllUrls(std::string &, std::string &);
  std::vector<std::string> split(const std::string&, char);

private:
  const std::string tsIn;
  const std::string hlsLocation;
  const std::string hlsIndexname;
  const std::string hlsHttpurl;
  const std::string format;
  const std::string extension;  
  std::string varnishBaseHttpUrl;
  std::string memcacheIp;
  std::string dropConnectionCallbackUrl;
  int fileDescriptor;

  double segmentDuration;
  const double quickstartTargetDuration;
  const double recordBuffer;
  
  std::string output_prefix;
  std::string output_prefix_td;
  std::string output_filename_prefix;
  std::string output_filename_prefix_td;
  std::string tmp_index;
  std::string index;
  std::string http_prefix;

  char * output_filename;
  size_t output_filename_size;

  TsSegment output_index;
  uint8_t quickstartAggregateSegmentsCount;
  uint64_t first_segment;
  uint64_t last_segment;
  std::list<TsSegment> *aggregateSegmentsList;
  std::list<TsSegment> *segmentsList;
  std::list<TsSegment> *qsSegmentsList;
  
  pthread_t pthreadId;
  pthread_mutex_t mutex;
  bool end;
  bool terminate;
  bool audioOnly;
  gboolean varnishEnable;
  gboolean memcacheEnable;
  gboolean qsEnable;
  int media_sequence;
  int media_sequence_td;
  int segments_count;
  memcached_st *memc;

  time_t last_audio_packet_time;
  time_t last_video_packet_time;

  AVFormatContext *ctx = NULL;
};

}

#endif

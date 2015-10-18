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

#define __STDC_CONSTANT_MACROS
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

extern "C"
{
#include <gst/gst.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <sys/time.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <curl/curl.h>
#include <unistd.h>
#include <pthread.h>
}

#include "ChainsawSink.hh"
#include <tr1/functional>

#include <list>
#include <vector>
#include <algorithm>
#include <thread>

GST_DEBUG_CATEGORY_STATIC (gst_chainsawsink_debug);
#define GST_CAT_DEFAULT gst_chainsawsink_debug

struct thread_infos
{
  pthread_t id;
  time_t t;
};

typedef std::list<struct thread_infos> threads_t;
threads_t threads;

struct chainsaw_parameters
{
  char * index;
  char * tmp_index;
  char * output_prefix;
};

namespace chainsaw
{

ChainsawSink::ChainsawSink(const char * tsIn,
                           const char * hlsLocation,
                           const char * hlsIndexname,
                           const char * hlsHttpurl,
                           gdouble segmentDuration,
                           gdouble recordBuffer,
                           bool audioOnly,
			   gdouble quickstartTargetDuration,
                           gboolean varnish_enable,
                           const char * varnish_base_http_url,
                           const char * memcache_ip,
			   const char * drop_connection_callback_url)
  : tsIn(tsIn),
    hlsLocation(hlsLocation),
    hlsIndexname(hlsIndexname),
    hlsHttpurl(hlsHttpurl),
    format("mpegts"),
    extension("ts"),
    segmentDuration(segmentDuration),
    quickstartTargetDuration(quickstartTargetDuration),
    recordBuffer(recordBuffer),
    end(false),
    terminate(false),
    audioOnly(audioOnly),
    varnishEnable(varnish_enable)
{
  this->segmentsList = new std::list<TsSegment>();
  pthread_mutex_init(&this->mutex, NULL);
  GST_DEBUG_CATEGORY_INIT (gst_chainsawsink_debug, 
                           "chainsawsink",
                           0,
                           "Template chainsawsink");
  GST_DEBUG("recordBuffer is %f", recordBuffer);
  GST_DEBUG("segmentDuration is %f", segmentDuration);
  GST_DEBUG("hlsHttpUrl is %s", this->hlsHttpurl.c_str());
  if (this->quickstartTargetDuration != 0.0) {
    this->qsSegmentsList = new std::list<TsSegment>();
    this->aggregateSegmentsList = new std::list<TsSegment>();
    this->quickstartAggregateSegmentsCount = this->quickstartTargetDuration / this->segmentDuration;
    GST_DEBUG("number of segment before aggregating (%f / %f): %d", this->quickstartTargetDuration, this->segmentDuration, this->quickstartAggregateSegmentsCount);
    this->qsEnable = true;
  }
  else
    this->qsEnable = false;
  this->segments_count = 0;
  if (memcache_ip != NULL) {
    this->memcacheEnable = true;
    this->memcacheIp = memcache_ip;
    std::string config_string = "--SERVER=" + this->memcacheIp;
    this->memc = memcached(config_string.c_str(), config_string.length());
    memcached_behavior_set(this->memc, MEMCACHED_BEHAVIOR_CONNECT_TIMEOUT, 1);
    memcached_behavior_set(this->memc, MEMCACHED_BEHAVIOR_POLL_TIMEOUT, 2);
    memcached_behavior_set(this->memc, MEMCACHED_BEHAVIOR_TCP_NODELAY, 1);
    memcached_behavior_set(this->memc, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);
  }
  else
    this->memcacheEnable = false;

  if (this->varnishEnable == true) {
    this->varnishBaseHttpUrl = varnish_base_http_url;
  }

  if (drop_connection_callback_url != NULL) {
    this->dropConnectionCallbackUrl = drop_connection_callback_url;
  }

  pthread_mutex_lock(&this->mutex);
  CURLcode crc = curl_global_init(CURL_GLOBAL_ALL);
  if (crc < 0)
  {
    GST_ERROR("cannot initialize curl correctly: %s\n", curl_easy_strerror(crc));
  }
  pthread_mutex_unlock(&this->mutex);

  this->last_video_packet_time = time(NULL);
  this->last_audio_packet_time = last_video_packet_time;

  return;
}

void ChainsawSink::init()
{

  static bool init = false;
  if (!init)
  {
    av_register_all();
    init = true;
  }
}

void monitor_stream(ChainsawSink *chainsawsink)
{
  time_t ctime;

  sleep(5);
  do {
    ctime = time(NULL);
    if (ctime - chainsawsink->getLastVideoPacketTime() > (chainsawsink->getSegmentDuration() * 5)) {
	GST_ERROR("there is no key frame on the stream %s since %ld second(s)", chainsawsink->getHlsIndexname().c_str(), ctime - chainsawsink->getLastVideoPacketTime());
    	//kill(getpid(), SIGINT);
        chainsawsink->callHttpUrl(chainsawsink->getDropConnectionCallbackUrl());
        return;
    }
    sleep(1);
    GST_DEBUG("chainsawsink->getEnd() = %d", chainsawsink->getEnd());
  } while (chainsawsink->getEnd() == false);

  GST_INFO("Quit monitor thread for stream %s", chainsawsink->getHlsIndexname().c_str());

  pthread_exit(NULL);
}

std::vector<std::string> ChainsawSink::split(const std::string& s, char seperator)
{
   std::vector<std::string> output;

    std::string::size_type prev_pos = 0, pos = 0;

    while((pos = s.find(seperator, pos)) != std::string::npos)
    {
        std::string substring( s.substr(prev_pos, pos-prev_pos) );

        output.push_back(substring);

        prev_pos = ++pos;
    }

    output.push_back(s.substr(prev_pos, pos-prev_pos)); // Last word

    return output;
}

void *varnishBanUrl(void *data)
{
  std::string *url_to_ban = (std::string *)data;
  CURL *curl_session;
  CURLcode crc;
  int rc = 0;
  long timeout = 500;

  GST_DEBUG("varnish ban requested on '%s'", url_to_ban->c_str());
  curl_session = curl_easy_init();
  curl_easy_setopt(curl_session, CURLOPT_CUSTOMREQUEST, "BAN");
  curl_easy_setopt(curl_session, CURLOPT_URL, url_to_ban->c_str());
  curl_easy_setopt(curl_session, CURLOPT_TIMEOUT_MS, timeout);
  curl_easy_setopt(curl_session, CURLOPT_CONNECTTIMEOUT_MS, timeout);
  crc =curl_easy_perform(curl_session);
  if (crc != CURLE_OK) {
    GST_ERROR("cannot perform varnish ban on url '%s': %s", url_to_ban->c_str(), curl_easy_strerror(crc));
    rc = -1;
  }
  curl_easy_cleanup(curl_session);
  if (rc == 0) {
    GST_INFO("url '%s' correctly ban", url_to_ban->c_str());
  }

  pthread_exit(NULL);
}

int ChainsawSink::callHttpUrl(std::string url)
{
  CURL *curl_session;
  CURLcode crc;
  int rc = 0;
  long timeout = 500;

  GST_DEBUG("connection must be dropped, calling callback '%s'", url.c_str());
  curl_session = curl_easy_init();
  curl_easy_setopt(curl_session, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl_session, CURLOPT_TIMEOUT_MS, timeout);
  curl_easy_setopt(curl_session, CURLOPT_CONNECTTIMEOUT_MS, timeout);
  crc =curl_easy_perform(curl_session);
  if (crc != CURLE_OK) {
    GST_ERROR("cannot perform GET on url '%s': %s", url.c_str(), curl_easy_strerror(crc));
    rc = -1;
  }
  curl_easy_cleanup(curl_session);

  pthread_exit(NULL);
}


int ChainsawSink::varnishBanAllUrls(std::string &varnish_base_url, std::string &varnish_path)
{
  std::vector<std::string> urls_vector = this->split(varnish_base_url, ',');
  std::vector<pthread_t *> threads;
  pthread_attr_t attr;
  pthread_t thread[urls_vector.size()];
  std::string * urls[urls_vector.size()];
  int i = 0;

  for (std::vector<std::string>::iterator it = urls_vector.begin(); it != urls_vector.end(); it++)
  {
    urls[i] = new std::string(*it + varnish_path);
    pthread_attr_init(&attr);
    pthread_create(&thread[i], &attr, &varnishBanUrl, (void *)urls[i]);
    i++;
  }

  for (i = 0; i < urls_vector.size(); i++)
  {
    pthread_join(thread[i], NULL);
    delete urls[i];
  }

  return 0;
}

static AVStream * add_output_stream(AVFormatContext *output_format_context, AVStream *input_stream) 
{
  AVCodecContext *input_codec_context;
  AVCodecContext *output_codec_context;
  AVStream *output_stream;

  output_stream = avformat_new_stream(output_format_context, 0);
  if (!output_stream) {
    GST_LOG("Could not allocate stream\n");
    return NULL;
  }

  input_codec_context = input_stream->codec;
  output_codec_context = output_stream->codec;

  output_codec_context->codec_id = input_codec_context->codec_id;
  output_codec_context->codec_type = input_codec_context->codec_type;
  output_codec_context->codec_tag = input_codec_context->codec_tag;
  output_codec_context->bit_rate = input_codec_context->bit_rate;
  output_codec_context->extradata = input_codec_context->extradata;
  output_codec_context->extradata_size = input_codec_context->extradata_size;

//   output_codec_context->rc_max_rate    = input_codec_context->rc_max_rate;
//   output_codec_context->rc_buffer_size = input_codec_context->rc_buffer_size;

  if((av_q2d(input_codec_context->time_base) * input_codec_context->ticks_per_frame > av_q2d(input_stream->time_base)) && 
     (av_q2d(input_stream->time_base) < 1.0/1000)) {
    output_codec_context->time_base = input_codec_context->time_base;
    output_codec_context->time_base.num *= input_codec_context->ticks_per_frame;

  }
  else {
    output_codec_context->time_base = input_stream->time_base;
  }

  switch (input_codec_context->codec_type) {
  case AVMEDIA_TYPE_AUDIO:
    output_codec_context->channel_layout = input_codec_context->channel_layout;
    output_codec_context->sample_rate = input_codec_context->sample_rate;
    output_codec_context->channels = input_codec_context->channels;
    output_codec_context->frame_size = input_codec_context->frame_size;
    if ((input_codec_context->block_align == 1 && input_codec_context->codec_id == CODEC_ID_MP3) || input_codec_context->codec_id == CODEC_ID_AC3) {
      output_codec_context->block_align = 0;
    }
    else {
      output_codec_context->block_align = input_codec_context->block_align;
    }
    break;
  case AVMEDIA_TYPE_VIDEO:
    output_codec_context->pix_fmt = input_codec_context->pix_fmt;
    output_codec_context->width = input_codec_context->width;
    output_codec_context->height = input_codec_context->height;
    output_codec_context->has_b_frames = input_codec_context->has_b_frames;
    
    if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER) {
      output_codec_context->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
    break;
  default:
    break;
  }

  return output_stream;
}

int ChainsawSink::writeIndexFile(bool end)
{
  std::string index_filename_base = this->hlsLocation + "/" + this->hlsIndexname;
  FILE *index_fp;
  char *write_buf;

  std::string index_filename = index_filename_base + ".m3u8";
  std::string index_filename_tmp = index_filename + ".tmp";

  GST_DEBUG("opening index file %s", index_filename_tmp.c_str());
  index_fp = fopen(index_filename_tmp.c_str(), "w");
  if (! index_fp) {
    GST_LOG("Could not open temporary m3u8 index file (%s), no index file will be created\n", index_filename_tmp.c_str());
    return -1;
  }

  write_buf = (char*)malloc(sizeof(char) * 1024);
  if (!write_buf) {
    GST_LOG("Could not allocate write buffer for index file, index file will be invalid\n");
    fclose(index_fp);
    return -1;
  }
        
  snprintf(write_buf, 1024, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:%u\n", (unsigned int)this->segmentDuration);

  if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
    GST_LOG("Could not write to m3u8 index file, will not continue writing to index file\n");
    free(write_buf);
    fclose(index_fp);
    return -1;
  }

  // Iterator on the segment list to write all ts files
  for (std::list<TsSegment>::iterator listIterator = segmentsList->begin(); listIterator != segmentsList->end(); listIterator++)
  {
    snprintf(write_buf, 1024, "#EXTINF:%.1f,\n%s%s-%llu.ts\n", listIterator->duration, this->http_prefix.c_str(), this->output_prefix.c_str(), listIterator->index);
    if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
      GST_LOG("Could not write to m3u8 index file, will not continue writing to index file\n");
      free(write_buf);
      fclose(index_fp);
      return -1;
    }
  }

  if (end == true) {
    snprintf(write_buf, 1024, "#EXT-X-ENDLIST\n");
    if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
      GST_LOG("Could not write last file and endlist tag to m3u8 index file\n");
      free(write_buf);
      fclose(index_fp);
      return -1;
    }
  }

  free(write_buf);
  fclose(index_fp);

  GST_DEBUG("renaming from %s to %s", index_filename_tmp.c_str(), index_filename.c_str());

  return rename(index_filename_tmp.c_str(), index_filename.c_str());
}

int ChainsawSink::writeIndexQuickstartFiles()
{
  FILE *index_fp;
  char *write_buf;
  std::string qs_index_filename_base = this->hlsLocation + "/" + this->hlsIndexname;

  for (int i = 0; i <= 3; i++) {
    char intStr[2];
    snprintf(intStr, sizeof(intStr), "%d", i);
    std::string qs_index_filename = qs_index_filename_base + "-seq" + intStr + ".m3u8";
    std::string qs_index_key = "/hls/noencoding/" + this->hlsIndexname + ".m3u8?qsseg=" + intStr;
    std::string qs_index_value;
    std::string qs_index_filename_tmp = qs_index_filename + ".tmp";

    GST_DEBUG("opening index file %s", qs_index_filename_tmp.c_str());
    index_fp = fopen(qs_index_filename_tmp.c_str(), "w");
    if (!index_fp) {
      GST_LOG("Could not open temporary m3u8 index file (%s), no index file will be created\n", qs_index_filename_tmp.c_str());
      return -1;
    }

    write_buf = (char*)malloc(sizeof(char) * 1024);
    if (!write_buf) {
      GST_LOG("Could not allocate write buffer for index file, index file will be invalid: %s\n", strerror(errno));
      fclose(index_fp);
      return -1;
    }
        
    snprintf(write_buf, 1024, "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:%d\n#EXT-X-TARGETDURATION:%u\n", this->media_sequence_td, (unsigned int)floor(this->segmentDuration * 2));

    qs_index_value += write_buf;
    if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
      GST_LOG("Could not write to m3u8 index file, will not continue writing to index file: %s\n", strerror(errno));
      free(write_buf);
      fclose(index_fp);
      return -1;
    }

    // Iterator on the segment list to write all ts files
    std::list<TsSegment>::iterator listIterator = this->segmentsList->begin();
    for (int j = 0; j < 3 - i; j++) {
	    listIterator++;
    }
    int nb_frag = 0;
    while ((listIterator != this->segmentsList->end()) && nb_frag < (3 - i))
    {
      snprintf(write_buf, 1024, "#EXTINF:%.1f,\n%s%s-%llu.ts\n", listIterator->duration, this->http_prefix.c_str(), this->output_prefix.c_str(), listIterator->index);
      if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
        GST_LOG("Could not write to m3u8 index file, will not continue writing to index file\n");
        free(write_buf);
        fclose(index_fp);
        return -1;
      }
      qs_index_value += write_buf;
      listIterator++;
      nb_frag++;
    }

    // Iterator on the quickstart segment list to write all ts files
    std::list<TsSegment>::iterator listQsIterator = this->qsSegmentsList->begin();
    for (int j = 0; j < this->qsSegmentsList->size() - i; j++) {
      listQsIterator++;
    }
    while (listQsIterator != this->qsSegmentsList->end())
    {
      snprintf(write_buf, 1024, "#EXTINF:%.1f,\n%s%s-%llu.ts\n", listQsIterator->duration, this->http_prefix.c_str(), this->output_prefix_td.c_str(), listQsIterator->index);
      if (fwrite(write_buf, strlen(write_buf), 1, index_fp) != 1) {
        GST_LOG("Could not write to m3u8 index file, will not continue writing to index file\n");
        free(write_buf);
        fclose(index_fp);
        return -1;
      }
      qs_index_value += write_buf;
      listQsIterator++;
    }

    free(write_buf);
    fclose(index_fp);

    GST_DEBUG("renaming from %s to %s", qs_index_filename_tmp.c_str(), qs_index_filename.c_str());
    rename(qs_index_filename_tmp.c_str(), qs_index_filename.c_str());

    if (this->memcacheEnable == true) {
      GST_DEBUG("inserting key='%s' with value='%s' in memcached\n", qs_index_key.c_str(), qs_index_value.c_str());
      memcached_return_t mrc = memcached_set(this->memc, qs_index_key.c_str(), qs_index_key.length(), qs_index_value.c_str(), qs_index_value.length(), (time_t)0, (uint32_t)0);
      if (mrc != MEMCACHED_SUCCESS)
      {
        GST_ERROR("cannot insert m3u8 file in memcached: %s\n", strerror(errno));
      }
    }
  }
  if (this->varnishEnable == true)
  {
    std::string varnish_url_to_ban = this->varnishBaseHttpUrl;
    std::string varnish_path = "/hls/noencoding/" + this->hlsIndexname + ".m3u8";
    int retry = 0;

    // If an error occured during connecting to the varnish server, retry 3 times, and then skip
    int rc;
    do {
      rc = this->varnishBanAllUrls(this->varnishBaseHttpUrl, varnish_path);
      retry++;
    } while ((rc != 0) && (retry < 3));
  }

  return 0;
}

/* ffmpeg thread for processing MPEG2/TS split
 */
void ChainsawSink::run() 
{
  GST_INFO("running chainsawink for %s", this->hlsIndexname.c_str());

  this->pthreadId = pthread_self();

  //
  // check
  if ((this->tsIn == "") || (this->hlsIndexname == "") || (this->hlsLocation == ""))
  {
    GST_ERROR("Temporary chainsaw file is NULL, can't continue...\n");
    this->end = true;
    return;
  }

  std::thread t1(monitor_stream, this);

  GST_DEBUG("tsIn: '%s'", this->tsIn.c_str());
  GST_DEBUG("hlsLocation: '%s'", this->hlsLocation.c_str());
  GST_DEBUG("hlsIndexname: '%s'", this->hlsIndexname.c_str());
  GST_DEBUG("hlsHttpurl: '%s'", this->hlsHttpurl.c_str());
  GST_DEBUG("format: '%s'", this->format.c_str());
  GST_DEBUG("extension: '%s'", this->extension.c_str());
  GST_DEBUG("audioOnly: '%s'", this->audioOnly ? "yes" : "no");

  struct timeval tv;
  gettimeofday(&tv, NULL);
  this->output_index.index = (unsigned long long)(tv.tv_sec * 1000) + (unsigned long long)(tv.tv_usec / 1000);
  this->output_index.duration = 0;
  this->first_segment = 1;
  this->last_segment = 1;
  this->index = this->hlsLocation + "/" + this->hlsIndexname + ".m3u8";
  this->tmp_index = this->index + ".tmp";  
  this->http_prefix = this->hlsHttpurl;
  this->output_prefix = this->hlsIndexname;
  this->output_prefix_td = this->hlsIndexname + "-td";
  this->output_filename_prefix = this->hlsLocation + "/" + this->output_prefix;
  this->output_filename_prefix_td = this->hlsLocation + "/" + this->output_prefix_td;

  GST_DEBUG("output_index: '%llu'", this->output_index.index);
  GST_DEBUG("first_segment: '%lu'", this->first_segment);
  GST_DEBUG("last_segment: '%lu'", this->last_segment);
  GST_DEBUG("output_prefix: '%s'", this->output_prefix.c_str());
  GST_DEBUG("index: '%s'", this->index.c_str());
  GST_DEBUG("index_tmp: '%s'", this->tmp_index.c_str());
  GST_DEBUG("http_prefix: '%s'", this->http_prefix.c_str());
  GST_DEBUG("output_prefix: '%s'", this->output_prefix.c_str());
  GST_DEBUG("output_filename_prefix: '%s'", this->output_filename_prefix.c_str());

  this->output_filename_size = 1024;
  this->output_filename = (char*)malloc(this->output_filename_size);
  bzero(this->output_filename, this->output_filename_size);

  while (! this->end && this->convert())
  {
    GST_WARNING("ffmpeg fail to encode: reinit");
  }

  initialized = FALSE;

  this->terminate = true;
  GST_INFO("chainsawsink encoding thread ending for %s", this->hlsIndexname.c_str());

  t1.join();
  pthread_exit(NULL);
}

void ChainsawSink::stop()
{
  // wait for index end tag is written 
  GST_INFO("stop chainsawsink for %s", this->hlsIndexname.c_str());
  this->end = true;  
}

int ChainsawSink::convert()
{
  int rc;
  AVFormatContext *oc;
  AVInputFormat *ifmt;
  AVOutputFormat *ofmt;
  AVStream *video_st = NULL;
  AVStream *audio_st = NULL;
  int video_index = -1;
  int audio_index = -1;
  unsigned int i;
  AVCodec *codec;

  uint64_t max_tsfiles = -1;
  uint64_t max_tsfiles_td;
  int decode_done;
  double prev_segment_time = 0;
  double current_time = 0;
  int ret;
  bool first_key_frame;

  GST_INFO("Starting conversion of the stream");

  if (this->qsEnable == true) {
    max_tsfiles = this->recordBuffer / this->segmentDuration;
    max_tsfiles_td = this->recordBuffer / this->quickstartTargetDuration;
  }
  this->media_sequence = 0;
  this->media_sequence_td = 3;
  GST_DEBUG("open input file %s", this->tsIn.c_str());
  ifmt = av_find_input_format("mpegts");
  this->ctx = avformat_alloc_context();
  rc = avformat_open_input(&this->ctx, this->tsIn.c_str(), ifmt, NULL);

  if ((rc < 0) || (!this->ctx)) {
    GST_ERROR("Could not read stream information\n");
    this->end = true;
    return rc;
  }

  if (avformat_find_stream_info(this->ctx, NULL) < 0) {
    GST_ERROR("Could not read stream information\n");
    this->end = true;
    return rc;
  }

  ofmt = av_guess_format(this->format.c_str(), NULL, NULL);
  if (!ofmt) {
    GST_ERROR("Could not find MPEG-TS muxer\n");
    this->end = true;
    return -1;
  }
  oc = avformat_alloc_context();
  if (!oc) {
    GST_ERROR("Could not allocated output context");
    this->end = true;
    return -1;
  }
  oc->oformat = ofmt;

  for (i = 0; i < this->ctx->nb_streams && (video_index < 0 || audio_index < 0); i++)
  {
    switch (this->ctx->streams[i]->codec->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
      if (this->audioOnly == false)
      {
        GST_INFO("configure video stream");
        video_index = i;
        this->ctx->streams[i]->discard = AVDISCARD_NONE;
        video_st = add_output_stream(oc, this->ctx->streams[i]);
        if (! video_st) {
          GST_ERROR("cannot add_output_stream video_st == NULL: %s", strerror(errno));
          GST_ERROR("quit convert");
          this->end = true;
          return -1;
        }
      }
      break;
    case AVMEDIA_TYPE_AUDIO:
      GST_INFO("configure audio stream");
      audio_index = i;
      this->ctx->streams[i]->discard = AVDISCARD_NONE;
      audio_st = add_output_stream(oc, this->ctx->streams[i]);
      if (! audio_st) {
        GST_ERROR("cannot add_output_stream audio_st == NULL: %s", strerror(errno));
        GST_ERROR("quit convert");
        this->end = true;
        return -1;
      }
      break;
    default:
      this->ctx->streams[i]->discard = AVDISCARD_ALL;
      break;
    }
  }

#ifndef NDEBUG
  // dump_format(oc, 0, this->output_filename_prefix.c_str(), 1);
#endif

  if (this->audioOnly == false)
  {
    codec = avcodec_find_decoder(video_st->codec->codec_id);
    if (!codec) {
      GST_LOG("Could not find video decoder, key frames will not be honored\n");
    }
    
    if (avcodec_open2(video_st->codec, codec, NULL) < 0) {
      GST_LOG("Could not open video decoder, key frames will not be honored\n");
    }
  }

  snprintf(this->output_filename, this->output_filename_size, "%s-%llu.ts", this->output_filename_prefix.c_str(), this->output_index.index);

  GST_DEBUG("output_filename is %s", this->output_filename);
  if (avio_open(&oc->pb, this->output_filename, AVIO_FLAG_WRITE) < 0) {
    GST_ERROR("Could not open '%s'\n", this->output_filename);
    this->end = true;
    return -1;
  }

  if (avformat_write_header(oc, NULL)) {
    GST_ERROR("Could not write mpegts header to first output file\n");
    this->end = true;
    return -1;
  }

  double segment_time = 0;
  do {
    AVPacket packet;

    decode_done = av_read_frame(this->ctx, &packet);
    if ((this->end == true) || (decode_done < 0)) {
      GST_ERROR("couldn't decode frame");
      rc = 0;
      break;
    }

    if (av_dup_packet(&packet) < 0) {
      GST_LOG("Could not duplicate packet");
      av_free_packet(&packet);
      rc = 0;
      break;
    }

    if (this->audioOnly == true) {
      if (packet.stream_index == audio_index) {
        segment_time = (double)av_stream_get_end_pts(audio_st) * audio_st->time_base.num / audio_st->time_base.den;
      }
    }
    else {
      if (packet.stream_index == audio_index) {
        current_time = (double)av_stream_get_end_pts(audio_st) * audio_st->time_base.num / audio_st->time_base.den;
        last_audio_packet_time = time(NULL);
      }
      if (packet.stream_index == video_index) {
        current_time = (double)av_stream_get_end_pts(video_st) * video_st->time_base.num / video_st->time_base.den;
        if (packet.flags & AV_PKT_FLAG_KEY) {
          this->last_video_packet_time = time(NULL);
          if (segment_time == 0) {
            segment_time = (double)av_stream_get_end_pts(video_st) * video_st->time_base.num / video_st->time_base.den;
            //prev_segment_time = segment_time;
            first_key_frame = true;
          }
          else
            segment_time = (double)av_stream_get_end_pts(video_st) * video_st->time_base.num / video_st->time_base.den;
          GST_DEBUG("Found a keyframe at segment_time %f prev_segment_time is %f and segment_duration is %f\n", segment_time, prev_segment_time, this->segmentDuration);
        }
      }
    }

    if ((segment_time - prev_segment_time >= (this->segmentDuration - 0.01)) || (first_key_frame == true)) {
      avio_flush(oc->pb);
      avio_close(oc->pb);

      if (this->memcacheEnable == true) {
        // mmaping new ts file
        char *memblock;
        struct stat sb;
        char index[64];
        snprintf(index, sizeof(index), "%llu", this->output_index.index);
        std::string ts_url_key = "/hls/noencoding/" + this->output_prefix + "-" + index + ".ts";

        int fd = open(this->output_filename, O_RDONLY);
        fstat(fd, &sb);
        memblock = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
        GST_DEBUG("inserting key='%s' with binary data in memcached", ts_url_key.c_str());
        memcached_return_t mrc = memcached_set(this->memc, ts_url_key.c_str(), ts_url_key.length() , memblock, sb.st_size, (time_t)0, (uint32_t)0);
        if (mrc != MEMCACHED_SUCCESS)
        {
          GST_ERROR("cannot insert ts file in memcached: %s", strerror(errno));
        }
        munmap(memblock, sb.st_size);
        close(fd);
      }

      if (first_key_frame == false) {
        this->segments_count++;
        GST_DEBUG("segment_time is %f, prev_segment_time is %f, this->segmentDuration is %f", segment_time, prev_segment_time, this->segmentDuration);

        if ((max_tsfiles != -1) && (this->segmentsList->size() >= max_tsfiles)) {
          TsSegment tsSeg = this->segmentsList->front();
          char index[64];
          snprintf(index, sizeof(index), "%llu", tsSeg.index);
          if (this->memcacheEnable == true) {
            std::string ts_url_key = "/hls/noencoding/" + this->output_prefix + "-" + index + ".ts";
            memcached_return_t mrc = memcached_delete(this->memc, ts_url_key.c_str(), ts_url_key.length(), 0);
            if (mrc != MEMCACHED_SUCCESS) {
              GST_ERROR("cannot delete key '%s' from memcached: %s", ts_url_key.c_str(), strerror(errno));
            }
          }
          std::string filename_to_delete = this->output_filename_prefix + "-" + index + ".ts";
          unlink(filename_to_delete.c_str());
          this->segmentsList->pop_front();
          this->media_sequence++;
        }
        this->output_index.duration = segment_time - prev_segment_time;
        this->segmentsList->push_back(this->output_index);
        int absoluteSegmentDuration = (this->output_index.duration > (floor(this->output_index.duration) + 0.5f)) ? ceil(this->output_index.duration) : floor(this->output_index.duration);
        GST_DEBUG("absoluteSegmentDuration is %d, output_index.duration is %f\n", absoluteSegmentDuration, this->output_index.duration);
        if (this->qsEnable == true) {
          this->aggregateSegmentsList->push_back(this->output_index);

          if (this->segments_count && (this->segments_count % this->quickstartAggregateSegmentsCount) == 0) {
            GST_DEBUG("Aggregating all segments to duration");
            char segmentFileName[1024];
            char segmentFileNameDest[1024];
            snprintf(segmentFileNameDest, sizeof(segmentFileNameDest), "%s-%llu.ts", this->output_filename_prefix_td.c_str(), aggregateSegmentsList->begin()->index);
            TsSegment seg;
            seg.duration = 0;
            seg.index = aggregateSegmentsList->begin()->index;
            if (this->qsSegmentsList->size() >= max_tsfiles_td) {
              TsSegment tsSeg = this->qsSegmentsList->front();
              char index[64];
              snprintf(index, sizeof(index), "%llu", tsSeg.index);
              if (this->memcacheEnable == true) {
                std::string ts_url_key = "/hls/noencoding/" + this->output_prefix_td + "-" + index + ".ts";
                memcached_return_t mrc = memcached_delete(this->memc, ts_url_key.c_str(), ts_url_key.length(), 0);
                if (mrc != MEMCACHED_SUCCESS) {
                  GST_ERROR("cannot delete key '%s' from memcached: %s", ts_url_key.c_str(), strerror(errno));
                }
              }
              std::string filename_to_delete = this->output_filename_prefix_td + "-" + index + ".ts";
              unlink(filename_to_delete.c_str());
              this->qsSegmentsList->pop_front();
              this->media_sequence_td++;
            }
            GST_DEBUG("Opening destination segment file %s", segmentFileNameDest);
            int fdest = open(segmentFileNameDest, O_WRONLY | O_CREAT, 0644);
            char *file_buffer = (char *)malloc(32100);
            while (! aggregateSegmentsList->empty())
            {
              seg.duration += aggregateSegmentsList->front().duration;
              snprintf(segmentFileName, sizeof(segmentFileName), "%s-%llu.ts", this->output_filename_prefix.c_str(), aggregateSegmentsList->front().index);
              GST_DEBUG("Opening source segment file %s", segmentFileName);
              int fd = open(segmentFileName, O_RDONLY);
              if (fd != -1) {
                ssize_t size_read;
                while ((size_read = read(fd, file_buffer, 32000))) {
                  GST_DEBUG("Read %lu bytes of source file name", size_read);
                  write(fdest, file_buffer, size_read);
                  GST_DEBUG("Wrote %lu bytes on destination file name", size_read);
                }
                close(fd);
              }
              else
                GST_ERROR("OOps ! Cannot open source file name %s: %s", segmentFileName, strerror(errno));
              aggregateSegmentsList->pop_front();
            }
            close(fdest);
            free(file_buffer);
  
            if (this->memcacheEnable == true) {
              // mmaping new ts file
              char *memblock;
              struct stat sb;
              char index[64];
              snprintf(index, sizeof(index), "%llu", seg.index);
              std::string ts_url_key = "/hls/noencoding/" + this->output_prefix_td + "-" + index + ".ts";
  
              int fd = open(this->output_filename, O_RDONLY);
              fstat(fd, &sb);
              memblock = (char *)mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0); 
              GST_DEBUG("inserting key='%s' with binary data in memcached", ts_url_key.c_str());
              memcached_return_t mrc = memcached_set(this->memc, ts_url_key.c_str(), ts_url_key.length() , memblock, sb.st_size, (time_t)0, (uint32_t)0);
              if (mrc != MEMCACHED_SUCCESS)
              {
                GST_ERROR("cannot insert ts file in memcached: %s", strerror(errno));
              }
              munmap(memblock, sb.st_size);
              close(fd);
            }
            qsSegmentsList->push_back(seg);
  
            if (this->qsSegmentsList->size() == max_tsfiles_td)
              this->writeIndexQuickstartFiles();
          }
          GST_DEBUG("Calling creation of writeIndexFile");
          this->writeIndexFile(false);
        }
        else {
          if (this->qsEnable == true) {
            this->segmentDuration = floor(segment_time - prev_segment_time);
          }
          this->writeIndexFile(false);
        }
      }  
      else {
        first_key_frame = false;
        unlink(this->output_filename);
      }

      this->output_index.index += (unsigned long long)(segment_time * 1000);
  
      snprintf(this->output_filename, this->output_filename_size, "%s-%llu.ts", this->output_filename_prefix.c_str(), this->output_index.index);

      GST_DEBUG("open %s", this->output_filename);
      if (avio_open(&oc->pb, this->output_filename, AVIO_FLAG_WRITE) < 0) {
        GST_LOG("Could not open '%s'", this->output_filename);
        rc = 0;
        break;
      }

      prev_segment_time = segment_time;
    }

    ret = av_interleaved_write_frame(oc, &packet);
    if (ret < 0) {
      GST_WARNING("Could not write frame interleaved, Discontinuity ?");
    }
    else if (ret > 0) {
      GST_LOG("End of stream requested");
      av_free_packet(&packet);
      rc = 0;
      break;
    }

    av_free_packet(&packet);
  } while (!decode_done && !this->end);

  GST_INFO("decode done for %s", this->hlsIndexname.c_str());
  GST_INFO("segment_time is %f, prev_segment_time is %f", segment_time, prev_segment_time);

  // If there is a remain ts, add it to segmentList XXX do it for QS and Live too
  if (segment_time > prev_segment_time) {
    avio_flush(oc->pb);
    this->output_index.duration = segment_time - prev_segment_time;
    this->segmentsList->push_back(this->output_index);

    // do it for qsSegmentsList
    if (this->qsEnable == true) {
      this->qsSegmentsList->push_back(output_index);
    }
  }

  av_write_trailer(oc);

  GST_DEBUG("cleaning ffmpeg context");
  if ((this->audioOnly == false) && video_st->codec)
  {
    avcodec_close(video_st->codec);
  }

  for(i = 0; i < oc->nb_streams; i++) {
    av_freep(&oc->streams[i]->metadata);      
    av_freep(&oc->streams[i]->index_entries);
    av_freep(&oc->streams[i]->priv_data);
    av_freep(&oc->streams[i]->codec);
    av_freep(&oc->streams[i]);
  }
  av_freep(&oc->priv_data);
  avio_close(oc->pb);
  av_freep(&oc);

  if (this->qsEnable == false) {
    this->writeIndexFile(true);
  }

  return rc;
}

}

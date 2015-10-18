extern "C"
{
#include <assert.h>
#include <stdio.h>
#include <expat.h>
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <mp4split/mp4_io.h>
#include <mp4split/mp4_fragment.h>
#include <mp4split/moov.h>
#include <mp4split/output_bucket.h>
#include <mp4split/output_ismc.h>
#include <mp4split/output_ismv.h>
#include <mp4split/get_uuid.h>
}

#include <mp4split/util.hh>

struct uuid1_t * uuid1Video[100];
struct uuid1_t * uuid1Audio[100];

static void generate_ismv(const char * mp4_file, const char * ismv_file)
{
  struct bucket_t * buckets = 0;
  mp4_context_t * mp4_context = 0;
  uint64_t filesize = 0;

  filesize = get_filesize(mp4_file);
  mp4_context = mp4_open(mp4_file, filesize, MP4_OPEN_ALL, 0);
  mp4_fragment_file(mp4_context, &buckets, 0, 0); // without uuid
  buckets_write(ismv_file, buckets, mp4_file);
  buckets_exit(buckets);
  buckets = 0;
  mp4_close(mp4_context);
}

static void generate_uuid(const char * ismv_file)
{
  mp4_context_t * ismv_context = 0;
  uint64_t filesize = 0;
  uint64_t last_video_pts = 0;
  uint64_t last_audio_pts = 0;

  filesize = get_filesize(ismv_file);
  ismv_context = mp4_open(ismv_file, filesize, MP4_OPEN_ALL, 0);
  assert (get_uuid(ismv_context,
                   &uuid1Video[0], &uuid1Audio[0], 
                   &last_video_pts, &last_audio_pts) == 0);
  mp4_close(ismv_context);
}

int main(int argc, char ** argv)
{
  generate_ismv(argv[1], argv[2]);
  generate_uuid(argv[2]);
}

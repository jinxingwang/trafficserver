/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "ink_config.h"
#include "ink_file.h"
#include "ink_unused.h"
#include "I_Layout.h"
#include "I_Version.h"

// Includes and namespaces etc.
#include "LogStandalone.cc"

#include "LogObject.h"
#include "hdrs/HTTP.h"

#include <math.h>
#include <sys/utsname.h>
#if defined(solaris)
#include <sys/types.h>
#include <unistd.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <string>
#include <algorithm>
#include <vector>
#if (__GNUC__ >= 3)
#define _BACKWARD_BACKWARD_WARNING_H    // needed for gcc 4.3
#include <ext/hash_map>
#include <ext/hash_set>
#undef _BACKWARD_BACKWARD_WARNING_H
#else
#include <hash_map>
#include <hash_set>
#include <map>
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#include <fcntl.h>

#if defined(__GNUC__)
using namespace __gnu_cxx;
#endif
using namespace std;

// Constants, please update the VERSION number when you make a new build!!!
#define PROGRAM_NAME		"traffic_logstats"

const int MAX_LOGBUFFER_SIZE = 65536;
const int DEFAULT_LINE_LEN = 78;
const double LOG10_1024 = 3.0102999566398116;


// Optimizations for "strcmp()", treat some fixed length (3 or 4 bytes) strings
// as integers.
const int GET_AS_INT = 5522759;
const int PUT_AS_INT = 5526864;
const int HEAD_AS_INT = 1145128264;
const int POST_AS_INT = 1414745936;

const int TEXT_AS_INT = 1954047348;

const int JPEG_AS_INT = 1734701162;
const int JPG_AS_INT = 6778986;
const int GIF_AS_INT = 6711655;
const int PNG_AS_INT = 6778480;
const int BMP_AS_INT = 7368034;
const int CSS_AS_INT = 7566179;
const int XML_AS_INT = 7105912;
const int HTML_AS_INT = 1819112552;
const int ZIP_AS_INT = 7367034;

const int JAVA_AS_INT = 1635148138;     // For "javascript"
const int X_JA_AS_INT = 1634348408;     // For "x-javascript"
const int RSSp_AS_INT = 728986482;      // For "RSS+"
const int PLAI_AS_INT = 1767992432;     // For "plain"
const int IMAG_AS_INT = 1734438249;     // For "image"
const int HTTP_AS_INT = 1886680168;     // For "http" followed by "s://" or "://"

/*
#include <stdio.h>
int main(int argc, char** argv) {
  printf("%s is %d\n", argv[1], *((int*)argv[1]));
}
*/


// Store our "state" (position in log file etc.)
struct LastState
{
  off_t offset;
  ino_t st_ino;
};
LastState last_state;


// Store the collected counters and stats, per Origin Server (or total)
struct StatsCounter
{
  int64 count;
  int64 bytes;
};

struct ElapsedStats
{
  int min;
  int max;
  float avg;
  float stddev;
};

struct OriginStats
{
  char *server;
  StatsCounter total;

  struct
  {
    struct
    {
      ElapsedStats hit;
      ElapsedStats ims;
      ElapsedStats refresh;
      ElapsedStats other;
      ElapsedStats total;
    } hits;
    struct
    {
      ElapsedStats miss;
      ElapsedStats ims;
      ElapsedStats refresh;
      ElapsedStats other;
      ElapsedStats total;
    } misses;
  } elapsed;

  struct
  {
    struct
    {
      StatsCounter hit;
      StatsCounter ims;
      StatsCounter refresh;
      StatsCounter other;
      StatsCounter total;
    } hits;
    struct
    {
      StatsCounter miss;
      StatsCounter ims;
      StatsCounter refresh;
      StatsCounter other;
      StatsCounter total;
    } misses;
    struct
    {
      StatsCounter client_abort;
      StatsCounter connect_fail;
      StatsCounter invalid_req;
      StatsCounter unknown;
      StatsCounter other;
      StatsCounter total;
    } errors;
    StatsCounter other;
  } results;

  struct
  {
    StatsCounter c_000;         // Bad
    StatsCounter c_100;
    StatsCounter c_200;
    StatsCounter c_201;
    StatsCounter c_202;
    StatsCounter c_203;
    StatsCounter c_204;
    StatsCounter c_205;
    StatsCounter c_206;
    StatsCounter c_2xx;
    StatsCounter c_300;
    StatsCounter c_301;
    StatsCounter c_302;
    StatsCounter c_303;
    StatsCounter c_304;
    StatsCounter c_305;
    StatsCounter c_307;
    StatsCounter c_3xx;
    StatsCounter c_400;
    StatsCounter c_401;
    StatsCounter c_402;
    StatsCounter c_403;
    StatsCounter c_404;
    StatsCounter c_405;
    StatsCounter c_406;
    StatsCounter c_407;
    StatsCounter c_408;
    StatsCounter c_409;
    StatsCounter c_410;
    StatsCounter c_411;
    StatsCounter c_412;
    StatsCounter c_413;
    StatsCounter c_414;
    StatsCounter c_415;
    StatsCounter c_416;
    StatsCounter c_417;
    StatsCounter c_4xx;
    StatsCounter c_500;
    StatsCounter c_501;
    StatsCounter c_502;
    StatsCounter c_503;
    StatsCounter c_504;
    StatsCounter c_505;
    StatsCounter c_5xx;
  } codes;

  struct
  {
    StatsCounter direct;
    StatsCounter none;
    StatsCounter sibling;
    StatsCounter parent;
    StatsCounter empty;
    StatsCounter invalid;
    StatsCounter other;
  } hierarchies;

  struct
  {
    StatsCounter http;
    StatsCounter https;
    StatsCounter none;
    StatsCounter other;
  } schemes;

  struct
  {
    StatsCounter options;
    StatsCounter get;
    StatsCounter head;
    StatsCounter post;
    StatsCounter put;
    StatsCounter del;
    StatsCounter trace;
    StatsCounter connect;
    StatsCounter purge;
    StatsCounter none;
    StatsCounter other;
  } methods;

  struct
  {
    struct
    {
      StatsCounter plain;
      StatsCounter xml;
      StatsCounter html;
      StatsCounter css;
      StatsCounter javascript;
      StatsCounter other;
      StatsCounter total;
    } text;
    struct
    {
      StatsCounter jpeg;
      StatsCounter gif;
      StatsCounter png;
      StatsCounter bmp;
      StatsCounter other;
      StatsCounter total;
    } image;
    struct
    {
      StatsCounter shockwave_flash;
      StatsCounter quicktime;
      StatsCounter javascript;
      StatsCounter zip;
      StatsCounter other;
      StatsCounter rss_xml;
      StatsCounter rss_atom;
      StatsCounter rss_other;
      StatsCounter total;
    } application;
    struct
    {
      StatsCounter wav;
      StatsCounter mpeg;
      StatsCounter other;
      StatsCounter total;
    } audio;
    StatsCounter none;
    StatsCounter other;
  } content;
};


///////////////////////////////////////////////////////////////////////////////
// Equal operator for char* (for the hash_map)
struct eqstr
{
  inline bool operator() (const char *s1, const char *s2) const
  {
    return strcmp(s1, s2) == 0;
  }
};

typedef hash_map <const char *, OriginStats *, hash <const char *>, eqstr> OriginStorage;
typedef hash_set <const char *, hash <const char *>, eqstr> OriginSet;


///////////////////////////////////////////////////////////////////////////////
// Globals, holding the accumulated stats (ok, I'm lazy ...)
static OriginStats totals;
static OriginStorage origins;
static OriginSet *origin_set;
static int parse_errors;
static char *hostname;

// Command line arguments (parsing)
static struct
{
  char log_file[1024];
  char origin_file[1024];
  char origin_list[2048];
  int max_origins;
  char state_tag[1024];
  int64 min_hits;
  int max_age;
  int line_len;
  int incremental;              // Do an incremental run
  int tail;                     // Tail the log file
  int summary;                  // Summary only
  int json;			// JSON output
  int cgi;			// CGI output (typically with json)
  int version;
  int help;
} cl;

ArgumentDescription argument_descriptions[] = {
  {"help", 'h', "Give this help", "T", &cl.help, NULL, NULL},
  {"log_file", 'f', "Specific logfile to parse", "S1023", cl.log_file, NULL, NULL},
  {"origin_list", 'o', "Only show stats for listed Origins", "S2047", cl.origin_list, NULL, NULL},
  {"origin_file", 'O', "File listing Origins to show", "S1023", cl.origin_file, NULL, NULL},
  {"max_orgins", 'M', "Max number of Origins to show", "I", &cl.max_origins, NULL, NULL},
  {"incremental", 'i', "Incremental log parsing", "T", &cl.incremental, NULL, NULL},
  {"statetag", 'S', "Name of the state file to use", "S1023", cl.state_tag, NULL, NULL},
  {"tail", 't', "Parse the last <sec> seconds of log", "I", &cl.tail, NULL, NULL},
  {"summary", 's', "Only produce the summary", "T", &cl.summary, NULL, NULL},
  {"json", 'j', "Produce JSON formatted output", "T", &cl.json, NULL, NULL},
  {"cgi", 'c', "Produce HTTP headers suitable as a CGI", "T", &cl.cgi, NULL, NULL},
  {"min_hits", 'm', "Minimum total hits for an Origin", "L", &cl.min_hits, NULL, NULL},
  {"max_age", 'a', "Max age for log entries to be considered", "I", &cl.max_age, NULL, NULL},
  {"line_len", 'l', "Output line length", "I", &cl.line_len, NULL, NULL},
  {"debug_tags", 'T', "Colon-Separated Debug Tags", "S1023", &error_tags, NULL, NULL},
  {"version", 'V', "Print Version Id", "T", &cl.version, NULL, NULL},
};
int n_argument_descriptions = SIZE(argument_descriptions);

static const char *USAGE_LINE =
  "Usage: " PROGRAM_NAME " [-l logfile] [-o origin[,...]] [-O originfile] [-m minhits] [-inshv]";


// Enum for return code levels.
enum ExitLevel
{
  EXIT_OK = 0,
  EXIT_WARNING = 1,
  EXIT_CRITICAL = 2,
  EXIT_UNKNOWN = 3
};

// Enum for parsing a log line
enum ParseStates
{
  P_STATE_ELAPSED,
  P_STATE_IP,
  P_STATE_RESULT,
  P_STATE_CODE,
  P_STATE_SIZE,
  P_STATE_METHOD,
  P_STATE_URL,
  P_STATE_RFC931,
  P_STATE_HIERARCHY,
  P_STATE_PEER,
  P_STATE_TYPE,
  P_STATE_END
};

// Enum for HTTP methods
enum HTTPMethod
{
  METHOD_OPTIONS,
  METHOD_GET,
  METHOD_HEAD,
  METHOD_POST,
  METHOD_PUT,
  METHOD_DELETE,
  METHOD_TRACE,
  METHOD_CONNECT,
  METHOD_PURGE,
  METHOD_NONE,
  METHOD_OTHER
};

// Enum for URL schemes
enum URLScheme
{
  SCHEME_HTTP,
  SCHEME_HTTPS,
  SCHEME_NONE,
  SCHEME_OTHER
};


///////////////////////////////////////////////////////////////////////////////
// Initialize the elapsed field
inline void
init_elapsed(OriginStats & stats)
{
  stats.elapsed.hits.hit.min = -1;
  stats.elapsed.hits.ims.min = -1;
  stats.elapsed.hits.refresh.min = -1;
  stats.elapsed.hits.other.min = -1;
  stats.elapsed.hits.total.min = -1;
  stats.elapsed.misses.miss.min = -1;
  stats.elapsed.misses.ims.min = -1;
  stats.elapsed.misses.refresh.min = -1;
  stats.elapsed.misses.other.min = -1;
  stats.elapsed.misses.total.min = -1;
}

// Update the counters for one StatsCounter
inline void
update_counter(StatsCounter& counter, int size)
{
  counter.count++;
  counter.bytes += size;
}

inline void
update_elapsed(ElapsedStats & stat, const int elapsed, const StatsCounter & counter)
{
  int newcount, oldcount;
  float oldavg, newavg, sum_of_squares;
  // Skip all the "0" values.
  if (elapsed == 0)
    return;
  if (stat.min == -1)
    stat.min = elapsed;
  else if (stat.min > elapsed)
    stat.min = elapsed;

  if (stat.max < elapsed)
    stat.max = elapsed;

  // update_counter should have been called on counter.count before calling
  // update_elapsed.
  newcount = counter.count;
  // New count should never be zero, else there was a programming error.
  assert(newcount);
  oldcount = counter.count - 1;
  oldavg = stat.avg;
  newavg = (oldavg * oldcount + elapsed) / newcount;
  // Now find the new standard deviation from the old one

  if (oldcount != 0)
    sum_of_squares = (stat.stddev * stat.stddev * oldcount);
  else
    sum_of_squares = 0;

  //Find the old sum of squares.
  sum_of_squares = sum_of_squares + 2 * oldavg * oldcount * (oldavg - newavg)
    + oldcount * (newavg * newavg - oldavg * oldavg);

  //Now, find the new sum of squares.
  sum_of_squares = sum_of_squares + (elapsed - newavg) * (elapsed - newavg);

  stat.stddev = sqrt(sum_of_squares / newcount);
  stat.avg = newavg;

}

///////////////////////////////////////////////////////////////////////////////
// Update the "result" and "elapsed" stats for a particular record
inline void
update_results_elapsed(OriginStats * stat, int result, int elapsed, int size)
{
  switch (result) {
  case SQUID_LOG_TCP_HIT:
    update_counter(stat->results.hits.hit, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.hit, elapsed, stat->results.hits.hit);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_MISS:
    update_counter(stat->results.misses.miss, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.miss, elapsed, stat->results.misses.miss);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  case SQUID_LOG_TCP_IMS_HIT:
    update_counter(stat->results.hits.ims, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.ims, elapsed, stat->results.hits.ims);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_IMS_MISS:
    update_counter(stat->results.misses.ims, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.ims, elapsed, stat->results.misses.ims);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  case SQUID_LOG_TCP_REFRESH_HIT:
    update_counter(stat->results.hits.refresh, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.refresh, elapsed, stat->results.hits.refresh);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_REFRESH_MISS:
    update_counter(stat->results.misses.refresh, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.refresh, elapsed, stat->results.misses.refresh);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  case SQUID_LOG_ERR_CLIENT_ABORT:
    update_counter(stat->results.errors.client_abort, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_ERR_CONNECT_FAIL:
    update_counter(stat->results.errors.connect_fail, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_ERR_INVALID_REQ:
    update_counter(stat->results.errors.invalid_req, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_ERR_UNKNOWN:
    update_counter(stat->results.errors.unknown, size);
    update_counter(stat->results.errors.total, size);
    break;
  case SQUID_LOG_TCP_DISK_HIT:
  case SQUID_LOG_TCP_MEM_HIT:
  case SQUID_LOG_TCP_REF_FAIL_HIT:
  case SQUID_LOG_UDP_HIT:
  case SQUID_LOG_UDP_WEAK_HIT:
  case SQUID_LOG_UDP_HIT_OBJ:
    update_counter(stat->results.hits.other, size);
    update_counter(stat->results.hits.total, size);
    update_elapsed(stat->elapsed.hits.other, elapsed, stat->results.hits.other);
    update_elapsed(stat->elapsed.hits.total, elapsed, stat->results.hits.total);
    break;
  case SQUID_LOG_TCP_EXPIRED_MISS:
  case SQUID_LOG_TCP_WEBFETCH_MISS:
  case SQUID_LOG_UDP_MISS:
    update_counter(stat->results.misses.other, size);
    update_counter(stat->results.misses.total, size);
    update_elapsed(stat->elapsed.misses.other, elapsed, stat->results.misses.other);
    update_elapsed(stat->elapsed.misses.total, elapsed, stat->results.misses.total);
    break;
  default:
    if ((result >= SQUID_LOG_ERR_READ_TIMEOUT) && (result <= SQUID_LOG_ERR_UNKNOWN)) {
      update_counter(stat->results.errors.other, size);
      update_counter(stat->results.errors.total, size);
    } else
      update_counter(stat->results.other, size);
    break;
  }
}


///////////////////////////////////////////////////////////////////////////////
// Update the "codes" stats for a particular record
inline void
update_codes(OriginStats * stat, int code, int size)
{
  switch (code) {
  case 100:
    update_counter(stat->codes.c_100, size);
    break;

  // 200's
  case 200:
    update_counter(stat->codes.c_200, size);
    break;
  case 201:
    update_counter(stat->codes.c_201, size);
    break;
  case 202:
    update_counter(stat->codes.c_202, size);
    break;
  case 203:
    update_counter(stat->codes.c_203, size);
    break;
  case 204:
    update_counter(stat->codes.c_204, size);
    break;
  case 205:
    update_counter(stat->codes.c_205, size);
    break;
  case 206:
    update_counter(stat->codes.c_206, size);
    break;

  // 300's
  case 300:
    update_counter(stat->codes.c_300, size);
    break;
  case 301:
    update_counter(stat->codes.c_301, size);
    break;
  case 302:
    update_counter(stat->codes.c_302, size);
    break;
  case 303:
    update_counter(stat->codes.c_303, size);
    break;
  case 304:
    update_counter(stat->codes.c_304, size);
    break;
  case 305:
    update_counter(stat->codes.c_305, size);
    break;
  case 307:
    update_counter(stat->codes.c_307, size);
    break;

  // 400's
  case 400:
    update_counter(stat->codes.c_400, size);
    break;
  case 401:
    update_counter(stat->codes.c_401, size);
    break;
  case 402:
    update_counter(stat->codes.c_402, size);
    break;
  case 403:
    update_counter(stat->codes.c_403, size);
    break;
  case 404:
    update_counter(stat->codes.c_404, size);
    break;
  case 405:
    update_counter(stat->codes.c_405, size);
    break;
  case 406:
    update_counter(stat->codes.c_406, size);
    break;
  case 407:
    update_counter(stat->codes.c_407, size);
    break;
  case 408:
    update_counter(stat->codes.c_408, size);
    break;
  case 409:
    update_counter(stat->codes.c_409, size);
    break;
  case 410:
    update_counter(stat->codes.c_410, size);
    break;
  case 411:
    update_counter(stat->codes.c_411, size);
    break;
  case 412:
    update_counter(stat->codes.c_412, size);
    break;
  case 413:
    update_counter(stat->codes.c_413, size);
    break;
  case 414:
    update_counter(stat->codes.c_414, size);
    break;
  case 415:
    update_counter(stat->codes.c_415, size);
    break;
  case 416:
    update_counter(stat->codes.c_416, size);
    break;
  case 417:
    update_counter(stat->codes.c_417, size);
    break;

  // 500's
  case 500:
    update_counter(stat->codes.c_500, size);
    break;
  case 501:
    update_counter(stat->codes.c_501, size);
    break;
  case 502:
    update_counter(stat->codes.c_502, size);
    break;
  case 503:
    update_counter(stat->codes.c_503, size);
    break;
  case 504:
    update_counter(stat->codes.c_504, size);
    break;
  case 505:
    update_counter(stat->codes.c_505, size);
    break;
  default:
    break;
  }

  if (code >= 500)
    update_counter(stat->codes.c_5xx, size);
  else if (code >= 400)
    update_counter(stat->codes.c_4xx, size);
  else if (code >= 300)
    update_counter(stat->codes.c_3xx, size);
  else if (code >= 200)
    update_counter(stat->codes.c_2xx, size);
  else
    update_counter(stat->codes.c_000, size);
}


///////////////////////////////////////////////////////////////////////////////
// Update the "methods" stats for a particular record
inline void
update_methods(OriginStats * stat, int method, int size)
{
  // We're so loppsided on GETs, so makes most sense to test 'out of order'.
  switch (method) {
  case METHOD_GET:
    update_counter(stat->methods.get, size);
    break;

  case METHOD_OPTIONS:
    update_counter(stat->methods.options, size);
    break;

  case METHOD_HEAD:
    update_counter(stat->methods.head, size);
    break;

  case METHOD_POST:
    update_counter(stat->methods.post, size);
    break;

  case METHOD_PUT:
    update_counter(stat->methods.put, size);
    break;

  case METHOD_DELETE:
    update_counter(stat->methods.del, size);
    break;

  case METHOD_TRACE:
    update_counter(stat->methods.trace, size);
    break;

  case METHOD_CONNECT:
    update_counter(stat->methods.connect, size);
    break;

  case METHOD_PURGE:
    update_counter(stat->methods.purge, size);
    break;

  case METHOD_NONE:
    update_counter(stat->methods.none, size);
    break;

  default:
    update_counter(stat->methods.other, size);
    break;
  }
}


///////////////////////////////////////////////////////////////////////////////
// Update the "schemes" stats for a particular record
inline void
update_schemes(OriginStats * stat, int scheme, int size)
{
  if (scheme == SCHEME_HTTP)
    update_counter(stat->schemes.http, size);
  else if (scheme == SCHEME_HTTPS)
    update_counter(stat->schemes.https, size);
  else if (scheme == SCHEME_NONE)
    update_counter(stat->schemes.none, size);
  else
    update_counter(stat->schemes.other, size);
}


///////////////////////////////////////////////////////////////////////////////
// Parse a log buffer
int
parse_log_buff(LogBufferHeader * buf_header, bool summary = false)
{
  static char *str_buf = NULL;
  static LogFieldList *fieldlist = NULL;

  LogEntryHeader *entry;
  LogBufferIterator buf_iter(buf_header);
  LogField *field;
  OriginStorage::iterator o_iter;
  ParseStates state;

  char *read_from;
  char *tok;
  char *ptr;
  int tok_len;
  int flag = 0;                 // Flag used in state machine to carry "state" forward

  // Parsed results
  int http_code = 0, size = 0, result = 0, hier = 0, elapsed = 0;
  OriginStats *o_stats;
  char *o_server;
  HTTPMethod method;
  URLScheme scheme;


  // Initialize some "static" variables.
  if (str_buf == NULL) {
    str_buf = (char *) xmalloc(LOG_MAX_FORMATTED_LINE);
    if (str_buf == NULL)
      return 0;
  }

  if (!fieldlist) {
    fieldlist = NEW(new LogFieldList);
    ink_assert(fieldlist != NULL);
    bool agg = false;
    LogFormat::parse_symbol_string(buf_header->fmt_fieldlist(), fieldlist, &agg);
  }
  // Loop over all entries
  while ((entry = buf_iter.next())) {
    read_from = (char *) entry + sizeof(LogEntryHeader);
    // We read and skip over the first field, which is the timestamp.
    if ((field = fieldlist->first()))
      read_from += INK_MIN_ALIGN;
    else                        // This shouldn't happen, buffer must be messed up.
      break;

    state = P_STATE_ELAPSED;
    o_stats = NULL;
    o_server = NULL;
    method = METHOD_OTHER;
    scheme = SCHEME_OTHER;

    while ((field = fieldlist->next(field))) {
      switch (state) {
      case P_STATE_ELAPSED:
        state = P_STATE_IP;
        elapsed = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        break;

      case P_STATE_IP:
        state = P_STATE_RESULT;
        // Just skip the IP, we no longer assume it's always the same.
        //
        // TODO address IP logged in text format (that's not good)
        // Warning: This is maybe not IPv6 safe.
        read_from += LogAccess::strlen(read_from);
        break;

      case P_STATE_RESULT:
        state = P_STATE_CODE;
        result = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        if ((result<32) || (result>255)) {
          flag = 1;
          state = P_STATE_END;
        }
        break;

      case P_STATE_CODE:
        state = P_STATE_SIZE;
        http_code = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        if ((http_code<0) || (http_code>999)) {
          flag = 1;
          state = P_STATE_END;
        }
        //printf("CODE == %d\n", http_code);
        break;

      case P_STATE_SIZE:
        // Warning: This is not 64-bit safe, when converting the log format,
        // this needs to be fixed as well.
        state = P_STATE_METHOD;
        size = *((int64 *) (read_from));
        read_from += INK_MIN_ALIGN;
        //printf("Size == %d\n", size)
        break;

      case P_STATE_METHOD:
        state = P_STATE_URL;
        //printf("METHOD == %s\n", read_from);
        flag = 0;

        // Small optimization for common (3-4 char) cases
        switch (*reinterpret_cast <int*>(read_from)) {
        case GET_AS_INT:
          method = METHOD_GET;
          read_from += LogAccess::round_strlen(3 + 1);
          break;
        case PUT_AS_INT:
          method = METHOD_PUT;
          read_from += LogAccess::round_strlen(3 + 1);
          break;
        case HEAD_AS_INT:
          method = METHOD_HEAD;
          read_from += LogAccess::round_strlen(4 + 1);
          break;
        case POST_AS_INT:
          method = METHOD_POST;
          read_from += LogAccess::round_strlen(4 + 1);
          break;
        default:
          tok_len = strlen(read_from);
          if ((tok_len == 5) && (strncmp(read_from, "PURGE", 5) == 0))
            method = METHOD_PURGE;
          else if ((tok_len == 6) && (strncmp(read_from, "DELETE", 6) == 0))
            method = METHOD_DELETE;
          else if ((tok_len == 7) && (strncmp(read_from, "OPTIONS", 7) == 0))
            method = METHOD_OPTIONS;
          else if ((tok_len == 1) && (*read_from == '-')) {
            method = METHOD_NONE;
            flag = 1;           // No method, so no need to parse the URL
          } else {
            ptr = read_from;
            while (*ptr && isupper(*ptr))
              ++ptr;
            // Skip URL if it doesn't look like an HTTP method
            if (*ptr != '\0')
              flag = 1;
          }
          read_from += LogAccess::round_strlen(tok_len + 1);
          break;
        }
        break;

      case P_STATE_URL:
        state = P_STATE_RFC931;

        //printf("URL == %s\n", tok);
        // TODO check for read_from being empty string
        if (flag == 0) {
          tok = read_from;
          if (*reinterpret_cast <int*>(tok) == HTTP_AS_INT) {
            tok += 4;
            if (*tok == ':') {
              scheme = SCHEME_HTTP;
              tok += 3;
              tok_len = strlen(tok) + 7;
            } else if (*tok == 's') {
              scheme = SCHEME_HTTPS;
              tok += 4;
              tok_len = strlen(tok) + 8;
            } else
              tok_len = strlen(tok) + 4;
          } else {
            if (*tok == '/')
              scheme = SCHEME_NONE;
            tok_len = strlen(tok);
          }
          //printf("SCHEME = %d\n", scheme);
          if (*tok == '/')      // This is to handle crazy stuff like http:///origin.com
            tok++;
          ptr = strchr(tok, '/');
          if (ptr && !summary) { // Find the origin
            *ptr = '\0';

            // TODO: If we save state (struct) for a run, we might need to always
            // update the origin data, no matter what the origin_set is.
            if (origin_set ? (origin_set->find(tok) != origin_set->end()) : 1) {
              //printf("ORIGIN = %s\n", tok);
              o_iter = origins.find(tok);
              if (o_iter == origins.end()) {
                o_stats = (OriginStats *) xmalloc(sizeof(OriginStats));
                init_elapsed(*o_stats);
                o_server = xstrdup(tok);
                if (o_stats && o_server) {
                  o_stats->server = o_server;
                  origins[o_server] = o_stats;
                }
              } else
                o_stats = o_iter->second;
            }
          }
        } else {
          // No method given
          if (*read_from == '/')
            scheme = SCHEME_NONE;
          tok_len = strlen(read_from);
        }
        read_from += LogAccess::round_strlen(tok_len + 1);

        // Update the stats so far, since now we have the Origin (maybe)
        update_results_elapsed(&totals, result, elapsed, size);
        update_codes(&totals, http_code, size);
        update_methods(&totals, method, size);
        update_schemes(&totals, scheme, size);
        update_counter(totals.total, size);
        if (o_stats != NULL) {
          update_results_elapsed(o_stats, result, elapsed, size);
          update_codes(o_stats, http_code, size);
          update_methods(o_stats, method, size);
          update_schemes(o_stats, scheme, size);
          update_counter(o_stats->total, size);
        }
        break;

      case P_STATE_RFC931:
        state = P_STATE_HIERARCHY;
        if (*read_from == '-')
          read_from += LogAccess::round_strlen(1 + 1);
        else
          read_from += LogAccess::strlen(read_from);
        break;

      case P_STATE_HIERARCHY:
        state = P_STATE_PEER;
        hier = *((int64 *) (read_from));
        switch (hier) {
        case SQUID_HIER_NONE:
          update_counter(totals.hierarchies.none, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.none, size);
          break;
        case SQUID_HIER_DIRECT:
          update_counter(totals.hierarchies.direct, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.direct, size);
          break;
        case SQUID_HIER_SIBLING_HIT:
          update_counter(totals.hierarchies.sibling, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.sibling, size);
          break;
        case SQUID_HIER_PARENT_HIT:
          update_counter(totals.hierarchies.parent, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.direct, size);
          break;
        case SQUID_HIER_EMPTY:
          update_counter(totals.hierarchies.empty, size);
          if (o_stats != NULL)
            update_counter(o_stats->hierarchies.empty, size);
          break;
        default:
          if ((hier >= SQUID_HIER_EMPTY) && (hier < SQUID_HIER_INVALID_ASSIGNED_CODE)) {
            update_counter(totals.hierarchies.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->hierarchies.other, size);
          } else {
            update_counter(totals.hierarchies.invalid, size);
            if (o_stats != NULL)
              update_counter(o_stats->hierarchies.invalid, size);
          }
          break;
        }
        read_from += INK_MIN_ALIGN;
        break;

      case P_STATE_PEER:
        state = P_STATE_TYPE;
        if (*read_from == '-')
          read_from += LogAccess::round_strlen(1 + 1);
        else
          read_from += LogAccess::strlen(read_from);
        break;

      case P_STATE_TYPE:
        state = P_STATE_END;
        if (*reinterpret_cast <int*>(read_from) == IMAG_AS_INT) {
          update_counter(totals.content.image.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.image.total, size);
          tok = read_from + 6;
          switch (*reinterpret_cast <int*>(tok)) {
          case JPEG_AS_INT:
            tok_len = 10;
            update_counter(totals.content.image.jpeg, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.jpeg, size);
            break;
          case JPG_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.jpeg, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.jpeg, size);
            break;
          case GIF_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.gif, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.gif, size);
            break;
          case PNG_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.png, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.png, size);
            break;
          case BMP_AS_INT:
            tok_len = 9;
            update_counter(totals.content.image.bmp, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.bmp, size);
            break;
          default:
            tok_len = 6 + strlen(tok);
            update_counter(totals.content.image.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.image.other, size);
            break;
          }
        } else if (*reinterpret_cast <int*>(read_from) == TEXT_AS_INT) {
          tok = read_from + 5;
          update_counter(totals.content.text.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.text.total, size);
          switch (*reinterpret_cast <int*>(tok)) {
          case JAVA_AS_INT:
            // TODO verify if really "javascript"
            tok_len = 15;
            update_counter(totals.content.text.javascript, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.javascript, size);
            break;
          case CSS_AS_INT:
            tok_len = 8;
            update_counter(totals.content.text.css, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.css, size);
            break;
          case XML_AS_INT:
            tok_len = 8;
            update_counter(totals.content.text.xml, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.xml, size);
            break;
          case HTML_AS_INT:
            tok_len = 9;
            update_counter(totals.content.text.html, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.html, size);
            break;
          case PLAI_AS_INT:
            tok_len = 10;
            update_counter(totals.content.text.plain, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.plain, size);
            break;
          default:
            tok_len = 5 + strlen(tok);;
            update_counter(totals.content.text.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.text.other, size);
            break;
          }
        } else if (strncmp(read_from, "application", 11) == 0) {
          tok = read_from + 12;
          update_counter(totals.content.application.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.application.total, size);
          switch (*reinterpret_cast <int*>(tok)) {
          case ZIP_AS_INT:
            tok_len = 15;
            update_counter(totals.content.application.zip, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.zip, size);
            break;
          case JAVA_AS_INT:
            tok_len = 22;
            update_counter(totals.content.application.javascript, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.javascript, size);
          case X_JA_AS_INT:
            tok_len = 24;
            update_counter(totals.content.application.javascript, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.application.javascript, size);
            break;
          case RSSp_AS_INT:
            if (strcmp(tok+4, "xml") == 0) {
              tok_len = 19;
              update_counter(totals.content.application.rss_xml, size);
              if (o_stats != NULL)
                update_counter(o_stats->content.application.rss_xml, size);
            } else if (strcmp(tok+4,"atom") == 0) {
              tok_len = 20;
              update_counter(totals.content.application.rss_atom, size);
              if (o_stats != NULL)
                update_counter(o_stats->content.application.rss_atom, size);
            } else {
              tok_len = 12 + strlen(tok);
              update_counter(totals.content.application.rss_other, size);
              if (o_stats != NULL)
                update_counter(o_stats->content.application.rss_other, size);
            }
            break;
          default:
            if (strcmp(tok, "x-shockwave-flash") == 0) {
              tok_len = 29;
              update_counter(totals.content.application.shockwave_flash, size);
              if (o_stats != NULL)
                update_counter(o_stats->content.application.shockwave_flash, size);
            } else if (strcmp(tok, "x-quicktimeplayer") == 0) {
              tok_len = 29;
              update_counter(totals.content.application.quicktime, size);
              if (o_stats != NULL)
                update_counter(o_stats->content.application.quicktime, size);
            } else {
              tok_len = 12 + strlen(tok);
              update_counter(totals.content.application.other, size);
              if (o_stats != NULL)
                update_counter(o_stats->content.application.other, size);
            }
          }
        } else if (strncmp(read_from, "audio", 5) == 0) {
          tok = read_from + 6;
          tok_len = 6 + strlen(tok);
          update_counter(totals.content.audio.total, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.audio.total, size);
          if ((strcmp(tok, "x-wav") == 0) || (strcmp(tok, "wav") == 0)) {
            update_counter(totals.content.audio.wav, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.audio.wav, size);
          } else if ((strcmp(tok, "x-mpeg") == 0) || (strcmp(tok, "mpeg") == 0)) {
            update_counter(totals.content.audio.mpeg, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.audio.mpeg, size);
          } else {
            update_counter(totals.content.audio.other, size);
            if (o_stats != NULL)
              update_counter(o_stats->content.audio.other, size);
          }
        } else if (*read_from == '-') {
          tok_len = 1;
          update_counter(totals.content.none, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.none, size);
        } else {
          tok_len = strlen(read_from);
          update_counter(totals.content.other, size);
          if (o_stats != NULL)
            update_counter(o_stats->content.other, size);
        }
        read_from += LogAccess::round_strlen(tok_len + 1);
        flag = 0;               // We exited this state without errors
        break;

      case P_STATE_END:
        // Nothing to do really
        if (flag) {
          parse_errors++;
        }
        break;
      }
    }
  }

  return 0;
}



///////////////////////////////////////////////////////////////////////////////
// Process a file (FD)
int
process_file(int in_fd, off_t offset, unsigned max_age)
{
  char buffer[MAX_LOGBUFFER_SIZE];
  int nread, buffer_bytes;

  while (true) {
    Debug("logcat", "Reading buffer ...");
    buffer[0] = '\0';

    unsigned first_read_size = 2 * sizeof(unsigned);
    LogBufferHeader *header = (LogBufferHeader *) & buffer[0];

    // Find the next log header, aligning us properly. This is not
    // particularly optimal, but we shouldn't only have to do this
    // once, and hopefully we'll be aligned immediately.
    if (offset > 0) {
      while (true) {
        if (lseek(in_fd, offset, SEEK_SET) < 0)
          return 1;

        // read the first 8 bytes of the header, which will give us the
        // cookie and the version number.
        nread = read(in_fd, buffer, first_read_size);
        if (!nread || nread == EOF) {
          return 0;
        }
        // ensure that this is a valid logbuffer header
        if (header->cookie && (header->cookie == LOG_SEGMENT_COOKIE)) {
          offset = 0;
          break;
        }
        offset++;
      }
      if (!header->cookie) {
        return 0;
      }
    } else {
      nread = read(in_fd, buffer, first_read_size);
      if (!nread || nread == EOF || !header->cookie)
        return 0;

      // ensure that this is a valid logbuffer header
      if (header->cookie != LOG_SEGMENT_COOKIE)
        return 1;
    }

    Debug("logstats", "LogBuffer version %d, current = %d", header->version, LOG_SEGMENT_VERSION);
    if (header->version != LOG_SEGMENT_VERSION)
      return 1;

    // read the rest of the header
    unsigned second_read_size = sizeof(LogBufferHeader) - first_read_size;
    nread = read(in_fd, &buffer[first_read_size], second_read_size);
    if (!nread || nread == EOF)
      return 1;

    // read the rest of the buffer
    if (header->byte_count > sizeof(buffer))
      return 1;

    buffer_bytes = header->byte_count - sizeof(LogBufferHeader) + 1;
    if (buffer_bytes <= 0 || (unsigned int) buffer_bytes > (sizeof(buffer) - sizeof(LogBufferHeader)))
      return 1;

    nread = read(in_fd, &buffer[sizeof(LogBufferHeader)], buffer_bytes);
    if (!nread || nread == EOF)
      return 1;

    // Possibly skip too old entries (the entire buffer is skipped)
    if (header->high_timestamp >= max_age)
      if (parse_log_buff(header, cl.summary != 0) != 0)
        return 1;
  }

  return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Determine if this "stat" (Origin Server) is worthwhile to produce a
// report for.
inline int
use_origin(const OriginStats * stat)
{
  return ((stat->total.count > cl.min_hits) && (strchr(stat->server, '.') != NULL) &&
          (strchr(stat->server, '%') == NULL));
}


///////////////////////////////////////////////////////////////////////////////
// Produce a nicely formatted output for a stats collection on a stream
inline void
format_center(const char *str, std::ostream & out)
{
  out << std::setfill(' ') << std::setw((cl.line_len - strlen(str)) / 2 + strlen(str)) << str << std::endl << std::endl;
}

inline void
format_int(int64 num, std::ostream & out)
{
  if (num > 0) {
    int64 mult = (int64) pow((double)10, (int) (log10((double)num) / 3) * 3);
    int64 div;
    std::stringstream ss;

    ss.fill('0');
    while (mult > 0) {
      div = num / mult;
      ss << div << std::setw(3);
      num -= (div * mult);
      if (mult /= 1000)
        ss << std::setw(0) << ',' << std::setw(3);
    }
    out << ss.str();
  } else
    out << '0';
}

void
format_elapsed_header(std::ostream & out)
{
  out << std::left << std::setw(24) << "Elapsed time stats";
  out << std::right << std::setw(7) << "Min" << std::setw(13) << "Max";
  out << std::right << std::setw(17) << "Avg" << std::setw(17) << "Std Deviation" << std::endl;
  out << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;
}

inline void
format_elapsed_line(const char *desc, const ElapsedStats & stat, std::ostream & out, bool json=false)
{
  static char buf[64];

  if (json) {
    out << "    " << '"' << desc << "\" : " << "{ ";
    out << "\"min\": \"" << stat.min << "\", ";
    out << "\"max\": \"" << stat.max << "\", ";
    out << "\"avg\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2) << stat.avg << "\", ";
    out << "\"dev\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2) << stat.stddev << "\" }," << std::endl;
  } else {
    out << std::left << std::setw(24) << desc;
    out << std::right << std::setw(7);
    format_int(stat.min, out);
    out << std::right << std::setw(13);
    format_int(stat.max, out);
    snprintf(buf, sizeof(buf), "%17.3f", stat.avg);
    out << std::right << buf;
    snprintf(buf, sizeof(buf), "%17.3f", stat.stddev);
    out << std::right << buf << std::endl;
  }
}


void
format_detail_header(const char *desc, std::ostream & out)
{
  out << std::left << std::setw(29) << desc;
  out << std::right << std::setw(15) << "Count" << std::setw(11) << "Percent";
  out << std::right << std::setw(12) << "Bytes" << std::setw(11) << "Percent" << std::endl;
  out << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;
}

inline void
format_line(const char *desc, const StatsCounter & stat, const StatsCounter & total, std::ostream & out, bool json=false)
{
  static char metrics[] = "KKMGTP";
  static char buf[64];
  int ix = (stat.bytes > 1024 ? (int) (log10((double)stat.bytes) / LOG10_1024) : 1);

  if (json) {
    out << "    " << '"' << desc << "\" : " << "{ ";
    out << "\"req\": \"" << stat.count << "\", ";
    out << "\"req_pct\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2) << (double)stat.count / total.count * 100 << "\", ";
    out << "\"bytes\": \"" << stat.bytes << "\", ";
    out << "\"bytes_pct\": \"" << std::setiosflags(ios::fixed) << std::setprecision(2) << (double)stat.bytes / total.bytes * 100 << "\" }," << std::endl;
  } else {
    out << std::left << std::setw(29) << desc;

    out << std::right << std::setw(15);
    format_int(stat.count, out);

    snprintf(buf, sizeof(buf), "%10.2f%%", ((double) stat.count / total.count * 100));
    out << std::right << buf;

    snprintf(buf, sizeof(buf), "%10.2f%cB", stat.bytes / pow((double)1024, ix), metrics[ix]);
    out << std::right << buf;

    snprintf(buf, sizeof(buf), "%10.2f%%", ((double) stat.bytes / total.bytes * 100));
    out << std::right << buf << std::endl;
  }
}


// Little "helpers" for the vector we use to sort the Origins.
typedef pair<const char *, OriginStats *>OriginPair;
inline bool
operator<(const OriginPair & a, const OriginPair & b)
{
  return a.second->total.count > b.second->total.count;
}

void
print_detail_stats(const OriginStats * stat, std::ostream & out, bool json=false)
{
  // Cache hit/misses etc.
  if (!json)
    format_detail_header("Request Result", out);

  format_line(json ? "hit.direct" : "Cache hit", stat->results.hits.hit, stat->total, out, json);
  format_line(json ? "hit.ims" : "Cache hit IMS", stat->results.hits.ims, stat->total, out, json);
  format_line(json ? "hit.refresh" : "Cache hit refresh", stat->results.hits.refresh, stat->total, out, json);
  format_line(json ? "hit.other" : "Cache hit other", stat->results.hits.other, stat->total, out, json);
  format_line(json ? "hit.total" : "Cache hit total", stat->results.hits.total, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "miss.direct" : "Cache miss", stat->results.misses.miss, stat->total, out, json);
  format_line(json ? "miss.ims" : "Cache miss IMS", stat->results.misses.ims, stat->total, out, json);
  format_line(json ? "miss.refresh" : "Cache miss refresh", stat->results.misses.refresh, stat->total, out, json);
  format_line(json ? "miss.other" : "Cache miss other", stat->results.misses.other, stat->total, out, json);
  format_line(json ? "miss.total" : "Cache miss total", stat->results.misses.total, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "error.client_abort" : "Client aborted", stat->results.errors.client_abort, stat->total, out, json);
  format_line(json ? "error.conenct_failed" : "Connect failed", stat->results.errors.connect_fail, stat->total, out, json);
  format_line(json ? "error.invalid_request" : "Invalid request", stat->results.errors.invalid_req, stat->total, out, json);
  format_line(json ? "error.unknown" : "Unknown error(99)", stat->results.errors.unknown, stat->total, out, json);
  format_line(json ? "error.other" : "Other errors", stat->results.errors.other, stat->total, out, json);
  format_line(json ? "error.total" : "Errors total", stat->results.errors.total, stat->total, out, json);

  if (!json) {
    out << std::setw(cl.line_len) << std::setfill('.') << '.' << std::setfill(' ') << std::endl;
    format_line("Total requests", stat->total, stat->total, out);
    out << std::endl << std::endl;

    // HTTP codes
    format_detail_header("HTTP return codes", out);
  }

  format_line(json ? "status.100" : "100 Continue", stat->codes.c_100, stat->total, out, json);

  format_line(json ? "status.200" : "200 OK", stat->codes.c_200, stat->total, out, json);
  format_line(json ? "status.201" : "201 Created", stat->codes.c_201, stat->total, out, json);
  format_line(json ? "status.202" : "202 Accepted", stat->codes.c_202, stat->total, out, json);
  format_line(json ? "status.203" : "203 Non-Authoritative Info", stat->codes.c_203, stat->total, out, json);
  format_line(json ? "status.204" : "204 No content", stat->codes.c_204, stat->total, out, json);
  format_line(json ? "status.205" : "205 Reset Content", stat->codes.c_205, stat->total, out, json);
  format_line(json ? "status.206" : "206 Partial content", stat->codes.c_206, stat->total, out, json);
  format_line(json ? "status.2xx" : "2xx other success", stat->codes.c_2xx, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "status.300" : "300 Multiple Choices", stat->codes.c_300, stat->total, out, json);
  format_line(json ? "status.301" : "301 Moved permanently", stat->codes.c_301, stat->total, out, json);
  format_line(json ? "status.302" : "302 Found", stat->codes.c_302, stat->total, out, json);
  format_line(json ? "status.303" : "303 See Other", stat->codes.c_303, stat->total, out, json);
  format_line(json ? "status.304" : "304 Not modified", stat->codes.c_304, stat->total, out, json);
  format_line(json ? "status.305" : "305 Use Proxy", stat->codes.c_305, stat->total, out, json);
  format_line(json ? "status.307" : "307 Temporary Redirect", stat->codes.c_307, stat->total, out, json);
  format_line(json ? "status.3xx" : "3xx other redirects", stat->codes.c_3xx, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "status.400" : "400 Bad request", stat->codes.c_400, stat->total, out, json);
  format_line(json ? "status.401" : "401 Unauthorized", stat->codes.c_401, stat->total, out, json);
  format_line(json ? "status.402" : "402 Payment Required", stat->codes.c_402, stat->total, out, json);
  format_line(json ? "status.403" : "403 Forbidden", stat->codes.c_403, stat->total, out, json);
  format_line(json ? "status.404" : "404 Not found", stat->codes.c_404, stat->total, out, json);
  format_line(json ? "status.405" : "405 Method Not Allowed", stat->codes.c_405, stat->total, out, json);
  format_line(json ? "status.406" : "406 Not Acceptable", stat->codes.c_406, stat->total, out, json);
  format_line(json ? "status.407" : "407 Proxy Auth Required", stat->codes.c_407, stat->total, out, json);
  format_line(json ? "status.408" : "408 Request Timeout", stat->codes.c_408, stat->total, out, json);
  format_line(json ? "status.409" : "409 Conflict", stat->codes.c_409, stat->total, out, json);
  format_line(json ? "status.410" : "410 Gone", stat->codes.c_410, stat->total, out, json);
  format_line(json ? "status.411" : "411 Length Required", stat->codes.c_411, stat->total, out, json);
  format_line(json ? "status.412" : "412 Precondition Failed", stat->codes.c_412, stat->total, out, json);
  format_line(json ? "status.413" : "413 Request Entity Too Large", stat->codes.c_413, stat->total, out, json);
  format_line(json ? "status.414" : "414 Request-URI Too Long", stat->codes.c_414, stat->total, out, json);
  format_line(json ? "status.415" : "415 Unsupported Media Type", stat->codes.c_415, stat->total, out, json);
  format_line(json ? "status.416" : "416 Req Range Not Satisfiable", stat->codes.c_416, stat->total, out, json);
  format_line(json ? "status.417" : "417 Expectation Failed", stat->codes.c_417, stat->total, out, json);
  format_line(json ? "status.4xx" : "4xx other client errors", stat->codes.c_4xx, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "status.500" : "500 Internal Server Error", stat->codes.c_500, stat->total, out, json);
  format_line(json ? "status.501" : "501 Not implemented", stat->codes.c_501, stat->total, out, json);
  format_line(json ? "status.502" : "502 Bad gateway", stat->codes.c_502, stat->total, out, json);
  format_line(json ? "status.503" : "503 Service unavailable", stat->codes.c_503, stat->total, out, json);
  format_line(json ? "status.504" : "504 Gateway Timeout", stat->codes.c_504, stat->total, out, json);
  format_line(json ? "status.505" : "505 HTTP Ver. Not Supported", stat->codes.c_505, stat->total, out, json);
  format_line(json ? "status.5xx" : "5xx other server errors", stat->codes.c_5xx, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "status.000" : "000 Unknown", stat->codes.c_000, stat->total, out, json);

  if (!json) {
    out << std::endl << std::endl;

    // Origin hierarchies
    format_detail_header("Origin hierarchies", out);
  }

  format_line(json ? "hier.none" : "NONE", stat->hierarchies.none, stat->total, out, json);
  format_line(json ? "hier.direct" : "DIRECT", stat->hierarchies.direct, stat->total, out, json);
  format_line(json ? "hier.sibling" : "SIBLING", stat->hierarchies.sibling, stat->total, out, json);
  format_line(json ? "hier.parent" : "PARENT", stat->hierarchies.parent, stat->total, out, json);
  format_line(json ? "hier.empty" : "EMPTY", stat->hierarchies.empty, stat->total, out, json);
  format_line(json ? "hier.invalid" : "invalid", stat->hierarchies.invalid, stat->total, out, json);
  format_line(json ? "hier.other" : "other", stat->hierarchies.other, stat->total, out, json);

  if (!json) {
    out << std::endl << std::endl;

    // HTTP methods
    format_detail_header("HTTP Methods", out);
  }

  format_line(json ? "method.options" : "OPTIONS", stat->methods.options, stat->total, out, json);
  format_line(json ? "method.get" : "GET", stat->methods.get, stat->total, out, json);
  format_line(json ? "method.head" : "HEAD", stat->methods.head, stat->total, out, json);
  format_line(json ? "method.post" : "POST", stat->methods.post, stat->total, out, json);
  format_line(json ? "method.put" : "PUT", stat->methods.put, stat->total, out, json);
  format_line(json ? "method.delete" : "DELETE", stat->methods.del, stat->total, out, json);
  format_line(json ? "method.trace" : "TRACE", stat->methods.trace, stat->total, out, json);
  format_line(json ? "method.connect" : "CONNECT", stat->methods.connect, stat->total, out, json);
  format_line(json ? "method.purge" : "PURGE", stat->methods.purge, stat->total, out, json);
  format_line(json ? "method.none" : "none (-)", stat->methods.none, stat->total, out, json);
  format_line(json ? "method.other" : "other", stat->methods.other, stat->total, out, json);

  if (!json) {
    out << std::endl << std::endl;

    // URL schemes (HTTP/HTTPs)
    format_detail_header("URL Schemes", out);
  }

  format_line(json ? "scheme.http" : "HTTP (port 80)", stat->schemes.http, stat->total, out, json);
  format_line(json ? "scheme.https" : "HTTPS (port 443)", stat->schemes.https, stat->total, out, json);
  format_line(json ? "scheme.none" : "none", stat->schemes.none, stat->total, out, json);
  format_line(json ? "scheme.other" : "other", stat->schemes.other, stat->total, out, json);

  if (!json) {
    out << std::endl << std::endl;

    // Content types
    format_detail_header("Content Types", out);
  }

  format_line(json ? "content.text.javascript" : "text/javascript", stat->content.text.javascript, stat->total, out, json);
  format_line(json ? "content.text.css" : "text/css", stat->content.text.css, stat->total, out, json);
  format_line(json ? "content.text.html" : "text/html", stat->content.text.html, stat->total, out, json);
  format_line(json ? "content.text.xml" : "text/xml", stat->content.text.xml, stat->total, out, json);
  format_line(json ? "content.text.plain" : "text/plain", stat->content.text.plain, stat->total, out, json);
  format_line(json ? "content.text.other" : "text/ other", stat->content.text.other, stat->total, out, json);
  format_line(json ? "content.text.total" : "text/ total", stat->content.text.total, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "content.image.jpeg" : "image/jpeg", stat->content.image.jpeg, stat->total, out, json);
  format_line(json ? "content.image.gif" : "image/gif", stat->content.image.gif, stat->total, out, json);
  format_line(json ? "content.image.png" : "image/png", stat->content.image.png, stat->total, out, json);
  format_line(json ? "content.image.bmp" : "image/bmp", stat->content.image.bmp, stat->total, out, json);
  format_line(json ? "content.image.other" : "image/ other", stat->content.image.other, stat->total, out, json);
  format_line(json ? "content.image.total" : "image/ total", stat->content.image.total, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "content.audio.x-wav" : "audio/x-wav", stat->content.audio.wav, stat->total, out, json);
  format_line(json ? "content.audio.x-mpeg" : "audio/x-mpeg", stat->content.audio.mpeg, stat->total, out, json);
  format_line(json ? "content.audio.other" : "audio/ other", stat->content.audio.other, stat->total, out, json);
  format_line(json ? "content.audio.total": "audio/ total", stat->content.audio.total, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "content.application.shockwave" : "application/x-shockwave", stat->content.application.shockwave_flash, stat->total, out, json);
  format_line(json ? "content.application.javascript" : "application/[x-]javascript", stat->content.application.javascript, stat->total, out, json);
  format_line(json ? "content.application.quicktime" : "application/x-quicktime", stat->content.application.quicktime, stat->total, out, json);
  format_line(json ? "content.application.zip" : "application/zip", stat->content.application.zip, stat->total, out, json);
  format_line(json ? "content.application.rss_xml" : "application/rss+xml", stat->content.application.rss_xml, stat->total, out, json);
  format_line(json ? "content.application.rss_atom" : "application/rss+atom", stat->content.application.rss_atom, stat->total, out, json);
  format_line(json ? "content.application.other" : "application/ other", stat->content.application.other, stat->total, out, json);
  format_line(json ? "content.application.total": "application/ total", stat->content.application.total, stat->total, out, json);

  if (!json)
    out << std::endl;

  format_line(json ? "content.none" : "none", stat->content.none, stat->total, out, json);
  format_line(json ? "content.other" : "other", stat->content.other, stat->total, out, json);

  if (!json)
    out << std::endl << std::endl;

  // Elapsed time
  if (!json)
    format_elapsed_header(out);

  format_elapsed_line(json ? "hit.direct.latency" : "Cache hit", stat->elapsed.hits.hit, out, json);
  format_elapsed_line(json ? "hit.ims.latency" : "Cache hit IMS", stat->elapsed.hits.ims, out, json);
  format_elapsed_line(json ? "hit.refresh.latency" : "Cache hit refresh", stat->elapsed.hits.refresh, out, json);
  format_elapsed_line(json ? "hit.other.latency" : "Cache hit other", stat->elapsed.hits.other, out, json);
  format_elapsed_line(json ? "hit.total.latency" : "Cache hit total", stat->elapsed.hits.total, out, json);

  format_elapsed_line(json ? "miss.direct.latency" : "Cache miss", stat->elapsed.misses.miss, out, json);
  format_elapsed_line(json ? "miss.ims.latency" : "Cache miss IMS", stat->elapsed.misses.ims, out, json);
  format_elapsed_line(json ? "miss.refresh.latency" : "Cache miss refresh", stat->elapsed.misses.refresh, out, json);
  format_elapsed_line(json ? "miss.other.latency" : "Cache miss other", stat->elapsed.misses.other, out, json);
  format_elapsed_line(json ? "miss.total.latency" : "Cache miss total", stat->elapsed.misses.total, out, json);

  if (!json) {
    out << std::endl;
    out << std::setw(cl.line_len) << std::setfill('_') << '_' << std::setfill(' ') << std::endl;
  } else {
    std::cout << "    \"_timestamp\" : \"" << static_cast<int>(ink_time_wall_seconds()) << '"' << std::endl;
  }
}


///////////////////////////////////////////////////////////////////////////////
// Little wrapper around exit, to allow us to exit gracefully
void
my_exit(ExitLevel status, const char *notice)
{
  vector<OriginPair> vec;
  bool first = true;
  int max_origins;

  if (cl.cgi) {
    std::cout << "Content-Type: application/javascript\r\n";
    std::cout << "Cache-Control: no-cache\r\n\r\n";
  }

  if (cl.json) {
    // TODO: Add JSON output
  } else {
    switch (status) {
    case EXIT_OK:
      break;
    case EXIT_WARNING:
      std::cout << "warning: " << notice << std::endl;
      break;
    case EXIT_CRITICAL:
      std::cout << "critical: " << notice << std::endl;
      _exit(status);
      break;
    case EXIT_UNKNOWN:
      std::cout << "unknown: " << notice << std::endl;
      _exit(status);
      break;
    }
  }

  if (!origins.empty()) {
    // Sort the Origins by 'traffic'
    for (OriginStorage::iterator i = origins.begin(); i != origins.end(); i++)
      if (use_origin(i->second))
        vec.push_back(*i);
    sort(vec.begin(), vec.end());

    if (!cl.json) {
      // Produce a nice summary first
      format_center("Traffic summary", std::cout);
      std::cout << std::left << std::setw(33) << "Origin Server";
      std::cout << std::right << std::setw(15) << "Hits";
      std::cout << std::right << std::setw(15) << "Misses";
      std::cout << std::right << std::setw(15) << "Errors" << std::endl;
      std::cout << std::setw(cl.line_len) << std::setfill('-') << '-' << std::setfill(' ') << std::endl;

      max_origins = cl.max_origins > 0 ? cl.max_origins : INT_MAX;
      for (vector<OriginPair>::iterator i = vec.begin(); (i != vec.end()) && (max_origins > 0); ++i, --max_origins) {
        std::cout << std::left << std::setw(33) << i->first;
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.hits.total.count, std::cout);
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.misses.total.count, std::cout);
        std::cout << std::right << std::setw(15);
        format_int(i->second->results.errors.total.count, std::cout);
        std::cout << std::endl;
      }
      std::cout << std::setw(cl.line_len) << std::setfill('=') << '=' << std::setfill(' ') << std::endl;
      std::cout << std::endl << std::endl << std::endl;
    }
  }

  // Next the totals for all Origins, unless we specified a list of origins to filter.
  if (!origin_set) {
    first = false;
    if (cl.json) {
      std::cout << "{ \"total\": {" << std::endl;
      print_detail_stats(&totals, std::cout, cl.json);
      std::cout << "  }";
    } else {
      format_center("Totals (all Origins combined)", std::cout);
      print_detail_stats(&totals, std::cout);
      std::cout << std::endl << std::endl << std::endl;
    }
  }

  // And finally the individual Origin Servers.
  max_origins = cl.max_origins > 0 ? cl.max_origins : INT_MAX;
  for (vector<OriginPair>::iterator i = vec.begin(); (i != vec.end()) && (max_origins > 0); ++i, --max_origins) {
    if (cl.json) {
      if (first) {
        std::cout << "{ ";
        first = false;
      } else {
        std::cout << "," << std::endl << "  ";
      }
      std::cout << '"' <<  i->first << "\": {" << std::endl;
      print_detail_stats(i->second, std::cout, cl.json);
      std::cout << "  }";
    } else {
      format_center(i->first, std::cout);
      print_detail_stats(i->second, std::cout);
      std::cout << std::endl << std::endl << std::endl;
    }
  }

  if (cl.json) {
    std::cout << std::endl << "}" << std::endl;
  }

  _exit(status);
}

///////////////////////////////////////////////////////////////////////////////
// Open the "default" log file (squid.blog), allow for it to be rotated.
int
open_main_log(char *exit_notice, const size_t exit_notice_size)
{
  int cnt = 3;
  int main_fd;

  while (((main_fd = open("./squid.blog", O_RDONLY)) < 0) && --cnt) {
    switch (errno) {
    case ENOENT:
    case EACCES:
      sleep(5);
      break;
    default:
      strncat(exit_notice, " can't open squid.blog", exit_notice_size - strlen(exit_notice) - 1);
      return -1;
    }
  }

  if (main_fd < 0) {
    strncat(exit_notice, " squid.blog not enabled", exit_notice_size - strlen(exit_notice) - 1);
    return -1;
  }
#if TS_HAS_POSIX_FADVISE
  posix_fadvise(main_fd, 0, 0, POSIX_FADV_DONTNEED);
#endif
  return main_fd;
}



///////////////////////////////////////////////////////////////////////////////
// main
int
main(int argc, char *argv[])
{
  ExitLevel exit_status = EXIT_OK;
  char exit_notice[4096];
  struct utsname uts_buf;
  int res, cnt;
  int main_fd;
  unsigned max_age;
  struct flock lck;

  // build the application information structure
  appVersionInfo.setup(PACKAGE_NAME,PROGRAM_NAME, PACKAGE_VERSION, __DATE__, __TIME__,
                       BUILD_MACHINE, BUILD_PERSON, "");

  // Before accessing file system initialize Layout engine
  Layout::create();

  memset(&totals, 0, sizeof(totals));
  init_elapsed(totals);
  memset(&cl, 0, sizeof(cl));
  memset(exit_notice, 0, sizeof(exit_notice));

  cl.line_len = DEFAULT_LINE_LEN;
  origin_set = NULL;
  parse_errors = 0;

  // Get log directory
  ink_strlcpy(system_log_dir, Layout::get()->logdir, PATH_NAME_MAX);
  if (access(system_log_dir, R_OK) == -1) {
    fprintf(stderr, "unable to change to log directory \"%s\" [%d '%s']\n", system_log_dir, errno, strerror(errno));
    fprintf(stderr, " please set correct path in env variable TS_ROOT \n");
    exit(1);
  }

  // process command-line arguments
  process_args(argument_descriptions, n_argument_descriptions, argv, USAGE_LINE);

  // Process as "CGI" ?
  if (strstr(argv[0], ".cgi") || cl.cgi) {
    char *query;
    char *pos1, *pos2;
    int len;

    cl.json = 1;
    cl.cgi = 1;

    query = getenv("QUERY_STRING");
    if (query) {
      char buffer[2048];

      ink_strncpy(buffer, query, sizeof(buffer));
      buffer[2047] = '\0';
      len = unescapifyStr(buffer);
      if (NULL != (pos1 = strstr(buffer, "origin_list="))) {
        pos1 += 12;
        if (NULL == (pos2 = strchr(pos1, '&')))
          pos2 = buffer + len;
        strncpy(cl.origin_list, pos1, (pos2 - pos1));
      }
      if (NULL != (pos1 = strstr(buffer, "max_origins="))) {
        pos1 += 12;
        cl.max_origins = strtol(pos1, NULL, 10); // Up until EOS or non-numeric
      }
    }
  }

  // check for the version number request
  if (cl.version) {
    std::cerr << appVersionInfo.FullVersionInfoStr << std::endl;
    _exit(0);
  }
  // check for help request
  if (cl.help) {
    usage(argument_descriptions, n_argument_descriptions, USAGE_LINE);
    _exit(0);
  }
  // Calculate the max age of acceptable log entries, if necessary
  if (cl.max_age > 0) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    max_age = tv.tv_sec - cl.max_age;
  } else {
    max_age = 0;
  }

  // initialize this application for standalone logging operation
  init_log_standalone_basic(PROGRAM_NAME);
  Log::init(Log::NO_REMOTE_MANAGEMENT | Log::LOGCAT);

  // Do we have a list of Origins on the command line?
  if (cl.origin_list[0] != '\0') {
    char *tok;
    char *sep_ptr;

    if (origin_set == NULL)
      origin_set = NEW(new OriginSet);
    if (cl.origin_list) {
      for (tok = strtok_r(cl.origin_list, ",", &sep_ptr); tok != NULL;) {
        origin_set->insert(tok);
        tok = strtok_r(NULL, ",", &sep_ptr);
      }
    }
  }
  // Load origins from an "external" file (\n separated)
  if (cl.origin_file[0] != '\0') {
    std::ifstream fs;

    fs.open(cl.origin_file, std::ios::in);
    if (!fs.is_open()) {
      std::cerr << "can't read " << cl.origin_file << std::endl;
      usage(argument_descriptions, n_argument_descriptions, USAGE_LINE);
      _exit(0);
    }

    if (origin_set == NULL)
      origin_set = NEW(new OriginSet);

    while (!fs.eof()) {
      std::string line;
      std::string::size_type start, end;

      getline(fs, line);
      start = line.find_first_not_of(" \t");
      if (start != std::string::npos) {
        end = line.find_first_of(" \t#/");
        if (end == std::string::npos)
          end = line.length();

        if (end > start) {
          char *buf;

          buf = xstrdup(line.substr(start, end).c_str());
          if (buf)
            origin_set->insert(buf);
        }
      }
    }
  }
  // Get the hostname
  if (uname(&uts_buf) < 0) {
    strncat(exit_notice, " can't get hostname", sizeof(exit_notice) - strlen(exit_notice) - 1);
    my_exit(EXIT_CRITICAL, exit_notice);
  }
  hostname = xstrdup(uts_buf.nodename);

  // Change directory to the log dir
  if (chdir(system_log_dir) < 0) {
    snprintf(exit_notice, sizeof(exit_notice), "can't chdir to %s", system_log_dir);
    my_exit(EXIT_CRITICAL, exit_notice);
  }

  if (cl.incremental) {
    // Do the incremental parse of the default squid log.
    std::string sf_name(system_log_dir);
    struct stat stat_buf;
    int state_fd;
    sf_name.append("/logstats.state");

    if (cl.state_tag[0] != '\0') {
      sf_name.append(".");
      sf_name.append(cl.state_tag);
    } else {
      // Default to the username
      struct passwd *pwd = getpwuid(geteuid());

      if (pwd) {
        sf_name.append(".");
        sf_name.append(pwd->pw_name);
      } else {
        strncat(exit_notice, " can't get current UID", sizeof(exit_notice) - strlen(exit_notice) - 1);
        my_exit(EXIT_CRITICAL, exit_notice);
      }
    }

    if ((state_fd = open(sf_name.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
      strncat(exit_notice, " can't open state file", sizeof(exit_notice) - strlen(exit_notice) - 1);
      my_exit(EXIT_CRITICAL, exit_notice);
    }
    // Get an exclusive lock, if possible. Try for up to 20 seconds.
    // Use more portable & standard fcntl() over flock()
    lck.l_type = F_WRLCK;
    lck.l_whence = 0; /* offset l_start from beginning of file*/
    lck.l_start = (off_t)0;
    lck.l_len = (off_t)0; /* till end of file*/
    cnt = 10;
    // while (((res = flock(state_fd, LOCK_EX | LOCK_NB)) < 0) && --cnt) {
    while (((res = fcntl(state_fd, F_SETLK, &lck)) < 0) && --cnt) {
      switch (errno) {
      case EWOULDBLOCK:
      case EINTR:
        sleep(2);
        break;
      default:
        strncat(exit_notice, " locking failure", sizeof(exit_notice) - strlen(exit_notice) - 1);
        my_exit(EXIT_CRITICAL, exit_notice);
        break;
      }
    }

    if (res < 0) {
      strncat(exit_notice, " can't lock state file", sizeof(exit_notice) - strlen(exit_notice) - 1);
      my_exit(EXIT_CRITICAL, exit_notice);
    }
    // Fetch previous state information, allow for concurrent accesses.
    cnt = 10;
    while (((res = read(state_fd, &last_state, sizeof(last_state))) < 0) && --cnt) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
        sleep(1);
        break;
      default:
        strncat(exit_notice, " can't read state file", sizeof(exit_notice) - strlen(exit_notice) - 1);
        my_exit(EXIT_CRITICAL, exit_notice);
        break;
      }
    }

    if (res != sizeof(last_state)) {
      // First time / empty file, so reset.
      last_state.offset = 0;
      last_state.st_ino = 0;
    }

    if ((main_fd = open_main_log(exit_notice, sizeof(exit_notice))) < 0)
      my_exit(EXIT_CRITICAL, exit_notice);

    // Get stat's from the main log file.
    if (fstat(main_fd, &stat_buf) < 0) {
      strncat(exit_notice, " can't stat squid.blog", sizeof(exit_notice) - strlen(exit_notice) - 1);
      my_exit(EXIT_CRITICAL, exit_notice);
    }
    // Make sure the last_state.st_ino is sane.
    if (last_state.st_ino <= 0)
      last_state.st_ino = stat_buf.st_ino;

    // Check if the main log file was rotated, and if so, locate
    // the old file first, and parse the remaining log data.
    if (stat_buf.st_ino != last_state.st_ino) {
      DIR *dirp = NULL;
      struct dirent *dp = NULL;
      ino_t old_inode = last_state.st_ino;

      // Save the current log-file's I-Node number.
      last_state.st_ino = stat_buf.st_ino;

      // Find the old log file.
      dirp = opendir(system_log_dir);
      if (dirp == NULL) {
        strncat(exit_notice, " can't read log directory", sizeof(exit_notice) - strlen(exit_notice) - 1);
        if (exit_status == EXIT_OK)
          exit_status = EXIT_WARNING;
      } else {
        while ((dp = readdir(dirp)) != NULL) {
          if (stat(dp->d_name, &stat_buf) < 0) {
            strncat(exit_notice, " can't stat ", sizeof(exit_notice) - strlen(exit_notice) - 1);
            strncat(exit_notice, dp->d_name, sizeof(exit_notice) - strlen(exit_notice) - 1);
            if (exit_status == EXIT_OK)
              exit_status = EXIT_WARNING;
          } else if (stat_buf.st_ino == old_inode) {
            int old_fd = open(dp->d_name, O_RDONLY);

            if (old_fd < 0) {
              strncat(exit_notice, " can't open ", sizeof(exit_notice) - strlen(exit_notice) - 1);
              strncat(exit_notice, dp->d_name, sizeof(exit_notice) - strlen(exit_notice) - 1);
              if (exit_status == EXIT_OK)
                exit_status = EXIT_WARNING;
              break;            // Don't attempt any more files
            }
            // Process it
            if (process_file(old_fd, last_state.offset, max_age) != 0) {
              strncat(exit_notice, " can't read ", sizeof(exit_notice) - strlen(exit_notice) - 1);
              strncat(exit_notice, dp->d_name, sizeof(exit_notice) - strlen(exit_notice) - 1);
              if (exit_status == EXIT_OK)
                exit_status = EXIT_WARNING;
            }
            close(old_fd);
            break;              // Don't attempt any more files
          }
        }
      }
      // Make sure to read from the beginning of the freshly rotated file.
      last_state.offset = 0;
    } else {
      // Make sure the last_state.offset is sane, stat_buf is for the main_fd.
      if (last_state.offset > stat_buf.st_size)
        last_state.offset = stat_buf.st_size;
    }

    // Process the main file (always)
    if (process_file(main_fd, last_state.offset, max_age) != 0) {
      strncat(exit_notice, " can't parse log", sizeof(exit_notice) - strlen(exit_notice) - 1);
      exit_status = EXIT_CRITICAL;

      last_state.offset = 0;
      last_state.st_ino = 0;
    } else {
      // Save the current file offset.
      last_state.offset = lseek(main_fd, 0, SEEK_CUR);
      if (last_state.offset < 0) {
        strncat(exit_notice, " can't lseek squid.blog", sizeof(exit_notice) - strlen(exit_notice) - 1);
        if (exit_status == EXIT_OK)
          exit_status = EXIT_WARNING;
        last_state.offset = 0;
      }
    }

    // Save the state, release the lock, and close the FDs.
    if (lseek(state_fd, 0, SEEK_SET) < 0) {
      strncat(exit_notice, " can't lseek state file", sizeof(exit_notice) - strlen(exit_notice) - 1);
      if (exit_status == EXIT_OK)
        exit_status = EXIT_WARNING;
    } else {
      if (write(state_fd, &last_state, sizeof(last_state)) == (-1)) {
        strncat(exit_notice, " can't write state_fd ", sizeof(exit_notice) - strlen(exit_notice) - 1);
        if (exit_status == EXIT_OK)
          exit_status = EXIT_WARNING;
      }
    }
    //flock(state_fd, LOCK_UN);
    lck.l_type = F_UNLCK;
    fcntl(state_fd, F_SETLK, &lck);
    close(main_fd);
    close(state_fd);
  } else {
    if (cl.log_file[0] != '\0') {
      main_fd = open(cl.log_file, O_RDONLY);
      if (main_fd < 0) {
        strncat(exit_notice, " can't open log file ", sizeof(exit_notice) - strlen(exit_notice) - 1);
        strncat(exit_notice, cl.log_file, sizeof(exit_notice) - strlen(exit_notice) - 1);
        my_exit(EXIT_CRITICAL, exit_notice);
      }
    } else {
      main_fd = open_main_log(exit_notice, sizeof(exit_notice));
    }

    if (cl.tail > 0) {
      if (lseek(main_fd, 0, SEEK_END) < 0) {
        strncat(exit_notice, " can't lseek squid.blog", sizeof(exit_notice) - strlen(exit_notice) - 1);
        my_exit(EXIT_CRITICAL, exit_notice);
      }
      sleep(cl.tail);
    }

    if (process_file(main_fd, 0, max_age) != 0) {
      close(main_fd);
      strncat(exit_notice, " can't parse log file ", sizeof(exit_notice) - strlen(exit_notice) - 1);
      strncat(exit_notice, cl.log_file, sizeof(exit_notice) - strlen(exit_notice) - 1);
      my_exit(EXIT_CRITICAL, exit_notice);
    }
    close(main_fd);
  }

  // All done.
  if (exit_status == EXIT_OK)
    strncat(exit_notice, " OK", sizeof(exit_notice) - strlen(exit_notice) - 1);
  my_exit(exit_status, exit_notice);
}

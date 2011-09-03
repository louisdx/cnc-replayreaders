#ifndef H_CNC3READER
#define H_CNC3READER

/**** Schneider's EA Command & Conquer replay reader tools ****
 *
 * This is a common header file for all three replay reader programs.
 * It defines general parsing tools, and this header file may be
 * precompiled to speed up compilation.
 *
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iterator>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <stdint.h>
#include <getopt.h>

#define READ_UINT16LE(a, b)  ( ((unsigned int)(b)<<8) | ((unsigned int)(a)) )
#define READ_UINT32LE(in) ( (unsigned int)((in)[0] | ((in)[1] << 8) | ((in)[2] << 16) | ((in)[3] << 24)) )
#define READ(f, x) do { f.read(reinterpret_cast<char*>(&x), sizeof(x)); } while (false)

typedef struct _header_cnc3_t
{
  char           str_magic[18];
  unsigned char  number1;       // maybe skirmish (0x04) vs. multiplayer (0x05)?
  unsigned char  vermajor[4];   // 0x01
  unsigned char  verminor[4];   // 0x00,...,0x09
  unsigned char  buildmajor[4];
  unsigned char  buildminor[4];
  unsigned char  six;           // My guess: No commentary = 0x06, with commentary track = 0x1E.
  unsigned char  zero;          // 0
} header_cnc3_t;

typedef struct _header_ra3_t
{
  char           str_magic[17];
  unsigned char  number1;       // maybe skirmish (0x04) vs. multiplayer (0x05)?
  unsigned char  vermajor[4];   // 0x01
  unsigned char  verminor[4];   // 0x00,...,0x09
  unsigned char  buildmajor[4];
  unsigned char  buildminor[4];
  unsigned char  six;           // My guess: No commentary = 0x06, with commentary track = 0x1E.
  unsigned char  zero;          // 0
} header_ra3_t;

typedef struct _twobytestring_t
{
  unsigned char byte1;
  unsigned char byte2;
} twobytestring_t;

typedef union _codepoint_t
{
  char c[4];
  unsigned char u[4];
  unsigned int i;
} codepoint_t;

typedef struct _date_text_t
{
  uint16_t data[8];
} date_text_t;

template<int N> struct unknown_uints_t
{
  uint32_t data[N];
};

typedef struct _apm_t
{
  _apm_t() { counter[0] = counter[1] = counter[2] = counter[3] = 0; }
  unsigned int counter[4];
} apm_t;

struct Options
{
  enum GameType { GAME_UNDEF = 0, GAME_KW, GAME_TW, GAME_RA3 };

  Options() : type(), cmd_filter(), time_series_filter(), fixpos(0), fixfn(NULL), audiofn(NULL),
              autofix(false), breakonerror(false), dumpchunks(false), dumpchunkswithraw(false),
              dumpaudio(false), filter_heartbeat(-1), printraw(false),
              apm(false), fixbroken(false), gametype(GAME_UNDEF), verbose(false) {}

  std::set<int> type;
  std::set<int> cmd_filter;
  std::set<int> time_series_filter;
  unsigned int fixpos;
  const char * fixfn;
  const char * audiofn;
  bool autofix;
  bool breakonerror;
  bool dumpchunks;
  bool dumpchunkswithraw;
  bool dumpaudio;
  int  filter_heartbeat;
  bool printraw;
  bool apm;
  bool fixbroken;
  GameType gametype;
  bool verbose;
};

typedef std::map<unsigned int, unsigned int> apm_1_map_t;
typedef std::map<unsigned int, apm_t>        apm_2_map_t;
//typedef std::map<int, std::map<unsigned int, unsigned int> > apm_histo_map_t;
typedef std::map<int, std::map<unsigned int, std::multiset<unsigned int>>> apm_histo_map_t;

typedef std::map<unsigned int, int> command_map_t;
typedef std::map<unsigned int, std::string> command_names_t;


/* Checks if a given number of bytes are all zero.
 */
inline bool array_is_zero(const unsigned char * data, size_t n)
{
  for (size_t i = 0; i < n; ++i)
    if (data[i] != 0)
      return false;
  return true;
}


/**** Utility functions, implemented in the source file. ****/


/** Converts a 15fps time code into "mm:ss::frame" format.
 */
std::string timecode_to_string(unsigned int tc);


/** Converts integers 0-6 into wekdays.
 */
const char * weekday(unsigned int d);


/** A standard string tokenizer, returns a vector of tokens.
 */
std::vector<std::string> tokenize(const std::string & str, const std::string & delimiters = " ");


/** Prints a printable character verbatim, or a replacement ('.') otherwise.
 */
void asciiprint(FILE * out, unsigned char c);


/** Dumps a byte buffer in traditional hex view.
 */
void hexdump(FILE * out, const unsigned char * buf, size_t length, const char * delim);


/** Various functions to read one-byte and two-byte strings from an istream or from memory.
 */
std::string read1ByteString(std::istream & in);
std::string read2ByteString(const char * in, size_t N);
std::string read2ByteString(std::istream & in);
std::string read2ByteStringN(std::istream & in, size_t N);


/** Creates a UTF-8 representation of a single unicode codepoint.
 */
void codepointToUTF8(unsigned int cp, codepoint_t * szOut);


/** Parses an argument of the form "5,8,-7,0xAB" into a set of intergers.
 */
std::set<int> parse_int_sequence_arg(char * str);


/** Checks whether a filter list is non-empty and a given value is in it.
 */
inline bool is_filtered(int value, const std::set<int> & filterlist)
{
  return !filterlist.empty() && filterlist.find(value) == filterlist.end();
}

#endif

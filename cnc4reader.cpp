#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstdint>
#include <cstring>
#include <ctime>

/* g++ -O4 -s -o cnc4replayreader.exe cnc4reader.cpp -enable-auto-import -static-libgcc -static-libstdc++ -fwhole-program -std=gnu++0x */

/* Chunks come in types 1 and 2.

   Chunk type 1 seems to be like in TW/KW/RA3, i.e. consisting of commands terminated by 0xFF,
   although there only appears to be one single command in each chunk. It looks like this:

   struct chunk_type_1
   {
     uint16_t number              ; // ??
     uint32_t number_of_commands  ;
     byte payload[chunk_size - 6] ; // commands, each terminated by { 0x00, 0x00, 0xFF }
   }

   Chunk type 2 looks like this:

   struct chunk_type_2
   {
     byte number                  ;
     uint16_t                     ; // == 1, maybe number of commands?
     uint32_t player_id           ;
     byte                         ; // == 0x05
     uint32_t timecode            ; // == chunk_timecode
     byte payload[chunk_size - 12]; // chunk payload
   }

   The "number" appears to be one of 31, 35 and 47, and always 47 for the heartbeat.
*/


#define READ(f, x) do { f.read(reinterpret_cast<char*>(&x), sizeof(x)); } while (false)
#define READ_UINT16LE(a, b)  ( ((unsigned int)(b)<<8) | ((unsigned int)(a)) )

std::string timecode_to_string(unsigned int tc)
{
  std::ostringstream os;
  os << tc/15/60 << ":" << std::setw(2) << std::setfill('0') << (tc/15)%60 << "::" << std::setw(2) << std::setfill('0') << tc%15;
  return os.str();
}

void Tokenize(const std::string & str, std::vector<std::string> & tokens, const std::string & delimiters = " ")
{
  // Skip delimiters at beginning.
  std::string::size_type lastPos = str.find_first_not_of(delimiters, 0);
  // Find first "non-delimiter".
  std::string::size_type pos     = str.find_first_of(delimiters, lastPos);

  while (std::string::npos != pos || std::string::npos != lastPos)
  {
    // Found a token, add it to the vector.
    tokens.push_back(str.substr(lastPos, pos - lastPos));
    // Skip delimiters.  Note the "not_of"
    lastPos = str.find_first_not_of(delimiters, pos);
    // Find next "non-delimiter"
    pos = str.find_first_of(delimiters, lastPos);
  }
}

const char * WEEKDAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "[ERROR]" };
char WEEKDAY_ERROR[8];

typedef struct _date_text_t
{
  uint16_t data[8];
} date_text_t;

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

const char * weekday(unsigned int d)
{
  if (d > 6) { sprintf(WEEKDAY_ERROR, "[%hu]", d); return WEEKDAY_ERROR; }
  return WEEKDAYS[d];
}

void codepointToUTF8(unsigned int cp, codepoint_t * szOut)
{
  size_t len = 0;

  szOut->u[0] = szOut->u[1] = szOut->u[2] = szOut->u[3] = 0;

  if (cp < 0x0080) len++;
  else if (cp < 0x0800) len += 2;
  else len += 3;

  int i = 0;
  if (cp < 0x0080)
    szOut->u[i++] = (unsigned char) cp;
  else if (cp < 0x0800)
  {
    szOut->u[i++] = 0xc0 | (( cp ) >> 6 );
    szOut->u[i++] = 0x80 | (( cp ) & 0x3F );
  }
  else
  {
    szOut->u[i++] = 0xE0 | (( cp ) >> 12 );
    szOut->u[i++] = 0x80 | (( ( cp ) >> 6 ) & 0x3F );
    szOut->u[i++] = 0x80 | (( cp ) & 0x3F );
  }
}

bool array_is_zero(const unsigned char * data, size_t n)
{
  for (size_t i = 0; i < n; ++i)
    if (data[i] != 0)
      return false;
  return true;
}

std::string read2ByteStringN(std::istream & in, size_t N)
{
  codepoint_t     ccp;
  twobytestring_t cbuf;
  std::string     s;
  size_t          n = N;

  while (n--)
  {
    in.read(reinterpret_cast<char*>(&cbuf), sizeof(twobytestring_t));
    codepointToUTF8(cbuf.byte1 + (cbuf.byte2 << 8), &ccp);
    s += std::string(ccp.c);
  }

  return s;
}

std::string read2ByteString(const char * in, size_t N)
{
  codepoint_t     ccp;
  twobytestring_t cbuf;
  std::string     s;

  while (N > 1)
  {
    cbuf.byte1 = *in++; --N;
    cbuf.byte2 = *in++; --N;

    if (READ_UINT16LE(cbuf.byte1, cbuf.byte2) == 0)
      break;

    codepointToUTF8(READ_UINT16LE(cbuf.byte1, cbuf.byte2), &ccp);

    s += std::string(ccp.c);
  }

  return s;
}

void asciiprint(FILE * out, unsigned char c)
{
  if (c < 32 || c > 127) fprintf(out, ".");
  else fprintf(out, "%c", c);
}

void hexdump(FILE * out, const unsigned char * buf, size_t length, const char * delim)
{
  for (size_t k = 0; 16*k < length; k++)
  {
    fprintf(out, "%s", delim);

    for (int i = 0; i < 16 && 16*k + i < length; ++i)
      fprintf(out, "0x%02X ", buf[16*k + i]);

    if (16*(k+1) > length) for (size_t i = 0; i < 16*(k+1)-length; ++i) fprintf(out, "     ");

    fprintf(out, "    ");

    for (size_t i = 0; i < 16 && 16*k + i < length; ++i)
      asciiprint(out, buf[16*k + i]);

    fprintf(out, "\n");
  }
}

int main(int argc, char * argv[])
{
  if (argc < 2) return 0;

  int parse = 0;
  if (argc > 2) { if (argv[2][0] == 'r') parse = 1; else if (argv[2][0] == 'c') parse = 2; }

  std::cerr << "Opening file \"" << argv[1] << "... ";

  std::ifstream infile(argv[1], std::fstream::binary);

  uint32_t N, Nlast = 0;
  uint16_t L, S;
  char magic[11], timeout[200], matchbuf[1024], player_who_saved;
  std::vector<std::string> player_names;
  date_text_t datetime;

  READ(infile, N);
  READ(infile, magic);

  if (N != 7 || std::strncmp(magic, "CnC4RPLCnC4", 11)) { std::cerr << "Not a good C&C4 replay file." << std::endl; return 0; }
  std::cerr << "OK!" << std::endl << std::endl;

  infile.seekg(0x21, std::fstream::beg);
  READ(infile, N);
  std::strftime(timeout, 200, "%Y-%m-%d %H:%M:%S (%Z)", std::gmtime(reinterpret_cast<time_t*>(&N)));
  std::cout << "Timestamp: " << timeout << std::endl << std::endl << "Header:" << std::endl;

  infile.seekg(0x4A, std::fstream::beg);
  
  READ(infile, N);

  std::vector<char> header(N);
  infile.read(header.data(), N);

  std::vector<std::string> tokens;
  Tokenize(std::string(header.begin(), header.end()), tokens, ";");

  for (size_t i = 0; i < tokens.size(); ++i)
  {
    std::cout << "  " << tokens[i] << std::endl;
  }
  std::cout << std::endl;

  for (std::vector<std::string>::const_iterator it = tokens.begin(), end = tokens.end(); it != end; ++it)
  {
    const std::string & token = *it;

    if (token[0] == 'S' && token[1] == '=')
    {
      std::cout << std::endl << "Found player information, parsing..." << std::endl;
      std::vector<std::string> subtokens;
      Tokenize(token.substr(2), subtokens, ":");

      for (size_t i = 0; i < subtokens.size(); ++i)
      {
        if (subtokens[i][0] != 'H') continue;

        std::vector<std::string> subsubtokens;
        Tokenize(subtokens[i].substr(1), subsubtokens, ",");

        player_names.push_back(subsubtokens[0]);

        std::cout << "  Player name: " << subsubtokens[0] << ", Faction: " <<  subsubtokens[6] << std::endl;
      }
      std::cout << std::endl;
      break;
    }
  }

  READ(infile, player_who_saved);

  if (size_t(player_who_saved) < player_names.size())
  {
    std::cout << "The player who saved this replay is: " << size_t(player_who_saved)
              << " (" << player_names[size_t(player_who_saved)] << ")." << std::endl;
  }
  else
  {
    std::cout << "The player who saved this replay is allegedly: " << size_t(player_who_saved) << std::endl;
  }

  infile.read(matchbuf ,8);
  if (array_is_zero(reinterpret_cast<const unsigned char*>(matchbuf), 8))
  {
    std::cout << std::endl << "Skipping 8 expected zero bytes." << std::endl;
  }
  else
  {
    std::cout << std::endl << "Encountered 8 unexpected bytes:" << std::endl;
    hexdump(stdout, reinterpret_cast<const unsigned char*>(matchbuf), 8, "  ");
  }

  READ(infile, N);
  std::string filename = read2ByteStringN(infile, N);

  READ(infile, datetime);

  READ(infile, N);
  std::vector<char> version(N);

  infile.read(version.data(), N);

  READ(infile, N);

  std::cout << "Advancing to 0x440, skipping " << std::dec << (0x440 - infile.tellg()) << " bytes (not yet understood)." << std::endl;

  std::cout << std::endl
            << "Filename:          \"" << filename << "\"" << std::endl
            << "Literal timestamp: \"" << weekday(datetime.data[2]) << ", " << std::setfill('0') << std::setw(4)
            << datetime.data[0] << "-" << std::setw(2) << datetime.data[1] << "-" << datetime.data[3] << " " << std::setw(2)
            << datetime.data[4] << ":" << datetime.data[5] << ":" << datetime.data[6]
            << "\", followed by the number " << datetime.data[7] << "." << std::endl
            << "Version magic:     \"" << std::string(version.begin(), version.end())
            << "\", followed by 0x" << std::hex << std::setw(8) << std::uppercase << N << std::endl;

  infile.seekg(0x440, std::fstream::beg);

  infile.read(matchbuf, 512);
  std::string match_name = read2ByteString(matchbuf, 512);

  infile.read(matchbuf, 1024);
  std::string match_desc = read2ByteString(matchbuf, 1024);

  infile.read(matchbuf, 512);
  std::string match_map = read2ByteString(matchbuf, 512);

  std::cout << "Match name:        \"" << match_name << "\"" << std::endl
            << "Match description: \"" << match_desc << "\"" << std::endl
            << "Match map:         \"" << match_map  << "\"" << std::endl
            << std::endl;

  for (size_t i = 0; i != 10; ++i)
  {
    uint32_t player_id, player_team;
    READ(infile, player_id);
    READ(infile, player_team);
    infile.read(matchbuf, 64);
    std::string player_name = read2ByteString(matchbuf, 64);

    std::cout << "Team " << player_team << " (ID: " << std::hex << std::setw(8) << player_id << "): " << player_name << std::endl;
  }

  infile.read(matchbuf, 144);

  std::cout << std::endl;

  if (!array_is_zero(reinterpret_cast<const unsigned char*>(matchbuf), 144))
  {
    std::cout << "Unexpected data in the 144 bytes after player data:" << std::endl;
    hexdump(stdout, reinterpret_cast<const unsigned char*>(matchbuf), 144, "  ");
  }
  else
  {
    std::cout << "Skipping 144 expected zero bytes." << std::endl;
  }

  uint32_t mystery, dummy;
  READ(infile, mystery);
  READ(infile, dummy);

  std::cout << "Mysterious numbers: " << std::dec << mystery << ", " << dummy << std::endl;

  if (!parse) return 0;

  std::cout << std::endl << "Main Data:" << std::dec << std::endl << std::endl;
  infile.seekg(0xFA8, std::fstream::beg);

  for (size_t counter = 0; !infile.eof(); ++counter)
  {
    READ(infile, N);
    READ(infile, L);
    READ(infile, S);

    if (N == 0xFFFFFFFF && L == 0xFFFF) { std::cout << "Replay duration: " << timecode_to_string(Nlast) << std::endl; break; }

    Nlast = N;

    if (S > 200) { std::cout << "At position " << infile.tellg() << " we read N = " << N << ", type = " << L << ", size = " << S << std::endl; return 1; }

    std::vector<char> vbuf(S);
    infile.read(vbuf.data(), S);
    const unsigned char * const buf = reinterpret_cast<const unsigned char*>(vbuf.data());

    if (parse == 1)
    {
      std::cout << "Chunk " << counter << " (size " << S << "), timecode " << timecode_to_string(N)
                << " (" << N << "), type = " << L << ". Now at " << infile.tellg() << "." << std::endl;
      hexdump(stdout, buf, S, "  --> ");
      std::cout << std::endl;
    }
    else if (parse == 2)
    {
      if (L == 1)
      {
        std::cout << "Chunk type 1 (size " << S << "), timecode " << timecode_to_string(N) << ", number " << *reinterpret_cast<const uint16_t *>(buf)
                  << ", number of commands = " << *reinterpret_cast<const uint32_t *>(buf+2) << ". Dissecting commands:" << std::endl;
        size_t p = 6, q = p;
        while (p < S)
        {
          while (!(buf[p] == 0 && buf[p+1] == 0 && buf[p+2] == 0xFF) && p < S) p++;
          hexdump(stdout, buf + q, p-q, " -----> ");
          p += 3;
          q = p;
        }
        std::cout << std::endl;
      }
      else if (L == 2)
      {
        if (buf[1] == 1 && buf[2] == 0 && buf[7] == 5 && *reinterpret_cast<const uint32_t*>(buf+8) == N)
        {
          std::cout << "Chunk type 2 (size " << S << "), timecode " << timecode_to_string(N) << ", number "
                    << (unsigned int)(buf[0]) << ", player " << *reinterpret_cast<const uint32_t *>(buf+3) << ". Payload:" << std::endl;
          hexdump(stdout, buf + 12, S - 12, " -2-> ");
          std::cout << std::endl;
        }
        else
        {
          std::cout << "PANIC: Unexpected type-2 chunk. Size " << S << "), timecode " << timecode_to_string(N) << ". Raw data:" << std::endl;
          hexdump(stdout, buf, S, " -?-> ");
          std::cout << std::endl;
        }
      }
      else 
      {
        std::cout << "PANIC: Unknown chunk type (" << L << "). Size " << S << "), timecode " << timecode_to_string(N) << ". Raw data:" << std::endl;
        hexdump(stdout, buf, S, " -?-> ");
        std::cout << std::endl;
      }
    }
  }

  std::cout << "End of file reached normally. Footer is " << S << " bytes:" << std::endl;

  std::vector<char> footer(S);
  infile.read(footer.data(), S);
  hexdump(stdout, reinterpret_cast<unsigned char*>(footer.data()), S, "  ==> ");

  std::cout << std::endl;
}

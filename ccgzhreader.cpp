#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <map>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <ctime>

/* MingW32/Windows:
   g++ -O4 -s -o ccgzhreader ccgzhreader.cpp -march=native -fwhole-program -std=gnu++0x -fno-strict-aliasing -enable-auto-import -static-libgcc -static-libstdc++

   Linux etc.
   g++ -O4 -s -o ccgzhreader ccgzhreader.cpp -march=native -fwhole-program -std=gnu++0x -fno-strict-aliasing

 */

#define READ(f, x) do { f.read(reinterpret_cast<char*>(&x), sizeof(x)); } while (false)

std::map<uint32_t, size_t> CHUNKSIZES;
std::map<uint32_t, size_t> COMMANDSIZES;
std::map<uint32_t, size_t>::const_iterator CHIT, CCIT;

enum GameType { UNDEF = 0, CCGZH, BFME, BFME2 };

void populate_command_sizes(GameType gt)
{
  COMMANDSIZES[0x0] =  4;
  COMMANDSIZES[0x1] =  4;
  COMMANDSIZES[0x2] =  1;
  COMMANDSIZES[0x3] =  4;
  COMMANDSIZES[0x4] =  4;
  COMMANDSIZES[0x6] = 12;
  COMMANDSIZES[0x7] = 12;
  COMMANDSIZES[0x8] = 16;
  COMMANDSIZES[0x9] = (gt == BFME2 ? 4 : 16);
  COMMANDSIZES[0xA] =  4;
}

std::string timecode_to_string(unsigned int tc)
{
  std::ostringstream os;
  os << std::setw(2) << std::setfill('0') << tc/15/60 << ":" << std::setw(2) << std::setfill('0')
     << (tc/15)%60 << "::" << std::setw(2) << std::setfill('0') << tc%15;
  return os.str();
}
std::string hexstr_to_dotdec(const std::string & str)
{
  if (str.size() != 8) return "[ERROR]";
  unsigned long int x1 = strtoul(str.substr(0, 2).c_str(), NULL, 16);
  unsigned long int x2 = strtoul(str.substr(2, 2).c_str(), NULL, 16);
  unsigned long int x3 = strtoul(str.substr(4, 2).c_str(), NULL, 16);
  unsigned long int x4 = strtoul(str.substr(6, 2).c_str(), NULL, 16);
  std::ostringstream s;
  s << x1 << "." << x2 << "." << x3 << "." << x4;
  return s.str();
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

std::string read1ByteString(std::istream & in)
{
  std::string s;
  char        cbuf;

  while (true)
  {
    in.read(&cbuf, sizeof(char));

    if (cbuf == 0)
      break;

    s += cbuf;
  }

  return s;
}

std::string read2ByteString(std::istream & in)
{
  codepoint_t     ccp;
  twobytestring_t cbuf;
  std::string     s;

  while (true)
  {
    in.read(reinterpret_cast<char*>(&cbuf), sizeof(twobytestring_t));
    const unsigned int val = cbuf.byte1 + (cbuf.byte2 << 8);
    if (val == 0)  break;
    codepointToUTF8(val, &ccp);
    s += std::string(ccp.c);
  }

  return s;
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


typedef struct ccgheader_t_
{
  char magic[6];
  char time1[4];
  char time2[4];
  char number[2];
  char zero[12];
} ccgheader_t;

typedef struct bfmeheader_t_
{
  char magic[8];
  char time1[4];
  char time2[4];
  char stuff[21];
} bfmeheader_t;

typedef struct chunk_header_t_
{
  uint32_t timecode;
  uint32_t command;
  uint32_t number;
} chunk_header_t;

int main(int argc, char * argv[])
{
  if (argc < 2) return 0;

  std::cerr << "Opening file \"" << argv[1] << "... ";

  GameType gametype = UNDEF;

  std::ifstream infile(argv[1], std::fstream::binary);

  ccgheader_t cheader;
  bfmeheader_t bheader;
  chunk_header_t chead;
  char ncomms, timestr1[200], timestr2[200];
  std::string filename, version, builddate, asciiheader;
  date_text_t datetime;
  unsigned char numbers[8];
  unsigned char bfmenumbers[5];
  time_t * time1, * time2;
  uint16_t verminor, vermajor;

  uint16_t x;
  uint32_t y1, y2, y3, y4, z1, z2;

  std::memset(&cheader, 0, sizeof(cheader));
  std::memset(&bheader, 0, sizeof(bheader));
  READ(infile, cheader);

  if (std::strncmp(cheader.magic, "GENREP", 6) == 0)
  {
    std::cerr << "Treating as CCG/ZH replay." << std::endl << std::endl;
    gametype = CCGZH;
    time1 = reinterpret_cast<time_t*>(cheader.time1);
    time2 = reinterpret_cast<time_t*>(cheader.time2);
  }
  else
  {
    infile.seekg(0, std::fstream::beg);
    READ(infile, bheader);
    if (std::strncmp(bheader.magic, "BFMEREPL", 8) == 0)
    {
      std::cerr << "Treating as BMFE replay." << std::endl << std::endl;
      gametype = BFME;
      time1 = reinterpret_cast<time_t*>(bheader.time1);
      time2 = reinterpret_cast<time_t*>(bheader.time2);
    }
    else if (std::strncmp(bheader.magic, "BFME2RPL", 8) == 0)
    {
      std::cerr << "Treating as BMFE2 replay." << std::endl << std::endl;
      gametype = BFME2;
      time1 = reinterpret_cast<time_t*>(bheader.time1);
      time2 = reinterpret_cast<time_t*>(bheader.time2);
    }
    else
    {
      std::cerr << "Not a good CCG/ZH, BMFE or BMFE2 replay file." << std::endl;
      return 0;
    }
  }

  populate_command_sizes(gametype);

  filename    = read2ByteString(infile);
  READ(infile,  datetime);
  version     = read2ByteString(infile);
  builddate   = read2ByteString(infile);
  READ(infile,  verminor);
  READ(infile,  vermajor);
  READ(infile,  numbers);
  if (gametype == BFME || gametype == BFME2) READ(infile, bfmenumbers);
  asciiheader = read1ByteString(infile);

  READ(infile, x);
  READ(infile, y1);
  READ(infile, y2);
  READ(infile, y3);
  READ(infile, y4);
  if (gametype == BFME2) { READ(infile, z1); READ(infile, z2); }

  std::strftime(timestr1, 200, "%Y-%m-%d %H:%M:%S (%Z)", std::gmtime(time1));
  std::strftime(timestr2, 200, "%Y-%m-%d %H:%M:%S (%Z)", std::gmtime(time2));

  std::cout << "Timestamp 1:         " << timestr1 << std::endl
            << "Timestamp 2:         " << timestr2 << std::endl
            << "Filename:            \"" << filename << '\"' << std::endl
            << "Literal timestamp:   \"" << weekday(datetime.data[2]) << ", " << std::setfill('0') << std::setw(4)
            << datetime.data[0] << "-" << std::setw(2) << datetime.data[1] << "-" << std::setw(2) << datetime.data[3] << " " << std::setw(2)
            << datetime.data[4] << ":" << std::setw(2) << datetime.data[5] << ":" << std::setw(2) << datetime.data[6]
            << "\", followed by the number " << datetime.data[7] << "." << std::endl
            << "Version string:      \"" << version << '\"' << std::endl
            << "Some date (build?):  \"" << builddate << '\"' << std::endl
            << "Version numbers:     " << vermajor << "." << verminor << std::endl
            << "Some data (hash?):   0x" << std::hex << std::setfill('0') << std::uppercase
            << std::setw(2) << (unsigned int)(numbers[0]) << std::setw(2) << (unsigned int)(numbers[1])
            << std::setw(2) << (unsigned int)(numbers[2]) << std::setw(2) << (unsigned int)(numbers[3])
            << std::setw(2) << (unsigned int)(numbers[4]) << std::setw(2) << (unsigned int)(numbers[5])
            << std::setw(2) << (unsigned int)(numbers[6]) << std::setw(2) << (unsigned int)(numbers[7]);
  if (gametype == BFME || gametype == BFME2) std::cout << std::hex << std::setfill('0') << std::uppercase << std::setw(2) << (unsigned int)(bfmenumbers[0]) << std::setw(2) << (unsigned int)(bfmenumbers[1]) << std::setw(2) << (unsigned int)(bfmenumbers[2]) << std::setw(2) << (unsigned int)(bfmenumbers[3]) << std::setw(2) << (unsigned int)(bfmenumbers[4]);
  std::cout << std::endl
            << "After header:        " << std::dec << x << "; " << y1 << ", " << y2 << ", " << y3 << ", " << y4;
  if (gametype == BFME2) std::cout << ", " << z1 << ", " << z2;
  std::cout << "." << std::endl
            << std::endl << "Header:              " << asciiheader << std::endl << std::endl;

  std::vector<std::string> tokens, player_names;
  Tokenize(std::string(asciiheader), tokens, ";");
  
  std::cout << "Header fields:" << std::endl;
  for (size_t i = 0; i < tokens.size()-1; ++i)
  {
    std::cout << "  " << tokens[i] << std::endl;
  }
  std::cout << std::endl;

  if (tokens[tokens.size()-1][0] == 'S' && tokens[tokens.size()-1][1] == '=')
  {
    std::cout << "Found player information, parsing..." << std::endl;
    std::vector<std::string> subtokens;
    Tokenize(tokens[tokens.size()-1].substr(2), subtokens, ":");

    for (size_t i = 0; i < subtokens.size(); ++i)
    {
      if (subtokens[i][0] != 'H') continue;

      std::vector<std::string> subsubtokens;
      Tokenize(subtokens[i].substr(1), subsubtokens, ",");

      player_names.push_back(subsubtokens[0]);

      std::cout << "  Player name: " << subsubtokens[0] << ", Faction: " <<  subsubtokens[6]
                << ", IP/Port: " << hexstr_to_dotdec(subsubtokens[1]) << ":" << subsubtokens[2]
                << std::endl;
    }
    std::cout << std::endl;
  }

  while (!infile.eof())
  {
    READ(infile, chead);
    READ(infile, ncomms);

    if (infile.eof()) break;
    //if (chead.command == 0x1B && ncomms == 0) { std::cout << "Done!" << std::endl; break; }

    std::cout << "Timecode: " << std::dec << std::setw(5) << std::setfill(' ') << chead.timecode << " ("
              << timecode_to_string(chead.timecode) << "), code: 0x" << std::hex << chead.command
              << ", Number: " << chead.number << ", " << (unsigned int)(ncomms) << " commands";

    if (ncomms == 0)
    {
      std::cout << "." << std::endl;
    }
    else
    {
      char buf[2 * (unsigned int)(unsigned char)ncomms];
      READ(infile, buf);

      std::cout << ":";

      for (char i = 0; i < ncomms; i++)
      {
        uint32_t type = (uint32_t)(unsigned char)(buf[2*i]);
        size_t  nargs = (size_t)(unsigned char)(buf[2*i+1]);

        if ((CCIT = COMMANDSIZES.find(type)) == COMMANDSIZES.end())
        {
          std::cerr << std::endl << "UNKNOWN COMMAND TYPE: 0x" << std::hex << std::uppercase
                    << type << ", at position 0x" << infile.tellg() << "." << std::endl;
          return 0;
        }

        size_t argsz = CCIT->second;
        char data[nargs * argsz];
        READ(infile, data);
        std::cout << " [" << type << "/" << nargs << ": ";
        for (size_t k = 0; k < nargs; k++)
        {
          if (k != 0) std::cout << ", ";
          std::cout << std::hex << std::uppercase << std::setfill('0');
          if (argsz == 4) std::cout << "0x" << *(unsigned int*)(data + k * argsz);
          if (argsz == 1) std::cout << "0x" << std::setw(2) << (unsigned int)(*reinterpret_cast<unsigned char*>(data + k * argsz));
          if (argsz == 12) std::cout << "(0x" << *(unsigned int*)(data + k * argsz)
                                     << ", 0x" << *(unsigned int*)(data + k * argsz + 4)
                                     << ", 0x" << *(unsigned int*)(data + k * argsz + 8) << ")";

          if (argsz == 16) std::cout << "(0x" << *(unsigned int*)(data + k * argsz)
                                     << ", 0x" << *(unsigned int*)(data + k * argsz + 4)
                                     << ", 0x" << *(unsigned int*)(data + k * argsz + 8)
                                     << ", 0x" << *(unsigned int*)(data + k * argsz + 12) << ")";

        }
        std::cout << "]";
      }
      std::cout << std::endl;
    }
  }


  /*

  
  READ(infile, N);

  char * header = new char[N];
  infile.read(header, N);


  */

}

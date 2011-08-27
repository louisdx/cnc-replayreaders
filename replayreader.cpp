#include "replayreader.h"


const char * WEEKDAYS[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "[ERROR]" };
char WEEKDAY_ERROR[8];

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

    if (READ_UINT16LE(cbuf.byte1, cbuf.byte2) == 0)
      break;

    codepointToUTF8(READ_UINT16LE(cbuf.byte1, cbuf.byte2), &ccp);

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
    codepointToUTF8(READ_UINT16LE(cbuf.byte1, cbuf.byte2), &ccp);
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


std::vector<std::string> tokenize(const std::string & str, const std::string & delimiters)
{
  std::vector<std::string> tokens;

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

  return tokens;
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


std::string timecode_to_string(unsigned int tc)
{
  std::ostringstream os;
  os << tc/15/60 << ":" << std::setw(2) << std::setfill('0') << (tc/15)%60 << "::" << std::setw(2) << std::setfill('0') << tc%15;
  return os.str();
}

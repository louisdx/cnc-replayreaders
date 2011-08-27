#include "replayreader.h"

const uint32_t TERM = 0x7FFFFFFF;
const uint32_t ZERO = 0x0;
const uint32_t NONZERO = 0x43;
const char FOOTERCC[] = "C&C3 REPLAY FOOTER";
const char FOOTERRA3[] = "RA3 REPLAY FOOTER";
const char FINALCC20[] = { 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
const char FINALCC1B[] = { 0x02, 0x1B, 0x00, 0x00, 0x00 };
const char FINALRA3[] = { 0x01, 0x02, 0x24, 0x00, 0x00, 0x00 };

std::string faction(unsigned int f, Options::GameType g)
{
  switch(g)
  {
  case Options::GAME_TW:
    switch(f)
    {
    case 1:  return "Random";
    case 2:  return "Observer";
    case 3:  return "Commentator";
    case 6:  return "GDI";
    case 7:  return "Nod";
    case 8:  return "Scrin";
    default: return "[ERROR]";
    }
    break;
  case Options::GAME_KW:
    switch(f)
    {
    case 1:  return "Random";
    case 2:  return "Observer";
    case 3:  return "Commentator";
    case 6:  return "GDI";
    case 7:  return "Steel Talons";
    case 8:  return "ZOCOM";
    case 9:  return "Nod";
    case 10: return "Black Hand";
    case 11: return "Marked of Kane";
    case 12: return "Scrin";
    case 13: return "Reaper-17";
    case 14: return "Traveler-59";
    default: return "[ERROR]";
    }
    break;
  case Options::GAME_RA3:
    switch(f)
    {
    case 1:  return "Observer";
    case 3:  return "Commentator";
    case 4:  return "Allies";
    case 8:  return "Soviets";
    case 2:  return "Empire";
    case 7:  return "Random";
    default: return "[ERROR]";
    }
    break;
  default: return "[ERROR]";
  }
}

bool parse_options(int argc, char * argv[], Options & opts)
{
  int opt;

  while ((opt = getopt(argc, argv, "A:t:T:f:F:egaRcCkwrpHvh")) != -1)
  {
    switch (opt)
    {
    case 'f':
      opts.fixbroken = true;
      opts.fixpos = atoi(optarg);
      break;
    case 'F':
      opts.fixbroken = true;
      opts.fixfn = optarg;
      break;
    case 'g':
      opts.autofix = true;
      break;
    case 'e':
      opts.breakonerror = true;
      break;
    case 'a':
      opts.dumpaudio = true;
      break;
    case 'A':
      opts.audiofn = optarg;
      break;
    case 't':
      opts.type = atoi(optarg);
      break;
    case 'T':
      opts.cmd_filter = std::strtol(optarg, NULL, 0);
      break;
    case 'c':
      opts.dumpchunks = true;
      break;
    case 'C':
      opts.dumpchunks = true;
      opts.dumpchunkswithraw = true;
      break;
    case 'R':
      opts.printraw = true;
      opts.dumpchunks = true;
      break;
    case 'H':
      opts.filter_heartbeat = true;
      break;
    case 'p':
      opts.apm = true;
      break;
    case 'k':
      opts.gametype = Options::GAME_KW;
      break;
    case 'r':
      opts.gametype = Options::GAME_RA3;
      break;
    case 'w':
      opts.gametype = Options::GAME_TW;
      break;
    case 'v':
      opts.verbose = true;
      break;
    case 'h':
    default:
      std::cout << std::endl
                << "Usage:  cnc3reader [-c|-C|-R] [-a] [-A audiofilename] [-w|-k|-r] [-t type] [-g] [-e] filename [filename]..." << std::endl
                << "        cnc3reader -f pos [-F name] [-w|-k|-r] filename" << std::endl
                << "        cnc3reader -h" << std::endl << std::endl
                << "        -c:          dump chunks (smart parsing)" << std::endl
                << "        -C:          dump chunks (smart parsing), but also print raw chunks (implies '-c')" << std::endl
                << "        -R:          print raw chunk data (implies '-c')" << std::endl
                << "        -a:          dump audio data" << std::endl
                << "        -A filename: filename for audio output" << std::endl
                << "        -t type:     filter chunks of type 'type'; only effective with '-c' or '-r'" << std::endl
                << "        -T cmd:      filter type-1 chunks with command number 'cmd'; only effective with '-c'" << std::endl
                << "        -p:          gather APM statistics" << std::endl
                << "        -w, -k, -r:  interpret as Tiberium Wars / Kane's Wrath / Red Alert 3 replay (otherwise treat as Kane's Wrath if unsure)" << std::endl
                << "        -f pos:      attempt to fix the replay file from last good position pos" << std::endl
                << "        -F name:     output filename for fixed replay file" << std::endl
                << "        -g:          automatically attempt to fix broken replays" << std::endl
                << "        -e:          stop processing if an error occurs and return non-zero return value" << std::endl
                << "        -h:          print usage information (this)" << std::endl
                << std::endl;
      return false;
    }
  }

  if (opts.autofix)
  {
    std::cerr << "Will attempt to fix broken replays automatically." << std::endl;
    opts.printraw = true;
    opts.dumpchunks = true;
    opts.type = 0;
  }

  if (opts.apm)
  {
    opts.dumpchunks = true;
    opts.cmd_filter = 0xFFFF;
  }

  return true;
}

void fix_replay_file(const char * filename, Options & opts)
{
  if (opts.gametype == Options::GAME_UNDEF)
  {
    std::cerr << "You must specify the game type explicitly. Try '-h' for help." << std::endl;
    return;
  }
  else
  {
    std::cerr << "Interpreting input file as game "
              << (opts.gametype == Options::GAME_RA3 ? "Red Alert 3" : opts.gametype == Options::GAME_KW ? "Kane's Wrath" : "Tiberium Wars")
              << "." << std::endl;
  }

  std::cerr << "Fixing file " << filename << ", writing output to " << opts.fixfn << ". Opening file \"" << filename << "\"...";
  std::ifstream myfile(filename, std::ios::in | std::ios::binary);
  if (!myfile) { std::cerr << " failed!" << std::endl; return; }

  myfile.seekg(0, std::fstream::end);
  unsigned int filesize = myfile.tellg();
  std::cerr << " succeeded. File size: " << filesize << " bytes." << std::endl;

  if (filesize < opts.fixpos)
  {
    std::cerr << "Error: Specified rescue position (" << opts.fixpos << ") exceeds file size. Aborting." << std::endl;
    return;
  }

  uint32_t time_code, chunk_size;
  char     chunk_type;

  myfile.seekg(opts.fixpos, std::fstream::beg);
  myfile.read(reinterpret_cast<char*>(&time_code), 4);
  myfile.read(&chunk_type, 1);
  myfile.read(reinterpret_cast<char*>(&chunk_size), 4);

  if (myfile.eof() || filesize - myfile.tellg() < chunk_size + 4)
  {
    std::cerr << "Error: Specified rescue position (" << opts.fixpos << ") does not point to a good chunk. Aborting." << std::endl;
  }
  else
  {
    std::cerr << "OK, last good chunk found, timecode " << std::hex << time_code << ", length " << std::dec << chunk_size << std::endl;
  }

  std::ofstream yourfile(opts.fixfn, std::ios::out | std::ios::binary);
  if (!yourfile)
  {
    std::cerr << "Error opening output file \"" << opts.fixfn << "\", aborting." << std::endl;
  }

  const unsigned int BUFSIZE = 65536, rescue_target = opts.fixpos + 13 + chunk_size;
  unsigned int bit = 0;
  char buf[BUFSIZE];

  yourfile.seekp(0, std::fstream::beg);
  myfile.seekg(0, std::fstream::beg);
  while ((unsigned int)(myfile.tellg()) + BUFSIZE <= rescue_target)
  {
    myfile.read(buf, BUFSIZE);
    yourfile.write(buf, BUFSIZE);
  }
  if ((bit = rescue_target - myfile.tellg()) > 0)
  {
    myfile.read(buf, bit);
    yourfile.write(buf, bit);
  }

  std::cerr << "Rescued " << rescue_target << " bytes. Writing new footer." << std::endl;

  yourfile.write(reinterpret_cast<const char*>(&TERM), 4);
  if (opts.gametype == Options::GAME_RA3)
  {
    yourfile.write(FOOTERRA3, 17);
    yourfile.write(reinterpret_cast<char*>(&time_code), 4);
    yourfile.write(FINALRA3, 6);
    for (size_t i = 0; i < 9; ++i) yourfile.write(reinterpret_cast<const char*>(&ZERO), 4);
    yourfile.write(reinterpret_cast<const char*>(&NONZERO), 4);
  }
  else
  {
    yourfile.write(FOOTERCC, 18);
    yourfile.write(reinterpret_cast<char*>(&time_code), 4);
    yourfile.write(FINALCC1B, 5);
  }
}

bool parse_chunk1_fixlen(const unsigned char * buf, size_t & pos, size_t opos, 
                         unsigned int cmd_id, size_t counter,
                         size_t cmd_len, const Options & opts)
{
  if (buf[opos + cmd_len - 1] == 0xFF)
  {
    if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
    {
      fprintf(stdout, " %2i: Command 0x%02X, fixed length %u.\n", counter, cmd_id, cmd_len);
      hexdump(stdout, buf + opos, cmd_len, "     ");
    }
    pos += cmd_len;
  }
  else
  {
    fprintf(stdout,
            "PANIC: fixed command length (%u) for command (0x%02X) does not lead to terminator, but to 0x%02X!\n",
            cmd_len, cmd_id, buf[opos + cmd_len - 1]);
    return false;
  }
  return true;
}

bool parse_chunk1_varlen(const unsigned char * buf, size_t & pos,
                         unsigned int cmd_id, size_t counter, size_t len,
                         size_t cmd_len_byte, const Options & opts)
{
  const size_t opos = pos;

  pos += cmd_len_byte;

  while (buf[pos] != 0xFF && pos < len)
  {
    const size_t adv = (buf[pos] >> 4) + 1;

    if (opts.dumpchunkswithraw && (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id)))
    {
      fprintf(stdout, "    --> lenbyteval: %u, values:", buf[pos] & 0x0F);
      for (size_t i = 0; i != adv; ++i) fprintf(stdout, " %u", READ_UINT32LE(buf + pos + 1 + 4 * i));
      fprintf(stdout, "\n");
    }

    pos += 4 * adv + 1;
  }

  ++pos;

  if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
  {
    fprintf(stdout, " %2i: Command 0x%02X, variable length %u.\n", counter, cmd_id, pos - opos);
    hexdump(stdout, buf + opos, pos - opos, "     ");
  }

  return true;
}



command_map_t RA3_commands;
command_map_t KW_commands;
command_map_t TW_commands;
command_names_t RA3_cmd_names;
command_names_t KW_cmd_names;
command_names_t TW_cmd_names;

/* The type-1 chunk command info.
 * Commands are either of fixed length (> 0), or of variable length.
 * Variable lengths commands that receive special treatment are set to 0,
 * while those that can be treated with parse_chunk1_varlen are set to -cmd_len_byte.
 *
 * Popular cmd_len_byte values are 2 and 4. Consequently, what appears to be
 * a fixed-length commmand of length n may actually be a variable-length
 * command if (n - 4) or (n - 6) is divisible by 4.
 *
 * Fixed length commands which for which I couldn't find any such pattern
 * are marked with "// OK", meaning they are probably genuinely of fixed
 * length, and their length isn't actually part of the data.
 *
 */

void populate_command_map_TW(command_map_t & tw_commands, command_names_t & tw_cmd_names)
{
  tw_commands[0x1E] = 35; // OK
  tw_commands[0x1F] = 28; // OK
  tw_commands[0x21] = 12; // OK
  tw_commands[0x23] = 26; // OK
  tw_commands[0x24] = 22;
  tw_commands[0x25] = 17;
  tw_commands[0x26] = 17;
  tw_commands[0x32] = 21;
  tw_commands[0x34] = 16;
  tw_commands[0x3B] = 21;
  tw_commands[0x3C] = 16; // OK
  tw_commands[0x3D] = 16;
  tw_commands[0x57] = 20; // OK
  tw_commands[0x70] = 29;
  tw_commands[0x7F] = 8;
  tw_commands[0x82] = 45;
  tw_commands[0x83] = 1049;
  tw_commands[0x84] = 16; // OK
  tw_commands[0x86] = 16; // OK
  tw_commands[0x87] = 10;

  tw_commands[0x1D] = 0;
  tw_commands[0x27] = 0;

  tw_commands[0x01] = -2;
  tw_commands[0x02] = -2;
  tw_commands[0x03] = -2;
  tw_commands[0x04] = -2;
  tw_commands[0x05] = -2;
  tw_commands[0x06] = -2;
  tw_commands[0x07] = -2;
  tw_commands[0x08] = -2;
  tw_commands[0x0C] = -2;
  tw_commands[0x0D] = -2;
  tw_commands[0x1C] = -15;
  tw_commands[0x2A] = -2;
  tw_commands[0x2D] = -2;
  tw_commands[0x39] = -2;
  tw_commands[0x3A] = -2;
  tw_commands[0x42] = -2;
  tw_commands[0x43] = -2;
  tw_commands[0x68] = -2;
  tw_commands[0x6D] = -2;
  tw_commands[0x74] = -2;
  tw_commands[0x75] = -2;
  tw_commands[0x7D] = -2;
  tw_commands[0x85] = -2;
  tw_commands[0xF5] = -4;
  tw_commands[0xF6] = -4;
  tw_commands[0xF8] = -4;
  tw_commands[0xF9] = -2;
  tw_commands[0xFA] = -2;
  tw_commands[0xFB] = -2;
  tw_commands[0xFC] = -2;
  tw_commands[0xFD] = -2;
  tw_commands[0xFE] = -2;
}

void populate_command_map_KW(command_map_t & kw_commands, command_names_t & kw_cmd_names)
{
  kw_commands[0x05] = 3;
  kw_commands[0x06] = 3;
  kw_commands[0x07] = 3;
  kw_commands[0x27] = 40;
  kw_commands[0x29] = 28;
  kw_commands[0x2B] = 17;
  kw_commands[0x2E] = 22;
  kw_commands[0x2F] = 17;
  kw_commands[0x30] = 17;
  kw_commands[0x34] = 8;
  kw_commands[0x35] = 12;
  kw_commands[0x36] = 13;
  kw_commands[0x3C] = 21;
  kw_commands[0x3E] = 16;
  kw_commands[0x3D] = 21;
  kw_commands[0x43] = 12;
  kw_commands[0x44] = 8;
  kw_commands[0x45] = 21;
  kw_commands[0x46] = 16;
  kw_commands[0x47] = 16;
  kw_commands[0x4C] = 3;
  kw_commands[0x61] = 20;
  kw_commands[0x72] = 8;
  kw_commands[0x77] = 3;
  kw_commands[0x7A] = 29;
  kw_commands[0x7E] = 12;
  kw_commands[0x7F] = 12;
  kw_commands[0x87] = 8;
  kw_commands[0x89] = 8;
  kw_commands[0x8C] = 45;
  kw_commands[0x8D] = 1049;
  kw_commands[0x8E] = 16;
  kw_commands[0x8F] = 40;
  kw_commands[0x92] = 8;
  kw_commands[0xF8] = 5;
  kw_commands[0xFD] = 8;

  kw_commands[0x26] = 0;
  kw_commands[0x2D] = 0;
  kw_commands[0x31] = 0;
  kw_commands[0xF5] = 0;
  kw_commands[0xF6] = 0;
  kw_commands[0xF9] = 0;
  kw_commands[0xFB] = 0;
  kw_commands[0xFC] = 0;
}

void populate_command_map_RA3(command_map_t & ra3_commands, command_names_t & ra3_cmd_names)
{
  ra3_commands[0x00] = 45;
  ra3_commands[0x03] = 17;
  ra3_commands[0x04] = 17;
  ra3_commands[0x05] = 20; // OK
  ra3_commands[0x06] = 20; // OK
  ra3_commands[0x07] = 17;
  ra3_commands[0x08] = 17;
  ra3_commands[0x09] = 35;
  ra3_commands[0x0F] = 16;
  ra3_commands[0x14] = 16; // OK
  ra3_commands[0x15] = 16; // OK
  ra3_commands[0x16] = 16; // OK
  ra3_commands[0x21] = 20; // OK
  ra3_commands[0x28] = -2;
  ra3_commands[0x29] = -2;
  ra3_commands[0x2A] = -2;
  ra3_commands[0x2C] = 29; // OK
  ra3_commands[0x2E] = -2;
  ra3_commands[0x2F] = -2;
  ra3_commands[0x32] = 53;
  ra3_commands[0x34] = 45;
  ra3_commands[0x35] = 1049;
  ra3_commands[0x36] = 16; //OK
  ra3_commands[0x5F] = 11;

  ra3_commands[0x01] = 0;
  ra3_commands[0x02] = 0;
  ra3_commands[0x0C] = 0;
  ra3_commands[0x10] = 0;
  ra3_commands[0x33] = 0;
  ra3_commands[0x4B] = 0;

  //  ra3_commands[0x4D] = 0;

  ra3_commands[0x0A] = -2;
  ra3_commands[0x0D] = -2;
  ra3_commands[0x0E] = -2;
  ra3_commands[0x12] = -2;
  ra3_commands[0x1A] = -2;
  ra3_commands[0x1B] = -2;
  ra3_commands[0x37] = -2;
  ra3_commands[0x47] = -2;
  ra3_commands[0x48] = -2;
  ra3_commands[0x4C] = -2;
  ra3_commands[0x4E] = -2;
  ra3_commands[0x52] = -2;
  ra3_commands[0xF8] = -4;
  ra3_commands[0xFB] = -2;
  ra3_commands[0xFC] = -2;
  ra3_commands[0xFD] = -7;
  ra3_commands[0xF5] = -5;
  ra3_commands[0xF6] = -5;
  ra3_commands[0xF9] = -2;
  ra3_commands[0xFA] = -7;
  ra3_commands[0xFE] = -15;
  ra3_commands[0xFF] = -34;

  ra3_cmd_names[0xFA] = "create group";
}

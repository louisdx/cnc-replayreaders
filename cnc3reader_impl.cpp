#include "replayreader.h"

const uint32_t TERM = 0x7FFFFFFF;
const char FOOTERCC[] = "C&C3 REPLAY FOOTER";
const char FOOTERRA3[] = "RA3 REPLAY FOOTER";
      char FINAL[] = { 0x02, 0x7F, 0x00, 0x00, 0x00 };

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

  while ((opt = getopt(argc, argv, "A:t:T:f:F:egaRcCkwrpP:H:vh")) != -1)
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
      opts.type = parse_int_sequence_arg(optarg);
      break;
    case 'T':
      opts.cmd_filter = parse_int_sequence_arg(optarg);
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
      opts.filter_heartbeat =  std::strtol(optarg, NULL, 0);
      break;
    case 'p':
      opts.apm = true;
      break;
    case 'P':
      opts.time_series_filter = parse_int_sequence_arg(optarg);
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
                << "Usage:  cnc3reader [-c|-C|-R] [-a] [-A audiofilename] [-w|-k|-r] [-t type] [-T cmd] [-g] [-e] [-p] [-P cmd] filename [filename]..." << std::endl
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
                << "        -P cmd:      display time series for commands" << std::endl
                << "        -w, -k, -r:  interpret as Tiberium Wars / Kane's Wrath / Red Alert 3 replay (otherwise treat as Kane's Wrath if unsure)" << std::endl
                << "        -f pos:      attempt to fix the replay file from last good position pos" << std::endl
                << "        -F name:     output filename for fixed replay file" << std::endl
                << "        -g:          automatically attempt to fix broken replays" << std::endl
                << "        -e:          stop processing if an error occurs and return non-zero return value" << std::endl
                << "        -h:          print usage information (this)" << std::endl
                << std::endl << "  The filters -t, -T and -P accept a comma-separated series of values, for example \"-t 3,4\"." << std::endl
                << std::endl;
      return false;
    }
  }

  if (opts.autofix)
  {
    std::cerr << "Will attempt to fix broken replays automatically." << std::endl;
    opts.printraw = true;
    opts.dumpchunks = true;
    opts.type.clear();
    opts.type.insert(0);
  }

  if (opts.apm)
  {
    opts.dumpchunks = true;
    opts.cmd_filter.clear();
    opts.cmd_filter.insert(0xFFFF);
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
    return;
  }
  else
  {
    std::cerr << "OK, last good chunk found, timecode " << std::hex << time_code << ", length " << std::dec << chunk_size << std::endl;
  }

  std::ofstream yourfile(opts.fixfn, std::ios::out | std::ios::binary);
  if (!yourfile)
  {
    std::cerr << "Error opening output file \"" << opts.fixfn << "\", aborting." << std::endl;
    return;
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
  if (opts.gametype == Options::GAME_RA3) { FINAL[1] = 0x1A; yourfile.write(FOOTERRA3, 17); }
  else                                    { FINAL[1] = 0x1B; yourfile.write(FOOTERCC,  18); }
  yourfile.write(reinterpret_cast<char*>(&time_code), 4);
  yourfile.write(FINAL, 5);
}

bool parse_chunk1_fixlen(const unsigned char * buf, size_t & pos, size_t opos, 
                         unsigned int cmd_id, size_t counter,
                         size_t cmd_len, const Options & opts)
{
  if (buf[opos + cmd_len - 1] == 0xFF)
  {
    if (!is_filtered(int(cmd_id), opts.cmd_filter))
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

    if (opts.dumpchunkswithraw && (!is_filtered(int(cmd_id), opts.cmd_filter)))
    {
      fprintf(stdout, "    --> lenbyteval: %u, values:", buf[pos] & 0x0F);
      for (size_t i = 0; i != adv; ++i) fprintf(stdout, " %u", READ_UINT32LE(buf + pos + 1 + 4 * i));
      fprintf(stdout, "\n");
    }

    pos += 4 * adv + 1;
  }

  ++pos;

  if (!is_filtered(int(cmd_id), opts.cmd_filter))
  {
    fprintf(stdout, " %2i: Command 0x%02X, variable length %u.\n", counter, cmd_id, pos - opos);
    hexdump(stdout, buf + opos, pos - opos, "     ");
  }

  return true;
}

bool parse_chunk1_uuid(const unsigned char * buf, size_t & pos, size_t len, unsigned int cmd_id, size_t counter, const Options & opts)
{
  size_t l = buf[pos + 3];
  if (len < l) return false;

  std::string s1(buf + pos + 4, buf + pos + 4 + l);

  if (!is_filtered(int(cmd_id), opts.cmd_filter))
    fprintf(stdout, " %2i: Command 0x%02X: First string length %u, \"%s\".", counter, cmd_id, l, s1.c_str());

  pos += l + 5;

  l = buf[pos];
  if (len < l) return false;

  std::string s2 = read2ByteString((const char*)buf + pos + 1, 2 * l);

  pos += 2 * l + 2;
  if (len < l) return false;

  if (!is_filtered(int(cmd_id), opts.cmd_filter))
    fprintf(stdout, " Second string length %u, \"%s\". Number: 0x%08X.\n", l, s2.c_str(), READ_UINT32LE(buf + pos));

  pos += 5;

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
  tw_commands[0x33] = 21;
  tw_commands[0x34] = 16;
  tw_commands[0x3B] = 21;
  tw_commands[0x3C] = 16; // OK
  tw_commands[0x3D] = 16;
  tw_commands[0x3E] = 16;
  tw_commands[0x51] = 16;
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
  tw_commands[0x81] = 0;

  tw_commands[0x00] = -2;
  tw_commands[0x01] = -2;
  tw_commands[0x02] = -2;
  tw_commands[0x03] = -2;
  tw_commands[0x04] = -2;
  tw_commands[0x05] = -2;
  tw_commands[0x06] = -2;
  tw_commands[0x07] = -2;
  tw_commands[0x08] = -2;
  tw_commands[0x09] = -2;
  tw_commands[0x0A] = -2;
  tw_commands[0x0C] = -2;
  tw_commands[0x0D] = -2;
  tw_commands[0x0F] = -2;
  tw_commands[0x10] = -2;
  tw_commands[0x1C] = -15;
  tw_commands[0x22] = -2;
  tw_commands[0x2A] = -2;
  tw_commands[0x2B] = -2;
  tw_commands[0x2C] = -2;
  tw_commands[0x2D] = -2;
  tw_commands[0x39] = -2;
  tw_commands[0x3A] = -2;
  tw_commands[0x42] = -2;
  tw_commands[0x43] = -2;
  tw_commands[0x68] = -2;
  tw_commands[0x69] = -2;
  tw_commands[0x6D] = -2;
  tw_commands[0x74] = -2;
  tw_commands[0x75] = -2;
  tw_commands[0x7D] = -2;
  tw_commands[0x85] = -2;
  tw_commands[0x88] = -2;
  tw_commands[0xF5] = -4;
  tw_commands[0xF6] = -4;
  tw_commands[0xF8] = -4;
  tw_commands[0xF9] = -2;
  tw_commands[0xFA] = -2;
  tw_commands[0xFB] = -2;
  tw_commands[0xFC] = -2;
  tw_commands[0xFD] = -2;
  tw_commands[0xFE] = -2;
  tw_commands[0xFF] = -2;

  tw_cmd_names[0x57] = "30s heartbeat";
  tw_cmd_names[0x85] = "'scroll'";
  tw_cmd_names[0xF5] = "drag selection box and/or select units/structures";
  tw_cmd_names[0xF8] = "left click";
}

void populate_command_map_KW(command_map_t & kw_commands, command_names_t & kw_cmd_names)
{
  kw_commands[0x01] = -2;
  kw_commands[0x02] = -2;
  kw_commands[0x03] = -2;
  kw_commands[0x04] = -2;
  kw_commands[0x05] = -2;
  kw_commands[0x06] = -2;
  kw_commands[0x07] = -2;
  kw_commands[0x08] = -2;
  kw_commands[0x09] = -2;
  kw_commands[0x0B] = -2;
  kw_commands[0x0C] = -2;
  kw_commands[0x0D] = -2;
  kw_commands[0x0F] = -2;
  kw_commands[0x10] = -2;
  kw_commands[0x11] = -2;
  kw_commands[0x12] = -2;
  kw_commands[0x17] = -2;

  kw_commands[0x27] = -34;
  kw_commands[0x29] = 28;
  kw_commands[0x2B] = -11;
  kw_commands[0x2C] = 17;
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
  kw_commands[0x48] = 16;
  kw_commands[0x5B] = 16;
  kw_commands[0x61] = 20;
  kw_commands[0x72] = -2;
  kw_commands[0x73] = -2;
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
  kw_commands[0x90] = 16;
  kw_commands[0x91] = 10;

  kw_commands[0x28] = 0;
  kw_commands[0x2D] = 0;
  kw_commands[0x31] = 0;
  kw_commands[0x8B] = 0;

  kw_commands[0x26] = -15;
  kw_commands[0x4C] = -2;
  kw_commands[0x4D] = -2;
  kw_commands[0x92] = -2;
  kw_commands[0x93] = -2;
  kw_commands[0xF5] = -4;
  kw_commands[0xF6] = -4;
  kw_commands[0xF8] = -4;
  kw_commands[0xF9] = -2;
  kw_commands[0xFA] = -2;
  kw_commands[0xFB] = -2;
  kw_commands[0xFC] = -2;
  kw_commands[0xFD] = -2;
  kw_commands[0xFE] = -2;
  kw_commands[0xFF] = -2;

  kw_cmd_names[0x61] = "30s heartbeat";
  kw_cmd_names[0x8F] = "'scroll'";
  kw_cmd_names[0xF5] = "drag selection box and/or select units/structures";
  kw_cmd_names[0xF8] = "left click";
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
  ra3_commands[0x2C] = 29; // OK
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
  ra3_commands[0x28] = -2;
  ra3_commands[0x29] = -2;
  ra3_commands[0x2A] = -2;
  ra3_commands[0x2E] = -2;
  ra3_commands[0x2F] = -2;
  ra3_commands[0x37] = -2;
  ra3_commands[0x47] = -2;
  ra3_commands[0x48] = -2;
  ra3_commands[0x4C] = -2;
  ra3_commands[0x4E] = -2;
  ra3_commands[0x52] = -2;
  ra3_commands[0xF5] = -5;
  ra3_commands[0xF6] = -5;
  ra3_commands[0xF8] = -4;
  ra3_commands[0xF9] = -2;
  ra3_commands[0xFA] = -7;
  ra3_commands[0xFB] = -2;
  ra3_commands[0xFC] = -2;
  ra3_commands[0xFD] = -7;
  ra3_commands[0xFE] = -15;
  ra3_commands[0xFF] = -34;

  ra3_cmd_names[0x02] = "set rally point";
  ra3_cmd_names[0x03] = "start/resume research upgrade";
  ra3_cmd_names[0x04] = "pause/cancel research upgrade";
  ra3_cmd_names[0x05] = "start/resume unit construction";
  ra3_cmd_names[0x06] = "pause/cancel unit construction";
  ra3_cmd_names[0x07] = "start/resume structure construction";
  ra3_cmd_names[0x08] = "pause/cancel structure construction";
  ra3_cmd_names[0x09] = "place structure";
  ra3_cmd_names[0x0A] = "sell structure";

  ra3_cmd_names[0x0C] = "ungarrison structure (?)";
  ra3_cmd_names[0x0D] = "attack";
  ra3_cmd_names[0x0E] = "force-fire";

  ra3_cmd_names[0x10] = "garrison structure";

  ra3_cmd_names[0x14] = "move unit";
  ra3_cmd_names[0x15] = "attack-move unit";
  ra3_cmd_names[0x16] = "force-move unit";
  ra3_cmd_names[0x1A] = "stop unit";

  ra3_cmd_names[0x21] = "3s heartbeat";

  ra3_cmd_names[0x28] = "start repair structure";
  ra3_cmd_names[0x29] = "stop repair structure";
  ra3_cmd_names[0x2A] = "'Q' select";
  ra3_cmd_names[0x2C] = "formation-move preview";
  ra3_cmd_names[0x2E] = "stance change";
  ra3_cmd_names[0x2F] = "waypoint/planning mode (?)";
  ra3_cmd_names[0x37] = "'scroll'";
  ra3_cmd_names[0x4E] = "player power";
  ra3_cmd_names[0xF5] = "drag selection box and/or select units/structures";
  ra3_cmd_names[0xF8] = "left click";
  ra3_cmd_names[0xF9] = "unit ungarrisons structure (automatic event) (?)";
  ra3_cmd_names[0xFA] = "create group";
  ra3_cmd_names[0xFB] = "select group";
}

/* This helper function computes the player number 0, 1, 2, ... from
 * the player number in the type-1 chunk commands, 0x19, 0x1A, etc.
 */
inline unsigned int mangle_player(unsigned int n, Options::GameType gametype)
{
  return n / 8 - (gametype == Options::GAME_RA3 ? 2 : 3);
}

bool dumpchunks(const unsigned char * buf, char chunktype, unsigned int chunklen, unsigned int timecode,
                unsigned char hsix, unsigned char hnumber1, std::ostream & audioout,
                apm_1_map_t & player_1_apm, apm_2_map_t & player_2_apm,
                apm_histo_map_t & player_indi_histo_apm, apm_histo_map_t & player_coal_histo_apm,
                unsigned int block_count, Options::GameType gametype, const Options & opts)
{
      const command_map_t & commands = gametype == Options::GAME_TW ? TW_commands
        : (gametype == Options::GAME_KW ? KW_commands : RA3_commands);

      // Chunk type 1
      if (chunktype == 1 && buf[0] == 1 && buf[chunklen-1] == 0xFF && READ_UINT32LE(buf+chunklen) == 0)
      {
        if (is_filtered(1, opts.type)) return true;

        const size_t ncommands = READ_UINT32LE(buf+1);

        if (!opts.apm && opts.dumpchunkswithraw)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number of commands: %u."
                  //" Payload:"
                  "\n  Dissecting chunk commands.\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, chunktype, ncommands);
        }

        /* We've completed the dissector, no more need for the raw dump! */
        if (opts.dumpchunkswithraw)
          hexdump(stdout, buf+5, chunklen-5, "  ");

        /* This next line was used during the learning phase to gather command statistics. */
        //if (ncommands == 1) fprintf(stdout, "MASTERPLAN 0x%02X %u\n", (int)buf[5], chunklen-5);

        size_t pos = 5, opos = pos, counter;

        for (counter = 1 ; ; counter++)
        {
          const unsigned int cmd_id = buf[opos];
          const unsigned int player_id = buf[opos + 1];

          player_indi_histo_apm[player_id][cmd_id].insert(timecode);;
          player_coal_histo_apm[mangle_player(player_id, gametype)][cmd_id].insert(timecode);
          ++player_1_apm[mangle_player(player_id, gametype)];

          command_map_t::const_iterator c = commands.find(cmd_id);

          if      (c != commands.end() && c->second > 0)  // Fixed-length commands
          {
            if (!parse_chunk1_fixlen(buf, pos, opos, cmd_id, counter, c->second, opts)) break;
          }
          else if (c != commands.end() && c->second < 0)  // variable-length commands
          {
            if (!parse_chunk1_varlen(buf, pos, cmd_id, counter, chunklen, -c->second, opts)) break;
          }
          else if (c != commands.end() && c->second <= 0) // special-length commands
          {
            char s[10] = { ' ', ' ', ' ', ' ', ' ', 0 };

            if (gametype == Options::GAME_RA3)
            {
              if (cmd_id == 0x0C)
              {
                size_t l = buf[pos + 3] + 1;
                pos += 4 * l + 5;
                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x01)
              {
                if (buf[pos + 2] == 0xFF)
                {
                  pos += 3;
                }
                else if (buf[pos + 7] == 0xFF)
                {
                  pos += 8;
                }
                else
                {
                  const size_t l = buf[pos + 17] + 1;
                  pos += 4 * l + 32;
                }
                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x02)
              {
                const size_t l = (buf[pos + 24] + 1) * 2 + 26;
                pos += l;

                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x10) /* 0x10 is special, it has two possible lengths, 12 or 13 */
              {
                const size_t l = buf[pos + 2] == 0x14 ? 12 : (buf[pos + 2] == 0x04 ? 13 : 99999);

                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, l);

                pos += l;
              }
              else if (cmd_id == 0x4B) /* 0x4B is special, it has two possible lengths, 8 or 16 */
              {
                const size_t l = buf[pos + 2] == 0x04 ? 8 : (buf[pos + 2] == 0x07 ? 16 : 99999);

                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, l);

                pos += l;
              }
              else if (cmd_id == 0x33)
              {
                parse_chunk1_uuid(buf, pos, chunklen, cmd_id, counter, opts);
              }
              else
              {
                fprintf(stdout, "Warning: Unrecognized variable-length command.\n");
                while (buf[pos] != 0xFF && pos < chunklen) pos++;
                if (buf[pos] != 0xFF) fprintf(stdout, "Panic: could not find terminator!\n");
                pos++;
                sprintf(s, " %2i: ", counter);
              }
            }
            else if (gametype == Options::GAME_KW)
            {
              if (cmd_id == 0x31)
              {
                size_t l = buf[pos + 12];
                pos += l * 18 + 17;
                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x28)
              {
                pos += (buf[pos + 17] + 1) * 4 + 32;

                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }

              }
              else if (cmd_id == 0x2D)
              {
                pos += buf[pos + 7] == 0xFF ? 8 : 26;

                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }

              }
              else if (cmd_id == 0x8B)
              {
                parse_chunk1_uuid(buf, pos, chunklen, cmd_id, counter, opts);
              }
              else
              {
                fprintf(stdout, "Warning: Unrecognized variable-length command.\n");
                while (buf[pos] != 0xFF && pos < chunklen) pos++;
                if (buf[pos] != 0xFF) fprintf(stdout, "Panic: could not find terminator!\n");
                pos++;
                sprintf(s, " %2i: ", counter);
              }
            }
            else if (gametype == Options::GAME_TW)
            {
              if (cmd_id == 0x1D)
              {
                if (buf[pos + 30] == 0xFF)
                {
                  pos += 35;
                }
                else
                {
                  pos += 32 + 4 * (buf[pos + 30] + 1);
                }
                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x27)
              {
                size_t l = buf[pos + 12];
                pos += l * 18 + 17;
                if (!is_filtered(int(cmd_id), opts.cmd_filter))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x81)
              {
                parse_chunk1_uuid(buf, pos, chunklen, cmd_id, counter, opts);
              }
              else
              {
                fprintf(stdout, "Warning: Unrecognized variable-length command.\n");
                while (buf[pos] != 0xFF && pos < chunklen) pos++;
                if (buf[pos] != 0xFF) fprintf(stdout, "Panic: could not find terminator!\n");
                pos++;
                sprintf(s, " %2i: ", counter);
              }
            }

            if (!is_filtered(int(cmd_id), opts.cmd_filter))
            {
              hexdump(stdout, buf + opos, pos - opos, s);
            }
          }
          else if (c == commands.end()) // we are missing information!
          {
            fprintf(stdout, "Warning: Unknown command type: 0x%02X\n", cmd_id);
            break;
          }
          else // obsolete, this code just searches naively for an 0xFF "terminator"
          {
            while (buf[pos] != 0xFF && pos < chunklen) pos++;

            if (buf[pos] != 0xFF) fprintf(stdout, "Panic: could not find terminator!\n");

            char s[10] = { 0 };
            pos++;
            sprintf(s, " %2i: ", counter);

            hexdump(stdout, buf + opos, pos - opos, s);
          }

          opos = pos;
          if (pos == chunklen) break;
        }

        if (!opts.apm && opts.dumpchunkswithraw) fprintf(stdout, "\n");

        if (counter > ncommands) { fprintf(stdout, "Panic: Too many commands dissected!\n\n"); }
      }

      // Chunk type 2
      else if ((chunktype == 2 && buf[0] == 1 && buf[1] == 0 && READ_UINT32LE(buf+7) == timecode && READ_UINT32LE(buf+chunklen) == 0) &&
               ((buf[6] == 0x0F && gametype == Options::GAME_RA3) || buf[6] == 0x0E))
      {
        const unsigned int player_id = READ_UINT32LE(buf + 2);

        if (opts.apm)
        {
          // Counters: 0 - heartbeat, 1 - other 40 byte, 2 - 24 byte

          if (chunklen == 40 && (timecode % 15 == 0 || timecode == 1)) player_2_apm[player_id].counter[0]++;
          else if (chunklen == 40) player_2_apm[player_id].counter[1]++;
          else if (chunklen == 24) player_2_apm[player_id].counter[2]++;
          else                     player_2_apm[player_id].counter[3]++;
        }

        if (is_filtered(2, opts.type)) return true;

        if (opts.type.count(2) != 0 && opts.filter_heartbeat == 1 && timecode % 15 && timecode != 1) return true;
        if (opts.type.count(2) != 0 && opts.filter_heartbeat == 0 && (timecode % 15 == 0 || timecode == 1)) return true;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number (Player ID?): %u. Payload:\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, chunktype, player_id);

          if (opts.dumpchunkswithraw)
            hexdump(stdout, buf+11, chunklen-11, "  ");

          fprintf(stdout, "  As floats:");
          for (size_t i = 12; i + 4 <= chunklen; i += 4)
            fprintf(stdout, " %7.2f", *reinterpret_cast<const float*>(buf + i));
          fprintf(stdout, "\n\n");
        }
      }

      // Chunk type 3 (audio?)
      else if (chunktype == 3 && buf[0] == 1 && buf[1] == 0 && buf[6] == 0x0D &&
               READ_UINT32LE(buf+7) == timecode && READ_UINT32LE(buf+chunklen) == 0 && hsix  == 0x1E)
      {
        if (opts.dumpaudio && audioout)
        {
          fprintf(stderr, "  writing audio chunk 0x%02X%02X (length: %2u bytes vs. %2u)...\n", buf[11], buf[12], READ_UINT16LE(buf[13], buf[14]), chunklen-15);

          if(READ_UINT16LE(buf[13], buf[14]) != chunklen-15)
            throw std::length_error("Unexpected audio track chunk.");

          audioout.write(reinterpret_cast<const char*>(buf) + 15, chunklen - 15);
        }

        if (is_filtered(3, opts.type)) return true;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number (Player ID?): %u. Audio counter: %u. Payload:\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, chunktype, READ_UINT32LE(buf+2), READ_UINT16LE(buf+11, buf+12));
          hexdump(stdout, buf+11, chunklen-11, "  ");
          fprintf(stdout, "\n");
        }
      }

      // Chunk type 3 (empty)
      else if (chunktype == 3 && buf[0] == 0 && buf[1] == 0 && chunklen == 2 && READ_UINT32LE(buf+chunklen) == 0 && hsix  == 0x1E)
      {
        if (is_filtered(3, opts.type)) return true;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Empty chunk.\n\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, chunktype);
        }
      }

      // Chunk type 4 (regular)
      else if (chunktype == 4 && buf[0] == 1 && buf[1] == 0 && buf[6] == 0x0F &&
               READ_UINT32LE(buf+7) == timecode && READ_UINT32LE(buf+chunklen) == 0 && hsix  == 0x1E)
      {
        if (is_filtered(4, opts.type)) return true;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number (Player ID?): %u. Payload:\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, chunktype, READ_UINT32LE(buf+2));
          hexdump(stdout, buf+11, chunklen-11, "  ");
          fprintf(stdout, "\n");
        }
      }

      // Chunk type 4 (empty)
      else if (chunktype == 4 && buf[0] == 0 && buf[1] == 0 && chunklen == 2 && READ_UINT32LE(buf+chunklen) == 0 && hsix  == 0x1E)
      {
        if (is_filtered(4, opts.type)) return true;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Empty chunk.\n\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, chunktype);
        }
      }

      // Chunk type 1, skirmish, empty chunk
      else if (chunktype == 1 && chunklen == 5 && hnumber1 == 4 && READ_UINT32LE(buf+chunklen) == 0 && READ_UINT32LE(buf+1) == 0 && buf[0] == 1)
      {
        if (is_filtered(1, opts.type)) return true;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Empty chunk (skirmish only).\n\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, chunktype);
        }
      }

      // Chunk type 254 (RA3 only)
      else if (chunktype == -2 && READ_UINT32LE(buf+chunklen) == 0)
      {
        if (is_filtered(-2, opts.type)) return true;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %d. Raw data:\n",
                  timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, (int)(chunktype));
          hexdump(stdout, buf, chunklen, "  ");
          fprintf(stdout, "\n");
        }
      }

      // Otherwise: Panic!
      else
      {
        fprintf(stderr, "\n************** Warning: Unexpected chunk data!\n");
        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %d. Raw data:\n",
                timecode, timecode_to_string(timecode).c_str(), block_count, chunklen, (int)(chunktype));
        hexdump(stdout, buf, chunklen+4, "XYZZY   ");
        fprintf(stdout, "\n");

        return false;
      }
      return true;
}

/*****************************
 * Schneider's Replay Reader
 * Handle with care.        
 *
 * Compile like this:
 *  g++ -O2 -s -o cnc3reader.exe cnc3reader.cpp -enable-auto-import -static-libgcc -static-libstdc++ -fwhole-program

 *
 *****************************/

/*** Factions ***

 ** Kane's Wrath **

 14: T59
 13: R17
 12: Scrin
 11: MoK
 10: BH
  9: Nod
  8: ZOCOM
  7: ST
  6: GDI
  3: Commentator
  2: Observer
  1: Random

  ** Tiberium Wars

  8: Scrin
  7: Nod
  6: GDI
  3: Commentator
  2: Observer
  1: Random
  

  ** Red Alert

  1: Observer
  3: Commentator
  7: Random
  2: Empire
  4: Allies
  8: Soviets

  ***/


/*** Canonical chunk types ***
 *

  // Full chunk structure:
  {
    uint32           time_code;
    byte             chunk_type;   // 1, 2, 3 or 4
    uint32           chunk_size;
    byte[chunk_size] data;
    uint32           zero;         // == 0
  }
  // Repeat until chunk_number == 0x7FFFFFF


  Let us call (data[0], data[1]) the "chunk signature". chunk_type 2 has signature
  (1,0),  while chunk_type 1 has signature (1,x), x = 1,2,3,...

  There seem to be two classes of chunks of chunk_type 1: Those of signature (1,0),
  and those of signature (1,x), where x=1,2,3,... is some positive integer.

  These chunks appear to have the following "canonical" structure.

  Type 1:
    {
      byte;     // == 1
      uint32 n; // number of commands
      byte[chunk_size-5] payload; // each command terminated by { 0x00, 0x00, 0x00, 0xFF }
    }

  Type 2:
    {
      byte[2];  // == { 1, 0 }
      uint32 n; // maybe the player number, 0 <= n < number_of_players ?
      byte;     // == 0x0E in TW/KW or 0x0F in RA3
      uint32 c; // == time_code;
      byte[chunk_size-11] payload;
    }

  Type 3: // only occurs when six == 0x1E?
    {
      byte[2];  // == { 1, 0 }
      uint32 n; // maybe the player number, 0 <= n < number_of_players ?
      byte;     // == 0x0D
      uint32 c; // == chunk_number;
      byte[chunk_size-11] payload;
    }

  Type 4: // only occurs when six == 0x1E?
    {
      byte[2];  // == { 1, 0 }
      uint32 n; // maybe the player number, 0 <= n < number_of_players ?
      byte;     // == 0x0F
      uint32 c; // == chunk_number;
      byte[chunk_size-11] payload;
    }

    Types 3 and 4 also exist as empty versions of length 2 and content { 0 ,0 }.
    Note that types 3 and 4 seem to only exist when there is a commentary track.
    Type 1 exists as an empty version of length 5 in skirmish replays.

  Update: In RA3, there also seems to be a type 0xFE = 254.


 ******************************/

#include "cnc3reader.h"


command_map_t RA3_commands;
command_map_t KW_commands;
command_map_t TW_commands;
command_names_t RA3_cmd_names;
command_names_t KW_cmd_names;
command_names_t TW_cmd_names;


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

const uint32_t TERM = 0x7FFFFFFF;
const uint32_t ZERO = 0x0;
const uint32_t NONZERO = 0x43;
const char FOOTERCC[] = "C&C3 REPLAY FOOTER";
const char FOOTERRA3[] = "RA3 REPLAY FOOTER";
const char FINALCC20[] = { 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 };
const char FINALCC1B[] = { 0x02, 0x1B, 0x00, 0x00, 0x00 };
const char FINALRA3[] = { 0x01, 0x02, 0x24, 0x00, 0x00, 0x00 };

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

/* This helper function computes the player number 0, 1, 2, ... from
 * the player number in the type-1 chunk commands, 0x19, 0x1A, etc.
 */
inline unsigned int mangle_player(unsigned int n, Options::GameType gametype)
{
  return n / 8 - (gametype == Options::GAME_RA3 ? 2 : 3);
}

bool parse_replay_file(const char * filename, Options & opts)
{
  Options::GameType gametype = opts.gametype;;

  header_cnc3_t header;
  header_ra3_t header_ra3;
  std::string str_title, str_matchdesc, str_mapname, str_mapid, str_filename, str_vermagic, str_anothername;
  std::vector<std::string> playerNames;
  std::vector< std::vector<std::string> > playerNames2;
  std::vector<int>         playerIDs;
  std::vector<int>         playerNos;

  char     nplayers, onebyte, cncrpl_magic[8], dummy3[9], cncfooter_magic[18], timeout[200];
  uint32_t player_id, hlen, after_vermagic, dummy, lastgood, firstchunk;

  unsigned char u33[33], u31[31], hnumber1, hsix;
  date_text_t datetime;
  unknown_uints_t<19> u19;
  unknown_uints_t<20> u20;

  std::cerr << "Opening file \"" << filename << "\"...";
  std::ifstream myfile(filename, std::ios::in | std::ios::binary);
  if (!myfile) { std::cerr << " failed!" << std::endl; return false; }

  myfile.seekg(0, std::fstream::end);
  int filesize = myfile.tellg();
  std::cerr << " succeeded. File size: " << filesize << " bytes." << std::endl;

  std::ofstream audioout;

  if (opts.type != -1)
    std::cerr << "Displaying only events of type " << opts.type << "." << std::endl;

  if (opts.dumpaudio)
  {
    if (opts.audiofn == NULL)
    {
      std::cerr << "Error: You must specify the audio dump filename with the \"-A\" option." << std::endl;
      return false;
    }

    std::cerr << "Attempting to dump audio tracks!" << std::endl;
    audioout.open(opts.audiofn, std::ios::out | std::ios::binary | std::ios::trunc);

    if (!audioout)
    {
      std::cerr << "Failed to create dump file \"" << opts.audiofn << "\", not dumping audio." << std::endl;
    }
    else
    {
      std::cerr << "Successfully opened dump file \"" << opts.audiofn << "\"." << std::endl;
    }
  }

  /* Unless explicitly overridden, set the game type according to filename */
  if (gametype == Options::GAME_UNDEF)
  {
    std::string fn(filename);
    std::transform(fn.begin(), fn.end(), fn.begin(), ::tolower);
    std::cerr << "Selecting game type according to file suffix: ";
    if (fn.rfind(".cnc3replay") + 11 == fn.length())
    {
      std::cerr << "We pick Tiberium Wars.";
      gametype = Options::GAME_TW;
    }
    else if (fn.rfind(".kwreplay") + 9 == fn.length())
    {
      std::cerr << "We pick Kane's Wrath.";
      gametype = Options::GAME_KW;
    }
    else if (fn.rfind(".ra3replay") + 10 == fn.length())
    {
      std::cerr << "We pick Red Alert 3.";
      gametype = Options::GAME_RA3;
    }
    else
    {
      std::cerr << "unable to determine game type. Please specify manually ('-w', '-k', '-r').";
    }
    std::cerr << std::endl;
  }

  myfile.seekg(0, std::fstream::beg);

  if (gametype != Options::GAME_RA3)
  {
    myfile.read(reinterpret_cast<char*>(&header), sizeof(header_cnc3_t));

    if ( strncmp(header.str_magic, "C&C3 REPLAY HEADER", 18) ||
         ((header.six  != 6 ) && (header.six  != 0x1E )) ||
         (header.zero != 0 ) ||
         ((header.number1 != 5) && (header.number1 != 4)) ||
         ((READ_UINT32LE(header.vermajor) != 1) && (READ_UINT32LE(header.verminor) > 9))
       )
    {
      std::cerr << "File does not seem to be a replay file." << std::endl;
      return false;
    }
    hnumber1 = header.number1;
    hsix = header.six;
  }
  else
  {
    myfile.read(reinterpret_cast<char*>(&header_ra3), sizeof(header_ra3_t));

    if ( strncmp(header_ra3.str_magic, "RA3 REPLAY HEADER", 17) ||
         ((header_ra3.six  != 6 ) && (header_ra3.six  != 0x1E )) ||
         (header_ra3.zero != 0 ) ||
         ((header_ra3.number1 != 5) && (header_ra3.number1 != 4)) ||
         ((READ_UINT32LE(header_ra3.vermajor) != 1) && (READ_UINT32LE(header_ra3.verminor) > 12))
       )
    {
      std::cerr << "File does not seem to be a RA3 replay file." << std::endl;
      return false;
    }
    hnumber1 = header_ra3.number1;
    hsix = header_ra3.six;
  }

  str_title     = read2ByteString(myfile);
  str_matchdesc = read2ByteString(myfile);
  str_mapname   = read2ByteString(myfile);
  str_mapid     = read2ByteString(myfile);

  myfile.read(&nplayers, 1);

  for (int n = 0 ; n < int(nplayers); ++n)
  {
    myfile.read(reinterpret_cast<char*>(&player_id), 4);
    playerIDs.push_back(player_id);

    playerNames.push_back(read2ByteString(myfile));

    // Multiplayer replay
    if (hnumber1 == 5)
    {
      myfile.read(&onebyte, 1);
      playerNos.push_back(int(onebyte));
    }
    // Skirmish replay
    else if (hnumber1 == 4)
    {
      playerNos.push_back(0);
    }
  }

  if (gametype != Options::GAME_RA3)
  {
    std::cout << "Game version: " << std::dec << READ_UINT32LE(header.vermajor) << "." << READ_UINT32LE(header.verminor)
              << ", Build: " << READ_UINT32LE(header.buildmajor) << "." << READ_UINT32LE(header.buildminor) << std::endl;
  }
  else
  {
    std::cout << "Game version: " << std::dec << READ_UINT32LE(header_ra3.vermajor) << "." << READ_UINT32LE(header_ra3.verminor)
              << ", Build: " << READ_UINT32LE(header_ra3.buildmajor) << "." << READ_UINT32LE(header_ra3.buildminor) << std::endl;
  }
  std::cout << "Title:        " << str_title << std::endl
            << "Description:  " << str_matchdesc << std::endl
            << "Map name:     " << str_mapname << std::endl
            << "Map ID:       " << str_mapid << std::endl
            << std::endl << "Number of players: " << int(nplayers) << std::endl;

  if (hsix  == 0x1E) std::cout << "Commentary track available." << std::endl;

  for (size_t i = 0; i < playerNames.size(); ++i)
    fprintf(stdout, "Team %d (ID: %08X): %s\n", playerNos[i], playerIDs[i], playerNames[i].c_str());

  myfile.read(reinterpret_cast<char*>(&dummy), 4);
  str_anothername = read2ByteString(myfile);
  fprintf(stdout, "Read four bytes after last human player, should be all zero: 0x%08X.\nOne more name: \"%s\"", dummy, str_anothername.c_str());

  // Multiplayer replay
  if (hnumber1 == 5)
  {
    myfile.read(&onebyte, 1);
    fprintf(stdout, " (Number: %u)", onebyte);
  }
  fprintf(stdout, "\n");

  myfile.read(reinterpret_cast<char*>(&dummy), 4);
  std::cout << "Offset from CNC3RPL magic to first chunk: 0x" << std::hex << dummy;

  firstchunk = (unsigned int)myfile.tellg() + 4 + dummy;
  std::cout << ", first chunk at 0x" << std::hex << firstchunk << std::endl;

  myfile.read(reinterpret_cast<char*>(&dummy), 4);

  myfile.read(cncrpl_magic, 8);
  if (dummy != 8 || strncmp(cncrpl_magic, "CNC3RPL\0", 8))
  {
    std::cerr << "Error: Unexpected content! Aborting." << std::endl;
    exit(1);
  }

  /* For TW, version ??, there is this extra bit of info, char modinfo[22]. */
  char modinfo[22];
  if (gametype == Options::GAME_UNDEF ||
      (gametype == Options::GAME_TW && READ_UINT32LE(header.verminor) >= 7))
  {
    myfile.read(modinfo, 22);

    if (gametype == Options::GAME_UNDEF && !strncmp(modinfo, "CNC3", 4))
    {
      gametype = Options::GAME_TW;
    }
    else if (gametype == Options::GAME_UNDEF)
    {
      gametype = Options::GAME_KW;
      myfile.seekg(-22, std::fstream::cur);
    }
  }

  if (gametype == Options::GAME_TW && READ_UINT32LE(header.verminor) >= 7)
  {
    std::cout << "Interpreting file as Tiberium Wars replay. Mod info: ";
    char *p(modinfo), *q(NULL);
    while (p < modinfo + 22)
    {
      q = std::strchr(p, '\0');
      if (q == NULL) break;
      if (p[0] != '\0')
        std::cout << "\"" << p << "\" ";
      p = q+1;
    }
    std::cout << std::endl;
  }
  else if (gametype == Options::GAME_TW)
  {
    std::cout << "Interpreting file as pre-1.7 Tiberium Wars replay." << std::endl;
  }
  else if (gametype == Options::GAME_KW)
  {
    std::cout << "Interpreting file as Kane's Wrath replay." << std::endl;
  }
  else if (gametype == Options::GAME_RA3)
  {
    std::cout << "Interpreting file as Red Alert 3 replay. Mod info: ";
    myfile.read(modinfo, 22);
    char *p(modinfo), *q(NULL);
    while (p < modinfo + 22)
    {
      q = std::strchr(p, '\0');
      if (q == NULL) break;
      if (p[0] != '\0')
        std::cout << "\"" << p << "\" ";
      p = q+1;
    }
    std::cout << std::endl;
  }

  myfile.read(reinterpret_cast<char*>(&dummy), 4);

  strftime(timeout, 200, "%Y-%m-%d %H:%M:%S (%Z)", gmtime(reinterpret_cast<time_t*>(&dummy)));
  std::cout << "Timestamp: " << std::dec << dummy << ", that is " << timeout << "." << std::endl;


  // Skipping unknown data. We print all this later.
  if (gametype == Options::GAME_RA3)
    myfile.read(reinterpret_cast<char*>(&u31), 31);
  else
    myfile.read(reinterpret_cast<char*>(&u33), 33);

  myfile.read(reinterpret_cast<char*>(&hlen), 4);

  if (hlen > 10000) { throw std::length_error("Requested header length too big."); }

  std::vector<char> header2(hlen);
  myfile.read(header2.data(), hlen);

  if (opts.printraw)
    std::cout << "Header string length: " << std::dec << hlen << ". Raw header data:" << std::endl
              << std::string(header2.begin(), header2.end()) << std::endl << std::endl;

  std::cout << std::endl << "Header string length: " << std::dec << hlen << ". Header fields:" << std::endl;

  std::vector<std::string> tokens;
  Tokenize(std::string(header2.begin(), header2.end()), tokens, ";");
  
  for (size_t i = 0; i < tokens.size(); ++i)
    std::cout << tokens[i] << std::endl;

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
        playerNames2.push_back(subsubtokens);

        std::istringstream iss("0x" + std::string(subsubtokens[1]));
        uint32_t v;
        iss >> std::hex >> v;

        fprintf(stdout, "Ingame player name: %s (Faction: %s, IP addr.: 0x%08X, %d.%d.%d.%d) Other data: \"",
                subsubtokens[0].c_str(), faction(std::atoi(subsubtokens[5].c_str()), gametype).c_str(), v,
                v>>24, ((v<<8)>>24), ((v<<16)>>24), ((v<<24)>>24) );
        for (size_t j = 2; j < subsubtokens.size()-1; ++j)
          std::cout << subsubtokens[j] << ", ";
        std::cout << subsubtokens[subsubtokens.size()-1] << "\"." << std::endl;
      }
    }
  }

  // Skipping unknown data. We print all this later.
  myfile.read(dummy3, 9);

  myfile.read(reinterpret_cast<char*>(&dummy), 4);
  str_filename = read2ByteStringN(myfile, dummy);
  std::cout << "File name (?): " << str_filename << std::endl;

  myfile.read(reinterpret_cast<char*>(&datetime),  sizeof(datetime));

  myfile.read(reinterpret_cast<char*>(&dummy), 4);

  if (dummy > 10000) { throw std::length_error("Requested version magic length too big."); }

  std::vector<char> ch_vermagic(dummy);
  myfile.read(ch_vermagic.data(), dummy);
  str_vermagic = std::string(ch_vermagic.begin(), ch_vermagic.end());
  myfile.read(reinterpret_cast<char*>(&after_vermagic), 4);

  // Skipping unknown data. We print all this later.
  myfile.read(&onebyte, 1);
  if (gametype == Options::GAME_RA3) myfile.read(reinterpret_cast<char*>(&u20), 20*4);
  else                               myfile.read(reinterpret_cast<char*>(&u19), 19*4);

  std::cout << "Version/build magic string: \"" << str_vermagic << "\", followed by 0x"
            << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << after_vermagic << " and 0x"
            << std::setw(2) << (unsigned int)(onebyte) << std::endl;

  /* 10 uint16_t's before the version magic are another version of the time stamp:
   * Final two numbers ([8],[9]) always seem to be (14,0), (7,0) or (15,0).
   */
  fprintf(stdout, "The literal timestamp says: \"%s, %04hu-%02hu-%02hu %02hu:%02hu:%02hu\". It is followed by the number %hu.\n",
          weekday(datetime.data[2]), datetime.data[0], datetime.data[1], datetime.data[3],
          datetime.data[4], datetime.data[5], datetime.data[6], datetime.data[7]);

  std::cout << std::endl << "===== Report on unknown header data follows ====" << std::endl;

  // 33 bytes skipped after global header, 'CNC3RPL ' magic and timestamp, expected all zero.
  if (gametype == Options::GAME_RA3)
  {
    if (array_is_zero(u31, 31))
    {
      std::cout << "We skipped  31 expected mysterious bytes, which were all zero." << std::endl;
    }
    else
    {
      std::cout << std::endl << "We skipped 31 mysterious bytes which were unexpected! They were:" << std::endl;
      hexdump(stdout, u31, 31, "  ");
    }
  }
  else
  {
    if (array_is_zero(u33, 33))
    {
      std::cout << "We skipped  33 expected mysterious bytes, which were all zero." << std::endl;
    }
    else
    {
      std::cout << std::endl << "We skipped 33 mysterious bytes which were unexpected! They were:" << std::endl;
      hexdump(stdout, u33, 33, "  ");
    }
  }

  // 9 bytes after global header #2, expected {1-8} all zero.
  if (array_is_zero(reinterpret_cast<unsigned char*>(dummy3)+1, 8))
  {
    if ((unsigned int)(dummy3[0]) < playerNames2.size())
      fprintf(stdout, "The player who saved this replay was number %u (%s).\n", dummy3[0], playerNames2[dummy3[0]][0].c_str());
    else
      fprintf(stdout, "Warning: unexpected value for the index of the player who saved the replay (got: %u)!\n", dummy3[0]);

    fprintf(stdout, "We skipped   8 expected mysterious bytes which were all zero.\n");
  }
  else
    fprintf(stdout, "We skipped 9 mysterious bytes which were unexpected; values: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
            dummy3[0], dummy3[1], dummy3[2], dummy3[3], dummy3[4], dummy3[5], dummy3[6], dummy3[7], dummy3[8]);

  // 19/20 uint32_t's after the version magic
  if (gametype == Options::GAME_RA3)
  {
    fprintf(stdout, "\nThe 20 integers after the version magic are: ");
    for (size_t i = 0; i < 20; ++i) fprintf(stdout, "%u, ", u20.data[i]);
  }
  else
  {
    fprintf(stdout, "\nThe 19 integers after the version magic are: ");
    for (size_t i = 0; i < 19; ++i) fprintf(stdout, "%u, ", u19.data[i]);
  }
  fprintf(stdout, "\n");

  if (!opts.dumpchunks && !opts.apm && !opts.printraw)
  {
    uint32_t footer_offset;
    myfile.seekg(-4, std::fstream::end);
    myfile.read(reinterpret_cast<char*>(&footer_offset), 4);
    fprintf(stdout, "Footer length is %u. ", footer_offset);

    myfile.seekg((gametype == Options::GAME_RA3 ? 17 : 18) - int(footer_offset), std::fstream::end);
    myfile.read(reinterpret_cast<char*>(&dummy), 4);
    fprintf(stdout, "Footer chunk number: 0x%08X (timecode: %s); %u bytes / %u frames = %.2f Bpf = %.2f Bps.\n",
            dummy, timecode_to_string(dummy).c_str(), filesize, dummy, double(filesize) / dummy, double(filesize) * 15.0 / dummy);
    return true;
  }

  apm_1_map_t player_1_apm;
  apm_2_map_t player_2_apm;
  apm_histo_map_t player_indi_histo_apm;
  apm_histo_map_t player_coal_histo_apm;

  if (myfile.tellg() != firstchunk)
  {
    fprintf(stdout, "\nWarning: We're not at the beginning of the chunks yet, difference = %d. Advancing...\n",  (int)firstchunk - (int)myfile.tellg());
    myfile.seekg(firstchunk, std::fstream::beg);
  }

  if (opts.apm)
  {
    std::cout << std::endl << "==== gathering APM statistics ====" << std::endl << std::endl;
  }
  else
  {
    std::cout << std::endl << "=================================================" << std::endl
              << std::endl << "Now dumping individual data blocks." << std::endl << std::endl;
  }

  for (int block_count = 0; !myfile.eof(); block_count++)
  {
    uint32_t len;

    myfile.read(reinterpret_cast<char*>(&dummy), 4);

    if (dummy == 0x7FFFFFFF) break;

    myfile.read(&onebyte, 1);
    myfile.read(reinterpret_cast<char*>(&len), 4);

    if (len > 10000) { throw std::length_error("Requested chunk length too big."); }

    if (myfile.eof() || filesize - myfile.tellg() < len + 4)
    {
      if (opts.autofix)
      {
        opts.fixfn = (std::string(filename) + "-FIXED").c_str();
        opts.fixpos = lastgood;
        std::cerr << "Warning: Unexpected end of file! Auto fix is requested, attempting to fix this replay. (Params: " << opts.fixfn << ", " << opts.fixpos << ")" << std::endl;
        myfile.close();
        opts.gametype = gametype;
        fix_replay_file(filename, opts);
        return true;
      }
      else
      {
        std::cerr << "Error: Unexpected end of file! Aborting. Try 'cnc3reader -f " << lastgood
                  << (gametype == Options::GAME_RA3 ? " -r" : gametype == Options::GAME_KW ? " -k" : " -w")
                  << " " << filename << "' for fixing." << std::endl;
        return false;
      }
    }

    std::vector<unsigned char> vbuf(len + 4);
    lastgood = int(myfile.tellg()) - 9;
    myfile.read(reinterpret_cast<char*>(vbuf.data()), len + 4);
    const unsigned char * const buf = vbuf.data();

    if (opts.printraw)
    {
      if (opts.type != -1 && opts.type != onebyte) continue;
      fprintf(stdout, "\nBlock TC: 0x%08X, timecode: %s, length: %u bytes, count: %u, filepos: 0x%X, Chunk Type: %u.\n",
          dummy, timecode_to_string(dummy).c_str(), len, block_count, int(myfile.tellg()), onebyte);

      hexdump(stdout, buf, len+4, "  ");
    }
    else if (opts.dumpchunks)
    {
      const command_map_t & commands = gametype == Options::GAME_TW ? TW_commands
        : (gametype == Options::GAME_KW ? KW_commands : RA3_commands);

      // Chunk type 1
      if (onebyte == 1 && buf[0] == 1 && buf[len-1] == 0xFF && READ_UINT32LE(buf+len) == 0)
      {
        if (opts.type != -1 && opts.type != 1) continue;

        const size_t ncommands = READ_UINT32LE(buf+1);

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number of commands: %u."
                  //" Payload:"
                  "\n  Dissecting chunk commands.\n",
                  dummy, timecode_to_string(dummy).c_str(), block_count, len, onebyte, ncommands);
        }

        /* We've completed the dissector, no more need for the raw dump! */
        if (opts.dumpchunkswithraw)
          hexdump(stdout, buf+5, len-5, "  ");

        /* This next line was used during the learning phase to gather command statistics. */
        //if (ncommands == 1) fprintf(stdout, "MASTERPLAN 0x%02X %u\n", (int)buf[5], len-5);

        size_t pos = 5, opos = pos, counter;

        for (counter = 1 ; ; counter++)
        {
          const unsigned int cmd_id = buf[opos];
          const unsigned int player_id = buf[opos + 1];

          ++player_indi_histo_apm[player_id][cmd_id];
          ++player_coal_histo_apm[mangle_player(player_id, gametype)][cmd_id];
          ++player_1_apm[mangle_player(player_id, gametype)];

          command_map_t::const_iterator c = commands.find(cmd_id);

          if      (c != commands.end() && c->second > 0)  // Fixed-length commands
          {
            if (!parse_chunk1_fixlen(buf, pos, opos, cmd_id, counter, c->second, opts)) break;
          }
          else if (c != commands.end() && c->second < 0)  // variable-length commands
          {
            if (!parse_chunk1_varlen(buf, pos, cmd_id, counter, len, -c->second, opts)) break;
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
                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
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
                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x02)
              {
                if (buf[pos + 20] == 0x02)
                {
                  pos += 28;
                }
                else
                {
                  pos += 32;
                }
                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x10) /* 0x10 is special, it has two possible lengths, 12 or 13 */
              {
                const size_t l = buf[pos + 2] == 0x14 ? 12 : (buf[pos + 2] == 0x04 ? 13 : 99999);

                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, l);

                pos += l;
              }
              else if (cmd_id == 0x4B) /* 0x4B is special, it has two possible lengths, 8 or 16 */
              {
                const size_t l = buf[pos + 2] == 0x04 ? 8 : (buf[pos + 2] == 0x07 ? 16 : 99999);

                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, l);

                pos += l;
              }
              else if (cmd_id == 0x33)
              {
                size_t l = buf[pos + 3];
                std::string s1(buf + pos + 4, buf + pos + 4 + l);
                fprintf(stdout, " %2i: Command 0x33: First string length %u, \"%s\".", counter, l, s1.c_str());
                pos += l + 5;
                l = buf[pos];
                std::string s2 = read2ByteString((const char*)buf + pos + 1, 2 * l);
                fprintf(stdout, " Second string length %u, \"%s\".", l, s2.c_str());
                pos += 2 * l + 2;
                fprintf(stdout, " Number: 0x%08X.\n", READ_UINT32LE(buf + pos));
                pos += 5;
              }
              else
              {
                fprintf(stdout, "Warning: Unrecognized variable-length command.\n");
                while (buf[pos] != 0xFF && pos < len) pos++;
                if (buf[pos] != 0xFF) fprintf(stdout, "Panic: could not find terminator!\n");
                pos++;
                sprintf(s, " %2i: ", counter);
              }
            }
            else if (gametype == Options::GAME_KW)
            {
              if (cmd_id == 0xF5 || cmd_id == 0xF6)
              {
                parse_chunk1_varlen(buf, pos, cmd_id, counter, len, 4, opts);
              }
              else if (cmd_id == 0xF9 || cmd_id == 0xFB || cmd_id == 0xFC)
              {
                parse_chunk1_varlen(buf, pos, cmd_id, counter, len, 2, opts);
              }
              else if (cmd_id == 0x26)
              {
                parse_chunk1_varlen(buf, pos, cmd_id, counter, len, 15, opts);
              }
              else if (cmd_id == 0x31)
              {
                size_t l = buf[pos + 12];
                pos += l * 18 + 17;
                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x2D)
              {
                pos += buf[pos + 7] == 0xFF ? 8 : 26;

                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }

              }
              else
              {
                fprintf(stdout, "Warning: Unrecognized variable-length command.\n");
                while (buf[pos] != 0xFF && pos < len) pos++;
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
                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else if (cmd_id == 0x27)
              {
                size_t l = buf[pos + 12];
                pos += l * 18 + 17;
                if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
                {
                  fprintf(stdout, " %2i: Command 0x%02X, special length %u.\n", counter, cmd_id, pos - opos);
                }
              }
              else
              {
                fprintf(stdout, "Warning: Unrecognized variable-length command.\n");
                while (buf[pos] != 0xFF && pos < len) pos++;
                if (buf[pos] != 0xFF) fprintf(stdout, "Panic: could not find terminator!\n");
                pos++;
                sprintf(s, " %2i: ", counter);
              }
            }

            if (opts.cmd_filter == -1 || opts.cmd_filter == int(cmd_id))
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
            while (buf[pos] != 0xFF && pos < len) pos++;

            if (buf[pos] != 0xFF) fprintf(stdout, "Panic: could not find terminator!\n");

            char s[10] = { 0 };
            pos++;
            sprintf(s, " %2i: ", counter);

            hexdump(stdout, buf + opos, pos - opos, s);
          }

          opos = pos;
          if (pos == len) break;
        }

        if (!opts.apm) fprintf(stdout, "\n");

        if (counter > ncommands) { fprintf(stdout, "Panic: Too many commands dissected!\n\n"); }
      }

      // Chunk type 2
      else if ((onebyte == 2 && buf[0] == 1 && buf[1] == 0 && READ_UINT32LE(buf+7) == dummy && READ_UINT32LE(buf+len) == 0) &&
               ((buf[6] == 0x0F && gametype == Options::GAME_RA3) || buf[6] == 0x0E))
      {
        const unsigned int player_id = READ_UINT32LE(buf + 2);

        if (opts.apm)
        {
          player_2_apm[player_id].counter[0]++;
          if (len == 40)      player_2_apm[player_id].counter[1]++;
          else if (len == 24) player_2_apm[player_id].counter[2]++;
          else                player_2_apm[player_id].counter[3]++;
        }

        if (opts.type != -1 && opts.type != 2) continue;
        if (opts.type == 2 && opts.filter_heartbeat && dummy % 15 && dummy != 1) continue;

        if (!opts.apm)
        {
          fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number (Player ID?): %u. Payload:\n",
                  dummy, timecode_to_string(dummy).c_str(), block_count, len, onebyte, player_id);
          hexdump(stdout, buf+11, len-11, "  ");
          fprintf(stdout, "\n");
        }
      }

      // Chunk type 3 (audio?)
      else if (onebyte == 3 && buf[0] == 1 && buf[1] == 0 && buf[6] == 0x0D &&
               READ_UINT32LE(buf+7) == dummy && READ_UINT32LE(buf+len) == 0 && hsix  == 0x1E)
      {
        if (opts.dumpaudio && audioout)
        {
          fprintf(stderr, "  writing audio chunk 0x%02X%02X (length: %2u bytes vs. %2u)...\n", buf[11], buf[12], READ_UINT16LE(buf[13], buf[14]), len-15);
          if(READ_UINT16LE(buf[13], buf[14]) != len-15) abort();
          audioout.write(reinterpret_cast<const char*>(buf) + 15, len - 15);
        }

        if (opts.type != -1 && opts.type != 3) continue;

        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number (Player ID?): %u. Payload:\n",
                dummy, timecode_to_string(dummy).c_str(), block_count, len, onebyte, READ_UINT32LE(buf+2));
        hexdump(stdout, buf+11, len-11, "  ");
        fprintf(stdout, "\n");
      }

      // Chunk type 3 (empty)
      else if (onebyte == 3 && buf[0] == 0 && buf[1] == 0 && len == 2 && READ_UINT32LE(buf+len) == 0 && hsix  == 0x1E)
      {
        if (opts.type != -1 && opts.type != 3) continue;
        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Empty chunk.\n\n",
                dummy, timecode_to_string(dummy).c_str(), block_count, len, onebyte);
      }

      // Chunk type 4 (regular)
      else if (onebyte == 4 && buf[0] == 1 && buf[1] == 0 && buf[6] == 0x0F &&
               READ_UINT32LE(buf+7) == dummy && READ_UINT32LE(buf+len) == 0 && hsix  == 0x1E)
      {
        if (opts.type != -1 && opts.type != 4) continue;
        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Number (Player ID?): %u. Payload:\n",
                dummy, timecode_to_string(dummy).c_str(), block_count, len, onebyte, READ_UINT32LE(buf+2));
        hexdump(stdout, buf+11, len-11, "  ");
        fprintf(stdout, "\n");
      }

      // Chunk type 4 (empty)
      else if (onebyte == 4 && buf[0] == 0 && buf[1] == 0 && len == 2 && READ_UINT32LE(buf+len) == 0 && hsix  == 0x1E)
      {
        if (opts.type != -1 && opts.type != 4) continue;
        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Empty chunk.\n\n",
                dummy, timecode_to_string(dummy).c_str(), block_count, len, onebyte);
      }

      // Chunk type 1, skirmish, empty chunk
      else if (onebyte == 1 && len == 5 && hnumber1 == 4 && READ_UINT32LE(buf+len) == 0 && READ_UINT32LE(buf+1) == 0 && buf[0] == 1)
      {
        if (opts.type != -1 && opts.type != 1) continue;
        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %u. Empty chunk (skirmish only).\n\n",
                dummy, timecode_to_string(dummy).c_str(), block_count, len, onebyte);
      }

      // Chunk type 254 (RA3 only)
      else if (onebyte == -2 && READ_UINT32LE(buf+len) == 0)
      {
        if (opts.type != -1 && opts.type != -2) continue;
        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %d. Raw data:\n",
                dummy, timecode_to_string(dummy).c_str(), block_count, len, (int)(onebyte));
        hexdump(stdout, buf, len, "  ");
        fprintf(stdout, "\n");
      }

      // Otherwise: Panic!
      else
      {
        fprintf(stderr, "\n************** Warning: Unexpected chunk data!\n");
        fprintf(stdout, "Chunk number 0x%08X (timecode: %s, count %u, length: %u): Type: %d. Raw data:\n",
                dummy, timecode_to_string(dummy).c_str(), block_count, len, (int)(onebyte));
        hexdump(stdout, buf, len+4, "XYZZY   ");
        fprintf(stdout, "\n");

        return false;
      }
    }

  } // for(...)

  /* Process the footer */
  if (gametype == Options::GAME_RA3)
  {
    myfile.read(cncfooter_magic, 17);
  }
  else
  {
    myfile.read(cncfooter_magic, 18);
  }

  if ((gametype != Options::GAME_RA3 && strncmp(cncfooter_magic, "C&C3 REPLAY FOOTER", 18)) ||
      (gametype == Options::GAME_RA3 && strncmp(cncfooter_magic, "RA3 REPLAY FOOTER", 17))     )
  {
    std::cerr << "Error: Unexpected content! Aborting." << std::endl;
    exit(1);
  }

  uint32_t final_timecode;
  myfile.read(reinterpret_cast<char*>(&final_timecode), 4);
  fprintf(stdout, "Footer magic string as expected.\nFooter chunk number: 0x%08X (timecode: %s).\n",
          final_timecode, timecode_to_string(final_timecode).c_str());

  myfile.read(dummy3, 2);
  if (gametype == Options::GAME_RA3)
  {
    char bra[44];
    myfile.read(bra, 44);
    fprintf(stdout, "Numbers in the footer: %u %u", dummy3[0], dummy3[1]);
    for (size_t i = 0; i < 11; ++i) fprintf(stdout, " %u", READ_UINT32LE(bra+4*i));
    fprintf(stdout, ".\n");
  }
  else
  {
    myfile.read(reinterpret_cast<char*>(&dummy), 4);
    myfile.read(reinterpret_cast<char*>(&hlen), 4);
    fprintf(stdout, "Numbers in the footer: %u %u %u %u.\n", dummy3[0], dummy3[1], dummy, hlen);
  }

  /* Report APM stats */
  if (opts.apm)
  {
    const command_names_t & cmd_names = gametype == Options::GAME_TW ? TW_cmd_names
        : (gametype == Options::GAME_KW ? KW_cmd_names : RA3_cmd_names);

    fprintf(stdout, "\nAPM statistics: Type-2 Chunks\n");
    for (apm_2_map_t::const_iterator i = player_2_apm.begin(), end = player_2_apm.end(); i != end; ++i)
      fprintf(stdout,
              "Player %u: Total: %u actions (%.1f apm). Len40: %u (%.1f). Len24: %u (%.1f). Other: %u (%.1f).\n",
              i->first,
              i->second.counter[0], (double)(i->second.counter[0])*15.0*60.0/(double)(final_timecode),
              i->second.counter[1], (double)(i->second.counter[1])*15.0*60.0/(double)(final_timecode),
              i->second.counter[2], (double)(i->second.counter[2])*15.0*60.0/(double)(final_timecode),
              i->second.counter[3], (double)(i->second.counter[3])*15.0*60.0/(double)(final_timecode)
              );

    fprintf(stdout, "\nAPM statistics: Type-1 Chunks\n");
    for (apm_1_map_t::const_iterator i = player_1_apm.begin(), end = player_1_apm.end() ; i != end; ++i)
    {
      fprintf(stdout, "Player %u: %u\n", i->first, i->second);
    }

    fprintf(stdout, "\nAPM statistics: Type-1 command histogram\n");
    for (apm_histo_map_t::const_iterator i = player_indi_histo_apm.begin(), end = player_indi_histo_apm.end(); i != end; ++i)
    {
      for (auto j = i->second.begin(), end = i->second.end(); j != end; ++j)
      {
        const command_names_t::const_iterator nit = cmd_names.find(j->first);
        const std::string cn = nit == cmd_names.end() ? "" : nit->second;
        fprintf(stdout, "Raw player 0x%02X -->   command 0x%02X: %u (\"%s\")\n",
                i->first, j->first, j->second, cn.c_str());
      }
    }
    for (apm_histo_map_t::const_iterator i = player_coal_histo_apm.begin(), end = player_coal_histo_apm.end(); i != end; ++i)
    {
      for (auto j = i->second.begin(), end = i->second.end(); j != end; ++j)
      {
        const command_names_t::const_iterator nit = cmd_names.find(j->first);
        const std::string cn = nit == cmd_names.end() ? "" : nit->second;
        fprintf(stdout, "Cooked player %u -->   command 0x%02X: %u (\"%s\")\n",
                i->first, j->first, j->second, cn.c_str());
      }
    }
  }

  return true;
}

int main(int argc, char * argv[])
{
  Options opts;

  if (!parse_options(argc, argv, opts))
    return 1;

  if (opts.fixbroken)
  {
    if (optind + 1 != argc) { std::cerr << "Can only fix one replay file at a time." << std::endl; return 0; }
    if (opts.fixfn == NULL) opts.fixfn = (std::string(argv[optind]) + "-FIXED").c_str();
    fix_replay_file(argv[optind], opts);
  }
  else
  {
    populate_command_map_RA3(RA3_commands, RA3_cmd_names);
    populate_command_map_KW(KW_commands, KW_cmd_names);
    populate_command_map_TW(TW_commands, TW_cmd_names);

    for ( ; optind < argc; ++optind)
    {
      bool res;
      try
      {
        res = parse_replay_file(argv[optind], opts);
      }
      catch (const std::exception & e)
      {
        std::cout << "Exception: " << e.what() << std::endl;
        res = false;
      }
      catch (...)
      {
        std::cout << "Unknown Exception!" << std::endl;
        res = false;
      }

      if (!res && opts.breakonerror) return 1;

      std::cout << std::endl << std::endl;
    }
  }

  return 0;
}

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

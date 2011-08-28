/*****************************************
 * Schneider's Replay Reader for TW/KW/RA3
 * Handle with care.        
 *
 * Compile like this:
 *  g++ -O2 -s -o cnc3reader.exe cnc3reader.cpp cnc3readerimpl.cpp replayreader.cpp \
 *      -enable-auto-import -static-libgcc -static-libstdc++ -fwhole-program
 *
 ******************************************/

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

#include "replayreader.h"

extern command_map_t RA3_commands;
extern command_map_t KW_commands;
extern command_map_t TW_commands;
extern command_names_t RA3_cmd_names;
extern command_names_t KW_cmd_names;
extern command_names_t TW_cmd_names;

/** Faction names for all TW/KW/RA3 games.
 */
std::string faction(unsigned int f, Options::GameType g);

/** Parse command line options.
 */
bool parse_options(int argc, char * argv[], Options & opts);

/** Set up type-1 chunk command codes, lengths and descriptions.
 */
void populate_command_map_RA3(command_map_t & ra3_commands, command_names_t & ra3_cmd_names);
void populate_command_map_KW(command_map_t & kw_commands, command_names_t & kw_cmd_names);
void populate_command_map_TW(command_map_t & tw_commands, command_names_t & tw_cmd_names);

void fix_replay_file(const char * filename, Options & opts);

bool dumpchunks(const unsigned char * buf, char chunktype, unsigned int chunklen, unsigned int timecode,
                unsigned char hsix, unsigned char hnumber1, std::ostream & audioout,
                apm_1_map_t & player_1_apm, apm_2_map_t & player_2_apm,
                apm_histo_map_t & player_indi_histo_apm, apm_histo_map_t & player_coal_histo_apm,
                unsigned int block_count, Options::GameType gametype, const Options & opts);

/* The main worker function.
 */
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

  /* For TW, version 1.07+, there is this extra bit of info, char modinfo[22]. */
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
    std::cout << "Interpreting file as pre-1.07 Tiberium Wars replay." << std::endl;
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

  std::vector<std::string> tokens = tokenize(std::string(header2.begin(), header2.end()), ";");
  
  for (size_t i = 0; i < tokens.size(); ++i)
    std::cout << tokens[i] << std::endl;

  for (std::vector<std::string>::const_iterator it = tokens.begin(), end = tokens.end(); it != end; ++it)
  {
    const std::string & token = *it;

    if (token[0] == 'S' && token[1] == '=')
    {
      std::cout << std::endl << "Found player information, parsing..." << std::endl;
      std::vector<std::string> subtokens = tokenize(token.substr(2), ":");

      for (size_t i = 0; i < subtokens.size(); ++i)
      {
        if (subtokens[i][0] != 'H') continue;

        std::vector<std::string> subsubtokens = tokenize(subtokens[i].substr(1), ",");
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

  if ((unsigned int)(dummy3[0]) < playerNames2.size())
    fprintf(stdout, "The player who saved this replay was number %u (%s).\n", dummy3[0], playerNames2[dummy3[0]][0].c_str());
  else
    fprintf(stdout, "Warning: unexpected value for the index of the player who saved the replay (got: %u)!\n", dummy3[0]);

  // 8 bytes after global header #2 + 1, expected all zero.
  if (array_is_zero(reinterpret_cast<unsigned char*>(dummy3)+1, 8))
  {
    fprintf(stdout, "We skipped   8 expected mysterious bytes which were all zero.\n");
  }
  else
  {
    fprintf(stdout, "We skipped 8 mysterious bytes which were unexpected; values: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
            dummy3[1], dummy3[2], dummy3[3], dummy3[4], dummy3[5], dummy3[6], dummy3[7], dummy3[8]);
  }

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
      if (!dumpchunks(buf, onebyte, len, dummy, hsix, hnumber1, audioout,
                      player_1_apm, player_2_apm, player_indi_histo_apm, player_coal_histo_apm, block_count, gametype, opts)) return false;
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

    std::map<unsigned int, std::pair<unsigned int, unsigned int>> apm_total;

    fprintf(stdout, "\nAPM statistics: Type-2 Chunks\n");
    for (apm_2_map_t::const_iterator i = player_2_apm.begin(), end = player_2_apm.end(); i != end; ++i)
      fprintf(stdout,
              "Player %u: 1s-heartbeats: %u (%.1f). Len40: %u (%.1f). Len24: %u (%.1f). Other: %u (%.1f).\n",
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
        fprintf(stdout, "Player %u, command 0x%02X: %u (\"%s\")\n",
                i->first, j->first, j->second, cn.c_str());

        if (gametype == Options::GAME_RA3)
        {
          const unsigned int & c = j->first;
          if (c != 0x21 && c != 0x37)
          {
            apm_total[i->first].first += j->second;

            if (c != 0xF8 && c != 0xF5)
              apm_total[i->first].second += j->second;
          }
        }
      }
      fprintf(stdout, "\n");
    }

    if (gametype == Options::GAME_RA3)
    {
      fprintf(stdout, "Experimental APM count:\n");
      for (auto it = apm_total.cbegin(), end = apm_total.cend(); it != end; ++it)
      {
        fprintf(stdout, "  Player %u: %u actions including clicks (%.1f apm), %u actions excluding clicks (%.1f apm)\n",
                it->first, it->second.first, double(it->second.first * 60 * 15)/double(final_timecode),
                it->second.second, double(it->second.second * 60 * 15)/double(final_timecode));
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

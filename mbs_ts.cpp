extern "C"
{
  #include "f_evt.h"
}

#include <iostream>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <sstream>
#include <unistd.h>
#include <iomanip>
#include <cstring>
#include <unordered_map>
#include <vector>

std::string wr_to_string(long wr, bool ns = false)
{
  time_t time = wr / 1e9;
  char* str = asctime(localtime(&time));
  if (ns)
  {
      char buf[256];
      char tbuf[256];
      struct tm* const timeptr = localtime(&time);
      strftime(tbuf, 256, "%Y-%m-%d %Z %H:%M:%S", timeptr);
      sprintf(buf, "%s.%09ld", tbuf, (wr - time*1000000000L));
      std::string sstr(buf);
      return sstr;
  }
  else
  {
    std::string sstr(str);
    return sstr;
  }
}

struct ts_info
{
  uint64_t prev_ts;
  std::unordered_map<int, std::vector<int64_t>> deltas;
};

int main(int argc, char** argv)
{
  char* inF;
  bool trigger3 = false;

  int c;
  while ((c = getopt(argc, argv, "th")) != -1)
  {
    switch(c)
    {
    case 't':
      trigger3 = true;
      break;
    case 'h':
        argc = 0;
        break;
    case '?':
      if (isprint(optopt))
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
      else
        fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
      return 1;
    default:
        abort();
    }
  }

  if (argc - optind < 1)
  {
    std::cerr << "Usage: " << argv[0] << " [options] input_lmd_file [input_lmd_file_N ...]" << std::endl;
    std::cerr << "This program will print AIDA words from MBS data" << std::endl;
    std::cerr << std::endl;
    std::cerr << "-e <n>            : Print MBS event number <n> only" << std::endl;
    std::cerr << "-l <n>            : Limit to <n> AIDA words" << std::endl;
    std::cerr << "-n                : Don't print AIDA words" << std::endl;
    std::cerr << "-o                : Print MBS Subevents" << std::endl;
    std::cerr << "-t                : Print AIDA timestamp SYNC words" << std::endl;
    return 1;
  }

  int32_t* event;
  int32_t* buffer;
  int blocks = 0;
  int words = 0;

  std::unordered_map<int, ts_info> aligns;

  for (int i = optind; i < argc; i++)
  {
    s_evt_channel channel = {0};
    inF = argv[i];
    int in = f_evt_get_open(GETEVT__FILE, inF, &channel, NULL, 1, 0);
    if (in != GETEVT__SUCCESS)
    {
      std::cerr << "Input file: " << inF << " could not be opened" << std::endl;
      return 1;
    }

    std::cerr << "Opened input file: " << inF << std::endl;

    while (f_evt_get_event(&channel, &event, &buffer) == GETEVT__SUCCESS)
    {
       s_ve10_1* eventH = reinterpret_cast<s_ve10_1*>(event);
       if(trigger3 && eventH->i_trigger != 3) continue;
       //std::cout << "Read Event " << std::hex << eventH->i_trigger << " " << std::dec << eventH->l_count << std::endl;
       int se = f_evt_get_subevent(eventH, 0, NULL, NULL, NULL);


       //std::cout << "Sub Events == " << se << std::endl;
       for (int i = 1; i <= se; ++i)
       {
         s_ves10_1* header;
         int32_t* data;
         int32_t dataLen;
         f_evt_get_subevent(eventH, i, (int32_t**)&header, &data, &dataLen);
         int32_t wr_id = *data;
         blocks++;
         int32_t* data_cur = data + 1;
         //std::cout << "Sub Event #" << i << " Subsystem ID: " << *data << std::endl;
         uint64_t wr = 0;
         uint32_t wr1 = *data_cur++;
         uint32_t wr2 = *data_cur++;
         uint32_t wr3 = *data_cur++;
         uint32_t wr4 = *data_cur++;
         wr |= (uint64_t)((wr1) & 0xFFFF) << 0;
         wr |= (uint64_t)((wr2) & 0xFFFF) << 16;
         wr |= (uint64_t)((wr3) & 0xFFFF) << 32;
         wr |= (uint64_t)((wr4) & 0xFFFF) << 48;
         wr1 = ((wr1 >> 16) & 0xFFFF);
         wr2 = ((wr2 >> 16) & 0xFFFF);
         wr3 = ((wr3 >> 16) & 0xFFFF);
         wr4 = ((wr4 >> 16) & 0xFFFF);

         uint64_t prev_us = aligns[wr_id].prev_ts;
         if (prev_us == 0) { aligns[wr_id].prev_ts = wr; continue; }

         for (auto& i : aligns)
         {
           if (i.first == wr_id) continue;

           uint64_t prev_them = i.second.prev_ts;
           uint64_t time_since_other = wr - prev_them;
           if (wr > prev_them && prev_them != 0)
        	 {
              i.second.deltas[wr_id].push_back(time_since_other);
        	    if (prev_them > prev_us)
        	    {
                aligns[wr_id].deltas[i.first].push_back(-time_since_other);
        	    }
        	  }
          }

          aligns[wr_id].prev_ts = wr;
       } // for
    }
    f_evt_get_close(&channel);
  }

  //std::cout << "Scanned " << blocks << " blocks and " << words << " AIDA words" << std::endl;

  for (auto& i : aligns)
  {
    //std::cout << "Alignment to WR_ID 0x" << std::hex << i.first << std::dec << std::endl;
    for (auto& j : i.second.deltas)
    {
      //std::cout << std::hex << j.first << std::dec << " - " << std::endl;
      for (auto& k : j.second)
      {
        std::cout << i.first << " " << j.first << " " <<  k << std::endl;
      }
    }
  }

  return 0;
}

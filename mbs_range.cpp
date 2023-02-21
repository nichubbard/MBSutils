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
    sstr.pop_back();
    return sstr;
  }
}

int main(int argc, char** argv)
{
  char* inF;

  int c;
  bool script = false;
  while ((c = getopt(argc, argv, "b")) != -1)
  {
    switch(c)
    {
    case 'b':
      script = true;
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
    std::cerr << "This program will determine the WR range of a file" << std::endl;
    std::cerr << std::endl;
    return 1;
  }

  int32_t* event;
  int32_t* buffer;
  int64_t firstWR = 0, lastWR;

  for (int i = optind; i < argc; i++)
  {
    if (i != optind && i != argc - 1) continue;
    s_evt_channel channel = {0};
    inF = argv[i];
    int in = f_evt_get_open(GETEVT__FILE, inF, &channel, NULL, 1, 0);
    if (in != GETEVT__SUCCESS)
    {
      std::cerr << "Input file: " << inF << " could not be opened" << std::endl;
      return 1;
    }

    if(!script)
      std::cout << "Opened input file: " << inF << std::endl;

    while (f_evt_get_event(&channel, &event, &buffer) == GETEVT__SUCCESS)
    {
       s_ve10_1* eventH = reinterpret_cast<s_ve10_1*>(event);
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
         if (wr1 == 0x03e1 && wr2 == 0x04e1 && wr3 == 0x05e1 && wr4 == 0x06e1)
         {
           if (firstWR == 0) firstWR = wr;
           lastWR = wr;
         }
       }
    }
    f_evt_get_close(&channel);
  }

  if (script)
  {
    std::cout << std::hex << firstWR << " " << lastWR << std::endl;
  }
  else
  {
    std::cout << "First White Rabbit is: " << std::hex << firstWR << std::dec << " " << wr_to_string(firstWR) << std::endl;
    std::cout << " Last White Rabbit is: " << std::hex <<  lastWR << std::dec << " " << wr_to_string( lastWR) << std::endl;
  }

  return 0;
}

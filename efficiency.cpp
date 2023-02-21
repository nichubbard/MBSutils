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
#include <map>
#include <memory>

std::map<int, std::string> wr_to_system =
{
  { 0x100, "FRS" },
  { 0x200, "FRS-S2" },
  { 0x400, "GALILEO" },
  { 0x500, "bPlastic" },
  { 0x700, "AIDA" },
  { 0x1500, "FATIMA VME" },
  { 0x1600, "FATIMA TAMEX" },
};

int mbs_open(const char* path, s_evt_channel* channel)
{
  std::string sp(path);
  int type = GETEVT__FILE;
  if (sp.find("stream://") == 0)
  {
    sp = sp.substr(9);
    std::cout << "Opening stream: " << sp << std::endl;
    type = GETEVT__STREAM;
  }
  std::unique_ptr<char> mbs_buffer(new char[sp.length()]);
  sp.copy(mbs_buffer.get(), std::string::npos);
  return f_evt_get_open(type, mbs_buffer.get(), channel, NULL, 1, 0);
}

int main(int argc, char** argv)
{
  char* inF;
  bool dump_sync = false;
  bool dump_subevents = false;
  bool no_aida = false;

  int c;
  int l = std::numeric_limits<int>::max();
  int e1 = 0;
  int e2 = 0;
  while ((c = getopt(argc, argv, "tol:e:n")) != -1)
  {
    switch(c)
    {
    case 'l':
      l = atoi(optarg);
      if (l == 0)
      {
        fprintf(stderr, "Invalid value for -l `%s`\n", optarg);
        return 1;
      }
      break;
    case 'e':
      {
        char* e_1 = strtok(optarg, "-");
        char* e_2 = strtok(NULL, "-");
        if (e_1 && e_2)
        {
          e1 = atoi(e_1);
          e2 = atoi(e_2);
        }
        else if (e_1)
        {
          e1 = e2 = atoi(e_1);
        }
        else
        {
          fprintf(stderr, "Invalid value for -e `%s`\n", optarg);
          return 1;
        }
      }
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
    std::cerr << "This program will calculate time-stitch efficiencies" << std::endl;
    std::cerr << std::endl;
    return 1;
  }

  int32_t* event;
  int32_t* buffer;
  int blocks = 0;
  int words = 0;

  std::map<int, int> mults_h;

  for (int i = optind; i < argc && blocks < l; i++)
  {
    s_evt_channel channel = {0};
    inF = argv[i];
    int in = mbs_open(inF, &channel);
    if (in != GETEVT__SUCCESS)
    {
      std::cerr << "Input file: " << inF << " could not be opened" << std::endl;
      return 1;
    }

    std::cout << "Opened input file: " << inF << std::endl;

    while (blocks < l && f_evt_get_event(&channel, &event, &buffer) == GETEVT__SUCCESS)
    {
       s_ve10_1* eventH = reinterpret_cast<s_ve10_1*>(event);
       if(e1 != 0 && e1 > eventH->l_count) continue;
       if(e2 != 0 && e2 < eventH->l_count) break;
       //std::cout << "Read Event " << std::hex << eventH->i_trigger << " " << std::dec << eventH->l_count << std::endl;
       int se = f_evt_get_subevent(eventH, 0, NULL, NULL, NULL);
       std::map<int, int> local_mults;
       bool frs = false;
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
         //std::cout << "Sub Event #" << i << " Subsystem ID: " << std::hex << wr_id << std::dec << std::endl;
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
           local_mults[wr_id]++;
         //if (wr_id == 0x100) frs = true;
         if (wr_id == 0x700)
         {
           int aida_imp_words = 0;
           while (data_cur < data + dataLen)
           {
              int32_t word = *data_cur++;
              int32_t lowTS = (*data_cur++) & 0x0FFFFFFF;
              words++;
              // WR timestamp marker
              if ((word & 0xF0000000) == 0xD0000000) {
                aida_imp_words++;
              }
              if (aida_imp_words >= 2)
              {
                //if (!frs) std::cout << "--AIDA IMPLANT--" <<std::endl;
                frs=true;
              }
            }
         }
       }
       if (frs)
       {
         for (auto& i : local_mults)
         {
            mults_h[i.first]++;
         }
       }
    }
    f_evt_get_close(&channel);
  }

  std::cout << "Scanned " << blocks << " blocks and " << words << " AIDA words" << std::endl;

  std::cout << "Multiplicity Data" << std::endl;
  for (auto& i : mults_h)
  {
    std::cout << "For " << (wr_to_system[i.first]) << " WRID = 0x" << std::hex << i.first << std::dec << std::endl;
    double total = mults_h[0x700];
    std::cout << " " << i.second << " subevents in coincidence (" << std::setprecision(2) << (100 * i.second / total) <<  "%)" << std::endl;
  }
  return 0;
}

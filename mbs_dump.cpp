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
#include <vector>

static bool dump = true;
#define DUMP_TRIGGER 0x15fb85ba00000000
bool wr_hex = true;

std::ios_base& wrfmt(std::ios_base& stream) {
  if (wr_hex) {
    stream.setf(std::ios_base::hex, std::ios_base::basefield);
  }
  else {
    stream.setf(std::ios_base::dec, std::ios_base::basefield);
  }
  return stream;
}

struct aida_event
{
  uint64_t timestamp;
  uint32_t data_word;
  uint32_t low_time;
};

int module_from_word(uint32_t word)
{
  if((word & 0xC0000000) == 0xC0000000)
  {
    int channelID = (word >> 16) & 0xFFF;
    return (channelID >> 6) & 0x3F;
  }
  else if ((word & 0xC0000000) == 0x80000000)
  {
    return (word >> 24) & 0x3F;
  }
  else
  {
    return -1;
  }
}

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

void dump_event(aida_event& event)
{
  std::cout << "        ";
  std::cout << std::setfill('0');
  std::cout << wrfmt << std::setw(16) << event.timestamp << std::dec << " | ";
  std::cout << std::hex << event.data_word << " " << std::setw(8) << event.low_time << std::dec << " | ";

  static uint64_t old_wr = 0;
  static uint64_t old_wr_asic[24][4] = {0};
  static int asic_multiplex[24][4] = {0};
  uint64_t delta = event.timestamp - old_wr;
  if (delta > 999999) delta = 999999;
  std::cout << std::setw(6) << delta << " | ";
  old_wr = event.timestamp;

  if((event.data_word & 0xC0000000) == 0xC0000000)
  {
    int channelID = (event.data_word >> 16) & 0xFFF;
    int feeID = 1 + ((channelID >> 6) & 0x3F);
    channelID &= 0x3F;
    int data = (event.data_word & 0xFFFF);
    int veto = (event.data_word >> 28) & 0x1;

    std::cout << "ADC " << std::setw(2) << feeID << ":"
      << std::setw(2) << channelID << " " << (veto ? 'H' : 'L')
      << " " << std::setw(4) << std::hex << data << std::dec;

    std::cout << std::setfill(' ');
    std::cout.precision(6);
    std::cout  << " | " << std::setw(8) << ((data - 0x8000) * 0.7) << (veto ? "MeV" : "keV");
    std::cout << std::setfill('0');

    int asic = channelID / 16;
    uint64_t delta = event.timestamp - old_wr_asic[feeID - 1][asic];
    old_wr_asic[feeID - 1][asic] = event.timestamp;
    if (delta > 999999) delta = 999999;
    std::cout <<  " | " << "[" << std::setw(2) << feeID << "," << asic << "] " << std::setw(6) << delta;
    if (delta >= 1990 && delta <= 2010) {
      asic_multiplex[feeID - 1][asic]++;
    }
    else {
      asic_multiplex[feeID -1][asic] = 0;
    }
    std::cout << " | -" << (asic_multiplex[feeID - 1][asic] * 2000);
  }
  else if ((event.data_word & 0xC0000000) == 0x80000000)
  {
    int feeID = 1 + ((event.data_word >> 24) & 0x3F);
    int infoCode = (event.data_word >> 20) & 0x000F;
    int infoField = event.data_word & 0x000FFFFF;

    std::cout << "INFO " << infoCode;

    if (infoCode == 2)
    {
      std::cout << " PAUSE " << std::setw(2) << feeID << " "
        << std::hex << std::setw(5) << infoField << std::dec;
    }
    else if (infoCode == 3)
    {
      std::cout << " RESUME " << std::setw(2) << feeID << " "
        << std::hex << std::setw(5) << infoField << std::dec;
    }
    else if (infoCode == 4)
    {
      std::cout << " SYNC4828 " << std::setw(2) << feeID << " "
        << std::hex << std::setw(5) << infoField << std::dec;
    }
    else if (infoCode == 5)
    {
      std::cout << " SYNC6348 " << std::setw(2) << feeID << " "
        << std::hex << std::setw(5) << infoField << std::dec;
    }
    else if (infoCode == 6)
    {
      int adc = ((infoField >> 16) & 0xF);
      int hits = infoField & 0xFFFF;
      std::cout << " DISCRIM " << std::setw(2) << feeID << ":" << adc << " "
        << std::hex << std::setw(4) << hits << std::dec;
    }
    else if (infoCode == 8)
    {
      int idx = ((infoField>> 16) & 0xF);
      int val = infoField & 0xFFFF;
      std::cout << " SCALER " << std::setw(2) << feeID << " " << idx << ":" << std::hex << std::setw(4) << val << std::dec;
    }
  }
  else
  {
    std::cout << "Unknown";
  }

  std::cout << std::endl;
}

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
  while ((c = getopt(argc, argv, "dtol:e:n")) != -1)
  {
    switch(c)
    {
    case 'd':
      wr_hex = false;
      break;
    case 't':
      dump_sync = true;
      break;
    case 'o':
      dump_subevents = true;
      break;
    case 'n':
      no_aida = true;
      break;
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
    std::cerr << "This program will print AIDA words from MBS data" << std::endl;
    std::cerr << std::endl;
    std::cerr << "-d                : Print White Rabbit timestamps in decimal" << std::endl;
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

  std::map<int, std::map<int, int>> mults_h;
  int minLen = 0;
  int maxLen = 0;
  double avgLen = 0;
  int avgLenN = 0;
  std::ofstream lengths("lengths.dat");
  std::map<std::string, int> total_mults;

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

    int eventIt = 0;
    while (blocks < l && f_evt_get_event(&channel, &event, &buffer) == GETEVT__SUCCESS)
    {
       s_ve10_1* eventH = reinterpret_cast<s_ve10_1*>(event);
       if(e1 != 0 && e1 > eventH->l_count) continue;
       if(e2 != 0 && e2 < eventH->l_count) break;
       //std::cout << "Read Event " << std::hex << eventH->i_trigger << " " << std::dec << eventH->l_count << std::endl;
       int se = f_evt_get_subevent(eventH, 0, NULL, NULL, NULL);
       if (dump && dump_subevents)
       {
          std::cout << "Event [" << std::dec << eventIt++ << "] #" << std::dec << eventH->l_count << " (Trigger " << eventH->i_trigger << ") contains " << se << " subevents" << std::endl;
       }
       std::map<int, int> local_mults;
       uint64_t wrs = 0;
       uint64_t wre = 0;
       //std::cout << "Sub Events == " << se << std::endl;
       std::vector<int> sub_ids;
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
         if (wr1 == 0x03e1 && wr2 == 0x04e1 && wr3 == 0x05e1 && wr4 == 0x06e1)
         {
           local_mults[wr_id]++;
           sub_ids.push_back(wr_id);
           if (wrs == 0) wrs = wr;
           wre = wr;
         }
         if (wr > DUMP_TRIGGER) {
           //dump = true;
         }
         if (dump && dump_subevents)
         {
           std::cout << "    Sub Event ProcID: " << std::dec << header->i_procid;
           if (header->i_procid == 90)
           {
             std::cout << " (AIDA)   ";
           }
           else if (header->i_procid == 80)
           {
             std::cout << " (Plastic)";
           }
           else if (header->i_procid == 70)
           {
             std::cout << " (FATIMA) ";
           }
           else if (header->i_procid == 75)
           {
             std::cout << " (FAT.TMX)";
           }
           else if (header->i_procid == 60)
           {
             std::cout << " (GALILEO)";
           }
           else if (header->i_procid == 50)
           {
             std::cout << " (FINGER)";
           }
           else if (header->i_procid == 10 || header->i_procid == 20 || header->i_procid == 25 || header->i_procid == 30)
           {
              std::cout << " (FRS)  ";
           }
           else
           {
              std::cout << "        ";
           }
           if (wr1 == 0x03e1 && wr2 == 0x04e1 && wr3 == 0x05e1 && wr4 == 0x06e1)
           {
             std::cout << "\tWR ID: " << std::hex << std::setw(4) << wr_id << " " << ", WR Time: " << wrfmt << wr << " " << wr_to_string(wr, true);
           }
           std::cout << std::endl;
           //continue;
         }
         if(wr_id != 0x700) continue;
         if(no_aida) continue;
         while (data_cur < data + dataLen)
         {
            int32_t word = *data_cur++;
            int32_t lowTS = (*data_cur++) & 0x0FFFFFFF;
            words++;
            // WR timestamp marker
            if ((word & 0xC0F00000) == 0x80500000)
            {
              aida_event evt = {0};
              evt.data_word = word;
              evt.timestamp = (wr & ~0x0FFFFFFFULL) | lowTS;
              evt.low_time = lowTS;

              if (dump_sync && dump)
              {
                dump_event(evt);
              }

              uint64_t middleTS = *data_cur++;
              lowTS = (*data_cur++) & 0x0FFFFFFF;


              evt.data_word = middleTS;
              evt.timestamp = (wr & ~0x0FFFFFFFULL) | lowTS;
              evt.low_time = lowTS;

              if (dump_sync && dump)
              {
                dump_event(evt);
              }

              uint64_t highTS = word & 0x000FFFFF;
              middleTS &= 0x000FFFFF;
              wr = (highTS << 48) | (middleTS << 28) | lowTS;
              words++;

              continue;
            }
            // Other data
            aida_event evt = {0};
            evt.data_word = word;
            evt.timestamp = (wr & ~0x0FFFFFFFULL) | lowTS;
            evt.low_time = lowTS;

            if (evt.timestamp > DUMP_TRIGGER) {
              dump = true;
            }

            if (dump)
              dump_event(evt);
         }
       }
       for (auto& i : local_mults)
       {
          mults_h[i.first][i.second]++;
       }
       int len = (int)(wre - wrs);
       if (len != 0)
       {
         lengths << eventIt << " " << len << std::endl;
         if (minLen == 0 || len < minLen) minLen = len;
         if (maxLen == 0 || len > maxLen) maxLen = len;
         avgLen = ((avgLen * avgLenN) + len) / (avgLenN + 1);
         avgLenN++;
       }
       std::sort(sub_ids.begin(), sub_ids.end());
       std::stringstream id_string;
       for (auto id : sub_ids) {
         id_string << "0x" << std::hex << std::setw(4) << id << std::dec << " ";
       }
       total_mults[id_string.str()]++;
    }
    f_evt_get_close(&channel);
  }

  std::cout << "Scanned " << blocks << " blocks and " << words << " AIDA words" << std::endl;

  std::cout << "Multiplicity Data" << std::endl;
  for (auto& i : mults_h)
  {
    std::cout << "For WRID = 0x" << std::hex << i.first << std::dec << std::endl;
    double total = 0;
    for (auto& j : i.second) total += j.second;
    for (auto& j : i.second)
    {
      std::cout << " " << j.first << " subevents in " << j.second << " of events (" << std::setprecision(2) << (100 * j.second / total) <<  "%)" << std::endl;
    }
  }

  for (auto& i : total_mults)
  {
    std::cout << i.second << " events with subevents " << i.first << std::endl;
  }

  std::cout << "Event Length Min: " << minLen << ", Avg: " << avgLen << ", Max: " << maxLen << std::endl;
  return 0;
}

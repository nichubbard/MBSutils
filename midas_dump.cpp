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
#include <byteswap.h>

static bool dump = true;
#define DUMP_TRIGGER 0x15fb85ba00000000

static bool wave_dump = false;

struct aida_event
{
  uint64_t timestamp;
  uint32_t data_word;
  uint32_t low_time;
  uint32_t* samples;
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
  std::cout << std::hex << std::setw(16) << event.timestamp << std::dec << " | ";
  std::cout << std::hex << event.data_word << " " << std::setw(8) << event.low_time << std::dec << " | ";

  if((event.data_word & 0xC0000000) == 0xC0000000)
  {
    int channelID = (event.data_word >> 16) & 0xFFF;
    int feeID = 1 + ((channelID >> 6) & 0x3F);
    channelID &= 0x3F;
    int data = (event.data_word & 0xFFFF);
    int veto = (event.data_word >> 28) & 0x1;
    int fail = (event.data_word >> 29) & 0x1;
    
    if (fail)
    {
      std::cout << "VERNIER " << std::setw(2) << feeID << ":"
      << std::setw(2) << channelID << " " << (veto ? 'H' : 'L')
      << " " << std::setw(4) << std::hex << data << std::dec;
    }
    else
    {
      std::cout << "ADC " << std::setw(2) << feeID << ":"
      << std::setw(2) << channelID << " " << (veto ? 'H' : 'L')
      << " " << std::setw(4) << std::hex << data << std::dec;
    }
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
  }
  else if ((event.data_word & 0xC0000000) == 0x40000000)
  {

    int channelID = (event.data_word >> 16) & 0xFFF;
    int feeID = 1 + ((channelID >> 6) & 0x3F);
    channelID &= 0x3F;
    int length = (event.data_word & 0xFFFF);
    std::cout << "WAVE " << std::setw(2) << feeID << ":"
    << std::setw(2) << channelID << " "
    << std::setw(4) << std::dec << length <<  " samples";
    if (wave_dump)
    {
      for(int i = 0; i < length; i += 8)
      {
        std::cout << "\n";
        std::cout << "        ";
        std::cout << std::setfill(' ');
        std::cout << std::hex << std::setw(16) << "" << std::dec << "   ";
        std::cout << std::hex << std::setw(8) << "" << " " << std::setw(8) << "" << std::dec << "   ";
        std::cout << std::hex;
        std::cout << std::setfill('0');
        for (int j = 0; j < 8 && i + j < length; j += 2)
        {
          int be1 = (event.samples[i/2 + j/2] & 0xFFFF);
          int be2 = (event.samples[i/2 + j/2] >> 16);
          std::cout << std::setw(4) << be1 << " " << std::setw(4) << be2 << " ";
        }
        std::cout << std::dec;
      }
    }
  }
  else
  {
    std::cout << "Unknown";
  }

  std::cout << std::endl;
}

int main(int argc, char** argv)
{
  char* inF;
  bool dump_sync = false;
  bool dump_subevents = false;
  bool no_aida = false;
  bool print_header = false;

  int c;
  int l = std::numeric_limits<int>::max();
  int e1 = 0;
  int e2 = 0;
  while ((c = getopt(argc, argv, "tol:e:nwH")) != -1)
  {
    switch(c)
    {
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
      case 'w':
        wave_dump = true;
        break;
      case 'H':
        print_header = true;
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
    std::cerr << "-w                : Print waveform data" << std::endl;
    std::cerr << "-H                : Print MIDAS header" << std::endl;
    return 1;
  }

  int blocks = 0;
  int words = 0;
  bool resyncing = false;

  for (int i = optind; i < argc && blocks < l; i++)
  {
    inF = argv[i];
    std::ifstream fs(inF, std::ios_base::in | std::ios_base::binary);

    std::cout << "Opened input file: " << inF << std::endl;

    char* blockData = 0;
    size_t blockDataLen = 0;
    uint64_t wr = 0;

    while (fs)
    {
      uint32_t b1, b2;
      fs.read(reinterpret_cast<char*>(&b1), 4);
      fs.read(reinterpret_cast<char*>(&b2), 4);
      int blockLen = 0;

      if (b1 == 0x45594245 && b2 == 0x41544144)
      {
        if (print_header) {
          std::cout << "MIDAS HEADER (EBYEDATA)" << std::endl;
        }
        resyncing = false;
        // Block Header
        //std::cout << "BLOCK" << std::endl;
        fs.read(reinterpret_cast<char*>(&b1), 4);
        fs.read(reinterpret_cast<char*>(&b2), 4);
        //std::cout << "Sequence = " << b1 << std::endl;
        //std::cout << "Unknown = " << b2 << std::endl;
        if (print_header) {
          std::cout << "- Sequence " << b1 << std::endl;
          uint16_t stream = b2 & 0xffff;
          uint16_t tape = b2 >> 16;
          std::cout << "- Stream " << stream << std::endl;
          std::cout << "- Tape " << tape << std::endl;
        }

        fs.read(reinterpret_cast<char*>(&b1), 4);
        //std::cout << "Endians = " << b1 << std::endl;
        fs.read(reinterpret_cast<char*>(&b2), 4);
        //std::cout << "Length = " << b2 << std::endl;
        if (print_header) {
          uint16_t stream = b1 & 0xffff;
          uint16_t tape = b1 >> 16;
          std::cout << "- Tape Endian " << stream << std::endl;
          std::cout << "- Data Endian " << tape << std::endl;
          std::cout << "- Data Length " << b2 << std::endl;
        }
        //
        bool endian = false;
        if (b1 != 0x00010001)
        {
          if (b1 == 0x01000001)
          {
            endian = true;
          }
          else
          {
            std::cout << "ENDIAN ERROR" << std::endl;
            std::cout << std::hex << b1 << std::dec << std::endl;
            continue;
          }
        }

        blockLen = b2;
        if (blockLen > blockDataLen)
        {
          blockData = new char[blockLen];
          blockDataLen = blockLen;
        }
        fs.read(blockData, blockLen);
        if (endian) {
          uint32_t* block = (uint32_t*)blockData;
          uint32_t* blockEnd = (uint32_t*)(blockData + blockLen);
          while(block < blockEnd) {
            uint32_t word = *block;
            *block = bswap_32(word);
            block++;
          }
        }

        /* Scan block to get a timestamp */
        uint32_t* block = (uint32_t*)blockData;
        uint32_t* blockEnd = (uint32_t*)(blockData + blockLen);
        blocks++;
        while(block < blockEnd)
        {
          int32_t word = *block++;
          int32_t lowTS = (*block++) & 0x0FFFFFFF;
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

            uint64_t middleTS = *block++;
            lowTS = (*block++) & 0x0FFFFFFF;


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
          // Old non-WR timestamp mode
          if ((word & 0xC0F00000) == 0x80400000)
          {
            aida_event evt = {0};
            evt.data_word = word;
            evt.timestamp = (wr & ~0x0FFFFFFFULL) | lowTS;
            evt.low_time = lowTS;

            if (dump_sync && dump)
            {
              dump_event(evt);
            }

            uint64_t highTS = word & 0x000FFFFF;
            wr = (wr >> 48) & 0x000FFFFF;
            wr <<= 48;
            wr |= (highTS << 28) | lowTS;
            words++;
            continue;
          }
          // Other data
          aida_event evt = {0};
          evt.data_word = word;
          evt.timestamp = (wr & ~0x0FFFFFFFULL) | lowTS;
          evt.low_time = lowTS;

          // Wave data eats a buffer
          if ((word & 0xC0000000) == 0x40000000)
          {
            int length = (word & 0xFFFF);
            evt.samples = block;
            block += (length / 2);
          }

          dump_event(evt);

        }
        fs.ignore(64*1024 - blockLen - 24);
      }
      else
      {
        if (!resyncing)
          std::cout << "LOST BLOCK SYNC" << std::endl;
        resyncing = true;
        //exit(1);
      }
    }
  }

    std::cout << "Scanned " << blocks << " blocks and " << words << " AIDA words" << std::endl;

    return 0;
  }

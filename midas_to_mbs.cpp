extern "C"
{
  #include "f_evt.h"
  #include "s_filhe.h"
}

#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include "zfstream.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string>
#include <map>
#include <regex>

typedef std::map<uint64_t, std::string> midas_map;

midas_map midas_idx;

/* Save WR at the opening file for the backwards searching thing
 * Scan WR until first INFO word, then subtract 1 INFO4 for the start */

//#define DEBUG 1

bool tryopen(gzifstream& fs, std::string f)
{
  fs.close();
  fs.open(f.c_str(), std::ios_base::in | std::ios_base::binary);
  if (!fs)
  {
    fs.close();
    fs.open((f + ".gz").c_str(), std::ios_base::in | std::ios_base::binary);
  }
  return (bool)fs;
}


int main(int argc, char** argv)
{
  s_evt_channel channel_out = {0};

  int c;
  int max_size = -1;
  uint64_t first_wr = 0;
  uint64_t last_wr = -1;
  bool prefix = false;
  bool volume = false;
  bool help = false;
  bool index = false;

  while ((c = getopt(argc, argv, "ihpvs:f:l:")) != -1)
  {
    switch(c)
    {
    case 'h':
      help = true;
      break;
    case 'i':
      index = true;
      break;
    case 'p':
      prefix = true;
      break;
    case 'v':
      volume = true;
      break;
    case 's':
      max_size = atoi(optarg);
      if (!max_size)
      {
        fprintf(stderr, "Invalid value for -s `%s`\n", optarg);
        return 1;
      }
      break;
    case 'f':
      first_wr = strtoll(optarg, NULL, 0);
      if (!first_wr)
      {
        fprintf(stderr, "Invalid value for -f `%s`\n", optarg);
        return 1;
      }
      break;
    case 'l':
      last_wr = strtoll(optarg, NULL, 0);
      if (!last_wr)
      {
        fprintf(stderr, "Invalid value for -l `%s`\n", optarg);
        return 1;
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

  if (help || argc - optind < 2)
  {
    std::cerr << "Usage: " << argv[0] << " input_midas_file output_lmd_file_prefix" << std::endl;
    std::cerr << "This program will convert an AIDA MIDAS file (input) to an MBS LMD file as if from the FDR" << std::endl;
    std::cerr << "The MBS WR timestamp is set to the timestamp of the first event in the block" << std::endl;
    std::cerr << "Aside from finding this WR timestamp no analysis of AIDA data is done!" << std::endl;
    std::cerr << std::endl;
    std::cerr << "-h      : Show tihs help" << std::endl;
    std::cerr << "-i      : Don't convert, just update midas.idx" << std::endl;
    std::cerr << "-p      : MIDAS file specifies a run (file prefix)" << std::endl;
    std::cerr << "-v      : MIDAS file specifies a volume (folder)" << std::endl;
    std::cerr << "-s S    : Split MBS files every S Megabytes" << std::endl;
    std::cerr << "-f WR   : Write data from timestamp WR" << std::endl;
    std::cerr << "-l WR   : Write data until timestamp WR" << std::endl;
    return 1;
  }

  std::cerr << "WR Limited from: " << std::hex << first_wr << " to " << last_wr << std::dec << std::endl;

  char* inFprefix = argv[optind];
  char* outFprefix = argv[argc - 1];

  char inF[256];
  char outF[256];

  int in_run = 1;
  int max_run = 1;
  int in_index = 0;
  int out_index = 1;

  {
    std::ifstream idxf("midas.idx");
    std::cerr << "Loading midas.idx to cache WR lookups" << std::endl;
    while (idxf) {
      std::string path;
      uint64_t wr;
      idxf >> path >> wr;
      if (wr > 0 && !path.empty()) {
        midas_idx[wr] = path;
      }
    }
    idxf.close();
  }

  // Only bother trying to use the index for volumes
  if (volume && first_wr > 0)
  {
    auto it = midas_idx.cbegin();
    for (; it != midas_idx.cend(); ++it) {
      if (it->first > first_wr) {
        break;
      }
    }
    if (it != midas_idx.cbegin())
      --it;
    if (it != midas_idx.cend()) {
      std::cerr << "Beginning search at " << it->second << " from midas.idx" << std::endl;
      std::regex pattern(R"(^([\w\/]+?)\/R(\d+)_(\d+))");
      std::smatch sm;
      if(std::regex_match(it->second, sm, pattern)) {
        std::string testPrefix = sm[1];
        int test_in_run = std::stoi(sm[2]);
        int test_in_index = std::stoi(sm[3]);
        if (strcmp(testPrefix.c_str(), inFprefix) == 0) {
          in_run = test_in_run;
          in_index = test_in_index;
          std::cerr << "Run is " << in_run << ", File is " << in_index << std::endl;
        }
      }
    }
  }

  if (volume)
  {
    std::cerr << "Reading directory to find run numbers" << std::endl;
    DIR* dir = opendir(inFprefix);
    struct dirent* e;
    while ((e = readdir(dir)) != NULL)
    {
      int r = 0;
      if (sscanf(e->d_name + 1, "%d", &r) == 1)
      {
        if (r > max_run) max_run = r;
      }
    }
    std::cerr << "Last run in the volume is " << max_run << std::endl;
  }

  if (volume)
  {
    sprintf(inF, "%s/R%d_%d", inFprefix, in_run, in_index);
  }
  else if (prefix)
  {
    sprintf(inF, "%s_%d", inFprefix, in_index);
  }
  else
  {
    sprintf(inF, "%s", inFprefix);
  }

  gzifstream fs;
  tryopen(fs, inF);
  if (!fs)
  {
    if (volume)
    {
      while (!fs && in_run < max_run)
      {
        std::cerr << "Input file: " << inF << " could not be opened" << std::endl;
        in_run += 1;
        sprintf(inF, "%s/R%d_%d", inFprefix, in_run, in_index);
        tryopen(fs, inF);
      }
    }
    else if (prefix)
    {
      std::cerr << "Input file: " << inF << " could not be opened" << std::endl;
      return 1;
    }
    if (!fs)
    {
      std::cerr << "Input file: " << inF << " could not be opened" << std::endl;
      return 1;
    }
  }

  in_index++;
  std::cerr << "Opened input file: " << inF << std::endl;

  if (strcmp(outFprefix, "fifo") == 0)
  {
    std::cout << "File \"fifo\" requested, making a file fifo.lmd" << std::endl;
    max_size = -1;
    struct stat buf;
    int err = stat("fifo.lmd", &buf);
    if (err == -1 && errno == ENOENT)
    {
        err = mkfifo("fifo.lmd", 0644);
        if (err == -1)
        {
          std::cerr << "Error making the FIFO" << std::endl;
          return 1;
        }
    }
    else if (!S_ISFIFO(buf.st_mode))
    {
      std::cerr << "FIFO file exists but isn't a FIFO!" << std::endl;
      return 1;
    }

    sprintf(outF, "%s.lmd", outFprefix);
  }
  else if (strcmp(outFprefix, "-") == 0)
  {
    std::cerr << "Using stdout for output" << std::endl;
    if (isatty(1))
    {
      std::cerr << "STDOUT is a TTY, not outputting because that's bad!" << std::endl;
      return 1;
    }
    max_size = -1;
    sprintf(outF, "/dev/stdout");
  }
  else
  {
    sprintf(outF, "%s%04d.lmd", outFprefix, out_index++);
  }
  int out = f_evt_put_open(outF, 32*1024, 16, 10, 1, &channel_out, NULL);
  size_t bytes_file = 0;

  if (out != PUTEVT__SUCCESS)
  {
    std::cerr << "Output file: " << outF << " could not be opened: " << out << std::endl;
    perror("Error:");
    return 1;
  }
  std::cerr << "Opened output file: " << outF << std::endl;

  int i = 0;
  uint64_t wr = 0;
  long block_wr = 0;
  bool wr_seek = false;
  bool file_wr_found = false;
  bool wr_found = false;
  bool block_found = true;

  char* blockData = 0;
  size_t blockDataLen = 0;
  uint32_t* data = 0;
  size_t dataLen = 0;

  long last_file_wr = 0;
  long file_start_wr = 0;
  int file_blocks = 0;

  long last_block_wr = 0;

  while (fs)
  {
    uint32_t b1, b2;
    fs.read(reinterpret_cast<char*>(&b1), 4);
    fs.read(reinterpret_cast<char*>(&b2), 4);
    size_t blockLen = 0;

    if (fs.eof() || (wr_seek && file_wr_found))
    {
      last_file_wr = file_start_wr;
      file_start_wr = 0;
      file_blocks = 0;
      file_wr_found = false;
      std::cerr << "Saved file's WR = " << std::hex << last_file_wr << std::dec << std::endl;
      if (volume)
      {
        sprintf(inF, "%s/R%d_%d", inFprefix, in_run, in_index++);
        tryopen(fs, inF);
        fs.read(reinterpret_cast<char*>(&b1), 4);
        fs.read(reinterpret_cast<char*>(&b2), 4);
        while (!fs && in_run < max_run)
        {
          std::cerr << "File " << inF << " not open, trying to increase run" << std::endl;
          in_run++;
          in_index = 0;
          sprintf(inF, "%s/R%d_%d", inFprefix, in_run, in_index++);
          tryopen(fs, inF);
          fs.read(reinterpret_cast<char*>(&b1), 4);
          fs.read(reinterpret_cast<char*>(&b2), 4);
        }
        std::cerr << "Opened input file: " << inF << std::endl;
      }
      else if (prefix)
      {
        sprintf(inF, "%s_%d", inFprefix, in_index++);
        tryopen(fs, inF);
        std::cerr << "Opened input file: " << inF << std::endl;
        fs.read(reinterpret_cast<char*>(&b1), 4);
        fs.read(reinterpret_cast<char*>(&b2), 4);
      }
      if (fs.eof())
      {
        std::cout << "didn't open the file, ending" << std::endl;
        break;
      }
    }

      // Block header -> just suck up other block entries
      if (b1 == 0x45594245 && b2 == 0x41544144)
      {
        block_found = true;
        // Block Header
        //std::cout << "BLOCK" << std::endl;
        fs.read(reinterpret_cast<char*>(&b1), 4);
        fs.read(reinterpret_cast<char*>(&b2), 4);
        //std::cout << "Sequence = " << b1 << std::endl;
        //std::cout << "Unknown = " << b2 << std::endl;

        fs.read(reinterpret_cast<char*>(&b1), 4);
        //std::cout << "Endians = " << b1 << std::endl;
        fs.read(reinterpret_cast<char*>(&b2), 4);
        //std::cout << "Length = " << b2 << std::endl;

        blockLen = b2;
        if (blockLen > blockDataLen)
        {
          delete[] blockData;
          blockData = new char[blockLen];
          blockDataLen = blockLen;
        }
        fs.read(blockData, blockLen);

        /* Scan block to get a timestamp */
        uint32_t* block = (uint32_t*)blockData;
        uint32_t* blockEnd = (uint32_t*)(blockData + blockLen);
        block++;
        file_blocks++;
        long lowTS = *block++ & 0x0FFFFFFF;
        block--;
        block--;
        if (file_start_wr == 0) file_start_wr = lowTS;
        long highTS = 0;
        long highestTS = 0;
        if (wr_found) wr_seek = false;
        while(block < blockEnd)
        {
          uint32_t b1 = *block++;
          long wrLow = *block++;
          if ((b1 & 0xC0F00000) == 0x80500000)
          {
              highestTS = b1 & 0x000FFFFF;
              highTS = *block & 0x000FFFFF;
              long syncLow = *(block + 1) & 0x0FFFFFFF;
              wr = (highestTS << 48) | (highTS << 28) | lowTS;
#if DEBUG
              std::cout << "Updated from INFO words " << std::hex << wr << std::dec <<std::endl;
#endif
              if (block != (uint32_t*)(blockData) + 2 && wrLow < lowTS)
              {
                block_wr = wr - 0x10000000;
              }
              else
              {
                block_wr = wr;
              }
              break;
          }
        }
        if (highestTS == 0)
        {
          block_wr = (wr & ~0x0FFFFFFF) | lowTS;
        }
        else if (!file_wr_found && highestTS != 0)
        {
          file_wr_found = true;
          file_start_wr |= (wr & ~0x0FFFFFFF);
          if (file_blocks > 1) file_start_wr -= 0x10000000;
          midas_idx[file_start_wr] = inF;
          if (index) {
            wr_seek = true;
            continue;
          }
        }

        fs.ignore(64*1024 - blockLen - 24);

      }
      else
      {
        if (block_found)
        {
          std::cerr << "Didn't find MIDAS block entry as expected..." << std::endl;
          std::cerr << std::hex << b1 << " , " << b2 << std::endl;
          block_found = false;
        }
        continue;
      }

      if (wr < first_wr)
      {
        wr_seek = true;
        if (wr_found) wr_seek = false;
        continue;
      }

      if (wr_seek)
      {
        std::cerr << "Found WR window in file (" << std::hex << wr << std::dec << "), going back a file to catch start " << std::endl;
        wr = last_file_wr;
        std::cerr << "Going back to WR = " << std::hex << last_file_wr << std::dec << std::endl;
        fs.close();
        wr_found = true;
        wr_seek = true;
        in_index -= 2;
        continue;
      }

      if (wr > last_wr)
      {
        std::cout << "White rabbit " << std::hex << wr << std::dec << " is past the last_wr, ending" << std::endl;
        break;
      }


      size_t req = (blockLen >> 2) + 50;
      if (req > dataLen)
      {
        delete[] data;
        data = new uint32_t[req];
        dataLen = req;
      }
      uint32_t* buf = data;
      uint32_t data_words = (blockLen >> 1) + 10;

      *buf++ = data_words + 10;
      *buf++ = 0x0001000A;
      *buf++ = 0x00010000;
      *buf++ = i++;

      *buf++ = data_words + 2;
      *buf++ = 0x0001000a;
      *buf++ = 0x2500005a;

      *buf++ = 0x700;
      *buf++ = (0x03e1 << 16) | ((block_wr >>  0) & 0xFFFF);
      *buf++ = (0x04e1 << 16) | ((block_wr >> 16) & 0xFFFF);
      *buf++ = (0x05e1 << 16) | ((block_wr >> 32) & 0xFFFF);
      *buf++ = (0x06e1 << 16) | ((block_wr >> 48) & 0xFFFF);

      if (block_wr < last_block_wr) {
        std::cerr << "ERROR: WR of this block was before previous block?!" << std::endl;
        return 1;
      }

      last_block_wr = block_wr;

      memcpy(buf, blockData, blockLen);

      f_evt_put_event(&channel_out, (int32_t*)data);
      bytes_file += data[0] * 2;
      if (max_size != -1 && bytes_file >= 1999 * 1024 * 1024)
      {
          // Reset file pointer
          f_evt_put_close(&channel_out);
          sprintf(outF, "%s%04d.lmd", outFprefix, out_index++);
          out = f_evt_put_open(outF, 32*1024, 16, 10, 1, &channel_out, NULL);
          if (out != PUTEVT__SUCCESS)
          {
            std::cerr << "Output file: " << outF << " could not be opened" << std::endl;
            return 1;
          }
          std::cerr << "Opened output file: " << outF << std::endl;
          bytes_file = 0;
      }

      if ((i % 100) == 0)
      {
        std::cerr << "Block number: " << i << ", WR timestamp: " << std::hex << wr << std::dec << "                             \r";
      }
  }

  std::cerr << std::endl;
  std::cerr << "Generated " << i << " MBS events (blocks)!" << std::endl;

  std::cerr << "Writing MBS file..." << std::endl;
  f_evt_put_close(&channel_out);
  std::cerr << "Completed" << std::endl;

  {
    std::ofstream ixf("midas.idx");
    for (auto i : midas_idx)
    {
      ixf << i.second << " " << i.first << std::endl;
    }
    ixf.close();
    std::cerr << "Updating MIDAS IDX file" << std::endl;
  }

  return 0;
}

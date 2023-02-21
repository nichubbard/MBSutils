extern "C"
{
  #include "f_evt.h"
}

#include <iostream>
#include <fstream>
#include <cstdint>
#include <algorithm>
#include <signal.h>
#include <sstream>
#include <unistd.h>

#include <term.h>
#include <curses.h>

sig_atomic_t ctrlc = true;

void ctrlc_handler(int)
{
  ctrlc = false;
}

struct aida_event
{
  uint64_t timestamp;
  uint32_t data_word;
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

std::string human_size(int64_t number)
{
  char buf[256];
  if (number > 1024 * 1024 * 1024)
  {
    sprintf(buf, "%ld %s", number / (1024*1024*1024), "GB");
  }
  else if (number > 1024 * 1024)
  {
    sprintf(buf, "%ld %s", number / (1024*1024), "MB");
  }
  else if (number > 1024)
  {
    sprintf(buf, "%ld %s", number / 1024, "kB");
  }
  else
  {
    sprintf(buf, "%ld %s", number, "B");
  }

  return std::string(buf);
}

std::string wr_to_string(long wr, bool ns = false)
{
  time_t time = wr / 1e9;
  char* str = asctime(localtime(&time));
  if (ns)
  {
      char buf[256];
      sprintf(buf, "%s.%f", str, 1./(wr - time));
      std::string sstr(buf);
      return sstr;
  }
  else
  {
    std::string sstr(str);
    return sstr;
  }
}

int main(int argc, char** argv)
{
  char* inF;
  char* outFprefix;

  if (argc < 3)
  {
    std::cerr << "Usage: " << argv[0] << " input_server output_lmd_file" << std::endl;
    std::cerr << "This program will convert an AIDA MIDAS file (input) to an MBS LMD file as if from the FDR" << std::endl;
    std::cerr << "The MBS WR timestamp is set to the timestamp of the first event in the block" << std::endl;
    std::cerr << "Aside from finding this WR timestamp no analysis of AIDA data is done!" << std::endl;
    return 1;
  }

  char *termtype = getenv ("TERM");
  tgetent (NULL, termtype);

  outFprefix = argv[argc - 1];
  char outF[256];

  int32_t* event;
  int32_t* buffer;

  s_evt_channel channel = {0};
  inF = argv[1];
  int in = f_evt_get_open(GETEVT__STREAM, inF, &channel, NULL, 1, 0);
  if (in != GETEVT__SUCCESS)
  {
    std::cerr << "Input server: " << inF << " could not be opened" << std::endl;
    return 1;
  }

  std::cout << "Connected to server: " << inF << std::endl;

  int aida_in = 0;
  int aida_out = 0;
  int sequence = 0;
  int mbs_in = 0;
  int64_t b_in = 0;
  int64_t b_out = 0;

  int old_aida_in = 0;
  int old_aida_out = 0;
  int old_sequence = 0;
  int old_mbs_in = 0;
  int64_t old_b_in = 0;
  int64_t old_b_out = 0;

  uint32_t* block = new uint32_t[16384 + 12];
  uint32_t* block_end = block + 16384 + 12;
  uint32_t* buf = block;
  *buf++ = 10;
  *buf++ = 0x0001000A;
  *buf++ = 1;
  *buf++ = 0;

  *buf++ = 2;
  *buf++ = 0x0001000a;
  *buf++ = 0x25000001;

  *buf++ = 0x200;

  int out_index = 0;
  int blocks_file = 0;
  s_evt_channel channel_out = {0};
  sprintf(outF, "%s%04d.lmd", outFprefix, out_index++);
  int out = f_evt_put_open(outF, 0, 0, 0, 0, &channel_out, NULL);
  if (out != GETEVT__SUCCESS)
  {
    std::cerr << "Output file: " << outF << " could not be opened" << std::endl;
    return 1;
  }

  uint32_t middle_wr_last[12] = {0};
  uint32_t high_wr_last[12] = {0};

  struct sigaction action;
  action.sa_handler = ctrlc_handler;
  sigemptyset (&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGINT, &action, NULL);

  time_t t = time(NULL);

  char* up = tgetstr("up", NULL);
  char* cr = tgetstr("cr", NULL);
  char* ce = tgetstr("ce", NULL);

  std::cout << "AIDA Alpha File 'Compressor'" << std::endl;
  std::cout << std::endl;
  std::cout << "Conneted to: " << inF << std::endl;
  std::cout << "Current file: " << outF << std::endl;
  std::cout << std::endl;
  std::cout << "Events in:" << std::endl;
  std::cout << "\tAIDA:\t0\t[0/s]" << std::endl;
  std::cout << "\tMBS:\t0\t[0/s]" << std::endl;
  std::cout << "\tData:\t0 kB\t[0/s]" << std::endl;
  std::cout << "Events out:" << std::endl;
  std::cout << "\tAIDA:\t0\t[0/s]" << std::endl;
  std::cout << "\tMBS:\t0\t[0/s]" << std::endl;
  std::cout << "\tData:\t0 kB\t[0/s]" << std::endl;

  while (ctrlc && f_evt_get_event(&channel, &event, &buffer) == GETEVT__SUCCESS)
  {
    if (t != time(NULL))
    {
      t = time(NULL);

      for (int i = 0; i < 11; i++)
      {
          std::cout << up << cr << ce;
      }

      std::cout << "Conneted to: " << inF << std::endl;
      std::cout << "Current file: " << outF << std::endl;
      std::cout << std::endl;
      std::cout << "Events in:" << std::endl;
      std::cout << "\tAIDA:\t" << aida_in << "\t[" << (aida_in - old_aida_in) << "/s]" << std::endl;
      std::cout << "\tMBS:\t" << mbs_in << "\t[" << (mbs_in - old_mbs_in) <<  "/s]" << std::endl;
      std::cout << "\tData:\t" << human_size(b_in) << "\t[" << human_size(b_in - old_b_in) <<  "/s]" << std::endl;
      std::cout << "Events out:" << std::endl;
      std::cout << "\tAIDA:\t" << aida_out << "\t[" << (aida_out - old_aida_out) <<  "/s]" << std::endl;
      std::cout << "\tMBS:\t" << sequence << "\t[" << (sequence - old_sequence) <<  "/s]" << std::endl;
      std::cout << "\tData:\t" << human_size(b_out) << "\t[" << human_size(b_out - old_b_out) <<  "/s]" << std::endl;

      old_aida_in = aida_in;
      old_aida_out = aida_out;
      old_sequence = sequence;
      old_mbs_in = mbs_in;
      old_b_in = b_in;
      old_b_out = b_out;
    }
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
       if(*data != 0x200) continue;
       int32_t* data_cur = data + 1;
       //std::cout << "Sub Event #" << i << " Subsystem ID: " << *data << std::endl;
       uint64_t wr = 0;
       wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 0;
       wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 16;
       wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 32;
       wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 48;
       b_in += dataLen;
       while (data_cur < data + dataLen)
       {
          int32_t word = *data_cur++;
          int32_t lowTS = (*data_cur++) & 0x0FFFFFFF;
          // WR timestamp marker
          if ((word & 0xC0F00000) == 0x80500000)
          {
            uint64_t middleTS = *data_cur++;
            lowTS = (*data_cur++) & 0x0FFFFFFF;
            uint64_t highTS = word & 0x000FFFFF;
            middleTS &= 0x000FFFFF;
            wr = (highTS << 48) | (middleTS << 28) | lowTS;
            aida_in += 2;
            continue;
          }
          // Other data
          aida_event evt = {0};
          evt.data_word = word;
          evt.timestamp = (wr & ~0x0FFFFFFFULL) | lowTS;
          aida_in++;

          if (buf + 6 >= block_end)
          {
              // Calculate block length = 10 + size of block in 16-bits (2*size of 32-bit)
              block[0] = ((buf - block - 12) << 1) + 10 + 10;
              block[3] = sequence++;
              // Update subblock length
              block[4] = ((buf - block - 12) << 1) + 2 + 10;

              // WRITE THE BLOCK HERE
              f_evt_put_event(&channel_out, (int32_t*)block);

              if (blocks_file++ == 32000)
              {
                  // Reset file pointer
                  f_evt_put_close(&channel_out);
                  sprintf(outF, "%s%04d.lmd", outFprefix, out_index++);
                  out = f_evt_put_open(outF, 0, 0, 0, 0, &channel_out, NULL);
                  if (out != GETEVT__SUCCESS)
                  {
                    std::cerr << "Output file: " << outF << " could not be opened" << std::endl;
                    return 1;
                  }
                  blocks_file = 0;
                  for (int i = 0; i < 12; ++i)
                  {
                    middle_wr_last[i] = 0;;
                    high_wr_last[i] = 0;
                  }
              }

              // Reset pointer to WR start
              buf = block + 8;
          }

          // Write White rabbit initial timestamp
          uint64_t wr_out = evt.timestamp;
          if(buf == block + 8)
          {
            *buf++ = (0x03e1 << 16) | ((wr_out >>  0) & 0xFFFF);
            *buf++ = (0x04e1 << 16) | ((wr_out >> 16) & 0xFFFF);
            *buf++ = (0x05e1 << 16) | ((wr_out >> 32) & 0xFFFF);
            *buf++ = (0x06e1 << 16) | ((wr_out >> 48) & 0xFFFF);
            b_out += 16;
          }

          // break the timestamp to the GREAT format
          uint32_t low_wr = wr_out & 0x0FFFFFFF;
          uint32_t middle_wr = (wr_out >> 28) & 0xFFFFF;
          uint32_t high_wr = (wr_out >> 48) & 0xFFFFF;

          int module = module_from_word(evt.data_word);
          if (module == -1) continue;
          if (middle_wr_last[module] != middle_wr || high_wr_last[module] != high_wr)
          {
            // Generate SYNC words if top WR bits changed
            *buf++ = 0x80500000 | high_wr | (module << 24);
            *buf++ = low_wr;
            *buf++ = 0x80400000 | middle_wr | (module << 24);
            *buf++ = low_wr;
            middle_wr_last[module] = middle_wr;
            high_wr_last[module] = high_wr;
            b_out += 8;
          }

          *buf++ = evt.data_word;
          *buf++ = low_wr;
          b_out += 4;

          aida_out++;
        }
        mbs_in++;
     }
  }

  f_evt_get_close(&channel);

  if(buf > block + 12)
  {
    // Calculate block length = 10 + size of block in 16-bits (2*size of 32-bit)
    block[0] = ((buf - block - 12) << 1) + 10 + 10;
    block[3] = sequence++;
    // Update subblock length
    block[4] = ((buf - block - 12) << 1) + 2 + 10;

    // WRITE THE BLOCK HERE
    f_evt_put_event(&channel_out, (int32_t*)block);
  }

  //std::cout << "Merging complete: AIDA events " << t << ", MBS blocks: " << sequence << std::endl;

  f_evt_put_close(&channel_out);

  for (int i = 0; i < 11; i++)
  {
      std::cout << up << cr << ce;
  }

  std::cout << "Conneted to: " << inF << std::endl;
  std::cout << "Current file: " << outF << std::endl;
  std::cout << std::endl;
  std::cout << "Events in:" << std::endl;
  std::cout << "\tAIDA:\t" << aida_in << std::endl;
  std::cout << "\tMBS:\t" << mbs_in << std::endl;
  std::cout << "\tData:\t" << human_size(b_in) << std::endl;
  std::cout << "Events out:" << std::endl;
  std::cout << "\tAIDA:\t" << aida_out << std::endl;
  std::cout << "\tMBS:\t" << sequence << std::endl;
  std::cout << "\tData:\t" << human_size(b_out) << std::endl;

  std::cout << std::endl;
  std::cout << "Finished, good bye" << std::endl;

  return 0;
}

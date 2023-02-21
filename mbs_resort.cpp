extern "C"
{
  #include "f_evt.h"
  #include "s_filhe.h"
}

#include <iostream>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <unistd.h>
#include <sys/time.h>
#include <vector>

static const size_t BUFFER_SIZE = 1024 * 1024 * 1024 * 4L;

struct aida_event
{
  uint64_t timestamp;
  uint32_t data_word;
};

struct aida_handle
{
    bool ram;
    std::ifstream handle;
    aida_event* buffer;
    aida_event* buffer_end;
};

static const size_t AIDA_EVENTS = BUFFER_SIZE / sizeof(aida_event);

bool aida_compare(const aida_event& a1, const aida_event& a2)
{
  return a1.timestamp < a2.timestamp;
}

void aida_sort(aida_event* start, aida_event* end)
{
  std::sort(start, end, aida_compare);
}

struct heap_entry
{
  aida_event event;
  int block;
};

bool heap_compare(const heap_entry& a1, const heap_entry& a2)
{
  return a1.event.timestamp > a2.event.timestamp;
}

bool aida_handle_eof(aida_handle const& handle)
{
  if(handle.ram)
  {
    return handle.buffer == handle.buffer_end;
  }
  return !handle.handle;
}

aida_event get_aida_event(aida_handle& handle)
{
  if (handle.ram)
  {
    return *handle.buffer++;
  }
  else
  {
    aida_event e;
    handle.handle.read((char*)&e, sizeof(aida_event));
    return e;
  }
}

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

void handle_block(int n, aida_event* aida_buf, aida_event* aida_buf_end)
{
  std::cout << "Sorting AIDA block" << std::endl;
  aida_sort(aida_buf, aida_buf_end);
  std::cout << "First timestamp: " << std::hex << aida_buf->timestamp << std::dec << std::endl;
  std::cout << "Last timestamp: " << std::hex << (aida_buf_end - 1)->timestamp << std::dec << std::endl;

  std::cout << "Writing AIDA blocK" << std::endl;
  std::ofstream f;
  std::stringstream ss;
  ss << "sorted_tmp_" << n;
  f.open(ss.str().c_str(), std::ios::out | std::ios::trunc | std::ios::binary);
  f.write((char*)aida_buf, (char*)aida_buf_end - (char*)aida_buf);
  f.close();
  std::cout << "Completed Block #" << n << std::endl;
}

s_evt_channel _merge_channel = {0};
int32_t* _merge_event = nullptr;
int32_t* _merge_buffer = nullptr;
bool _eof = false;

int emit_block(s_evt_channel* channel, int32_t* aida_block)
{
   uint64_t aida_wr = 0;
   aida_wr |= (uint64_t)((aida_block[8]) & 0xFFFF) << 0;
   aida_wr |= (uint64_t)((aida_block[9]) & 0xFFFF) << 16;
   aida_wr |= (uint64_t)((aida_block[10]) & 0xFFFF) << 32;
   aida_wr |= (uint64_t)((aida_block[11]) & 0xFFFF) << 48;

   int length = 0;

   while (true)
   {
     // If we're out of MBS events, we try to open the nextinput (not done)
     // Otherwise just emit the AIDA block
     if (_eof)
     {
       int status = f_evt_put_event(channel, aida_block);
        if (status != PUTEVT__SUCCESS)
        {
          f_evt_error(status, NULL, 0);
        }
       return length;
     }

     s_ve10_1* eventH = reinterpret_cast<s_ve10_1*>(_merge_event);
     int se = f_evt_get_subevent(eventH, 0, NULL, NULL, NULL);
     uint64_t block_wr = 0;
     //std::cout << "Sub Events == " << se << std::endl;
     for (int i = 1; i <= se; ++i)
     {
       s_ves10_1* header;
       int32_t* data;
       int32_t dataLen;
       f_evt_get_subevent(eventH, i, (int32_t**)&header, &data, &dataLen);
       if(*data == 0x200) continue;
       if((data[1] & 0xFFFF0000) == 0x03e10000)
       {
         block_wr |= (uint64_t)((data[1]) & 0xFFFF) << 0;
         block_wr |= (uint64_t)((data[2]) & 0xFFFF) << 16;
         block_wr |= (uint64_t)((data[3]) & 0xFFFF) << 32;
         block_wr |= (uint64_t)((data[4]) & 0xFFFF) << 48;
        break;
       }
     }
     // Not a valid event (basically means it's an AIDA event)
     if (block_wr == 0)
     {
        _eof = f_evt_get_event(&_merge_channel, &_merge_event, &_merge_buffer) != GETEVT__SUCCESS;
        continue;
     }

     // Emit a block before the AIDA block
     if (block_wr < aida_wr)
     {
        int status = f_evt_put_event(channel, _merge_event);
        if (status != PUTEVT__SUCCESS)
        {
          f_evt_error(status, NULL, 0);
        }
        length += eventH->l_dlen * 2;
        _eof = f_evt_get_event(&_merge_channel, &_merge_event, &_merge_buffer) != GETEVT__SUCCESS;
        continue;
     }
     else
     {
       int status = f_evt_put_event(channel, aida_block);
        if (status != PUTEVT__SUCCESS)
        {
          f_evt_error(status, NULL, 0);
        }
       return length;
     }
   }
}

int main(int argc, char** argv)
{
  char* inF;
  char* outFprefix;

  if (argc < 3)
  {
    std::cerr << "Usage: " << argv[0] << " input_lmd_file [input_lmd_file_N ...] output_lmd_file" << std::endl;
    std::cerr << "This program will sort an AIDA MIDAS file (input)" << std::endl;
    std::cerr << "This really means sort - ordering all AIDA events by timestamp, incase the AIDA DAQ had an issue" << std::endl;
    std::cerr << "It will try to keep data in RAM using a buffer but will use files to sort very large data sets" << std::endl;
    return 1;
  }

  outFprefix = argv[argc - 1];
  char outF[256];

  std::cout << "Using sort buffer of " << (BUFFER_SIZE / (1024 * 1024 * 1024)) << " GB, which holds " << AIDA_EVENTS << " AIDA Events" << std::endl;
  aida_event* aida_buf = new aida_event[AIDA_EVENTS];
  aida_event* aida_buf_end = aida_buf + AIDA_EVENTS;
  aida_event* aida_buf_cur = aida_buf;

  int32_t* event;
  int32_t* buffer;
  int sorted_blocks = 0;

  for (int i = 1; i < argc - 1; i++)
  {
    s_evt_channel channel = {0};
    inF = argv[i];
    int in = f_evt_get_open(GETEVT__FILE, inF, &channel, NULL, 1, 0);
    if (in != GETEVT__SUCCESS)
    {
      std::cerr << "Input file: " << inF << " could not be opened" << std::endl;
      return 1;
    }

    std::cout << "Opened input file: " << inF << std::endl;

    uint64_t wrs[12] = {0};

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
         if(*data != 0x200) continue;
         int32_t* data_cur = data + 1;
         //std::cout << "Sub Event #" << i << " Subsystem ID: " << *data << std::endl;
         uint64_t wr = 0;
         wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 0;
         wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 16;
         wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 32;
         wr |= (uint64_t)((*data_cur++) & 0xFFFF) << 48;
         for (int i = 0; i < 12; ++i) {
           if (wrs[i] == 0) wrs[i] = wr;
         }
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
              int module = module_from_word(word);
              wrs[module] = (highTS << 48) | (middleTS << 28) | lowTS;
              continue;
            }
            // Other data
            aida_event evt = {0};
            evt.data_word = word;
            int module = module_from_word(word);
            //evt.timestamp = (wr & ~0x0FFFFFFFULL) | lowTS;
            evt.timestamp = (wrs[module] & ~0x0FFFFFFFULL) | lowTS;
            *aida_buf_cur++ = evt;
            if (aida_buf_cur == aida_buf_end)
            {
              handle_block(sorted_blocks++, aida_buf, aida_buf_cur);
              aida_buf_cur = aida_buf;
            }
         }
       }
    }

    f_evt_get_close(&channel);
  }

  std::vector<heap_entry> heap(sorted_blocks + 1);
  std::vector<aida_handle> handles(sorted_blocks + 1);

  if (sorted_blocks == 0)
  {
    std::cout << "Have " << 1 << " sorted blocks" << std::endl;
    std::cout << "Not using files, keeping it all in RAM" << std::endl;

    std::cout << "Sorting AIDA block" << std::endl;
    aida_sort(aida_buf, aida_buf_cur);
    std::cout << "First timestamp: " << std::hex << aida_buf->timestamp << std::dec << std::endl;
    std::cout << "Last timestamp: " << std::hex << (aida_buf_cur - 1)->timestamp << std::dec << std::endl;

    sorted_blocks++;

    handles[0].ram = true;
    handles[0].buffer = aida_buf;
    handles[0].buffer_end = aida_buf_cur;
    heap_entry h;
    h.event = get_aida_event(handles[0]);
    h.block = 0;
    heap[0] = h;
  }
  else
  {
    handle_block(sorted_blocks++, aida_buf, aida_buf_cur);

    std::cout << "Have " << sorted_blocks << " sorted blocks" << std::endl;

    delete[] aida_buf;

    for (int i = 0; i < sorted_blocks; ++i)
    {
      std::stringstream ss;
      ss << "sorted_tmp_" << i;
      handles[i].ram = false;
      handles[i].handle.open(ss.str().c_str(), std::ios::in | std::ios::binary);
      heap_entry h;
      h.event = get_aida_event(handles[i]);
      h.block = i;
      heap[i] = h;
    }
}

  std::make_heap(heap.begin(), heap.end(), heap_compare);
  std::cout << "Merging blocks..." << std::endl;
  int t = 0;
  int active = sorted_blocks;
  int sequence = 0;

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
  int bytes_file = 0;

  s_evt_channel channel_out = {0};
  sprintf(outF, "%s%04d.lmd", outFprefix, out_index++);
  int out = f_evt_put_open(outF, 32*1024, 16, 10, 1, &channel_out, NULL);
  if (out != GETEVT__SUCCESS)
  {
    std::cerr << "Output file: " << outF << " could not be opened" << std::endl;
    return 1;
  }

  // Start the input file again for mergin'
  int merge = f_evt_get_open(GETEVT__FILE, argv[1], &_merge_channel, NULL, 1, 0);
  if (merge != GETEVT__SUCCESS)
  {
    std::cerr << "Input file: " << argv[1] << " could not be opened" << std::endl;
    return 1;
  }
  _eof = f_evt_get_event(&_merge_channel, &_merge_event, &_merge_buffer) != GETEVT__SUCCESS;

  uint32_t middle_wr_last[12] = {0};
  uint32_t high_wr_last[12] = {0};

  while(active)
  {
      std::pop_heap(heap.begin(), heap.end(), heap_compare);
      heap_entry write = heap.back();

      // Check enough room for the new entry (worst case)
      if (buf + 6 >= block_end)
      {
          // Calculate block length = 10 + size of block in 16-bits (2*size of 32-bit)
          block[0] = ((buf - block - 12) << 1) + 10 + 10;
          block[3] = sequence++;
          // Update subblock length
          block[4] = ((buf - block - 12) << 1) + 2 + 10;

          // WRITE THE BLOCK HERE
          //f_evt_put_event(&channel_out, (int32_t*)block);
          bytes_file += emit_block(&channel_out, (int32_t*)block);
          bytes_file += block[0] * 2;

          if (bytes_file >= 1990 * 1024 * 1024)
          {
              // Reset file pointer
              f_evt_put_close(&channel_out);
              sprintf(outF, "%s%04d.lmd", outFprefix, out_index++);
              out = f_evt_put_open(outF, 64*1024, 16, 10, 1, &channel_out, NULL);
              if (out != GETEVT__SUCCESS)
              {
                std::cerr << "Output file: " << outF << " could not be opened" << std::endl;
                return 1;
              }
              bytes_file = 0;
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
      uint64_t wr = write.event.timestamp;
      if(buf == block + 8)
      {
        *buf++ = (0x03e1 << 16) | ((wr >>  0) & 0xFFFF);
        *buf++ = (0x04e1 << 16) | ((wr >> 16) & 0xFFFF);
        *buf++ = (0x05e1 << 16) | ((wr >> 32) & 0xFFFF);
        *buf++ = (0x06e1 << 16) | ((wr >> 48) & 0xFFFF);
      }

      // break the timestamp to the GREAT format
      uint32_t low_wr = wr & 0x0FFFFFFF;
      uint32_t middle_wr = (wr >> 28) & 0xFFFFF;
      uint32_t high_wr = (wr >> 48) & 0xFFFFF;

      int module = module_from_word(write.event.data_word);
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
      }

      *buf++ = write.event.data_word;
      *buf++ = low_wr;

      t++;

      heap_entry nh;
      nh.block = write.block;
      nh.event = get_aida_event(handles[nh.block]);

      if (aida_handle_eof(handles[write.block]))
      {
        active--;
        heap.pop_back();
        continue;
      }

      heap.back() = nh;
      std::push_heap(heap.begin(), heap.end(), heap_compare);
  }

  if(buf > block + 8)
  {
    // Calculate block length = 10 + size of block in 16-bits (2*size of 32-bit)
    block[0] = ((buf - block - 12) << 1) + 10 + 10;
    block[3] = sequence++;
    // Update subblock length
    block[4] = ((buf - block - 12) << 1) + 2 + 10;

    // WRITE THE BLOCK HERE
    f_evt_put_event(&channel_out, (int32_t*)block);
  }

  std::cout << "Merging complete: AIDA events " << t << ", MBS blocks: " << sequence << std::endl;

  f_evt_put_close(&channel_out);

  if(sorted_blocks > 1)
  {
    std::cout << "Cleanup..." << std::endl;
    for (int i = 0; i < sorted_blocks; ++i)
    {
      std::stringstream ss;
      ss << "sorted_tmp_" << i;
      handles[i].handle.close();
      unlink(ss.str().c_str());
    }
  }

  return 0;
}

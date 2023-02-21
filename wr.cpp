#include <iostream>
#include <string>
#include <cstdio>
#include <string>
#include <cstdint>
#include <map>

using wr_t = int64_t;

std::map<wr_t, wr_t> tai_map =
{
  {  63072010, 10 },
  {  78796811, 11 },
  {  94694412, 12 },
  { 126230413, 13 },
  { 157766414, 14 },
  { 189302415, 15 },
  { 220924816, 16 },
  { 252460817, 17 },
  { 283996818, 18 },
  { 315532819, 19 },
  { 362793620, 20 },
  { 394329621, 21 },
  { 425865622, 22 },
  { 489024023, 23 },
  { 567993624, 24 },
  { 631152025, 25 },
  { 662688026, 26 },
  { 709948827, 27 },
  { 741484828, 28 },
  { 773020829, 29 },
  { 820454430, 30 },
  { 867715231, 31 },
  { 915148832, 32 },
  {1136073633, 33 },
  {1230768034, 34 },
  {1341100835, 35 },
  {1435708836, 36 },
  {1483228837, 37 },
};

wr_t get_offset(wr_t time)
{
  wr_t offset = 0;
  for (auto i : tai_map)
  {
    if (time > i.first * 1000000000L) offset = i.second * 1000000000L;
  }
  return offset;
}

std::string wr_to_string(long wr, bool ns = false, bool tai = false)
{
  if (!tai)
  {
    wr -= get_offset(wr);
  }
  time_t time = wr / 1e9;
  char buf[256];
  char tbuf[256];
  if (tai)
  {
    struct tm* const timeptr = gmtime(&time);
    strftime(tbuf, 256, "%Y-%m-%d  TAI %H:%M:%S", timeptr);
  }
  else
  {
    struct tm* const timeptr = localtime(&time);
    strftime(tbuf, 256, "%Y-%m-%d %4Z %H:%M:%S", timeptr);
  }
  if (ns)
  {
    sprintf(buf, "%s.%09ld", tbuf, (wr - time*1000000000L));
  }
  else
  {
    sprintf(buf,"%s", tbuf);
  }
  std::string sstr(buf);
  return sstr;
}

int main(int argc, char** argv)
{
   if (argc == 2)
   {
      uint64_t wr = std::strtol(argv[1], NULL, 0);
      std::cout << "Timestamp   : 0x" << std::hex << wr << std::endl;
      std::cout << "Atomic Time : " << wr_to_string(wr, true, true) << std::endl;
      std::cout << "Clock Time  : " << wr_to_string(wr, true) << std::endl;
   }
   else
   {
     std::cerr << "Usage: " << argv[0] << " <WR timestamp>" << std::endl;
     std::cerr << "Turns a WR timestamp into a readable time" << std::endl;
     std::cerr << "Corrects for atomic time differences to be accurate to the second from 1972 until 2021" << std::endl;
     std::cerr << "Future times cannot be computed due to lack of knowledge of leap seconds" << std::endl;
     exit(1);
   }
}

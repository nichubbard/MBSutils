#include "typedefs.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <ctype.h>
#include <stdlib.h>
#include "sbs_def.h"
#include "sys_def.h"
#include "ml_def.h"
#include "portnum_def.h"
#include "s_daqst.h"
#include "s_setup.h"
#include "s_set_ml.h"
#include "s_set_mo.h"
#include "f_stccomm.h"
#include "f_ut_status.h"

#include <iostream>

extern "C" INTS4 f_mbs_status(CHARS *c_node, s_daqst *ps_daqst);

int main(int argc, char** argv)
{
  if (argc == 1)
  {
    fprintf(stderr, "Usage: %s [server]\n", argv[0]);
    return 1;
  }

  s_daqst stat = {0};
  char serv[128];
  strncpy(serv, argv[1], 127);
  f_mbs_status(serv, &stat);

  std::cout << "1: " << stat.l_endian << std::endl;
  std::cout << "Events: " << stat.bl_r_events << std::endl;
  std::cout << "Buffers: " << stat.bl_r_buffers << std::endl;
  std::cout << "BufStream: " << stat.bl_r_bufstream << std::endl;
  std::cout << "KByte: " <<stat.bl_r_kbyte << std::endl;
  std::cout << "Tape: " << stat.bl_r_kbyte_tape << std::endl;
  std::cout << "Trans: " << stat.bl_trans_connected << std::endl;
  std::cout << "Streams: " << stat.l_free_streams << " / " << stat.bl_no_streams << std::endl;


  return 0;
}

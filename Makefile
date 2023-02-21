PROJECTS = mbs_resort mbs_dump mbs_compress midas_to_mbs mbs_ts mbs_rate mbs_range midas_dump efficiency wr

all: $(PROJECTS)

ObjSuf=o
DepSuf=dep
dir = $(shell pwd)

include MbsAPI/Module.mk
include MbsAPIbase/Module.mk

CFLAGS=-g -fPIC -fdiagnostics-color -Wall -DLinux -pthread  -m64 -I$(dir)/MbsAPI -I$(dir)/MbsAPIbase $(DEFINITIONS) -O3 -Wno-unused

mbs_compress: mbs_compress.cpp $(MBSAPI_O) $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS) -lncurses

mbs_dump: mbs_dump.cpp $(MBSAPI_O) $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS)

efficiency: efficiency.cpp $(MBSAPI_O) $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS)

mbs_resort: mbs_resort.cpp $(MBSAPI_O) $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS)

midas_to_mbs: midas_to_mbs.cpp zfstream.cc $(MBSAPI_O) $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS) -lz

mbs_ts: mbs_ts.cpp $(MBSAPI_O) $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS)

mbs_rate: mbs_rate.cpp MbsAPIbase/f_mbs_status.o $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS)

mbs_range: mbs_range.cpp $(MBSAPI_O) $(MBSAPIBASE_O)
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS)

midas_dump: midas_dump.cpp
	$(CXX) -o $@ $^ -std=c++11 $(CFLAGS)

virtual_scalers: virtual_scalers.cpp
	$(CXX) -o $@ $^ -std=c++17 -Icxxcurses/include -lncurses

wr: wr.cpp
	$(CXX) -o $@ $^ -std=c++11

.PHONY: clean
clean:
	$(MAKE) clean-bin
	-rm $(PROJECTS)

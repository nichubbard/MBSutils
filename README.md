AIDA in MBS Utilities:

## mbs_dump

Synopsis: Dumps all AIDA event words or MBS events

Output Format: WR Timestamp | Raw event words | Extracted information

Simple tool to print all AIDA events in one or more MBS files.
It will print the full WR timestamp (constructed), the raw event data (2 32-bit words)
and than unpack the event word to show what the bits refer to.

By default it won't print timestamp events (SYNC), just track them to build TS
The option -t will print such events

With the option -o it will print all MBS subevents showing the MBS structure
with the option -n it will supress the AIDA event printing and allow just
monitoring the MBS structure

At the end it will print the subevent multiplicity for verifying time-stitching

ADC format:
 ADC FF:CC G DDDD
 FF: FEE number (from 1)
 CC: Channel number (from 0)
 G: Gain: H for High energy or L for Low energy
 DDDD: ADC data (16-bit intensity)

INFO format:
  INFO C XXX DDD:
  C: Info code
  XXX: Info code meaning
  DDD: Specific info code details

  INFO 2 PAUSE FF
  FF: FEE number (from 1)

  INFO 3 RESUME FF
  FF: FEE number (from 1)

  INFO 4 SYNC48 FF TTTTTT
  FF: FEE number (from 1)
  TTTTT: Timestamp bits 48:28

  INFO 5 SYNC63 FF TTTT
  FF: FEE number (from 1)
  TTTTT: Timestamp bits 63:48

  INFO 6 DISCRIM FF:A HHHH
  FF: FEE number (from 1)
  A: ASIC number (1-4)
  HHHH: 16-bit hit pattern (bit 1 = channel hit)

## midas_to_mbs

Synopsis: Convert MIDAS data to MBS data (as if saved from the AIDA FDR)

Usage: midas_to_mbs [input_file] [output_lmd_prefix]

input_file is the path to a MIDAS file (compressed or not)
if -p is used input_file is the prefix to a run ending (Rx_)
if -v is used input_file is a volume (folder) of MIDAS files

output_lmd_prefix is the output file without .lmd
if -s [size] is used, the prefix will be NNNN.lmd with N increasing for splits
If output_lmd_prefix is "fifo" it will write to fifo.lmd as a FIFO (pipe)
for passing to other tools (ucesb)

-f [WR] and -l [WR] can be used to set the first and last WR respectively to write
This allows synchronising with other MBS files (e.g. to merge MIDAS AIDA with non-AIDA MBS)

Volume runs (-v) will produce a midas.idx file to speed up finding the first/last
file with -f and -l
You can use -i to create this without doing anything else

## mbs_ts

## mbs_rate

Synopsis: Test to extract rates from an MBS stat server

## mbs_range

Synopsis: Print the first and last WR timestamp for one or more MBS files

## midas_dump

Synopsis: Like mbs_dump but for MIDAS data... less up-to-date
Can print WAVE data too

## efficiency

Synopsis: Check time-stitching efficiency by counting subevents in coincidence

## wr

Synopsis: Convert WR timestamp to a human readable time

Corrects for leap seconds

## mbs_resort

Synopsis: Sort (& compress) AIDA data!

Note: This program is obsolete with modern merger fixes

A complex program to do two tasks to overcome online merger issues:

1st (major): Will read in all data and sort it in ascending timestamp order,
this is what the format *should be* but timewarps ocassionally creep in.
Note: Due to the nature of how timestamps are stored it may be the calculated
timestamp is incorrect depending on how the data warps.
I need to identify why.

2nd (minor): Unlike the online merger, the final output only contains
minimal SYNC words to reconstruct timestamp for actual events, this reduces
file size especially for events with very low events/s (in which case the raw data is predominantly SYNC)

TODO: The option -f will track timestamps per FEE (for sorting)
TODO: The option -n will ignore the timestamp at the start of an MBS block

## mbs_compress

Synopsis: Save an AIDA file without extraneous SYNC words

Note:  This program is obsolete with modern merger fixes

This file connects to an MBS server, and opens an MBS file to write data recieved.
Instead of writing the raw data it "compresses" data by only writing SYNC
timestamps when there's non-SYNC data. For low data rate (alpha background) this
can reduce the file size by orders of magnitude.
(The rate of SYNC is approximately 10,000 events/sec!)

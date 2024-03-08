#ifndef _STRUCTURE_H
#define _STRUCTURE_H

#include <stdio.h>

#define MAXBUFSIZE 100000          // the largest total number of bytes you can receive
#define THRESHOLD  (64*1000)       // maximum size of the congestion window in bytes

#define MAXPKTSIZE 10028            // number of total bytes per packet, we have 16 bytes for header info
#define MSS        10000            // maximum number of actual data bytes per packet

#define TYPE_DATA  0
#define TYPE_ACK   1

#define TYPE       0               // type is listed at start
#define SEQNUM     4               // then seq number
#define ACKNUM     12               // then ack number
#define LENGTH     20              // then the length of data
#define DATA       28              // then the data itself

typedef enum state_t{
    SlowStart, // increase the congestion wiindow size exponentially
    CongestAvoid, // increase the congestion window size linearly becuase we passed the threshold
    FastRecov // halve congestion window after duplicates or timeout
} states;


#endif // _STRUCTURE_H

#include <math.h>      // required for mathematic operations
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>   // required for congestion control
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include "structure.h" // required to use MACROS

struct sockaddr_in sa_other;
int sock, sock_len;

#define TIMEOUTVAL 10000                    // timeout option in ms
#define MAXBUF     66*10000000              // maximum size of the sendable buffer

uint64_t total_trans_bytes = 0;             // size of the data to be sent
uint64_t curr_ack_num = 0;                  // number of the next ACK packets
int num_dup_ack = 0;                        // congestion control signal

uint64_t cwnd = 1 * MSS;                    // number of bytes to be sent in the window
uint64_t ssthresh = THRESHOLD;              // maximum threshold value of the window
uint64_t wind_min = 0;                      // minimum size of the received packet, min index in the window
uint64_t wind_max = 0;                      // maximum size of the next packet
int packet_loss_flag = 0;                   // flagged when the pacekt was loss

int init_flag = 0;                          // flag to indicate the size of the data
states next_state = SlowStart;              // state variable for congestion control

uint64_t curr_pkt_num = 0;                  // current packet number

static void *send_thread(void *arg);
static void *receive_thread(void *arg);

FILE *file_src;

// initalize the header of the message
#define _SET_MSG_HD(_seq_num, _ack_num, _len) *((uint32_t *)(msg+TYPE)) = TYPE_DATA; \
                                            *((uint64_t*)(msg+SEQNUM)) = (_seq_num); \
                                            *((uint64_t*)(msg+ACKNUM)) = (_ack_num); \
                                            *((uint64_t*)(msg+LENGTH)) = (_len);

// fill buffer by 0
#define _BE_2_ZERO(_msg, _len) memset(_msg, 0, _len);

// function for control congestion
// @time_out : flagged when the packet was timeout
// @dup_ack  : flagged when ACK was duplicated
void congestion_control(bool time_out, bool dup_ack) {
    switch (next_state)
    {
    case SlowStart:
        if (dup_ack)
            num_dup_ack++;
        else
            cwnd = cwnd + MSS, num_dup_ack = 0;
        
        if (time_out)
            packet_loss_flag = 1, num_dup_ack = 0, ssthresh = cwnd / 2, cwnd = MSS;       

        if (cwnd >= ssthresh)
            next_state = CongestAvoid;
        
        if (num_dup_ack == 3) {
            packet_loss_flag = 1, next_state = FastRecov, ssthresh = cwnd / 2;
            cwnd = ssthresh + 3;
        }
        break;
    case CongestAvoid:
        if (dup_ack) num_dup_ack++;
        else cwnd = cwnd + MSS * (MSS / cwnd), num_dup_ack = 0;

        if (dup_ack) num_dup_ack++;
        if (num_dup_ack == 3) {
            packet_loss_flag = 1, next_state = FastRecov, ssthresh = cwnd / 2;
            cwnd = ssthresh + 3;
        }

        if (time_out)
            packet_loss_flag = 1, next_state = SlowStart, ssthresh = cwnd / 2, cwnd = 1 * MSS, num_dup_ack = 0;

        break;
    case FastRecov:
        if (dup_ack) cwnd = cwnd + MSS;
        else next_state = CongestAvoid, cwnd = ssthresh, num_dup_ack = 0;

        if (time_out)
            packet_loss_flag = 1, next_state = SlowStart, ssthresh = cwnd / 2, cwnd = 1 * MSS, num_dup_ack = 0;

        break;
    }
}

// Helper function to send the total bytes to the receiver
void send_total_trans_bytes() {
    char msg[MAXPKTSIZE];
    _BE_2_ZERO(msg, MAXPKTSIZE)
    _SET_MSG_HD(0, 0, total_trans_bytes);
    sendto(sock, (void *)(msg), MAXPKTSIZE, 0, (struct sockaddr *)&sa_other, sock_len);
    printf("starting connection which send %d number of bytes \n", msg[LENGTH]);
}

// thread for sending data to the receiver
static void *send_thread(void *arg) {
    /* Open connection with sending bytes to transter */
    if (init_flag != 1)
        send_total_trans_bytes();

    /* loop while receiving data to be end */
    while ((int) wind_min < (int) wind_max) {
        uint64_t send_len = 0;
        if (packet_loss_flag || curr_pkt_num <= wind_max)
            send_len = ((int) curr_pkt_num > (int) total_trans_bytes - MSS) ? total_trans_bytes - curr_pkt_num : MSS;
        
        char msg[MAXPKTSIZE];
        _BE_2_ZERO(msg, MAXPKTSIZE)
        
        if (packet_loss_flag) curr_pkt_num = wind_min;
        if (curr_pkt_num < wind_max || packet_loss_flag) {
            _SET_MSG_HD(curr_pkt_num+1, curr_pkt_num+1, send_len)
            fseek(file_src, curr_pkt_num, SEEK_SET);
            fread(msg+DATA, send_len, 1, file_src);

            printf("in window, send packet #%d with length: %d\n", *((uint64_t *)(msg + SEQNUM)), send_len);
            printf("In fact, sender sents %d bytes \n", sendto(sock, (void *)msg, MAXPKTSIZE, 0, (struct sockaddr *)&sa_other, sock_len));
            curr_pkt_num += MSS, packet_loss_flag = 0;
        }        
    }

    return 0;
}

// Helper function to calculate right position of the window from memory
uint64_t calc_wind_max() {
    return (wind_min+cwnd) >= total_trans_bytes ? total_trans_bytes - 1 : wind_min+cwnd;
}

// Helper function to calculate left position of the window from memory
uint64_t calc_wind_min() {
    return (wind_min + MSS) >= total_trans_bytes ? total_trans_bytes - 1 : wind_min + MSS;
}

// Helper function to receive ACK from receiver
void receive_first_ack() {
    init_flag = 1;
    printf("initiation ack received \n");
}

// Helper function to receive new ACK
void receive_new_ack(uint64_t curr_ack) {
    congestion_control(false, false);
    wind_min = calc_wind_min();
    wind_max = calc_wind_max();
    curr_ack_num = curr_ack;    
}

// Helper function to receive duplicated ACK
void receive_duplicate_ack(uint64_t prev_ack) {
    congestion_control(false, true);
    wind_max = calc_wind_max();
    curr_ack_num = prev_ack;
}

// Helper function to process when packet is timeout
void process_timeout() {
    congestion_control(1, 0);
    wind_max = calc_wind_max();
}

// Helper function to check whether the packet is the last one
bool is_last_packet() {
    if ((int)curr_ack_num == total_trans_bytes+1) {
        printf("the connection is closed \n");
        return true;
    }
    return false;
}

// Thread for receiving ACK
static void *receive_thread(void *arg) {
    uint64_t prev_ack = 0;     // record the previous ACK number received

    clock_t init, later;                // used for calculating elapsed time to decide whether it is timeout.
    double diff_time;                    // elapsed time
    init = clock();                     // initial time
    while (1) {
        // if last packet was received, end loop
        if (is_last_packet()) break;

        char ack_msg[MAXPKTSIZE];
        _BE_2_ZERO(ack_msg, MAXPKTSIZE)
        if (recvfrom(sock, (void *)(ack_msg), MAXPKTSIZE, 0, (struct sockaddr *)&sa_other, (socklen_t *)&sock_len) != 0) {
            uint64_t curr_ack = 0;           // record the temporary ACK bytes
            curr_ack = *((uint64_t *)(ack_msg + ACKNUM));
            printf("curr_ack: %d, precACK: %d\n", curr_ack, prev_ack);
            
            switch (curr_ack) 
            {
            case 1:
                receive_first_ack();
                break;
            case 2: // if you get duplicated ACK
                receive_duplicate_ack(prev_ack);
                printf("receive duplicate ACK, cwnd: %d \n", cwnd);
                break;            
            default:
                if (curr_ack != total_trans_bytes+1 && curr_ack < prev_ack + MSS) // unexpected case (less than next packet's byte number) sequence number, trigger congestion control
                    receive_duplicate_ack(prev_ack);
                else { // new packet is received correctly
                    receive_new_ack(curr_ack);
                    prev_ack = curr_ack;
                    init = clock();
                }
                break;
            }
        }

        // check timeout value
        later = clock();
        diff_time = (later-init)*1000 / CLOCKS_PER_SEC;
        printf("diff_time in receive: %d", diff_time);
        if (diff_time > TIMEOUTVAL)
            process_timeout();

        printf("in receive: windmin %d, windmax %d\n", wind_min, wind_max);
    }
    return 0;
}

// Main function for sending data to the receiver
int reliablyTransfer(char *host_name, uint16_t host_udp_port, char *src_file_path, uint64_t bytes_trans)
{
    printf("start runSender function \n");
    /* Open the file to be sent */ 
    file_src = fopen(src_file_path, "rb");
    if (file_src == NULL) {
        printf("Could not open file to send.");
        return 1;
    }

    /* Calculate the target file size */
    fseek(file_src, 0, SEEK_END);
    uint64_t fileSize = ftell(file_src); // getting file size
    fseek(file_src, 0, SEEK_SET); 

    /* Set bytes to be transfered */
    total_trans_bytes = bytes_trans;
    if (total_trans_bytes > fileSize) total_trans_bytes = fileSize;

    wind_max = (total_trans_bytes >= MSS)? MSS: total_trans_bytes;
    
    printf("the number of total bytes to transfer: %d in a %d-bytes file \n", total_trans_bytes, fileSize);
    
    /* Create a socket */
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("socket");
        return 1;
    }

    sock_len = sizeof(sa_other);
    _BE_2_ZERO((char *)&sa_other, sock_len);
    sa_other.sin_family = AF_INET;
    sa_other.sin_port = htons(host_udp_port);
    if (inet_aton(host_name, &sa_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        return 1;
    }

    /* process recieving ACK and sending data by thread*/
    pthread_t tid1, tid2;
    pthread_create(&tid1, NULL, send_thread, NULL);
    pthread_create(&tid2, NULL, receive_thread, NULL);

    pthread_join(tid1, NULL);
    pthread_join(tid2, NULL);

    close(sock);
    printf("Closing the socket\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "Usage: %s receiver_hostname receiver_port file_to_transfer num_bytes_to_transfer\n\n", argv[0]);
        return 1;
    }    

    uint16_t udp_port = (uint16_t)atoi(argv[2]);
    uint64_t num_bytes = atoll(argv[4]);

    return  reliablyTransfer(argv[1], udp_port, argv[3], num_bytes);
}
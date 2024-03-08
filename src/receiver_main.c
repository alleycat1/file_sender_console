#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "structure.h"

#define _BE_2_ZERO(_msg, _len) memset(_msg, 0, _len);

struct sockaddr_in sa_me, sa_other;
int sock, sock_len;

FILE*    file_des; // Description of the receiving file

// Helper function to get the total bytes to be received
uint64_t get_total_rec_data_bytes() {
    uint64_t total_rec_data_bytes = 0;              // total bytes to be received
    char temp_rec_msg[MAXPKTSIZE];                  // message that we can get receiving packets in
    while (1) {
        int recvbyte = recvfrom(sock, (void*)(temp_rec_msg), MAXPKTSIZE, 0, (struct sockaddr *)&sa_other, (socklen_t*) &sock_len); // receive the first packet
        total_rec_data_bytes = *((uint64_t*)(temp_rec_msg+LENGTH));                     // get the total bytes
        if (*((uint64_t*)(temp_rec_msg+ACKNUM)) == 0 && recvbyte != 0) // data is 0 in first packet, and init_flag is not 0
            break;
    }
    return total_rec_data_bytes;
}

// Helper function to receive packet
uint64_t receive_packet_msg(uint64_t rec_data_bytes) {
    char temp_rec_msg[MAXPKTSIZE];           // message that we can get receiving packets in
    _BE_2_ZERO(temp_rec_msg, MAXPKTSIZE)
    int recvbytes = recvfrom(sock, (void*)temp_rec_msg, MAXPKTSIZE, 0, (struct sockaddr *)&sa_other, (socklen_t*) &sock_len);
    printf("getting packet %d, %d \n", recvbytes, *((uint64_t*)(temp_rec_msg+SEQNUM)) );
    if( rec_data_bytes + 1 == *((uint64_t*)(temp_rec_msg+SEQNUM)) ){ // see whether our bytes are the seq# we expect
        rec_data_bytes += *((uint64_t*)(temp_rec_msg+LENGTH));       // size of the packet to be sent
        fwrite(temp_rec_msg+DATA, 1, *((uint64_t*)(temp_rec_msg+LENGTH)), file_des); // write file to the destination
    }
    return rec_data_bytes;
}

// Helper function to return ACK to the center after receiving packet
void send_ack_msg(uint64_t rec_data_bytes) {
    char temp_ack_msg[MAXPKTSIZE];           // message that we can send outgoing packets in
    _BE_2_ZERO(temp_ack_msg, MAXPKTSIZE)
    *((uint64_t*)(temp_ack_msg+TYPE)) = TYPE_ACK;                // always respond the some kind of ACK
    *((uint64_t*)(temp_ack_msg+ACKNUM)) = rec_data_bytes+1;      // send how many we've received in order
    printf("receiver acks %d bytes \n", *((uint64_t*)(temp_ack_msg+ACKNUM)) );
    sendto(sock, (void*)(temp_ack_msg), MAXPKTSIZE, 0, (struct sockaddr *)&sa_other, sock_len); // send data
}

// Receiving thread function
static void *rec_thread(void *arg){
    uint64_t total_rec_data_bytes = get_total_rec_data_bytes();            // total bytes to be received
    printf("receiver receives %d bytes in the packet \n", total_rec_data_bytes);

    uint64_t rec_data_bytes = 0;                                  // A bytes that we've gotten in the correct order
    while(rec_data_bytes <  total_rec_data_bytes){                // loop until we have all the bytes expected
        rec_data_bytes = receive_packet_msg(rec_data_bytes);
        send_ack_msg(rec_data_bytes);        
    }
}

// Helper function to write data into files from the received packets
void process_write_2_file(char* des_file_path) {
    file_des = fopen( des_file_path, "w" ); // open file to be written

    _BE_2_ZERO((char *) &sa_other, sock_len)
	/* Now receive data and send ACK */
    pthread_t tid;
    pthread_create(&tid, NULL, rec_thread, NULL);   // make the actual threads
    pthread_join(tid, NULL);                        // wait for thread to end

    fclose(file_des);             // close file
}

// The main function to receive data from the sender
int reliablyReceive(uint16_t udp_port, char* des_file_path) {      
    /* create a socket */  
    if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        perror("socket");
        return 1;
    }

    /* set socket data*/
    sock_len = sizeof(sa_other);
    _BE_2_ZERO((char *) &sa_me, sock_len)

    sa_me.sin_family = AF_INET;
    sa_me.sin_port = htons(udp_port);
    sa_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");

    /* bind socket to the udp_port */
    if (bind(sock, (struct sockaddr*) &sa_me, sock_len) == -1) {
        perror("bind");
        return 1;
    }

    // write data into the file from the packets
    process_write_2_file(des_file_path);

    close(sock);
	printf("%s received.", des_file_path);
    return 2;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        return 1;
    }

    uint16_t udpPort = (uint16_t) atoi(argv[1]);

    return reliablyReceive(udpPort, argv[2]);
}
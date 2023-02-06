#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h> 
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

void *sendThread(void *args);
void *recvThread(void *args);
void *stdinThread(void *args);

// A timer class to keep track of the time that calculates it difference by reading the cpu clock
class msec_timer{
private:
    unsigned int msec_time;
    bool timer_active;
public:
    void start_timer(){
        this->msec_time = clock() * 1000 / CLOCKS_PER_SEC;
        this->timer_active = true;
    }
    void stop_timer(){
        this->timer_active = false;
    }
    bool timeout(unsigned int timout_threshold){
        unsigned int msec_time_now = clock() * 1000 / CLOCKS_PER_SEC;
        if((msec_time_now - msec_time >= timout_threshold || msec_time_now < msec_time) && this->timer_active){
            this->timer_active = false;
            return true;
        }
        return false;
    }
    bool isActive(){
        return this->timer_active;
    }
};


// There are 3 threads (excluding main) running
// These classes are for parameter passing between all threads
class recvThreadArgs{
    public:
        int sock;
        struct sockaddr* client;
        socklen_t* client_length;
        unsigned char *lastAck;
        bool *lastAckChange;
        bool connection_established;
        bool *shutdown;
        bool *quit_flag;
};

class sendThreadArgs{
    public:
        int sock;
        struct sockaddr* client;
        socklen_t* client_length;
        char *stdinBuffer;
        char *sendBuffer;
        unsigned char *lastSeqNum;
        unsigned char *lastAck;
        bool *lastAckChange;
        bool *inputFlag;
        bool *shutdown;
        bool *quit_flag;
};

class stdinThreadArgs{
    public:
        char *stdinBuffer;
        char *sendBuffer;
        unsigned char *lastSeqNum;
        bool *inputFlag;
        bool *shutdown;
        bool *termination_output_flag;
};

void send_seq(sendThreadArgs* args, unsigned char sequence_number);
void error(string msg);
void send_ack(recvThreadArgs* args, unsigned char  sequence_number);
void recieve_ack(unsigned char ack_seq, unsigned char *lastAck, bool* lastAckChange);

int main(int argc, char *argv[]){

    if(argc != 2){
        error("usage: ./server <server-port-number>");
    }

    int sock  = socket(AF_INET, SOCK_DGRAM, 0);
    
    if(sock < 0)
        error("socket opening error");

    struct sockaddr_in server, client;

    int server_length = sizeof(server);
    bzero(&server, server_length);
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("192.168.1.170");
    server.sin_port = htons(atoi(argv[1]));

    if((bind(sock, (struct sockaddr*) &server, server_length)) < 0)
        error("binding error");

    socklen_t client_len = sizeof(struct sockaddr_in);

    // Three heap allocations for parameter passing objects for three threads
    recvThreadArgs* argsRecv = new recvThreadArgs;
    sendThreadArgs* argsSend = new sendThreadArgs;
    stdinThreadArgs* argsStdin = new stdinThreadArgs;

    char stdinBuffer[1024], sendBuffer[4096];
    unsigned char  lastSeqNum = 0;
    unsigned char lastAck = 0;
    bool lastAckChange = false;
    bool inputFlag = false;
    bool shutdown = false;
    bool quit_flag = false;
    bool termination_output_flag = false;
    
    argsRecv->sock = sock;
    argsRecv->client = (struct sockaddr*)&client;
    argsRecv->client_length = (socklen_t*)&client_len;
    argsRecv->lastAck = &lastAck;
    argsRecv->lastAckChange = &lastAckChange;
    argsRecv->connection_established = false;
    argsRecv->shutdown = &shutdown;
    argsRecv->quit_flag = &quit_flag;

    argsSend->sock = sock;
    argsSend->client = (struct sockaddr*)&client;
    argsSend->client_length = (socklen_t*)&client_len;
    argsSend->stdinBuffer = stdinBuffer;
    argsSend->sendBuffer = sendBuffer;
    argsSend->lastSeqNum = &lastSeqNum;
    argsSend->lastAck = &lastAck;
    argsSend->lastAckChange = &lastAckChange;
    argsSend->inputFlag = &inputFlag;
    argsSend->shutdown = &shutdown;
    argsSend->quit_flag = &quit_flag;

    argsStdin->sendBuffer = sendBuffer;
    argsStdin->stdinBuffer = stdinBuffer;
    argsStdin->lastSeqNum = &lastSeqNum;
    argsStdin->inputFlag = &inputFlag;
    argsStdin->shutdown = &shutdown;
    argsStdin->termination_output_flag = &termination_output_flag;

    // Creating threads
    pthread_t threadNo[3];

    pthread_create(&threadNo[0], NULL, recvThread,argsRecv);
    
    pthread_create(&threadNo[1], NULL, sendThread,argsSend);

    pthread_create(&threadNo[2], NULL, stdinThread,argsStdin);

    // Busy wait until quit_flag is set
    while(!quit_flag){;}
    
    if(termination_output_flag == false){
        fprintf(stderr,"/*-------------------*\\\n");
        fprintf(stderr,"TERMINATING CONNECTION\n");
        fprintf(stderr,"\\*-------------------*/\n");
    }
    // Deallocation of heap objects
    free(argsRecv);
    free(argsSend);
    free(argsStdin);

    return 0;

}
/*
    The thread that controlls the socket for any incoming packeges
    14 byte is used for message, 15th byte is used for 
    sequence_number and 16th byte is used for flags
    ( 0 : message to be printed on stdout)
    ( 1 : an ack packet)
    ( 2 : termination flag)
*/
void *recvThread(void *args){

    recvThreadArgs actualArgs = *(recvThreadArgs*)args;
    char recieveBuffer[16];

    unsigned char next_seqnum = 0;

    while(1){
        if(recvfrom(actualArgs.sock, recieveBuffer, 16, 0, actualArgs.client, actualArgs.client_length) < 1)
            error("recvfrom error");
        
        if((unsigned char)recieveBuffer[15] == (unsigned char)2){
            *actualArgs.shutdown = true;
            break;
        }
        if((unsigned char)recieveBuffer[15] == (unsigned char)1){
            recieve_ack(recieveBuffer[14],actualArgs.lastAck,actualArgs.lastAckChange);
            continue;
        }
        /*
            With the connection of client to server, both display
            a "connection established" text to notify users and 
            due to this packet transfer, server can initiate the 
            chat too
        */
        if(actualArgs.connection_established == false){
            fprintf(stderr,"/*-------------------*\\\n");
            fprintf(stderr,"CONNNECTION ESTABLISHED\n");
            fprintf(stderr,"\\*-------------------*/\n");
            actualArgs.connection_established = true;
            send_ack((recvThreadArgs*)args,next_seqnum);
            next_seqnum++;
            continue;
        }
        if((unsigned char)recieveBuffer[14] == next_seqnum){
            fprintf(stdout, "%s",recieveBuffer);
            send_ack((recvThreadArgs*)args,next_seqnum);
            next_seqnum++;
        }
        else{
            send_ack((recvThreadArgs*)args,next_seqnum -(unsigned char)1);
        }
    }
    /*
        When 2 consecutive new line characters are sent to stdin in client,
        it sends a termination packet with flag = 2 and both server and client
        goes into the termination procedure
        Since client wont send an ACK for an ACK, server just sends 2 identical 
        ACK packets in case of packet loss and terminates
    */
    char buffer[16];
    bzero(buffer,16);
    buffer[15] = (unsigned char)2;
    if(sendto(actualArgs.sock,buffer, 16, 0, actualArgs.client, *actualArgs.client_length) < 1)
        error("sendto error");
    if(sendto(actualArgs.sock,buffer, 16, 0, actualArgs.client, *actualArgs.client_length) < 1)
        error("sendto error");
    *actualArgs.quit_flag = true;

    pthread_exit(NULL);
}
/*
    A bool is used to track the arrival of ACK packets
    When a ACK packet arrives, following function sets the
    ACK change flag to true and on the contrary, when it
    is used in the send thread it sets ACK change flag to false
*/
void recieve_ack(unsigned char ack_seq, unsigned char *lastAck, bool* lastAckChangeFlag){
    *lastAck = ack_seq;
    *lastAckChangeFlag = true;
}
/*
    A function that sends the ACK packet with provided
    sequence number and setting the appropriate flag
*/
void send_ack(recvThreadArgs* args, unsigned char sequence_number){
    char buffer[16];
    bzero(buffer,16);
    buffer[14] = sequence_number;
    buffer[15] = (unsigned char)1;
    if(sendto(args->sock,buffer, 16, 0, args->client, *args->client_length) < 1)
        error("sendto error");
}
/*
    The thread that controlls the sending procedure 
    14 byte is used for message, 15th byte is used 
    for sequence_number and 16th byte is used for flags
    ( 0 : message to be printed on stdout)
    ( 1 : an ack packet)
    ( 2 : termination flag)
*/
void *sendThread(void *args){
    sendThreadArgs actualArgs = *(sendThreadArgs*)args;

    unsigned char send_base = 0;
    unsigned char next_seqnum = 0;
    unsigned char sending_window = 8;
    msec_timer timerr;
    timerr.stop_timer();
    while(!(*actualArgs.shutdown)){
        timerr.start_timer();
        while(next_seqnum != *actualArgs.lastSeqNum || timerr.isActive()){
            if((next_seqnum != (send_base + sending_window)) && (next_seqnum != *actualArgs.lastSeqNum)){
                send_seq((sendThreadArgs*)args, next_seqnum);
                next_seqnum++;
            }
            if(*actualArgs.lastAckChange){
                *actualArgs.lastAckChange = false;
                send_base = *actualArgs.lastAck +1;
                if(send_base == next_seqnum){
                    timerr.stop_timer();
                }
                else{
                    timerr.start_timer();
                }
            }
            if(timerr.timeout(180)){
                timerr.start_timer();
                for(unsigned char i = send_base; i != next_seqnum; i++){
                    send_seq((sendThreadArgs*)args, i);
                }
            }
        }   
    }
    /*
        When program recieves the termination signal from stdin
        it notifies the client with termination packets with flag=2
        In case of packet loss of all ACK packets from client,
        server tries to send the same termination packet three times 
        in a row while checking ACK packets in recieve thread
        if there is no response from the client or recieve thread
        gives quit signal with quit_flag program terminates
    */
    char buffer[16];
    bzero(buffer,16);
    buffer[15] = (unsigned char)2;

    for(int i = 0; i < 3; i++){
        if(sendto(actualArgs.sock, buffer, 16, 0, actualArgs.client, *actualArgs.client_length) < 1)
            error("sendto error");
        sleep(1);
        if(*actualArgs.quit_flag == true)
            break;
    }
    *actualArgs.quit_flag = true;
    pthread_exit(NULL);
}

/*
    A function that sends the message packet with provided
    sequence number and setting the appropriate flag
*/
void send_seq(sendThreadArgs* args, unsigned char sequence_number){

    (args->sendBuffer+(16*sequence_number))[14] = sequence_number;
    (args->sendBuffer+(16*sequence_number))[15] = 0;

    if(sendto(args->sock, args->sendBuffer+(16*sequence_number), 16, 0, args->client, *args->client_length) < 1)
        error("sendto error");
}

/*
    The third thread is stdin thread
    This thread keeps checking the stdin for inputs
    and formats them accordingly into sendBuffer which
    is used to send packets and to keep track of sequence
    numbers of packets which are used by go-back-N algorithm
*/
void *stdinThread(void *args){

    stdinThreadArgs actualArgs = *(stdinThreadArgs*)args;

    int newline_count = 0;

    char str[14] = "\n";

    while(!(*actualArgs.shutdown)){

        bzero(actualArgs.stdinBuffer,1024);
        fgets(actualArgs.stdinBuffer,1024,stdin);

        int outerIteration = ((strlen(actualArgs.stdinBuffer) / 14) + 1) % 1024;

        for(int i = 0; i< outerIteration; i++){
            for(int j = 0; j < 14; j++){
                actualArgs.sendBuffer[((*actualArgs.lastSeqNum)%256)*16 + j] = actualArgs.stdinBuffer[i*14 + j];
            }
            if(strcmp(str, actualArgs.stdinBuffer+(i*14)) == 0){
                newline_count++;
            }
            else{
                newline_count = 0;
            }
            if(newline_count == 2){
            fprintf(stderr,"/*-------------------*\\\n");
            fprintf(stderr,"TERMINATING CONNECTION\n");
            fprintf(stderr,"\\*-------------------*/\n");
                *actualArgs.termination_output_flag = true;
                *actualArgs.shutdown = true;
            }
            *actualArgs.inputFlag = true;
            (*actualArgs.lastSeqNum)++;
        }
    }
    pthread_exit(NULL);
}

// An error function to display error to stderr
void error(string msg){
    fprintf(stderr,"%s\n",msg.c_str());
    exit(0);
}

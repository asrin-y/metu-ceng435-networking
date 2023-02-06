/*
    The implementation of client application is almost identical to
    server application. If you havent read the server.cpp file, I recommend
    you to doing so before examining this.

    One of minor differences is at ln 130
*/

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


class recvThreadArgs{
    public:
        int sock;
        struct sockaddr* server;
        socklen_t* server_length;
        unsigned char *lastAck;
        bool *lastAckChange;
        bool connection_established;
        bool *shutdown;
        bool *quit_flag;
};
class sendThreadArgs{
    public:
        int sock;
        struct sockaddr* server;
        socklen_t* server_length;
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

    if(argc != 3)
        error("usage: ./client <server-ip-address> <server-port-number>");

    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if(sock<1)
        error("socket opening error");

    struct sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[2]));
    server.sin_addr.s_addr = inet_addr(argv[1]);

    socklen_t server_length = sizeof(struct sockaddr_in);

    recvThreadArgs* argsRecv = new recvThreadArgs;
    sendThreadArgs* argsSend = new sendThreadArgs;
    stdinThreadArgs* argsStdin = new stdinThreadArgs;

    char stdinBuffer[1024], sendBuffer[4096];
    bzero(sendBuffer,4096);
    /*
        This lastSeqNum is the upper threshold for sending packets
        The sequence number of packets cannot exceed lastSeqNum
        It is controlled by stdin thread and incremented when user
        gives input to stdin
        lastSeqNum's initial value should be zero to wait for stdin
        and not send packet as it is in server.cpp.
        But in client, an initial packet has to be sent to ensure that
        after the connection, both parties can send the first chat message
    */
    unsigned char  lastSeqNum = 1;
    unsigned char lastAck = 0;
    bool lastAckChange = false;
    bool inputFlag = false;
    bool shutdown = false;
    bool quit_flag = false;
    bool termination_output_flag = false;

    argsRecv->sock = sock;
    argsRecv->server = (struct sockaddr*)&server;
    argsRecv->server_length = (socklen_t*)&server_length;
    argsRecv->lastAck = &lastAck;
    argsRecv->lastAckChange = &lastAckChange;
    argsRecv->connection_established = false;
    argsRecv->shutdown = &shutdown;
    argsRecv->quit_flag = &quit_flag;

    argsSend->sock = sock;
    argsSend->server = (struct sockaddr*)&server;
    argsSend->server_length = (socklen_t*)&server_length;
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

    pthread_t threadNo[3];

    pthread_create(&threadNo[0], NULL, recvThread,argsRecv);
    
    pthread_create(&threadNo[1], NULL, sendThread,argsSend);

    pthread_create(&threadNo[2], NULL, stdinThread,argsStdin);

    while(!quit_flag){;}

    if(termination_output_flag == false){
        fprintf(stderr,"/*-------------------*\\\n");
        fprintf(stderr,"TERMINATING CONNECTION\n");
        fprintf(stderr,"\\*-------------------*/\n"); 
    }
    free(argsRecv);
    free(argsSend);
    free(argsStdin);

    return 0;

}

void *recvThread(void *args){

    recvThreadArgs actualArgs = *(recvThreadArgs*)args;
    char recieveBuffer[16];

    unsigned char next_seqnum = 0;

    while(1){
        if(actualArgs.connection_established == false && *actualArgs.lastAckChange == true){
            fprintf(stderr,"/*-------------------*\\\n");
            fprintf(stderr,"CONNNECTION ESTABLISHED\n");
            fprintf(stderr,"\\*-------------------*/\n");
            actualArgs.connection_established = true;
        }
        if(recvfrom(actualArgs.sock, recieveBuffer, 16, 0, actualArgs.server, actualArgs.server_length) < 1)
                error("recvfrom error");

        if((unsigned char)recieveBuffer[15] == (unsigned char)2){
            *actualArgs.shutdown = true;
            break;
        }        
        if((unsigned char)recieveBuffer[15] == (unsigned char)1){
            recieve_ack(recieveBuffer[14],actualArgs.lastAck,actualArgs.lastAckChange);
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
    char buffer[16];
    bzero(buffer,16);
    buffer[15] = (unsigned char)2;
    if(sendto(actualArgs.sock,buffer, 16, 0, actualArgs.server, *actualArgs.server_length) < 1)
        error("sendto error");
    if(sendto(actualArgs.sock,buffer, 16, 0, actualArgs.server, *actualArgs.server_length) < 1)
        error("sendto error");
    *actualArgs.quit_flag = true;

    pthread_exit(NULL);
}

void recieve_ack(unsigned char ack_seq, unsigned char *lastAck, bool* lastAckChangeFlag){
    *lastAck = ack_seq;
    *lastAckChangeFlag = true;
}

void send_ack(recvThreadArgs* args, unsigned char sequence_number){
    char buffer[16];
    bzero(buffer,16);
    buffer[14] = sequence_number;
    buffer[15] = (unsigned char)1;
    if(sendto(args->sock,buffer, 16, 0, args->server, *args->server_length) < 1)
        error("sendto error");
}

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

    char buffer[16];
    bzero(buffer,16);
    buffer[15] = (unsigned char)2;

    for(int i = 0; i < 3; i++){
        if(sendto(actualArgs.sock, buffer, 16, 0, actualArgs.server, *actualArgs.server_length) < 1)
            error("sendto error");
        sleep(1);
        if(*actualArgs.quit_flag == true)
            break;
    }
    *actualArgs.quit_flag = true;
    pthread_exit(NULL);
}

void send_seq(sendThreadArgs* args, unsigned char sequence_number){

    (args->sendBuffer+(16*sequence_number))[14] = sequence_number;
    (args->sendBuffer+(16*sequence_number))[15] = 0;

    if(sendto(args->sock, args->sendBuffer+(16*sequence_number), 16, 0, args->server, *args->server_length) < 1)
        error("sendto error");
}

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

void error(string msg){
    fprintf(stderr,"%s\n",msg.c_str());
    exit(0);
}
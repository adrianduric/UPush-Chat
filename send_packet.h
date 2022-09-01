#ifndef SEND_PACKET_H
#define SEND_PACKET_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <arpa/inet.h>

#define TRUE 1
#define FALSE 0

#define MAKS_KLIENTER 100
#define MAKS_TEKST 1400
#define BUFSIZE_KLIENT (MAKS_TEKST + 100)
#define MAKS_BYTES_PER_NICK 20
#define ORD_PER_PKT_LOOKUP 8
#define ORD_PER_PKT_MELDING 7
#define BUFSIZE_SERVER 100
#define ORD_PER_PKT_SERVER 4
#define HJERTESLAG_INTERVALL 10
#define HJERTESLAG_TIDSGRENSE 30

/* This function is used to set the probability (a value between 0 and 1) for
 * dropping a packet in the send_packet function. You call set_loss_probability
 * once in your program, and send_packet will drop packets after that.
 */
void set_loss_probability( float x );

/* This is a lossy replacement for the sendto function. It uses a random
 * number generator to drop packets with the probability chosen with
 * set_loss_probability. If it doesn't drop the packet, it calls sendto.
 */
ssize_t send_packet( int sock, void* buffer, size_t size, int flags, struct sockaddr* addr, socklen_t addrlen );




// ***************   NB! EGENDEFINERTE STRUCTS OG FUNKSJONER FØLGER:   ******************

struct melding
{
    char *pakke; 
    int seq_num;
    char *msg_type;
    struct klient *klient;
    struct timeval time_recieved;
    char timeout_forsok;
    char aktiv;
    char har_gjort_oppslag;
    int oppslag_seq_num;
    struct melding *neste;
    struct melding *forrige;
};

struct klient
{
    char *nick; //Maks 20B
    char *ip; //Lagres i strengformat
    uint16_t port; //Lagres i host byte order
    struct klient *neste;
    struct klient *forrige;
    char last_seq_num;
    time_t last_heartbeat;
    char is_blocked;
    struct melding *melding_hode;
    struct melding *melding_hale;
};

//FUNKSJONER FOR MELDINGER

void set_between_meldinger(struct melding *venstre, struct melding *midt, struct melding *hoyre);

struct melding *push_melding(char *pakke, char timeout_forsok, struct klient *klient);

void free_melding(struct melding *melding);

void remove_melding(struct melding *melding);

struct melding *search_melding(int seq_num, struct klient *hode, struct klient *hale); 

void remove_melding_oppslag(struct melding *melding, char vellykket_oppslag, struct klient *hode, struct klient *hale);

char *pop_melding(struct klient *klient);

void free_meldinger(struct klient *klient);

//FUNKSJONER FOR KLIENTER

void set_between_klienter(struct klient *venstre, struct klient *midt, struct klient *hoyre);

struct klient *push_klient(char *nick, char *ip, in_port_t port, struct klient *hode);

void free_klient(struct klient *klient);

void remove_klient(struct klient *klient);

char *pop_klient(struct klient *hode);

void print_klienter(struct klient *hode, struct klient *hale);

struct klient *search_klient(char *nick, struct klient *hode, struct klient *hale); 

void update_klient(struct klient *klient, char *ip, uint16_t port); 

void free_klienter(struct klient *hode, struct klient *hale);

//DIVERSE FUNKSJONER

void check_error(int res, char *msg);

void get_string(char buf[], int size);

void quit_main(int fd, int exit_success, struct klient *hode, struct klient *hale);

//FUNKSJONER FOR SENDING OG HÅNDTERING AV MELDINGER

void send_melding(struct melding *melding, struct klient *mottaker, int socket_fd, struct klient *hode, struct klient *hale);

void klient_hjerteslag(char *min_nick, int seq_num, struct klient *server, int socket_fd,
    struct klient *hode, struct klient *hale);

void klient_oppslag(char *annen_nick, int seq_num, int oppslag_seq_num, struct klient *server, int socket_fd,
    struct klient *hode, struct klient *hale);

void klient_melding(char *min_nick, int seq_num, char *msg, struct klient *mottaker, char har_gjort_oppslag, 
    char antall_forsok, int socket_fd, struct klient *hode, struct klient *hale);

#endif /* SEND_PACKET_H */

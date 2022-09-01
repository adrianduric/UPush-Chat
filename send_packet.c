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

#include "send_packet.h"

static float loss_probability = 0.0f;

void set_loss_probability( float x )
{
    loss_probability = x;
}

ssize_t send_packet( int sock, void* buffer, size_t size, int flags, struct sockaddr* addr, socklen_t addrlen )
{
    float rnd = drand48();

    if( rnd < loss_probability)
    {
        fprintf(stderr, "Randomly dropping a packet\n");
        return size;
    }

    return sendto( sock,
                   buffer,
                   size,
                   flags,
                   addr,
                   addrlen );
}

// ***************NB! EGENDEFINERTE FUNKSJONER FØLGER:******************


//FUNKSJONER FOR MELDINGER

void set_between_meldinger(struct melding *venstre, struct melding *midt, struct melding *hoyre) 
{
    venstre->neste = midt;
    midt->forrige = venstre;
    midt->neste = hoyre;
    hoyre->forrige = midt;
}

struct melding *push_melding(char *pakke, char timeout_forsok, struct klient *klient)
{
    struct melding* melding = malloc(sizeof(struct melding));
    if (melding == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    melding->pakke = strdup(pakke);
    if (melding->pakke == NULL)
    {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    char msg_type_temp[strlen(pakke)]; 
    strcpy(msg_type_temp, pakke); //Kopierer til nytt minneområde siden nullbytes vil settes med strtok

    strtok(msg_type_temp, " "); //Utfører strtok for å komme frem til tredje ord
    melding->seq_num = atoi(strtok(NULL, " ")); //Det andre ordet er seq_num
    melding->msg_type = strdup(strtok(NULL, " ")); //Det tredje ordet brukes for å identifisere msg_type

    melding->klient = klient;

    int rc = gettimeofday(&melding->time_recieved, NULL);
    check_error(rc, "gettimeofday");
    melding->timeout_forsok = timeout_forsok;
    melding->aktiv = 1;
    melding->har_gjort_oppslag = 0;
    melding->oppslag_seq_num = 0;

    set_between_meldinger(klient->melding_hode, melding, klient->melding_hode->neste);

    return melding;
}

void free_melding(struct melding *melding)
{
    free(melding->pakke);
    free(melding->msg_type);
    free(melding);
}

void remove_melding(struct melding *melding) 
{
    struct melding *forrige = melding->forrige;
    struct melding *neste = melding->neste;
    forrige->neste = neste;
    neste->forrige = forrige;
    free_melding(melding);
}

struct melding *search_melding(int seq_num, struct klient *hode, struct klient *hale)
{
    struct melding *funnet_melding = NULL;
    char melding_funnet = 0;

    struct klient *klient_temp = hode->neste;
    while (klient_temp != hale) //For hver klient som er lagret lokalt:
    {
        struct melding *melding_temp = klient_temp->melding_hode->neste;
        while (melding_temp != klient_temp->melding_hale) //For hver ubesvarte melding til klienten:
        {
            if (melding_temp->seq_num == seq_num) //Hvis dette er tilhørende melding til ACK'en:
            {
                funnet_melding = melding_temp; //Lagrer meldingen
                melding_funnet++; //Setter flagg for funnet melding
                break; //Avslutter søket
            }
            melding_temp = melding_temp->neste;
        }
        if (melding_funnet) //Hvis meldingen har blitt funnet:
            break; //Avslutter søket

        //Går videre til neste klient
        klient_temp = klient_temp->neste;
    }

    return funnet_melding;
}

void remove_melding_oppslag(struct melding *melding, char vellykket_oppslag, struct klient *hode, struct klient *hale) 
{
    struct melding *forrige = melding->forrige;
    struct melding *neste = melding->neste;
    forrige->neste = neste;
    neste->forrige = forrige;

    struct melding *funnet_melding = search_melding(melding->oppslag_seq_num, hode, hale);

    free_melding(melding);

    if (funnet_melding != NULL) //Hvis det fins en tilknyttet melding:
    {
        if (vellykket_oppslag) //Hvis oppslaget lyktes:
        {
            //Siden oppslaget lyktes, skal alle meldingene tilknyttet klienten gjøres aktive
            struct melding *temp = funnet_melding->klient->melding_hode;
            while (temp != funnet_melding->klient->melding_hale)
            {
                int rc = gettimeofday(&temp->time_recieved, NULL);
                check_error(rc, "gettimeofday");
                temp->har_gjort_oppslag = 1;
                temp->aktiv = 1;
                temp->timeout_forsok = 2; //Gir 2 nye forsøk til den opprinnelige meldingen

                temp = temp->neste;
            }
        }
        else //Hvis oppslaget mislyktes:
        {
            //Siden klienten ikke finnes hos serveren, kan den slettes med sine meldinger
            remove_klient(funnet_melding->klient); //Slett den tilknyttede klienten
        }
    }
}

char *pop_melding(struct klient *klient) 
{
    char *pakke = klient->melding_hode->neste->pakke;
    remove_melding(klient->melding_hode->neste);
    return pakke;
}

void free_meldinger(struct klient *klient) 
{
    struct melding *temp = klient->melding_hode->neste;
    while (temp != klient->melding_hale)
    {
        struct melding *forrige = temp;
        temp = temp->neste;        
        free_melding(forrige);
    }
}

//FUNKSJONER FOR KLIENTER

void set_between_klienter(struct klient *venstre, struct klient *midt, struct klient *hoyre) 
{
    venstre->neste = midt;
    midt->forrige = venstre;
    midt->neste = hoyre;
    hoyre->forrige = midt;
}

struct klient *push_klient(char *nick, char *ip, in_port_t port, struct klient *hode)
{
    struct klient* klient = malloc(sizeof(struct klient));
    if (klient == NULL)
    {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    klient->nick = strdup(nick);
    if (klient->nick == NULL)
    {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    klient->ip = strdup(ip);
    if (klient->ip == NULL)
    {
        perror("strdup");
        exit(EXIT_FAILURE);
    }

    klient->port = port;
    klient->last_seq_num = 0;
    klient->last_heartbeat = time(NULL);
    check_error(klient->last_heartbeat, "time");
    klient->is_blocked = FALSE;

    klient->melding_hode = malloc(sizeof(struct melding));
    klient->melding_hale = malloc(sizeof(struct melding));

    klient->melding_hode->neste = klient->melding_hale;
    klient->melding_hale->forrige = klient->melding_hode;
    
    set_between_klienter(hode, klient, hode->neste);

    return klient;
}

void free_klient(struct klient *klient)
{
    free(klient->nick);
    free(klient->ip);

    free_meldinger(klient);
    free(klient->melding_hode);
    free(klient->melding_hale);

    free(klient);
}

void remove_klient(struct klient *klient) 
{
    struct klient *forrige = klient->forrige;
    struct klient *neste = klient->neste;
    forrige->neste = neste;
    neste->forrige = forrige;
    free_klient(klient);
}

char *pop_klient(struct klient *hode) 
{
    char *nick = hode->neste->nick;
    remove_klient(hode->neste);
    return nick;
}

void print_klienter(struct klient *hode, struct klient *hale) 
{
    struct klient *temp = hode->neste;
    while (temp != hale)
    {
        printf("%s\n", temp->nick);
        temp = temp->neste;
    }
}

struct klient *search_klient(char *nick, struct klient *hode, struct klient *hale) 
{
    struct klient *klient = NULL;
    struct klient *temp = hode->neste;
    while (temp != hale)
    {
        if (strcmp(nick, temp->nick) == 0)
        {
            klient = temp;
            break;
        }
        temp = temp->neste;
    }
    
    return klient; 
}

void update_klient(struct klient *klient, char *ip, uint16_t port)
{
    strcpy(klient->ip, ip);
    klient->port = port;
    klient->last_heartbeat = time(NULL);
    check_error(klient->last_heartbeat, "time");
}

void free_klienter(struct klient *hode, struct klient *hale) 
{
    struct klient *temp = hode->neste;
    while (temp != hale)
    {
        struct klient *forrige = temp;
        temp = temp->neste;        
        free_klient(forrige);
    }
}

//DIVERSE FUNKSJONER

void check_error(int res, char *msg)
{
    if (res == -1)
    {
        perror(msg);
        exit(EXIT_FAILURE);
    }
}

void get_string(char buf[], int size)
{
    char c;

    fgets(buf, size, stdin);
    if (buf[strlen(buf) - 1] == '\n')
    {
        buf[strlen(buf) - 1] = 0;
    }
    else
    {
        while((c = getchar()) != '\n' && c != EOF);
    }
}

void quit_main(int fd, int exit_success, struct klient *hode, struct klient *hale)
{
    free_klienter(hode, hale);
    free(hode);
    free(hale);
    close(fd);
    if (exit_success)
        exit(EXIT_SUCCESS);
    else
        exit(EXIT_FAILURE);
}

//FUNKSJONER FOR SENDING OG HÅNDTERING AV MELDINGER

void send_melding(struct melding *melding, struct klient *mottaker, int socket_fd, struct klient *hode, struct klient *hale)
{
    int wc;

    struct sockaddr_in dest_addr;
    struct in_addr ip_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    //Henter fram melding som skal sendes
    char *pakke = melding->pakke;

    //Forbereder sending av pakke
    wc = inet_pton(AF_INET, mottaker->ip, &ip_addr.s_addr);
    check_error(wc, "inet_pton");
    if (!wc)
    {
        fprintf(stderr, "Ugyldig IP-adresse: %s\n", mottaker->ip);
        quit_main(socket_fd, EXIT_FAILURE, hode, hale);
    } 

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(mottaker->port);
    dest_addr.sin_addr = ip_addr;

    //Søker hos serveren etter brukeren med angitt nick
    wc = send_packet(socket_fd, pakke, strlen(pakke), 0,
        (struct sockaddr *) &dest_addr, addr_len);
    check_error(wc, "send_packet");
}

void klient_hjerteslag(char *min_nick, int seq_num, struct klient *server, int socket_fd,
    struct klient *hode, struct klient *hale)
{
    //Oppretter melding
    char reg[strlen("PKT  REG ") + strlen(min_nick) + sizeof(seq_num)];
    sprintf(reg, "PKT %d REG %s", seq_num, min_nick);
    struct melding *melding = push_melding(reg, 1, server); //1 forsøk basert på oppgaveteksten

    //Sender melding
    send_melding(melding, server, socket_fd, hode, hale);
}

void klient_oppslag(char *annen_nick, int seq_num, int oppslag_seq_num, struct klient *server, int socket_fd,
    struct klient *hode, struct klient *hale)
{
    //Oppretter melding
    char oppslag[strlen("PKT  LOOKUP ") + strlen(annen_nick) + sizeof(seq_num)];
    sprintf(oppslag, "PKT %d LOOKUP %s", seq_num, annen_nick);
    struct melding *melding = push_melding(oppslag, 3, server); //3 forsøk basert på oppgaveteksten

    //Dersom oppslaget utføres som en del av en meldingsprosedyre, husker oppslagsmeldingen hvilken
    //opprinnelige melding den tilhører, slik at denne kan "reaktiveres" når oppslaget gir resultat
    melding->oppslag_seq_num = oppslag_seq_num;

    //Sender melding
    send_melding(melding, server, socket_fd, hode, hale);
}

void klient_melding(char *min_nick, int seq_num, char *msg, struct klient *mottaker, char har_gjort_oppslag, 
    char antall_forsok, int socket_fd, struct klient *hode, struct klient *hale)
{
    //Oppretter melding
    char pkt[strlen("PKT  FROM  TO  MSG ") + sizeof(seq_num) + strlen(min_nick)
    + strlen(mottaker->nick) + strlen(msg)]; 
    sprintf(pkt, "PKT %d FROM %s TO %s MSG %s", seq_num, min_nick, mottaker->nick, msg);

    //Enten 2 eller 0 forsøk blir gitt avhengig av om det skal lede til umiddelbart oppslag eller ikke
    struct melding *melding = push_melding(pkt, antall_forsok, mottaker); 
    melding->har_gjort_oppslag = har_gjort_oppslag;

    //Sender melding
    send_melding(melding, mottaker, socket_fd, hode, hale);
}
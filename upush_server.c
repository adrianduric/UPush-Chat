#include "send_packet.h"

int main(int argc, char *argv[])
{
    //Sjekker antall argumenter ved kjøring av programmet
    if (argc != 3)
    {
        printf("Riktig syntaks: %s <port> <tapssannsynlighet>\n", argv[0]);
        return EXIT_SUCCESS; 
    }

    printf("\n****************************************************************************\n");
    printf("***************************     UPush-server     ***************************\n");
    printf("****************************************************************************\n\n");

    //Oppretter liste for å holde alle klienter
    struct klient *hode = malloc(sizeof(struct klient));
    struct klient *hale = malloc(sizeof(struct klient));
    
    hode->neste = hale;
    hale->forrige = hode;

    int socket_fd, rc, wc;
    char buf[BUFSIZE_SERVER];
    struct sockaddr_in my_addr;
    struct sockaddr_in src_addr;
    socklen_t addr_len = sizeof(struct sockaddr_in);

    //Leser ut parametere fra kommandolinjen
    int portnr = atoi(argv[1]);
    float tapssannsynlighet = atof(argv[2]);
    tapssannsynlighet = tapssannsynlighet / 100;
    
    //Setter static float-en for sannsynlighet til samme verdi som angitt
    time_t sekunder;
    sekunder = time(NULL);
    check_error(sekunder, "time");
    srand48(sekunder); //Brukes fordi drand48 ikke fungerer alene
    set_loss_probability(tapssannsynlighet);

    //Oppretter socket og binder den til en adresse
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    check_error(socket_fd, "socket");

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(portnr);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    rc = bind(socket_fd, (struct sockaddr *) &my_addr, sizeof(struct sockaddr_in));
    check_error(rc, "bind");
    
    //Oppretter tidsvariabel for hjerteslag-meldinger:
    time_t curr_time;

    //Oppretter fd_set for å kunne lytte på tastatur for "QUIT"
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(socket_fd, &fds); 

    while (TRUE) //Serveren skal lytte evig
    {   
        FD_ZERO(&fds);
        FD_SET(socket_fd, &fds);  
        FD_SET(STDIN_FILENO, &fds);

        rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
        check_error(rc, "select");

        if (FD_ISSET(STDIN_FILENO, &fds)) //Hvis melding fra tastaturet:
        {
            //Leser meldingen inn i bufferet
            get_string(buf, BUFSIZE_KLIENT); 
            char input[strlen(buf)];
            strcpy(input, buf);

            if (strcmp(input, "QUIT") == 0) //Hvis programmet skal avsluttes:
            {
                quit_main(socket_fd, EXIT_SUCCESS, hode, hale);
            }
        }

        else if (FD_ISSET(socket_fd, &fds)) //Hvis input fra socket:
        {

            //Steg å gjennomføre idet en pakke ankommer
                //1: Leser meldingen inn i et buffer
                //2: Sjekker om meldingen er Registrering eller Oppslag
                //Dersom Registrering:
                    //3a: Søk etter eksisterende / opprett ny klient, og lagre i minnet
                //Dersom Oppslag:
                    //3b: Søk etter klient i minnet, og returner data hvis funnet

            //1:
            rc = recvfrom(socket_fd, buf, BUFSIZE_SERVER - 1, 0, (struct sockaddr *) &src_addr, &addr_len);
            check_error(rc, "recvfrom");
            buf[rc] = '\0';
            
            curr_time = time(NULL); //Lagrer når melding blir mottatt
            check_error(curr_time, "time");

            struct in_addr klient_ip_struct = src_addr.sin_addr;
            char klient_ip[(int) addr_len];
            const char *error_check = inet_ntop(AF_INET, &klient_ip_struct, klient_ip, addr_len);
            if (error_check == NULL)
            {
                perror("inet_ntop");
                quit_main(socket_fd, EXIT_FAILURE, hode, hale);
            }

            //2:
            char *msg_parts[ORD_PER_PKT_SERVER];
            int input_count = 0;

            char *token = strtok(buf, " ");
            while (token != NULL)
            {
                msg_parts[input_count] = token;
                printf("%s ", msg_parts[input_count]);
                input_count++;
                token = strtok(NULL, " ");
            }
            printf("\n");

            //Alle meldinger til serveren fra klienter skal inneholde 4 ord ved riktig syntaks
            if (input_count != 4) //Hvis feil mengde ord har blitt sendt i en melding til serveren:
            {
                perror("Mottok ikke 4 ord i meldingen");
                quit_main(socket_fd, EXIT_FAILURE, hode, hale);
            }

            int seq_num = atoi(msg_parts[1]);
            char *msg_type = msg_parts[2];
            char nick[20] = { 0 };
            strncpy(nick, msg_parts[3], MAKS_BYTES_PER_NICK); 
            nick[19] = '\0';

            if (strcmp(msg_type, "REG") == 0) //Hvis Registrering:
            {
                //3a
                //Sjekker først om klient allerede eksisterer på serveren

                struct klient *funnet_klient = search_klient(nick, hode, hale);
                if (funnet_klient == NULL) //Hvis klienten ikke finnes fra før:
                    push_klient(nick, klient_ip, ntohs(src_addr.sin_port), hode);

                else //Hvis klienten allerede er lagret:
                    update_klient(funnet_klient, klient_ip, ntohs(src_addr.sin_port));

                //Sender ACK-melding tilbake:
                char ack[strlen("ACK  OK") + sizeof(seq_num)];
                sprintf(ack, "ACK %d OK", seq_num);

                wc = send_packet(socket_fd, ack, strlen(ack), 0,
                    (struct sockaddr *) &src_addr, addr_len);
                check_error(wc, "send_packet");
            }

            else if (strcmp(msg_type, "LOOKUP") == 0) //Hvis Oppslag:
            {
                //3b
                //Sjekker først om klient allerede eksisterer på serveren

                struct klient *funnet_klient = search_klient(nick, hode, hale);

                if (funnet_klient == NULL) //Hvis klienten ikke er lagret:
                {
                    //Sender ACK-melding tilbake:
                    char ack[strlen("ACK  NOT FOUND") + sizeof(seq_num)];
                    sprintf(ack, "ACK %d NOT FOUND", seq_num);

                    wc = send_packet(socket_fd, ack, strlen(ack), 0,
                        (struct sockaddr *) &src_addr, addr_len);
                    check_error(wc, "send_packet");
                }

                else //Hvis klienten er lagret:
                {
                    //Sjekker om klienten er en foreldet oppføring:
                    if ((curr_time - funnet_klient->last_heartbeat) > HJERTESLAG_TIDSGRENSE)
                    {
                        //Sender ACK-melding tilbake:
                        char ack[strlen("ACK  NOT FOUND") + sizeof(seq_num)];
                        sprintf(ack, "ACK %d NOT FOUND", seq_num);

                        wc = send_packet(socket_fd, ack, strlen(ack), 0,
                            (struct sockaddr *) &src_addr, addr_len);
                        check_error(wc, "send_packet");

                        //Sletter foreldet oppføring
                        fprintf(stdout, "Sletter foreldet oppføring med brukernavn %s.\n", funnet_klient->nick);
                        remove_klient(funnet_klient);
                    }

                    else //Klienten er gyldig, og kan sendes som oppslagsresultat
                    {
                        char ack[strlen("ACK  NICK  IP  PORT ") + sizeof(seq_num) + strlen(nick)
                            + strlen(funnet_klient->ip) + sizeof(funnet_klient->port)];
                        sprintf(ack, "ACK %d NICK %s IP %s PORT %d", seq_num, nick,
                            funnet_klient->ip, funnet_klient->port);

                        wc = send_packet(socket_fd, ack, strlen(ack), 0,
                                (struct sockaddr *) &src_addr, addr_len);
                            check_error(wc, "send_packet");
                    }
                }
            } 
        }
    }

    perror("Feil oppdaget: Utenfor hovedhendelseløkken");
    quit_main(socket_fd, EXIT_FAILURE, hode, hale);
}
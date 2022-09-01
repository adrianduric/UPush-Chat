#include "send_packet.h"

int main(int argc, char *argv[])
{
    //Sjekker antall argumenter ved kjøring av programmet
    if (argc != 6)
    {
        printf("Riktig syntaks: %s <nick> <adresse> <port> <timeout> <tapssannsynlighet>\n", argv[0]);
        return EXIT_SUCCESS; 
    }

    int socket_fd, rc, wc;
    fd_set fds;
    char buf[BUFSIZE_KLIENT];
    socklen_t addr_len = sizeof(struct sockaddr_in);
    int seq_num = 0;
    time_t last_heartbeat, time_check;
    
    //Leser ut parametere fra kommandolinjen
    char *min_nick = argv[1];
    char *adresse = argv[2];
    int portnr = atoi(argv[3]);
    float timeout_input = (float) atof(argv[4]);
    float tapssannsynlighet = (float) atof(argv[5]);
    tapssannsynlighet = tapssannsynlighet / 100;

    //Lager struct timeval for brukerdefinert timeout
    long timeout_sek = (long) timeout_input;
    long timeout_usek = (long) ((timeout_input - timeout_sek) * 1000000);

    struct timeval timeout;
    timeout.tv_sec = timeout_sek;
    timeout.tv_usec = timeout_usek;

    //Sjekker at nick følger regler for lengde og karakterer
    if (strlen(min_nick) > MAKS_BYTES_PER_NICK)
    {
        printf("For langt nick. Angi nick med maksimalt 20 tegn.");
        return EXIT_SUCCESS;
    }
    for (int i = 0; i < (int) strlen(min_nick); i++)
    {
        if (!(isalnum(min_nick[i])))
        {
            printf("nick kan bare bestå av alfanumeriske tegn (ikke whitespace)");
            return EXIT_SUCCESS;
        }
    }

    //Oppretter liste for å holde alle klienter
    struct klient *hode = malloc(sizeof(struct klient));
    struct klient *hale = malloc(sizeof(struct klient));

    hode->neste = hale;
    hale->forrige = hode;
    
    //Setter static float-en for sannsynlighet til samme verdi som angitt
    time_t sekunder;
    sekunder = time(NULL);
    check_error(sekunder, "time");
    srand48(sekunder); //Brukes fordi drand48 ikke fungerer alene
    set_loss_probability(tapssannsynlighet);

    //Initialiserer variable som må være med i sendto()
    struct sockaddr_in server_addr;
    struct sockaddr_in dest_addr;
    struct in_addr ip_addr;

    //Oppretter socket
    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    check_error(socket_fd, "socket");

    //Fyller in_addr-structen
    wc = inet_pton(AF_INET, adresse, &ip_addr.s_addr);
    check_error(wc, "inet_pton");
    if (!wc)
    {
        fprintf(stderr, "Ugyldig IP-adresse: %s\n", adresse);
        quit_main(socket_fd, EXIT_FAILURE, hode, hale);
    } 

    //Fyller inn constaddr_in-structen
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(portnr);
    server_addr.sin_addr = ip_addr;

    //Lagrer serveren som klient-struct til senere meldingshåndtering
    struct klient *server = push_klient("SERVER", adresse, portnr, hode);

    //Konstruerer og sender REG-melding til serveren
    strcpy(buf, "PKT 0 REG ");
    strcat(buf, min_nick);

    wc = send_packet(socket_fd, buf, strlen(buf), 0, (struct sockaddr *) &server_addr, addr_len);
    check_error(wc, "send_packet");

    memset(buf, 0, BUFSIZE_KLIENT);

    //Venter på melding fra serveren eller timeout
    FD_ZERO(&fds);
    FD_SET(socket_fd, &fds);   

    rc = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
    check_error(rc, "select");

    if (rc == 0) //Hvis det ikke har blitt mottatt noe svar:
    {
        perror("Timeout: fikk ikke respons");
        quit_main(socket_fd, EXIT_FAILURE, hode, hale);
    }

    //Hvis det mottas svar: les meldingen, og sjekk at REG er godkjent
    rc = read(socket_fd, buf, BUFSIZE_KLIENT - 1); 
    check_error(rc, "read");
    buf[rc] = 0;

    last_heartbeat = time(NULL);
    check_error(last_heartbeat, "time");

    if (strcmp(buf, "ACK 0 OK") != 0)
    {
        perror("Feil oppstod: ikke registrert hos server");
        quit_main(socket_fd, EXIT_FAILURE, hode, hale);
    }

    seq_num++;

    //Tittel og instrukser til brukeren
    printf("\n****************************************************************************\n");
    printf("***************************     UPush-klient     ***************************\n");
    printf("****************************************************************************\n\n");
    printf("Bruk følgende input:\n");
    printf("'QUIT' for å avslutte klienten.\n");
    printf("'@<nick> <tekst>' for å sende melding (maks 1400 tegn) til brukeren <nick>.\n");
    printf("'BLOCK <nick>' for å blokkere meldinger fra brukeren <nick>.\n");
    printf("'UNBLOCK <nick>' for å fjerne blokkering av meldinger fra brukeren <nick>.\n\n");

    //Lager ny struct timeval for hovedhendelsesløkken, som styrer hvor ofte programmet
    //skal se håndtere sendte meldinger som ikke har mottatt ACK. Denne blir satt til
    //10ms, med en rimelig antagelse om at brukeren ikke vil sette lavere timeout enn det.
    struct timeval intervall;
    intervall.tv_sec = 0;
    intervall.tv_usec = 10000; //10ms

    while (TRUE) //Hovedhendelsesløkken
    {
        FD_ZERO(&fds);
        FD_SET(socket_fd, &fds);  
        FD_SET(STDIN_FILENO, &fds);

        intervall.tv_sec = 0;
        intervall.tv_usec = 10000; 
    
        rc = select(FD_SETSIZE, &fds, NULL, NULL, &intervall);
        check_error(rc, "select");

        if (rc == 0) //Hvis ingen input innen timeout:
        {
            //Sender hjerteslag hvis det er på tide å gjøre dette
            time_check = time(NULL);
            check_error(time_check, "time");
            if ((time_check - last_heartbeat) >= HJERTESLAG_INTERVALL)
            {
                klient_hjerteslag(min_nick, seq_num, server, socket_fd, hode, hale);
                last_heartbeat = time(NULL);
                check_error(last_heartbeat, "time");
                seq_num++;
            }

            //Søker gjennom klienter, og sjekker om de har ubesvarte meldinger; i så fall behandles disse.
            struct klient *klient_temp = hode->neste;
            while (klient_temp != hale) //For hver klient som er lagret lokalt:
            {
                //Leser av nåværende tid for sammenligning
                struct timeval curr_time;
                rc = gettimeofday(&curr_time, NULL);
                check_error(rc, "gettimeofday");
                struct melding *melding_temp = klient_temp->melding_hode->neste;
                while (melding_temp != klient_temp->melding_hale) //For hver ubesvarte melding til klienten:
                {
                    //Sjekker om meldingen har vært ubesvart i tiden angitt av timeout
                    if (((curr_time.tv_sec - melding_temp->time_recieved.tv_sec) > timeout.tv_sec)
                        || (((curr_time.tv_sec - melding_temp->time_recieved.tv_sec) == timeout.tv_sec)
                        && ((curr_time.tv_usec - melding_temp->time_recieved.tv_usec) > timeout.tv_usec)))
                    {
                        if (!melding_temp->aktiv) //Hvis meldingen er markert som inaktiv:
                        {
                            //Hopper over meldingen
                            melding_temp = melding_temp->neste;
                            continue;
                        }

                        //Oppdaterer tiden til neste timeout intreffer for meldingen
                        rc = gettimeofday(&melding_temp->time_recieved, NULL);
                        check_error(rc, "gettimeofday");

                        if (melding_temp->timeout_forsok > 0)
                            melding_temp->timeout_forsok--; //Reduserer antall forsøk
                        
                        if (melding_temp->timeout_forsok < 0)
                        {
                            perror("Feil oppdaget: melding har mindre enn 0 forsøk igjen");
                            quit_main(socket_fd, EXIT_FAILURE, hode, hale);
                        }

                        if (melding_temp->timeout_forsok == 0) //Hvis alle forsøkene er brukt opp:
                        {
                            if (strcmp(melding_temp->msg_type, "REG") == 0) //Hvis Hjerteslag:
                            {
                                //Manglende ACK ikke viktig; sletter bare meldingen
                                struct melding *skal_slettes = melding_temp;
                                melding_temp = melding_temp->neste; //Flytter pekeren før meldingen slettes
                                remove_melding(skal_slettes); 
                                continue; //Sørger for at pekeren ikke flyttes dobbelt
                            }

                            else if (strcmp(melding_temp->msg_type, "LOOKUP") == 0) //Hvis Oppslag:
                            {
                                //Oppgaven defininerer for oppslagsprosedyrer at ved manglende svar fra
                                //serveren skal klienten skrive ut feilmelding og utføre QUIT-prosedyren
                                fprintf(stderr, "ACK ikke mottatt (oppslag); avslutter klientprosessen.\n");
                                quit_main(socket_fd, EXIT_FAILURE, hode, hale);
                            }

                            else if (strcmp(melding_temp->msg_type, "FROM") == 0) //Hvis Melding:
                            {
                                if (melding_temp->har_gjort_oppslag) //Hvis oppslag allerede har blitt utført:
                                {
                                    //Skriver ut feilmelding og sletter meldingen
                                    fprintf(stderr, "NICK %s UNREACHABLE\n", klient_temp->nick);
                                    struct melding *skal_slettes = melding_temp;
                                    melding_temp = melding_temp->neste; //Flytter pekeren før meldingen slettes
                                    remove_melding(skal_slettes);
                                }
                                else //Hvis oppslag ikke har blitt forsøkt ennå:
                                {
                                    melding_temp->aktiv = 0;
                                    //Utfører oppslagsprosedyren på nytt, siden IP/portnr må oppdateres
                                    klient_oppslag(klient_temp->nick, seq_num, melding_temp->seq_num, server, 
                                        socket_fd, hode, hale);
                                    seq_num++;
                                }
                                continue; //Sørger for at pekeren ikke flyttes dobbelt
                            }
                        }

                        else //Hvis det er flere forsøk igjen:
                        {
                            //Sender meldingen på nytt; øker ikke seq_num siden det er samme melding som før
                            send_melding(melding_temp, klient_temp, socket_fd, hode, hale);
                        }
                    }

                    melding_temp = melding_temp->neste;
                }

                //Går videre til neste klient
                klient_temp = klient_temp->neste;
            }
        }

        else if (FD_ISSET(STDIN_FILENO, &fds)) //Hvis melding fra tastaturet:
        {
            //En melding fra tastaturet
                //1: Lese meldingen inn i et buffer
                //Hvis QUIT:
                    //2a: avslutt klientprosessen
                //Hvis Melding:
                    //2b: Finn (evt. søk opp) mottaker, og send meldingen til mottakeren

            //1        
            get_string(buf, BUFSIZE_KLIENT); 
            char input[strlen(buf)];
            strcpy(input, buf);

            if (strlen(input) == 0) //Ignorerer tom input fra tastatur
                continue;

            //2a
            if (strcmp(input, "QUIT") == 0) //Hvis programmet skal avsluttes:
            {
                quit_main(socket_fd, EXIT_SUCCESS, hode, hale);
            }

            //Deler opp input for å sjekke gyldighet
            char *input_parts[strlen(input)]; 
            int input_count = 0;
            //Lager separat beholder for meldingen (uten nick til mottaker)
            char msg[MAKS_TEKST];
            memset(msg, 0, MAKS_TEKST);

            char *token = strtok(input, " ");
            //Sjekker at meldingen er innenfor maksimalt antall tegn

            while (token != NULL)
            {
                input_parts[input_count] = token;
                input_count++;
                token = strtok(NULL, " ");
            }

            for (int i = 1; i < input_count; i++) //Begynner å iterere etter nick
            {
                if (strlen(msg) + strlen(input_parts[i] + sizeof(char)) > MAKS_TEKST)
                    break; //Sørger for å ikke overskride maks antall tegn i meldingen
                strcat(msg, input_parts[i]);
                if (i < (input_count - 1)) //Sørger for at mellomrom ikke blir lagt til etter siste ord
                    strcat(msg, " ");
            }

            if (strcmp(input_parts[0], "BLOCK") == 0) //Hvis første ord er "BLOCK":
            {
                //Forventer input på format "BLOCK <nick>", så sjekker om den kun inneholder 2 ord
                if (input_count != 2)
                {
                    fprintf(stderr, "Feil input mottatt. Bruk 'BLOCK <nick>' for å blokkere meldinger fra brukeren <nick>.\n");
                    continue;
                }
                if (strcmp(input_parts[1], min_nick) == 0) //Hvis forsøk på å blokkere eget navn
                {
                    fprintf(stderr, "Blokkering av eget brukernavn ikke tillatt.\n");
                    continue;
                }
                struct klient *funnet_klient = search_klient(input_parts[1], hode, hale);
                if (funnet_klient == NULL) //Hvis det ikke ble funnet en klient med det angitte kallenavnet:
                {
                    //Oppretter en "tom" bruker som holder på brukernavnet, for å angi at det er blokkert.
                    funnet_klient = push_klient(input_parts[1], "xxx.xxx.xxx.xxx", 0, hode);
                }
                //Fant klient: angir den som blokkert på denne klienten
                funnet_klient->is_blocked = TRUE;

                continue;
            }

            if (strcmp(input_parts[0], "UNBLOCK") == 0) //Hvis første ord er "UNBLOCK":
            {
                //Forventer input på format "UNBLOCK <nick>", så sjekker om den kun inneholder 2 ord
                if (input_count != 2)
                {
                    fprintf(stderr, "Feil input mottatt. Bruk 'UNBLOCK <nick>' for å avblokkere meldinger fra brukeren <nick>.\n");
                    continue;
                }
                struct klient *funnet_klient = search_klient(input_parts[1], hode, hale);
                if (funnet_klient == NULL) //Hvis det ikke ble funnet en klient med det angitte kallenavnet:
                {
                    fprintf(stdout, "Fant ikke bruker med nick: %s\n", input_parts[1]);
                    continue;
                }
                //Fant klient: angir den som avblokkert på denne klienten
                funnet_klient->is_blocked = FALSE;
                if (strcmp(funnet_klient->ip, "xxx.xxx.xxx.xxx") == 0) //Hvis klienten var en "tom" klient med bare brukernavn:
                    remove_klient(funnet_klient);

                continue;
            }

            //2b
            char *annen_nick = input_parts[0];

            if (*annen_nick == '@') //Hvis første ord begynner med @:
            {
                if (strlen(annen_nick) == 1) //Hvis det angitte kallenavnet er tomt ("@ "):
                {
                    fprintf(stderr, "Tomt <nick> oppdaget. Angi <nick> på mellom 1 og 20 tegn.\n");
                    continue;
                }

                char ascii_check = 0;
                for (int i = 0; i < (int) strlen(msg); i++) //Går gjennom meldingen og sjekker om alle tegnene er ASCII
                {
                    if (!(isascii(msg[i])))
                    {
                        fprintf(stderr, "Meldinger kan bare bestå av ASCII-tegn.\n");
                        ascii_check++;
                        break;
                    }
                }
                if (ascii_check) //Hvis meldingen inneholdt et tegn som ikke er ASCII:
                    continue; //Ignorer meldingen og fortsett videre

                annen_nick++; //Flytter pekeren til å peke på selve kallenavnet

                if (strcmp(annen_nick, min_nick) == 0) //Sjekker først om mottaker er seg selv:
                {
                    //Printer i så fall melding med en gang, uten å måtte sende over socket
                    fprintf(stdout, "%s: %s\n", annen_nick, msg); 
                    continue; //Endrer ikke seq_num, siden det ikke benyttes stop and wait her
                }

                //Ser etter om klienten er lagret lokalt fra før:
                struct klient *funnet_klient = search_klient(annen_nick, hode, hale);

                if (funnet_klient == NULL) //Hvis klienten ikke er lagret:
                {
                    //Oppretter midlertidig tom bruker
                    funnet_klient = push_klient(annen_nick, "xxx.xxx.xxx.xxx", 0, hode);

                    //Lager inaktiv melding med 0 forsøk, og knytter den til oppslag
                    char pkt[strlen("PKT  FROM  TO  MSG ") + sizeof(seq_num) + strlen(min_nick)
                        + strlen(annen_nick) + strlen(msg)]; 
                    sprintf(pkt, "PKT %d FROM %s TO %s MSG %s", seq_num, min_nick, annen_nick, msg);

                    struct melding *ny_melding = push_melding(pkt, 0, funnet_klient);
                    ny_melding->aktiv = 0;
                    seq_num++;

                    //Lager oppslag knyttet til den inaktive meldingen
                    klient_oppslag(annen_nick, seq_num, ny_melding->seq_num, server, socket_fd, hode, hale);
                    seq_num++;
                }

                //Sjekker om klienten er blokkert før melding sendes
                else if (funnet_klient->is_blocked) //Hvis blokkert:
                {
                    fprintf(stdout, "Brukeren %s er blokkert. Bruk 'UNBLOCK <nick>' for å avblokkere.\n", funnet_klient->nick); //Varsler brukeren
                }

                else if (strcmp(funnet_klient->ip, "xxx.xxx.xxx.xxx") == 0)
                {
                    /*
                    I noen tilfeller kan det hende at man har sendt en melding til en ny bruker og
                    da allerede har iverksatt et oppslag for den brukeren, som nå er lagret som en
                    tom klient. Hvis man samtidig som det skjer, vil sende en ny melding til samme
                    bruker, håndteres dette ved å sette inn en ny, inaktiv melding hos den tomme
                    klient->structen, som aktiveres idet structen oppdateres ved vellykket oppslag.
                    */
                    char pkt[strlen("PKT  FROM  TO  MSG ") + sizeof(seq_num) + strlen(min_nick)
                        + strlen(annen_nick) + strlen(msg)]; 
                    sprintf(pkt, "PKT %d FROM %s TO %s MSG %s", seq_num, min_nick, annen_nick, msg);

                    struct melding *ny_melding = push_melding(pkt, 0, funnet_klient);
                    ny_melding->aktiv = 0;
                    seq_num++;
                }

                else //Hvis klienten er lagret og ikke blokkert:
                {
                    //Sender melding til lagret klient
                    klient_melding(min_nick, seq_num, msg, funnet_klient, FALSE, 2, socket_fd, hode, hale);
                    seq_num++;
                }

            }
        }

        else if (FD_ISSET(socket_fd, &fds)) //Hvis input fra socket:
        {
            //En melding fra annen klient, eller ACK fra klient eller server
                //1: Lese meldingen inn i et buffer
                //2: Verifisere korrekt format bestemme meldingstype
                    //Hvis ACK: slette tilhørende sendt melding
                //3: Skrive melding til stdout
                //4: Sende ack tilbake til klient

            //1
            rc = recvfrom(socket_fd, buf, BUFSIZE_KLIENT - 1, 0, (struct sockaddr *) &dest_addr, &addr_len); 
            check_error(rc, "recvfrom");
            buf[rc] = 0;

            //2
            char msg_recieved[BUFSIZE_KLIENT];
            strcpy(msg_recieved, buf);
            //fprintf(stdout, "%s\n", msg_recieved);

            int msg_len = strlen(msg_recieved);
            char *msg_parts[msg_len];
            int input_count = 0;

            char *token = strtok(buf, " ");
            while (token != NULL)
            {
                msg_parts[input_count] = token;
                //printf("%s ", token);
                input_count++;
                token = strtok(NULL, " ");
            }
            //printf("\n");

            //Leser ut informasjon
            char msg_type[strlen(msg_parts[0])];
            strcpy(msg_type, msg_parts[0]);
            int pkt_num = atoi(msg_parts[1]);

            if (strcmp(msg_type, "PKT") == 0) //Hvis den mottatte pakken er en Melding:
            {
                //Fortsetter med utlesing fra pakken
                char *fra_nick = msg_parts[3];
                char *til_nick = msg_parts[5];
                char tekst[msg_len];
                memset(tekst, 0, msg_len);
                //Legger til alt etter "MSG" til tekst
                for (int i = 7; i < input_count; i++)
                {
                    strcat(tekst, msg_parts[i]);
                    strcat(tekst, " ");
                }

                //Lager sjekker for å sikre betingelsene for melding i oppgaven
                char verify_0 = (strcmp(msg_parts[0], "PKT") == 0);
                char verify_2 = (strcmp(msg_parts[2], "FROM") == 0);
                char verify_4 = (strcmp(msg_parts[4], "TO") == 0);
                char verify_5 = (strcmp(til_nick, min_nick) == 0);
                char verify_6 = (strcmp(msg_parts[6], "MSG") == 0);

                if (!(verify_0 && verify_2 && verify_4 && verify_6)) //Hvis feil format:
                {
                    fprintf(stderr, "Feil format i mottatt melding\n");

                    char feilmelding[strlen("ACK  WRONG FORMAT") + sizeof(pkt_num)];
                    sprintf(feilmelding, "ACK %d WRONG FORMAT", pkt_num);
                    wc = send_packet(socket_fd, feilmelding, strlen(feilmelding), 0,
                        (struct sockaddr *) &dest_addr, addr_len);
                    check_error(wc, "send_packet");

                    continue;
                }

                if (!verify_5) //Hvis feil navn:
                {
                    fprintf(stderr, "Feil navn i mottatt melding\n");

                    char feilmelding[strlen("ACK  WRONG NAME") + sizeof(pkt_num)];
                    sprintf(feilmelding, "ACK %d WRONG NAME", pkt_num);
                    wc = send_packet(socket_fd, feilmelding, strlen(feilmelding), 0,
                        (struct sockaddr *) &dest_addr, addr_len);
                    check_error(wc, "send_packet");

                    continue;
                }

                //Henter ut info om avsenderen
                struct in_addr klient_ip_struct = dest_addr.sin_addr;
                char klient_ip[(int) addr_len];
                const char *error_check = inet_ntop(AF_INET, &klient_ip_struct, klient_ip, addr_len);
                if (error_check == NULL)
                {
                    perror("inet_ntop");
                    quit_main(socket_fd, EXIT_FAILURE, hode, hale);
                }

                //3
                //Sjekker om avsenderen allerede finnes lagret
                struct klient *avsender = search_klient(fra_nick, hode, hale);
                if (avsender == NULL) //Hvis klienten ikke er lagret fra før:
                {
                    //Lagrer avsenderen lokalt hos klient
                    avsender = push_klient(fra_nick, klient_ip, ntohs(dest_addr.sin_port), hode);
                }

                else //Hvis klienten er lagret fra før:
                {
                    //Oppdaterer IP-adresse og portnummer
                    update_klient(avsender, klient_ip, ntohs(dest_addr.sin_port));
                }
                
                //Sjekker om avsenderen er blokkert
                if (avsender->is_blocked) {} //Hvis blokkert: sender ikke ack tilbake, siden klienten ikke skal sende noe til blokkerte nicks

                //Sjekker om meldingen er et duplikat
                else if (avsender->last_seq_num != pkt_num) //Hvis ikke blokkert og ikke duplikat:
                {
                    fprintf(stdout, "%s: %s\n", fra_nick, tekst); //Print ut teksten
                    avsender->last_seq_num = pkt_num; //Oppdaterer last_seq_num for avsender
                }   

                //4
                //Sender svar uavhengig av om duplikat eller ikke
                char ack[strlen("ACK  OK") + sizeof(pkt_num)];
                sprintf(ack, "ACK %d OK", pkt_num);
                wc = send_packet(socket_fd, ack, strlen(ack), 0,
                    (struct sockaddr *) &dest_addr, addr_len);
                check_error(wc, "send_packet");

            }

            else if (strcmp(msg_type, "ACK") == 0) //Hvis den mottatte pakken er en ACK:
            {
                //Finner tilhørende pakke
                struct melding *funnet_melding = search_melding(pkt_num, hode, hale);

                if (funnet_melding == NULL) //Hvis ACK'en ikke tilhører noen sendt melding:
                    continue; //Ingen atferd definert i oppgaven, så ignorerer bare ACK'en.

                //Siden vi nå har funnet meldingen, kan vi lese av hvilken meldingstype det var for
                //å slippe testing for å finne hva ACK-en svarte på.
                memset(msg_type, 0, strlen(msg_type));
                strcpy(msg_type, funnet_melding->msg_type);

                //Utfører verifisering og behandling av melding avhengig av meldingstypen
                if (strcmp(msg_type, "REG") == 0) //Hvis Hjerteslag:
                {
                    char ack_verify[strlen("ACK  OK") + sizeof(pkt_num)];
                    sprintf(ack_verify, "ACK %d OK", pkt_num);

                    if (strcmp(ack_verify, msg_recieved) != 0) //Hvis ikke forventet svar:
                    {
                        perror("Feil oppdaget: Feil format i ACK");
                        quit_main(socket_fd, EXIT_FAILURE, hode, hale);
                    }

                    remove_melding(funnet_melding); //Sletter meldingen som har fått ACK.
                }

                else if (strcmp(msg_type, "LOOKUP") == 0) //Hvis Oppslag:
                {
                    char not_found_verify[strlen("ACK  NOT FOUND") + sizeof(pkt_num)];
                    sprintf(not_found_verify, "ACK %d NOT FOUND", pkt_num);

                    if (strcmp(not_found_verify, msg_recieved) == 0) //Hvis klient ikke er registrert:
                    {
                        fprintf(stderr, "ACK %d NOT REGISTERED\n", pkt_num);
                        /*
                        Siden det har blitt opprettet en tom klient for brukeren det ble søkt på,
                        må denne finnes og slettes.
                        */
                        struct melding *tilhorende_melding = search_melding(funnet_melding->oppslag_seq_num, hode, hale);
                        remove_klient(tilhorende_melding->klient);

                        remove_melding_oppslag(funnet_melding, FALSE, hode, hale);
                    }

                    else //Antar nå å ha fått treff hos serveren som svar
                    {
                        char *etterspurt_nick = msg_parts[3];
                        char *funnet_ip = msg_parts[5];
                        uint16_t funnet_portnr = atoi(msg_parts[7]);

                        struct klient *funnet_klient = search_klient(etterspurt_nick, hode, hale);
                        if (funnet_klient == NULL) //Hvis klienten ikke er lagret lokalt fra før:
                        {
                            push_klient(etterspurt_nick, funnet_ip, funnet_portnr, hode); //Opprett klienten lokalt
                        }
                        else //Hvis oppslaget var en del av en meldingsprosedyre:
                        {
                            update_klient(funnet_klient, funnet_ip, funnet_portnr); //Oppdater klienten
                        }
                        /*
                        Meldingen som opprinnelig skulle bli sendt før oppslaget, finnes fortsatt lagret, 
                        men den har brukt opp alle sine forsøk. Når oppslaget som førte til denne ACK'en
                        blir slettet, vil først den opprinnelige meldingen "reaktiveres" med nye forsøk.
                        Det skal altså ikke sendes en ny melding her. I stedet brukes remove_melding_oppslag,
                        som håndterer det ovennevnte.
                        */
                        remove_melding_oppslag(funnet_melding, TRUE, hode, hale);
                        continue;
                    }
                }

                else if (strcmp(msg_type, "FROM") == 0) //Hvis Melding:
                {
                    char ack_verify[strlen("ACK  OK") + sizeof(pkt_num)];
                    sprintf(ack_verify, "ACK %d OK", pkt_num);

                    if (strcmp(ack_verify, msg_recieved) != 0) //Hvis ikke forventet svar:
                    {
                        //Antar at feilmelding har blitt mottatt
                        char wrong_format_verify[strlen("ACK  WRONG FORMAT") + sizeof(pkt_num)];
                        sprintf(wrong_format_verify, "ACK %d WRONG FORMAT", pkt_num);

                        if (strcmp(wrong_format_verify, msg_recieved) == 0) //Hvis feil format:
                        {
                            perror("Feil oppdaget: Sendte melding på feil format");
                            quit_main(socket_fd, EXIT_FAILURE, hode, hale);
                        }

                        char wrong_name_verify[strlen("ACK  WRONG NAME") + sizeof(pkt_num)];
                        sprintf(wrong_name_verify, "ACK %d WRONG NAME", pkt_num);

                        if (strcmp(wrong_name_verify, msg_recieved) == 0) //Hvis feil navn:
                        {
                            perror("Feil oppdaget: Sendte melding med feil navn");
                            quit_main(socket_fd, EXIT_FAILURE, hode, hale);
                        }
                    }
                    remove_melding(funnet_melding); //Sletter meldingen som har fått ACK.
                }
            }

            else //Hvis feil format på meldingen:
            {
                fprintf(stderr, "Feil format i mottatt melding\n");

                char feilmelding[strlen("ACK  WRONG FORMAT") + sizeof(pkt_num)];
                sprintf(feilmelding, "ACK %d WRONG FORMAT", pkt_num);
                wc = send_packet(socket_fd, feilmelding, strlen(feilmelding), 0,
                    (struct sockaddr *) &dest_addr, addr_len);
                check_error(wc, "send_packet");
            }
        }
    }

    perror("Feil oppdaget: Utenfor hovedhendelseløkken");
    quit_main(socket_fd, EXIT_FAILURE, hode, hale);
}
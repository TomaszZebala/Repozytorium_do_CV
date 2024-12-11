//Klient POSIX
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>

#define MAX_BUFF                     3        //bufor do odbioru (i wysylki) maskymalnego rozmiaru wiadomosci protkolu (3 bajty wiadomosci z temepratura)
#define PORT_NUMBER                  "1204"   //numer portu do nas³uchiwania na zgloszenia serwerów UDP

#define TEMP_THRESHOLD               512      //prog przekroczenia temeperatury
#define MAX_SERVERS                  9        //maksymalna ilosc serwerow
#define PORT_LEN                     48

//Typy wiadomosci
#define HELLO                        '\x1'    //0001
#define KEEP_ALIVE                   '\x2'    //0010
#define KEEP_ALIVE_RESPONSE          '\x3'    //0011
#define TEMP_REQ                     '\x4'    //0100
#define TEMP_RSP                     '\x5'    //0101
#define SOUND_SIGNAL                 '\x6'    //0110

//Maski
#define TYPE_MASK                    '\x80'   //1000 0000 (1 bit na typ serwera)
#define ID_MASK                      '\x70'   //0111 0000 (3 bity na ID serwera)
#define MSG_MASK                     '\x0F'   //0000 1111 (4 bity na typ wiadomosci) 
#define TEMP_INFO_MASK               '\x03FF' //0000 0011 1111 1111 (10 bitow na wartosc temperatury)

//Typy serwerow
#define SERVER_TYPE_1                '\x80' //1000 0000 (1 na najstarszym bicie)
#define SERVER_TYPE_2                '\x00' //0 (0 na najstarszym bicie)

#define KEEP_ALIVE_INTERVAL          1        //co ile wysylana jest wiadomosc KEEP_ALIVE (1000ms)
#define CLIENT_TIMEOUT               10       //czas serwera na odpowiedz na KEEP_ALIVE -> KEEP_ALIVE_RESPONSE (10000ms)
#define TEMPERATURE_REQUEST_INTERVAL 1.9      //co ile wysylana jest wiadomosc TEMP_REQ (1900ms)



//Deklaracje funkcji
void monitorSockets(int); //odbieranie wiadomosci od serwerow
void whatIsTemperature(int); //cyklicznie co 1900ms odpytuje serwery typu 1 o temperature (TEMP_REQ)
void actionWithTemperature(int); //jezeli int val > TSH, polecenie dla s2, zeby wlaczyl glosnik na 1250ms
void send_keep_alive(); //sprawdza cyklicznie czy s2 jest aktywny, jezeli przez 10000ms nie odpowiada to usunac go z listy
void check_timeouts(); //sprawdzanie czy s2 nie przekroczyl 10000ms


//=============Deklaracja zmiennych======================

//struktura danych do przetrzymywania serwerow typu 1
struct server1 {
    char ip [20];
    int port;
    char token;  //ID serwera (np. 001 dla typu 1)
  };
  
struct server1 connected_servers1[MAX_SERVERS];
int num = 0;

//struktura danych do przetrzymywania serwerow typu 2
struct server2 {
    char ip [20];
    int port;
    char token; //ID serwera (np. 000 dla typu 2)
  }myServer2;
  
//Flagi
bool isServer2Alive = false; //flaga czy dziala serwer typu 2. Jest ona wlaczana po HELLO, a wylaczana przez funkcje isS2Alive()
bool terminate = false; //flaga, ktora zmienia wartosc na true, gdy serwer 2 siê odlaczy - zgodnie z zadaniem klient konczy prace

struct addrinfo h, *r=NULL;
struct sockaddr_in c;
int s, c_len=sizeof(c);

//Bufory na wiadomosci
unsigned char buffer[MAX_BUFF];
unsigned char msg[MAX_BUFF];
unsigned char signalMsg[MAX_BUFF];


uint16_t temp_value = 0x0000; //2 bajty na temperature
uint16_t last_N_temps[MAX_SERVERS]; //tablica na mozliwe wartosci temperatur od N serwerow (zeby nie nadpisywaly sie za szybko w buforze)

time_t last_time_KEEP_ALIVE_RESPONSE;
time_t last_temperature_request_time = 0;
time_t last_temperature_response_time = 0; 

//Inicjalizacja file descriptorow
fd_set master_set, working_set;



int main(){
  printf("\nStarting UDP client...\n");

  //Przygotowanie srodowiska do nasluchiwania i wysylania
  memset(&h, 0, sizeof(struct addrinfo));
  h.ai_family=PF_INET;
  h.ai_socktype=SOCK_DGRAM;
  h.ai_flags=AI_PASSIVE;
  if(getaddrinfo(NULL, PORT_NUMBER, &h, &r)!=0){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  if((s=socket(r->ai_family, r->ai_socktype, r->ai_protocol))==-1){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  if(bind(s, r->ai_addr, r->ai_addrlen)!=0){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  
  // Inicjalizacja zbioru deskryptorow
  FD_ZERO(&master_set);
  FD_SET(s, &master_set);
  
  time_t last_keep_alive_sent = time(NULL);
  time_t last_what_temperature_sent = time(NULL); 
  last_time_KEEP_ALIVE_RESPONSE = time(NULL);

  for(;;){
    //sprawdzanie przychodzacych wiadomosci od serwerow 
    monitorSockets(s);

    //Wysylanie wiadomosci KEEP_ALIVE
    if (difftime(time(NULL), last_keep_alive_sent) >= KEEP_ALIVE_INTERVAL) {
        send_keep_alive(s);
        last_keep_alive_sent = time(NULL);
    }
    //Sprawdzanie, czy serwer typu 2 nie przekroczyl czasu timeoutu
    check_timeouts();
    
    //Wysylanie wiadomosci TEMP_REQ do serwerow typu 1 i przesylanie odpowiedzi do serwera typu 2 jezeli temperatura jest wieksza niz TEMP_THRESHOLD
    if (difftime(time(NULL), last_what_temperature_sent) >= TEMPERATURE_REQUEST_INTERVAL) {
        whatIsTemperature(s);
        last_what_temperature_sent = time(NULL);
        for(int i=0;i<num;i++){
          if(last_N_temps[i] >= TEMP_THRESHOLD){
            actionWithTemperature(s);
          }
        }
    }
    if(terminate) {printf("\nLOST_CONNECTION to TYPE 2 server, TERMINATING...\n\n"); break;}
  }
  
  freeaddrinfo(r);
  close(s);
  return 0;
}


void monitorSockets(int s){
  FD_ZERO(&working_set);
  memcpy(&working_set, &master_set, sizeof(master_set));

  //akcja z select
  struct timeval timeout = {0, 50000}; // Timeout 50ms
  int activity = select(FD_SETSIZE, &working_set, NULL, NULL, &timeout);
  if (activity < 0) {
        perror("select() error");
        return;
  }
  
  //sprawdzamy polaczania przychodzace
  
  if (FD_ISSET(s, &working_set)) {
    int pos=recvfrom(s, buffer, MAX_BUFF, 0, (struct sockaddr*)&c, &c_len);
    if(pos<0){printf("ERROR: %s\n", strerror(errno));exit(-4);}
    if(c_len>0){
      buffer[pos]='\0';
      if( ((buffer[0] & MSG_MASK) == HELLO) || ((buffer[0] & MSG_MASK) == KEEP_ALIVE_RESPONSE) || ((buffer[0] & MSG_MASK) == TEMP_RSP)  ){
        //Odbior HELLO
        if((buffer[0] & MSG_MASK) == HELLO) {
        
          //Rozpoznanie Serwera TYPE 2
          if( (buffer[0] & TYPE_MASK) == 0x00) {
            last_time_KEEP_ALIVE_RESPONSE = time(NULL);
            printf("  Received HELLO from (%s:%d) - TYPE 2: %b\n", inet_ntoa(c.sin_addr),ntohs(c.sin_port), buffer[0]);  
            myServer2.port = ntohs(c.sin_port);
            strcpy(myServer2.ip, inet_ntoa(c.sin_addr));
            myServer2.token = (buffer[0] & ID_MASK) >> 4;
            isServer2Alive = true;
          }
          
          //Rozpoznanie Serwera TYPE 1
          if( (buffer[0] & TYPE_MASK) == 0x80) {
            printf("  Received HELLO from (%s:%d) - TYPE 1: %b\n", inet_ntoa(c.sin_addr),ntohs(c.sin_port), buffer[0]); 
            struct server1 current_server;
            current_server.port = ntohs(c.sin_port);
            strcpy(current_server.ip, inet_ntoa(c.sin_addr));
            current_server.token = (buffer[0] & ID_MASK) >> 4;
            
            bool is = false;
            
            for(int i=0;i<num;i++){
              if(connected_servers1[i].token == current_server.token){
                strcpy(connected_servers1[i].ip, current_server.ip);
                connected_servers1[i].port = current_server.port;
                
                is = true;
              }
            }
            
            if(!is){
              connected_servers1[num++] = current_server;
            }
          }
        }
        
        //Odbior KEEP_ALIVE_RESPONSE
        if ((buffer[0] & MSG_MASK) == KEEP_ALIVE_RESPONSE){
          printf("  Received KEEP_ALIVE_RESPONSE from TYPE 2 server\n");
          last_time_KEEP_ALIVE_RESPONSE = time(NULL); //aktualizacja od ostatniej aktywnosci    
        }
        
        //Odbior TEMP_RSP
        if ((buffer[0] & MSG_MASK) == TEMP_RSP){
          temp_value = ((buffer[1] << 8) | buffer[2]);
          int index = (buffer[0] & ID_MASK) >> 4;
          last_N_temps[index - 1] = temp_value;
          printf("  Received TEMP_RSP (Val: %d) from TYPE 1 server -> (%s)\n", temp_value, inet_ntoa(c.sin_addr), index);
        }
      }
      else{
        printf("---- UNKNOWN message: DROPPED ----\n");
      }
    }
  }
}


void send_keep_alive(int s){
  //Nadawanie KEEP_ALIVE
  if(isServer2Alive){
    msg[0] = 0x00 | (myServer2.token << 4) | KEEP_ALIVE;
    printf("Sending KEEP_ALIVE \n");
    
    char port_str[PORT_LEN];
    snprintf(port_str, PORT_LEN, "%d", myServer2.port);
    
    if (getaddrinfo(myServer2.ip, port_str, &h, &r) != 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    int pos = sendto(s, msg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen); 
    if (pos < 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    if (pos != MAX_BUFF) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
  }
}


void check_timeouts(){
  if(isServer2Alive){
    time_t current_time = time(NULL);
    double seconds_since_last_active = difftime(current_time, last_time_KEEP_ALIVE_RESPONSE);;
    if (seconds_since_last_active >= CLIENT_TIMEOUT) {
        isServer2Alive = false;
        terminate = true;  
    }
  }
}


void whatIsTemperature(int s){
  //Zapytanie TEMP_REQ do serwera typu 1
  for(int i = 0; i < num; i++){
    msg[0] = 0x80 | (connected_servers1[i].token << 4) | TEMP_REQ;
    printf("Sending TEMP_REQ to TYPE 1 server -> (%s)\n", connected_servers1[i].ip);
    
    char port_str[PORT_LEN];
    snprintf(port_str, PORT_LEN, "%d", connected_servers1[i].port);
    
    if (getaddrinfo(connected_servers1[i].ip, port_str, &h, &r) != 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    int pos = sendto(s, msg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen); 
    if (pos < 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    if (pos != MAX_BUFF) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
  }
}


void actionWithTemperature(int s){
  if(isServer2Alive){
    //WYSLANIE DO SERWERA TYPU 2 POLECENIA O ODPALENIU GLOSNIKA
    signalMsg[0] = 0x00 | (myServer2.token << 4) | SOUND_SIGNAL;
    printf("Sending SOUND_SIGNAL to TYPE 2 server\n");
    
    char port_str[PORT_LEN];
    snprintf(port_str, PORT_LEN, "%d", myServer2.port);
    
    if (getaddrinfo(myServer2.ip, port_str, &h, &r) != 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }

    int pos = sendto(s, signalMsg, MAX_BUFF, 0, r->ai_addr, r->ai_addrlen);
    
    if (pos < 0) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
    if (pos != MAX_BUFF) {
        printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
    }
  }
}

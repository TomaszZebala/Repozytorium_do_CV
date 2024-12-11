//UDP CLIENT
#include <sys/socket.h>
#include <sys/types.h>
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

#define MAX_BUFF 128
#define PORT_NUMBER "1204" //numer portu do nas³uchiwania na zg³oszenia serwerów UDP
#define LENGTH_CON 9
#define LEN_RANDOM_MSG 23
#define PORT_LEN 48


//Deklaracje funkcji
void *listening();
void *keep_alive();
void *empty_buffer();
void *random_msg();
void delete_from_list(int position);

//=============Deklaracja zmiennych======================
//struktura danych do przetrzmywania serwerów
struct server {
    char ip [20];
    int port;
    char token [4]; //s1-9
  };
struct server connected_servers [LENGTH_CON];
int num = 0;

//deklaracje do nas³uchiwania -> l
struct addrinfo h_l, *r_l=NULL;
struct sockaddr_in c_l;
int s_l, c_len_l=sizeof(c_l);
unsigned char buffer_l[MAX_BUFF];

//deklaracje do wysy³ania -> s
struct addrinfo h_s, *r_s=NULL;
struct sockaddr_in c_s;
int s_s, c_len_s=sizeof(c_s);
unsigned char buffer_s[MAX_BUFF];

pthread_mutex_t mutex;

//Funkcja sleep w milisekundach
void sleep_ms(int milliseconds)
{
    // Convert milliseconds to microseconds
    usleep(milliseconds * 1000);
}

//Struktura danych do czasu
struct timeval start, start_t, end_t, random_t;
//struct timespec start_t, end;

int main(){
  printf("Starting UDP client...\n");

  //Przygotowanie srodowiska do nas³uchiwania
  memset(&h_l, 0, sizeof(struct addrinfo));
  h_l.ai_family=PF_INET;
  h_l.ai_socktype=SOCK_DGRAM;
  h_l.ai_flags=AI_PASSIVE;
  if(getaddrinfo(NULL, PORT_NUMBER, &h_l, &r_l)!=0){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  if((s_l=socket(r_l->ai_family, r_l->ai_socktype, r_l->ai_protocol))==-1){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  if(bind(s_l, r_l->ai_addr, r_l->ai_addrlen)!=0){
    printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__); return -1;
  }
  
  //Przygotowanie srodowiska do wysy³ania
  memset(&h_s, 0, sizeof(struct addrinfo));
  h_s.ai_family=PF_INET;
  h_s.ai_socktype=SOCK_DGRAM;
  
  //=========Rozpoczêcie w¹tków aplikacji:========================
  printf("Waiting for UDP servers...\n");
  
  pthread_mutex_init(&mutex, NULL);
  pthread_t listening_th;
  pthread_t keep_alive_th;
  pthread_t empty_buffer_th;
  pthread_t random_msg_th;
  
  if (pthread_create(&listening_th, NULL, &listening, NULL) != 0) return -2;
  if (pthread_create(&keep_alive_th, NULL, &keep_alive, NULL) != 0) return -2;
  if (pthread_create(&empty_buffer_th, NULL, &empty_buffer, NULL) !=0) return -2;
  if (pthread_create(&random_msg_th, NULL, &random_msg, NULL) !=0) return -2;
  
  if (pthread_join(listening_th, NULL) != 0) return -2;
  if (pthread_join(keep_alive_th, NULL) != 0) return -2;
  if (pthread_join(empty_buffer_th, NULL) != 0) return -2;
  if (pthread_join(random_msg_th, NULL) != 0) return -2;
  
  pthread_mutex_destroy(&mutex);
  
  //Zakonczenie programu
  freeaddrinfo(r_l);
  close(s_l);
}


void generate_random_msg(char msg[], int len){
  static char chars[] = "abcdefghijklmnopqrstuwxyz0123456789";
  int x = strlen(chars);
  int random_number;

  for(int i = 0; i < len; i++){
    random_number = rand() % x;
    msg[i] = chars[random_number];  
  }
  msg[len] = '\0';
}


struct server generate_random_server(){
    int x = num;
    int random_server;
    random_server = rand() % x;
    return connected_servers[random_server];
}


//Funkcja do nas³uchiwania
void *listening(){
  for(;;){
    int pos=recvfrom(s_l, buffer_l, MAX_BUFF, 0, (struct sockaddr*)&c_l, &c_len_l);
    
    //printf("DANE Z LISTENING: %s\n", buffer_l);
    if(pos<0){printf("ERROR: %s\n", strerror(errno));exit(-4);}
    if(c_len_l>0){
      buffer_l[pos]='\0';
      
      if(pos < 4 && (buffer_l[0] == 's')){
        //Print Hello
        printf("Received HELLO from (%s:%d): %s\n\n", inet_ntoa(c_l.sin_addr),ntohs(c_l.sin_port),buffer_l);  
        
        struct server current_server;
        current_server.port = ntohs(c_l.sin_port);
        strcpy(current_server.ip, inet_ntoa(c_l.sin_addr));
        strcpy(current_server.token, buffer_l);
        
        //Sprawdzenie, czy w tablicy serwerów jest ju¿ taki o przychodz¹cym ID(Tokenie) i uaktualnienie danych lub dodanie nowego wpisu
        bool is = false;
        
        for(int i=0;i<num;i++){
          if(!strcmp(connected_servers[i].token, current_server.token)){
            strcpy(connected_servers[i].ip, current_server.ip);
            pthread_mutex_lock(&mutex);
            connected_servers[i].port = current_server.port;
            pthread_mutex_unlock(&mutex);
            
            is = true;
          }
        }
        
        if(!is){
          pthread_mutex_lock(&mutex);
          connected_servers[num++] = current_server;
          pthread_mutex_unlock(&mutex);
         }
         
      
      }
      if(buffer_l[0] == 's' && buffer_l[3] == 'Y' && buffer_l[4] == 'E' && buffer_l[5] == 'S'){
        //Printowanie przychodz¹cej odpowiedzi KEEP_ALIVE
        //printf("KEEP_ALIVE_RESPONSE: %s\n", buffer_l);
      }
      else if(buffer_l[0] == 's' && buffer_l[2] == '-' && buffer_l[3] == '>'){
        //Printowanie przychodz¹cej odpowiedzi na WIADOMOŒÆ-POLECENIE o losowej treœci
        printf("ODEBRANO: %s\n", buffer_l);
      
      }
    }
  }
}


//Funkcja do wysy³ania losowych wiadomoœci-poleceñ
void *random_msg(){
  srand(time(NULL));
  for(;;){
    if(num >= 1){
    
      
      char msg[LEN_RANDOM_MSG];
      int len = LEN_RANDOM_MSG;
      //Generowanie losowej wiadomoœci
      generate_random_msg(msg, len);
      //Wybieranie losowego serwera
      struct server ser = generate_random_server();
      
      //Losowanie czasu, po którym wyœle siê kolejna wiadomoœæ
      int scope = 1001;
      int random_time = 1500 + rand() % scope;
      
      char port_str[PORT_LEN];
      snprintf(port_str, PORT_LEN, "%d", ser.port);
      if (getaddrinfo(ser.ip, port_str, &h_s, &r_s) != 0) {
          printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
      }
    
      if ((s_s = socket(r_s->ai_family, r_s->ai_socktype, r_s->ai_protocol)) == -1) {
          printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
          freeaddrinfo(r_s);
      }
      
      char random_msg[LEN_RANDOM_MSG]; 
      
      snprintf(random_msg, MAX_BUFF, "%s->%s", ser.token, msg);
      
      //printf("\n------------------------------------------------------\n");
      //printf("WYSLANO: %s\n", random_msg);
      //printf("%s\n", random_msg);
      
      int pos = sendto(s_s, random_msg, strlen(random_msg), 0, r_s->ai_addr, r_s->ai_addrlen);  
       
      //Pocz¹tek pomiaru czasu
      gettimeofday(&start_t, NULL);
      
      if (pos < 0) {
          printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
      }
      if (pos != strlen(random_msg)) {
          printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
      }
      
      //Pocz¹tek czasu do pêtli oczekiwania na odpowiedŸ od serwera
      gettimeofday(&start, NULL);
      //Koñcowy czas trwania pêtli (10ms)
      int endtime = start.tv_usec + 10000;
      
      long time_elapsed;
      while(start.tv_usec < endtime){
         if (buffer_l[0] == 's' && buffer_l[2] == '-' && buffer_l[3] == '>'){
           gettimeofday(&end_t, NULL);
           time_elapsed = (end_t.tv_sec - start_t.tv_sec) * 1000000 + (end_t.tv_usec - start_t.tv_usec);     
           break;      
         }
         gettimeofday(&start, NULL);
      }
      
      time_t current_time;
      struct tm *local_time;
      time(&current_time);
      local_time = localtime(&current_time);
      
      
      printf("Data: %s Czas: %02d:%02d:%02d Czas operacji: %lu us\n\n", __DATE__,
                                                       local_time->tm_hour,
                                                       local_time->tm_min,        
                                                       local_time->tm_sec,
                                                       time_elapsed);
      //printf("------------------------------------------------------\n");

      sleep_ms(random_time);
    }
  }
}


void *keep_alive() {
    for (;;) {
        for (int i = 0; i < num; i++) {

            struct server ser = connected_servers[i];

            char keep_alive_info[56];
            char keep_alive_msg[56];
            char port_str[PORT_LEN];

            //Pozyskiwanie danych adresowych serwera, do którego bêdzie wysy³ana wiadomoœæ KEEP_ALIVE
            snprintf(port_str, PORT_LEN, "%d", ser.port);
            if (getaddrinfo(ser.ip, port_str, &h_s, &r_s) != 0) {
                printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
                continue;
            }

            if ((s_s = socket(r_s->ai_family, r_s->ai_socktype, r_s->ai_protocol)) == -1) {
                printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
                freeaddrinfo(r_s);
                continue;
            }

            snprintf(keep_alive_msg, MAX_BUFF, "%s OK?", ser.token);
            snprintf(keep_alive_info, MAX_BUFF, "%s DC!", ser.token);
            
            //printf("Sending a KEEP ALIVE message for %s: %s\n", ser.token, keep_alive_msg);
            
            //Wysy³anie wiadomoœci KEEP_ALIVE
            int pos = sendto(s_s, keep_alive_msg, strlen(keep_alive_msg), 0, r_s->ai_addr, r_s->ai_addrlen);
            
            if (pos < 0) {
                printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
            }
            if (pos != strlen(keep_alive_msg)) {
                printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
            }
            
            //Pocz¹tek czasu do pêtli oczekiwania na odpowiedŸ od serwera
            gettimeofday(&start, NULL);
            //Koñcowy czas trwania pêtli (50ms)
            int endtime = start.tv_usec + 50000;
            
            char response[MAX_BUFF];
            snprintf(response, MAX_BUFF, "%s YES", ser.token);
            
            while(start.tv_usec < endtime){
               if (!strcmp(buffer_l, response)) {
                 break; // Koniec pêtli, gdy otrzymamy oczwkiwan¹ odpowiedŸ
               }
               gettimeofday(&start, NULL);
            }
            
            //Po zakoñczeniu pêtli, sprawdzamy, czy odpowiedŸ by³a niepoprawna
            if (strcmp(buffer_l, response) != 0) {
                delete_from_list(i);
                
                //Informowanie serwera o jego od³¹czeniu (na wypadek zgubienia pakietu UDP bêdzie mia³ szansê do³¹czyæ z powrotem)
                printf("\n=====INFORMUJE %s O ODLACZENIU! %s =====\n", ser.token, keep_alive_info);
               
                int pos_info = sendto(s_s, keep_alive_info, strlen(keep_alive_info), 0, r_s->ai_addr, r_s->ai_addrlen);
                if (pos_info < 0) {
                  printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
                }
                if (pos_info != strlen(keep_alive_msg)) {
                  printf("ERROR: %s (%s:%d)\n", strerror(errno), __FILE__, __LINE__);
                }
                i--;
            }
            
            freeaddrinfo(r_s);
            close(s_s);
        }
        /*printf("---------------------------------------\n");
        //testowy print tablicy
        for(int i=0;i<num;i++){
          struct server ser = connected_servers[i];
          
          printf("Server %s - %s:%d:\n", ser.token, ser.ip, ser.port);
          
        }
        printf("---------------------------------------\n\n");*/
        sleep_ms(220);
    }
}


//Funkcja do usuwania elementów z listy
void delete_from_list(int position){
  pthread_mutex_lock(&mutex);
  printf("=====Usuwam niekatywny serwer=====\n");
  for(int i = position; i < num-1; i++){
    connected_servers[i] = connected_servers[i+1];
  }
  num--;
  pthread_mutex_unlock(&mutex);
}


//Funkcja czyszcz¹ca bufor co 5s, gdy dostêpny tylko 1 serwer
void *empty_buffer(){
  for(;;){
    if(num==1){
      sleep(5);
      //printf("=============CLEARING BUFFER============\n");
      strcpy(buffer_l, "");
    }
  }
}
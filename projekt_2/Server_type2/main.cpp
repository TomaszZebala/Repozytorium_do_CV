//SERWER TYPE 2 - "Fizyczne" Arduino

#include <Arduino.h>
#include <ZsutEthernet.h>
#include <ZsutEthernetUdp.h>
#include <ZsutFeatures.h>

//Rozmiary buforow
#define SEND_BUFFER_SIZE    1             //standardowy bufor na naglowek protokolu (1 bajt)
#define RCV_BUFFER_SIZE     3             //bufor do odbioru wiadomosci protokolu

//Porty
#define CLIENT_PORT         1204          //port klienta
#define LOCAL_PORT          49222         //port tego serwera

//Typy wiadomosci
#define HELLO               '\x1'         //0001
#define KEEP_ALIVE          '\x2'         //0010
#define KEEP_ALIVE_RESPONSE '\x3'         //0011
#define SOUND_SIGNAL        '\x6'         //0110

//Maski
#define TYPE_MASK           '\x80'        //1000 0000 (1 bit na typ serwera)
#define ID_MASK             '\x70'        //0111 0000 (3 bity na ID serwera)
#define MSG_MASK            '\x0F'        //0000 1111 (4 bity na typ wiadomosci) 

//Dane tego serwera (Typ 2)
#define SERVER_TYPE_2       '\x00'        //0
#define SERVER_T2_ID        '\x00'        //000

//Glosnik - emulacja
#define SPEAKER             ZSUT_PIN_D3   //Symulacja pinu glosnika


//Bufory
unsigned char sendBuffer[SEND_BUFFER_SIZE];
unsigned char packetBuffer[RCV_BUFFER_SIZE];


const int signal_duration = 1250;
int duration;
bool signal_flag = 0;

unsigned long beginMillis = 0;

ZsutIPAddress address_ip = ZsutIPAddress(192,168,89,3); //ip klienta

unsigned int localPort = LOCAL_PORT;

byte mac[] = {0x00, 0x31, 0x54, 0x45, 0x89, 0xAB};

ZsutEthernetUDP Udp;



// ------------ FUNKCJE ------------

//Funkcja wysylajaca wiadomosc HELLO do klienta
void sendHELLO() {
  sendBuffer[0] = SERVER_TYPE_2 | SERVER_T2_ID | HELLO; //pelna wiadomosc protkolu - 0 (TYPE 2), 000 (SERVER ID), 0001 (HELLO)
  
  Udp.beginPacket(address_ip, CLIENT_PORT);
  Udp.write(sendBuffer, SEND_BUFFER_SIZE);
  Udp.endPacket(); 
}


//Funkcja wysylajaca wiadomosc KEEP_ALIVE_RESPONSE do klienta
void sendKEEP_ALIVE_RESP() {
  Serial.println("Sending KEEP_ALIVE_RESPONSE");
  
  sendBuffer[0] = SERVER_TYPE_2 | SERVER_T2_ID | KEEP_ALIVE_RESPONSE; //pelna wiadomosc protkolu - 0 (TYPE 2), 000 (SERVER ID), 0011 (KEEP_ALIVE_RESPONSE)
  
  Udp.beginPacket(address_ip, CLIENT_PORT);
  Udp.write(sendBuffer, SEND_BUFFER_SIZE);
  Udp.endPacket();  
}


//Funkcja do nasluchiwania na rozne typy wiadomosci od klienta
void listenUDP() {
  int packetSize=Udp.parsePacket();
  
  if(packetSize>0) {
    int len = Udp.read(packetBuffer, RCV_BUFFER_SIZE);
    packetBuffer[len]='\0';
    
    //Sprawdzenie, czy wiadomosc istnieje
    if(len<=0){
      Udp.flush(); return;
    }
      
    //Weryfikacja, czy wiadomosc jest skierowana do niego (ID: 000) i jego typu (type 2: 0)
    if( ((packetBuffer[0] & ID_MASK) != (unsigned char) SERVER_T2_ID) || ((packetBuffer[0] & TYPE_MASK) != (unsigned char) SERVER_TYPE_2)) {
      Serial.print("---- Packet DROPPED (wrong Server ID or Server TYPE) ----\n");
      Udp.flush(); 
      return;
    }
    
    //Weryfikacja poprawnego typu wiadomosci - oczekujemty tylko KEEP_ALIVE lub SOUND_SIGNAL -> odrzucanie innych
    if( ((packetBuffer[0] & MSG_MASK) != KEEP_ALIVE) && ((packetBuffer[0] & MSG_MASK) != SOUND_SIGNAL)) {
      Serial.print("---- UNKNOWN message: DROPPED ----\n");
      Udp.flush(); 
      return;
    }

    //Obsluga wiadomosci KEEP_ALIVE
    if( (packetBuffer[0] & MSG_MASK) == KEEP_ALIVE ) {
      Serial.println("  Received KEEP_ALIVE");
      
      //Odeslanie odpowiedzi
      sendKEEP_ALIVE_RESP();
    }
    
    //Obsluga wiadomosci sygnalizujacych uruchomienie dzwieku
    if( (packetBuffer[0] & MSG_MASK) == SOUND_SIGNAL )  {
      Serial.println("\n  Received SOUND_SIGNAL \n");
      //Uruchomienie sygnału dźwiękowego
      signal_flag = 1;
      beginMillis = ZsutMillis();
    }
    
  }
}


void soundSignal() {
  if(signal_flag){
    if( (ZsutMillis() - beginMillis) <= signal_duration){
      ZsutDigitalWrite(SPEAKER, LOW); //symulacja generowania dzwieku glosnika
      
    }else{
      Serial.println("\n ---- Sound generation: BZZZZZZZZZZ ---- ");
      duration = (ZsutMillis() - beginMillis);
      Serial.print("Sound signal duration: "); Serial.println(duration); Serial.println("");
      signal_flag = 0;
      ZsutDigitalWrite(SPEAKER, HIGH);
    }
  }
}


//Uruchomienie kodu
void setup() {
  //Przerwania dla dzwieku
  ZsutPinMode(SPEAKER, OUTPUT);

  Serial.begin(115200);

  //Wypisanie komunikatow poczatkowych - start serwera
  Serial.print(F("Starting TYPE 2 server... [")); 
  Serial.print(F(__FILE__));
  Serial.print(F(", "));
  Serial.print(F(__DATE__));
  Serial.print(F(", ")); 
  Serial.print(F(__TIME__)); 
  Serial.println(F("]"));

  Serial.print("IP address: ");
  ZsutEthernet.begin(mac); 
  Serial.println(ZsutEthernet.localIP());

  //Uruchomienie nasłuchiwania
  Udp.begin(localPort);

  Serial.println("Sending HELLO... ");
  
  sendHELLO();
  Serial.println("Listening started...");

}


void loop() {
  listenUDP();
  soundSignal();
}

//SERWER TYPE 1.2 - "Temperaturowe" emulowane Arduino

#include <Arduino.h>
#include <ZsutEthernet.h>
#include <ZsutEthernetUdp.h>
#include <ZsutFeatures.h>   

//Rozmiary buforow
#define SEND_HELLO_BUFFER_SIZE    1      //standardowy bufor na naglowek protokolu (1 bajt)
#define SEND_TEMP_BUFFER_SIZE     3      //standardowy bufor na naglowek protkolu (1 bajt) + dane o temperaturze (2 bajty)
#define RCV_BUFFER_SIZE           3      //bufor do odbioru wiadomosci protokolu

//Porty
#define CLIENT_PORT               1204   //port klienta
#define LOCAL_PORT                49002  //port tego serwera

//Typy wiadomosci
#define HELLO                     '\x1'  //0001
#define TEMP_REQUEST              '\x4'  //0100
#define TEMP_RESPONSE             '\x5'  //0101

//Maski
#define TYPE_MASK                 '\x80' //1000 0000 (1 bit na typ serwera)
#define ID_MASK                   '\x70' //0111 0000 (3 bity na ID serwera)
#define MSG_MASK                  '\x0F' //0000 1111 (4 bity na typ wiadomosci) 

//Dane tego serwera (Typ 1)
#define SERVER_TYPE_1             '\x80' //1000 0000 (1 na najstarszym bicie)
#define SERVER_T1_ID              '\x20' //0010 0000 (numer 2 dla 3 bitów ID)


//Bufory
unsigned char sendBuffer[SEND_HELLO_BUFFER_SIZE];
unsigned char sendTempBuffer[SEND_TEMP_BUFFER_SIZE];
unsigned char packetBuffer[RCV_BUFFER_SIZE];

ZsutIPAddress address_ip = ZsutIPAddress(192,168,89,3); //ip klienta
unsigned int localPort = LOCAL_PORT; //port serwera

byte mac[] = {0x02, 0x31, 0x54, 0x45, 0x89, 0xAB}; //adres MAC

ZsutEthernetUDP Udp;


// ------------ FUNKCJE ------------

//Funkcja wysylajaca wiadomosc HELLO do klienta
void sendHELLO() {
  sendBuffer[0] = SERVER_TYPE_1 | SERVER_T1_ID | HELLO; //naglowek protkolu - 1 (TYPE 1), 001 (SERVER ID), 0001 (HELLO)
  
  Udp.beginPacket(address_ip, CLIENT_PORT);
  Udp.write(sendBuffer, SEND_HELLO_BUFFER_SIZE);
  Udp.endPacket(); 
}


//Funkcja do odpowiedzi na zapytanie o temperature
void sendTEMP_RESP() {
  Serial.print("  |  Sending TEMP_RSP. Current Temperature: ");
  
  //Odczyt temperatury
  int tempValue = (int) ZsutAnalog2Read();
  
 
  
  uint8_t header = SERVER_TYPE_1 | SERVER_T1_ID | TEMP_RESPONSE;  //naglowek protkolu - 1 (TYPE 1), 001 (SERVER ID), 0101 (TEMP_RESPONSE)
  uint8_t tempMSB = (tempValue >> 8) & 0xFF; //starsze bity, numer 10 i 9 
  uint8_t tempLSB = tempValue & 0xFF; //mlodsze bity, numer 8-1

  //Dolaczenie bitow do bufora, ktory wysylamy
  sendTempBuffer[0] = header;
  sendTempBuffer[1] = tempMSB;
  sendTempBuffer[2] = tempLSB;
  
  //Wysylana wartosci tempratury (laczenie 2 bajtow)
  uint16_t temp_val = ((sendTempBuffer[1] << 8) | sendTempBuffer[2]);
  Serial.print(temp_val, DEC); Serial.print(" (DEC)  |  "); Serial.print(temp_val, HEX); Serial.println(" (HEX)");
  
  //Wysylanie pakietu UDP
  Udp.beginPacket(address_ip, CLIENT_PORT);
  Udp.write(sendTempBuffer, SEND_TEMP_BUFFER_SIZE);
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
      
    //Weryfikacja, czy wiadomosc jest skierowana do tego serwera (ID: 010) i jego typu (type 1: 1)
    if( ((packetBuffer[0] & ID_MASK) != (unsigned char) SERVER_T1_ID) || ((packetBuffer[0] & TYPE_MASK) != (unsigned char) SERVER_TYPE_1) ) {
      Udp.flush(); 
      Serial.print("---- Packet DROPPED (wrong Server ID or Server TYPE) ----\n");
      return;
    }

    //Obsluga wiadomosci TEMP_REQUEST
    if( (packetBuffer[0] & MSG_MASK) == TEMP_REQUEST ) {
      Serial.print("Received TEMP_REQ: ");
      Serial.print((byte) packetBuffer[0], BIN);
      //Odeslanie odpowiedzi
      sendTEMP_RESP();
    }
    else {
      Serial.print("---- UNKNOWN message: DROPPED ----\n");
      Udp.flush(); 
      return;
    }
  }
}


//Uruchomienie kodu
void setup() {
  //Inicjalizacja serial
  Serial.begin(115200);
  Serial.println("Start Serwera typu 1");

  //Wypisanie komunikatow poczatkowych - start serwera
  Serial.print(F("UDP Server typ 1 init... [")); 
  Serial.print(F(__FILE__));
  Serial.print(F(", "));
  Serial.print(F(__DATE__));
  Serial.print(F(", ")); 
  Serial.print(F(__TIME__)); 
  Serial.println(F("]"));

  //Sprawdzenie przypisanego adresu IP
  ZsutEthernet.begin(mac);
  Serial.print("IP Address: "); Serial.println(ZsutEthernet.localIP());

  //Uruchomienie nasluchiwania
  Udp.begin(localPort);

  Serial.println("Sending HELLO... ");
  sendHELLO();
  
  Serial.println("Listening started...");
}


void loop() {
  listenUDP();
}

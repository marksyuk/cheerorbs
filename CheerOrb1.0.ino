  /* 
 *  CheerOrb
 *  
 *  Cheerlights on a Neopixel on a Wemos ESP8266 using MQTT

Neopixel application using a WeMoS D1 ESP-8266 board connecting to an MQTT broker
expects messages of the form: "#rrggbb" and sets the LED to that colour


By Andy Stanford-Clark (@andysc)
 May 2016 to
 Dec 2021


*** update the VERSION string!!! ***

1.0 CheerOrb v1.0  strip out all the provisioning stuff. Still use Wifi manager

Revisions
2022-01-17 David Marks. Incorporated support for M5STICK-C plus requires testing with NeoPixel and 8266
2022-03-22 David Marks. Incorporated support for M5 STAMP-C3 Mate - still requires testing with 8266

Known Issues
2022-03-22 David Marks. The STAMP-C3 version doesn't save the wifi credentials, so when you re-boot the board it you need to login to the webserver and choose your ap and password

Devices supported:

  ESP8266: "WeMos D1 R2 & mini"  
  
  ESP32:   "M5STICK-C Plus" "M5 STAMP-C3 Mate"      


*/

// select target device - comment out whichever does not apply 
// #define __ESP8266
// #define __M5StickCPlus
#define __M5STAMPC3

#ifdef __M5StickCPlus
  #include <M5StickCPlus.h>        
#endif

#ifdef __M5STAMPC3
  //#include <M5StickCPlus.h>        
#endif

#define VERSION "1.0"

// define this if you want to force a reset of the wifi credentials at the start
//#define RESET_WIFI


// Access Point name over-ride
// if this is defined, use that instead of the stored "ident" string
#define AP_NAME "CheerOrb"


#include <Adafruit_NeoPixel.h>          

// it seems GRB is the default
#define ORDER NEO_GRB
// change it to RGB if it's one that flashes white at power up
//#define ORDER NEO_RGB

#ifdef __ESP8266
  #include <ESP8266WiFi.h>
#endif
               
#ifdef __M5StickCPlus
  #include <WiFi.h>
#endif

#ifdef __M5STAMPC3
  #include <WiFi.h>
#endif

#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic

#include <PubSubClient.h>         // https://github.com/plapointe6/EspMQTTClient and dependencies

#include <Ticker.h>

// remember to change MQTT_KEEPALIVE to 60 in the header file {arduino installation}/libraries/PubSubClient/src/PubsSubClient.h

/////////////////////////////////////////////////////////////////////////////////////////////



// subscribe to this for commands:
#define COMMAND_TOPIC "cheerlightsRGB"


WiFiClient espClient;
WiFiManager wifiManager;

PubSubClient client(espClient);

#ifdef __ESP8266
  //Change this if using different number of neopixels or different pin
  Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, D2, ORDER); // one pixel, on pin D2
#endif

#ifdef __M5StickCPlus
  //Requires testing for M5STICK-C  
  Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, 21, ORDER); // one pixel, on pin 21
#endif

#ifdef __M5STAMPC3
  int LED_BUILTIN = 2;  // I think that this references pin GPIO8
  //Change this if using different number of neopixels or different pin
  Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, LED_BUILTIN, ORDER); // one pixel, on pin referenced by LED_BUILTIN 
#endif

Ticker ticker;


// flashes this colour when connecting to wifi:
static uint32_t wifi_colour = pixel.Color(128, 0, 128); // magenta

// flashes this colour when in AP config mode
static uint32_t config_colour = pixel.Color(0, 0, 128); // blue

// flashes this colour when connecting to MQTT:
static uint32_t mqtt_colour = pixel.Color(0, 128, 128); // cyan

static uint32_t current_colour = 0x000000; // black
static uint32_t current_LED = current_colour;



// broker connection information
char broker[] = "mqtt.cheerlights.com";
int port = 1883;
char clientID[128];

char mac_string[20]; // mac address

/////////////////////////////////////////////////////////////////////////////////////////////


void setup() {
  
  #ifdef __ESP8266
    Serial.begin(9600);
  #endif
  
  #ifdef __M5StickCPlus
    Serial.begin(115200);
  #endif

  #ifdef __M5STAMPC3
    pinMode (LED_BUILTIN, OUTPUT);
    Serial.begin(115200);
  #endif

  pixel.begin();
  delay(10);
  pixel.show(); // Initialize all pixels to 'off'


  #ifdef __M5StickCPlus
      // initialize the M5StickC object
      M5.begin();
      M5.Lcd.fillScreen(TFT_WHITE);
      Serial.println("M5STICK-C Plus");
  #endif

  delay(500);

  Serial.println();
  Serial.println("CheerOrb by @andysc");
  Serial.print("Firmware version ");
  Serial.println(VERSION);

  Serial.print("Acess Point name set to: ");
  Serial.println(AP_NAME);
  
  // make it wait longer when trying to connect
  wifiManager.setConnectTimeout(30);




#ifdef RESET_WIFI

  Serial.print("Looking for ");
  Serial.print(WiFi.SSID().c_str());
  Serial.print(" / ");
  Serial.println(WiFi.psk().c_str());

  Serial.println("*** Resetting WiFi credentials ***");
  WiFi.mode(WIFI_STA);
  
  WiFi.persistent(true);
  WiFi.disconnect(true);
  WiFi.persistent(false);

  // reset so we don't get confused and reconfigure it and then get it wiped again
  delay(2000);
  ESP.reset();
#endif


    
      // display mac address
      mac_address();
      

      Serial.print("Looking for ");
      Serial.print(WiFi.SSID().c_str());
      Serial.print(" / ");
      Serial.println(WiFi.psk().c_str());
    
      // connecting to wifi colour
      set_colour(wifi_colour); 
      ticker.attach(1, tick);

      // call us back when it's connected so we can reset the pixel
      wifiManager.setAPCallback(configModeCallback);
      
      // set the AP name it comes up as
      if (!wifiManager.autoConnect(AP_NAME)) {

        Serial.println("failed to connect to an access point");
        delay(2000);
        // not much else to do, really, other than try again

        #ifdef __ESP8266
          ESP.reset();
        #endif
        #ifdef __ESP32  
          ESP.restart();  // advice given by compiler error refers
        #endif        
      }
    
      Serial.println("connected to wifi!");
      ticker.detach();
    
      // reset the pixel to show we've connected successfully, before going for MQTT connection colour
      set_colour(0);

      Serial.print("connecting to broker: ");
      Serial.print(broker);
      Serial.print(":");
      Serial.println(port);

      client.setServer(broker, port);
      client.setCallback(callback);


    // create unique client ID
    // last 2 octets of MAC address plus some time bits
    sprintf(clientID, "%s_%s_%05d","CheerOrb", (char *)(mac_string+9), millis()%100000);
 
    Serial.print("clientID: ");
    Serial.println(clientID);

}


void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());

  Serial.println(myWiFiManager->getConfigPortalSSID());

  ticker.detach();
  set_colour(config_colour);
  ticker.attach(1, tick);
}


void tick()
{
  toggle_pixel();
}



void callback(char* topic, byte* payload, unsigned int length) {
  char content[10];
  
  // #rrggbb
  
  Serial.print("Message arrived: ");


  if (length != 7)
  {
    Serial.print("expected 7 bytfes, got ");
    Serial.println(length);
    return;
  }

  // "else"...
  // "+1" to skip over the '#'
  strncpy(content, (char *) (payload+1), length-1);
  content[length-1] = '\0';


  Serial.print("'");
  Serial.print((char)payload[0]);
  Serial.print(content);
  Serial.println("'");

  // convert the hex number to decimal
  uint32_t value = strtol(content, 0, 16);

  set_colour(value);
  
}


void wait_for_wifi()
{
  
  Serial.println("waiting for Wifi");
  
  // connecting to wifi colour
  set_colour(wifi_colour);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    toggle_pixel();
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
   
}


void reconnect() {
  
  boolean first = true;

  // Loop until we're reconnected to the broker
  while (!client.connected()) {

    if (WiFi.status() != WL_CONNECTED) {
      ticker.detach();
      wait_for_wifi();
      first = true;
    }

    Serial.print("Attempting MQTT connection...");
    if (first) {
      // now we're on wifi, show connecting to MQTT colour
      set_colour(mqtt_colour);
      first = false;

      // flash every 2 sec while we're connecting to broker
      ticker.attach(2,tick);
    }

    if (client.connect(clientID)) {
      Serial.println("connected");
    } 
    else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      
      // wait 5 seconds to prevent connect storms
      delay(5000);
    }
    
  }

  ticker.detach();

  delay(1000); // wait a second to make sure they saw the mqtt_colour
  set_colour(0); // clear pixel when connected (black)
  
  // subscribe to the command topic
  Serial.print("subscribing to '");
  Serial.print(COMMAND_TOPIC);
  Serial.println("'");
  
  client.subscribe(COMMAND_TOPIC);

}


void set_colour(uint32_t colour) {
  
  set_pixels(colour);
  // Updates current_LED with what the user last requested,
  // so we can toggle it to black and back again.

  // DJM TODO convert colour to RGB656, update screen on M5STICK-C -> M5.Lcd.fillScreen() refers
  
  current_colour = colour;
}



void set_pixels(uint32_t colour) {
  
  for (int i = 0; i < pixel.numPixels(); i++) {
    pixel.setPixelColor(i, colour);
  }
  pixel.show();

  // Store current actual LED colour
  // (which may be black if toggling code turned it off.)
  current_LED = colour;
}


void toggle_pixel() {

  if (current_LED == 0) 
  {
    // if it's black, set it to the stored colour
    set_pixels(current_colour);
  } 
  else
  {
    // otherwise set it to black
    set_pixels(0);
  }
}


void loop() {
       
    if (!client.connected()) {
      reconnect();
    }
    
    // service the MQTT  client
    client.loop();
}



void mac_address() {
  byte mac[6];                   

  WiFi.macAddress(mac);   // the MAC address of your Wifi shield

  sprintf(mac_string, "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);

  Serial.println(mac_string);
}

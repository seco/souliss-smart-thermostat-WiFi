//#define MaCaco_DEBUG_INSKETCH
//  #define MaCaco_DEBUG   1
//
//#define VNET_DEBUG_INSKETCH
//  #define VNET_DEBUG    1

/**************************************************************************
   Souliss - Web Configuration

    This example demonstrate a complete web configuration of ESP8266 based
	nodes, the node starts as access point and allow though a web interface
	the configuration of IP and Souliss parameters.

	This example is only supported on ESP8266.
***************************************************************************/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <DHT.h>

// Configure the Souliss framework
#include "bconf/MCU_ESP8266.h"              // Load the code directly on the ESP8266
#include "preferences.h"

#if(DYNAMIC_CONNECTION==1)
#include "conf/RuntimeGateway.h"            // This node is a Peer and can became a Gateway at runtime
#include "conf/DynamicAddressing.h"         // Use dynamically assigned addresses
#include "conf/WEBCONFinterface.h"          // Enable the WebConfig interface
#include "connection_dynamic.h"
#else
#include "conf/IPBroadcast.h"
#include "connection_static.h"
#endif

#include "Souliss.h"

#include "encoder.h"
#include "constants.h"
#include "display.h"
#include "display2.h"
#include "language.h"
#include "ntp.h"
#include <Time.h>
#include <MenuSystem.h>
#include "menu.h"
#include "crono.h"
#include "read_save.h"

//*************************************************************************
//*************************************************************************

DHT dht(DHTPIN, DHTTYPE);
float temperature = 0;
float humidity = 0;
float setpoint = 0;
float encoderValue_prec = 0;

//DISPLAY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include <SPI.h>
#include <Arduino.h>
#include "Ucglib.h"


int backLEDvalue = 0;
int backLEDvalueHIGH = BRIGHT_MAX;
int backLEDvalueLOW = BRIGHT_MIN_DEFAULT;
bool FADE = 1;


// Menu
MenuSystem* myMenu;

// Use hardware SPI
Ucglib_ILI9341_18x240x320_HWSPI ucg(/*cd=*/ 2 , /*cs=*/ 15);

// Setup the libraries for Over The Air Update
OTA_Setup();

void setup()
{
  SERIAL_OUT.begin(115200);

  //DISPLAY INIT
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  ucg.begin(UCG_FONT_MODE_SOLID);
  ucg.setColor(0, 0, 0);
  ucg.setRotate90();
  //BACK LED
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  digitalWrite(BACKLED, HIGH);
  pinMode(BACKLED, OUTPUT);                     // Background Display LED

  display_print_splash_screen(ucg);
  Initialize();

#if(DYNAMIC_CONNECTION==1)
  DYNAMIC_CONNECTION_Init();
#else
  STATIC_CONNECTION_Init();
#endif
  //*************************************************************************
  //*************************************************************************
  Set_T52(SLOT_TEMPERATURE);
  Set_T53(SLOT_HUMIDITY);
  Set_T19(SLOT_BRIGHT_DISPLAY);
  Set_T11(SLOT_AWAY);

  //set default mode
  Set_Thermostat(SLOT_THERMOSTAT);
   set_ThermostatModeOn(SLOT_THERMOSTAT);
  set_DisplayMinBright(SLOT_BRIGHT_DISPLAY, BRIGHT_MIN_DEFAULT);

  // Define output pins
  pinMode(RELE, OUTPUT);    // Heater
  dht.begin();

  //ENCODER
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode (ENCODER_PIN_A, INPUT_PULLUP);
  pinMode (ENCODER_PIN_B, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), encoderFunction, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), encoderFunction, CHANGE);
  // SWITCH ENCODER
  digitalWrite(BACKLED, HIGH);
  pinMode(ENCODER_SWITCH, INPUT);

  //NTP
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  initNTP();
  delay(1000);
  //*************************************************************************
  //*************************************************************************

  // EEPROM 
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  Store_Init();
  ReadAllSettingsFromEEPROM();

  //MENU
  /////////////////////////////////////////////////////////////////////////////////////////////////////////
  initMenu();
  myMenu = getMenu();

  // Init the OTA
  OTA_Init();


  //SPI Frequency
  SPI.setFrequency(80000000);

  // Init HomeScreen
  initScreen();
}

float fVal;
void loop()
{
  EXECUTEFAST() {
    UPDATEFAST();

    FAST_50ms() {
      //set point attuale
      setpoint = Souliss_SinglePrecisionFloating(memory_map + MaCaco_OUT_s + SLOT_THERMOSTAT + 3);
      //Stampa il setpoint solo se il valore dell'encoder è diverso da quello impostato nel T31
      if (!getMenuEnabled()) {
        if (arrotonda(getEncoderValue()) != arrotonda(encoderValue_prec)) {
          FADE = 1;
          //TICK TIMER
          timerDisplay_setpoint_Tick();
          //SETPOINT PAGE ////////////////////////////////////////////////////////////////

          if (getLayout1()) {
            SERIAL_OUT.println("display_setpointPage - layout 1");
            display_layout1_background(ucg, arrotonda(getEncoderValue()) - arrotonda(setpoint));
          }
          else if (getLayout2()) {
            SERIAL_OUT.println("display_setpointPage - layout 2");
            display_layout2_Setpoint(ucg, getEncoderValue());
          }
        }

        encoderValue_prec = getEncoderValue();
      } else {
        //Bright high if menu enabled
        FADE = 1;
        //Menu Command Section
        if (getEncoderValue() != encoderValue_prec)
        {
          if (getEncoderValue() > encoderValue_prec) {
            //Menu DOWN
            myMenu->next();
          } else {
            //Menu UP
            myMenu->prev();
          }
          printMenu(ucg);
          encoderValue_prec = getEncoderValue();
        }
      }

      Logic_T19(SLOT_BRIGHT_DISPLAY);
      Logic_T11(SLOT_AWAY);
    }

    FAST_110ms() {
      if (!getMenuEnabled()) {
        if (timerDisplay_setpoint()) {
          //timeout scaduto
          display_layout1_background_black(ucg);
          setEncoderValue(setpoint);
        } else {
          //timer non scaduto. Memorizzo
          setpoint = getEncoderValue();
          //memorizza il setpoint nel T31
          setSetpoint(setpoint);

          // Trig the next change of the state
          setSoulissDataChanged();
        }
      }

      //SWITCH ENCODER
      if (!digitalRead(ENCODER_SWITCH)) {
        if (!getMenuEnabled()) {
          //IF MENU NOT ENABLED
          setEnabled(true);
          //il flag viene impostato a true, così quando si esce dal menu la homescreen viene aggiornata ed il flag riportato a false
          setChanged();
          ucg.clearScreen();
        } else {
          //IF MENU ENABLED
          myMenu->select(true);
          yield();
          /// CRONO 
          if (getProgCrono()) { 
          byte menu;
            ucg.clearScreen();
            drawCrono(ucg); 
            menu=1;
                  while(menu==1){
                    setDay(ucg);
                    drawBoxes(ucg);
                    setBoxes(ucg);
                    //delay(2000);
                    if(digitalRead(ENCODER_SWITCH)==LOW)
                    {menu=0; }
                  }
          }
        }
        printMenu(ucg);
      }

      //FADE
      if (FADE == 0) {
        //Raggiunge il livello di luminosità minima, che può essere variata anche da SoulissApp
        if ( backLEDvalue != backLEDvalueLOW) {
          if ( backLEDvalue > backLEDvalueLOW) {
            backLEDvalue -= BRIGHT_STEP_FADE_OUT;
          } else {
            backLEDvalue += BRIGHT_STEP_FADE_OUT;
          }
          bright(backLEDvalue);
        }
      } else  if (FADE == 1 && backLEDvalue < backLEDvalueHIGH) {
        backLEDvalue +=  BRIGHT_STEP_FADE_IN;
        bright(backLEDvalue);
      }
    }

    FAST_210ms() {   // We process the logic and relevant input and output
      //*************************************************************************
      //*************************************************************************
      Logic_Thermostat(SLOT_THERMOSTAT);
      // Start the heater and the fans
      nDigOut(RELE, Souliss_T3n_HeatingOn, SLOT_THERMOSTAT);    // Heater

      //if menu disabled and nothing changed
      if (!getMenuEnabled() && !getSystemChanged()) {
        if (getLocalSystem() != getSoulissSystemState())
          setSystem(getSoulissSystemState());
      }

      //*************************************************************************
      //*************************************************************************
    }

    FAST_510ms() {
      // Compare the acquired input with the stored one, send the new value to the
      // user interface if the difference is greater than the deadband
      Logic_T52(SLOT_TEMPERATURE);
      Logic_T53(SLOT_HUMIDITY);

    }


    FAST_710ms() {
      //HOMESCREEN ////////////////////////////////////////////////////////////////
      ///update homescreen only if menu exit
      if (!getMenuEnabled() && getSystemChanged()) {
        //EXIT MENU - Actions
        //write min bright on T19
        memory_map[MaCaco_OUT_s + SLOT_BRIGHT_DISPLAY + 1] = getDisplayBright();
        SERIAL_OUT.print("Set Display Bright: "); SERIAL_OUT.println(memory_map[MaCaco_OUT_s + SLOT_BRIGHT_DISPLAY + 1]);

        //write system ON/OFF
        if (getLocalSystem()) {
          //ON
          SERIAL_OUT.println("Set system ON ");
          set_ThermostatModeOn(SLOT_THERMOSTAT);        // Set System On
        } else {
          //OFF
          SERIAL_OUT.println("Set system OFF ");
          set_ThermostatOff(SLOT_THERMOSTAT);
        }

        memory_map[MaCaco_IN_s + SLOT_THERMOSTAT] = Souliss_T3n_RstCmd;          // Reset
        // Trig the next change of the state

        setSoulissDataChanged();

        SERIAL_OUT.println("Init Screen");
        initScreen();

        resetSystemChanged();
      }

    }

    FAST_910ms() {
      if (timerDisplay_setpoint()) {
        //if timeout read value of T19
        backLEDvalueLOW =  memory_map[MaCaco_OUT_s + SLOT_BRIGHT_DISPLAY + 1];
        FADE = 0;
        //HOMESCREEN ////////////////////////////////////////////////////////////////
        if (!getMenuEnabled()) {
          if (getLayout1()) {
            display_layout1_HomeScreen(ucg, temperature, humidity, setpoint, getSoulissSystemState());
          } else if (getLayout2()) {
            //
          }
        }
      }
    }

#if(DYNAMIC_CONNECTION)
    DYNAMIC_CONNECTION_fast();
#else
    STATIC_CONNECTION_fast();
#endif
  }

  EXECUTESLOW() {
    UPDATESLOW();

    SLOW_50s() {
      if (!getMenuEnabled()) {
        if (getLayout1()) {
          getTemp();
        } else if (getLayout2()) {
          display_layout2_print_circle_white(ucg);
          getTemp();
          display_layout2_HomeScreen(ucg, temperature, humidity, setpoint);
          display_layout2_print_datetime(ucg);
          display_layout2_print_circle_black(ucg);
          yield();
          display_layout2_print_circle_green(ucg);
        }
      }
    }

    SLOW_70s() {
      if (!getMenuEnabled()) {
        if (getLayout1()) {
          //
        } else if (getLayout2()) {
          calcoloAndamento(ucg, temperature);
          display_layout2_print_datetime(ucg);
          display_layout2_print_circle_green(ucg);
        }
      }
    }

    SLOW_15m() {
      //NTP
      /////////////////////////////////////////////////////////////////////////////////////////////////////////
      yield();
      initNTP();
      yield();
    }


#if(DYNAMIC_CONNECTION==1)
    DYNAMIC_CONNECTION_slow();
#endif
  }
  // Look for a new sketch to update over the air
  OTA_Process();
}


void set_ThermostatModeOn(U8 slot) {
  SERIAL_OUT.println("set_ThermostatModeOn");
  memory_map[MaCaco_OUT_s + slot] |= Souliss_T3n_HeatingMode | Souliss_T3n_SystemOn;

  // Trig the next change of the state

  setSoulissDataChanged();
}

void set_ThermostatOff(U8 slot) {
  SERIAL_OUT.println("set_ThermostatOff");
  //memory_map[MaCaco_IN_s + slot] = Souliss_T3n_ShutDown;
  memory_map[MaCaco_OUT_s + SLOT_THERMOSTAT] &= ~ (Souliss_T3n_SystemOn | Souliss_T3n_FanOn1 | Souliss_T3n_FanOn2 | Souliss_T3n_FanOn3 | Souliss_T3n_CoolingOn | Souliss_T3n_HeatingOn);
  setSoulissDataChanged();
}

void set_DisplayMinBright(U8 slot, U8 val) {
  memory_map[MaCaco_OUT_s + slot + 1] = val;
  // Trig the next change of the state
  
  setSoulissDataChanged();
}

void getTemp() {
  // Read temperature value from DHT sensor and convert from single-precision to half-precision
  temperature = dht.readTemperature();
  //Import temperature into T31 Thermostat
  ImportAnalog(SLOT_THERMOSTAT + 1, &temperature);
  ImportAnalog(SLOT_TEMPERATURE, &temperature);

  // Read humidity value from DHT sensor and convert from single-precision to half-precision
  humidity = dht.readHumidity();
  ImportAnalog(SLOT_HUMIDITY, &humidity);

  SERIAL_OUT.print("aquisizione Temperature: "); SERIAL_OUT.println(temperature);
  SERIAL_OUT.print("aquisizione Humidity: "); SERIAL_OUT.println(humidity);
}

boolean getSoulissSystemState() {
  return memory_map[MaCaco_OUT_s + SLOT_THERMOSTAT] & Souliss_T3n_SystemOn;
}

void bright(int lum) {
  int val = ((float)lum / 100) * 1023;
  if (val > 1023) val = 1023;
  if (val < 0) val = 0;
  analogWrite(BACKLED, val);
}


void initScreen() {
  ucg.clearScreen();
  if (getLayout1()) {
    SERIAL_OUT.println("HomeScreen Layout 1");

    display_layout1_HomeScreen(ucg, temperature, humidity, setpoint, getSoulissSystemState());
    getTemp();
  }
  else if (getLayout2()) {
    SERIAL_OUT.println("HomeScreen Layout 2");
    getTemp();
    display_layout2_HomeScreen(ucg, temperature, humidity, setpoint);
    display_layout2_print_circle_white(ucg);
    display_layout2_print_datetime(ucg);
    display_layout2_print_circle_black(ucg);
    yield();
    display_layout2_print_circle_green(ucg);
  }
}

void encoderFunction() {
  encoder();
}

void setSetpoint(float setpoint) {
  //SERIAL_OUT.print("Away: ");SERIAL_OUT.println(memory_map[MaCaco_OUT_s + SLOT_AWAY]);
  if (memory_map[MaCaco_OUT_s + SLOT_AWAY]) {
    //is Away

  } else {
    //is not Away
  }
  Souliss_HalfPrecisionFloating((memory_map + MaCaco_OUT_s + SLOT_THERMOSTAT + 3), &setpoint);
}


void setSoulissDataChanged() {
  SERIAL_OUT.println("setSoulissDataChanged");
  data_changed = Souliss_TRIGGED;
}


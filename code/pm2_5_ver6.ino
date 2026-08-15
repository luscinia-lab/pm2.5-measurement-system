/***************************************************************
   Air-dust Density Logger for Arduino-Uno (ATmega328P)
      Initial Version V.00     Mar.26 2022    (c)Akira Tominaga
      Functions:
        - Measure PM1.0, PM2.5 and PM10 densities (μg/m^3).
        - Display current density of PM2.5  to TM1637 LED.
        - Record each minuite's average data to micro SD card.
      Pin connections:
              Air-dust sensor ZH03B; TX = software Rx pin 2.
              Micro SD card; MOSI=11, MISO=12, CLK=13, CS=10.
              Real time clock DS3231; SDA=A4, SCL=A5.
              TM1637 4digit LED display: DIO=6, CLK=7.
              Optional switch: pin9 to avoid pwr-off during
              SD-write timing (though extremely rare).
 ***************************************************************/
#include "SoftwareSerial.h"
#define sRx 2                     // sS-Rx = sensor Tx pin
#define sTx -1                    // sS-Tx = sensor Rx not connected
#define sSbaud 9600               // software-serial baud rate
#include "Wire.h"                 // for RTC interface
#include "SPI.h"                  // for SD interface
#include "SD.h"                   // for micro SD drive
byte rBt[24];                     // rcv bytes from software-Serial
// *** for air-dust sensor ***
SoftwareSerial sS(sRx, sTx);      // sS as SW-serial symbol
uint16_t PM1_0, PM2_5, PM10_;     // measured values
#define iuLen 24                  // length of sensor-Initiative-Upload
uint16_t cSum;                    // checksum
uint8_t dC = 0;                   // data counter to calculate averages
uint16_t tPM1_0 = 0;              // total PM1.0 value
uint16_t tPM2_5 = 0;              // total PM2.5 value
uint16_t tPM10_ = 0;              // total PM10. value
// *** for Real Time Clock ***
#define RTC_addr 0x68
#define mdI 0                     // Initial mode
#define mds 1                     // ss
#define mdm 2                     // mm
#define mdh 3                     // hh
#define mdW 4                     // WW=Day of Week
#define mdD 5                     // DD
#define mdM 6                     // MM
#define mdY 7                     // YY
char  MMDD_hhmm[14];              // editing area for calendar and clock
char  hhmmss[10];                 // editing area for hh:mm:ss
uint8_t vI[8] = { 0, 0, 0, 0, 0, 0, 0, 0}; // integers for RTC data
// *** for micro SD drive ***
#define cS 10                     // chip select
String fName;                     // CSV file name
String Rec;                       // contents of record
uint8_t minSave;                  // save area for minute value
// *** SD disable switch to avoid power-off during SD writing
#define SDsw 9                    // SD enable/disable switch pin
// *** for TM1637 display to diplay real time PM2.5 μg/m^3
//#include "TM1637Display.h"
#define tmDIO 6                   // TM1637 DIO
#define tmCLK 7                   // TM1637 CLK
//TM1637Display TM(tmCLK, tmDIO);  // class TM for TM1637

//------------------------------------------------------------------------

#include <Wire.h>
#include "RTClib.h"

RTC_DS3231 rtc;
//------------------------------------------------------------------
#include <DHT.h>              // ライブラリのインクルード

#define DHT_PIN 4             // DHT11のDATAピンをデジタルピン4に定義
#define DHT_MODEL DHT11       // 接続するセンサの型番を定義する(DHT11やDHT22など)

DHT dht(DHT_PIN, DHT_MODEL);  // センサーの初期化
//---------------------------------------------------------------------------
unsigned long startTime;
unsigned long currentTime;
const unsigned long period = 1000;
const byte ledPin = 13;

void setup() { // ***** Arduino setup *****
  if (! rtc.begin()) {
        Serial.println("Couldn't find RTC");
        while (1);
      }
    // <---- ここで現在時刻を設定（PCの時刻を取得してセット）
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  // 例：2025-07-27 22:48:00 など
  Serial.println("RTC is set!");
    
  Serial.begin(sSbaud);           // start hardware serial
  sS.begin(sSbaud);               // start SW-serial sS
  Wire.begin();                   // start I2C

  dht.begin();                // センサーの動作開始

  startTime = millis(); //initial start time


  // *** start TM1337 display
  //TM.setBrightness(2); // *** set LED brightness(low0-high7)
  const byte segs[] = {0x40, 0x40, 0x40, 0x40}; // "----"
  //TM.setSegments(segs, 4, 0);     // displsy "----"
  // *** set SD file name as MMDDhhmm.csv, using start-time
  getTime();
  minSave = vI[mdm];              // save minute value for later use
  sprintf(MMDD_hhmm, "%02d%02d%02d%02d.csv", vI[mdM], vI[mdD], vI[mdh], vI[mdm]);
  fName = String(MMDD_hhmm);
  pinMode(SDsw, INPUT_PULLUP);    // set SD enable/disable Sw HIGH(enable)
  // *** check SD card
  if (!SD.begin(cS)) {
    Serial.println(F("SD err"));
    while (true) {}
  }
  File aqLog = SD.open(fName, FILE_WRITE); // make the file for data
  // ***** write column header
  //aqLog.println("MM/DD-hh:mm,PM1.0,PM2.5, PM10");
  //Serial.print(fName); Serial.println(F(" created"));
  aqLog.close();
  delay(200);

}
void loop() {  // ***** Arduino Loop *****
  DateTime now = rtc.now();
  
  Serial.print(now.year(), DEC); Serial.print(",");
  Serial.print(now.month(), DEC); Serial.print(",");
  Serial.print(now.day(), DEC); Serial.print(",");
  Serial.print(now.hour(), DEC); Serial.print(",");
  Serial.print(now.minute(), DEC); Serial.print(",");
  Serial.print(now.second(), DEC);
  //Serial.print("   ");
  Serial.print(",");
  
  //delay(1000);


  while (sS.available() < iuLen) {} // wait if not data ready
  // *** read data from sensor
  for (int j = 0; j < iuLen; j++) {
    rBt[j] = sS.read();
  } // got data
  // *** check validity of the data
#define okD 0x00
#define ngD 0x01
  byte ckD = okD;
  // *** confirm correct sensor-initiative-upload
  if (rBt[0] != 0x42) {           // if invalid I.U. header
    ckD = ngD;                    // do not use error data
    flushData();
  }
  // ***** check Checksum *****
  cSum = 0;
  for (uint8_t j = 0; j < iuLen - 2; j++) {
    cSum += rBt[j];
  }
  byte cSh = cSum / 256;
  byte cSl = cSum % 256;
  if ((cSh != rBt[22]) | (cSl != rBt[23])) { // if error,
    Serial.println(F("*Cksum err")); // then
    ckD = ngD;                      // do not use error data
    flushData();
  }
  // *** process for valid data only
  if (ckD == okD) {
    getTime();
    sprintf(MMDD_hhmm, "%02d/%02d-%02d:%02d", vI[mdM], vI[mdD], vI[mdh], vI[mdm], vI[mds]);
    sprintf(hhmmss, "%02d:%02d:%02d ", vI[mdh], vI[mdm], vI[mds]);
    PM1_0 = rBt[10] * 256 + rBt[11];
    PM2_5 = rBt[12] * 256 + rBt[13];
    PM10_ = rBt[14] * 256 + rBt[15];
    
    //Serial.print(hhmmss); 

    //Serial.print("PM1.0:");
    Serial.print(PM1_0);
    Serial.print(","); 

    //Serial.print("PM2.5:");
    Serial.print(PM2_5);
    Serial.print(","); 
    
    //Serial.print("PM10:");
    Serial.print(PM10_);
    Serial.print(","); 
    
    
    //TM.showNumberDec(PM2_5, false, 4, 0);   // show PM2.5 to LED
    // *** if minute changed record log, else accumulate data
    if ((minSave != vI[mdm]) & (dC > 0)) {  // if minute value changed,
      minSave = vI[mdm];            // save new minute to check
      float fPM1_0 = (float)tPM1_0 / (float)dC;
      float fPM2_5 = (float)tPM2_5 / (float)dC;
      float fPM10_ = (float)tPM10_ / (float)dC;
      Rec = String(MMDD_hhmm) + "," + String(fPM1_0, 2) + "," + String(fPM2_5, 2) + "," + String(fPM10_ , 2);
      putLog();
      //Serial.println(Rec);
      dC = 0; tPM1_0 = 0; tPM2_5 = 0; tPM10_ = 0; // reset accumulated values
    } else {                        // if not timing, accumulate totals
      dC++;  tPM1_0 += PM1_0; tPM2_5 += PM2_5; tPM10_ += PM10_;
    }
  }
  // no delay allowed in the sensor-initiative-upload mode

  currentTime = millis();
  if(currentTime - startTime >= period){

    float Humidity = dht.readHumidity();          // 湿度の読み取り
  float Temperature = dht.readTemperature();    // 温度の読み取り(摂氏)

  if (isnan(Humidity) || isnan(Temperature)) {  // 読み取りのチェック
    Serial.println("ERROR");
    return;
  }

// シリアルモニタに温度&湿度を表示  
  //Serial.print("温度: ");
  Serial.print(Temperature-1.00);
  Serial.print(",");

  //Serial.print("  湿度: "); 
  Serial.print(Humidity);  
  Serial.println();

  }
  startTime = currentTime;
} // end of loop()
/***********************************************************
    User defined functions
 ***********************************************************/
// ***** get time *** getTime() *****
void getTime(void) {
  byte  vR[8];                      // values in RTC registers
  Wire.beginTransmission(RTC_addr);
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.requestFrom(RTC_addr, 7);
  while (Wire.available() < 7) {} // wait for data ready
  for (int i = 1; i < 8; i++) {
    vR[i] = Wire.read();
  }
  Wire.endTransmission();
  // *** convert RTC-format to Integers
  vI[mds] = ((vR[mds] & B01110000) / 16) * 10 + (vR[mds] & B00001111);
  vI[mdm] = ((vR[mdm] & B01110000) / 16) * 10 + (vR[mdm] & B00001111);
  vI[mdh] = ((vR[mdh] & B00100000) / 32) * 20 + ((vR[mdh] & B00010000) / 16) * 10 + (vR[mdh] & B00001111);
  vI[mdW] = vR[mdW];
  vI[mdD] = ((vR[mdD] & B01110000) / 16) * 10 + (vR[mdD] & B00001111);
  vI[mdM] = ((vR[mdM] & B00010000) / 16) * 10 + (vR[mdM] & B00001111);
  vI[mdY] = ((vR[mdY] & B11110000) / 16) * 10 + (vR[mdY] & B00001111);
}
// ***** check and flush if broken data exists
void flushData(void) {
  int resBt = sS.available();
  if (resBt > 0) {              // if rest of data exists
    for (int k = resBt; k <= 0; k--) {
      rBt[0] = sS.read();       // dummy read for flush
    } // end for
    Serial.println(F("Flushed"));
  } // end if
}
// ***** put log Rec *** putLog() *****
void putLog(void) {
  if (digitalRead(SDsw) == HIGH) {    // do only when SDsw is HIGH
    File aqLog = SD.open(fName, FILE_WRITE);
    aqLog.println(Rec);
    aqLog.close();
  }
}
// ***** End of Sketch **
#include <Arduino.h>
#ifdef ESP32
   #include <WiFi.h>
#else
   #include <ESP8266WiFi.h>
#endif
#include <SPI.h>
#include <Ethernet.h>
#include <Utility\Socket.h>
// Library for a Web server
#include <ESPAsyncWebServer.h>
//Library for Phil emulation
#define ESPALEXA_ASYNC //it is important to define this before #include <Espalexa.h>!
#include <Espalexa.h>


//Library for the InfraRed
#include <IRremoteESP8266.h>
// Library to SEND Infrared
#include <IRsend.h>
// Remote control for the AC code of Mitsubishi: change here for specific model
// Make sure to change the parameters "Mitsubishi" accordingly for the IR ( on input yours if you know it)
#include <ir_Mitsubishi.h>
// Remote control for Philips TV (Future version)
//#include "PhilipsRC6Remote.h"
// Time Sync ( for AC) through a NTP server
#include <NTPClient.h>
// credentials.h contains the  according to your router configuration
#include "credentials.h"
// Library for TwoWire from Adafruit
#include <Wire.h>
#define SDA_PIN 2
#define SCL_PIN 0

// Library for the 2320: a good Temperature and Humidity sensor
#include <AM2320.h>

AM2320 Ambient;



#include <WiFiUdp.h>

//Define the WebServer
AsyncWebServer WebServer(80);



WiFiUDP ntpUDP;
// Change this parameters according to your timezone
NTPClient timeClient(ntpUDP, "sg.pool.ntp.org", 28800);



const uint16_t kIrLed = 5;  // ESP8266 GPIO pin to use. Recommended: 4 (D2) I have used 5 (D1).
IRMitsubishiAC ac(kIrLed);
// PhilipsRC6Remote BedTV("4");


// -----------------------------------------------------------------------------


#define SERIAL_BAUDRATE     115200
#define TV_Bedroom         "Philips"
#define AC_Bedroom         "CoolBed"
#define AC_Sleep           "SweetDream"
#define DVD_Player         "Player"
// The next is only for future use
#define ID_Temperature     "Temperature"
#define ID_Humidity        "Humidity"

//callback functions
void Night_Mode(uint8_t brightness);
void AC_Power(uint8_t brightness);
void BR_Temperature(uint8_t brightness);
void BR_Humidity(uint8_t brightness);
void BR_TV(uint8_t brightness);
void BR_Dvd(uint8_t brightness);


Espalexa espalexa;
EspalexaDevice* Night_ModeAx;
EspalexaDevice* AC_PowerAx;
EspalexaDevice* BR_TemperatureAx;
EspalexaDevice* BR_HumidityAx;
EspalexaDevice* BR_TVAx;
EspalexaDevice* BR_DvdAx;


bool SwitchTV, SwitchAC, SwitchDVD, SwitchSleep, SwitchTemperature, SwitchHumidity;

bool StatusTV, StatusAC, StatusDVD, StatusSleep, StatusTemperature, StatusHumidity, StatusActive, PowerAC, SensorOK = false;

//  The next few paramenters are needed to create a night of sleep.
//  The night it's divided by NightPart with duration, kind , temperature and deviation in % (for sensor use)
//  First coold down( NightStartCool) for the time defined
//  Then works for a while (NightSleepFirst)
//  Stop for a while (NightACPause)
//  Finally keep the temperature after a while (NightACFinal)
//  I am planning to use a temperature/humidity probe in the next version as a threshold as well as indroduce a web interface to input those data
//  Since the (Mitsubishi) AC use only up to 6 fractions of hours use always multiple of 10 minutes for the time

int NightStart = 2150;    //When the night start
int NightEnd = 800;      //When the night end : it's needed to reset the "SwitchNight" and "Nightphase" after the end of the night

// Max and min parameters for probe use

int MaxTemp = 31;  // Max temperature to switch on the aircon even in "off" mode
int MinTemp = 25;  // Min temperature to switch off the aircon even in "on" mode, useless in my case
int MaxHumidity = 90;  // Max % of humidity to switch on the aircon even in "off" mode
int MinHumidity = 60;  // Min % of humidity to switch off the aircon even in "on" mode, almost useless in my case
int LowTemperature = 40;
int LowHumidity = 100;
// NightPart  defininition: time , 2nd: Mode (Auto/Dry/Off),

#define NightParts 4

//-------------------------------------------------------------------------------------------
// NightPart  defininition: duration , Mode (Cool/Auto/Dry/Off), temperature , % of variation
// Note that Cool and Auto are both using a "Silent" configuration as set for sleep
//-------------------------------------------------------------------------------------------
String NightPart[NightParts][4] = { {"30", "Auto", "26", "10"},
  {"130", "Dry", "29", "10"},
  {"130", "Off", "30", "30"},
  {"90", "Dry", "29", "10"}
};



int NightDivision[NightParts + 1][2]; // Contain start and end time of every NightPart
double EnvironmentHistory[144][2];     // Number of elements of the history to keep/show (144x10 minutes = 24 hours)
double EnvironmentLastHour[60][2];     // Number of elements of the history to keep/show (144x10 minutes = 24 hours)
int LastMin = 0; //Last minute read
int ActualHistory = 0;              // The Actual moment in time ( used as NightDivision is seein in circle)
int Minutes_Interval = 10;          // The frequency of the data kept ( ex 5 = 5 minutes sample)
int Steps = -1;                      // Used to count the minutes of the interval
int ResetI2CCount = 0;              // Used as check for the I2C routine

// To avoid the problem of Midnight (greater/minor) I calculate the minutes from start to midnight and later shift every time accordingly
int TimeToAdd = 60 * int(2400 - NightStart) / 100 + (NightStart % 100);


int NightPhase = -1;        // Keep track of the phase fo the night
int RunningPhase = -1;      //

// Now translate those paramenters in "AC Time"
uint8_t NStart = int(NightStart / 100) * 6 + int((NightStart % 100) / 10);

bool SwitchNight = false;


const char header_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html><head>
  <title>ESP Night Division</title>
  <link href='https://fonts.googleapis.com/css?family=Acme' rel='stylesheet'>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <default style="text-align: center; font-family:'Acme';"><body style="background-color:powderblue;">
  </head><body>
  <H1>ESP Night Partitions<br></H1>
  <form action="/get"><input type='button' value='History' onclick="window.location.href='/History.html'" /> 
  <input type='button' value='Home' onclick="window.location.href='.\\'" />
  <br>
)rawliteral";
const char index_html[] PROGMEM = R"rawliteral(
<br><h2>New Parameters</h2>
<table >
<tbody>
<form action="/get"><style>table, tr, td {margin-left:auto;margin-right:auto; border: 0px ;}</style>
<table >
<tbody>
<tr>
<td >The night start at :</td>
<td ><select id="Part Number" class="element select" style="width: 4em;" name="NightStartH">
<option value="0">00</option>
<option value="1">01</option>
<option value="2">02</option>
<option value="3">03</option>
<option value="4">04</option>
<option value="5">05</option>
<option value="6">06</option>
<option value="7">07</option>
<option value="8">08</option>
<option value="9">09</option>
<option value="10">10</option>
<option value="11">11</option>
<option value="12">12</option>
<option value="13">13</option>
<option value="14">14</option>
<option value="15">15</option>
<option value="16">16</option>
<option value="17">17</option>
<option value="18">18</option>
<option value="19">19</option>
<option value="20">20</option>
<option value="21">21</option>
<option value="22">22</option>
<option value="23">23</option>
</select><select class="element select" style="width: 4em;" name="NightStartM">
<option value="00">00</option>
<option value="10">10</option>
<option value="20">20</option>
<option value="30">30</option>
<option value="40">40</option>
<option value="50">50</option>
</select></td><td><input type="submit" value="Submit" />
  </form></td>
</tr><tr><form action="/get"><td>The night end at:</td><td><select  class="element select" style="width: 4em;" name="NightEndH">
<option value="0">00</option>
<option value="1">01</option>
<option value="2">02</option>
<option value="3">03</option>
<option value="4">04</option>
<option value="5">05</option>
<option value="6">06</option>
<option value="7">07</option>
<option value="8">08</option>
<option value="9">09</option>
<option value="10">10</option>
<option value="11">11</option>
<option value="12">12</option>
<option value="13">13</option>
<option value="14">14</option>
<option value="15">15</option>
<option value="16">16</option>
<option value="17">17</option>
<option value="18">18</option>
<option value="19">19</option>
<option value="20">20</option>
<option value="21">21</option>
<option value="22">22</option>
<option value="23">23</option>
</select><select class="element select" style="width: 4em;" name="NightEndM">
<option value="00">00</option>
<option value="10">10</option>
<option value="20">20</option>
<option value="30">30</option>
<option value="40">40</option>
<option value="50">50</option>
</td><td></select><input type="submit" value="Submit" /></form></td></tr>
<tr><form action="/get"><label class="description" for="Parts"><td>Part</td></label><td><table><tr>
<td>Number:</td><td>Duration (min):</td><td>Type</td><td>Temperature:</td><td></tr><tr><td>
<select id="Part Number" class="element select" style="width: 4em;" name="PartNumber">
<option value="1">1</option>
<option value="2">2</option>
<option value="3">3</option>
<option value="4">4</option>
<option value="5">5</option>
<option value="6">6</option></td><td>
</select><select name="Duration">
<option value="10">10</option>
<option value="20">20</option>
<option value="30">30</option>
<option value="40">40</option>
<option value="50">50</option>
<option value="60">60</option>
<option value="70">70</option>
<option value="80">80</option>
<option value="90">90</option>
<option value="100">100</option>
<option value="110">110</option>
<option value="120">120</option>
<option value="130">130</option>
<option value="140">140</option>
<option value="150">150</option>
<option value="160">160</option>
<option value="170">170</option>
<option value="180">180</option>
<option value="190">190</option>
<option value="200">200</option>
<option value="210">210</option>
<option value="220">220</option>
<option value="230">230</option>
<option value="240">240</option>
<option value="250">250</option>
<option value="260">260</option>
<option value="270">270</option>
<option value="280">280</option>
<option value="290">290</option>
<option value="300">300</option>
<option value="310">310</option>
<option value="320">320</option>
<option value="330">330</option>
<option value="340">340</option>
<option value="350">350</option>
<option value="360">360</option>
<option value="370">370</option>
<option value="380">380</option>
<option value="390">390</option>
<option value="400">400</option></td><td>
</select><select id="Mode" class="element select" style="width: 4em;" name="Mode">
<option value="Dry">Dry</option>
<option value="Cool">Cool</option>
<option value="Auto">Auto</option>
<option value="Off">Off</option></td><td>
</select> <select id="element_2_4" class="element select" style="width: 4em;" name="Temperature">
<option value="16">16</option>
<option value="17">17</option>
<option value="18">18</option>
<option value="19">19</option>
<option value="20">20</option>
<option value="21">21</option>
<option value="22">22</option>
<option value="23">23</option>
<option value="24">24</option>
<option value="25">25</option>
<option value="26">26</option>
<option value="27">27</option>
<option value="28">28</option>
<option value="29">29</option>
<option value="30">30</option>
<option value="31">31</option>
<option value="32">32</option>
</select></td></td></tr></table><td>
<input type="submit" value="Submit" />
  </form></tr><tr>
  </tbody>
</table>
</html>
)rawliteral";

// -----------------------------------------------------------------------------


// Make up a mac Address and an IP address. Both should be globally unique or
// at least unique on the local network. 
static byte g_abyMyMacAddress[] = {0x5C,0xCF,0x7F,0x78,0x6B,0xC2};
static IPAddress g_MyIPAddress(192,168,1,190);

// The machine to wake up. WOL should already be configured for the target machine. 
// The free windows program "Wake On LAN Ex 2" by Joseph Cox can be useful for testing the remote
// machine is properly configured. Download it here: http://software.bootblock.co.uk/?id=wakeonlanex2
static byte g_TargetMacAddress[] = {0x70,0xAF,0x24,0x4D,0xD8,0x66};



// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
//
//  Start of code      
//
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Wake On Lan
// -----------------------------------------------------------------------------

void SendWOLMagicPacket(byte * pMacAddress)
{
  // The magic packet data sent to wake the remote machine. Target machine's
  // MAC address will be composited in here.
  const int nMagicPacketLength = 102;
  byte abyMagicPacket[nMagicPacketLength] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  byte abyTargetIPAddress[] = { 192, 168, 1, 127 }; // don't seem to need a real ip address.
  const int nWOLPort = 7;
  const int nLocalPort = 8888; // to "listen" on (only needed to initialize udp)

  
  // Compose magic packet to wake remote machine. 
  for (int ix=6; ix<102; ix++)
    abyMagicPacket[ix]=pMacAddress[ix%6];
  
  if (UDP_RawSendto(abyMagicPacket, nMagicPacketLength, nLocalPort, 
  abyTargetIPAddress, nWOLPort) != nMagicPacketLength)
    Serial.println("Error sending WOL packet");
}

int UDP_RawSendto(byte* pDataPacket, int nPacketLength, int nLocalPort, byte* pRemoteIP, int nRemotePort)
{
  int nResult;
  int nSocketId; // Socket ID for Wiz5100

  // Find a free socket id.
  nSocketId = MAX_SOCK_NUM;
  for (int i = 0; i < MAX_SOCK_NUM; i++) 
  {
    uint8_t s = W5100.readSnSR(i);
    if (s == SnSR::CLOSED || s == SnSR::FIN_WAIT) 
   {
      nSocketId = i;
      break;
    }
  }

  if (nSocketId == MAX_SOCK_NUM)
    return 0; // couldn't find one. 

  if (socket(nSocketId, SnMR::UDP, nLocalPort, 0))
  {
    nResult = sendto(nSocketId,(unsigned char*)pDataPacket,nPacketLength,(unsigned char*)pRemoteIP,nRemotePort);
    close(nSocketId);
  } else
    nResult = 0;

  return nResult;
}


// -----------------------------------------------------------------------------
// End WOL part
// -----------------------------------------------------------------------------


//------------------------------------------------------------------
// This function return the time in the form hhmm. Expect the input to be in the same way for InitialTime and in Minute for ToAdd
int TimeSum( int InitialTime,int ToAdd) {
  
int ReturnMinute = ((InitialTime % 100) + ToAdd) % 60;
int ReturnHour = (int ((InitialTime % 100) + ToAdd) / 60+ int (InitialTime/100)) % 24;
int ReturnTotal = 100*ReturnHour + ReturnMinute; 
// Only for test print the result
// printf (" InitialTime", InitialTime," Add this minutes ",ToAdd," Result ",ReturnTotal);
return ReturnTotal;

}
//------------------------------------------------------------------



// -----------------------------------------------------------------------------
// CalculatePhase() Fill one array with time of start and end of every phase
// -----------------------------------------------------------------------------

void CalculatePhase() {
int PhaseTime = NightStart;
  for ( int i=0;i<NightParts;i++) {
    NightDivision[i][0]=PhaseTime;
    PhaseTime=TimeSum(PhaseTime,NightPart[i][0].toInt());
    NightDivision[i][1]=PhaseTime;
  }
  NightDivision[NightParts][0]=PhaseTime;
  // This is the last "empty phase". Ideally in the last part the AC must be already off
  
  if (PhaseTime<NightEnd) { NightDivision[NightParts][1]=NightEnd; }
                 else
                          {  NightDivision[NightParts][1]=PhaseTime;}
  
}
// -----------------------------------------------------------------------------
// End CalculatePhase()
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Natural Time : return a string in a more "natural" form HH:mm from one integer HHmm
// -----------------------------------------------------------------------------
String NaturalTime(int TTime){
  int H= int(TTime/100);
  int M= int(TTime%100);
  String ToReturn= (H>0?(H<10?"0"+String(H,DEC):String(H,DEC)):"00")+":"+(M>0?(M<10?"0"+String(M,DEC):String(M,DEC)):"00");
return ToReturn;
 }
// -----------------------------------------------------------------------------
// End NaturalTime
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Create_Button_html(<variable name>,<Button Name>,<variable value(bool)>) 
// :Create a string contain the HTML of button, take as input the name of the bool variable to use, and the variable itself
// -----------------------------------------------------------------------------

String Create_Button_html(String VariableName,String ButtonName, bool &Flag ) {
String ToReturn = "<form action=\"/get\"><input type=\"hidden\" id='"+VariableName+"' name='"+VariableName+"' value=\"1\"><input style=\"font-size: large;background-color:";
ToReturn+=(Flag?"green":"red");
ToReturn+=";color:white\" type='submit' value='"+ButtonName+"' /></form>";  
return ToReturn;
}
// -----------------------------------------------------------------------------
// End Create_Button_html
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Buttons_html() 
// Create a string contain the HTML of buttons
// -----------------------------------------------------------------------------

String Buttons_html(bool Head) {
String ButtonsToReturn="<head><base target=\"_parent\"></head><html>";
if (Head) {ButtonsToReturn+=" <table><tr>";} else {ButtonsToReturn=" <table><tr>";} 
ButtonsToReturn+=Create_Button_html("StatusTV","TV",StatusTV);
ButtonsToReturn+=Create_Button_html("StatusAC","AC (Manual)",StatusAC);
ButtonsToReturn+=Create_Button_html("SensorOK","Sensor",SensorOK);
ButtonsToReturn+=Create_Button_html("SwitchNight","Night Mode",SwitchNight);
ButtonsToReturn+=Create_Button_html("SwitchNightNow","Night Mode Now",SwitchNight);
ButtonsToReturn+=Create_Button_html("PowerAC","AC Running",PowerAC);
ButtonsToReturn+="</tr></table>";
if (Head) {ButtonsToReturn+="</html>";}
return ButtonsToReturn;
}
// -----------------------------------------------------------------------------
// End Buttons_html
// -----------------------------------------------------------------------------



// -----------------------------------------------------------------------------
// Actual_Param_html() :Create a string contain the HTML of a page showing the running parameters
// -----------------------------------------------------------------------------

String Actual_Param_html() {
  String Temp_String2=(StatusAC?"<br> and manual AC is ON and":"<br> and manual AC is OFF and");
  Temp_String2+=(PowerAC?" AC is ON and <br>":" AC is OFF and <br>");
  Temp_String2+=(SwitchNight?" we are in night mode and":" we are not in night mode and");
  Temp_String2+=(SensorOK?" the sensor is working":" the sensor is not working");
  Temp_String2+=" and I2C has been reset ";
  Temp_String2+=ResetI2CCount;
  Temp_String2+=" times";
  String Temp_String=" ";
  Temp_String="<html><head><style>table#Blk, th, td {  border: 1px solid black;}</style></head><body><pre><span>Now "+NaturalTime((timeClient.getHours()*100)+timeClient.getMinutes())+" the temperature is "+Ambient.t+"&#8451; and the humidity is "+Ambient.h+"% <br> the night start at "+NaturalTime(NightStart);
  Temp_String=Temp_String+" and end at "+NaturalTime(NightEnd)+Temp_String2+"</span></pre><br><table id='Blk'><tr><th>Pt.</th><th>Duration</th><th>Mode</th><th>Temperature</th><th>P.Start</th><th>P.end</th></tr>";
  for (int Pts=0; Pts<NightParts;Pts++)
  {  
  Temp_String+="<tr><th>"+String(Pts+1,DEC)+"</th>";  
  for (int i=0; i<3;i++)
  {
    Temp_String=Temp_String+"<th>";
    Temp_String=Temp_String + NightPart[Pts][i];
    Temp_String=Temp_String+"</th>";
  }
  for (int i=0; i<2;i++)
  {
    Temp_String=Temp_String+"<th>";
    Temp_String=Temp_String + NaturalTime(NightDivision[Pts][i]);
    Temp_String=Temp_String+"</th>";
  }
  Temp_String+="</tr>";
  }
  Temp_String+="</table>";
  return Temp_String;//ToReturn;
}

// -----------------------------------------------------------------------------
// End Actual_Param_html()
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// History_html() :Create a string contain the HTML5 of a page showing the graph of the history
// Use Google open api as the code result lughter, but the client need to have internet connection
// -----------------------------------------------------------------------------



String History_html() {
  float temper,humid=0;
  String Colour=SensorOK?"green":"red";
  String Temp_String="<html><head><style> div {width: '100%';height:500px;}; body {background-color:powderblue;}> </style><script type='text/javascript' src='https://www.gstatic.com/charts/loader.js'></script><script type='text/javascript'>google.charts.load('current', {'packages':['corechart']});google.charts.setOnLoadCallback(drawChart);function drawChart() {";
  Temp_String=Temp_String+"var dataH = google.visualization.arrayToDataTable([['Time', 'Temperature', 'Humidity'],";
  // Note: the graph is create on a max of 24 hours: no date is taken in consideration
  int TimeG=TimeSum((timeClient.getHours()*100)+timeClient.getMinutes(),1440-(Minutes_Interval*144));
  // For a better visualization I use only multiple of 10 minutes but if needed a shorter interval just remove the next line
  TimeG=10*int(TimeG/10);
  for (int i=0;i<144;i++){
    // Note: ActualHistory+1 contains the oldest value in the array  (Circular)
    TimeG=TimeSum(TimeG,Minutes_Interval);  
    // Temper and humid are used only to makes the structure more readable
    temper=EnvironmentHistory[(ActualHistory+1+i)%144][0]; 
    humid=EnvironmentHistory[(ActualHistory+1+i)%144][1];      
    Temp_String=Temp_String+"['"+NaturalTime(TimeG)+"',"+temper+","+humid+"]";
    if (i<143) {Temp_String=Temp_String+",";}
    }
    Temp_String=Temp_String+"]);var dataHm = google.visualization.arrayToDataTable([['Time', 'Temperature', 'Humidity'],";
    TimeG=TimeSum((timeClient.getHours()*100)+LastMin,1440-60);// This is useful for time near to the midnight,alternatively was sufficient to subtract 1 hour, but I am lazy ;)  
    for (int i=0;i<60;i++){
    // Note: ActualHistory+1 contains the oldest value in the array  (Circular)
    TimeG=TimeSum(TimeG,1);  
    // Temper and humid are used only to makes the structure more readable
    temper=EnvironmentLastHour[(LastMin+1+i)%60][0]; 
    humid=EnvironmentLastHour[(LastMin+1+i)%60][1];      
    Temp_String=Temp_String+"['"+NaturalTime(TimeG)+"',"+temper+","+humid+"]";
    if (i<59) {Temp_String=Temp_String+",";}
    }
    
    Temp_String=Temp_String+"]);var options = {title: 'Temperature graph',curveType: 'function',legend: { position: 'bottom' }};var dataT=dataH.clone();dataT.removeColumn(2);dataH.removeColumn(1);var dataTm=dataHm.clone();dataTm.removeColumn(2);dataHm.removeColumn(1);var chart = new google.visualization.LineChart(document.getElementById('Temperature'));chart.draw(dataT,options); chart=new google.visualization.LineChart(document.getElementById('Temperaturem'));chart.draw(dataTm,options); chart=new google.visualization.LineChart(document.getElementById('Humidity'));options.title='Humidity graph';chart.draw(dataH, options);chart = new google.visualization.LineChart(document.getElementById('Humiditym'));chart.draw(dataHm, options);}</script></head><body><h1 style='text-align:center;color:"+Colour+";' >History</h1><div style='text-align:center;height:50px;'><input type='button' value='Modify' onclick=\"window.location.href='\\index.html'\" /> <input type='button' value='Home' onclick=\"window.location.href='.\\\\'\" /></div> <div id='Temperaturem'></div><div id='Temperature'></div><div id='Humiditym'></div><div id='Humidity'></div></body></html>";
 return Temp_String;//ToReturn;
}

// -----------------------------------------------------------------------------
// End History_html()
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// wifiSetup(): Setup the Wifi
// -----------------------------------------------------------------------------

void wifiSetup() {

    // Set WIFI module to STA mode
    WiFi.mode(WIFI_STA);

    // Connect
    Serial.printf("[WIFI] Connecting to %s ", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    // Wait
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(100);
    }
    Serial.println();

    // Connected!
    Serial.printf("[WIFI] STATION Mode, SSID: %s, IP address: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

}
// -----------------------------------------------------------------------------
// End wifiSetup()
// -----------------------------------------------------------------------------



// -----------------------------------------------------------------------------
// serverSetup()
// Initialize a Web server. This is used to input/modify the night parameters for the Airconditioner and for one easy TV setup test
// -----------------------------------------------------------------------------

void serverSetup() {
    
    // Entry point for the webserver
    WebServer.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", header_html+Buttons_html(true)+"<br><H2> Actual Parameters </H2> <br>"+Actual_Param_html()+index_html );    
    });

    WebServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", "<!DOCTYPE html><html ><head ><link href='https://fonts.googleapis.com/css?family=Gugi' rel='stylesheet'><style>body,table,td {font-family: 'Gugi';font-size: 22px;margin-left:auto;margin-right:auto; border: 0px ;text-align:center;background-color:powderblue;&}</style><title>Actual Temperature and Humidity</title><script type='text/javascript' src='https://www.gstatic.com/charts/loader.js'></script><script type='text/javascript'>  google.charts.load('current', {'packages':['gauge']});  google.charts.setOnLoadCallback(drawChart);function drawChart() {var dataHumid = google.visualization.arrayToDataTable([['Label', 'Value'],['Humid',"+String(Ambient.h,DEC)+"],      ]);    var dataTemp = google.visualization.arrayToDataTable([['Label','Value'],['Temp', "+String(Ambient.t,DEC)+"]]);var optionsHumid={width:300,height:300,redFrom:85,redTo:100,yellowFrom:65,yellowTo:85,greenFrom:40,greenTo:65,minorTicks:10,majorTicks:['0','10','20','30','40','50','60','70','80','90', '100'],};var formatter = new google.visualization.NumberFormat({suffix:'%',fractionDigits: 1});formatter.format(dataHumid,1);var optionsTemp={min:0,max:40,width:300,height:300,redFrom:34, redTo:40,yellowFrom:29,yellowTo:34,greenFrom:19,greenTo:29,minorTicks: 10,majorTicks:['0','10','20','30','40'],};var formatter = new google.visualization.NumberFormat({suffix:' C',fractionDigits:1});formatter.format(dataTemp,1);var chartHumid=new google.visualization.Gauge(document.getElementById('chart_Humid'));var chartTemp = new google.visualization.Gauge(document.getElementById('chart_Temp'));chartHumid.draw(dataHumid, optionsHumid,formatter);chartTemp.draw(dataTemp, optionsTemp, formatter);}</script></head><body><h2> Actual Parameters </h2><table><td class='gauge' id='chart_Humid'></td><td class='gauge' id='chart_Temp'></td></table>"+Buttons_html(false)+"<table> "+Actual_Param_html()+"</table><input type='button' value='Modify' onclick=\"window.location.href='\\index.html'\" /> <input type='button' value='History' onclick=\"window.location.href='\\History.html'\" /></body></html>");    
    });

     WebServer.on("/Actual_Param.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", Actual_Param_html() );
    });
     WebServer.on("/History.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", History_html() );
    });
    WebServer.on("/buttons.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", Buttons_html(true) );
    });
    //------------------------------------------------------------------
    // These two callbacks are required for gen1 and gen3 compatibility

    WebServer.onNotFound([](AsyncWebServerRequest *request) {
        String body = (request->hasParam("body", true)) ? request->getParam("body", true)->value() : String();
        if (!espalexa.handleAlexaApiCall(request)) //if you don't know the URI, ask espalexa whether it is an Alexa control request
      {
        //whatever you want to do with 404s
        request->send(404, "text/plain", "Not found");
      }
    });

    //------------------------------------------------------------------
    // Now process the requests from the Webpages to modify the night parameters
    // Refer to  Rui Santos project details at https://RandomNerdTutorials.com/esp32-esp8266-input-data-html-form/
    
    WebServer.on("/get", HTTP_GET, [] (AsyncWebServerRequest *request) {
    String inputMessage;
    String inputParam;
    // GET input1 value on <ESP_IP>/get?input1=<inputMessage>
    if (request->hasParam("NightStartM")) {
      inputMessage = 100*(request->getParam("NightStartH")->value().toInt())+(request->getParam("NightStartM")->value().toInt());
      inputParam = "NightStart";      
      NightStart= inputMessage.toInt();
      TimeToAdd = 60*int(2400-NightStart)/100+ (NightStart % 100);
      RunningPhase = -1;
      if (SwitchNight) {SwitchSleep = true;}
    }
    else if (request->hasParam("NightEndM")) {
      inputMessage = 100*(request->getParam("NightEndH")->value().toInt())+request->getParam("NightEndM")->value().toInt();
      inputParam = "NightEnd";
      NightEnd= inputMessage.toInt();
      RunningPhase = -1;      
      if (SwitchNight) {SwitchSleep = true;}
    } 
    else if (request->hasParam("PartNumber")) {
      inputMessage = request->getParam("PartNumber")->value();
      inputParam = "PartNumber";
      NightPart[inputMessage.toInt()-1][0]= request->getParam("Duration")->value();
      NightPart[inputMessage.toInt()-1][1]= request->getParam("Mode")->value();
      NightPart[inputMessage.toInt()-1][2]= request->getParam("Temperature")->value();
      RunningPhase = -1;
      if (SwitchNight) {SwitchSleep = true;}
      // Debug only
      Serial.print("Night Part:");
      Serial.println(inputMessage);   
      Serial.print("Duration:");      
      Serial.println(NightPart[inputMessage.toInt()-1][0]);     
      Serial.print("Mode:");
      Serial.println(NightPart[inputMessage.toInt()-1][1]);            
      Serial.print("Temperature: ");
      Serial.println(NightPart[inputMessage.toInt()-1][2]);
      // End debug
    }
    else if (request->hasParam("SwitchNight")) {
    inputMessage = request->getParam("SwitchNight")->value();
      inputParam = "SwitchNight";
      SwitchNight = !SwitchNight;
      Night_Mode(SwitchNight?255:0);
    }
    else if (request->hasParam("SwitchNightNow")) {
    inputMessage = request->getParam("SwitchNightNow")->value();
      inputParam = "SwitchNightNow";
      SwitchNight = !SwitchNight;
      if (SwitchNight) {
              NightStart= (timeClient.getHours()*100)+10*int(timeClient.getMinutes()/10);
              TimeToAdd = 60*int(2400-NightStart)/100+ (NightStart % 100);
              }
      Night_Mode(SwitchNight?255:0);
    }
    else if (request->hasParam("StatusAC")) {
    inputMessage = request->getParam("StatusAC")->value();
      inputParam = "StatusAC";
      StatusAC= !StatusAC;
      AC_Power(StatusAC?255:0);
      }
    else{
      inputMessage = "No message sent";
      inputParam = "none";
    }
    
  
    request->send(200, "text/html", "<html><head><meta http-equiv=\"refresh\" content=\"0;url=.\\index.html\" /><title></title></head><body>Executed successfully <br><a href=\"/\">Return to Home Page</a></body></html>");
                                     
    CalculatePhase();
    });
    Night_ModeAx = new EspalexaDevice(AC_Sleep, Night_Mode); 
    AC_PowerAx = new EspalexaDevice(AC_Bedroom, AC_Power);
    BR_TVAx = new EspalexaDevice(TV_Bedroom, BR_TV);
    BR_DvdAx = new EspalexaDevice(DVD_Player, BR_Dvd);
    BR_TemperatureAx = new EspalexaDevice(ID_Temperature, BR_Temperature);
    BR_HumidityAx = new EspalexaDevice(ID_Humidity, BR_Humidity);
    espalexa.addDevice(Night_ModeAx); 
    espalexa.addDevice(AC_PowerAx);
    espalexa.addDevice(BR_TVAx);
    espalexa.addDevice(BR_DvdAx);
    espalexa.addDevice(BR_TemperatureAx);
    espalexa.addDevice(BR_HumidityAx);
    

    
    // Start the WebServer
    espalexa.begin(&WebServer);
    
    // Start the WebServer
    WebServer.begin();

}

//-----------------------------------------------------------------
// ACMode set the different parameters of the airconditioned based on Mode ( Dry/Off/Auto) and the temperature defined. 

void AcMode(String ModeAC, int Temperature, bool TimerOn , uint8_t StartTime , uint8_t EndTime ) {
  ac.setTemp(Temperature);
  if (TimerOn) 
    {
    ac.setTimer(kMitsubishiAcStartStopTimer);
    ac.setStartClock(StartTime);
    ac.setStopClock(EndTime);  
      }
    else 
    {
    ac.setTimer(kMitsubishiAcNoTimer);
    }
  if (ModeAC=="Dry") {
     // case "Dry"
     ac.on();
     PowerAC = true; // The AC is running
     //ac.setFan(1);
     ac.setMode(kMitsubishiAcDry);
     ac.setVane(kMitsubishiAcVaneAutoMove);
     ac.setFan(kMitsubishiAcFanSilent);
  } else
  if (ModeAC=="Cool") {
     //case "Cool"
     ac.on();
     //ac.setFan(1);
     ac.setMode(kMitsubishiAcCool); 
     ac.setVane(kMitsubishiAcVaneAutoMove);
     ac.setFan(kMitsubishiAcFanAuto);
     PowerAC = true; // The AC is running
  } else
  if (ModeAC=="Off") {
     //case "Off"
     ac.off();
     PowerAC = false; // The AC is not running     
  } else
  if (ModeAC=="Auto") {
     // case "Auto"
     ac.on();
     PowerAC = true; // The AC is running
     //ac.setFan(1);
     ac.setMode(kMitsubishiAcCool);  //kMitsubishiAcAuto doesn't work on my model for some reason
     ac.setVane(kMitsubishiAcVaneAutoMove);
     ac.setFan(kMitsubishiAcFanAuto);
     
  }
  ac.send();
  delay(600);

}
//-----------------------------------------------------------------

//-----------------------------------------------------------------
// Print on serial the parameters set for the IR 
// Use this only for debug on serial. Not needed in production
void printState() {
  // Display the settings.
  Serial.println("Mitsubishi A/C remote is in the following state:");
  Serial.printf("  %s\n", ac.toString().c_str());
  // Display the encoded IR sequence.
  unsigned char* ir_code = ac.getRaw();
  Serial.print("IR Code: 0x");
  for (uint8_t i = 0; i < kMitsubishiACStateLength; i++)
    Serial.printf("%02X", ir_code[i]);
  Serial.println();
}
//------------------------------------------------------------------


//------------------------------------------------------------------
// Mitsubishi threat the time in "1/6" of hour. Different model may need to modify this function

uint8_t TimeConvert( int ToConvert) {
 return (int(ToConvert/100)*6+int((ToConvert % 100)/10));
 }

//----------------------------------------------------------------------------------------------



//------------------------------------------------------------------
// Setup and initialize
void setup() {
  Wire.begin(SDA_PIN,SCL_PIN); // Initialize the I2C communication with SDA on D4 (2), SCL on D3 (0) 
  Ambient.Read();
  ac.begin(); 
  delay(200);
  Serial.begin(SERIAL_BAUDRATE);
  Serial.println();
  Serial.println();

  // Create the array with start and end time of each phase
  CalculatePhase();
  
  // Set up what we want to send. See ir_Mitsubishi.cpp for all the options.
  Serial.println("Default state of the remote.");
  printState();
  Serial.println("Setting desired state for A/C.");

  // Now I'll define the standard parameters of the AC. This is not strictly necessary
   
  ac.off();
  ac.setFan(1);
  ac.setMode(kMitsubishiAcDry);
  ac.setTemp(29);
  ac.setVane(kMitsubishiAcVaneAutoMove);
  ac.setFan(kMitsubishiAcFanSilent);
  ac.setTimer(kMitsubishiAcNoTimer);
  
  // Init serial port and clean garbage
    
    // Wifi
    wifiSetup();

    // Initialize the WebServer 

    // The TCP port must be 80 for gen3 devices (default is 1901)
    // This has to be done before the call to enable()
    serverSetup();
    Ethernet.begin(g_abyMyMacAddress, g_MyIPAddress);
    
    // You can use different ways to invoke alexa to modify the devices state:
    // "Alexa, turn TVBed lamp on"
    // "Alexa, turn on TVBed lamp
    // "Alexa, set TVBed lamp to fifty" (50 means 50% of brightness, note, this example does not use this functionality)

    
//Initialize the Time stamp
timeClient.begin();
timeClient.update();

uint8_t CurrentTime = (timeClient.getHours()*6)+int(timeClient.getMinutes()/10);
ac.setClock(CurrentTime);
AC_PowerAx->setValue(0);
Night_ModeAx->setValue(0);
Ambient.Read();
}

   
//AC_Power

void AC_Power(uint8_t brightness){
StatusActive=true; 
StatusAC=(brightness>0)?true:false;
if (brightness>0) {
      ac.on();
      // Set the temperature to 27 degrees or a % of the "light" (16 degree = 1, 32 degree = 254
      ac.setTemp(((brightness<255)?16+int(brightness/16):27));
      SwitchAC = true;
      PowerAC = true; // The AC is running
      AC_PowerAx->setValue(brightness);
      
} else     {
      // The request is to switch off
      // I don't look at all the other parameters
      ac.off();             
      SwitchAC = true; 
      PowerAC = false; // The AC is not running
      AC_PowerAx->setValue(0);
           }                        
Serial.print(" Switching the AC : ");
Serial.println(PowerAC?"On":"off");
    
}
//AC_power end

//Night_Mode 

void Night_Mode(uint8_t brightness)
{
StatusActive = true;   
StatusSleep=(brightness>0)?true:false;
SwitchSleep = true;
RunningPhase = -1;
SwitchNight = (brightness>0)?true:false;
LowTemperature = Ambient.t;  // Note that if the sensor is Offline this is Zero during the whole time
LowHumidity = Ambient.h ; // Note that if the sensor is Offline this is Zero during the whole time
Serial.print(" Switching NightMode to ");
Serial.println(SwitchNight? "On":"Off");
Night_ModeAx->setValue(SwitchNight?255:0);
}
   
//Night_Mode end


//BR_Temperature 

void BR_Temperature(uint8_t brightness)
{
StatusActive = true;  
StatusTemperature = SensorOK; // Not so useful at the moment
BR_TemperatureAx->setValue(brightness);
Serial.print(" Temperature is");
Serial.println(Ambient.t);
}
   
//BR_Temperature end


//BR_Humidity 

void BR_Humidity(uint8_t brightness)
{
StatusActive = true;  
StatusHumidity = SensorOK; // Not so useful at the moment
BR_HumidityAx->setValue(brightness);
Serial.print(" Humidity is");
Serial.println(Ambient.h);
}
   
//BR_Humidity end

//BR_TV 

void BR_TV(uint8_t brightness)
{
StatusActive = true;  
StatusTV = !StatusTV; // Switch the status on & off 
Serial.print(" TV is");
Serial.println(StatusTV?"On":"Off");
}
   
//BR_Temperature end

//BR_DVD 

void BR_Dvd(uint8_t brightness)
{
StatusActive = false;   
StatusDVD = !StatusDVD; // Switch the status on & off 
Serial.print(" Dvd is");
Serial.println(StatusDVD?"On":"Off");
}
   
//BR_DVD end

     



void loop() {

    // EspAlexa uses an async TCP server but a sync UDP server
    // Therefore, we have to manually poll for UDP packets
    
    espalexa.loop();

    // This is a sample code to output free heap every 5 seconds
    // This is a cheap way to detect memory leaks
    static unsigned long last = millis();
    
    if ((millis() - last > 60000)||(SwitchSleep)) { 
        //Check every 60 seconds or immediately if the sleep mode has just been switched (on/off)
        // Do this routin every minute ( 60000 / 1000 milliseconds )
        // This parameters can be increased easily to 300 seconds without affecting the results except the 1st run as the AC works in 1/6th of hours
        // However change this affect the sampling later on
        if (millis() - last > 60000) { last = millis(); Steps=(Steps+1)%Minutes_Interval;}
        Serial.printf("[MAIN] Free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.println("Chip = AM2320");
        
        // Debug Start
        //
        
        switch(Ambient.Read()) {
        case 2:
        Serial.println(" CRC failed");
        SensorOK= false; // The sensor is not working
        break;
        case 1:
        Serial.println(" Sensor offline");
        SensorOK=false; // The sensor is not working
        BR_Humidity(0);
        BR_Temperature(0);
        break;
        case 0:
        Serial.print(" Humidity = ");
        Serial.print(Ambient.h);
        Serial.println("%");
        BR_Humidity(int(Ambient.h*2.55));
        Serial.print(" Temperature = ");
        Serial.print(Ambient.t);
        BR_Temperature(int(Ambient.t*2.55)); // Bring the value in percent of 255
        Serial.println("*C");
        SensorOK=true; // The sensor is working
        break;
        }  // End Switch
        Serial.print("Steps = ");
        Serial.println(Steps);
        // Keep the data of last hour: please note that if the interval of check is more than 60 sec, the function needs to be modified
        LastMin=timeClient.getMinutes();
        EnvironmentLastHour[LastMin][0]=Ambient.t;
        EnvironmentLastHour[LastMin][1]=Ambient.h; 
        if (Steps==0) {
        // Keep the data of the last 144*Minutes_Interval minutes in a circular array
        Serial.print("Recording data number");
        Serial.println(ActualHistory);        
        ActualHistory=(ActualHistory+1)%144; 
        EnvironmentHistory[ActualHistory][0]=Ambient.t;
        EnvironmentHistory[ActualHistory][1]=Ambient.h;
        }
        // Debug End
        
        if (SwitchNight) 
                         { // We are in NightMode
                           timeClient.update();
                           int ReadableTime = (timeClient.getHours()*100)+timeClient.getMinutes();
                           int CurrentTimeNorm = TimeSum(ReadableTime,TimeToAdd); 
                           ac.setClock(TimeConvert(ReadableTime));
                           LowHumidity=LowHumidity<Ambient.h?LowHumidity:Ambient.h;  // I keep tack of the lowest humidity
                           LowTemperature=LowTemperature<Ambient.t?LowTemperature:Ambient.t; // I keep tack of the lowest temperature
                           /*Serial.print(" Current Time normalized is ");
                           Serial.println(CurrentTimeNorm);
                           Serial.print(" The Time to add is ");
                           Serial.println(TimeToAdd);
                           */
                           // We are in night mode, need to identify in which phase
                              for (int i=0;i<=NightParts;i++)
                                    {
                                if ((CurrentTimeNorm>TimeSum(NightDivision[i][0],TimeToAdd)) && (CurrentTimeNorm<TimeSum(NightDivision[i][1],TimeToAdd)))
                                        { NightPhase = i;  }
                                    }
                            // Uncomment if need for debug
                            /*
                            Serial.print("RunningPhase is");
                            Serial.println(RunningPhase);
                            Serial.print("NightPhase is");
                            Serial.println(NightPhase);                          
                            */
                            //
                           if (RunningPhase<NightPhase)
                              { if ((NightPhase==NightParts)||(NightPart[NightPhase][1]=="Off")) 
                                {                      
                                  // Since is set by timer the off signal is useless
                                  // AcMode("Off",NightPart[NightPhase][2].toInt(),false,0,0);
                                  // Serial.println("Do I really need to switch off?");
                                  if (NightPhase==NightParts)
                                    { // The night is finished, time to reset everything
                                      SwitchNight= false;
                                      RunningPhase = -1;
                                      NightPhase = -1;
                                      AcMode("Off",29,false,0,0);
                                      StatusAC= false;
                                      PowerAC=false;
                                      AC_PowerAx->setValue(0);
                                      Night_ModeAx->setValue(0);
                                    }                        
                                  }                   
                                else
                                {
                                  // I send a command to the aircon only if I need to set a timer
                                  if ((NightPart[NightPhase][1]!="Off") && (NightPhase!=-1)) {
                                    AcMode(NightPart[NightPhase][1],NightPart[NightPhase][2].toInt(),true,TimeConvert(NightDivision[NightPhase][0]),TimeConvert(NightDivision[NightPhase][1])); 
                                    printState();
                                    //Serial.println("I am supposed to send something");
                                    RunningPhase = NightPhase;
                                    } else if ((!PowerAC) && (NightPhase!=-1) && (SensorOK)) { //We are in a phase of the night when the AC is off, let's check if the environment is becoming unconfortable   
                                    if (((100*((Ambient.t-LowTemperature)/Ambient.t))>NightPart[NightPhase][3].toInt()) || ((Ambient.h-LowHumidity)>NightPart[NightPhase][3].toInt())) {
                                      // We are out of the paramenters defined. Let's switch on the AC for 30 min with the settings of the previous phase
                                      // Note that it's assumed that there will be not two "Off" phase in sequence as this makes no sense
                                      AcMode(NightPart[NightPhase-1][1],NightPart[NightPhase-1][2].toInt(),true,TimeConvert(ReadableTime),TimeConvert(TimeSum(ReadableTime,30))); // 
                                      PowerAC=true;
                                    }
                                    }
                                }                   
                             RunningPhase = NightPhase;
                             }
                         
                         SwitchSleep = false;    
                         } 
                 else if ((SwitchSleep) && (!StatusActive)) {SwitchSleep = false;} //Some changes made to the time, this is just a check      
        if (!SensorOK) {//Reset the I2C if the sensor it's not working
                        Serial.print(" The I2C doesn't reply properly.. see here : ");
                        Serial.print(digitalRead(SCL_PIN));    //should be HIGH
                        Serial.println(digitalRead(SDA_PIN));   //should be HIGH, is LOW on stuck I2C bus
                        if(digitalRead(SCL_PIN) == HIGH && digitalRead(SDA_PIN) == LOW) {
                              Serial.println("reset");
                              pinMode(SDA_PIN, OUTPUT);      // is connected to SDA
                              digitalWrite(SDA_PIN, LOW);
                              delay(1000);              //maybe too long
                              pinMode(SDA_PIN, INPUT);       // reset pin
                              delay(50);
                              Serial.print(digitalRead(SCL_PIN));    //should be HIGH
                              Serial.println(digitalRead(SDA_PIN));  //should be HIGH, is LOW on stuck I2C bus
                              Wire.begin(SDA_PIN,SCL_PIN);
                              } 
                       ResetI2CCount=ResetI2CCount+1;       
                       } //Reset the I2C
                         
          }
    if (StatusActive) { // Do the check only if something is happened
    StatusActive = false;  
    // Switch the AC ON/OFF ( not depending on timer)
    if (SwitchAC) {
      ac.setVane(kMitsubishiAcVaneAutoMove);
      ac.setFan(kMitsubishiAcFanAuto);
      ac.setTimer(kMitsubishiAcNoTimer);
      ac.setMode(kMitsubishiAcCool);
      ac.send();
      delay(500);
      SwitchAC= false;
      AC_PowerAx->setValue(StatusAC?0:255);
      printState();
    } 
   
     // We are not in NightMode but the switch has just been triggered means that we need to switch off everything
    if ((SwitchSleep) && (!SwitchNight)) {
      StatusAC= false;
      ac.off();
      ac.send();
      PowerAC=false;
      delay(500);
      printState();
      SwitchSleep = false;
      RunningPhase = -1;
      NightPhase= -1;
      AC_PowerAx->setValue(0);
      Night_ModeAx->setValue(0);
    }
    }
                  
    // If your device state is changed by any other means (MQTT, physical button,...)
    // you can instruct the library to report the new state to Alexa on next request:
    // fauxmo.setState(TV_Bedroom, true, 255);
    delay(50); // Let the cpu take a rest...
} 

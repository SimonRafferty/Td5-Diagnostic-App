/*
Td5Comm.cpp -
 
 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 
 */

#include <Arduino.h>

// ESP32 / ESP32-S3 board support
#if defined(ESP32)
  #include <HardwareSerial.h>
  // XIAO ESP32S3 pin definitions
  #define K_OUT 2   // GPIO2 = D1 on XIAO ESP32S3 (TX to L9637D)
  #define K_IN  1   // GPIO1 = D0 on XIAO ESP32S3 (RX from L9637D)

  // Use Hardware Serial1 for K-Line
  HardwareSerial obdSerial(1);  // UART1

  // Flag to track if serial has been initialized with pin mapping
  static bool obdSerialInitialized = false;

  // Override begin() to use pin mapping on ESP32
  #define obdSerial_begin(baud) do { \
    obdSerial.begin(baud, SERIAL_8N1, K_IN, K_OUT); \
    obdSerialInitialized = true; \
  } while(0)

#elif defined ARDUINO_BLACK_F407VE || defined ARDUINO_BLACK_F407ZE || defined ARDUINO_BLACK_F407ZG || defined ARDUINO_FK407M1 || defined ARDUINO_GENERIC_F407VE || defined ARDUINO_DIYMORE_F407VGT // F4 based boards on STM core
  #include "LRDuinoDefs407VE.h"
  #define obdSerial Serial3
  #define obdSerial_begin(baud) obdSerial.begin(baud)

#elif defined ARDUINO_MAPLEMINI_F103CB || defined ARDUINO_BLUEPILL_F103CB || defined ARDUINO_BLUEPILL_F103C8 || defined ARDUINO_BLACKPILL_F303CC // F1 based boards, either core
  #include "LRDuinoDefsMM.h"
  #define obdSerial Serial3
  #define obdSerial_begin(baud) obdSerial.begin(baud)

#else
  #error "Unsupported board - please add definitions for your platform"
#endif

//Uncomment to see ECU response via USB Serial.
//#define _DEBUG_

#include "td5comm.h"
#include "keygen.h"

// =============================================================================
// PID DEFINITIONS - Service 0x21 (ReadDataByLocalIdentifier)
// Names are LEGACY - see comments for actual content based on Nanocom captures
// =============================================================================
const unsigned char pid_0x00[] = { 0x81, 0x13, 0xF7, 0x81, 0x0C };        // INIT_FRAME - Fast init sequence
const unsigned char pid_0x01[] = { 0x02, 0x10, 0xA0, 0x00 };              // START_DIAG - Start diagnostic session
const unsigned char pid_0x02[] = { 0x02, 0x27, 0x01, 0x00 };              // REQ_SEED - Request seed for auth
const unsigned char pid_0x03[] = { 0x04, 0x27, 0x02, 0x00, 0x00, 0x00 };  // SEND_KEY - Send key response
const unsigned char pid_0x04[] = { 0x02, 0x21, 0x20, 0x00 };              // START_FUELLING - PID 0x20: Injection quantity
const unsigned char pid_0x05[] = { 0x02, 0x21, 0x09, 0x00 };              // ENGINE_RPM - PID 0x09: Engine RPM (2 bytes)
const unsigned char pid_0x06[] = { 0x02, 0x21, 0x1A, 0x00 };              // TEMPERATURES - PID 0x1A: Coolant temp + MAP + Boost (composite, 18 bytes)
const unsigned char pid_0x07[] = { 0x02, 0x21, 0x1C, 0x00 };              // INLET_PRES_MAF - PID 0x1C: Actually Inlet Air Temp + Fuel Temp (composite, 10 bytes) - NAME IS WRONG
const unsigned char pid_0x08[] = { 0x02, 0x21, 0x10, 0x00 };              // BATTERY_VOLT - PID 0x10: Battery voltage + Reference voltage (6 bytes, millivolts)
const unsigned char pid_0x09[] = { 0x02, 0x21, 0x23, 0x00 };              // AMBIENT_PRES - PID 0x23: Actually Accelerator Tracks (LITTLE-ENDIAN!) - NAME IS WRONG
const unsigned char pid_0x0A[] = { 0x02, 0x21, 0x0D, 0x00 };              // VEHICLE_SPEED - PID 0x0D: Vehicle speed (1 byte, km/h)
const unsigned char pid_0x0B[] = { 0x02, 0x21, 0x1B, 0x00 };              // THROTTLE_POS - PID 0x1B: Actually Airflow (g/s) + Ambient Pressure (kPa) - NAME IS WRONG
const unsigned char pid_0x0C[] = { 0x03, 0x30, 0xC0, 0xF0, 0x00 };        // IO_CONTROL - Output control
const unsigned char pid_0x0D[] = { 0x02, 0x21, 0x40, 0x00 };              // INJ_BALANCE - PID 0x40: Cylinder fuel trim (5 cylinders, signed)
const unsigned char pid_0x0E[] = { 0x02, 0x21, 0x21, 0x00 };              // RPM_ERROR - PID 0x21: Digital inputs (brake/clutch/handbrake switches)
const unsigned char pid_0x0F[] = { 0x02, 0x21, 0x37, 0x00 };              // EGR_MOD - PID 0x37: EGR position (% * 100)
const unsigned char pid_0x16[] = { 0x02, 0x21, 0x1E, 0x00 };              // CRUISE_SWITCHES - PID 0x1E: Cruise/Brake switches (Nanocom-validated)
const unsigned char pid_0x10[] = { 0x02, 0x21, 0x38, 0x00 };              // ILT_MOD - PID 0x38: Wastegate position (% * 100)
const unsigned char pid_0x11[] = { 0x02, 0x21, 0x38, 0x00 };              // TWG_MOD - PID 0x38: Duplicate of wastegate (legacy)
const unsigned char pid_0x12[] = { 0x02, 0x3E, 0x01, 0x00 };              // KEEP_ALIVE - Service 0x3E: Tester present
const unsigned char pid_0x13[] = { 0x02, 0x21, 0x3B, 0x00 };              // FAULT_CODES - PID 0x3B: Read DTCs
const unsigned char pid_0x14[] = { 0x14, 0x31, 0xDD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };              // CLEAR_FAULTS
const unsigned char pid_0x15[] = { 0x02, 0x21, 0x1D, 0x00 };              // FUELLING - PID 0x1D: Fuelling data

const byte *td5_pids[] =
{
  pid_0x00, pid_0x01, pid_0x02, pid_0x03, pid_0x04, pid_0x05, pid_0x06, pid_0x07,
  pid_0x08, pid_0x09, pid_0x0A, pid_0x0B, pid_0x0C, pid_0x0D, pid_0x0E, pid_0x0F,
  pid_0x10, pid_0x11, pid_0x12, pid_0x13, pid_0x14, pid_0x15, pid_0x16
};

Td5Pid pidInitFrame(INIT_FRAME, 5, 5);
Td5Pid pidStartDiag(START_DIAG, 4, 3);
Td5Pid pidRequestSeed(REQ_SEED, 4, 6);
Td5Pid pidSendKey(SEND_KEY, 6, 4);
// cycleTime reduced to 0 - let the main loop control polling rate
// The library's Td5RequestDelay (55ms) provides minimum inter-request timing
Td5Pid pidRPM(ENGINE_RPM, 4, 6, 0);
Td5Pid pidTurboPressureMaf(INLET_PRES_MAF, 4, 12, 0);
Td5Pid pidTemperatures(TEMPERATURES, 4, 20, 0);
Td5Pid pidBatteryVoltage(BATTERY_VOLT, 4, 8, 0);
Td5Pid pidAmbientPressure(AMBIENT_PRES, 4, 8, 0);
Td5Pid pidStartFuelling(START_FUELLING, 4, 8);
Td5Pid pidKeepAlive(KEEP_ALIVE, 4, 3, 1500);  // Keep-alive still needs rate limiting
Td5Pid pidFaultCodes(FAULT_CODES, 4, 39);
Td5Pid pidResetFaults(CLEAR_FAULTS, 22, 4);
Td5Pid pidInjectorsBalance(INJ_BALANCE, 4, 14, 0);
Td5Pid pidVehicleSpeed(VEHICLE_SPEED, 4, 5, 0);
Td5Pid pidThrottlePosition(THROTTLE_POS, 4, 14, 0);
Td5Pid pidDigitalInputs(RPM_ERROR, 4, 6, 0);   // 0x21 - brake/clutch/handbrake switches
Td5Pid pidCruiseBrakeSwitches(CRUISE_SWITCHES, 4, 6, 0);  // 0x1E - cruise control switches
Td5Pid pidEGR(EGR_MOD, 4, 6, 0);
Td5Pid pidILT(ILT_MOD, 4, 6, 0);
Td5Pid pidTWG(TWG_MOD, 4, 6, 0);
Td5Pid pidFuelling(FUELLING, 4, 22);

///////////////////////////////////////////////////
//              Generic functions                //
///////////////////////////////////////////////////
void debug_log_byte(byte b)
{
  // say what you got:
  if (b < 16) 
  {
    Serial.print('0'); 
  }        
  Serial.print(b, HEX); 
  Serial.print(' '); 
}

void debug_log_frame(byte *datasent, byte sentlen, byte *datarecv, byte recvlen)
{
  byte i;

  for(i=0; i<sentlen; i++)
    debug_log_byte(datasent[i]);

  Serial.print(' ');  

  for(i=0; i<recvlen; i++)
    debug_log_byte(datarecv[i]);

  Serial.println();  
}

void retrieve_keys_from_eeprom(uint8_t *seed, uint8_t *key)
{
  keyBytes_t myKey;
  myKey.low_byte = seed[1];  //might need to switch order
  myKey.high_byte = seed[0];
  keyGenerate(&myKey);
  key[1] = myKey.low_byte; 
  key[0] = myKey.high_byte; 
}

void keyGenerate(keyBytes_t * key) {
    uint16_t seed = key->keyword;
    uint16_t tmp = 0;
    uint8_t count = 0;
    uint8_t idx;
    uint8_t tap = 0;
    count = ((seed >> 0xC & 0x8) | (seed >> 0x5 & 0x4) | (seed >> 0x3 & 0x2) | (seed & 0x1)) + 1;
    for (idx = 0; idx < count; idx++) {    
        tap = ((seed >> 1 ) ^ (seed >> 2 ) ^ (seed >> 8 ) ^ (seed >> 9 )) & 1;   
        tmp = ((seed >> 1) | (tap << 0xF));      
        if ((seed >> 0x3 & 1) && (seed >> 0xD & 1)) {
            seed = tmp & ~1; 
        } else {
            seed = tmp | 1;
        }
    }
    key->keyword = seed;    
}


///////////////////////////////////////////////////
//                 Class Td5Comm                 //
///////////////////////////////////////////////////
Td5Comm::Td5Comm()
{
  lastReceivedPidTime = 0;
  initStep = 0;
  lostFrames = 0;
  consLostFrames = 0;
  initTime = 0;
  ecuConnection = false;
  newDataAvailable = false;
}

void Td5Comm::init()
{
  // init pinouts
  digitalWrite(K_OUT, HIGH); // write it high before enabling with pinmode ensures we don't drop the k-line low before our code initialises.
  pinMode(K_OUT, OUTPUT);
  pinMode(K_IN, INPUT);
}

bool Td5Comm::read_byte(byte * b)
{
  int readData;
  bool success = true;
  byte t=0;

  while(t != READ_ATTEMPTS  && (readData=obdSerial.read())==-1) 
  {
    delay(1);
    t++;
  }

  if (t >= READ_ATTEMPTS) 
  {
    success = false;
  }

  if (success)
  {
    *b = (byte) readData;
  }
  return success;
}

void Td5Comm::write_byte(byte b)
{
  obdSerial.write(b);
  delay(Td5RequestByteDelay);  // ISO requires 5-20 ms delay between bytes (but the TD5 ECU doesn't care so we set it to 1ms)
}

int8_t Td5Comm::getPid(Td5Pid* pid)
{
  bool gotData = false;
  byte responseIndex = 0;
  byte dataCaught = '\0';  
  byte dataLen = 0;
  byte fluff = '\0';
  
  if (pid->id != INIT_FRAME)
    dataLen = pid->requestFrame[0] + 2;
  else
    dataLen = 5;

  long currentTime = millis();

  if ((currentTime >= (lastReceivedPidTime + Td5RequestDelay)) && (currentTime >= (pid->lastSeenTime + pid->cycleTime)))
  {
#if defined(ESP32)
    // Flush any stale RX bytes (e.g. a previous response that arrived just
    // after its 300ms timeout) so this request's reply is not read out of a
    // desynced buffer. Without this, fast back-to-back polling reads the
    // PREVIOUS PID's response -> every parameter shows another PID's bytes.
    while (obdSerial.available()) obdSerial.read();
#endif
    // Send the message
    pid->requestFrame[dataLen-1] = checksum(pid->requestFrame, dataLen-1);

    for (byte i = 0; i < dataLen; i++)
    {
      write_byte(pid->requestFrame[i]);
      read_byte(&fluff); // read in the echoed char of each TX (effectively ignores it). Need to comment this out if not using k-line, as this will break responses on raw uart (sent bytes are not echod back on rx as they ar with k-line)
    }
    
    // Wait for response for 300 ms
    long waitResponseTime = currentTime + 300;
    do
    {
      // If we find any data, keep catching it until it ends
      while(read_byte(&dataCaught) && (responseIndex < pid->responseLength))
      {
        gotData = true;
        pid->responseFrame[responseIndex] = dataCaught;
        responseIndex++;
      }
    }
    // Keep waiting until a COMPLETE Td5 frame has arrived, not just the first
    // burst. Byte 0 is the frame length; a full frame is (length + 2) bytes.
    // Without this, a long response that arrives in bursts (composite PIDs
    // 0x1A/0x1B/0x1C/0x1D and the 39-byte fault-code frame) is cut at the first
    // inter-byte gap; its tail then desyncs the next request, so decoded values
    // alternate between correct and garbage.
    while (millis() <= waitResponseTime
           && (!gotData || responseIndex < (int)pid->responseFrame[0] + 2)
           && (responseIndex < pid->responseLength));

    if (gotData && (responseIndex > 1))
    {
      lastReceivedPidTime = millis();
      pid->lastSeenTime = lastReceivedPidTime;
      consLostFrames = 0;
      #ifdef _DEBUG_        
      debug_log_frame(pid->requestFrame, dataLen, pid->responseFrame, responseIndex);
      #endif
      if(checksum(pid->responseFrame, responseIndex-1) == pid->responseFrame[responseIndex-1])      
      {
        if(pid->responseFrame[1] != 0x7F) 
        {
          return responseIndex;
        }
        else
        {
          return PID_NEGATIVE_ANSWER; // negative response
        }
      }
    }
  }
  else 
  {
    return PID_NOT_READY;
  }

  #ifdef _DEBUG_
  Serial.println("Lost frame detected");
  #endif
  lostFrames += 1;
  consLostFrames += 1;
  return PID_LOST_FRAME;      
}

bool Td5Comm::ecuIsConnected()
{
  return ecuConnection; 
}

bool Td5Comm::newDataIsAvailable()
{
  if(newDataAvailable)
  {
    newDataAvailable = false;
    return true;
  }
  else
    return false;
}

unsigned long Td5Comm::getLastReceivedPidTime()
{
  return lastReceivedPidTime;
}

unsigned long Td5Comm::getLastReceivedPidElapsedTime()
{
  return (millis() - lastReceivedPidTime);
}

int Td5Comm::getLostFrames()
{
  return lostFrames;
};

int Td5Comm::getConsecutiveLostFrames()
{
  return consLostFrames;
};

byte Td5Comm::checksum(byte *data, byte len)
{
  byte crc=0;
  for(byte i=0; i<len; i++)
    crc=crc+data[i];
  return crc;
}

void Td5Comm::setInitStep(byte init_step)
{
  initStep = init_step;  
}

byte Td5Comm::getInitStep()
{
  return initStep;  
}

void Td5Comm::initComm()
{
  unsigned long currentTime = millis();

  switch (initStep)
  {
  case 0:
    // setup
    ecuConnection = false;
    initTime = currentTime;
    Serial.println("[TD5] Init step 0: Starting initialization");
    initStep++;
    break;
  case 1:
    if (currentTime >= initTime)
    {
      // Ensure K-line is idle (HIGH) for 300ms before fast init
      #if defined(ESP32)
      // On ESP32, end any existing serial first
      obdSerial.end();
      pinMode(K_OUT, OUTPUT);
      #endif
      digitalWrite(K_OUT, HIGH);
      Serial.println("[TD5] Init step 1: K-line HIGH for 300ms");
      initTime = currentTime + 300;
      initStep++;
    }
    break;
  case 2:
    if (currentTime >= initTime)
    {
      // Fast init pulse: LOW for 25ms
      digitalWrite(K_OUT, LOW);
      Serial.println("[TD5] Init step 2: K-line LOW for 25ms (fast init pulse)");
      initTime = currentTime + 25;
      initStep++;
    }
    break;
  case 3:
    if (currentTime >= initTime)
    {
      // Fast init pulse: HIGH for 25ms (TD5 specification)
      digitalWrite(K_OUT, HIGH);
      Serial.println("[TD5] Init step 3: K-line HIGH for 25ms");
      initTime = currentTime + 25;  // Changed from 118 to 25ms per TD5 spec
      initStep++;
    }
    break;
  case 4:
    if (currentTime >= initTime)
    {
      // Switch to 10400 baud serial
      Serial.println("[TD5] Init step 4: Starting serial at 10400 baud");
      obdSerial_begin(10400);
      delay(30);  // Let serial stabilize

      // Send init frame and wait for response
      Serial.println("[TD5] Sending init frame...");
      int8_t result = getPid(&pidInitFrame);
      if (result <= 0)
      {
        Serial.print("[TD5] Init frame failed, result: ");
        Serial.println(result);
        initStep = 0;
        break;
      }
      Serial.println("[TD5] Init frame OK");

      lastReceivedPidTime = currentTime;
      initTime = currentTime + Td5RequestDelay;
      initStep++;
    }
    break;
  case 5:
    if (currentTime >= initTime)
    {
      Serial.println("[TD5] Init step 5: Sending StartDiag...");
      if (getPid(&pidStartDiag) <= 0)
      {
        Serial.println("[TD5] StartDiag failed");
        initStep = 0;
        break;
      }
      Serial.println("[TD5] StartDiag OK");
      lastReceivedPidTime = currentTime;
      initTime = currentTime + Td5RequestDelay;
      initStep++;
    }
    break;
  case 6:
    if (currentTime >= initTime)
    {
      Serial.println("[TD5] Init step 6: Requesting seed...");
      if (getPid(&pidRequestSeed) <= 0)
      {
        Serial.println("[TD5] Seed request failed");
        initStep = 0;
        break;
      }
      Serial.print("[TD5] Seed received: 0x");
      Serial.print(pidRequestSeed.getResponseByte(3), HEX);
      Serial.println(pidRequestSeed.getResponseByte(4), HEX);

      lastReceivedPidTime = currentTime;
      initTime = currentTime + Td5RequestDelay;
      initStep++;
    }
    break;
  case 7:
    if (currentTime >= initTime)
    {
      uint8_t seed[2], key[2];
      seed[0] = pidRequestSeed.getResponseByte(3);
      seed[1] = pidRequestSeed.getResponseByte(4);
      retrieve_keys_from_eeprom(seed, key);
      pidSendKey.setRequestByte(key[0], 3);
      pidSendKey.setRequestByte(key[1], 4);

      Serial.print("[TD5] Init step 7: Sending key: 0x");
      Serial.print(key[0], HEX);
      Serial.println(key[1], HEX);

      if (getPid(&pidSendKey) <= 0)
      {
        Serial.println("[TD5] Key rejected!");
        initStep = 0;
        break;
      }

      Serial.println("[TD5] Authentication successful!");
      lastReceivedPidTime = currentTime;
      initTime = currentTime + Td5RequestDelay;

      delay(55);
      ecuConnection = true;
      initStep = 0;
    }
    break;
  }
}

bool Td5Comm::connectToEcu(bool showBar)
{
  initStep = 0;
  do
  {
    initComm();
  }
  while (initStep != 0);
  
  initTime = 0;
  return ecuConnection;
}

void Td5Comm::disconnectFromEcu()
{
  obdSerial.end();
  lostFrames = 0;
  ecuConnection = false; 
}


int Td5Comm::getFaultCodes()
{
  faultCodesCount = 0;
 
  if(getPid(&pidFaultCodes) > 0)
  {
    for(int i=0;i<35;i++)
    {
      byte fault_code_byte = 0;
      fault_code_byte = pidFaultCodes.getResponseByte(i+3);
      for(int j=0;j<=7;j++)
      {
        if(bitRead(fault_code_byte,j))
        {
          faultCodesCount += 1;
        }        
      }
    }
    return faultCodesCount;
  }
  return -1;
}

int Td5Comm::getFaultCode(int index)
{
  int fault_codes_cnt = 0;
  byte fault_code_byte = 0;

  if(index < MAX_FAULT_CODE)
  {
    for(int i=0;i<35;i++)
    {
      fault_code_byte = pidFaultCodes.getResponseByte(i+3);
      for(int j=0;j<=7;j++)
      {
        if(bitRead(fault_code_byte,j))
        {
          if(fault_codes_cnt == index)
          {
            return ((i*8)+j);   
          }
          else
          {
            fault_codes_cnt += 1;
          }
        }        
      }
    }
  }
  return -1;  
}

int8_t Td5Comm::resetFaults()
{
  return getPid(&pidResetFaults);
}


///////////////////////////////////////////////////
//                 Class Td5Pid                  //
///////////////////////////////////////////////////
Td5Pid::Td5Pid(byte ID, byte reqlen, byte resplen, long cycletime)
{
  id = ID;
  cycleTime = cycletime;
  lastSeenTime = 0;
  
  responseLength = resplen;  

  requestFrame= (byte *)malloc(sizeof(byte) * reqlen);
  memcpy(requestFrame, td5_pids[id],reqlen);
  responseFrame = (byte *)malloc(sizeof(byte) * resplen);
}

bool Td5Pid::getValue(float *fvalue, byte index)
{
  bool dataValid = false;
  uint16_t value;
  
  switch(id)
  {
    case AMBIENT_PRES:
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      *fvalue = (float) value / 10000.0;
      dataValid = true;
      break;
    case TEMPERATURES:     // 1/10 Celsius degrees
      value = (uint16_t)(((responseFrame[3 + (index * 4)] * 256L) + responseFrame[4 + (index * 4)]) - 2732L);
      *fvalue = (float) value / 10.0;
      dataValid = true;
      break;
    case INLET_PRES_MAF:   // 1/10000 bar
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      switch(index)
      {
        case 0:
        case 1:
          *fvalue = (float) value / 10000.0;    
          break;
        case 2:
        case 3:
          *fvalue = (float) value / 10.0;    
          break;
      }
      dataValid = true;
      break;
    case BATTERY_VOLT:     // 1/1000 Volt
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      *fvalue = (float) value / 1000.0;
      dataValid = true;
      break;
    case THROTTLE_POS:     // 1/1000 Volt   
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      *fvalue = (float) value / 1000.0;
      dataValid = true;
      break;
    case EGR_MOD:         // %   
    case ILT_MOD:         // %   
    case TWG_MOD:         // %   
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      *fvalue = (float) value / 10.0;
      dataValid = true;
      break;

    default: // return value incompatible with this pid
      *fvalue = 0.0;
      dataValid = false;
      break;
  }

  return dataValid;
}

float Td5Pid::getfValue(byte index)
{
  uint16_t value;
  
  switch(id)
  {
    case AMBIENT_PRES:
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      return ((float) value / 10000.0);
    case TEMPERATURES:     // 1/10 Celsius degrees
      value = (uint16_t)(((responseFrame[3 + (index * 4)] * 256L) + responseFrame[4 + (index * 4)]) - 2732L);
      return ((float) value / 10.0);
    case INLET_PRES_MAF:   // 1/10000 bar
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      switch(index)
      {
        case 0:
        case 1:
          return ((float) value / 10000.0);    
        case 2:
        case 3:
          return ((float) value / 10.0);    
      }
    case BATTERY_VOLT:     // 1/1000 Volt
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      return ((float) value / 1000.0);
    case THROTTLE_POS:     // 1/1000 Volt   
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      return ((float) value / 1000.0);
    case EGR_MOD:         // %   
    case ILT_MOD:         // %   
    case TWG_MOD:         // %       
      value = (uint16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      return ((float) value / 1000.0);
    case FUELLING:   // 1/10000 bar
      value = (uint16_t)((responseFrame[3 + (index)] * 256L) + responseFrame[4 + (index)]); // fixed a bug in luca72's code here, index was being multiplied by 2 which meant the wrong values were being returned - may need correcting for other non tested PIDs
      switch(index)
      {
        case 0:
        case 1:
          return ((float) value / 100.0); // Driver Demand   
        case 2:
        case 3:
          return ((float) value / 100.0); // MafAirmass
        case 4:
        case 5:
          return ((float) value / 10.0);  // MapAirMass
        case 6:
        case 7:
          return ((float) value / 100.0); // Injection Quantity
        case 8:
        case 9:
          return ((float) value / 100.0); // AfRatio
        case 10:
        case 11:
          return ((float) value / 100.0); // TorqueLimit
        case 12:
        case 13:
          return ((float) value / 100.0); // SmokeLimit
        case 14:
        case 15:
          return ((float) value / 100.0); // IdleDemand                              
      }

    default: // return value incompatible with this pid
      return 0.0;
  }
}


bool Td5Pid::getValue(uint16_t *value, byte index)
{
  bool dataValid = false;
  
  switch(id)
  {
    case ENGINE_RPM:       // rpm
      *value = (uint16_t)((responseFrame[3] * 256L) + responseFrame[4]);
      dataValid = true;
      break;
    case RPM_ERROR:       // rpm
      *value = (uint16_t)((responseFrame[3] * 256L) + responseFrame[4]);
      dataValid = true;
      break;

    default: // return value incompatible with this pid
      *value = 0L;
      dataValid = false;
      break;
  }

  return dataValid;
}


bool Td5Pid::getValue(int *value, byte index)
{
  bool dataValid = false;
  
  switch(id)
  {
    case INJ_BALANCE:       // injectors balance
      *value = (int)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      dataValid = true;
      break;

    default: // return value incompatible with this pid
      *value = 0;
      dataValid = false;
      break;
  }

  return dataValid;
}


uint16_t Td5Pid::getulValue(byte index)
{
  switch(id)
  {
    case ENGINE_RPM:       // rpm
      return((uint16_t)((responseFrame[3] * 256L) + responseFrame[4]));
      break;
    case RPM_ERROR:       // rpm
      return((uint16_t)((responseFrame[3] * 256L) + responseFrame[4]));
      break;
    case INJ_BALANCE:       // injectors balance
      return(int)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      break;      

    default: // return value incompatible with this pid
      return 0L;
  }
}

int16_t Td5Pid::getlValue(byte index)
{
  switch(id)
  {
    case ENGINE_RPM:       // rpm
      return(int16_t)((responseFrame[3] * 256L) + responseFrame[4]);
      break;
    case RPM_ERROR:       // rpm
      return(int16_t)((responseFrame[3] * 256L) + responseFrame[4]);
      break;
    case INJ_BALANCE:       // injectors balance
      return(int16_t)((responseFrame[3 + (index * 2)] * 256L) + responseFrame[4 + (index * 2)]);
      break;      

    default: // return value incompatible with this pid
      return 0L;
  }
}

byte Td5Pid::getbValue(byte index)
{
  switch(id)
  {
    case VEHICLE_SPEED:       // vehicle speed
      return((byte)(responseFrame[3]));

    default: // return value incompatible with this pid
      return 0;
  }
}

void Td5Pid::setRequestByte(byte value, byte pos)
{
  requestFrame[pos] = value;    
}

byte Td5Pid::getResponseByte(byte pos)
{
  return responseFrame[pos];    
}

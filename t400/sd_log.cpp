#include "Arduino.h"  // for boolean type
#include "t400.h"
#include "sd_log.h"
#include "fmt.h"

#if SD_LOGGING_ENABLED
#include "src/SdFat/SdFat.h"
#endif

#include <avr/wdt.h>

extern uint8_t temperatureUnit;

namespace sd {

#if SD_LOGGING_ENABLED
SdFat sd;
SdFile file;
#endif

uint32_t syncTime      = 0;     // time of last sync(), in millis()


#if 0
// NOTE: This code will make subdirectories to get around a max file
// limit.  the problem is, mkdir takes up around 1k of flash!!! So we
// are not going to use it

#define MAX_DIRECTORIES     100
#define MAX_FILES           100
//#define DEBUG_FILE_SEARCH
bool find_filename(uint8_t * dirid_ptr, uint8_t * fileid_ptr)
{
    char outbuff[30];
    uint8_t dirid,fileid;

    // Start in root
    sd.chdir(1);

    #ifdef DEBUG_FILE_SEARCH
    sprintf(outbuff,"Search dir\n");
    Serial.print(outbuff);
    #endif
    for(dirid=1;dirid<MAX_DIRECTORIES;dirid++)
    {
        // Look for folder DATAx, if it exists see if we can add a file, if
        // it doesn't exist, make it and select this with file id = 1

        #ifdef DEBUG_FILE_SEARCH
        sprintf(outbuff,"Try dir %d\n",dirid);
        Serial.print(outbuff);
        #endif

        sprintf(outbuff,"DATA%d",dirid);
        if(!sd.chdir(outbuff,false))
        {
            // Folder doesn't exist, make it
            if(!sd.mkdir(outbuff,false))
            {
                #ifdef DEBUG_FILE_SEARCH
                sprintf(outbuff,"mkdir failed %d\n",dirid);
                Serial.print(outbuff);
                #endif
                break;
            }
            // We did make the directory
            fileid = 1;
            #ifdef DEBUG_FILE_SEARCH
            sprintf(outbuff,"mkdir good %d\n",dirid);
            Serial.print(outbuff);
            #endif
            break;
        }else{
            // Folder does exist, see how many files are in here

            // Actually go into the folder
            sd.chdir(outbuff,true);

            for(fileid=1;fileid<MAX_FILES;fileid++)
            {
                sprintf(outbuff,"FILE%d",fileid);
                if(!sd.exists(outbuff))
                {
                    // This file doesn't exist, use it
                    //Serial.print(outbuff);
                    #ifdef DEBUG_FILE_SEARCH
                    if(!file.open(outbuff, O_CREAT | O_WRITE | O_EXCL))
                    {
                        Serial.print("write err\n");
                    }
                    file.clearWriteError();
                    file.print("Test");
                    file.println();
                    file.flush();
                    file.close();

                    sprintf(outbuff,"Use file %d\n",fileid);
                    Serial.print(outbuff);
                    #endif

                    break;
                }
            }
            if(fileid<99) break;

            // if we have over 100 files, go to next directory

            #ifdef DEBUG_FILE_SEARCH
            Serial.print("Over 100 files\n");
            #endif

            // got back to root
            sd.chdir(1);

        }
    }

    *dirid_ptr = dirid;
    *fileid_ptr = fileid;

    // got back to root
    sd.chdir(1);

    if(dirid>=99) return false;

    #ifdef DEBUG_FILE_SEARCH
    sprintf(outbuff,"Dir:%d File:%d\n",dirid,fileid);
    Serial.print(outbuff);
    #endif

    return true;
}
#endif

// Column headers for the log file and the serial stream: "time (s), timestamp, temp_0 (C), ..."
static void printHeader(Print& out) {
  out.print(F("time (s), timestamp"));
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    out.print(F(", temp_"));
    out.print(i);
    out.print(F(" ("));
    out.print(unitLetter(temperatureUnit));
    out.print(')');
  }
  out.println();
}

// Build "LDxxxx.CSV" for the given index
static void makeLogName(char* name, uint16_t i) {
  name[0] = 'L';
  name[1] = 'D';
  fmtInt(name + 2, i, 4, '0');
  strcpy_P(name + 6, PSTR(".CSV"));
}

void init() {
  close();
  #if SD_LOGGING_ENABLED
  if (!sd.begin(SD_CS, SPI_FULL_SPEED)) {
//    error_P("card.init");
    return;
  }
  #endif
}

bool open(char* fileName)
{
  // Create LDxxxx.CSV for the lowest value of x.

  #if SD_LOGGING_ENABLED

  uint16_t i = 0;
  // NOTE: Could jump by 100 here, take up like 60 bytes of flash
#if 0
  do{
      i+=100;
      makeLogName(fileName, i);
  }while(sd.exists(fileName));
  // We now know that value doesn't exist, go back 100 and search from here
  i-=100;
#endif
  do{
      i+=10;
      makeLogName(fileName, i);
      // This could take a while, so reset the watchdog here
      wdt_reset();
  }while(sd.exists(fileName));
  // We now know that value doesn't exist, go back 10 and search from here
  i-=10;
  do{
      i+=1;
      makeLogName(fileName, i);
  }while(sd.exists(fileName));

  if(!file.open(fileName, O_CREAT | O_WRITE | O_EXCL)) {
//    error_P("file open");
    return false;
  }
  file.clearWriteError();

  // write data header
  printHeader(file);
  file.flush();
  #endif
  #if SERIAL_OUTPUT_ENABLED
  printHeader(Serial);
  #endif

  #if SD_LOGGING_ENABLED
  return (file.getWriteError() == false);
  #else
    return true;
  #endif
}

void close() {
  #if SD_LOGGING_ENABLED
  file.close();
  #endif
}

bool log(char* message) {
  write(message);
  return endRow();
}

void write(const char* text) {
  // TODO: Test if file is open first
  #if SD_LOGGING_ENABLED
  file.print(text);
  #endif
}

bool endRow() {
  #if SD_LOGGING_ENABLED
  file.println();
  #endif

  sync(false);

  #if SD_LOGGING_ENABLED
  return (file.getWriteError() == false);
  #else
    return true;
  #endif
}

void sync(boolean force) {
  // TODO: Test if file is open first?

  //don't sync too often - requires 2048 bytes of I/O to SD card

  if (!force && (millis() - syncTime) <  SYNC_INTERVAL) {
    return;
  }

  syncTime = millis();
  #if SD_LOGGING_ENABLED
  file.flush();
  #endif
}

} // namespace sd

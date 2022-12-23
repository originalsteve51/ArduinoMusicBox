//-----------------------------------------------
// 12/16/2022 v1.00 Basic music box as wired and sent in box
// 12/17/2022 v1.01 Added simple non-interrupt vol ctl
// 12/23/2022 v1.02 Made vol ctl interrupt driven, removed obsolete code,
//                  Added LED flash when skipping, enabled forever playing,
//                  and generally cleaned up code
//-----------------------------------------------

// What's our version?
#define VERSION 1.02

// Some useful constants
#define UNDEFINED_VALUE -999

// Pins 2 and 3 are hardware interrupt enabled on the Nano. 
// The following define explicitly tells the Encoder to use interrupts 
// generated as the rotary encoder is turned.
// The volume control works best with this setting. Still it is sluggish. I'm
// working on a solution to sluggishness. Might just be a hardware limitation
// using a Nanoboard! Must be commented out if you do not want to use interrupts.
#define ENCODER_USE_INTERRUPTS
#define ENCODER_DT_PIN 2
#define ENCODER_CLK_PIN 3

// The following define tells Encoder to use polling of the pins 2 and 3 (or
// whatever pins are specified...) It works, but is very sluggish!
// Comment it out when you want interrupts to be used.
// #define ENCODER_DO_NOT_USE_INTERRUPTS

#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Encoder.h>

// I use the real-time clock just so I can learn about it. Someday I might make
// an alarm clock using the RTC. Now it just serves to randomize song selection.
#include <RTClib.h>
RTC_DS3231 rtc;

// Pins 10 and 11 are used to provide a software serial port. This allows me to
// use the 'real' serial port for attachment to the Arduino development system on
// my Macbook.
#define SOFT_SERIAL_RX 10
#define SOFT_SERIAL_TX 11
SoftwareSerial mySoftwareSerial(SOFT_SERIAL_RX, SOFT_SERIAL_TX);

// The hardware mp3 player, which includes many functions that are selected by sending
// commands through the software serial port. These are provided by calling an api
// found in the DFRobotDFPlayerMini header file.
DFRobotDFPlayerMini myDFPlayer;

// A rotary quadrature encoder is used as a digital volume control. The DT and CLK
// connections are assigned when the Encoder is instantiated.
Encoder myEnc(ENCODER_DT_PIN, ENCODER_CLK_PIN);
long oldPosition  = UNDEFINED_VALUE;

// The control button is assigned to a digital pin
#define PAUSE_PLAY_SKIP_BTN 8

// The following entry/exit trace is very verbose. This is controlled here so it can be
// suppressed unless it is needed.
// #define PAUSE_PLAY_SKIP_ENTRY_EXIT_TRACE
// #define ENTRY_EXIT_TRACE

// Initial volume in range 0 - 30
#define PRESET_VOLUME 20

// LEDs to indicate volume limits are assigned to following digital out pins
#define VOLUME_UP_LED 12
#define VOLUME_DOWN_LED 13

// Forward declarations because I want to place these troublesome functions
// at the end of the file where I can easily locate them!!
bool checkPausePlaySkipBtn();
void readEncoder();
void flashLEDs();

// After skipping a track, wait IGNORE_AFTER_SKIPPING_SECONDS before accepting another 
// button command. This is to allow a track to be skipped without pausing it immediately.
#define IGNORE_AFTER_SKIPPING_SECONDS 4

// Global value used to keep track of how long it has been since a track was skipped.
// This is used to block pause button actions for IGNORE_AFTER_SKIPPING_SECONDS
// after skipping. Without ignoring, a pause (short button press) occurs immediately
// after a long button press. It is global so it keeps its value as checkPausePlaySkipBtn()
// is called many times while looping.
// 
long g_skippedTime = 0;

// Debounce the button used to go back to a previous track
bool debounce = false;
bool paused = false;

// Array with {folder, track, played_flag} tuples
// These are the tracks that are randomly selected by folder and track in folder. 
// When a track is played, a flag
// value initially false (0) is set true (1) and this is used to prevent repeated
// playing of the same track during a session.
// When all tracks have been played, the played flags are all reset to false and the
// first track is played followed by randomly selected tracks ad-infinitum.
#define NUMBER_OF_TRACKS 55
uint8_t g_tracklist[NUMBER_OF_TRACKS][3] = 
    { {1,3,0}, {2,3,0}, {3,2,0}, {5,1,0}, {7,1,0},
      {1,4,0}, {2,4,0}, {3,4,0}, {5,2,0}, {7,4,0}, 
      {1,5,0}, {2,5,0}, {3,5,0}, {5,4,0}, {7,6,0}, 
      {1,6,0}, {2,7,0}, {3,7,0}, {5,5,0}, {1,7,0}, 
      {2,8,0}, {3,8,0}, {5,6,0}, {1,8,0}, {2,11,0}, 
      {3,9,0}, {5,7,0}, {1,9,0}, {2,12,0}, {3,10,0}, 
      {5,8,0}, {1,10,0}, {2,14,0}, {3,14,0}, {5,9,0}, 
      {1,11,0}, {2,15,0}, {5,14,0}, {2,18,0}, {1,15,0}, 
      {2,19,0}, {1,16,0}, {2,20,0}, {1,17,0}, {2,22,0}, 
      {5,10,0}, {2,23,0}, {1,21,0}, {5,11,0}, {1,22,0},
      {5,13,0}, {1,14,0}, {5,16,0}, {5,17,0}, {5,6,0}
    };

                                                                  
void setup () 
{
  // Delay for 1 second to give the system time to 'boot up', otherwise
  // the serial link fails to instantiate
  delay(1000);

  // Initialize a soft serial link to control the mp3 player
  mySoftwareSerial.begin(9600);

  // Configure the hardware serial link to the programming host 
  // where code is developed
  Serial.begin(115200);

  Serial.print(F("Music box Version "));
  Serial.println(VERSION);
  Serial.println(F("Initializing mp3 device ..."));
  
  
  // Use SoftwareSerial to communicate with mp3 player.
  if (!myDFPlayer.begin(mySoftwareSerial)) 
  {  
    Serial.println(F("Unable to start mp3 device:"));
    Serial.println(F("1.Please recheck the connections!"));
    Serial.println(F("2.Please be sure the SD card is in place!"));

    // Block here (forever), i.e. until unit is restarted
    while(true);
  }
  Serial.println(F("mp3 device is online."));

  // Set initial volume
  myDFPlayer.volume(PRESET_VOLUME);  

  // Fire up the real-time clock
  // @TODO Need to retry a limited number of times and issue final fail msg if not started
  while (!rtc.begin()) 
  {
    Serial.println(F("Couldn't start real time clock module"));
    delay(1000);
  }

  // Set up a button to pause/resume (short press) or skip (long press) on a digital pin 
  // normally high (i.e. that goes low when pressed)
  pinMode(PAUSE_PLAY_SKIP_BTN, INPUT_PULLUP);

  // Set up LEDs that show when volume limits have been reached 
  pinMode(VOLUME_UP_LED, OUTPUT);
  pinMode(VOLUME_DOWN_LED, OUTPUT);
}

//--------
// Play a track from a folder. This call blocks until the track is either finished
// playing or it is skipped because the user long-pressed the function button.
//--------
void playTrackNum(uint16_t folderNum, uint16_t num)
{
#ifdef ENTRY_EXIT_TRACE  
  Serial.println(F("=====> playTrackNum entry"));
#endif
  Serial.print(F("Play track: "));
  Serial.print(num, DEC); 
  Serial.print(F(" from folder: "));
  Serial.println(folderNum, DEC);
  myDFPlayer.playFolder(folderNum, num);
  
  bool finished = false;
  while(!finished)
  {
    // The player status can be read when it's not playing. The available() function
    // tells us when we can read status. Block in this loop until notified 
    // that track has finished.
    if (myDFPlayer.available()) 
    {
      int status = myDFPlayer.readType();
      Serial.print(F("Player available, status is: "));
      Serial.println(status, DEC);
      if (status == DFPlayerPlayFinished)
      {
        finished = true;
        break;
      }
    }
    else
    {
      // If the player is not available, it is still playing a track. As long as it is 
      // playing we can check the function button for user actions (short or long presses.)
      // We also can check the rotary digital volume control.
      //
      // Check and respond to the pause/play/skip button while the track is still playing.
      // This call returns a flag indicating whether to stop playing the current song and to play
      // another instead.
      finished = checkPausePlaySkipBtn();

      // Read and respond to changes to the rotary digital volume control.
      readEncoder();
    }
    

  } // end while(!finished)
#ifdef ENTRY_EXIT_TRACE  
  Serial.println(F("<===== playTrackNum exit"));
#endif
}

void playTimeRandomizedTrk(uint8_t nextTrk)
{
    // Mix up the order of play so it is less monotonous...
    // Use real time seconds to do this.
    nextTrk+=rtc.now().second();

    // Make sure we don't overrun the number of tracks available.
    while (nextTrk>NUMBER_OF_TRACKS-1)
    {
      nextTrk-=NUMBER_OF_TRACKS-1;
    }

    // Look for an unplayed track. The randomly selected track will play unless it
    // has already played. If already played, the next unplayed track is located to play,
    //
    // The tracklist entries are of the form {folder, track, played} where played is 
    // a boolean-valued field indicating if a track was already played.
    uint8_t numberChecked = 0;
    while (g_tracklist[nextTrk][2] == 1)
    {
      numberChecked++;
      Serial.print(F("Track already played, choosing another: "));
      Serial.println(nextTrk, DEC);
      nextTrk++;
      if(nextTrk>NUMBER_OF_TRACKS-1)
      {
        // Wrap the search if the end if reached.
        nextTrk-=NUMBER_OF_TRACKS-1;
      }
      
      if (numberChecked == NUMBER_OF_TRACKS)
      {
        // All tracks have been played. Mark all unplayed and play the first.
        Serial.println(F("All tracks have been played. Start over with the first!"));
        for(int idx=0; idx<NUMBER_OF_TRACKS; idx++)
        {
          g_tracklist[idx][2] = 0; 
        }
        blinkLEDs(10);
        // Play the first track in the list.
        nextTrk = 0;
      }
    }

    Serial.print(F("Play track index: "));
    Serial.println(nextTrk, DEC);

    // Set the flag for the selected track to show it has been played
    g_tracklist[nextTrk][2] = 1;

    // Play the randomly selected track. This call blocks during the time that the
    // track is played. 
    playTrackNum(g_tracklist[nextTrk][0], g_tracklist[nextTrk][1]);
}

void playTracksBasedOnTime()
{
#ifdef ENTRY_EXIT_TRACE  
  Serial.println(F("=====> playTracksBasedOnTime entry"));
#endif
  DateTime now = rtc.now();
  Serial.print(F("DateTime-checking loop started at: "));
  Serial.print(now.hour(), DEC);
  Serial.print(":");
  Serial.println(now.minute(), DEC);

  uint8_t trackIdx = -1;

  // Weirdly use the real time clock to randomly select a track.
  trackIdx = rtc.now().minute();

  // There are 55 tracks in the first version. Adjust the starting track accordingly.
  if (trackIdx>54)
  {
    trackIdx = trackIdx - 54;
  }
  
  playTimeRandomizedTrk(trackIdx);
  
#ifdef ENTRY_EXIT_TRACE  
  Serial.println(F("<===== playTracksBasedOnTime exit"));
#endif
}


void loop () 
{
#ifdef ENTRY_EXIT_TRACE  
  Serial.println(F("=====> loop entry"));
#endif
  blinkLEDs(1);
  playTracksBasedOnTime();
#ifdef ENTRY_EXIT_TRACE  
  Serial.println(F("<===== loop exit"));
#endif
}

//--------
// There is one button that has three functions: Pause, Resume after pause (play), and skip.
// Short (pause/play) and long (skip) button presses are detected here.
// See comments in body for details.
//--------
bool checkPausePlaySkipBtn()
{
  #ifdef PAUSE_PLAY_SKIP_ENTRY_EXIT_TRACE
  Serial.println(F("=====> checkPausePlaySkipBtn entry"));
  #endif

  // The return code tells the caller when to stop playing the current track and to start a new
  // track. After a short press return false to cause a pause to occur without starting a new
  // track. When paused and a short press occurs, return false and the current track resumes.
  // After a long press, return true to tell the caller to break the loop that controls the
  // current track playback. The control loop will then restart with a new track.
  bool rc = false;

  // Button presses must be debounced. I could have used a Button library that did this for me but
  // I wanted to try it myself. Maybe next time I will try a library function instead!
  if (debounce)
  {
    debounce = false;

    // Wait to be sure switch is done changing state.
    delay(1000);
  }
  else
  {
    for (int i=0; i<100; i++)
    {
      int toggle = digitalRead(PAUSE_PLAY_SKIP_BTN);
      if (toggle == LOW)
      {
        if (paused)
        {
          Serial.println(F("Resuming playback after a pause"));
          g_skippedTime = 0;
          myDFPlayer.start();
          paused = false;
        }
        else
        {
          // Do not look for button presses immediately after skipping a track. Give it some time
          // to play before accepting another button command. Use the real time clock
          // to determine how long since last skip track request.
          if (g_skippedTime == 0 || (rtc.now().secondstime() - g_skippedTime > IGNORE_AFTER_SKIPPING_SECONDS))
          {
            Serial.println(F("Pausing playback after a short button press"));

            g_skippedTime = 0;
            myDFPlayer.pause();
            paused = true;
  
            delay(800);
            int skipPausedTrk = digitalRead(PAUSE_PLAY_SKIP_BTN);
            if (skipPausedTrk == LOW)
            {
              g_skippedTime = rtc.now().secondstime();
              paused = false;
              debounce = true;
              Serial.print(F("Skipping to another track at time: "));
              Serial.println(g_skippedTime, DEC);
              rc = true;
              // Give the user an indication that track skip has been done.
              flashLEDs();
              g_skippedTime = 0;
              break;
            }
          }
          else
          {
            Serial.println(F("Bypassing a pause request...too soon"));
          }
        }
        
        debounce = true;
        break;
      }
      delay(10);
    }
  }
  
  #ifdef PAUSE_PLAY_SKIP_ENTRY_EXIT_TRACE
  Serial.print(F("<===== checkPausePlaySkipBtn exit with return code: "));
  Serial.println(rc, DEC);
  #endif

  return rc;
}

uint8_t volumeSetting = PRESET_VOLUME;
void readEncoder()
{
  long newPosition = myEnc.read();
  if (newPosition != oldPosition) 
  {
    if ((newPosition > oldPosition) && (volumeSetting < 30))
    {
      // Clockwise turn, increase volume
      volumeSetting+=4;
      if (volumeSetting > 30)
      {
        volumeSetting = 30;
      }
      myDFPlayer.volume(volumeSetting);

      // We just increased volume so we know the low volume limit LED should be off. 
      digitalWrite(VOLUME_DOWN_LED, LOW);

      // When the upper limit is reached, light up an LED to tell the user.
      if (volumeSetting == 30)
      {
        digitalWrite(VOLUME_UP_LED, HIGH);       
      }
      else
      {
        digitalWrite(VOLUME_UP_LED, LOW);
      }
    }
    if ((newPosition < oldPosition) && (volumeSetting > 0))
    {
      // Counter-clockwise turn, decrease volume
      // Decrease faster than increase, just because when things are too loud it seems
      // more urgent to silence them!
      volumeSetting-=6;
      if (volumeSetting < 0)
      {
        volumeSetting = 0;
      }
      myDFPlayer.volume(volumeSetting);
      
      // We just decreased volume so we know the high volume limit LED should be off. 
      digitalWrite(VOLUME_UP_LED, LOW);

      // When the lower limit is reached, light up an LED to tell the user.
      if (volumeSetting == 0)
      {
        digitalWrite(VOLUME_DOWN_LED, HIGH);       
      }
      else
      {
        digitalWrite(VOLUME_DOWN_LED, LOW);
      }
    }
    oldPosition = newPosition;
    Serial.print(F("Volume setting: "));
    Serial.println(volumeSetting);
  }
  
}

void flashLEDs()
{
  digitalWrite(VOLUME_DOWN_LED, HIGH); 
  digitalWrite(VOLUME_UP_LED, HIGH); 
  delay(250);
  digitalWrite(VOLUME_DOWN_LED, LOW);
  digitalWrite(VOLUME_UP_LED, LOW); 
}

void blinkLEDs(uint8_t count)
{
  for (uint8_t idx=0; idx < count; idx++)
  {
    digitalWrite(VOLUME_DOWN_LED, HIGH); 
    digitalWrite(VOLUME_UP_LED, LOW); 
    delay(100);
    digitalWrite(VOLUME_DOWN_LED, LOW);
    digitalWrite(VOLUME_UP_LED, HIGH); 
    delay(100);
  }
  digitalWrite(VOLUME_UP_LED, LOW); 
  digitalWrite(VOLUME_DOWN_LED, LOW); 
      
}

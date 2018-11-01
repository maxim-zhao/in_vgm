unit defs;

interface

uses
  Windows, SysUtils, Messages,

  GZIO;

type
  PShortInt = ^ShortInt;

  ConfigProc = procedure(hwndParent : HWND); cdecl;
  AboutProc = procedure(hwndParent : HWND); cdecl;
  GetFileInfoProc = procedure(afile, title : PChar; length_in_ms : PInteger); cdecl;
  InfoBoxProc = function(afile : PChar; hwndParent : HWND) : Integer; cdecl;
  IsOurFileProc = function(afile : PChar) : Integer; cdecl;
  PlayProc = function(fn : PChar) : Integer; cdecl;
  IsPausedProc = function : Integer; cdecl;
  GetLengthProc = function : Integer; cdecl;
  GetOutputTimeProc = function : Integer; cdecl;
  SetOutputTimeProc = procedure(time_in_ms : Integer); cdecl;
  SetVolumeProc = procedure(volume : Integer); cdecl;
  SetPanProc = procedure(pan : Integer); cdecl;
  SAVSAInitProc = procedure(maxlatency_in_ms, srate : Integer); cdecl;
  SAAddPCMDataProc = procedure(PCMData : Pointer; nch, bps, timestamp : Integer); cdecl;
  SAGetModeProc = function : Integer; cdecl;
  SAAddProc = procedure(data : Pointer; timestamp, csa : Integer); cdecl;
  VSAAddPCMDataProc = procedure(PCMData : Pointer; nch, bps, timestamp : Integer); cdecl;
  VSAGetModeProc = function(specNch, waveNch : PInteger) : Integer; cdecl;
  VSAAddProc = procedure(data : Pointer;timestamp : Integer); cdecl;
  VSASetInfoProc = procedure(nch, srate : Integer); cdecl;
  dsp_isactiveProc = function : Integer; cdecl;
  dsp_dosamplesProc = function(samples : PSmallInt; numsamples, bps, nch, srate : Integer) : Integer; cdecl;
  EQSetProc = procedure(aOn : Integer; data : PChar; preamp : Integer); cdecl;
  SetInfoProc = procedure(bitrate, srate, stereo, synched : Integer); cdecl;

  OpenProc = function(samplerate, numchannels, bitspersamp, bufferlenms, prebufferms : Integer) : Integer; cdecl;
  WriteProc = function(buf : PChar; len : Integer) : Integer; cdecl;
  CanWriteProc = function : Integer; cdecl;
  IsPlayingProc = function : Integer; cdecl;
  PauseProc = function(pause : Integer) : Integer; cdecl;
  FlushProc = procedure(t : Integer); cdecl;
  GetWrittenTimeProc = function : Integer; cdecl;

  PWinampOutputPlugin = ^TWinampOutputPlugin;

  PWinampInputPlugin = ^TWinampInputPlugin;
  TWinampInputPlugin = packed Record
    Version : Integer;        // module type (IN_VER)
    Description : PChar;      // description of module, with version string
    hMainWindow : HWND;	      // winamp's main window (filled in by winamp)
    hDllInstance : HINST;     // DLL instance handle (Also filled in by winamp)
    FileExtensions : PChar;   // 'mp3'+#0+'Layer 3 MPEG'+#0+'mp2'+#0+'Layer 2 MPEG'+#0+'mpg'+#0+'Layer 1 MPEG';
                              // May be altered from Config, so the user can select what they wants
    is_seekable : Integer;    // is this stream seekable?
    UsesOutputPlug : Integer; // does this plug-in use the output plug-ins? (musn't ever change, ever :)
    Config : ConfigProc;      // configuration dialog
    About : AboutProc;        // about dialog
    Init : procedure; cdecl;  // called at program init
    Quit : procedure; cdecl;  // called at program quit
    GetFileInfo : GetFileInfoProc; // if file == NULL, current playing is used
    InfoBox : InfoBoxProc;
    IsOurFile : IsOurFileProc;// called before extension checks, to allow detection of mms://, etc

    //***** playback stuff *****//
    Play : PlayProc;            // return zero on success, -1 on file-not-found, some other value on other (stopping winamp) error
    Pause : procedure; cdecl;   // pause stream
    UnPause : procedure; cdecl; // unpause stream
    IsPaused : IsPausedProc;    // ispaused? return 1 if paused, 0 if not
    Stop : procedure; cdecl;    // stop (unload) stream

    //***** time stuff *****//
    GetLength : GetLengthProc;         // get length in ms
    GetOutputTime : GetOutputTimeProc; // returns current output time in ms. (usually returns outMod->GetOutputTime()
    SetOutputTime : SetOutputTimeProc; // seeks to point in stream (in ms). Usually you signal your thread to seek,
                                       // which seeks and calls outMod->Flush()..

    //***** volume stuff *****//
    SetVolume : SetVolumeProc; // from 0 to 255.. usually just call outMod->SetVolume
    SetPan : SetPanProc;       // from -127 to 127.. usually just call outMod->SetPan

    //***** in-window builtin vis stuff *****//
    SAVSAInit : SAVSAInitProc;      // call once in Play(). maxlatency_in_ms should be the value returned from outMod->Open();
                                    // call after opening audio device with max latency in ms and samplerate
    SAVSADeInit : procedure; cdecl; // call in Stop()

    //***** simple vis supplying mode *****//
    SAAddPCMData : SAAddPCMDataProc; // sets the spec data directly from PCM data
                                     // quick and easy way to get vis working :)
                                     // needs at least 576 samples :)

    //***** advanced vis supplying mode, only use if you're cool. Use SAAddPCMData for most stuff. *****//
    SAGetMode : SAGetModeProc; // gets csa (the current type (4=ws,2=osc,1=spec))
                               // use when calling SAAdd()
    SAAdd : SAAddProc;         // sets the spec data, filled in by winamp

    //***** vis stuff (plug-in) *****//
    //***** simple vis supplying mode *****//
    VSAAddPCMData : VSAAddPCMDataProc; // sets the vis data directly from PCM data
    				       // quick and easy way to get vis working :)
    				       // needs at least 576 samples :)

    //***** advanced vis supplying mode, only use if you're cool. Use VSAAddPCMData for most stuff. *****//
    VSAGetMode : VSAGetModeProc; // use to figure out what to give to VSAAdd
    VSAAdd : VSAAddProc;         // filled in by winamp, called by plug-in
    VSASetInfo : VSASetInfoProc; // call this in Play() to tell the vis plug-ins the current output params.

    //***** dsp plug-in processing *****//
    // (filled in by winamp, called by input plug)
    dsp_isactive : dsp_isactiveProc;   // returns 1 if active (which means that the number of samples returned by dsp_dosamples
    				       // could be greater than went in.. Use it to estimate if you'll have enough room in the
    				       // output buffer
    dsp_dosamples : dsp_dosamplesProc; // returns number of samples to output. This can be as much as twice numsamples.
                                       // be sure to allocate enough buffer for samples, then.

    //***** eq stuff *****//
    EQSet : EQSetProc;                  // 0-64 each, 31 is +0, 0 is +12, 63 is -12. Do nothing to ignore.

    SetInfo : SetInfoProc;              // info setting (filled in by winamp)

    OutputPlugin : PWinampOutputPlugin; // filled in by winamp, optionally used
  end;

  TWinampOutputPlugin = packed record
    Version : Integer; // module version (OUT_VER)
    Description : PChar; // description of module, with version string
    id : Integer;	// module id. each input module gets its own. non-nullsoft modules should be >= 65536.
    hMainWindow : HWND; // winamp's main window (filled in by winamp)
    hDllInstance : HINST; // DLL instance handle (filled in by winamp)
    Config : ConfigProc; // configuration dialog
    About : AboutProc; // about dialog
    Init : procedure; cdecl; // called when loaded
    Quit : procedure; cdecl; // called when unloaded
    Open : OpenProc;
    		// returns >=0 on success, <0 on failure
    		// NOTENOTENOTE : bufferlenms and prebufferms are ignored in most if not all output plug-ins.
    		//    ... so don't expect the max latency returned to be what you asked for.
    		// returns max latency in ms (0 for diskwriters, etc)
    		// bufferlenms and prebufferms must be in ms. 0 to use defaults.
    		// prebufferms must be <= bufferlenms

    Close : procedure; cdecl; // close the ol' output device.

    Write : WriteProc;
    		// 0 on success. Len == bytes to write (<= 8192 always). buf is straight audio data.
    		// 1 returns not able to write (yet). Non-blocking, always.

    CanWrite : CanWriteProc; // returns number of bytes possible to write at a given time.
    		 // Never will decrease unless you call Write (or Close, heh)

    IsPlaying : IsPlayingProc; // non 0 if output is still going or if data in buffers waiting to be
    				// written (i.e. closing while IsPlaying() returns 1 would truncate the song

    Pause : PauseProc; // returns previous pause state
    SetVolume : SetVolumeProc;// volume is 0-255
    SetPan : SetVolumeProc; // pan is -128 to 128
    Flush : FlushProc; // flushes buffers and restarts output at time t (in ms)
    			 // (used for seeking)

    GetOutputTime : GetOutputTimeProc; // returns played time in MS
    GetWrittenTime : GetWrittenTimeProc; // returns time written in MS (used for synching up vis stuff)
  end;

  function winampGetInModule2 : PWinampInputPlugin; cdecl; export;

  procedure config(hwndParent : HWND); cdecl;
  procedure about(hwndParent : HWND); cdecl;
  procedure init; cdecl;
  procedure quit; cdecl;
  function IsOurFile(fn : PChar) : Integer; cdecl;
  function play(fn : PChar) : Integer; cdecl;
  procedure pause; cdecl;
  procedure unpause; cdecl;
  function ispaused : Integer; cdecl;
  procedure stop; cdecl;
  function getlength : Integer; cdecl;
  function getoutputtime : Integer; cdecl;
  procedure setoutputtime(time_in_ms : Integer); cdecl;
  procedure setvolume(volume : Integer); cdecl;
  procedure setpan(pan : Integer); cdecl;
  procedure getfileinfo(filename, title : PChar;length_in_ms : PInteger); cdecl;
  function InfoBox(afile : PChar; hwndParent : HWND) : Integer; cdecl;
  procedure eq_set(aOn : Integer; data : PChar; preamp : Integer); cdecl;

const
  IN_VER = $100;
  OUT_VER = $10;

  WM_WA_MPEG_EOF = WM_USER + 2;

  WinampInputPlugin : TWinampInputPlugin = (
    Version : IN_VER;
    Description : 'VGM input plugin v0.11';
    hMainWindow : 0;
    hDllInstance : 0;
    FileExtensions : 'vgm;vgz'#0'VGM game audio (*.vgm,*.vgz)'#0; // accepted extensions
    is_seekable : 1;
    UsesOutputPlug : 1;
    Config : Config;
    About : About;
    Init : Init;
    Quit : Quit;
    GetFileInfo : GetFileInfo;
    InfoBox : InfoBox;
    IsOurFile : IsOurFile;
    Play : Play;
    Pause : Pause;
    UnPause : Unpause;
    IsPaused : IsPaused;
    Stop : Stop;
    GetLength : GetLength;
    GetOutputTime : GetOutputTime;
    SetOutputTime : SetOutputTime;
    SetVolume : SetVolume;
    SetPan : SetPan;
    SAVSAInit : nil;
    SAVSADeInit :  nil;
    SAAddPCMData :  nil;
    SAGetMode :  nil;
    SAAdd :  nil;
    VSAAddPCMData :  nil;
    VSAGetMode :  nil;
    VSAAdd :  nil;
    VSASetInfo :  nil;
    dsp_isactive :  nil;
    dsp_dosamples :  nil;
    EQSet : EQ_Set;
    SetInfo : nil;
    OutputPlugin : nil;
  );

implementation

{$RESOURCE dialogue.res}

type
  TVGMHeader = packed record
    IDString:array[0..3] of char;
    FileSize,Version,PSGClock,FMClock,GD3Location,TrackLength,LoopPoint,LoopLength:longint;
  end;
  TGD3Header = packed record
    IDString:array[0..3] of char;
    Version,Length:longint;
  end;


const
  killDecodeThread:integer=0;
  thread_handle:THandle=INVALID_HANDLE_VALUE;

  RequiredGD3Version=$100;
  NumStrings=11; // Number of strings in GD3 tag
  BufferSize=$1000; // File buffer size (4KB)
  NumChannels=2;
  MinVersion=$100;

var
  lastfn:string;
  sample_buffer:array[0..576*2*NumChannels-1] of SmallInt;
  Paused:integer;
  f:gzfile;
  TrackLengthInms,LoopLengthInms,LoopOffset:integer;
  InfoStrings:array[0..NumStrings] of string; // 0 is the filename...

  // Global sound synth vars
  SeekToSampleNumber: longint;
  PSGVolumes:array[0..3] of shortint;
  PSGStereo,PSGMute:byte;
  PSGClockValue:longint;

  // File buffer
  FileBuffer:pByteArray;
  FileBufferPos:longint;

  // Looping
  NumLoopsDone,NumLoops:integer;

  TrackTitleFormat:string;

function ReadString(const fh:gzfile):string;
var
  MyChar:array[0..1] of widechar;
  s:string;
begin
  s:='';
  repeat
    gzread(fh,@MyChar,2);
    s:=s+WideCharToString(@MyChar);
  until (Word(MyChar[0])=0) or gzEoF(fh);
  Result:=s;
end;

procedure about(hwndParent : HWND); cdecl;
begin
  MessageBox(
    WinampInputPlugin.hMainWindow,
    'VGM 1.00 Winamp input plugin'#13#13'by Maxim in 2001'#13'maxim@mwos.cjb.net'#13'http://www.smspower.org/music'#13'Made in Delphi'#13#13+
    'Current status:'#13+
    'PSG tone channels - perfect'#13+
    'PSG white noise - Same method as Meka, MAME, Dega... woefully inaccurate'#13+
    'PSG sychronous noise - tested against a real system, seems right'#13+
    '(note that a real PSG is quite far from the ideal device I''m simulating...)'#13+
    'PSG stereo - seems to work'#13+
    'YM2413 - horribly complicated...'#13+
    'YM(other) - not even considered yet'#13#13+
    'Don''t be put off by the pre-0.1 version numbers. This is a non-commercial'#13+
    'project and as such it is permanently in beta.',
    WinampInputPlugin.Description,
    0
  );
end;

procedure init; cdecl;
var
  INIFilename:string;
  TempPChar:PChar;
begin
  INIFilename:=ExtractFilePath(ParamStr(0))+'winamp.ini';
  NumLoops:=GetPrivateProfileInt('Maxim''s VGM input plugin','NumLoops',1,PChar(INIFilename));
  TempPChar:=StrAlloc(1024);
  try
    GetPrivateProfileString('Maxim''s VGM input plugin','Title format','%1 - %2',TempPChar,1024,PChar(INIFilename));
    TrackTitleFormat:=TempPChar;
  finally
    StrDispose(TempPChar);
  end;
end;

procedure quit;
var
  INIFilename:string;
begin
  INIFilename:=ExtractFilePath(ParamStr(0))+'winamp.ini';
  WritePrivateProfileString('Maxim''s VGM input plugin','NumLoops',PChar(IntToStr(NumLoops)),PChar(INIFilename));
  WritePrivateProfileString('Maxim''s VGM input plugin','Title format',PChar(TrackTitleFormat),PChar(INIFilename));
end;

function IsOurFile(fn : PChar) : Integer;
begin
  result:=0;
end;

procedure PlayThread(b:Pointer); stdcall;
const
  NoiseInitialState=0;
  PSGVolumeValues:array[0..15] of word = (8028,8028,8028,6842,5603,4471,3636,2909,2316,1778,1427,1104,862,673,539,0);
  //###
//  WaitFactor=1.2;
var
  l,x,SamplesTillNextRead:integer;
  {a,}d,PSGFrequencyLowBits,Channel:byte;
  ToneFreqs:array[0..3] of word; // frequency register values (total)
  ToneFreqVals:array[0..3] of smallint; // frequency register values (counters)
  ToneFreqPos:array[0..3] of shortint; // frequency channel flip-flops
  NoiseShiftRegister:dword;
  NoiseFeedback:longint;
  Clock,dClock:double;
  NumClocksForSample:byte;
  i:byte;
  Channels:array[0..3] of smallint;

  //###
//  FractionalSamplesTillNextRead:single;

  function ReadByte:byte;
  begin
    if FileBufferPos=BufferSize then begin // read a block
      FileBufferPos:=0;
      gzRead(f,FileBuffer,BufferSize);
    end;
    Result:=FileBuffer[FileBufferPos];
    Inc(FileBufferPos);
  end;
begin
  dClock:=PSGClockValue/16/44100;
  PSGFrequencyLowBits:=0;
  Channel:=4;
  // Set volumes to 0
  for i:=0 to 3 do PSGVolumes[i]:=0;
  // Set all channels on
  PSGMute:=$f;
  PSGStereo:=$ff;
  // Set all frequencies to 0
  for i:=0 to 3 do ToneFreqs[i]:=$000;
  // Set counters to 0
  for i:=0 to 3 do ToneFreqVals[i]:=0;
  // Set flip-flops to a position
  for i:=0 to 3 do ToneFreqPos[i]:=1;
  // Initialise noise generator
  NoiseShiftRegister:=NoiseInitialState;
  NoiseFeedback:=0;
  // Zero clock
  Clock:=0;
  SamplesTillNextRead:=0;
  //###
//  FractionalSamplesTillNextRead:=0;

  while integer(b^)=0 do begin
    // ---number of bytes I can write---------  --size of buffer----- ---x2 if dsp is active because----
    //                                          ----in bytes--------- --it can buffer as much as 100%---
    if WinampInputPlugin.OutputPlugin.CanWrite>=SizeOf(sample_buffer) shl WinampInputPlugin.dsp_isactive then begin
      // Main loop
      l:=sizeof(sample_buffer) div (2*NumChannels); // l = number of samples
      for x:=0 to l div 2 -1 do begin // fill sample buffer
        while (SamplesTillNextRead=0) {and not EoF(f)} do begin
          case ReadByte of
          $4f: PSGStereo:=ReadByte;
          $50: begin // PSG
                 d:=ReadByte;
                 case (d and $90) of
                 $00,$10: begin
                   if (Channel and $4 =0) then begin
                     ToneFreqs[Channel]:=(d and $3F) shl 4 or PSGFrequencyLowBits;
                     if ToneFreqs[Channel]<5 then begin
                       ToneFreqs[Channel]:=0; // seems to be what a real SMS does
                       PSGVolumes[Channel]:=0;
                       ToneFreqVals[Channel]:=0;
                     end;
                   end;
                 end;
                 $80: if (d and $60=$60) then begin // noise
                        ToneFreqs[3]:=$10 shl (d and $3);
                        if (d and $4=$4) then NoiseFeedback:=$12000 else NoiseFeedback:=$8000;
                        NoiseShiftRegister:=NoiseInitialState;
                        Channel:=4;
                      end
                      else begin
                        Channel:=(d and $60) shr 5;
                        PSGFrequencyLowBits:=d and $F;
                      end; // First frequency byte
                 $90: begin
                        PSGVolumes[(d and $60) shr 5]:=d and $F; // Volume
                        Channel:=4;
                      end;  
                 end;
               end;
          $51..$5f: begin readbyte; readbyte; end;
{
          $51: begin // YM413
                 a:=ReadByte;
                 d:=ReadByte;
                 case a of
                 // Registers 00 to 06 for user-defined instrument... so not important
                 // Variables not actually here yet...
                 // AM[n] = amplitude modulation on n (0=carrier, 1=modulator) on (1) or off (0)
                 // FV[n] = frequency vibration
                 // Decay[n]
                 $00,$01: begin
                   AM[a]   :=Ord(d and $80>0);
                   FV[a]   :=Ord(d and $40>0);
                   Decay[n]:=Ord(d and $20>0);
                   // key scale rate???
                   // multi sample wave selection???
                 end;
                 $02:;
                 // Variables not actually here yet...
                 // FMRhythm = which rhythm intruments we are asked to play
                 $0E: begin // rhythm
                        if (d and $20>0) then FMRhythm:=d and $1F;
                      end;
                 end;
               end;
}
          $61: SamplesTillNextRead:=ReadByte or ReadByte shl 8;
          $62: SamplesTillNextRead:=735;
          $63: SamplesTillNextRead:=882;
          $66: begin // end of sound data
                 Inc(NumLoopsDone);
                 if (NumLoopsDone>NumLoops) or (LoopOffset=0) then repeat // stop file
                   WinampInputPlugin.OutputPlugin.CanWrite; // tell it to try and keep playing, until...
                   if WinampInputPlugin.OutputPlugin.IsPlaying=0 then begin
                      PostMessage(WinampInputPlugin.hMainWindow,WM_WA_MPEG_EOF,0,0);
                      exit;
                   end;
                   Dec(FileBufferPos);
                   Sleep(10);
                 until false;

                 // loop file
                 if gzSeek(f,LoopOffset,SEEK_SET)=-1 then begin
                   gzclose(f);
                   f:=gzopen(lastfn,'r',0);
                   gzSeek(f,LoopOffset,SEEK_SET);
                 end;
                 FileBufferPos:=BufferSize;
               end;
          end;

          //###
//          FractionalSamplesTillNextRead:=FractionalSamplesTillNextRead+SamplesTillNextRead*WaitFactor;
//          SamplesTillNextRead:=Trunc(FractionalSamplesTillNextRead);
//          FractionalSamplesTillNextRead:=Frac(FractionalSamplesTillNextRead);

          if SeekToSampleNumber>-1 then begin
            Dec(SeekToSampleNumber,SamplesTillNextRead);
            SamplesTillNextRead:=0;
            if SeekToSampleNumber<-1 then SamplesTillNextRead:=-SeekToSampleNumber+1;
            Continue;
          end;
        end;


        Channels[0]:=PSGMute       and $1*PSGVolumeValues[PSGVolumes[0]]*ToneFreqPos[0];
        Channels[1]:=PSGMute shr 1 and $1*PSGVolumeValues[PSGVolumes[1]]*ToneFreqPos[1];
        Channels[2]:=PSGMute shr 2 and $1*PSGVolumeValues[PSGVolumes[2]]*ToneFreqPos[2];
        Channels[3]:=PSGMute shr 3 and $1*PSGVolumeValues[PSGVolumes[3]]*(NoiseShiftRegister and $1);

        // Debug permanent channel muting 
//        Channels[0]:=0;
//        Channels[1]:=0;
//        Channels[2]:=0;
//        Channels[3]:=0;

        // left
        sample_buffer[2*x]  :=(PSGStereo shr 4 and $1*Channels[0]+
                               PSGStereo shr 5 and $1*Channels[1]+
                               PSGStereo shr 6 and $1*Channels[2]+
                               PSGStereo shr 7 and $1*Channels[3]);
        // right
        sample_buffer[2*x+1]:=(PSGStereo       and $1*Channels[0]+
                               PSGStereo shr 1 and $1*Channels[1]+
                               PSGStereo shr 2 and $1*Channels[2]+
                               PSGStereo shr 3 and $1*Channels[3]);

        Clock:=Clock+dClock;
        NumClocksForSample:=Trunc(Clock);
        Clock:=Frac(Clock);

        // Repetetive code is marginally faster than a loop (probably)
        if ToneFreqs[0]<>0 then Dec(ToneFreqVals[0],NumClocksForSample);
        if ToneFreqs[1]<>0 then Dec(ToneFreqVals[1],NumClocksForSample);
        if ToneFreqs[2]<>0 then Dec(ToneFreqVals[2],NumClocksForSample);

        if ToneFreqs[3]=128 then ToneFreqVals[3]:=ToneFreqVals[2] else Dec(ToneFreqVals[3],NumClocksForSample);

        if ToneFreqVals[0]<0 then begin ToneFreqPos[0]:=-ToneFreqPos[0]; Inc(ToneFreqVals[0],ToneFreqs[0]); end;
        if ToneFreqVals[1]<0 then begin ToneFreqPos[1]:=-ToneFreqPos[1]; Inc(ToneFreqVals[1],ToneFreqs[1]); end;
        if ToneFreqVals[2]<0 then begin ToneFreqPos[2]:=-ToneFreqPos[2]; Inc(ToneFreqVals[2],ToneFreqs[2]); end;
        if ToneFreqVals[3]<0 then begin
          ToneFreqPos[3]:=-ToneFreqPos[3];
          Inc(ToneFreqVals[3],ToneFreqs[3]);
          if ToneFreqPos[3]=1 then begin
            if NoiseShiftRegister=0 then NoiseShiftRegister:=1;
            if (NoiseShiftRegister and $1=$1)
            then NoiseShiftRegister:=NoiseShiftRegister shr 1 xor NoiseFeedback
            else NoiseShiftRegister:=NoiseShiftRegister shr 1;
          end;
        end;

        Dec(SamplesTillNextRead);
      end;

      x:=WinampInputPlugin.OutputPlugin.GetWrittenTime;
      WinampInputPlugin.SAAddPCMData(@sample_buffer,NumChannels,16,x);  // add to spectrum vis
      WinampInputPlugin.VSAAddPCMData(@sample_buffer,NumChannels,16,x); // add to wave vis

      l:=WinampInputPlugin.dsp_dosamples(PSmallInt(@sample_buffer),l div NumChannels,16,NumChannels,44100); // process with EQ

      WinampInputPlugin.OutputPlugin.Write(PChar(@sample_buffer),l*2*NumChannels); // send to output plugin
    end else sleep(50);
  end;
end;

function play(fn : PChar) : Integer;
var
  maxlatency,tmp:integer;
  VGMHeader: TVGMHeader;

  function gzFileSize(f:gzfile):longint;
  var
    s:gz_streamp;
  begin
    s:=gz_streamp(f);
    result:=FileSize(s^.gzfile);
  end;
begin
  if not FileExists(fn) then begin
    result:=-1;
    exit;
  end;

  lastfn:=strpas(fn);

  f:=gzOpen(fn,'r',0);
  gzRead(f,@VGMHeader,SizeOf(VGMHeader));

  if (VGMHeader.IDString<>'Vgm ')
  then begin
    MessageBox(WinampInputPlugin.hMainWindow,'Not a VGM file',WinampInputPlugin.Description,0);
    result:=-1;gzClose(f);exit;
  end;

  if not ((VGMHeader.Version>=MinVersion) and (VGMHeader.Version and $100 = $100))
  and (MessageBox(WinampInputPlugin.hMainWindow,'Unsupported VGM version detected! Do you want to try to play it anyway?',WinampInputPlugin.Description,MB_YESNO+MB_DEFBUTTON2)=idNo)
  then begin
    result:=-1;gzClose(f);exit;
  end;

  // file buffer
  GetMem(FileBuffer,BufferSize);
  FileBufferPos:=BufferSize;

  if VGMHeader.TrackLength=0
  then TrackLengthInms:=-1000
  else TrackLengthInms:=Trunc(VGMHeader.TrackLength/44.1);

  if VGMHeader.LoopPoint=0
  then begin
    LoopLengthInms:=0;
    LoopOffset:=0;
  end else begin
    LoopLengthInms:=Trunc(VGMHeader.LoopLength/44.1);
    LoopOffset:=VGMHeader.LoopPoint+$1c;
  end;

  PSGClockValue:=VGMHeader.PSGClock;
  if PSGClockValue=0 then PSGClockValue:=3579540;

  gzSeek(f,$40,0);
  paused:=0;
  SeekToSampleNumber:=-1;

  FillChar(sample_buffer,SizeOf(sample_buffer),0);

  maxlatency:=WinampInputPlugin.OutputPlugin.Open(44100,2,16,-1,-1);
  if maxlatency<0 then begin
    result:=1;
    exit;
  end;

  if TrackLengthInms=0
  then WinampInputPlugin.SetInfo(0,44,NumChannels,1)
  else WinampInputPlugin.SetInfo(
    (gzFileSize(F)*8) div (TrackLengthInms*1024 div 1000), // kbps
    44, // kHz
    NumChannels,
    1   // Synched (?)
  );
  WinampInputPlugin.SAVSAInit(maxlatency,44100);
  WinampInputPlugin.VSASetInfo(NumChannels,44100);
  WinampInputPlugin.OutputPlugin.SetVolume(-666);
  killDecodeThread:=0;

  NumLoopsDone:=0;
  thread_handle:=CreateThread(nil,0,@PlayThread,@killDecodeThread,0,tmp);
  SetThreadPriority(thread_handle,THREAD_PRIORITY_HIGHEST); // stops jumps?
  result:=0;
end;

procedure pause;
begin
  paused:=1;
  WinampInputPlugin.OutputPlugin.Pause(1);
end;

procedure unpause;
begin
  paused:=0;
  WinampInputPlugin.OutputPlugin.Pause(0);
end;

function ispaused : Integer;
begin
  Result:=paused;
end;

procedure stop;
begin
  if thread_handle<>-1 then begin
    killDecodeThread:=1;
    if WaitForSingleObject(thread_handle,Infinite)=WAIT_TIMEOUT then begin
      MessageBox(WinampInputPlugin.hMainWindow,'error asking thread to die!','error killing decode thread',0);
      TerminateThread(thread_handle,0);
    end;
    CloseHandle(thread_handle);
    thread_handle:=INVALID_HANDLE_VALUE;
  end;

  gzClose(f);

  if FileBuffer<>nil then FreeMem(FileBuffer,BufferSize);

  WinampInputPlugin.OutputPlugin.Close;
  WinampInputPlugin.SAVSADeInit;
end;

function getlength : Integer;
begin
  result:=TrackLengthInms+NumLoops*LoopLengthInms;
end;

function getoutputtime : Integer;
begin
  result:=WinampInputPlugin.OutputPlugin.GetOutputTime;
end;

procedure setoutputtime(time_in_ms : Integer);
begin
  WinampInputPlugin.OutputPlugin.Flush(time_in_ms);
  SeekToSampleNumber:=Trunc(time_in_ms*44.1);
  gzSeek(f,$40,0);
  FileBufferPos:=BufferSize;
  NumLoopsDone:=0;
end;

procedure setvolume(volume : Integer);
begin
  WinampInputPlugin.OutputPlugin.SetVolume(volume);
end;

procedure setpan(pan : Integer);
begin
  WinampInputPlugin.OutputPlugin.SetPan(pan);
end;

procedure getfileinfo(filename, title : PChar; length_in_ms : PInteger);
const
  strnums:array[1..6] of byte = (1,3,5,7,9,10);
var
  VGMHeader:TVGMHeader;
  fh:gzfile;
  GD3Info:string;
  GD3Header:TGD3Header;
  LocalInfoStrings:array[1..NumStrings] of string; // 0 is the filename...
  i,j:integer;
begin
  if (Filename=nil) or (filename[0]=#0) then Filename:=PChar(lastfn);
  if Assigned(Title) then begin
    if not FileExists(Filename) then begin
      strpcopy(title,'Dead file! '+filename);
      exit;
    end;
    fh:=gzopen(Filename,'r',0);
    gzread(fh,@VGMHeader,SizeOf(VGMHeader));

    GD3Info:=extractfilename(Filename);

    if (VGMHeader.IDString<>'Vgm ') then GD3Info:='Not an VGM file - '+GD3Info
    else if not ((VGMHeader.Version>=MinVersion) and (VGMHeader.Version and $100 = $100)) then GD3Info:='Unsupported VGM version - '+GD3Info
    else if VGMHeader.GD3Location>0 then begin
      gzSeek(fh,VGMHeader.GD3Location+$14,0);
      gzRead(fh,@GD3Header,SizeOf(GD3Header));
      if (GD3Header.IDString='Gd3 ') and (GD3Header.Version>=RequiredGD3Version) and (GD3Header.Version and $100 = $100) then begin
        for i:=1 to NumStrings do LocalInfoStrings[i]:=ReadString(fh);
        for i:=1 to NumStrings do if LocalInfoStrings[i]='' then LocalInfoStrings[i]:='Unknown';
        GD3Info:=TrackTitleFormat;
        for i:=1 to 6 do begin
          j:=pos('%'+inttostr(i),GD3Info);
          if j>0 then begin
            Delete(GD3Info,j,2);
            Insert(LocalInfoStrings[strnums[i]],GD3Info,j);
          end;
        end;
      end;
      if Assigned(length_in_ms) then length_in_ms^:=trunc((VGMHeader.TrackLength+VGMHeader.LoopLength*NumLoops)/44.1);
    end else begin
      if Assigned(length_in_ms) then length_in_ms^:=trunc((VGMHeader.TrackLength+VGMHeader.LoopLength*NumLoops)/44.1);
    end;
    gzClose(fh);
    strPcopy(title,GD3Info);
  end;
end;

function InfoBox(afile : PChar; hwndParent : HWND) : Integer; cdecl;
var
  VGMHeader:TVGMHeader;
  GD3Header:TGD3Header;
  fh:gzfile;
  i:integer;
function MainDialogProc(DlgWin:hWnd;DlgMessage:UInt;DlgWParam:WParam;DlgLParam:LParam):Bool; stdcall;
const
  WinClose=2;
  CloseBtn=103;
  FilenameBox=104;
  TrackName=101;
  GameName=102;
  SystemName=107;
  Author=108;
  ReleaseDate=109;
  Creator=110;
  Notes=111;
  EnglishCheckBox=105;
  JapCheckBox=106;
  btnConfig=119;
procedure SetLang(Japanese:boolean);
begin
  SetDlgItemText(DlgWin,TrackName ,PChar(InfoStrings[1+Ord(Japanese)]));
  SetDlgItemText(DlgWin,GameName  ,PChar(InfoStrings[3+Ord(Japanese)]));
  SetDlgItemText(DlgWin,SystemName,PChar(InfoStrings[5+Ord(Japanese)]));
  SetDlgItemText(DlgWin,Author    ,PChar(InfoStrings[7+Ord(Japanese)]));
end;
begin
  Result:=True;
  case DlgMessage of
    WM_INITDIALOG:begin // Initialise dialogue
      // Fill in text
      SetDlgItemText(DlgWin,FilenameBox,PChar(InfoStrings[0]));
      SetLang(False);
      SendDlgItemMessage(DlgWin,EnglishCheckBox,BM_SETCHECK,1,0);
      SetDlgItemText(DlgWin,ReleaseDate,PChar(InfoStrings[9]));
      SetDlgItemText(DlgWin,Creator    ,PChar(InfoStrings[10]));
      SetDlgItemText(DlgWin,Notes      ,PChar(InfoStrings[11]));
      // Select Close button
      SetFocus(GetDlgItem(DlgWin,CloseBtn));
    end;
    WM_COMMAND:case LoWord(DlgWParam) of // Receive control-messages from dialog
      CloseBtn,WinClose:begin // System-close pressed, reset and exit player
        EndDialog(DlgWin, LOWORD(DlgWParam));
        Exit;
      end;
      EnglishCheckBox,JapCheckBox:SetLang(IsDlgButtonChecked(DlgWin,JapCheckBox)=1);
      btnConfig: Config(DlgWin);
    end;
  end;
  Result:=False;
end;
begin
  Result:=0;
  if not FileExists(afile) then begin
    MessageBox(WinampInputPlugin.hMainWindow,PChar('File not found - '+afile),WinampInputPlugin.Description,0);
    exit;
  end;
  fh:=gzopen(afile,'r',0);
  gzRead(fh,@VGMHeader,SizeOf(VGMHeader));
  if VGMHeader.GD3Location>0 then begin
    gzSeek(fh,VGMHeader.GD3Location+$14,0);
    gzRead(fh,@GD3Header,SizeOf(GD3Header));
    if (GD3Header.IDString='Gd3 ') and (GD3Header.Version>=RequiredGD3Version) and (GD3Header.Version and $100 = $100)
    then for i:=1 to NumStrings do InfoStrings[i]:=ReadString(fh);
  end;
  i:=1;
  while i<length(InfoStrings[11]) do begin
    if (InfoStrings[11][i]=#10) then begin
      Insert(#13,InfoStrings[11],i);
      Inc(i);
    end;
    Inc(i);
  end;
  InfoStrings[0]:=afile;
  gzClose(fh);

  DialogBox(hInstance, 'FILEINFODIALOGUE', hwndParent, @MainDialogProc); // Open dialog
end;

procedure eq_set(aOn : Integer; data : PChar; preamp : Integer);
begin
end;

function winampGetInModule2 : PWinampInputPlugin; cdecl;
begin
  Result:=@WinampInputPlugin;
end;

procedure config(hwndParent : HWND); cdecl;
function ConfigDialogProc(DlgWin:hWnd;DlgMessage:UInt;DlgWParam:WParam;DlgLParam:LParam):Bool; stdcall;
const
  btnOK=101;
  cbChan1=105;
  cbChan2=106;
  cbChan3=107;
  cbChan4=108;
  ebLoopCount=103;
  ebTrackTitle=110;
  btnNSMIMEStuff=112;
var
  mybool:longbool;
  i:integer;
  TempPChar:PChar;
  key: HKey;
begin
  Result:=True;
  case DlgMessage of
    WM_INITDIALOG:begin // Initialise dialogue
      // Fill in text
      SetFocus(GetDlgItem(DlgWin,btnOK));
      // Check tone channel checkboxes
      SendDlgItemMessage(DlgWin,cbChan1,BM_SETCHECK,Ord(PSGMute and $1>0),0);
      SendDlgItemMessage(DlgWin,cbChan2,BM_SETCHECK,Ord(PSGMute and $2>0),0);
      SendDlgItemMessage(DlgWin,cbChan3,BM_SETCHECK,Ord(PSGMute and $4>0),0);
      SendDlgItemMessage(DlgWin,cbChan4,BM_SETCHECK,Ord(PSGMute and $8>0),0);
      // Set loop count
      SetDlgItemInt(DlgWin,ebLoopCount,NumLoops,True);
      // Set title format text
      SetDlgItemText(DlgWin,ebTrackTitle,PChar(TrackTitleFormat));
    end;
    WM_COMMAND:case LoWord(DlgWParam) of // Receive control-messages from dialog
      btnOK: begin
        i:=GetDlgItemInt(DlgWin,ebLoopCount,MyBool,True);
        if MyBool then NumLoops:=i;
        TempPChar:=StrAlloc(1024);
        try
          GetDlgItemText(DlgWin,ebTrackTitle,TempPChar,1024);
          TrackTitleFormat:=TempPChar;
        finally
          StrDispose(TempPChar);
        end;
        EndDialog(DlgWin, LOWORD(DlgWParam));
        Exit;
      end;
      IDCANCEL: begin
        EndDialog(DlgWin, LOWORD(DlgWParam));
        Exit;
      end;
      cbChan1,cbChan2,cbChan3,cbChan4:
        PSGMute:=IsDlgButtonChecked(DlgWin,cbChan1)
              or IsDlgButtonChecked(DlgWin,cbChan2) shl 1
              or IsDlgButtonChecked(DlgWin,cbChan3) shl 2
              or IsDlgButtonChecked(DlgWin,cbChan4) shl 3;
      btnNSMIMEStuff: if MessageBox(DlgWin,
        'This will write these two registry values:'#13+
        'HKCU\.vgm\Content Type=audio/vgm'#13+
        'HKCU\.vgz\Content Type=audio/vgm'#13+
        'which will make Netscape associate files described as audio/vgm by the server with Winamp.'#13+
        'IE does not need this. Do you want to continue?',
        'Set MIME types',
        MB_ICONQUESTION+MB_YESNO
      )=idYes then begin
        if (RegCreateKey(HKEY_CLASSES_ROOT,'.vgz',key)<>ERROR_SUCCESS)
        or (RegSetValueEx(key,'Content Type',0,REG_SZ,PChar('audio/vgm'),10)<>ERROR_SUCCESS)
        then MessageBox(DlgWin,'Error setting value!','Error',MB_ICONERROR);
        RegCloseKey(key);
        if (RegCreateKey(HKEY_CLASSES_ROOT,'.vgm',key)<>ERROR_SUCCESS)
        or (RegSetValueEx(key,'Content Type',0,REG_SZ,PChar('audio/vgm'),10)<>ERROR_SUCCESS)
        then MessageBox(DlgWin,'Error setting value!','Error',MB_ICONERROR);
        RegCloseKey(key);
      end;
    end;
  end;
  Result:=False; // say I processed the message and set my control focus
end;
begin
  DialogBox(hInstance, 'CONFIGDIALOGUE', hwndParent, @ConfigDialogProc); // Open dialog
end;

end.

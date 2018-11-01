unit SN76489;

interface

procedure SN76489_Init(const PSGClockValue:longint);
procedure SN76489_Write(const data:byte);
procedure SN76489_GGStereoWrite(const data:byte);
procedure SN76489_WriteToBuffer(var buffer:array of smallint; const position:integer);

var
  PSGMute:byte; // expose this for inspection/modification for channel muting

implementation

const
  NoiseInitialState=0;
  NoiseWhiteFeedback=$12000;
  NoiseSynchFeedback=$8000; // 1 shl 15
  PSGVolumeValues:array[0..15] of word = (8028,8028,8028,6842,5603,4471,3636,2909,2316,1778,1427,1104,862,673,539,0);

var
  Clock,
  dClock         :double;

  PSGFrequencyLowBits,
  Channel,
  PSGStereo      :byte;

  ToneFreqs:   array[0..3] of word; // frequency register values (total)
  ToneFreqVals:array[0..3] of smallint; // frequency register values (counters)
  ToneFreqPos: array[0..3] of shortint; // frequency channel flip-flops
  Channels:    array[0..3] of smallint;
  PSGVolumes:  array[0..3] of shortint;

  NoiseShiftRegister,NoiseFeedback:cardinal;

  NumClocksForSample:byte;

  Active:boolean; // Set to true by SN76489_Init(), if false then all procedures exit immediately

//------------------------------------------------------------------------------
procedure SN76489_Init(const PSGClockValue:longint);
var
  i:integer;
begin
  Active:=PSGClockValue>0;
  if not Active then exit;
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
  // Set flip-flops to zero
  for i:=0 to 2 do ToneFreqPos[i]:=0;
  // But not this one! It's not audible anyway, plus starting it at 0 messes up other stuff 
  ToneFreqPos[3]:=1;
  // Initialise noise generator
  NoiseShiftRegister:=NoiseInitialState;
  NoiseFeedback:=0;
  // Zero clock
  Clock:=0;
end;


//------------------------------------------------------------------------------
procedure SN76489_Write(const data:byte);
begin
  if not Active then exit;
  case (data and $90) of
  $00,$10: if (Channel and $4 =0) then begin // second frequency byte
    ToneFreqs[Channel]:=(data and $3F) shl 4 or PSGFrequencyLowBits; // Set frequncy register
    if ToneFreqPos[Channel]=0 then ToneFreqPos[Channel]:=1; // need the if, for when writes happen when pos=-1 (want to keep that)
  end;
  $80: if (data and $60=$60) then begin // noise
    ToneFreqs[3]:=$10 shl (data and $3); // set shift rate
    if (data and $4=$4) then NoiseFeedback:=NoiseWhiteFeedback else NoiseFeedback:=NoiseSynchFeedback; // set feedback type
    NoiseShiftRegister:=NoiseInitialState; // reset register
    Channel:=4;
  end else begin // First frequency byte
    Channel:=(data and $60) shr 5; // select channel
    PSGFrequencyLowBits:=data and $F; // remember frequency data
  end;
  $90: begin // Volume
    PSGVolumes[(data and $60) shr 5]:=data and $F; // set volume
    Channel:=4;
  end;
  end; // case
end;

//------------------------------------------------------------------------------
procedure SN76489_GGStereoWrite(const data:byte);
begin
  if not Active then exit;
  PSGStereo:=data;
end;

//------------------------------------------------------------------------------
procedure SN76489_WriteToBuffer(var buffer:array of smallint; const position:integer);
var
  i:integer;
begin
  if not Active then exit;
  for i:=0 to 2 do
    Channels[i]:=PSGMute shr i and $1*PSGVolumeValues[PSGVolumes[i]]*ToneFreqPos[i];
  Channels[3]:=PSGMute shr 3 and $1*PSGVolumeValues[PSGVolumes[3]]*(NoiseShiftRegister and $1);

  buffer[2*position  ]:=0;
  buffer[2*position+1]:=0;
  for i:=0 to 3 do begin
    Inc(buffer[2*position  ],PSGStereo shr (i+4) and $1*Channels[i]); // left
    Inc(buffer[2*position+1],PSGStereo shr  i    and $1*Channels[i]); // right
  end;

  Clock:=Clock+dClock;
  NumClocksForSample:=Trunc(Clock);
  Clock:=Frac(Clock);

  // Decrement tone channel counters
  for i:=0 to 2 do if ToneFreqs[i]<>0 then Dec(ToneFreqVals[i],NumClocksForSample);
  // Noise channel: match to tone2 or decrement its counter
  if ToneFreqs[3]=128 then ToneFreqVals[3]:=ToneFreqVals[2]
              else if ToneFreqs[3]<>0 then Dec(ToneFreqVals[3],NumClocksForSample);

  // Tone channels:
  for i:=0 to 2 do
    if ToneFreqVals[i]<0 then begin    // If it gets below 0...
      ToneFreqPos[i]:=-ToneFreqPos[i]; // Flip the flip-flop
      if ToneFreqs[i]>0 then repeat Inc(ToneFreqVals[i],ToneFreqs[i]); until ToneFreqVals[i]>=0; // ...and increment it until it gets above 0 again
    end;

  // Noise channel
  if ToneFreqVals[3]<0 then begin                         // If it gets below 0...
    ToneFreqPos[3]:=-ToneFreqPos[3];                      // Flip the flip-flop
    if ToneFreqs[3]>0 then repeat Inc(ToneFreqVals[3],ToneFreqs[3]); until ToneFreqVals[3]>0; // ...and increment it until it gets above 0 again
    if ToneFreqPos[3]=1 then begin                        // Only once per cycle...
      if NoiseShiftRegister=0 then NoiseShiftRegister:=1; // zero state protection
      if (NoiseShiftRegister and $1=$1)                   // If the lowest bit is set...
      then NoiseShiftRegister:=NoiseShiftRegister shr 1 xor NoiseFeedback // then shift and do the feedback
      else NoiseShiftRegister:=NoiseShiftRegister shr 1;  // else just shift it
    end;
  end;
end;

initialization
begin
  Active:=False;
end;

end.

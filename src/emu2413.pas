unit EMU2413;

interface

uses SysUtils;

const
  OPLL_TONE_NUM=2;

  // Size of Sintable ( 1 -- 18 can be used, but 7 -- 14 recommended.)
  PG_BITS=9;
  PG_WIDTH=1 shl PG_BITS;

type
  //----------------------------------------------------------------------------
  //  Data types
  //----------------------------------------------------------------------------

{  uint32=cardinal;
   int32=longint;
  uint16=word;
   int16=smallint;
  uint8 =byte;
   int8 =shortint;}

  uint32=integer;
   int32=integer;
  uint16=integer;
   int16=integer;
  uint8 =integer;
   int8 =integer;

  puint32=^uint32;
  pint32=^int32;

  uint32array=array [0..PG_WIDTH-1] of uint32;
  puint32array=^uint32array;

  unsignedint=uint32;
  int=int32;
  signedshort=int16;
  unsignedshort=uint16;
  signedchar=int8;
  unsignedchar=uint8;

  // voice data
  tOPLL_PATCH = packed record
    TL,FB,EG,ML,AR,DR,SL,RR,KR,KL,AM,PM,WF:unsignedint;
  end;
  pOPLL_PATCH = ^tOPLL_PATCH;

  // Definition of envelope mode
  envnum = (SETTLE,ATTACK,DECAY,SUSHOLD,SUSTINE,RELEASE,FINISH);

  // slot
  tOPLL_SLOT = record
    patch:pOPLL_PATCH;

    type_:int;                       // 0 : modulator 1 : carrier

    // OUTPUT
    feedback:int32;
    output:array[0..5-1] of int32;  // Output value of slot

    // for Phase Generator (PG)
    sintbl:puint32array;            // Wavetable
    phase :uint32;                  // Phase
    dphase:uint32;                  // Phase increment amount
    pgout :uint32;                  // output

    // for Envelope Generator (EG)
    fnum     :int;                  // F-Number
    block    :int;                  // Block
    volume   :int;                  // Current volume
    sustine  :int;                  // Sustine 1 = ON, 0 = OFF
    tll      :uint32;               // Total Level + Key scale level
    rks      :uint32;               // Key scale offset (Rks)
    eg_mode  :envnum;               // Current state
    eg_phase :uint32;               // Phase
    eg_dphase:uint32;               // Phase increment amount
    egout    :uint32;               // output

    // refer to opll->
    plfo_pm:pint32;
    plfo_am:pint32;
  end;
  pOPLL_SLOT = ^tOPLL_SLOT;

  // Channel
  tOPLL_CH = record
    patch_number:int;
    key_status:int;
    mod_,car:pOPLL_SLOT;
  end;
  pOPLL_CH=^tOPLL_CH;

  // opll
  tOPLL = record
    adr:uint32;
    output:array[0..2-1] of int32;

    // Register
    reg:array[0..$40-1] of unsignedchar;
    slot_on_flag:array[0..18-1] of int;

    // Rythm Mode : 0 = OFF, 1 = ON
    rythm_mode:int;

    // Pitch Modulator
    pm_phase:uint32;
    lfo_pm:int32;

    // Amp Modulator
    am_phase:int32;
    lfo_am:int32;

    // Noise Generator
    noise_seed,
    whitenoise,
    noiseA,
    noiseB,
    noiseA_phase,
    noiseB_phase,
    noiseA_idx,
    noiseB_idx,
    noiseA_dphase,
    noiseB_dphase:uint32;

    // Channel & Slot
    ch:array[0..9-1] of pOPLL_CH;
    slot:array[0..18-1] of pOPLL_SLOT;

    // Voice Data
    patch:array[0..19*2-1] of pOPLL_PATCH;
    patch_update:array[0..2-1] of int; // flag for check patch update

    mask:uint32;

    masterVolume:int; // 0 min -- 64 -- 127 max (Linear)
  end;
  pOPLL=^tOPLL;

  tOPLL_PATCHarray = array[0..OPLL_TONE_NUM-1,0..(16+3)*2-1] of tOPLL_PATCH;

const
  //----------------------------------------------------------------------------
  //  Exposed constants
  //----------------------------------------------------------------------------
  // Mask
  OPLL_MASK_CH:array[0..9] of integer = ($0001,$0002,$0004,$0008,
                                         $0010,$0020,$0040,$0080,
                                         $0100,$0200);
  OPLL_MASK_HH  = 1 shl 9;
  OPLL_MASK_CYM = 1 shl 10;
  OPLL_MASK_TOM = 1 shl 11;
  OPLL_MASK_SD  = 1 shl 12;
  OPLL_MASK_BD  = 1 shl 13;
  OPLL_MASK_RYTHM = OPLL_MASK_HH or OPLL_MASK_CYM or OPLL_MASK_TOM or OPLL_MASK_SD or OPLL_MASK_BD;

  //----------------------------------------------------------------------------
  //  Procedure interface
  //----------------------------------------------------------------------------

// Initialise
procedure OPLL_init(c,r:uint32);
procedure OPLL_close;

// Create Object
function OPLL_new:pOPLL;
procedure OPLL_delete(opll:pOPLL);

// Setup
procedure OPLL_reset(opll:pOPLL);
procedure OPLL_reset_patch(opll:pOPLL;type_:integer);
procedure OPLL_setClock(c,r:uint32) ;

// Port/Register access
procedure OPLL_writeIO(opll:pOPLL;adr,val:uint32);
procedure OPLL_writeReg(opll:pOPLL;reg,data:uint32);

// Synthsize
function OPLL_calc(opll:pOPLL):int16;

// Misc
//procedure OPLL_copyPatch(opll:pOPLL;num:integer;patch:pOPLL_PATCH);
procedure OPLL_forceRefresh(opll:pOPLL);
//procedure dump2patch(dump:pbytearray;patch:pOPLL_PATCH);

// Channel Mask
function OPLL_setMask(opll:pOPLL;mask:uint32):uint32;
function OPLL_toggleMask(opll:pOPLL;mask:uint32):uint32;

implementation

uses Math;

const
  //----------------------------------------------------------------------------
  //  Constants
  //----------------------------------------------------------------------------

  // Default patch data - 2-D array 2 x (19*16)
  default_inst:packed array[0..OPLL_TONE_NUM-1,0..(16+3)*16-1] of unsignedchar
    = (( {$INCLUDE 'YM2413 voice.dat'} ),( {$INCLUDE 'VCR7 voice.dat'} ));

  // Phase increment counter
  DP_BITS=18;
  DP_WIDTH=1 shl DP_BITS;
  DP_BASE_BITS=DP_BITS - PG_BITS;

  // Dynamic range
  DB_STEP=0.375;
  DB_BITS=7;
  DB_MUTE=1 shl DB_BITS;

  // Dynamic range of envelope
  EG_STEP=0.375;
  EG_BITS=7;
  EG_MUTE=1 shl EG_BITS;

  // Dynamic range of total level
  TL_STEP=0.75;
  TL_BITS=6;
  TL_MUTE=1 shl TL_BITS;

  // Dynamic range of sustine level
  SL_STEP=3.0;
  SL_BITS=4;
  SL_MUTE=1 shl SL_BITS;

  // Volume of Noise (dB)
  DB_NOISE=24.0;

  // Bits for liner value
  DB2LIN_AMP_BITS=10;
  SLOT_AMP_BITS=DB2LIN_AMP_BITS;

  // Bits for envelope phase incremental counter
  EG_DP_BITS=22;
  EG_DP_WIDTH=1 shl EG_DP_BITS;

  // Bits for Pitch and Amp modulator
  PM_PG_BITS=8;
  PM_PG_WIDTH=1 shl PM_PG_BITS;
  PM_DP_BITS=16;
  PM_DP_WIDTH=1 shl PM_DP_BITS;
  AM_PG_BITS=8;
  AM_PG_WIDTH=1 shl AM_PG_BITS;
  AM_DP_BITS=16;
  AM_DP_WIDTH=1 shl AM_DP_BITS;

  // PM table is calcurated by PM_AMP * pow(2,PM_DEPTH*sin(x)/1200)
  PM_AMP_BITS=8;
  PM_AMP=1 shl PM_AMP_BITS;

  // PM speed(Hz) and depth(cent)
  PM_SPEED=6.4;
  PM_DEPTH=13.75;

  // AM speed(Hz) and depth(dB)
  AM_SPEED=3.7;
  AM_DEPTH=4.8;

  //----------------------------------------------------------------------------
  //  consts for "OPLL internal interfaces"
  //----------------------------------------------------------------------------

  SLOT_BD1=12;
  SLOT_BD2=13;
  SLOT_HH =14;
  SLOT_SD =15;
  SLOT_TOM=16;
  SLOT_CYM=17;

  //----------------------------------------------------------------------------
  //  Initialised vars
  //----------------------------------------------------------------------------

  noiseAtable:array[0..64-1] of int32=(
    -1,1,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,
    -1,1,0,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0,-1,1,0,0
  );

  noiseBtable:array[0..8-1] of int32=(
    -1,1,-1,1,0,0,0,0
  );

  // Empty voice data
  null_patch:tOPLL_PATCH = (TL:0;FB:0;EG:0;ML:0;AR:0;DR:0;SL:0;RR:0;KR:0;KL:0;AM:0;PM:0;WF:0);

var
  //----------------------------------------------------------------------------
  //  Variables
  //----------------------------------------------------------------------------

  // Sampling rate
  rate:uint32;
  // Input clock
  clk:uint32;

  // WaveTable for each envelope amp
  fullsintable:array[0..PG_WIDTH-1] of uint32;
  halfsintable:array[0..PG_WIDTH-1] of uint32;
  snaretable  :array[0..PG_WIDTH-1] of uint32;

  // LFO Table
  pmtable:array[0..PM_PG_WIDTH-1] of int32;
  amtable:array[0..AM_PG_WIDTH-1] of int32;

  // Noise and LFO
  pm_dphase,
  am_dphase:uint32;

  // dB to Liner table
  DB2LIN_TABLE:array[0..(DB_MUTE + DB_MUTE)*2-1] of int32;

  // Liner to Log curve conversion table (for Attack rate).
  AR_ADJUST_TABLE:array[0..1 shl EG_BITS-1] of uint32;

  // Basic voice Data
  default_patch:tOPLL_PATCHarray;

  // Phase incr table for Attack
  dphaseARTable:array[0..16-1,0..16-1] of uint32;
  // Phase incr table for Decay and Release
  dphaseDRTable:array[0..16-1,0..16-1] of uint32;

  // KSL + TL Table
  tllTable:array[0..16-1,0..8-1,0..1 shl TL_BITS-1,0..4-1] of uint32;
  rksTable:array[0..2-1,0..8-1,0..2-1] of int32;

  // Phase incr table for PG
  dphaseTable:array[0..512-1,0..8-1,0..16-1] of uint32;

const
  //----------------------------------------------------------------------------
  //  More constants (which need to be after some vars)
  //----------------------------------------------------------------------------

  waveform:array[0..{5}3-1] of puint32array = (@fullsintable,@halfsintable,@snaretable);


  //----------------------------------------------------------------------------
  //  Functions which do stuff done in #DEFINEs in the original
  //----------------------------------------------------------------------------
function EG2DB(d:integer):integer;
begin
  result:=trunc(d*EG_STEP/DB_STEP);
end;

function TL2EG(d:integer):integer;
begin
  result:=trunc(d*TL_STEP/EG_STEP);
end;

function SL2EG(d:integer):integer;
begin
  result:=trunc(d*SL_STEP/EG_STEP);
end;

function DB_POS(x:single):integer;
begin
  result:=trunc(x/DB_STEP);
end;

function DB_NEG(x:single):integer;
begin
  result:=trunc(DB_MUTE+DB_MUTE+x/DB_STEP);
end;

// Cut the lower b bit(s) off.
function HIGHBITS(c,b:integer):integer;
begin
  result:=c shr b;
end;

// Leave the lower b bit(s).
function LOWBITS(c,b:integer):integer;
begin
  result:=c and (1 shl b -1);
end;

// Expand x which is s bits to d bits.
function EXPAND_BITS(x,s,d:integer):integer;
begin
  result:=x shl (d-s);
end;

// Expand x which is s bits to d bits and fill expanded bits '1'
function EXPAND_BITS_X(x,s,d:integer):integer;
begin
  result:=(x shl (d-s)) or (1 shl (d-s) -1);
end;

// Adjust envelope speed which depends on sampling rate.
function rate_adjust(x:single):integer;
begin
  result:=Trunc(x*clk/72/rate + 0.5); // +0.5 to round
end;

{
function mod_(opll_:pOPLL;x:integer):pointer;
begin
  result:=opll_.ch[x].mod_;
end;

function car(opll_:pOPLL;x:integer):pointer;
begin
  result:=opll_.ch[x].car;
end;
}
//------------------------------------------------------------------------------
//  Code!!!
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
//  Create tables
//------------------------------------------------------------------------------

function Min(i,j:int32):int32;
begin
  if i<j then result:=i else result:=j;
end;

// Table for AR to LogCurve.
procedure makeAdjustTable;
var
  i:integer;
begin
  AR_ADJUST_TABLE[0]:=1 shl EG_BITS;
  for i:=1 to 12 do AR_ADJUST_TABLE[i]:=Trunc(1 shl EG_BITS - 1 - (1 shl EG_BITS)*ln(i)/ln(128));
end;

// Table for dB(0 -- (1<<DB_BITS)) to Liner(0 -- DB2LIN_AMP_WIDTH)
procedure makeDB2LinTable;
var
  i:integer;
begin
  for i:=0 to DB_MUTE+DB_MUTE-1 do begin
    DB2LIN_TABLE[i]:=Trunc((1 shl DB2LIN_AMP_BITS -1)*Power(10,-i*DB_STEP/20));
    if i>=DB_MUTE then DB2LIN_TABLE[i]:=0;
    DB2LIN_TABLE[i+DB_MUTE+DB_MUTE]:=-DB2LIN_TABLE[i];
  end;
end;

// Liner(+0.0 - +1.0) to dB((1<<DB_BITS) - 1 -- 0)
function lin2db(d:double):int32;
begin
  if d=0
  then Result:=DB_MUTE-1
  else Result:=Min(-Trunc(20.0*Log10(d)/DB_STEP),DB_MUTE-1); // 0 -- 128
end;

// Sin Table
procedure makeSinTable;
var
  i:integer;
begin
  for i:=0 to PG_WIDTH div 4-1 do begin
    fullsintable[i]:=lin2db(sin(2.0*pi*i/PG_WIDTH));
    snaretable[i]:=Trunc(6.0/DB_STEP);
  end;

  for i:=0 to PG_WIDTH div 4-1 do begin
    fullsintable[PG_WIDTH div 2-1-i]:=fullsintable[i];
    snaretable[PG_WIDTH div 2-1-i]:=snaretable[i];
  end;

  for i:=0 to PG_WIDTH div 4-1 do begin
    fullsintable[PG_WIDTH div 2+i]:=DB_MUTE+DB_MUTE+fullsintable[i];
    snaretable[PG_WIDTH div 2+i]:=DB_MUTE+DB_MUTE+snaretable[i];
  end;

  for i:=0              to PG_WIDTH div 2-1 do halfsintable[i]:=fullsintable[i];
  for i:=PG_WIDTH div 2 to PG_WIDTH      -1 do halfsintable[i]:=fullsintable[0];

  for i:=0 to 63 do begin
         if noiseAtable[i]>0 then noiseAtable[i]:=DB_POS(0)
    else if noiseAtable[i]<0 then noiseAtable[i]:=DB_NEG(0)
                             else noiseAtable[i]:=DB_MUTE-1;
  end;

  for i:=0 to 7 do begin
         if noiseBtable[i]>0 then noiseBtable[i]:=DB_POS(0)
    else if noiseBtable[i]<0 then noiseBtable[i]:=DB_NEG(0)
                             else noiseBtable[i]:=DB_MUTE-1;
  end;
end;

// Table for Pitch Modulator
procedure makePmTable;
var
  i:integer;
begin
  for i:=0 to PM_PG_WIDTH-1 do
    pmtable[i]:=trunc(PM_AMP*Power(2,pm_depth*sin(2.0*pi*i/PM_PG_WIDTH/1200)));
end;

// Table for Amp Modulator
procedure makeAmTable;
var
  i:integer;
begin
  for i:=0 to AM_PG_WIDTH-1 do
    amtable[i]:=Trunc(AM_DEPTH/2/DB_STEP*(1.0+sin(2.0*pi*i/PM_PG_WIDTH)));
end;

// Phase increment counter table
procedure makeDphaseTable;
const
  mltable:array[0..16-1] of uint32 = (1,1*2,2*2,3*2,4*2,5*2,6*2,7*2,8*2,9*2,10*2,10*2,12*2,12*2,15*2,15*2);
var
  fnum,block,ML:uint32;
begin
  for fnum:=0 to 512-1 do for block:=0 to 8-1 do for ML:=0 to 16-1 do
    dphaseTable[fnum,block,ML]:=rate_adjust(((fnum*mltable[ML]) shl block ) shr (20-DP_BITS));
end;

procedure makeTllTable;
const
  kltable:array[0..16-1] of uint32 = (
    // int(x*2) where x =
    // 0, 9,12,13.875,15,16.125,16.875,17.625,18,18.75,19.125,19.5,19.875,20.25,20.625,21
       0,18,24,27    ,30,32    ,33    ,35    ,36,37   ,38    ,39  ,39    ,40   ,41    ,42
  );
var
  tmp:int32;
  fnum,block,TL,KL:int;
begin
  for fnum:=0 to 16-1 do for block:=0 to 8-1 do for TL:=0 to 64-1 do for KL:=0 to 4-1 do
    if KL=0
    then tllTable[fnum,block,TL,KL]:=TL2EG(TL)
    else begin
      tmp:=kltable[fnum]- {dB2(3.000)} 6*(7-block);
      if tmp<=0
      then tllTable[fnum,block,TL,KL]:=TL2EG(TL)
      else tllTable[fnum,block,TL,KL]:=Trunc((tmp shr (3-KL))/EG_STEP)+TL2EG(TL);
    end;
end;

// Rate Table for Attack
procedure makeDphaseARTable;
var
  AR,Rks,RM,RL:integer;
begin
  for AR:=0 to 16-1 do for Rks:=0 to 16-1 do begin
    RM:=AR+(Rks shr 2);
    if RM>15 then RM:=15;
    RL:=Rks and 3;
    case AR of
      0: dphaseARTable[AR,Rks]:=0;
     15: dphaseARTable[AR,Rks]:=EG_DP_WIDTH;
    else dphaseARTable[AR,Rks]:=rate_adjust(3*(RL+4) shl (RM+1)) ;
    end;
  end;
end;

// Rate Table for Decay
procedure makeDphaseDRTable;
var
  DR,Rks,RM,RL:integer;
begin
  for DR:=0 to 16-1 do for Rks:=0 to 16-1 do begin
    RM:=DR+Rks shr 2;
    RL:=Rks and 3;
    if RM>15 then RM:=15;
    case DR of
      0: dphaseDRTable[DR,Rks]:=0;
    else dphaseDRTable[DR,Rks]:=rate_adjust((RL+4) shl (RM-1));
    end;
  end;
end;

procedure makeRksTable;
var
  fnum8,block,KR:integer;
begin
  for fnum8:=0 to 2-1 do for block:=0 to 8-1 do for KR:=0 to 2-1 do
    if KR<>0
    then rksTable[fnum8,block,KR]:=block shl 1 +fnum8
    else rksTable[fnum8,block,KR]:=block shr 1;
end;

// dump = flat array of values
// patch = array [0..1,0..(16+3)*2-1] of tOPLL_PATCH;
// i = 0..1
// j = 0..19-1
procedure dump2patch(dump:array of unsignedchar;var patch:tOPLL_PATCHarray;i,j:integer);
begin
  patch[i,j*2+0].AM:=dump[j*2+0] shr 7 and 1;
  patch[i,j*2+1].AM:=dump[j*2+1] shr 7 and 1;
  patch[i,j*2+0].PM:=dump[j*2+0] shr 6 and 1;
  patch[i,j*2+1].PM:=dump[j*2+1] shr 6 and 1;
  patch[i,j*2+0].EG:=dump[j*2+0] shr 5 and 1;
  patch[i,j*2+1].EG:=dump[j*2+1] shr 5 and 1;
  patch[i,j*2+0].KR:=dump[j*2+0] shr 4 and 1;
  patch[i,j*2+1].KR:=dump[j*2+1] shr 4 and 1;
  patch[i,j*2+0].ML:=dump[j*2+0] and 15;
  patch[i,j*2+1].ML:=dump[j*2+1] and 15;
  patch[i,j*2+0].KL:=dump[j*2+2] shr 6 and 3;
  patch[i,j*2+1].KL:=dump[j*2+3] shr 6 and 3;
  patch[i,j*2+0].TL:=dump[j*2+2] and 63;
  patch[i,j*2+0].FB:=dump[j*2+3] and 7;
  patch[i,j*2+0].WF:=dump[j*2+3] shr 3 and 1;
  patch[i,j*2+1].WF:=dump[j*2+3] shr 4 and 1;
  patch[i,j*2+0].AR:=dump[j*2+4] shr 4 and 15;
  patch[i,j*2+1].AR:=dump[j*2+5] shr 4 and 15;
  patch[i,j*2+0].DR:=dump[j*2+4] and 15;
  patch[i,j*2+1].DR:=dump[j*2+5] and 15;
  patch[i,j*2+0].SL:=dump[j*2+6] shr 4 and 15;
  patch[i,j*2+1].SL:=dump[j*2+7] shr 4 and 15;
  patch[i,j*2+0].RR:=dump[j*2+6] and 15;
  patch[i,j*2+1].RR:=dump[j*2+7] and 15;
end;

procedure makeDefaultPatch;
var
  i,j:integer;
begin
  for i:=0 to {(OPLL_TONE_NUM-1)}1 do
  for j:=0 to 19-1 do
//    dump2patch(pbytearray(integer(@default_inst[i])+j*16),default_patch[i,j*2]);
    dump2patch(default_inst[i],default_patch,i,j);
  // !!! Dodgy translation here... original C:
  //    dump2patch(default_inst[i]+j*16,&default_patch[i][j*2]) ;
end;

//------------------------------------------------------------------------------
//  Calc Parameters
//------------------------------------------------------------------------------

function calc_eg_dphase(slot:pOPLL_SLOT):uint32;
begin
  case slot.eg_mode of
  ATTACK : result:=dphaseARTable[slot.patch.AR,slot.rks];
  DECAY  : result:=dphaseDRTable[slot.patch.DR,slot.rks];
//  SUSHOLD: result:=0;
  SUSTINE: result:=dphaseDRTable[slot.patch.RR,slot.rks];
  RELEASE: if slot.sustine<>0
           then result:=dphaseDRTable[5,slot.rks]
           else if slot.patch.EG<>0
           then result:=dphaseDRTable[slot.patch.RR,slot.rks]
           else result:=dphaseDRTable[7,slot.rks];
//  FINISH: result:=0;
  else    result:=0;
  end;
  // I commented out SUSHOLD and FINISH since the else catches them
end;

//------------------------------------------------------------------------------
//  OPLL internal interfaces
//------------------------------------------------------------------------------

procedure UPDATE_PG(S:pOPLL_SLOT);
begin
  S.dphase:=dphaseTable[S.fnum,S.block,S.patch.ML];
end;

procedure UPDATE_TLL(S:pOPLL_SLOT);
begin
  if S.type_=0
  then s.tll:=tllTable[S.fnum shr 5,S.block,S.patch.TL,S.patch.KL]
  else s.tll:=tllTable[S.fnum shr 5,S.block,S.volume  ,S.patch.KL]
end;

procedure UPDATE_RKS(S:pOPLL_SLOT);
begin
  S.rks:=rksTable[S.fnum shr 8,S.block,S.patch.KR];
end;

procedure UPDATE_WF(S:pOPLL_SLOT);
begin
  S.sintbl:=waveform[S.patch.WF];
end;

procedure UPDATE_EG(S:pOPLL_SLOT);
begin
  S.eg_dphase:=calc_eg_dphase(S);
end;

procedure UPDATE_ALL(S:pOPLL_SLOT);
begin
  UPDATE_PG(S);
  UPDATE_TLL(S);
  UPDATE_RKS(S);
  UPDATE_WF(S);
  UPDATE_EG(S); // EG should be last
end;

// Force Refresh (When external program changes some parameters).
procedure OPLL_forceRefresh(opll:pOPLL);
var
  i:integer;
begin
  if opll=nil then exit;

  for i:=0 to 18-1 do begin
    UPDATE_PG(opll.slot[i]);
    UPDATE_RKS(opll.slot[i]);
    UPDATE_TLL(opll.slot[i]);
    UPDATE_WF(opll.slot[i]);
    UPDATE_EG(opll.slot[i]);
  end;
end;

// Slot key on
procedure slotOn(slot:pOPLL_SLOT);
begin
  slot.eg_mode:=ATTACK;
  slot.phase:=0;
  slot.eg_phase:=0;
end;

// Slot key off
procedure slotOff(slot:pOPLL_SLOT);
begin
  if slot.eg_mode=ATTACK
  then slot.eg_phase:=EXPAND_BITS(AR_ADJUST_TABLE[HIGHBITS(slot.eg_phase,EG_DP_BITS-EG_BITS)],EG_BITS,EG_DP_BITS)
  else slot.eg_mode:=RELEASE;
end;

// Channel key on
procedure keyOn(opll:pOPLL;i:integer);
begin
  if opll.slot_on_flag[i*2  ]=0 then slotOn(opll.ch[i].mod_);
  if opll.slot_on_flag[i*2+1]=0 then slotOn(opll.ch[i].car );
  opll.ch[i].key_status:=1;
end;

// Channel key off
procedure keyOff(opll:pOPLL;i:integer);
begin
  if opll.slot_on_flag[i*2+1]<>0 then slotOff(opll.ch[i].car );
  opll.ch[i].key_status:=0;
end;

procedure keyOn_BD (opll:pOPLL); begin keyOn(opll,6); end;
procedure keyOn_SD (opll:pOPLL); begin if opll.slot_on_flag[SLOT_SD] =0 then slotOn(opll.ch[7].car ); end;
procedure keyOn_TOM(opll:pOPLL); begin if opll.slot_on_flag[SLOT_TOM]=0 then slotOn(opll.ch[8].mod_); end;
procedure keyOn_HH (opll:pOPLL); begin if opll.slot_on_flag[SLOT_HH] =0 then slotOn(opll.ch[7].mod_); end;
procedure keyOn_CYM(opll:pOPLL); begin if opll.slot_on_flag[SLOT_CYM]=0 then slotOn(opll.ch[8].car ); end;

// Drum key off
procedure keyOff_BD (opll:pOPLL); begin keyOff(opll,6); end;
procedure keyOff_SD (opll:pOPLL); begin if opll.slot_on_flag[SLOT_SD] <>0 then slotOff(opll.ch[7].car ); end;
procedure keyOff_TOM(opll:pOPLL); begin if opll.slot_on_flag[SLOT_TOM]<>0 then slotOff(opll.ch[8].mod_); end;
procedure keyOff_HH (opll:pOPLL); begin if opll.slot_on_flag[SLOT_HH] <>0 then slotOff(opll.ch[7].mod_); end;
procedure keyOff_CYM(opll:pOPLL); begin if opll.slot_on_flag[SLOT_CYM]<>0 then slotOff(opll.ch[8].car ); end;

// Change a voice
procedure setPatch(opll:pOPLL;i,num:integer);
begin
  opll.ch[i].patch_number:=num;
  opll.ch[i].mod_.patch:=opll.patch[num*2+0];
  opll.ch[i].car.patch:=opll.patch[num*2+1];
end;

// Change a rythm voice
procedure setSlotPatch(slot:pOPLL_SLOT;patch:pOPLL_PATCH);
begin
  slot.patch:=patch;
end;

// Set sustine parameter
procedure setSustine(opll:pOPLL;c,sustine:integer);
begin
  opll.ch[c].car.sustine:=sustine;
  if opll.ch[c].mod_.type_<>0 then opll.ch[c].mod_.sustine:=sustine;
end;

// Volume : 6bit ( Volume register << 2 )
procedure setVolume(opll:pOPLL;c,volume:integer);
begin
  opll.ch[c].car.volume:=volume;
end;

procedure setSlotVolume(slot:pOPLL_SLOT;volume:integer);
begin
  slot.volume:=volume;
end;

// Set F-Number ( fnum : 9bit )
procedure setFnumber(opll:pOPLL;c,fnum:integer);
begin
  opll.ch[c].car.fnum:=fnum;
  opll.ch[c].mod_.fnum:=fnum;
end;

// Set Block data (block : 3bit )
procedure setBlock(opll:pOPLL;c,block:integer);
begin
  opll.ch[c].car.block:=block;
  opll.ch[c].mod_.block:=block;
end;

// Change Rythm Mode
procedure setRythmMode(opll:pOPLL;mode:integer);
begin
  opll.rythm_mode:=mode;
  if mode<>0 then begin
    opll.ch[6].patch_number:=16;
    opll.ch[7].patch_number:=17;
    opll.ch[8].patch_number:=18;
    setSlotPatch(opll.slot[SLOT_BD1],opll.patch[16*2+0]);
    setSlotPatch(opll.slot[SLOT_BD2],opll.patch[16*2+1]);
    setSlotPatch(opll.slot[SLOT_HH] ,opll.patch[17*2+0]);
    setSlotPatch(opll.slot[SLOT_SD] ,opll.patch[17*2+1]);
    opll.slot[SLOT_HH].type_:=1;
    setSlotPatch(opll.slot[SLOT_TOM],opll.patch[18*2+0]);
    setSlotPatch(opll.slot[SLOT_CYM],opll.patch[18*2+1]);
    opll.slot[SLOT_TOM].type_:=1;
  end else begin
    setPatch(opll,6,opll.reg[$36] shr 4);
    setPatch(opll,7,opll.reg[$37] shr 4);
    opll.slot[SLOT_HH].type_:=0;
    setPatch(opll,8,opll.reg[$38] shr 4);
    opll.slot[SLOT_TOM].type_:=0;
  end;

  if opll.slot_on_flag[SLOT_BD1]=0 then opll.slot[SLOT_BD1].eg_mode:=FINISH;
  if opll.slot_on_flag[SLOT_BD2]=0 then opll.slot[SLOT_BD2].eg_mode:=FINISH;
  if opll.slot_on_flag[SLOT_HH] =0 then opll.slot[SLOT_HH] .eg_mode:=FINISH;
  if opll.slot_on_flag[SLOT_SD] =0 then opll.slot[SLOT_SD] .eg_mode:=FINISH;
  if opll.slot_on_flag[SLOT_TOM]=0 then opll.slot[SLOT_TOM].eg_mode:=FINISH;
  if opll.slot_on_flag[SLOT_CYM]=0 then opll.slot[SLOT_CYM].eg_mode:=FINISH;
end;

procedure OPLL_copyPatch(opll:pOPLL;num:integer;var patch:tOPLL_PATCH);
begin
  Move(opll.patch[num]^,patch,sizeof(tOPLL_PATCH));
//  memcpy(opll->patch[num],patch,sizeof(OPLL_PATCH)) ;
end;

//------------------------------------------------------------------------------
//  Initializing
//------------------------------------------------------------------------------

procedure OPLL_SLOT_reset(slot:pOPLL_SLOT);
begin
  slot.sintbl:=waveform[0];
  slot.phase:=0;
  slot.dphase:=0;
  slot.output[0]:=0;
  slot.output[1]:=0;
  slot.feedback:=0;
  slot.eg_mode:=SETTLE;
  slot.eg_phase:=EG_DP_WIDTH;
  slot.eg_dphase:=0;
  slot.rks:=0;
  slot.tll:=0;
  slot.sustine:=0;
  slot.fnum:=0;
  slot.block:=0;
  slot.volume:=0;
  slot.pgout:=0;
  slot.egout:=0;
  slot.patch:=@null_patch;
end;

function OPLL_SLOT_new:pOPLL_SLOT;
//var
//  slot:pOPLL_SLOT;
begin
//  slot:=allocmem(sizeof(OPLL_SLOT));
//  if slot=nil then result:=nil else result:=slot; // ??? don't see why this if is needed
  result:=allocmem(sizeof(tOPLL_SLOT));
end;

procedure OPLL_SLOT_delete(slot:pOPLL_SLOT);
begin
  FreeMem(slot);
end;

procedure OPLL_CH_reset(ch:pOPLL_CH);
begin
  if ch.mod_<>nil then OPLL_SLOT_reset(ch.mod_);
  if ch.car <>nil then OPLL_SLOT_reset(ch.car);
  ch.key_status:=0;
end;

function OPLL_CH_new:pOPLL_CH;
var
  ch:pOPLL_CH;
  mod_,car:pOPLL_SLOT;
begin
  mod_:=OPLL_SLOT_new;
  if mod_=nil then begin
    result:=nil;
    exit;
  end;

  car:=OPLL_SLOT_new();
  if car=nil then begin
    OPLL_SLOT_delete(mod_);
    result:=nil;
    exit;
  end;

  ch:=AllocMem(sizeof(tOPLL_CH));
  if ch=nil then begin
    OPLL_SLOT_delete(mod_);
    OPLL_SLOT_delete(car);
    result:=nil;
    exit;
  end;

  mod_.type_:=0;
  car.type_:=1;
  ch.mod_:=mod_;
  ch.car:=car;

  result:=ch;
end;

procedure OPLL_CH_delete(ch:pOPLL_CH);
begin
  OPLL_SLOT_delete(ch.mod_);
  OPLL_SLOT_delete(ch.car);
  FreeMem(ch);
end;

// Whoops! I need a calloc (allocmem for records). Here's DELPHI-JEDI's...
//function calloc(nmemb, size: cardinal): Pointer; cdecl;
//begin
//  Result := AllocMem(nmemb * size);
//end;

function OPLL_new:pOPLL;
var
  opll:pOPLL;
  ch:array[0..9-1] of pOPLL_CH;
  patch:array[0..19*2-1] of pOPLL_PATCH;
  i,j:integer;
begin
  result:=nil; // for early exits
  for i:=0 to 19*2-1 do begin // Allocate 19*2 patches
    patch[i]:=allocmem(sizeof(tOPLL_PATCH));
    if patch[i]=nil then begin
      for j:=i downto 1 do FreeMem(patch[j-1]);
      exit;
    end;
  end;

  for i:=0 to 9-1 do begin // Allocate 9 channels
    ch[i]:=OPLL_CH_new;
    if ch[i]=nil then begin
      for j:=i downto 1 do OPLL_CH_delete(ch[j-1]);
      for j:=0 to 19*2-1 do FreeMem(patch[j]);
      exit;
    end;
  end;

  opll:=AllocMem(sizeof(tOPLL)); // Allocate OPLL
  if opll=nil then exit;

  for i:=0 to 19*2-1 do opll.patch[i]:=patch[i]; // Attach patches

  for i:=0 to 9-1 do begin // Attach channels and their modulator/carrier values
    opll.ch[i]:=ch[i];
    opll.slot[i*2+0]:=ch[i].mod_;
    opll.slot[i*2+1]:=ch[i].car;
  end;

  for i:=0 to 18-1 do begin
    opll.slot[i].plfo_am:=@opll.lfo_am;
    opll.slot[i].plfo_pm:=@opll.lfo_pm;
  end;

  opll.mask:=0;

  OPLL_reset(opll);
  OPLL_reset_patch(opll,0);

  opll.masterVolume:=32;

  result:=opll;
end;

procedure OPLL_delete(opll:pOPLL);
var
  i:integer;
begin
  for i:=0 to 9-1 do OPLL_CH_delete(opll.ch[i]);

  for i:=0 to 19*2-1 do FreeMem(opll.patch[i]);

  FreeMem(opll);
end;

// Reset patch datas by system default.
procedure OPLL_reset_patch(opll:pOPLL;type_:integer);
var
  i:integer;
begin
  for i:=0 to 19*2-1 do OPLL_copyPatch(opll,i,default_patch[type_ mod OPLL_TONE_NUM,i]);
end;

// Reset whole of OPLL except patch datas.
procedure OPLL_reset(opll:pOPLL);
var
  i:integer;
begin
  if opll=nil then exit;

  opll.adr:=0;

  opll.output[0]:=0;
  opll.output[1]:=0;

  opll.pm_phase:=0;
  opll.am_phase:=0;

  opll.noise_seed:=$ffff;
  opll.noiseA:=0;
  opll.noiseB:=0;
  opll.noiseA_phase:=0;
  opll.noiseB_phase:=0;
  opll.noiseA_dphase:=0;
  opll.noiseB_dphase:=0;
  opll.noiseA_idx:=0;
  opll.noiseB_idx:=0;

  for i:=0 to 9-1 do begin
    OPLL_CH_reset(opll.ch[i]);
    setPatch(opll,i,0);
  end;

  for i:=0 to $40-1 do OPLL_writeReg(opll,i,0);
end;

procedure OPLL_setClock(c,r:uint32);
begin
  clk:=c;
  rate:=r;
  makeDphaseTable;
  makeDphaseARTable;
  makeDphaseDRTable;
  pm_dphase:=rate_adjust(Trunc(PM_SPEED*PM_DP_WIDTH/(clk/72)));
  am_dphase:=rate_adjust(Trunc(AM_SPEED*AM_DP_WIDTH/(clk/72)));
end;

procedure OPLL_init(c,r:uint32);
begin
  makePmTable;
  makeAmTable;
  makeDB2LinTable;
  makeAdjustTable;
  makeTllTable;
  makeRksTable;
  makeSinTable;
  makeDefaultPatch;
  OPLL_setClock(c,r);
end;

procedure OPLL_close;
begin
{}
end;

//------------------------------------------------------------------------------
//  Generate wave data
//------------------------------------------------------------------------------

// Convert Amp(0 to EG_HEIGHT) to Phase(0 to 2PI).
function wave2_2pi(e:integer):integer;
begin
  if SLOT_AMP_BITS - PG_BITS > 0
  then result:=e shr (SLOT_AMP_BITS - PG_BITS)
  else result:=e shl (PG_BITS - SLOT_AMP_BITS);
end;

// Convert Amp(0 to EG_HEIGHT) to Phase(0 to 4PI).
function wave2_4pi(e:integer):integer;
begin
       if SLOT_AMP_BITS-PG_BITS-1=0 then result:=e
  else if SLOT_AMP_BITS-PG_BITS-1>0 then result:=e shr (SLOT_AMP_BITS-PG_BITS-1)
                                    else result:=e shl (1+PG_BITS-SLOT_AMP_BITS);
end;

//* Convert Amp(0 to EG_HEIGHT) to Phase(0 to 8PI).
function wave2_8pi(e:integer):integer;
begin
       if SLOT_AMP_BITS-PG_BITS-2=0 then result:=e
  else if SLOT_AMP_BITS-PG_BITS-2>0 then result:=e shr (SLOT_AMP_BITS-PG_BITS-2)
                                    else result:=e shl (2+PG_BITS-SLOT_AMP_BITS);
end;

// 16bit rand
function mrand(seed:uint32):uint32;
begin
  result:=(seed shr 15) xor ((seed shr 12) and 1) or ((seed shl 1) and $ffff);
end;

function DEC(db:uint32):uint32;
begin
  if db<DB_MUTE+DB_MUTE
  then result:=Min(db+DB_POS(0.375*2),DB_MUTE-1)
  else result:=Min(db+DB_POS(0.375*2),DB_MUTE+DB_MUTE+DB_MUTE-1);
end;

// Update Noise unit
procedure update_noise(opll:pOPLL);
begin
  opll.noise_seed:=mrand(opll.noise_seed);
  opll.whitenoise:=opll.noise_seed and 1;

  opll.noiseA_phase:=(opll.noiseA_phase + opll.noiseA_dphase);
  opll.noiseB_phase:=(opll.noiseB_phase + opll.noiseB_dphase);

  if opll.noiseA_phase<1 shl 11 then begin
    if opll.noiseA_phase>16 then opll.noiseA:=DB_MUTE-1;
  end else begin
    opll.noiseA_phase:=opll.noiseA_phase and ((1 shl 11)-1);
    opll.noiseA_idx:=(opll.noiseA_idx+1) and 63;
    opll.noiseA:=noiseAtable[opll.noiseA_idx];
  end;

  if opll.noiseB_phase<1 shl 12 then begin
    if opll.noiseB_phase>16 then opll.noiseB:=DB_MUTE-1;
  end else begin
    opll.noiseB_phase:=opll.noiseB_phase and ((1 shl 12)-1);
    opll.noiseB_idx:=(opll.noiseB_idx+1) and 7;
    opll.noiseB:=noiseBtable[opll.noiseB_idx];
  end;
end;

// Update AM, PM unit
procedure update_ampm(opll:pOPLL);
begin
  opll.pm_phase:=(opll.pm_phase+pm_dphase) and (PM_DP_WIDTH-1);
  opll.am_phase:=(opll.am_phase+am_dphase) and (AM_DP_WIDTH-1);
  opll.lfo_am:=amtable[HIGHBITS(opll.am_phase, AM_DP_BITS-AM_PG_BITS)];
  opll.lfo_pm:=pmtable[HIGHBITS(opll.pm_phase, PM_DP_BITS-PM_PG_BITS)];
end;

// PG
function calc_phase(slot:pOPLL_SLOT):uint32;
begin
  if slot.patch.PM<>0
  then slot.phase:=slot.phase+(slot.dphase*slot.plfo_pm^) shr PM_AMP_BITS
  else slot.phase:=slot.phase+slot.dphase;

  slot.phase:=slot.phase and (DP_WIDTH-1);

  result:=HIGHBITS(slot.phase,DP_BASE_BITS);
end;

// EG
function calc_envelope(slot:pOPLL_SLOT):uint32;
//  function S2E(x:integer):integer;
//  begin
//    result:=SL2EG(Trunc(x/SL_STEP)) shl (EG_DP_BITS-EG_BITS);
//  end;
const
//  SLVals:array[0..16-1] of byte = (0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,48);
  // array of SL2EG(x) where
  // SL2EG(x) = trunc(x/SL_STEP) shl (EG_DP_BITS-EG_BITS)
  //          = trunc(x/3)       shl  22-7
  //          = trunc(x/3)       shl  15
  // x        =                 0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,48);
  SL:array[0..16-1] of int32 = (0,
                                1 shl 15,
                                2 shl 15,
                                3 shl 15,
                                4 shl 15,
                                5 shl 15,
                                6 shl 15,
                                7 shl 15,
                                8 shl 15,
                                9 shl 15,
                               10 shl 15,
                               11 shl 15,
                               12 shl 15,
                               13 shl 15,
                               14 shl 15,
                               16 shl 15);
var
  egout:uint32;
begin
//  for i:=0 to 16-1 do SL[i]:=S2E(SLVals[i]);
  case slot.eg_mode of
    ATTACK: begin
      Inc(slot.eg_phase,slot.eg_dphase);
      if (EG_DP_WIDTH and slot.eg_phase)<>0 then begin
        egout:=0;
        slot.eg_phase:=0;
        slot.eg_mode:=DECAY;
        UPDATE_EG(slot);
      end
      else egout:=AR_ADJUST_TABLE[HIGHBITS(slot.eg_phase,EG_DP_BITS-EG_BITS)];
    end;
    DECAY: begin
      Inc(slot.eg_phase,slot.eg_dphase);
      egout:=HIGHBITS(slot.eg_phase,EG_DP_BITS-EG_BITS);
      if (slot.eg_phase>=SL[slot.patch.SL]) then
      begin
        if slot.patch.EG<>0 then begin
          slot.eg_phase:=SL[slot.patch.SL];
	  slot.eg_mode:=SUSHOLD;
          UPDATE_EG(slot);
        end else begin
          slot.eg_phase:=SL[slot.patch.SL];
          slot.eg_mode:=SUSTINE;
          UPDATE_EG(slot);
        end;
        egout:=HIGHBITS(slot.eg_phase,EG_DP_BITS-EG_BITS);
      end;
    end;
    SUSHOLD: begin
      egout:=HIGHBITS(slot.eg_phase,EG_DP_BITS-EG_BITS);
      if slot.patch.EG=0 then begin
        slot.eg_mode:=SUSTINE;
        UPDATE_EG(slot);
      end;
    end;
    SUSTINE,RELEASE: begin
      Inc(slot.eg_phase,slot.eg_dphase);
      egout:=HIGHBITS(slot.eg_phase,EG_DP_BITS-EG_BITS);
      if egout>=1 shl EG_BITS then begin
        slot.eg_mode:=FINISH;
        egout:=1 shl EG_BITS -1;
      end;
    end;
//    FINISH: egout:=1 shl EG_BITS -1;
    else egout:=1 shl EG_BITS -1;
  end;

  if (slot.patch.AM<>0)
  then egout:=EG2DB(egout+slot.tll)+slot.plfo_am^
  else egout:=EG2DB(egout+slot.tll);

  if (egout>=DB_MUTE) then egout:=DB_MUTE-1;

  result:=egout;
end;

function calc_slot_car(slot:pOPLL_SLOT;fm:int32):int32;
begin
  slot.egout:=calc_envelope(slot);
  slot.pgout:=calc_phase(slot);
  if slot.egout>=DB_MUTE-1
  then result:=0
  else result:=DB2LIN_TABLE[slot.sintbl[(slot.pgout+wave2_8pi(fm)) and (PG_WIDTH-1)]+slot.egout];
end;

function calc_slot_mod(slot:pOPLL_SLOT):int32;
var
  fm:int32;
begin
  slot.output[1]:=slot.output[0];
  slot.egout:=calc_envelope(slot);
  slot.pgout:=calc_phase(slot);

  if slot.egout>=DB_MUTE-1
  then slot.output[0]:=0
  else if slot.patch.FB<>0 then begin
    fm:=wave2_4pi(slot.feedback) shr (7 - slot.patch.FB);
    slot.output[0]:=DB2LIN_TABLE[slot.sintbl[(slot.pgout+fm) and (PG_WIDTH-1)] + slot.egout] ;
  end else begin
    slot.output[0]:=DB2LIN_TABLE[slot.sintbl[slot.pgout] + slot.egout];
  end;

  slot.feedback:=(slot.output[1]+slot.output[0]) shr 1;

  result:=slot.feedback;
end;

function calc_slot_tom(slot:pOPLL_SLOT):int32;
begin
  slot.egout:=calc_envelope(slot);
  slot.pgout:=calc_phase(slot);
  if slot.egout>=DB_MUTE-1
  then result:=0
  else result:=DB2LIN_TABLE[slot.sintbl[slot.pgout]+slot.egout];
end;

// calc SNARE slot
function calc_slot_snare(slot:pOPLL_SLOT;whitenoise:uint32):int32;
begin
  slot.egout:=calc_envelope(slot);
  slot.pgout:=calc_phase(slot);
  if slot.egout>=DB_MUTE-1
  then result:=0
  else if whitenoise<>0
  then result:=DB2LIN_TABLE[snaretable[slot.pgout]+slot.egout]+DB2LIN_TABLE[slot.egout+6]
  else result:=DB2LIN_TABLE[snaretable[slot.pgout]+slot.egout] ;
end;

function calc_slot_cym(slot:pOPLL_SLOT;a,b,c:int32):int32;
begin
  slot.egout:=calc_envelope(slot);
  if slot.egout>=DB_MUTE-1
  then result:=0
  else result:=DB2LIN_TABLE[slot.egout+a]+
             ((DB2LIN_TABLE[slot.egout+b]+
               DB2LIN_TABLE[slot.egout+c]) shr 2);
end;

function calc_slot_hat(slot:pOPLL_SLOT;a,b,c:int32;whitenoise:uint32):int32;
begin
  slot.egout:=calc_envelope(slot);
  if slot.egout>=DB_MUTE-1
  then result:=0
  else if whitenoise<>0
  then result:=DB2LIN_TABLE[slot.egout+a]+
             ((DB2LIN_TABLE[slot.egout+b]+
               DB2LIN_TABLE[slot.egout+c]) shr 2)
  else result:=0;
end;

function OPLL_calc(opll:pOPLL):int16;
var
  inst,perc,out_,rythmC,rythmH:int32;
  i:integer;
begin
  inst:=0;perc:=0;//out_:=0;
//  rythmC:=0;rythmH:=0;

  update_ampm(opll);
  update_noise(opll);

  for i:=0 to 6-1 do
    if (not (opll.mask and OPLL_MASK_CH[i]<>0)
        and (opll.ch[i].car.eg_mode<>FINISH))
    then Inc(inst,calc_slot_car(opll.ch[i].car,calc_slot_mod(opll.ch[i].mod_)));

  if opll.rythm_mode=0 then begin
    for i:=6 to 9-1 do
    if (not (opll.mask and OPLL_MASK_CH[i]<>0)
        and (opll.ch[i].car.eg_mode<>FINISH))
    then Inc(inst,calc_slot_car(opll.ch[i].car,calc_slot_mod(opll.ch[i].mod_)));
  end else begin
    opll.ch[7].mod_.pgout:=calc_phase(opll.ch[7].mod_);
    opll.ch[8].car .pgout:=calc_phase(opll.ch[8].car );

    if opll.ch[7].mod_.phase<256 then rythmH:=DB_NEG(12.0) else rythmH:=DB_MUTE-1;
    if opll.ch[8].car .phase<256 then rythmC:=DB_NEG(12.0) else rythmC:=DB_MUTE-1;

    if (opll.mask and OPLL_MASK_BD =0) and (opll.ch[6].car.eg_mode<>FINISH)
    then Inc(perc,calc_slot_car(opll.ch[6].car,calc_slot_mod(opll.ch[6].mod_)));

    if (opll.mask and OPLL_MASK_HH =0) and (opll.ch[6].car.eg_mode<>FINISH)
    then Inc(perc,calc_slot_hat(opll.ch[7].mod_,opll.noiseA,opll.noiseB,rythmH,opll.whitenoise));

    if (opll.mask and OPLL_MASK_SD =0) and (opll.ch[6].car.eg_mode<>FINISH)
    then Inc(perc,calc_slot_snare(opll.ch[7].car,opll.whitenoise));

    if (opll.mask and OPLL_MASK_TOM=0) and (opll.ch[6].car.eg_mode<>FINISH)
    then Inc(perc,calc_slot_tom(opll.ch[8].mod_));

    if (opll.mask and OPLL_MASK_CYM=0) and (opll.ch[6].car.eg_mode<>FINISH)
    then Inc(perc,calc_slot_cym(opll.ch[8].car,opll.noiseA,opll.noiseB,rythmC));
  end;

  if SLOT_AMP_BITS>8 then begin
    inst:=(inst shr (SLOT_AMP_BITS-8));
    perc:=(perc shr (SLOT_AMP_BITS-9));
  end else begin
    inst:=(inst shl (8-SLOT_AMP_BITS));
    perc:=(perc shl (9-SLOT_AMP_BITS));
  end;

  out_:=((inst+perc)*opll.masterVolume) shr 2;

  if out_>32767  then out_:=32767
  else if out_<-32768 then out_:=-32768;

  result:=out_;
end;

function OPLL_setMask(opll:pOPLL;mask:uint32):uint32;
var
  ret:uint32;
begin
  if opll<>nil then begin
    ret:=opll.mask;
    opll.mask:=mask;
    result:=ret;
  end else result:=0;
end;

function OPLL_toggleMask(opll:pOPLL;mask:uint32):uint32;
var
  ret:uint32;
begin
  if opll<>nil then begin
    ret:=opll.mask;
    opll.mask:=opll.mask xor mask;
    result:=ret;
  end else result:=0;
end;

//------------------------------------------------------------------------------
//  Interfaces
//------------------------------------------------------------------------------

procedure OPLL_writeReg(opll:pOPLL;reg,data:uint32);
var
  i,v,ch:integer;
begin
  data:=data and $ff;
  reg:=reg and $3f;

  case reg of
    $00: begin
      opll.patch[0].AM:=(data shr 7) and 1;
      opll.patch[0].PM:=(data shr 6) and 1;
      opll.patch[0].EG:=(data shr 5) and 1;
      opll.patch[0].KR:=(data shr 4) and 1;
      opll.patch[0].ML:=(data) and 15;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then begin
        UPDATE_PG (opll.ch[i].mod_);
        UPDATE_RKS(opll.ch[i].mod_);
        UPDATE_EG (opll.ch[i].mod_);
      end;
    end;

    $01: begin
      opll.patch[1].AM:=(data shr 7) and 1;
      opll.patch[1].PM:=(data shr 6) and 1;
      opll.patch[1].EG:=(data shr 5) and 1;
      opll.patch[1].KR:=(data shr 4) and 1;
      opll.patch[1].ML:=(data) and 15;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then begin
          UPDATE_PG (opll.ch[i].car);
          UPDATE_RKS(opll.ch[i].car);
          UPDATE_EG (opll.ch[i].car);
      end;
    end;

    $02: begin
      opll.patch[0].KL:=(data shr 6) and 3 ;
      opll.patch[0].TL:=(data) and 63 ;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then UPDATE_TLL(opll.ch[i].mod_);
    end;

    $03: begin
      opll.patch[1].KL:=(data shr 6) and 3;
      opll.patch[1].WF:=(data shr 4) and 1;
      opll.patch[0].WF:=(data shr 3) and 1;
      opll.patch[0].FB:=(data) and 7 ;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then begin
        UPDATE_WF(opll.ch[i].mod_);
        UPDATE_WF(opll.ch[i].car);
      end;
    end;

    $04: begin
      opll.patch[0].AR:=(data shr 4) and 15;
      opll.patch[0].DR:=(data) and 15;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then UPDATE_EG(opll.ch[i].mod_);
    end;

    $05: begin
      opll.patch[1].AR:=(data shr 4) and 15;
      opll.patch[1].DR:=(data) and 15;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then UPDATE_EG(opll.ch[i].car);
    end;

    $06: begin
      opll.patch[0].SL:=(data shr 4) and 15;
      opll.patch[0].RR:=(data) and 15;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then UPDATE_EG(opll.ch[i].mod_);
    end;

    $07: begin
      opll.patch[1].SL:=(data shr 4) and 15;
      opll.patch[1].RR:=(data) and 15;
      for i:=0 to 9-1 do if opll.ch[i].patch_number=0 then UPDATE_EG(opll.ch[i].car);
    end;

    $0e: begin
      if opll.rythm_mode<>0 then begin
        opll.slot_on_flag[SLOT_BD1]:=(opll.reg[$0e] and $10) or (opll.reg[$26] and $10);
        opll.slot_on_flag[SLOT_BD2]:=(opll.reg[$0e] and $10) or (opll.reg[$26] and $10);
        opll.slot_on_flag[SLOT_SD] :=(opll.reg[$0e] and $08) or (opll.reg[$27] and $10);
        opll.slot_on_flag[SLOT_HH] :=(opll.reg[$0e] and $01) or (opll.reg[$27] and $10);
        opll.slot_on_flag[SLOT_TOM]:=(opll.reg[$0e] and $04) or (opll.reg[$28] and $10);
        opll.slot_on_flag[SLOT_CYM]:=(opll.reg[$0e] and $02) or (opll.reg[$28] and $10);
      end else begin
        opll.slot_on_flag[SLOT_BD1]:=(opll.reg[$26] and $10);
        opll.slot_on_flag[SLOT_BD2]:=(opll.reg[$26] and $10);
        opll.slot_on_flag[SLOT_SD] :=(opll.reg[$27] and $10);
        opll.slot_on_flag[SLOT_HH] :=(opll.reg[$27] and $10);
        opll.slot_on_flag[SLOT_TOM]:=(opll.reg[$28] and $10);
        opll.slot_on_flag[SLOT_CYM]:=(opll.reg[$28] and $10);
      end;

      if data shr 5 and 1 xor opll.rythm_mode <> 0 then setRythmMode(opll,(data and 32) shr 5);

      if opll.rythm_mode<>0 then begin
        if data and $10 <>0 then keyOn_BD (opll) else keyOff_BD (opll);
        if data and $8  <>0 then keyOn_SD (opll) else keyOff_SD (opll);
        if data and $4  <>0 then keyOn_TOM(opll) else keyOff_TOM(opll);
        if data and $2  <>0 then keyOn_CYM(opll) else keyOff_CYM(opll);
        if data and $1  <>0 then keyOn_HH (opll) else keyOff_HH (opll);
      end;

      UPDATE_ALL(opll.ch[6].mod_);
      UPDATE_ALL(opll.ch[6].car );
      UPDATE_ALL(opll.ch[7].mod_);
      UPDATE_ALL(opll.ch[7].car );
      UPDATE_ALL(opll.ch[8].mod_);
      UPDATE_ALL(opll.ch[8].car );
    end;

    $10..$18: begin
      ch:=reg-$10;
      setFnumber(opll,ch,data+opll.reg[$20+ch] and 1 shl 8);
      UPDATE_ALL(opll.ch[ch].mod_);
      UPDATE_ALL(opll.ch[ch].car );
      case reg of
        $17: opll.noiseA_dphase:=(data+((opll.reg[$27] and 1) shl 8)) shl ((opll.reg[$27] shr 1) and 7);
        $18: opll.noiseB_dphase:=(data+((opll.reg[$28] and 1) shl 8)) shl ((opll.reg[$28] shr 1) and 7);
      end;
    end;

    $20..$28: begin
      ch:=reg-$20;
      setFnumber(opll,ch,((data and 1) shl 8)+opll.reg[$10+ch]);
      setBlock(opll,ch,(data shr 1) and 7);
      opll.slot_on_flag[ch*2]:=(opll.reg[reg]) and $10;
      opll.slot_on_flag[ch*2+1]:=opll.slot_on_flag[ch*2];

      if opll.rythm_mode<>0 then case reg of
        $26: begin
          opll.slot_on_flag[SLOT_BD1]:=opll.slot_on_flag[SLOT_BD1] or ((opll.reg[$0e]) and $10);
          opll.slot_on_flag[SLOT_BD2]:=opll.slot_on_flag[SLOT_BD2] or ((opll.reg[$0e]) and $10);
        end;
        $27: begin
          opll.noiseA_dphase:=(((data and 1) shl 8)+opll.reg[$17]) shl ((data shr 1) and 7);
          opll.slot_on_flag[SLOT_SD]:=opll.slot_on_flag[SLOT_SD] or ((opll.reg[$0e]) and $08);
          opll.slot_on_flag[SLOT_HH]:=opll.slot_on_flag[SLOT_HH] or ((opll.reg[$0e]) and $01);
        end;
        $28: begin
          opll.noiseB_dphase:=(((data and 1) shl 8)+opll.reg[$18]) shl ((data shr 1) and 7);
          opll.slot_on_flag[SLOT_TOM]:=opll.slot_on_flag[SLOT_TOM] or ((opll.reg[$0e]) and $04);
          opll.slot_on_flag[SLOT_CYM]:=opll.slot_on_flag[SLOT_CYM] or ((opll.reg[$0e]) and $02);
        end;
      end;

      if (opll.reg[reg] xor data) and $20 <>0 then setSustine(opll,ch,(data shr 5) and 1);
      if data and $10 <>0 then keyOn(opll,ch) else keyOff(opll,ch);
      UPDATE_ALL(opll.ch[ch].mod_);
      UPDATE_ALL(opll.ch[ch].car);
    end;

    $30..$38: begin
      i:=(data shr 4) and 15;
      v:=data and 15;

      if (opll.rythm_mode<>0) and (reg>=$36) then case reg of
        $37: setSlotVolume(opll.ch[7].mod_,i shl 2);
        $38: setSlotVolume(opll.ch[8].mod_,i shl 2);
      end else setPatch(opll,reg-$30,i);

      setVolume(opll,reg-$30,v shl 2);
      UPDATE_ALL(opll.ch[reg-$30].mod_);
      UPDATE_ALL(opll.ch[reg-$30].car);
    end;
  end;
  opll.reg[reg]:=data;
end;

procedure OPLL_writeIO(opll:pOPLL;adr,val:uint32);
begin
  adr:=adr and $ff;
  if adr=$7c
  then opll.adr:=val
  else if adr=$7d then OPLL_writeReg(opll,opll.adr,val);
end;

end.



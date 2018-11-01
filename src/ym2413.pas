unit YM2413;

interface

uses EMU2413;

procedure YM2413_Init(const YMClockValue:longint);
procedure YM2413_Write(const reg,data:byte);
procedure YM2413_WriteToBuffer(var buffer:array of smallint; const position:integer);
procedure YM2413_Close;

implementation

var
  opll:pOPLL;
  Active:boolean; // Set to true by SN76489_Init(), if false then all procedures exit immediately

procedure YM2413_Init(const YMClockValue:longint);
begin
  Active:=YMClockValue>0;
  if not Active then exit;
  OPLL_init(YMClockValue,44100);
  opll:=OPLL_new;
  OPLL_reset(opll);
  OPLL_reset_patch(opll,0);      //* if use default voice data. */

  OPLL_writeReg(opll,$30,$30);   //* select PIANO Voice to ch1. */
  OPLL_writeReg(opll,$10,80);    //* set F-Number(L). */
  OPLL_writeReg(opll,$20,$15);   //* set BLK & F-Number(H) and keyon. */
end;

procedure YM2413_Write(const reg,data:byte);
begin
  if not Active then exit;
  OPLL_writeReg(opll,reg,data);
end;

procedure YM2413_WriteToBuffer(var buffer:array of smallint; const position:integer);
begin
  if not Active then exit;
  // left
  buffer[2*position]  :=OPLL_calc(opll);
  // right
//  buffer[2*position+1]:=buffer[2*position];
// Removed right so PSG can show for now
end;

procedure YM2413_Close;
begin
  if not Active then exit;
  OPLL_delete(opll);
  OPLL_close;
end;

initialization
begin
  Active:=False;
end;

end.

unit http;

interface

function URL2File(const URL:string):string;

implementation

uses
  Windows, Messages, SysUtils, WinInet, main;

function URL2File(const URL:string):string;
function DownloadDialogueProc(DlgWin:hWnd;DlgMessage:UInt;DlgWParam:WParam;DlgLParam:LParam):Bool; stdcall;
const
  btnCancel=101;
  edtProgress=102;
//  procedure SetStyle(const ID,style:integer);
//  var
//    Control:HWnd;
//  begin
//    Control:=GetDlgItem(DlgWin,ID);
//    SetWindowLong(Control,GWL_STYLE,GetWindowLong(Control,GWL_STYLE) or style);
//  end;
begin
  Result:=True;
  case DlgMessage of
//    WM_INITDIALOG:begin // Initialise dialogue
//    end;
    WM_COMMAND:case LoWord(DlgWParam) of // Receive control-messages from dialog
      IDCANCEL,btnCancel: begin
        // Close dialogue
        EndDialog(DlgWin, LOWORD(DlgWParam));
        Exit;
      end;
    end;
    WM_USER+1:begin
      SetDlgItemText(DlgWin,edtProgress,PChar(DlgWParam));
      SendDlgItemMessage(DlgWin,edtProgress,WM_PAINT,0,0);
    end;
  end;
  Result:=False; // say I processed the message and set my control focus
end;
const
  Headers = 'User-Agent: Mozilla/4.0 (compatible; VGM plugin)'#13#10+
            'Accept: *.*, */*'#13#10;
var
  NetHandle:HINTERNET;
  UrlHandle:HINTERNET;
  Buffer:array[0..1024-1] of Char;
  BytesRead,TotalRead:integer;
  f:file;
  i:integer;
  DlgWnd:HWnd;
begin
  DlgWnd:=CreateDialog(hInstance, 'DOWNLOADDIALOGUE', Main.WinampInputPlugin.hMainWindow, @DownloadDialogueProc); // Open dialog

  GetTempPath(Sizeof(buffer),buffer);
  result:=buffer;
  if result[Length(result)]<>'\' then result:=result+'\';
  result:=result+'temp.vgz';
  if FileExists(result) then DeleteFile(result);

  AssignFile(f,result);
  Rewrite(f,1);
  SendMessage(DlgWnd,wm_user+1,Integer(PChar('Connecting to DLL...'#13#10+URL)),0);
  NetHandle:=InternetOpen(
    'VGM plugin',                  // Agent: Identify my program... fairly arbitrary?
    INTERNET_OPEN_TYPE_PRECONFIG,  // Access type: Use user's internet settings
    nil,                           // Proxy: none
    nil,                           // ProxyBypass:
    0);                            // Flags:
  if Assigned(NetHandle) then begin
    Main.DisplayMessage('Connecting...'+URL);
    SendMessage(DlgWnd,wm_user+1,Integer(PChar('Connecting...'#13#10+URL)),0);
    // Try offline first, then try online
    UrlHandle:=InternetOpenUrl(NetHandle,PChar(URL),PChar(Headers),Length(Headers),INTERNET_FLAG_RAW_DATA+INTERNET_FLAG_OFFLINE,0);
    if not Assigned(UrlHandle) then UrlHandle:=InternetOpenUrl(
      NetHandle,            // Handle: from InternetOpen
      PChar(URL),           // URL
      PChar(Headers),       // Headers
      Length(Headers),      // Headers length
                            // Flags:
      INTERNET_FLAG_RAW_DATA+          // Raw data
      INTERNET_FLAG_READ_PREFETCH,     // Prefetch... sounds good
      0                     // Context?
    );

    if Assigned(UrlHandle) then begin // UrlHandle valid? Proceed with download
      FillChar(Buffer,SizeOf(Buffer),0);
      TotalRead:=0;
      repeat
        InternetReadFile(UrlHandle,@Buffer,SizeOf(Buffer),BytesRead);
        BlockWrite(f,Buffer,BytesRead);
        Inc(TotalRead,BytesRead);
        SendMessage(DlgWnd,wm_user+1,Integer(PChar('Downloading...'#13#10+URL+#13#10+IntToStr(TotalRead)+' bytes')),0);
        SendMessage(DlgWnd,WM_PAINT,0,0);
        SendMessage(WinampInputPlugin.hMainWindow,WM_PAINT,0,0);
      until BytesRead=0;
      InternetCloseHandle(UrlHandle);
    end else Result:='# Download failed! '+URL;
    InternetCloseHandle(NetHandle);
  end else Result:='# Connection failed! '+URL;
  Seek(f,0);
  BlockRead(f,Buffer,100,i);
  CloseFile(f);
  if i=0
  then result:='# Zero-byte file'
  else if (pos('<html>',lowercase(buffer))<>0)
       then result:='# HTML file returned';
  SendMessage(DlgWnd,WM_CLOSE,0,0);
end;

end.

(*
  license..: GPL-2.0
  copyright: beldzhang@gmail.com
*)
program Project1;

{$mode objfpc}{$H+}

uses
  {$ifdef linux}cthreads,{$endif}
  SysUtils, Classes, sockets;

type
  TSboiPduHead = packed record
    magic:  Longword;
    cmmd:   Byte;
    index:  Byte;
    rsv1:   Word;
    offset: QWord;
    length: Integer;
    rsv2:   array[0..2] of Longword;
  end;

  TSboiPduDisk = packed record
    _typ:   Word;
    flag:   Word;
    rsvd:   Longword;
    size:   QWord;
  end;

const
  SBOI_MAGIC      = $694f6273;  // 'sbOi'

  SBOI_MAX        = 16;

  SBOI_CMD_READ   = $01;
  SBOI_CMD_WRITE  = $02;
  SBOI_CMD_OPEN   = $03;
  SBOI_CMD_CLOSE  = $04;

  SBOI_RSP_READ   = $11;
  SBOI_RSP_WRITE  = $12;
  SBOI_RSP_OPEN   = $13;
  SBOI_RSP_CLOSE  = $14;

type

  { TClientThread }

  TClientThread = class(TThread)
  private
    FSkt: TSocket;
    FAddr: TInetSockAddr;
    FCmmd,
    FResp: TSboiPduHead;
    FBuff: array[0..256 * 1024] of Char;
    FStream: TStream;

    function do_cmd_open: Integer;
    function do_cmd_read: Integer;
    function do_cmd_write: Integer;
    function do_cmd_close: Integer;
  protected
    procedure Execute; override;
  public
    constructor Create(cskt: TSocket; caddr: TInetSockAddr);
  end;

var
  sskt, cskt: TSocket;
  inaddr: TInetSockAddr;
  len: Integer;

{ TClientThread }

constructor TClientThread.Create(cskt: TSocket; caddr: TInetSockAddr);
begin
  FSkt := cskt;
  FAddr := caddr;
  inherited Create(False);
end;

function TClientThread.do_cmd_open: Integer;
var
  diskinfo: TSboiPduDisk;
begin
  if FCmmd.length > 0 then begin
    if fpRecv(FSkt, @FBuff, FCmmd.length, MSG_WAITALL) <> FCmmd.length then
      Exit(-1);
  end;

  FillChar(diskinfo, SizeOf(diskinfo), 0);
  diskinfo.size := FStream.Size;

  FResp.cmmd := SBOI_RSP_OPEN;
  FResp.index := 1;
  FResp.length := SizeOf(diskinfo);

  if fpSend(FSkt, @FResp, SizeOf(FResp), 0) <> SizeOf(FResp) then
    Exit(-2);
  if fpSend(FSkt, @diskinfo, SizeOf(diskinfo), 0) <> SizeOf(diskinfo) then
    Exit(-3);

  Result := 0;
end;

function TClientThread.do_cmd_read: Integer;
begin
  if FCmmd.index > SBOI_MAX then
    Exit(-1);
  if FCmmd.offset >= FStream.Size then
    Exit(-2);
  if FCmmd.offset + FCmmd.length > FStream.Size then
    Exit(-3);

  FResp.cmmd := SBOI_RSP_READ;
  FResp.index := FCmmd.index;
  FResp.offset := FCmmd.offset;
  FResp.length := FCmmd.length;

  FStream.Position := FResp.offset;
  if FStream.Read(FBuff, FResp.length) <> FResp.length then
    Exit(-4);

  if fpSend(FSkt, @FResp, SizeOf(FResp), 0) <> SizeOf(FResp) then
    writeln('send resp(read) fail');
  if fpSend(FSkt, @FBuff, FResp.length, 0) <> FResp.length then
    writeln('send resp(data) fail');

  Result := 0;
end;

function TClientThread.do_cmd_write: Integer;
begin
  if fpRecv(FSkt, @FBuff, FCmmd.length, MSG_WAITALL) <> FCmmd.length then
    writeln('recv cmmd(data) fail');

  if FCmmd.index > SBOI_MAX then
    Exit(-1);
  if FCmmd.offset >= FStream.Size then begin
    writeln(format('  -> write exceed[1]: %d > %d', [FCmmd.offset, FStream.Size]));
    Exit(-2);
  end;
  if FCmmd.offset + FCmmd.length > FStream.Size then begin
    writeln(format('  -> write exceed[2]: %d > %d', [FCmmd.offset + FCmmd.length, FStream.Size]));
    Exit(-3);
  end;

  FResp.cmmd := SBOI_RSP_WRITE;
  FResp.index := FCmmd.index;
  FResp.offset := FCmmd.offset;
  FResp.length := FCmmd.length;

  FStream.Position := FResp.offset;
  if FStream.Write(FBuff, FResp.length) <> FResp.length then
    Exit(-4);

  if fpSend(FSkt, @FResp, SizeOf(FResp), 0) <> SizeOf(FResp) then
    writeln('send resp(write) fail');

  Result := 0;
end;

function TClientThread.do_cmd_close: Integer;
begin
  FResp.cmmd := SBOI_RSP_CLOSE;
  if fpSend(FSkt, @FResp, SizeOf(FResp), 0) <> SizeOf(FResp) then
    ;
  Result := 0;
end;

procedure TClientThread.Execute;
var
  r: Integer;
begin
  writeln(format('client open.: %s', [NetAddrToStr(FAddr.sin_addr)]));
  if FileExists('/tmp/rootfs.img') then
    FStream := TFileStream.Create('/tmp/rootfs.img', fmOpenReadWrite or fmShareDenyNone)
  else begin
    FStream := TFileStream.Create('/tmp/rootfs.img', fmCreate);
    FStream.Size := Int64(1024 * 1024 * 1024) * 8;
  end;

  while True do begin
    FillChar(FCmmd, SizeOf(FCmmd), 0);
    FillChar(FResp, SizeOf(FResp), 0);

    if fpRecv(FSkt, @FCmmd, SizeOf(FCmmd), MSG_WAITALL) <> SizeOf(FCmmd) then begin
      writeln('recv cmmd fail');
      Break;
    end;

    if FCmmd.magic <> SBOI_MAGIC then begin
      writeln('recv cmmd: bad magic');
      Break;
    end;
    FResp.magic := SBOI_MAGIC;

    case FCmmd.cmmd of
      SBOI_CMD_OPEN:  begin r := do_cmd_open;  end;
      SBOI_CMD_READ:  begin r := do_cmd_read;  end;
      SBOI_CMD_WRITE: begin r := do_cmd_write; end;
      SBOI_CMD_CLOSE: begin r := do_cmd_close; Break; end;
    end;
    if r <> 0 then begin
      writeln(format('handle cmmd(%x) fail: %d', [FCmmd.cmmd, r]));
      Break;
    end;
  end;
  writeln(format('client close: %s', [NetAddrToStr(FAddr.sin_addr)]));
  FStream.Free;
  CloseSocket(cskt);
end;

begin
  writeln('sboi server 0.02.0001');

  sskt := fpSocket(AF_INET, SOCK_STREAM, 0);

  len := 1;
  fpsetsockopt(sskt, SOL_SOCKET, SO_REUSEADDR, @len, SizeOf(len));
  FillChar(inaddr, SizeOf(inaddr), 0);
  inaddr.sin_family := AF_INET;
  inaddr.sin_addr.s_addr := 0;
  inaddr.sin_port := htons(12345);
  fpBind(sskt, @inaddr, SizeOf(inaddr));

  fpListen(sskt, 5);

  while True do begin
    len := SizeOf(inaddr);
    cskt := fpAccept(sskt, @inaddr, @len);
    TClientThread.Create(cskt, inaddr);
  end;
  CloseSocket(sskt);
end.


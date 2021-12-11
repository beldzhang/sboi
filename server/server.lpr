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

  { TClientThread }

  TClientThread = class(TThread)
  private
    FSkt: TSocket;
    FAddr: TInetSockAddr;
    FBuff: array[0..256 * 1024] of Char;
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

procedure TClientThread.Execute;
var
  stream: TStream;
  sector: Longword;
  count: Word;
  write: Byte;
  len: Integer;
  c, r: Integer;
begin
  writeln(format('client open: %s', [NetAddrToStr(FAddr.sin_addr)]));
  if FileExists('/tmp/rootfs.img') then
    stream := TFileStream.Create('/tmp/rootfs.img', fmOpenReadWrite or fmShareDenyNone)
  else begin
    stream := TFileStream.Create('/tmp/rootfs.img', fmCreate);
    stream.Size := Int64(1024 * 1024 * 1024) * 8;
  end;
  sector := stream.Size div 512;
  fpSend(FSkt, @sector, 4, 0);
  while True do begin
    if fpRecv(FSkt, @write,  1, MSG_WAITALL) <> 1 then Break;
    if fpRecv(FSkt, @sector, 4, MSG_WAITALL) <> 4 then Break;
    if fpRecv(FSkt, @count,  2, MSG_WAITALL) <> 2 then Break;

    stream.Position := Int64(sector) * 512;
    len := count * 512;
    if write = 0 then begin
      //writeln(format('%2d: %8d, %4d', [write, sector, length]));
      stream.Read(FBuff, len);
      if fpSend(cskt, @FBuff, len, 0) <> len then
        writeln('fpSend()');
    end
    else begin
      //writeln(format('%2d: %8d, %4d', [write, sector, count]));
      c := 0;
      while len > 0 do begin
        r := fpRecv(cskt, @FBuff[c], len, MSG_WAITALL);
        Inc(c, r);
        Dec(len, r);
      end;
      stream.Write(FBuff, c);
    end;
  end;
  writeln(format('client close: %s', [NetAddrToStr(FAddr.sin_addr)]));
  stream.Free;
  CloseSocket(cskt);
end;

begin
  writeln('sboi server 0.01.0001');
  sskt := fpSocket(AF_INET, SOCK_STREAM, 0);

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


(*
  license..: GPL-2.0
  copyright: beldzhang@gmail.com
*)
program Project1;

{$mode objfpc}{$H+}

uses
  SysUtils, Classes, sockets;

var
  sskt, cskt: TSocket;
  inaddr: TInetSockAddr;
  len: Integer;
  stream: TStream;
  sector: Longword;
  count: Word;
  write: Byte;
  buffer: array[0..256 * 1024] of Char;
  c, r: Integer;
begin
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
    writeln('client open');
    if FileExists('/tmp/rootfs.img') then
      stream := TFileStream.Create('/tmp/rootfs.img', fmOpenReadWrite)
    else begin
      stream := TFileStream.Create('/tmp/rootfs.img', fmCreate);
      stream.Size := Int64(1024 * 1024 * 1024) * 8;
    end;
    sector := stream.Size div 512;
    fpSend(cskt, @sector, 4, 0);
    while True do begin
      if fpRecv(cskt, @write,  1, 0) <> 1 then Break;
      if fpRecv(cskt, @sector, 4, 0) <> 4 then Break;
      if fpRecv(cskt, @count,  2, 0) <> 2 then Break;

      stream.Position := Int64(sector) * 512;
      len := count * 512;
      if write = 0 then begin
        //writeln(format('%2d: %8d, %4d', [write, sector, length]));
        stream.Read(buffer, len);
        if fpSend(cskt, @buffer, len, 0) <> len then
          writeln('fpSend()');
      end
      else begin
        c := 0;
        while len > 0 do begin
          r := fpRecv(cskt, @buffer[c], len, 0);
          Inc(c, r);
          Dec(len, r);
        end;
        stream.Write(buffer, c);
        //writeln(format('%2d: %8d, %4d', [write, sector, c]));
      end;
    end;
    writeln('client closed');
    stream.Free;
    CloseSocket(cskt);
  end;
  CloseSocket(sskt);
end.


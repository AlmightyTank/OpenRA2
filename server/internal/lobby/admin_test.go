package lobby

import (
    "errors"
    "testing"
)

// The admin room view must not disclose client addresses, persistent IDs,
// tunnel IDs or internal connection pointers through JSON.
func TestAdminRoomRosterAndWaitingRoomClosure(t *testing.T) {
    srv, addr := startServer(t, Config{MaxConnsPerIP: 10,MaxRoomsPerIP: 4})
    host := dial(t,addr)
    host.send(hello("Host"))
    host.expect("hello_ok")
    host.send(Msg{T:"create",Name:"Private Match",Max:4,Private:true,V3:8101,Map:"Arctic"})
    code:=host.expect("created").Code
    guest:=dial(t,addr)
    guest.send(hello("Guest"))
    guest.expect("hello_ok")
    guest.send(Msg{T:"join",Code:code,V3:8102})
    guest.expect("joined")
    host.expect("peer")

    rooms:=srv.AdminRooms()
    if len(rooms)!=1 {t.Fatalf("expected one private room, got %+v",rooms)}
    room:=rooms[0]
    if room.Code!=code || !room.Private || room.Started || room.Connected!=2 ||
        room.Players!=2 || room.Max!=4 || len(room.Participants)!=2 {
        t.Fatalf("unexpected private room snapshot: %+v",room)
    }
    if room.Participants[0].Name!="Host" || room.Participants[0].Role!="host" ||
        room.Participants[1].Name!="Guest" || room.Participants[1].Role!="guest" {
        t.Fatalf("unexpected participants: %+v",room.Participants)
    }

    if err:=srv.AdminCloseWaitingRoom("not-a-code");!errors.Is(err,ErrRoomNotFound) {
        t.Fatalf("invalid code should be not found: %v",err)
    }
    if err:=srv.AdminCloseWaitingRoom("AAAAAA");!errors.Is(err,ErrRoomNotFound) {
        t.Fatalf("unknown code should be not found: %v",err)
    }
    if err:=srv.AdminCloseWaitingRoom(code);err!=nil {t.Fatal(err)}
    if m:=guest.expect("room_closed");m.Code!=code {t.Fatalf("wrong guest close: %+v",m)}
    if m:=host.expect("room_closed");m.Code!=code {t.Fatalf("host not informed: %+v",m)}
    if srv.RoomCount()!=0 || len(srv.AdminRooms())!=0 {t.Fatal("admin closed room remains listed")}
    if err:=srv.AdminCloseWaitingRoom(code);!errors.Is(err,ErrRoomNotFound) {t.Fatalf("duplicate close: %v",err)}

    // The TCP host can reuse the connection to create a fresh room.
    host.send(Msg{T:"create",Name:"Next Round",Max:2,V3:8103})
    host.expect("created")
    if srv.RoomCount()!=1 {t.Fatal("operator closed host connection instead of just the room")}
}

func TestAdminCannotCloseStartedMatch(t *testing.T) {
    srv,addr:=startServer(t,Config{})
    host:=dial(t,addr)
    host.send(hello("Host"))
    host.expect("hello_ok")
    host.send(Msg{T:"create",Name:"In Progress",Max:2,V3:8201})
    code:=host.expect("created").Code
    host.send(Msg{T:"update",Started:true,Players:2})
    host.send(Msg{T:"ping"})
    host.expect("pong")
    if err:=srv.AdminCloseWaitingRoom(code);!errors.Is(err,ErrRoomStarted) {
        t.Fatalf("started match must be protected: %v",err)
    }
    if srv.RoomCount()!=1 || !srv.AdminRooms()[0].Started {t.Fatal("started room removed")}
    host.send(Msg{T:"ping"})
    host.expect("pong")
}

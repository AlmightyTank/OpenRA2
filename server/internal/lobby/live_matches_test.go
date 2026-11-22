package lobby

import (
    "bytes"
    "encoding/binary"
    "fmt"
    "net"
    "testing"
    "time"

    "ra2lobbyd/internal/tunnel"
)

// An actual UDP socket is used for every simulated player, and the production
// V3 Serve loop forwards every game datagram. This is transport validation,
// NOT a headless Red Alert 2 simulation or physical Android-device test.
func TestLiveConcurrentV3Matches(t *testing.T) {
    sizes := []int{2, 4, 6, 8}
    cfg := Config{MaxRooms: 20, MaxRoomsPerIP: 10, MaxConnsPerIP: 30, JoinsPerMinute: 120}
    srv, address := startServer(t, cfg)
    v3 := tunnel.NewV3(tunnel.Config{AllowLoopback: true, IPLimit: 40, MaxClients: 64})
    udpServer, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
    if err != nil { t.Fatal(err) }
    t.Cleanup(func(){ udpServer.Close() })
    go func(){ _ = tunnel.Serve(udpServer, v3, time.Second) }()
    relayAddr := udpServer.LocalAddr().(*net.UDPAddr)

    type participant struct {
        link *tc
        game *net.UDPConn
        id uint32
    }
    type match struct {
        code string
        peers []participant
    }
    matches := make([]match, 0, len(sizes))
    nextID := uint32(0x10000)
    for matchIndex, size := range sizes {
        m := match{peers: make([]participant, 0, size)}
        for i:=0; i<size; i++ {
            cl := dial(t, address)
            introduction := hello(fmt.Sprintf("P%d_%d", matchIndex, i))
            if matchIndex%2 == 1 { introduction.Plat = 2 } // Android-style hello (not an Android runtime)
            cl.send(introduction)
            cl.expect("hello_ok")
            sock, e := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
            if e != nil { t.Fatal(e) }
            t.Cleanup(func(){ sock.Close() })
            nextID++
            peer := participant{link:cl, game:sock, id:nextID}
            if i == 0 {
                cl.send(Msg{T:"create", Name:fmt.Sprintf("Match%d", matchIndex), Map:"Tour of Egypt", Max:size, V3:peer.id})
                m.code = cl.expect("created").Code
            } else {
                cl.send(Msg{T:"join", Code:m.code, V3:peer.id, Relay:"v3"})
                joined := cl.expect("joined")
                if joined.Code!=m.code || joined.HostV3!=m.peers[0].id {
                    t.Fatalf("wrong host for match %d: %+v", matchIndex, joined)
                }
                hostPeer := m.peers[0].link.expect("peer")
                if hostPeer.V3!=peer.id { t.Fatalf("wrong guest attached to match %d: %+v", matchIndex, hostPeer) }
            }
            m.peers=append(m.peers,peer)
        }
        m.peers[0].link.send(Msg{T:"update", Players:size, Max:size, Map:"Tour of Egypt", Started:true})
        m.peers[0].link.send(Msg{T:"ping"})
        m.peers[0].link.expect("pong")
        matches=append(matches,m)
    }
    stats:=srv.Stats()
    if stats.Total!=4 || stats.Started!=4 || stats.Participants!=20 {
        t.Fatalf("four live room groups with 20 players: %+v",stats)
    }

    // Register every endpoint on the V3 tunnel via a packet with an unknown
    // destination. Registration occurs before forwarding destination lookup.
    for _, m := range matches {
        for _, p := range m.peers {
            packet:=append(tunnel.V3Header(p.id,0xFFFFFFFE),byte('R'))
            if _,e:=p.game.WriteToUDP(packet,relayAddr);e!=nil { t.Fatal(e) }
        }
    }
    deadline:=time.Now().Add(3*time.Second)
    for v3.Clients()!=20 && time.Now().Before(deadline) { time.Sleep(10*time.Millisecond) }
    if v3.Clients()!=20 { t.Fatalf("expected 20 registered endpoints, got %d",v3.Clients()) }

    // Every participant exchanges a distinct packet with their match host.
    // Payload and the V3 sender/receiver headers must be preserved exactly.
    expectedForward:=uint64(0)
    for groupIndex, m := range matches {
        host:=m.peers[0]
        for guestIndex:=1;guestIndex<len(m.peers);guestIndex++ {
            guest:=m.peers[guestIndex]
            payload:=[]byte(fmt.Sprintf("match=%d,guest=%d",groupIndex,guestIndex))
            up:=append(tunnel.V3Header(guest.id,host.id),payload...)
            if _,e:=guest.game.WriteToUDP(up,relayAddr);e!=nil { t.Fatal(e) }
            buf:=make([]byte,2048)
            host.game.SetReadDeadline(time.Now().Add(2*time.Second))
            n,_,e:=host.game.ReadFromUDP(buf)
            if e!=nil || !bytes.Equal(buf[:n],up) {
                t.Fatalf("match %d guest %d to host: %v, got %x, want %x",groupIndex,guestIndex,e,buf[:n],up)
            }
            if binary.LittleEndian.Uint32(buf[:4])!=guest.id {t.Fatal("V3 sender changed")}
            expectedForward++
            down:=append(tunnel.V3Header(host.id,guest.id),payload...)
            if _,e:=host.game.WriteToUDP(down,relayAddr);e!=nil { t.Fatal(e) }
            guest.game.SetReadDeadline(time.Now().Add(2*time.Second))
            n,_,e=guest.game.ReadFromUDP(buf)
            if e!=nil || !bytes.Equal(buf[:n],down) {
                t.Fatalf("match %d host to guest %d: %v, got %x, want %x",groupIndex,guestIndex,e,buf[:n],down)
            }
            expectedForward++
        }
    }
    if got:=v3.Stats.Forwarded.Load();got!=expectedForward {
        t.Fatalf("forwarded %d datagrams, want %d",got,expectedForward)
    }
    // No unexpected datagrams should have gone to any unrelated participant.
    for group, m:=range matches {
        for _,p:=range m.peers {
            buf:=make([]byte,128)
            p.game.SetReadDeadline(time.Now().Add(15*time.Millisecond))
            if n,_,e:=p.game.ReadFromUDP(buf);e==nil {
                t.Fatalf("unrequested cross-match datagram reached group %d: %x",group,buf[:n])
            } else if ne,ok:=e.(net.Error); !ok || !ne.Timeout() {
                t.Fatalf("unexpected UDP read failure: %v",e)
            }
        }
    }
    // The lobby remains responsive while four matches have started.
    observer:=dial(t,address)
    observer.send(hello("Spectator"));observer.expect("hello_ok")
    observer.send(Msg{T:"list"})
    if got:=observer.expect("rooms").Rooms;len(got)!=4 {
        t.Fatalf("started rooms still appear in public list: %d",len(got))
    }

    // Closing one match changes directory state without closing the other
    // host/guest control connections, then all rooms clean up independently.
    matches[1].peers[0].link.send(Msg{T:"close"})
    for _,p:=range matches[1].peers[1:] {p.link.expect("room_closed")}
    if srv.RoomCount()!=3 { t.Fatalf("expected three surviving matches, got %d",srv.RoomCount()) }
    for i,m:=range matches {
        if i==1 {continue}
        m.peers[0].link.send(Msg{T:"ping"})
        m.peers[0].link.expect("pong")
    }
}

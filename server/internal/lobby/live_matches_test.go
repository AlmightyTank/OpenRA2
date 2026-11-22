package lobby

import (
    "bytes"
    "encoding/binary"
    "fmt"
    "net"
    "sync"
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

 
// Concurrent traffic test: all eight UDP endpoints are active at the SAME
// time. Every endpoint sends game-like bytes and receives from its partner;
// packet headers and payloads are checked for accidental cross-room mixing.
func TestLiveParallelV3TrafficAcrossFourMatches(t *testing.T) {
    _, address := startServer(t, Config{MaxRoomsPerIP: 8,MaxConnsPerIP: 20,JoinsPerMinute: 80})
    v3 := tunnel.NewV3(tunnel.Config{AllowLoopback:true,IPLimit:24,MaxClients:24})
    udpServer,err:=net.ListenUDP("udp4",&net.UDPAddr{IP:net.IPv4(127,0,0,1)})
    if err!=nil {t.Fatal(err)}
    t.Cleanup(func(){udpServer.Close()})
    go func(){_ = tunnel.Serve(udpServer,v3,time.Second)}()
    serverAddr:=udpServer.LocalAddr().(*net.UDPAddr)
    type endpoint struct{ socket *net.UDPConn; id uint32 }
    type pair struct{ host, guest endpoint }
    pairs:=make([]pair,4)
    for index:=range pairs {
        host:=dial(t,address)
        host.send(hello(fmt.Sprintf("Host%d",index)));host.expect("hello_ok")
        hid:=uint32(index*2+10001)
        host.send(Msg{T:"create",Name:fmt.Sprintf("Concurrent%d",index),Max:2,V3:hid})
        roomCode:=host.expect("created").Code
        guest:=dial(t,address)
        guest.send(hello(fmt.Sprintf("Guest%d",index)));guest.expect("hello_ok")
        gid:=hid+1
        guest.send(Msg{T:"join",Code:roomCode,V3:gid})
        guest.expect("joined");host.expect("peer")
        hs,e:=net.ListenUDP("udp4",&net.UDPAddr{IP:net.IPv4(127,0,0,1)})
        if e!=nil {t.Fatal(e)}
        gs,e:=net.ListenUDP("udp4",&net.UDPAddr{IP:net.IPv4(127,0,0,1)})
        if e!=nil {hs.Close();t.Fatal(e)}
        t.Cleanup(func(){hs.Close();gs.Close()})
        pairs[index]=pair{host:endpoint{hs,hid},guest:endpoint{gs,gid}}
    }
    // Register all IDs before starting the packet burst, avoiding test
    // dependence on registration ordering.
    for _,p:=range pairs {
        for _,e:=range []endpoint{p.host,p.guest} {
            packet:=append(tunnel.V3Header(e.id,0xFFFFFFFE),byte('R'))
            if _,err:=e.socket.WriteToUDP(packet,serverAddr);err!=nil {t.Fatal(err)}
        }
    }
    deadline:=time.Now().Add(3*time.Second)
    for v3.Clients()!=8 && time.Now().Before(deadline) {time.Sleep(10*time.Millisecond)}
    if v3.Clients()!=8 {t.Fatalf("expected 8 V3 endpoints, got %d",v3.Clients())}

    const rounds=20
    start:=make(chan struct{})
    var wg sync.WaitGroup
    errs:=make(chan error,8)
    for index,p:=range pairs {
        run:=func(group int,self,other endpoint) {
            defer wg.Done()
            <-start
            buf:=make([]byte,256)
            for round:=0;round<rounds;round++ {
                payload:=[]byte(fmt.Sprintf("match-%d-round-%02d",group,round))
                sent:=append(tunnel.V3Header(self.id,other.id),payload...)
                if _,err:=self.socket.WriteToUDP(sent,serverAddr);err!=nil {
                    errs<-fmt.Errorf("group %d send round %d: %w",group,round,err);return
                }
                self.socket.SetReadDeadline(time.Now().Add(2*time.Second))
                n,_,err:=self.socket.ReadFromUDP(buf)
                expected:=append(tunnel.V3Header(other.id,self.id),payload...)
                if err!=nil || !bytes.Equal(buf[:n],expected) {
                    errs<-fmt.Errorf("group %d round %d received %x expected %x: %v",group,round,buf[:n],expected,err);return
                }
            }
        }
        wg.Add(2)
        go run(index,p.host,p.guest)
        go run(index,p.guest,p.host)
    }
    close(start)
    wg.Wait()
    close(errs)
    for err:=range errs {t.Error(err)}
    if t.Failed(){return}
    if got:=v3.Stats.Forwarded.Load();got!=8*rounds {
        t.Fatalf("concurrent V3 forwarded %d datagrams, want %d",got,8*rounds)
    }
}

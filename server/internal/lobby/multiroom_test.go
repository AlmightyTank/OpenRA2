package lobby

import (
	"net"
	"sync"
	"testing"
	"time"

	"ra2lobbyd/internal/relay"
)

// Integration tests use real loopback TCP sockets and exercise the live server,
// not just the room map. Multiple room codes must stay independent.
func TestMultipleSimultaneousMatches(t *testing.T) {
	s, addr := startServer(t, Config{MaxConnsPerIP: 16, MaxRoomsPerIP: 8, JoinsPerMinute: 60})
	hosts := make([]*tc, 3)
	guests := make([]*tc, 3)
	codes := make([]string, 3)
	for i := range hosts {
		hosts[i] = dial(t, addr)
		hosts[i].send(hello("Host"))
		hosts[i].expect("hello_ok")
		hosts[i].send(Msg{T: "create", Name: "Match", Max: 4, V3: uint32(100 + i), Private: i == 1})
		codes[i] = hosts[i].expect("created").Code
	}
	if st := s.Stats(); st.Total != 3 || st.Waiting != 3 || st.Private != 1 || st.Participants != 3 {
		t.Fatalf("initial aggregate room metrics: %+v", st)
	}
	if s.RoomCount() != 3 || codes[0] == codes[1] || codes[1] == codes[2] || codes[0] == codes[2] {
		t.Fatalf("three distinct rooms expected: %q", codes)
	}
	for i := range guests {
		guests[i] = dial(t, addr)
		guests[i].send(hello("Guest"))
		guests[i].expect("hello_ok")
		guests[i].send(Msg{T: "join", Code: codes[i], V3: uint32(200 + i)})
		if got := guests[i].expect("joined"); got.Code != codes[i] {
			t.Fatalf("guest joined wrong room: %+v", got)
		}
		if got := hosts[i].expect("peer"); got.V3 != uint32(200 + i) {
			t.Fatalf("peer received for wrong match: %+v", got)
		}
	}
	if st := s.Stats(); st.Total != 3 || st.Participants != 6 {
		t.Fatalf("three concurrent 2-player matches must report six participants: %+v", st)
	}
	guests[0].send(Msg{T: "list"})
	rooms := guests[0].expect("rooms").Rooms
	if len(rooms) != 2 {
		t.Fatalf("expected two public rooms, private one hidden: %+v", rooms)
	}
	for _, r := range rooms {
		if r.Code == codes[1] || r.Players != 2 {
			t.Fatalf("room visibility or live occupancy is incorrect: %+v", r)
		}
	}

	hosts[0].send(Msg{T: "update", Started: true, Players: 2})
	hosts[0].send(Msg{T: "ping"})
	hosts[0].expect("pong")
	if st := s.Stats(); st.Started != 1 || st.Waiting != 2 {
		t.Fatalf("started/waiting match counts incorrect: %+v", st)
	}
	probe := dial(t, addr)
	probe.send(hello("Probe"))
	probe.expect("hello_ok")
	probe.send(Msg{T: "join", Code: codes[0], V3: 300})
	if m := probe.expect("error"); m.Reason != "started" {
		t.Fatalf("started match must reject joins: %+v", m)
	}
	// IDs in the V3 tunnel are global: an ID from another match cannot be
	// reused even when the room itself has never seen it.
	probe.send(Msg{T: "join", Code: codes[2], V3: 200})
	if m := probe.expect("error"); m.Reason != "v3_taken" {
		t.Fatalf("cross-match V3 collision must be rejected: %+v", m)
	}
	hosts[1].send(Msg{T: "close"})
	if m := guests[1].expect("room_closed"); m.Code != codes[1] {
		t.Fatalf("closing match B must notify only its guest: %+v", m)
	}
	if got := s.RoomCount(); got != 2 {
		t.Fatalf("closing one match must leave two running, got %d", got)
	}
	guests[0].send(Msg{T: "list"})
	rooms = guests[0].expect("rooms").Rooms
	if len(rooms) != 2 {
		t.Fatalf("other matches were affected by close: %+v", rooms)
	}
	hosts[2].send(Msg{T: "close"})
	guests[2].expect("room_closed")
	if s.RoomCount() != 1 {
		t.Fatalf("third match closure affected remaining match")
	}
}

func TestConcurrentCreatesRespectServerRoomCap(t *testing.T) {
	s, addr := startServer(t, Config{MaxRooms: 3, MaxRoomsPerIP: 12, MaxConnsPerIP: 12})
	hosts := make([]*tc, 8)
	for i := range hosts {
		hosts[i] = dial(t, addr)
		hosts[i].send(hello("Host"))
		hosts[i].expect("hello_ok")
	}
	var wg sync.WaitGroup
	for i, h := range hosts {
		wg.Add(1)
		go func(h *tc, id uint32) {
			defer wg.Done()
			h.send(Msg{T: "create", Name: "Concurrent", Max: 4, V3: id})
		}(h, uint32(500+i))
	}
	wg.Wait()
	codes := map[string]bool{}
	for _, h := range hosts {
		switch m := h.recv(); m.T {
		case "created":
			if codes[m.Code] {
				t.Fatalf("duplicate room code %q", m.Code)
			}
			codes[m.Code] = true
		case "error":
			if m.Reason != "full" {
				t.Fatalf("unexpected create failure: %+v", m)
			}
		default:
			t.Fatalf("unexpected create result: %+v", m)
		}
	}
	if len(codes) != 3 || s.RoomCount() != 3 {
		t.Fatalf("concurrent creations exceeded or underused limit: rooms=%d count=%d", len(codes), s.RoomCount())
	}
}

func TestConcurrentJoinsCannotOverfillMatch(t *testing.T) {
	s, addr := startServer(t, Config{MaxConnsPerIP: 16, JoinsPerMinute: 60})
	host := dial(t, addr)
	host.send(hello("Host"))
	host.expect("hello_ok")
	host.send(Msg{T: "create", Name: "Duel", Max: 2, V3: 1001})
	code := host.expect("created").Code
	guests := make([]*tc, 6)
	for i := range guests {
		guests[i] = dial(t, addr)
		guests[i].send(hello("Guest"))
		guests[i].expect("hello_ok")
	}
	var wg sync.WaitGroup
	for i, g := range guests {
		wg.Add(1)
		go func(g *tc, id uint32) {
			defer wg.Done()
			g.send(Msg{T: "join", Code: code, V3: id})
		}(g, uint32(1100+i))
	}
	wg.Wait()
	success, full := 0, 0
	for _, g := range guests {
		switch m := g.recv(); m.T {
		case "joined":
			success++
		case "error":
			if m.Reason != "full" {
				t.Fatalf("unexpected join rejection: %+v", m)
			}
			full++
		default:
			t.Fatalf("unexpected join response: %+v", m)
		}
	}
	if success != 1 || full != len(guests)-1 {
		t.Fatalf("one open guest slot must accept exactly one join: joined=%d full=%d", success, full)
	}
	host.expect("peer")
	if s.RoomCount() != 1 {
		t.Fatalf("match unexpectedly removed")
	}
	host.send(Msg{T: "update", Players: 1, Max: 2})
	host.send(Msg{T: "ping"})
	host.expect("pong")
	guests[0].send(Msg{T: "list"})
	if rooms := guests[0].expect("rooms").Rooms; len(rooms) != 1 || rooms[0].Players != 2 {
		t.Fatalf("directory must reflect connected players even if host reports stale count: %+v", rooms)
	}
}

func TestDuplicateJoinLeavesRoomSlotsAvailable(t *testing.T) {
	_, addr := startServer(t, Config{})
	host := dial(t, addr)
	host.send(hello("Host"))
	host.expect("hello_ok")
	host.send(Msg{T: "create", Name: "Match", Max: 3, V3: 2001})
	code := host.expect("created").Code
	g := dial(t, addr)
	g.send(hello("Guest"))
	g.expect("hello_ok")
	g.send(Msg{T: "join", Code: code, V3: 2002})
	g.expect("joined")
	host.expect("peer")
	g.send(Msg{T: "join", Code: code, V3: 2003})
	if m := g.expect("error"); m.Reason != "already" {
		t.Fatalf("repeat join should not reserve a new slot: %+v", m)
	}
	other := dial(t, addr)
	other.send(hello("Another"))
	other.expect("hello_ok")
	other.send(Msg{T: "join", Code: code, V3: 2003})
	other.expect("joined")
	host.expect("peer")
}

func TestFailedPortRelayJoinIsAtomic(t *testing.T) {
	occupied, err := net.ListenUDP("udp4", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
	if err != nil { t.Fatal(err) }
	defer occupied.Close()
	port := occupied.LocalAddr().(*net.UDPAddr).Port
	s, addr := startServer(t, Config{})
	s.Pool = relay.NewPool(relay.Config{BindIP: "127.0.0.1", PortLo: port, PortHi: port})
	host := dial(t, addr)
	host.send(hello("Host"))
	host.expect("hello_ok")
	host.send(Msg{T: "create", Name: "R", Max: 2, V3: 3001})
	code := host.expect("created").Code
	g := dial(t, addr)
	g.send(hello("Guest"))
	g.expect("hello_ok")
	g.send(Msg{T: "join", Code: code, V3: 3002, Relay: "port"})
	if m := g.expect("error"); m.Reason != "busy" {
		t.Fatalf("occupied relay port should reject without joining: %+v", m)
	}
	s.mu.Lock()
	registered := len(s.rooms[code].members)
	reserved := s.activeV3[3002]
	s.mu.Unlock()
	if registered != 0 || reserved {
		t.Fatalf("failed allocation left a phantom guest: members=%d v3=%v", registered, reserved)
	}
	// The same V3 ID can be used for a real join after allocation fails.
	g.send(Msg{T: "join", Code: code, V3: 3002, Relay: "v3"})
	g.expect("joined")
	host.expect("peer")
}

func TestClosingRoomFreesPortRelayWhileGuestStaysConnected(t *testing.T) {
	s, addr := startServer(t, Config{})
	s.Pool = relay.NewPool(relay.Config{BindIP: "127.0.0.1"})
	host := dial(t, addr)
	host.send(hello("Host"))
	host.expect("hello_ok")
	host.send(Msg{T: "create", Name: "R", Max: 2, V3: 4001})
	code := host.expect("created").Code
	g := dial(t, addr)
	g.send(hello("Guest"))
	g.expect("hello_ok")
	g.send(Msg{T: "join", Code: code, V3: 4002, Relay: "port"})
	g.expect("joined")
	host.expect("peer")
	if s.Pool.InUse() != 1 { t.Fatal("expected one active relay allocation") }
	host.send(Msg{T: "close"})
	g.expect("room_closed")
	if n := s.Pool.InUse(); n != 0 {
		t.Fatalf("closed room retained relay allocation: %d", n)
	}
	g.send(Msg{T: "ping"})
	g.expect("pong") // guest TCP control channel remains alive
}

func TestDisconnectedGuestReleasesTunnelID(t *testing.T) {
	s, addr := startServer(t, Config{})
	host := dial(t, addr)
	host.send(hello("Host"))
	host.expect("hello_ok")
	host.send(Msg{T: "create", Name: "R", Max: 4, V3: 5001})
	code := host.expect("created").Code
	g := dial(t, addr)
	g.send(hello("Guest"))
	g.expect("hello_ok")
	g.send(Msg{T: "join", Code: code, V3: 5002})
	g.expect("joined")
	host.expect("peer")
	g.c.Close()
	deadline := time.Now().Add(3 * time.Second)
	for time.Now().Before(deadline) {
		s.mu.Lock()
		used := s.activeV3[5002]
		s.mu.Unlock()
		if !used { return }
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("guest tunnel ID remained reserved after disconnect")
}

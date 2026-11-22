package lobby

import (
    "errors"
    "sort"
    "time"
)

// The admin API returns display names and platform tags but deliberately
// withholds IPs, player IDs, tunnel IDs and session tokens.
type AdminParticipant struct {
    Name string `json:"name"`
    Platform int `json:"platform"`
    Role string `json:"role"`
}

type AdminRoom struct {
    Code string `json:"code"`
    Name string `json:"name"`
    Map string `json:"map"`
    Private bool `json:"private"`
    Started bool `json:"started"`
    Players int `json:"players"`
    Connected int `json:"connected"`
    Max int `json:"max"`
    Created time.Time `json:"created"`
    Participants []AdminParticipant `json:"participants"`
}

var (
    ErrRoomNotFound = errors.New("room not found")
    ErrRoomStarted = errors.New("match is already started")
)

func (s *Server) AdminRooms() []AdminRoom {
    s.mu.Lock()
    defer s.mu.Unlock()

    out := make([]AdminRoom, 0, len(s.rooms))
    for _, r := range s.rooms {
        host := AdminParticipant{Name:r.host.nick, Platform:r.host.plat, Role:"host"}
        entry := AdminRoom{
            Code:r.Code, Name:r.Name, Map:r.Map, Private:r.Private,
            Started:r.Started, Players:roomPlayerCount(r),
            Connected:len(r.members)+1, Max:r.Max, Created:r.created,
            Participants: []AdminParticipant{host},
        }
        for member := range r.members {
            entry.Participants = append(entry.Participants,
                AdminParticipant{Name:member.nick,Platform:member.plat,Role:"guest"})
        }
        sort.Slice(entry.Participants[1:],func(i,j int)bool{
            return entry.Participants[i+1].Name < entry.Participants[j+1].Name
        })
        out=append(out,entry)
    }
    sort.Slice(out,func(i,j int)bool{
        if out[i].Created.Equal(out[j].Created){return out[i].Code<out[j].Code}
        return out[i].Created.Before(out[j].Created)
    })
    return out
}

// Administrative removal is intentionally limited to waiting rooms. Closing
// a directory entry cannot terminate an already running lockstep match, so
// offering that action on started rooms would be misleading and disruptive.
func (s *Server) AdminCloseWaitingRoom(code string) error {
    if !ValidCode(code) {return ErrRoomNotFound}
    s.mu.Lock()
    room:=s.rooms[code]
    if room==nil {s.mu.Unlock();return ErrRoomNotFound}
    host:=room.host
    s.mu.Unlock()
    return s.closeRoomIf(host,code,true)
}

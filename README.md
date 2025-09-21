# Multithreaded Multiplayer Game Server

Concurrent TCP server and client that allows for real-time multiplayer tic-tac-toe games.
Built in C with POSIX threading

## Highlights

- Supports unlimited simultaneous client connections
- Thread safety. Game states are synchronized across multiple game sessions
- Memory-Safe Operations: Proper use of memory management for cleanup and leak prevention using ASAN
- Signal-based server gracefully terminates and properly cleans up resources

## Core Components

**TTT Server**
- Manages the game states; pairs clients together for matches
- Checks if moves are valid and if it's their proper turn
- Displays the board and the position of X's and O's 
- Said moves are custom protocols
- Automatically cleans up terminated connections and completed games

**TTT Client** 
- Self-explanatory
- Communicates to the server throughout the match.

## Specifications

- Written in C
- POSIX Threads (pthread), Mutex synchronization
- Berkeley Sockets (TCP/IPv4 and IPv6)
- GNU Make with ASAN enabled
- POSIX signals (SIGINT, SIGTERM) for graceful shutdown

## Building/Deployment
```bash
make           # Builds both server binary and test client
make clean     # In the situation that an error occurs; rebuilds

# Start server on a port
./ttts 8080

# Connect test client
./ttt localhost 8080
```

## Protocol

### Message Format (Used by client and server)
- `COMMAND|LENGTH|PAYLOAD|`

### Core Commands (Client-side)
- `PLAY|{length}|{playername}|` - Begin game search
- `MOVE|{length}|{mark}|{coordinates}|` - Submit move
- `DRAW|{length}|{action}|` - Handle draw negotiations
- `RSGN|{length}|` - Resign

### Server Responses
- `WAIT|0|` - Matchmaking in progress  
- `BEGN|{length}|{role}|{opponent}|` - Game session started
- `MOVD|{length}|{mark}|{coords}|{board}|` - Move confirmed
- `OVER|{length}|{result}|{message}|` - Game terminated
- `INVL|{length}|{reason}|` - Invalid response from client

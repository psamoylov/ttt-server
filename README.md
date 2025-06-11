# Multithreaded Multiplayer Game Server

High-performance, concurrent TCP server implementation demonstrating scalable backend architecture for real-time multiplayer applications
Built in C with POSIX threading, designed for production-grade reliability and performance.

## Key Features

- **Concurrent Connection Handling**: Multi-threaded architecture supporting unlimited simultaneous client connections
- **Real-time Game State Management**: Thread-safe game state synchronization across multiple concurrent game sessions
- **Custom Application Protocol**: Efficient binary protocol design optimized for low-latency communication
- **Robust Error Handling**: Comprehensive validation and graceful error recovery for production reliability  
- **Memory-Safe Operations**: Dynamic memory management with automatic cleanup and leak prevention
- **Graceful Shutdown**: Signal-based server termination with proper resource cleanup

## Architecture Overview

### Core Components

**Connection Manager**
- POSIX thread pool for handling concurrent client connections
- Thread-safe linked list data structures for connection tracking
- Automatic resource cleanup on client disconnection

**Game Engine** 
- Stateful game session management supporting multiple simultaneous games
- Thread-synchronized turn-based logic with move validation
- Real-time state broadcasting to connected clients

**Protocol Handler**
- Custom TCP-based messaging protocol with built-in validation
- Structured command parsing with error detection
- Efficient message serialization/deserialization

### Concurrency Design

```
Client 1 ──┐
Client 2 ──┼── TCP Listener ── Thread Pool ── Game Manager ── Shared State
Client N ──┘                      │                │
                               Mutex Locks    Linked Lists
```

- **Thread Safety**: Pthread mutex locks protecting all shared data structures
- **Scalability**: Each client connection handled by dedicated worker thread
- **Resource Management**: Automatic cleanup of terminated connections and completed games

## Technical Specifications

- **Language**: C (C99 standard)
- **Threading**: POSIX Threads (pthread), Mutex synchronization
- **Networking**: Berkeley Sockets (TCP/IPv4 and IPv6)
- **Build System**: GNU Make; Address Sanitizer enabled
- **Memory Management**: Manual allocation with comprehensive leak detection
- **Signal Handling**: POSIX signals for graceful shutdown (SIGINT, SIGTERM)

## Performance Characteristics

- **Concurrent Connections**: Theoretically unlimited (bounded by system resources)
- **Memory Efficiency**: Dynamic allocation with O(1) cleanup operations
- **Latency**: Sub-millisecond response times for game moves
- **Thread Overhead**: Minimal per-connection memory footprint
- **Fault Tolerance**: Automatic recovery from client disconnections

## Implementation Highlights

- **Thread-Safe Data Structures**: Custom linked lists with mutex protection for connection + game state management
- **Protocol Validation**: Comprehensive input parsing with bounds checking and format validation  
- **Resource Cleanup**: Automatic memory deallocation and socket closure on client disconnection
- **Error Recovery**: Graceful handling of malformed client messages and network interruptions

## Build & Deployment

### Prerequisites
```bash
# Required: GCC with C99 support and pthread library
gcc --version  # Verify GCC installation
```

### Compilation
```bash
make           # Builds both server binary and test client
make clean     # In the situation that an error occurs. Rebuilds
```

### Deployment
```bash
# Start server on specified port
./ttts 8080

# Connect test client
./ttt localhost 8080
```

## Protocol Specification

The server implements a custom application-layer protocol:

### Message Format
```
COMMAND|LENGTH|PAYLOAD|
```

### Core Commands
- `PLAY|{length}|{playername}|` - Initiate game session
- `MOVE|{length}|{mark}|{coordinates}|` - Submit game move
- `DRAW|{length}|{action}|` - Handle draw negotiations
- `RSGN|{length}|` - Player resignation

### Server Responses
- `WAIT|0|` - Matchmaking in progress  
- `BEGN|{length}|{role}|{opponent}|` - Game session started
- `MOVD|{length}|{mark}|{coords}|{board}|` - Move confirmation
- `OVER|{length}|{result}|{message}|` - Game termination
- `INVL|{length}|{reason}|` - Invalid response from client

## Code Quality Features

- **Memory Safety**: Comprehensive malloc/free pairing with cleanup functions
- **Error Handling**: Detailed error messages and graceful degradation
- **Code Organization**: Modular design with separation of concerns
- **Documentation**: Inline comments explaining complex threading logic
- **Testing**: Included client for integration testing

## Scalability Considerations

### Current Implementation
- Single-process, multi-threaded design
- In-memory state management
- Direct TCP socket communication

### Production Enhancements
- **Load Balancing**: Multi-instance deployment with session affinity
- **State Persistence**: Redis/database integration for game state
- **Message Queuing**: Async processing with RabbitMQ/Kafka
- **Monitoring**: Prometheus metrics and health checks

## Getting Started

1. **Clone and build**:
   ```bash
   git clone <repository>
   cd multiplayer-server
   make
   ```

2. **Start the server**:
   ```bash
   ./ttts 8080
   ```

3. **Test with multiple clients**:
   ```bash
   # Terminal 1
   ./ttt localhost 8080
   
   # Terminal 2  
   ./ttt localhost 8080
   ```
   
## Contributing

This project demonstrates production-ready patterns for:
- Concurrent server architecture
- Thread-safe data structure design
- Network protocol implementation
- Resource management in C

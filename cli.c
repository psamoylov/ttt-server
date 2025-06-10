// ttt - which implements a simple game client
//         Arguments are domain name and port number of the desired service
//     Connect to the service (provided ttts)
//     Display the current state of the grid to the player
//     Report moves by the other player
//     Obtain and transmit moves made by its player.

// Connections are initiated by the client, which sends PLAY
//     In response, the server will reply with WAIT or INVL
//         Clients will wait for the next message.
//     Once the server has connected two players, it will send BEGN to both players
//         Along with their role and their opponent's name.
//         Player X will move first.

// Game move
//     Moving player
//         The client for the player who is moving will send MOVE, RSGN, or DRAW S.
//             In response to MOVE, the server will reply MOVD or INVL.
//                 A response of MOVD means the move was accepted and it is now the opposing player's turn.
//             In response to RSGN
//                 The server will reply OVER
//             In response to DRAW S
//                 The server will reply with DRAW R if the opposing player chooses not to draw
//                 OVER if the opposing player has chosen to draw.
//     Waiting player
//         The client for the player who is not moving will wait for the server to indicate the opposing player's move
//             Will be one of MOVD, DRAW S, or OVER.
//         After receiving MOVD
//             It is now that player's turn.
//         After receiving DRAW S
//             The player can respond with DRAW R or DRAW A and then wait for the next move.
//         After receiving OVER
//             The game has ended

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>

int connect_inet(char *host, char *service){
    struct addrinfo hints, *info_list, *info;
    int sock, error;

    // look up remote host
    memset(&hints, 0, sizeof(hints)); // set everything to zero in hints
    hints.ai_family   = AF_UNSPEC;  // in practice, this means give us IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // indicate we want a streaming socket. 
                                     // presents communication as a stream, like a pipe

    error = getaddrinfo(host, service, &hints, &info_list);
    if (error) { // did it work?
        fprintf(stderr, "error looking up %s:%s: %s\n", host, service, gai_strerror(error));
        return -1;
    }

    // iterate through linked list
    for (info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol); // make a socket for this family, socktype, protocol
        if (sock < 0) continue; // if -1, didn't work. try next one

        error = connect(sock, info->ai_addr, info->ai_addrlen); // worked. try connecting
        if (error) { // something went wrong
            close(sock);
            continue; // try next method
        }

        break;
    }
    freeaddrinfo(info_list); //not important anymore, free it

    if (info == NULL) { // on success it won't be null. skip
                        // if none succeeds. couldn't open a connection
        fprintf(stderr, "Unable to connect to %s:%s\n", host, service);
        return -1;
    }

    return sock; // we now have an open file representing a network connection to the requested host
                 // a fd. can use read to recieve data, and write to send data.
                    // remember that read blocks data when data is not yet available
                    // data arriving from network is held by os until we ask for it
                 // can also use recv() and send(), which have more parameters
}



#define BUFSIZE 256

int main(int argc, char **argv){
    int sock, bytes;
    char buf[BUFSIZE];
    char recieve[BUFSIZE];

    //takes two arguments - domain name and port number
    //example - ./ttts localhost 15000

    if (argc != 3) {
        printf("Specify host and service\n");
        exit(EXIT_FAILURE);
    }

    // try to open a connection to it
    sock = connect_inet(argv[1], argv[2]);
    if (sock < 0) exit(EXIT_FAILURE);



    // if succeeds, tries reading from standard input
    while ((bytes = read(STDIN_FILENO, buf, BUFSIZE)) > 0) {
        write(sock, buf, bytes);
        int msg = read(sock, recieve, BUFSIZE);
        recieve[msg] = '\0';
        if (strcmp(recieve, "WAIT|O|")){ // waits to receive another message before writing.
            printf("server says WAIT: %d bytes, with the message: %s\n", msg, recieve);
            int msg = read(sock, recieve, BUFSIZE);
            printf("server says WAIT: %d bytes, with the message: %s\n", msg, recieve);
        } else if (strcmp(recieve, "INVL|16|Invalid command|")){ // recieves message. Immediately given another opportunity to write another command.
            printf("server says INVL: %d bytes, with the message: %s\n", msg, recieve);
        } else {
            printf("server says BEGN: %d bytes, with the message: %s\n", msg, recieve);
        }
        // FIXME: should check whether the write succeeded!
    }

    close(sock);

    return EXIT_SUCCESS;
}

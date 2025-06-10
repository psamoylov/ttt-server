// ttts - server used to coordinate games and enforce the rules
//         Argument is the port number it will use for connection requests.
//     Pair up players
//     Choose who will go first
//     Receive commands from the players
//     Track the state of the grid
//     Ensure that invalid moves are rejected 
//     Determine when the game has ended

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


// NOTE: must use option -pthread when compiling!
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#define QUEUE_SIZE 8

//lock stuff
pthread_mutex_t lock;
int count = 0;

volatile int active = 1;

void handler(int signum){
    active = 0;
}

// set up signal handlers for primary thread
// return a mask blocking those signals for worker threads
// FIXME should check whether any of these actually succeeded
void install_handlers(sigset_t *mask){
    struct sigaction act;
    act.sa_handler = handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);

    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
    
    sigemptyset(mask);
    sigaddset(mask, SIGINT);
    sigaddset(mask, SIGTERM);
}

// data to be sent to worker threads
typedef struct connection_data {
	struct sockaddr_storage addr;
	socklen_t addr_len;
	int fd;
}connection_data;


typedef struct Game{
    int gameNumber;
    int playerOne; // whoever wrote play first
    char *playerOneName;
    int playerOneSize;
    int playerTwo; // whoever wrote play second
    char *playerTwoName;
    int playerTwoSize;
    int turn; //0 is p1 and 1 is p2
    char *grid;
    int draw;
    int olive;
    struct Game *next;
}Game;

struct Game *gameList = NULL;
int gameCount = 1;

// linked list of connections
typedef struct fdList{
    int fileDescriptor;
    int start;
    int ingame;
    int finished;
    struct fdList *next;
}fdList;

struct fdList *fileDescriptors = NULL;


//inserts socket into LL
struct fdList *insertFdList(int fd, struct fdList *head){

    pthread_mutex_lock(&lock);


    struct fdList *sub = calloc(1, sizeof(struct fdList));

    sub->fileDescriptor = fd;
    sub->start = 0;
    sub->ingame = 0;
    sub->finished = 0;
    sub->next = NULL;
    //if LL is empty
    if (head == NULL){
        head = sub;
        pthread_mutex_unlock(&lock);
	    return head;
    } else {
        struct fdList *current = head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = sub;
    }
    pthread_mutex_unlock(&lock);
    return head;
}

void traverseFileDescriptors(struct fdList *head){
    struct fdList *current = head;
    while (current != NULL) {
        printf("fd: %d, ", current->fileDescriptor);
        printf("looking for game: %d, ", current->start);
        printf("in game: %d", current->ingame);

        if (current->next != NULL){
		    printf(" | ");
        }
        current = current->next;
    }
	printf("\n");

    return;
}

struct fdList *searchFileList(int fileDesc){
    struct fdList *current = fileDescriptors;
    while (current != NULL) {
        if (current->fileDescriptor == fileDesc){
            return current;
        }
        current = current->next;
    }
    return NULL;
}

struct fdList *finishedGame(int target, struct fdList *head){
    pthread_mutex_lock(&lock);
    struct fdList *current = head;
    //If head is the target
    while (current != NULL) {
        if (current->finished == 0 && current->fileDescriptor == target){
            current->finished = 1;
            current->ingame = 0;
            pthread_mutex_unlock(&lock);
            return current;
        }
        else if (current->finished == 1 && current->fileDescriptor == target){
            pthread_mutex_unlock(&lock);
            return current;
        }
        current = current->next;
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

int isFinished(int target){
    struct fdList *current = fileDescriptors;
    while (current != NULL) {
        if (current->fileDescriptor == target && current->finished == 1){
            return 1;
        }
        current = current->next;
    }
    return 0;
}

struct fdList *deleteFd(int target, struct fdList *head){
    pthread_mutex_lock(&lock);
    struct fdList *current = head;
    struct fdList *prev = current;
    //If head is the target
    if (current == NULL){
        pthread_mutex_unlock(&lock);
        return head;
    }
    if (current->fileDescriptor == target){
        head = head->next;
        free(current);
        pthread_mutex_unlock(&lock);
        return head;
    }

    //Removing middle of the pack
    while (current != NULL) {
        if (current->fileDescriptor == target){
            if (current->next == NULL){
                prev->next = NULL;
                free(current);
                pthread_mutex_unlock(&lock);
                return head;
            }
            prev->next = current->next;
            free(current);
            pthread_mutex_unlock(&lock);
            return head;
        }
        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&lock);

    return head;
}

int open_listener(char *service, int queue_size){
    struct addrinfo hint, *info_list, *info;
    int error, sock;
    int reuse = 1;  

    // initialize hints
    memset(&hint, 0, sizeof(struct addrinfo));
    hint.ai_family   = AF_UNSPEC;
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_flags    = AI_PASSIVE;

    // obtain information for listening socket
    error = getaddrinfo(NULL, service, &hint, &info_list);
    if (error) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(error));
        return -1;
    }

    // attempt to create socket
    for (info = info_list; info != NULL; info = info->ai_next) {
        sock = socket(info->ai_family, info->ai_socktype, info->ai_protocol);

        // if we could not create the socket, try the next method
        if (sock == -1) continue;

        // Socket reuse option
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            perror("setsockopt");
            close(sock);
            continue;
        }

        // bind socket to requested port
        error = bind(sock, info->ai_addr, info->ai_addrlen);
        if (error) {
            close(sock);
            continue;
        }

        // enable listening for incoming connection requests
        error = listen(sock, queue_size);
        if (error) {
            close(sock);
            continue;
        }

        // if we got this far, we have opened the socket
        break;
    }

    freeaddrinfo(info_list);

    // info will be NULL if no method succeeded
    if (info == NULL) {
        fprintf(stderr, "Could not bind\n");
        return -1;
    }

    return sock;
}

typedef struct readList{
    char *data;
	int size;
    struct readList *next;
}readList;

struct readList *deleteRL(char *target, struct readList *head){
    struct readList *current = head;
    //If head is the target
    if (current == NULL){
        return head;
    }
    if (current->data == target){
        head = head->next;
        free(current);
        return head;
    }

    return head;
}

struct readList *insertRL(char *word, struct readList *head, int len){
    struct readList *current = head;

    struct readList *sub = (struct readList *)malloc(sizeof(struct readList));
    sub->data = word;
	sub->size = len;
    //if LL is empty
    if (head == NULL){
	    sub->next = NULL;
        head = sub;
	    return head;
    }
    while (current->next != NULL) {
        current = current->next;
    }

    current->next = sub;
    sub->next = NULL;
    return head;
}

void traverseRL(struct readList *head){
    struct readList *current = head;
    while (current != NULL) {
        printf("%s", current->data);
        if (current->next != NULL){
		    printf("|");
        }
        current = current->next;
    }
	printf("\n");

    return;
}

void lengthRL(struct readList* head){
    int length = 0;
    struct readList *current = head;
    while(current != NULL){
        length++;
        current = current->next;
    }
    printf("%d", length);
    return;
}

struct readList *freeRL(struct readList* head){
    struct readList* sub;
    while (head != NULL){
        sub = head;
        head = head->next;
        free(sub->data);
		sub->data = NULL;
		free(sub);
		sub = NULL;
    }
	return head;
}

char *makeWord(int wordSize, int runThrough, char *lineBuffer){
	char *newWord = calloc(wordSize + 1, sizeof(char)); // +1 is to accomodate for '\0'
	memmove(newWord, &lineBuffer[runThrough - wordSize], wordSize); // copy char into newWord
    newWord[wordSize] = '\0';
    return newWord;
}

struct readList *turnToRL(int linePos, char *lineBuffer, struct readList *head){
	int wordSize = 0;
    int runThrough = 0;
    int howManyPipes = 0;
    if ((lineBuffer[runThrough] == '\n')){ //improper format - line is empty
		// fputs("line is empty\n", stderr);
		return head; //head will be NULL.
	}   
	if ((lineBuffer[runThrough] == '|')){ //improper format - | should not be the first character
		// fputs("| should not be first character!\n", stderr);
		return head; //head will be NULL.
	} 

    int noPipe = 0;
    if ((lineBuffer[linePos - 2] != '|')){ // improper format - | should be the last character
		noPipe = 1;
    }
	while ((runThrough < linePos - 1)){ //reads the first four characters
		if (lineBuffer[runThrough] == '|'){
            howManyPipes++;
			//allocates size of thing before space
			head = insertRL(makeWord(wordSize, runThrough, lineBuffer), head, wordSize);
			wordSize = 0;
		} else {
            wordSize++;
        }
		runThrough++;
	}
    if ((runThrough == (linePos - 1)) && (noPipe == 1)){
        head = insertRL(makeWord(wordSize, runThrough, lineBuffer), head, wordSize);
        wordSize = 0;
    }

	return head;
}

struct readList *turnToRLCompletely(int linePos, char *lineBuffer, struct readList *head){
	int wordSize = 0;
    int runThrough = 0;
    if ((lineBuffer[runThrough] == '\n')){ //improper format - line is empty
		// fputs("line is empty\n", stderr);
		return head; //head will be NULL.
	}   
	if ((lineBuffer[runThrough] == '|')){ //improper format - | should not be the first character
		// fputs("| should not be first character\n", stderr);
		return head; //head will be NULL.
	} 
    if ((lineBuffer[linePos - 2] != '|')){ // improper format - | should be the last character
		// fputs("| should be last character\n", stderr);
		return head; //head will be NULL.
    }

	while ((runThrough < linePos)){ //reads the first four characters
		if (lineBuffer[runThrough] == '|'){
			//allocates size of thing before space
			head = insertRL(makeWord(wordSize, runThrough, lineBuffer), head, wordSize);
			wordSize = 0;
		} else {
            wordSize++;
        }
		runThrough++;
	}

	return head;
}


struct Game *initGame(struct Game *head){
    pthread_mutex_lock(&lock);
    struct Game *sub = calloc(1, sizeof(struct Game));
    sub->gameNumber = gameCount; // set game number
    gameCount++;
    sub->playerOne = 0;
    sub->playerTwo = 1;
    sub->next = NULL;
    pthread_mutex_unlock(&lock);
    head = sub;
    return head; 
}

struct Game *insertGame(char *name, struct Game *head, int fd, int nameSize){
    pthread_mutex_lock(&lock);
    struct Game *current = head->next;
    if (current == NULL){
        // malloc the game
        struct Game *sub = calloc(1, sizeof(struct Game));
        sub->gameNumber = gameCount; // set game number
        gameCount++;
        sub->playerOne = fd;
        char playerName[85];
        strcpy(playerName, name);
        sub->playerOneName = playerName;
        sub->playerOneSize = nameSize;
        sub->playerTwo = 0;
        sub->draw = 0;
        sub->olive = 0;
        //playerTwoName will be empty
        //playerTwoSize will be empty
        char tttGrid[10];
        for (int i = 0; i < 9; i++){
            char letter = '.';
            tttGrid[i] = letter;
        }
        tttGrid[9] = '\0';
        sub->turn = 0;
        sub->grid = tttGrid;
        sub->next = NULL;
        struct fdList *playerFd = searchFileList(fd);
        playerFd->start = 1;
        head->next = sub;
        pthread_mutex_unlock(&lock);
        return head; 
    }
    while (current->next != NULL){
        current = current->next;
    }
    if (current->playerTwo == 0){
        current->playerTwo = fd;
        current->playerTwoSize = nameSize;
        char playerName[85];
        strcpy(playerName, name);
        current->playerTwoName = playerName;

        //I changed this a bit since you only used it in BEGN, but when this runs, it displays the OPPONENT'S name.
        char *opponentName = current->playerOneName;
        int opponentSize = current->playerOneSize;
        opponentSize = opponentSize + 3; //for the additional stuff "X| ... |"

        char lengthOfRest[85];
        sprintf(lengthOfRest, "%d", opponentSize);

        char *begn = "BEGN|";
        char *symbol = "|X|";
        char *firstOpponent = opponentName;
        char *lastBar = "|";

        // printf("\n\n%s", begn);       
        // printf("%s", lengthOfRest);
        // printf("%s", symbol);
        // printf("%s", firstOpponent);
        // printf("%s\n", lastBar);

        char reason[99];
        strcpy(reason, begn);
        strcat(reason, lengthOfRest);
        strcat(reason, symbol);
        strcat(reason, firstOpponent);
        strcat(reason, lastBar);

        write(current->playerOne, reason, strlen(reason));

        //player Two
        int otherSize = current->playerTwoSize;
        char *otherName = current->playerTwoName;
        otherSize = otherSize + 3;

        char lengthOfRestTwo[85];
        sprintf(lengthOfRestTwo, "%d", otherSize);

        char *newSymbol = "|O|";
        char *secondOpponent = otherName;

        // printf("%s", begn);       
        // printf("%s", lengthOfRestTwo);
        // printf("%s", newSymbol);
        // printf("%s", secondOpponent);
        // printf("%s\n", lastBar);

        char reasonTwo[99];
        strcpy(reasonTwo, begn);
        strcat(reasonTwo, lengthOfRestTwo);
        strcat(reasonTwo, newSymbol);
        strcat(reasonTwo, secondOpponent);
        strcat(reasonTwo, lastBar);

        struct fdList *playerFd = searchFileList(current->playerOne);
        playerFd->ingame = 1;
        playerFd->start = 0;
        playerFd = searchFileList(current->playerTwo);
        playerFd->ingame = 1;
        playerFd->start = 0;

        write(current->playerTwo, reasonTwo, strlen(reasonTwo));
        pthread_mutex_unlock(&lock);
        return head;
    } else {
        // malloc the game
        struct Game *sub = calloc(1, sizeof(struct Game));
        sub->gameNumber = gameCount; // set game number
        gameCount++;
        sub->playerOne = fd;
        char playerName[85];
        strcpy(playerName, name);
        sub->playerOneName = playerName;
        sub->playerOneSize = nameSize;
        sub->playerTwo = 0;
        sub->draw = 0;
        sub->olive = 0;
        //playerTwoName will be empty
        //playerTwoSize will be empty
        char tttGrid[10];
        for (int i = 0; i < 9; i++){
            char letter = '.';
            tttGrid[i] = letter;
        }
        tttGrid[9] = '\0';
        sub->turn = 0;
        sub->grid = tttGrid;
        sub->next = NULL;
        struct fdList *playerFd = searchFileList(fd);
        playerFd->start = 1;
        current->next = sub;
        pthread_mutex_unlock(&lock);
        return head; 
    }
}

void traverseGames(struct Game *head){
    struct Game *current = head;
    while (current != NULL) {
        // puts("here?\n");
        printf("game: %d, ", current->gameNumber);
        printf("p1: %d, ", current->playerOne);
        printf("p2: %d", current->playerTwo);

        if (current->next != NULL){
		    printf(" | ");
        }
        current = current->next;
    }
    printf("\n----------\n");

    return;
}

struct Game *findGame(struct Game *head, int fd){
    struct Game *current = head->next;
    // searches for game
    while (current != NULL){
        if (fd == (current->playerOne)){
            return current;
        } else if (fd == (current->playerTwo)){
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int findDuplicateName(struct Game *head, char *name){//returns 1 if duplicate name
    struct Game *current = head->next;
    // searches for game
    while (current != NULL){
        if (strcmp(name, current->playerOneName) == 0){
            return 1;
        } else if (current->playerTwo != 0){
            if (strcmp(name, current->playerTwoName) == 0){
                return 1;
            }
        }
        current = current->next;
    }
    return 0;
}

struct Game *deleteGame(struct Game *target, struct Game *head){
    pthread_mutex_lock(&lock);
    struct Game *current = head;
    struct Game *prev = current;
    current = current->next;
    //If head is the target
    if (current == NULL){
        pthread_mutex_unlock(&lock);
        return head;
    }
    //Removing middle of the pack
    while (current != NULL) {
        if (current == target){
            if (current->next == NULL){
                prev->next = NULL;
            } else {
                prev->next = current->next;
            }
            // Free the dynamically allocated memory
            if (current->playerOneName) free(current->playerOneName);
            if (current->playerTwoName) free(current->playerTwoName);
            if (current->grid) free(current->grid);
            free(current);
            pthread_mutex_unlock(&lock);
            return head;
        }
        prev = current;
        current = current->next;
    }
    pthread_mutex_unlock(&lock);
    return head;
}

int isNumber(const char* str) {
    if (str == NULL || *str == '\0') {
        return 0; // empty string or null pointer
    }
    while (*str != '\0') {
        if (!isdigit(*str)) {
            return 0; // non-digit character found
        }
        str++;
    }
    return 1;
}

char* printBoard(int arr[]) {
    char* str = calloc(10, sizeof(char)); // allocate memory for string
    int i;
    for (i = 0; i < 10; i++) {
        str[i] = arr[i] + '0'; // convert integer to character
    }

    str[9] = '\0'; // terminate the string
    return str;
}

int checkForWin(char square[]){
    int returnValue = 0;
    if ((square[0] != '.') && (square[0] == square[1]) && (square[1] == square[2])) returnValue = 1;
    else if ((square[3] != '.') &&(square[3] == square[4]) && (square[4] == square[5])) returnValue = 1;
    else if ((square[6] != '.') &&(square[6] == square[7]) && (square[7] == square[8])) returnValue = 1;
    else if ((square[0] != '.') &&(square[0] == square[4]) && (square[4] == square[8])) returnValue = 1;
    else if ((square[2] != '.') &&(square[2] == square[4]) && (square[4] == square[6])) returnValue = 1;
    else if ((square[0] != '.') &&(square[0] == square[3]) && (square[3] == square[6])) returnValue = 1;
    else if ((square[1] != '.') &&(square[1] == square[4]) && (square[4] == square[7])) returnValue = 1;
    else if ((square[2] != '.') &&(square[2] == square[5]) && (square[5] == square[8])) returnValue = 1;
    else returnValue = -1;
    return returnValue;
}

//check if EVERY slot has been used
int checkForDraw(char square[]){
    for (int i = 0; i < 9; i++) {
        if (square[i] == '.') return 0;
    }
  return 1;
}

#define BUFSIZE 256
#define HOSTSIZE 100
#define PORTSIZE 10
void *read_data(void *arg){
	struct connection_data *con = arg;
    char buffer[BUFSIZE + 1], host[HOSTSIZE], port[PORTSIZE];
    int bytes, error;
    int pos;

    if(fileDescriptors == NULL){
        fileDescriptors = insertFdList(0, fileDescriptors);
    }

    // traverseGames(gameList);
    fileDescriptors = insertFdList(con->fd, fileDescriptors);
    traverseFileDescriptors(fileDescriptors);

    error = getnameinfo((struct sockaddr *)&con->addr, con->addr_len,
    host, HOSTSIZE, port, PORTSIZE, NI_NUMERICSERV);

    if (error) {
        fprintf(stderr, "getnameinfo: %s\n", gai_strerror(error));
        strcpy(host, "??");
        strcpy(port, "??");
    }

    // printf("Connection from %s:%s\n", host, port);

    char *lineBuffer = malloc(BUFSIZE);
    int lineSize = BUFSIZE;
    int linePos = 0;
	readList *list = NULL;

    fdList *yourFd = searchFileList(con->fd);
    yourFd->finished = 0;
    //is the client in game? set to 1 after play
    int ingame = 0;
    int searching = 0;

    while ((yourFd->finished == 0) && active && (bytes = read(con->fd, buffer, BUFSIZE)) > 0) { //con->fd is this thread's current file descriptor
        puts("\n");

		for (pos = 0; pos < bytes; ++pos) {
			if (buffer[pos] == '\n') {
				int thisLen = pos + 1;
//////////////  vvvvv where append originally was vvvvv

                    // buf is buffer + lstart
                    // thisLen is len

                char *buf = buffer;
                int len = thisLen;
                
                int newPos = linePos + len;
                if (newPos > lineSize) {
                    lineSize *= 2;
                    assert(lineSize >= newPos);
                    lineBuffer = realloc(lineBuffer, lineSize);
                    if (lineBuffer == NULL) {
                        perror("line buffer");
                        exit(EXIT_FAILURE);
                    }
                }

                memcpy(lineBuffer + linePos, buf, len);
                linePos = newPos;

//////////////  ^^^^^ where append originally was ^^^^^

				list = turnToRL(linePos, lineBuffer, list);
                traverseRL(list);

                int runThrough = 0;
                int howManyPipes = 0;
                while ((runThrough < linePos - 1)){ //reads the first four characters
                    if (lineBuffer[runThrough] == '|'){
                        howManyPipes++;
                        //allocates size of thing before space
                    }
                    runThrough++;
                }
                if (howManyPipes < 2){
                    list = freeRL(list);
                    char *reason = "INVL|31|Cannot measure size accurately|"; ///////////////////////////////////////////////////////////////////////////
                    write(con->fd, reason, strlen(reason));
                    if (yourFd->ingame == 1){
                        Game *thisGame = findGame(gameList, con->fd);
                        char *whatHappened = "OVER|24|W|Opponent has resigned|";
                        fdList *otherFd;
                        if (con->fd == thisGame->playerOne) {
                            write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerTwo);
                        } else { //con->fd is player Two
                            write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerOne);
                        }
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        otherFd->finished = 1;
                        deleteGame(thisGame, gameList);

                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                        close(otherFd->fileDescriptor);
                    } else { //not in a game
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                    }
				    linePos = 0;
                    buffer[bytes] = '\0';
                    break;
                }



                if (list == NULL){
                    list = freeRL(list);
                    char *reason = "INVL|16|Invalid command|"; ///////////////////////////////////////////////////////////////////////////
                    write(con->fd, reason, strlen(reason));
                    if (yourFd->ingame == 1){
                        Game *thisGame = findGame(gameList, con->fd);
                        char *whatHappened = "OVER|24|W|Opponent has resigned|";
                        fdList *otherFd;
                        if (con->fd == thisGame->playerOne) {
                            write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerTwo);
                        } else { //con->fd is player Two
                            write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerOne);
                        }
                         pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        otherFd->finished = 1;
                        deleteGame(thisGame, gameList);

                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                        close(otherFd->fileDescriptor);
                    } else { //not in a game
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                    }
				    linePos = 0;
                    buffer[bytes] = '\0';
                    break;
                } else if (list->next == NULL){
                // use THIS code right here for when the code's wrong for now...
                    list = freeRL(list);
                    char *reason = "INVL|16|Invalid command|"; ///////////////////////////////////////////////////////////////////////////
                    write(con->fd, reason, strlen(reason));
                    if (yourFd->ingame == 1){
                        Game *thisGame = findGame(gameList, con->fd);
                        char *whatHappened = "OVER|24|W|Opponent has resigned|";
                        fdList *otherFd;
                        if (con->fd == thisGame->playerOne) {
                            write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerTwo);
                        } else { //con->fd is player Two
                            write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerOne);
                        }
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        otherFd->finished = 1;
                        deleteGame(thisGame, gameList);

                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                        close(otherFd->fileDescriptor);
                    } else { //not in a game
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                    }
				    linePos = 0;
                    buffer[bytes] = '\0';
                    break;
                }////
                
                
                // checks if it was a complete message
                readList *fieldTwo = list->next;
                int fieldChecker = isNumber(fieldTwo->data);
                int fieldNumber = 0;
                int firstTwoFields;
                int addl_bytes = 0;
                if (fieldChecker == 1){ // it's a number
                    fieldNumber = atoi(fieldTwo->data);
                    firstTwoFields = fieldTwo->size + 6;
                    fieldNumber = fieldNumber + firstTwoFields; //what the byte length should be
                    if (fieldNumber > (bytes - 1)){ // need to read one more time
                        list = freeRL(list);
                        addl_bytes = read(con->fd, buffer + (bytes - 1), BUFSIZE - bytes); //edge case needed if read returns 0 or -1
                        pos = bytes - 1;
                        thisLen = pos + 1;

                        buf = buffer;
                        len = thisLen;                        
                        linePos = 0;
                        newPos = linePos + len;
                        if (newPos > lineSize) {
                            lineSize *= 2;
                            assert(lineSize >= newPos);
                            lineBuffer = realloc(lineBuffer, lineSize);
                            if (lineBuffer == NULL) {
                                perror("line buffer");
                                exit(EXIT_FAILURE);
                            }
                        }
                        memcpy(lineBuffer + linePos, buf, len + addl_bytes - 1);
                        linePos = newPos + addl_bytes - 1;

                        list = turnToRLCompletely(linePos, lineBuffer, list); //add the rest, then merge the third field with next if necessary.
                    } else if (fieldNumber < (bytes - 1)){ // size is smaller - kill the program
                            list = freeRL(list);
                            char *reason = "INVL|16|Incorrect bytes|";
                            write(con->fd, reason, strlen(reason));
                            if (yourFd->ingame == 1){
                                Game *thisGame = findGame(gameList, con->fd);
                                char *whatHappened = "OVER|24|W|Opponent has resigned|";
                                fdList *otherFd;
                                if (con->fd == thisGame->playerOne) {
                                    write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerTwo);
                                } else { //con->fd is player Two
                                    write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerOne);
                                }
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                                close(otherFd->fileDescriptor);
                            } else { //not in a game
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                            }
                            linePos = 0;
                            buffer[bytes] = '\0';
                            break;
                    }
                } else { // err - not a number
                            list = freeRL(list);
                            char *reason = "INVL|23|Field two not a number|";
                            write(con->fd, reason, strlen(reason));
                            if (yourFd->ingame == 1){
                                Game *thisGame = findGame(gameList, con->fd);
                                char *whatHappened = "OVER|24|W|Opponent has resigned|";
                                fdList *otherFd;
                                if (con->fd == thisGame->playerOne) {
                                    write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerTwo);
                                } else { //con->fd is player Two
                                    write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerOne);
                                }
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                                close(otherFd->fileDescriptor);
                            } else { //not in a game
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                            }
                            linePos = 0;
                            buffer[bytes] = '\0';
                            break;
                }

                readList *findLength = list;
                findLength = findLength->next->next; //name->length->next args


                int listLength = 0;                
                while (findLength != NULL){
                    listLength = listLength + findLength->size + 1;
                    findLength = findLength->next;
                } 


                printf("updated -> ");
                traverseRL(list);

                assert(lineBuffer[linePos-1] == '\n');

                readList *current = list; //name

/////////////////// CHECK THE SECOND FIELD. ///////////////////
                int fieldTwoNumber = atoi(fieldTwo->data);
                if ((fieldTwoNumber < listLength) || (fieldTwoNumber > listLength)){ // err - the length is wrong.
                    list = freeRL(list);
                    char *reason = "INVL|16|Incorrect bytes|";
                    write(con->fd, reason, strlen(reason));
                    if (yourFd->ingame == 1){
                        Game *thisGame = findGame(gameList, con->fd);
                        char *whatHappened = "OVER|24|W|Opponent has resigned|";
                        fdList *otherFd;
                        if (con->fd == thisGame->playerOne) {
                            write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerTwo);
                        } else { //con->fd is player Two
                            write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerOne);
                        }
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        otherFd->finished = 1;
                        deleteGame(thisGame, gameList);

                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                        close(otherFd->fileDescriptor);
                    } else { //not in a game
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                    }
                    linePos = 0;
                    buffer[bytes] = '\0';
                    break;
                }


                // returns the current game that the client is in
                Game *currentGame = NULL;
                if (ingame == 1){
                    currentGame = findGame(gameList, con->fd);
                }

// PLAY -> 10 -> Joe Smith -> NULL

                // play has 4 arguments
                if (strcmp("PLAY", current->data) == 0){ //don't need to check if draw == 0 since it already won't work if you're in a game (plus a game doesn't exist at this point and it breaks if I check it)
                    if (ingame == 1){ // err - game has already started
                        list = freeRL(list);
                        char *reason = "INVL|16|Already in game|"; ///////////////////////////////////////////////////////////////////////////
                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    } else if (current->next == NULL || current->next->next == NULL || current->next->next->next != NULL){ // err - length is empty
                            list = freeRL(list);
                            char *reason = "INVL|16|Invalid command|";
                            write(con->fd, reason, strlen(reason));
                            if (yourFd->ingame == 1){
                                Game *thisGame = findGame(gameList, con->fd);
                                char *whatHappened = "OVER|24|W|Opponent has resigned|";
                                fdList *otherFd;
                                if (con->fd == thisGame->playerOne) {
                                    write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerTwo);
                                } else { //con->fd is player Two
                                    write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerOne);
                                }
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                                close(otherFd->fileDescriptor);
                            } else { //not in a game
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                            }
                            linePos = 0;
                            buffer[bytes] = '\0';
                            break;
                    } else { //everything looks good...

                        // it's the second field now.
                        current = current->next;

                        // third field.
                        current = current->next;
                        if (current->size > 50){ // err - name too long
                            list = freeRL(list);
                            char *reason = "INVL|16|Name's too long|"; ///////////////////////////////////////////////////////////////////////////
                                                  //Name's too long|
                            write(con->fd, reason, strlen(reason));
                            linePos = 0;
                            buffer[bytes] = '\0';
                            continue;
                        }

                        if (gameList != NULL){
                            int check = findDuplicateName(gameList, current->data);
                            if (check == 1){
                                list = freeRL(list);
                                char *reason = "INVL|16|Name is occupied|"; ///////////////////////////////////////////////////////////////////////////
                                                   //17|Name is occupied|
                                write(con->fd, reason, strlen(reason));
                                linePos = 0;
                                buffer[bytes] = '\0';
                                continue;
                            }
                        }

                        ingame = 1;
                        searching = 1;

                        //everything looks all set? then execute play.
                        char *reason = "WAIT|0|";
                        write(con->fd, reason, strlen(reason));
                        sleep(1);
                        if (gameList == NULL){
                            gameList = initGame(gameList);
                            puts("init game\n");
                        }

                        gameList = insertGame(current->data, gameList, con->fd, current->size);
                        traverseGames(gameList);
                    }



//MOVE -> 6 -> X -> 2,2 -> NULL
                } else if (strcmp("MOVE", current->data) == 0) {
                    if (ingame == 0){ // err - game hasn't started
                        list = freeRL(list);
                        char *reason = "INVL|20|Game hasn't started|"; ///////////////////////////////////////////////////////////////////////////
                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    } else if (current->next == NULL || current->next->next == NULL || current->next->next->next == NULL
                                || current->next->next->next->next != NULL){ // err - length is empty
                            list = freeRL(list);
                            char *reason = "INVL|16|Invalid command|";
                            write(con->fd, reason, strlen(reason));
                            if (yourFd->ingame == 1){
                                Game *thisGame = findGame(gameList, con->fd);
                                char *whatHappened = "OVER|24|W|Opponent has resigned|";
                                fdList *otherFd;
                                if (con->fd == thisGame->playerOne) {
                                    write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerTwo);
                                } else { //con->fd is player Two
                                    write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerOne);
                                }
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                                close(otherFd->fileDescriptor);
                            } else { //not in a game
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                            }
                            linePos = 0;
                            buffer[bytes] = '\0';
                            break;
                    } else if (currentGame->draw != 0) { // err - draw was called, what are you doing brother
                        list = freeRL(list);
                        char *reason = "INVL|16|Draw was called|"; ///////////////////////////////////////////////////////////////////////////
                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    }
                    searching = 0;
                    
                    // it's the second field now.
                    current = current->next;
                    //should check if the length is 6

                    // third field. the character.
                    current = current->next;

                    // is it just one character (the mark)?
                    // check to see if the string given is either X or O
                    char mark[2];
                    mark[1] = '\0';
                    int remember; //0 if X, 1 if O
                    if (strcmp("X", current->data) == 0){ 
                        mark[0] = 'X'; // it's X
                        remember = 0;
                    } else if (strcmp("O", current->data) == 0){ 
                        mark[0] = 'O'; // it's O
                        remember = 1;
                    } else { // err - neither X nor O
                            list = freeRL(list);
                            char *reason = "INVL|16|Invalid command|";
                            write(con->fd, reason, strlen(reason));
                            if (yourFd->ingame == 1){
                                Game *thisGame = findGame(gameList, con->fd);
                                char *whatHappened = "OVER|24|W|Opponent has resigned|";
                                fdList *otherFd;
                                if (con->fd == thisGame->playerOne) {
                                    write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerTwo);
                                } else { //con->fd is player Two
                                    write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerOne);
                                }
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                                close(otherFd->fileDescriptor);
                            } else { //not in a game
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                            }
                            linePos = 0;
                            buffer[bytes] = '\0';
                            break;
                    }
                    
                    //ensures the right player is moving, the makes sure they're using the right mark
                    if ((currentGame->turn == 0) && (con->fd == currentGame->playerOne)) {
                        if (remember == 1) { // err - wrong mark. O when should be X
                            list = freeRL(list);
                            char *reason = "INVL|16|Wrong role used|"; ///////////////////////////////////////////////////////////////////////////
                            write(con->fd, reason, strlen(reason));
                            linePos = 0;
                            buffer[bytes] = '\0';
                            continue;
                        }
                    } else if ((currentGame->turn == 1) && (con->fd == currentGame->playerTwo)) {
                        if (remember == 0) { // err - wrong mark. X when should be O
                            list = freeRL(list);
                            char *reason = "INVL|16|Wrong role used|"; ///////////////////////////////////////////////////////////////////////////

                            write(con->fd, reason, strlen(reason));
                            linePos = 0;
                            buffer[bytes] = '\0';
                            continue;
                        }
                    } else { //wrong player
                        list = freeRL(list);
                        char *reason = "INVL|16|Wait your turn!|"; ///////////////////////////////////////////////////////////////////////////
                                              //Wait your turn!
                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    }
                    
                    //fourth field
                    current = current->next;
                    //we need the exact coords of where the move is being made
                    int fourthSize = current->size;
                    if (fourthSize != 3){ // err - size needs to be 3
                            list = freeRL(list);
                            char *reason = "INVL|16|Invalid command|";
                            write(con->fd, reason, strlen(reason));
                            if (yourFd->ingame == 1){
                                Game *thisGame = findGame(gameList, con->fd);
                                char *whatHappened = "OVER|24|W|Opponent has resigned|";
                                fdList *otherFd;
                                if (con->fd == thisGame->playerOne) {
                                    write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerTwo);
                                } else { //con->fd is player Two
                                    write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                    otherFd = searchFileList(thisGame->playerOne);
                                }
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                                close(otherFd->fileDescriptor);
                            } else { //not in a game
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                            }
                            linePos = 0;
                            buffer[bytes] = '\0';
                            break;
                            linePos = 0;
                            buffer[bytes] = '\0';
                            break;
                    }

                    char *fourthData = current->data;
                    if ((isdigit(fourthData[0]) == 0) || (isdigit(fourthData[2])) || (fourthData[1] != ',')){ // err - digit needs to be a number
                        list = freeRL(list);
                        char *reason = "INVL|16|Invalid command|";
                        write(con->fd, reason, strlen(reason));
                        if (yourFd->ingame == 1){
                            Game *thisGame = findGame(gameList, con->fd);
                            char *whatHappened = "OVER|24|W|Opponent has resigned|";
                            fdList *otherFd;
                            if (con->fd == thisGame->playerOne) {
                                write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerTwo);
                            } else { //con->fd is player Two
                                write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerOne);
                            }
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                            close(otherFd->fileDescriptor);
                        } else { //not in a game
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                        }
                        linePos = 0;
                        buffer[bytes] = '\0';
                        break;
                    }
                    char coords[5];
                    coords[0] = fourthData[0];
                    coords[1] = ',';
                    coords[2] = fourthData[2];
                    coords[3] = '|';
                    coords[4] = '\0';

                    //since the grid is a single array, add the coords minus 1 to get exactly where it should be in the array
                    int sum = 0;  
                    // 0 1 2
                    // 3 4 5
                    // 6 7 8
                    char first = coords[0];
                    char second = coords[2];
                    int firstInt = first - '0';
                    int secondInt = second - '0';

                    if (firstInt == 2) sum = 3;
                    else if (firstInt == 3) sum = 6;
                    sum += (secondInt);
                    sum = sum - 1;

                    //the very last check! ensure there isn't a mark where the move is being made

                    // printf("%d\n", sum);

                    // printf("%s\n", board);

                    if (currentGame->grid[sum] != '.') { // err - space is occupied 
                        list = freeRL(list);
                        char *reason = "INVL|16|Space occupied.|"; ///////////////////////////////////////////////////////////////////////////
                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    }
                    //everything looks all set? then execute move.
                    currentGame->grid[sum] = mark[0];
                    // printf("%s\n", board);

                    //now the server has to reply to both with the move made
                    // char* board = printBoard(gameList->grid);


                    char *movd = "MOVD|16|";
                    // printf("\n%s", movd);
                    // printf("%s", mark);
                    char *bar = "|";
                    // printf("%s", bar);
                    // printf("%s", coords);
                    // printf("%s", board);
                    // printf("%s\n\n", bar);

                    char *reason = calloc(150, sizeof(char));
                    strcpy(reason, movd);
                    strcat(reason, mark);
                    strcat(reason, bar);
                    strcat(reason, coords);
                    strcat(reason, currentGame->grid);
                    strcat(reason, bar);
                    // printf("\n%s\n\n", reason);

                    printf("%s\n", currentGame->grid);

                    //writes the updated position
                    write(con->fd, reason, strlen(reason));
                    if (con->fd == currentGame->playerOne) {
                        write(currentGame->playerTwo, reason, strlen(reason));
                    } else {
                        write(currentGame->playerOne, reason, strlen(reason));
                    }
                    free(reason);
                    
                    //who won?
                    int over = checkForWin(currentGame->grid); //it should check if the game is won due to the move made
                    //con->fd is the winner
                    int opponentSize;
                    char winnerName[85];
                    
                    if (currentGame->playerTwo == con->fd){
                        opponentSize = currentGame->playerTwoSize;
                        strcpy(winnerName, currentGame->playerTwoName);
                    } else if (currentGame->playerOne == con->fd){
                        opponentSize = currentGame->playerOneSize;
                        strcpy(winnerName, currentGame->playerOneName);
                    }

                    opponentSize = opponentSize + 12;
                    char *gameOver = "OVER|";

                    char lengthOfRest[99];
                    sprintf(lengthOfRest, "%d", opponentSize);

                    char *win = "W|";
                    char *loss = "L|";
                    char *hasWon = " has won.";

                    char winReason[150];
                    char lossReason[150];

                    // printf("\n\n%s", gameOver);       
                    // printf("%s", lengthOfRest);
                    // printf("%s", bar);
                    // printf("%s", win);
                    // printf("%s", winnerName);
                    // printf("%s\n", hasWon);

                    strcpy(winReason, gameOver);
                    strcat(winReason, lengthOfRest);
                    strcat(winReason, bar);
                    strcat(winReason, win);
                    strcat(winReason, winnerName);
                    strcat(winReason, hasWon);
                    strcat(winReason, bar);

                    strcpy(lossReason, gameOver);
                    strcat(lossReason, lengthOfRest);
                    strcat(lossReason, bar);
                    strcat(lossReason, loss);
                    strcat(lossReason, winnerName);
                    strcat(lossReason, hasWon);
                    strcat(lossReason, bar);


                    if (over == 1) { //if the game is over
                        fdList *otherFd;
                        if (con->fd == currentGame->playerOne) {
                            printf("%s\n", winReason);
                            write(con->fd, winReason, strlen(winReason));
                            write(currentGame->playerTwo, lossReason, strlen(lossReason));
                            otherFd = searchFileList(currentGame->playerTwo);

                        } else { //con->fd is player Two
                            write(con->fd, winReason, strlen(winReason));
                            write(currentGame->playerOne, lossReason, strlen(lossReason));
                            otherFd = searchFileList(currentGame->playerOne);
                        }
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        otherFd->finished = 1;
                        deleteGame(currentGame, gameList);

                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                        close(otherFd->fileDescriptor);
                        ingame = 0;
                        linePos = 0;
                        buffer[bytes] = '\0';
                        break;                    }
                    //now we need to know if the game should end by default due to no more possible moves existing
                    if (checkForDraw(currentGame->grid) == 1) {
                        fdList *otherFd;

                        char *reason = "OVER|17|D|No moves left.|";
                        write(con->fd, reason, strlen(reason));
                        if (con->fd == currentGame->playerOne) {
                            write(currentGame->playerTwo, reason, strlen(reason));
                            otherFd = searchFileList(currentGame->playerTwo);
                        } else {
                            write(currentGame->playerOne, reason, strlen(reason));
                            otherFd = searchFileList(currentGame->playerOne);
                        }
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        otherFd->finished = 1;
                                deleteGame(currentGame, gameList);

                        pthread_mutex_unlock(&lock);
                        close(currentGame->playerOne);
                        close(currentGame->playerTwo);
                        ingame = 0;
                        linePos = 0;
                        buffer[bytes] = '\0';
                        break; 
                    }

                    //should probably alternate the turns once all of this is done too, teehee
                    pthread_mutex_lock(&lock);
                    if (currentGame->turn == 0) {
                        currentGame->turn = 1;
                    } else {
                        currentGame->turn = 0;
                    }
                    pthread_mutex_unlock(&lock);


// RSGN Indicates that the player has resigned.
    // The server will respond with OVER.

//RSGN -> NULL
                } else if (strcmp("RSGN", current->data) == 0){
                    if ((ingame == 0) || (current->next == NULL) || (current->next->next != NULL)){ // err - game hasn't started || err - resign has too many args                        list = freeRL(list);
                        list = freeRL(list);
                        char *reason = "INVL|16|Invalid command|";
                        write(con->fd, reason, strlen(reason));
                        if (yourFd->ingame == 1){
                            Game *thisGame = findGame(gameList, con->fd);
                            char *whatHappened = "OVER|24|W|Opponent has resigned|";
                            fdList *otherFd;
                            if (con->fd == thisGame->playerOne) {
                                write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerTwo);
                            } else { //con->fd is player Two
                                write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerOne);
                            }
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                            close(otherFd->fileDescriptor);
                        } else { //not in a game
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                        }
                        linePos = 0;
                        buffer[bytes] = '\0';
                        break;
                    } else if (currentGame->draw != 0) { // err - draw was called, what are you doing brother
                        list = freeRL(list);
                        char *reason = "INVL|16|Draw was called|"; ///////////////////////////////////////////////////////////////////////////
                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    }
                    searching = 0;

                    //send the resign function to both file descriptors
                    //we need to know WHO lost exactly

                    int opponentSize;
                    char loserName[85];
                    
                    if (currentGame->playerTwo == con->fd){
                        opponentSize = currentGame->playerTwoSize;
                        strcpy(loserName, currentGame->playerTwoName);
                    } else if (currentGame->playerOne == con->fd){
                        opponentSize = currentGame->playerOneSize;
                        strcpy(loserName, currentGame->playerOneName);
                    }

                    opponentSize = opponentSize + 13;
                    char *gameOver = "OVER|";

                    char lengthOfRest[99];
                    sprintf(lengthOfRest, "%d", opponentSize);
                    
                    char *win = "W|";
                    char *loss = "L|";
                    char *bar = "|";
                    char *hasResigned = " resigned.";

                    char winReason[150];
                    char lossReason[150];

                    strcpy(winReason, gameOver);
                    strcat(winReason, lengthOfRest);
                    strcat(winReason, bar);
                    strcat(winReason, loss);
                    strcat(winReason, loserName);
                    strcat(winReason, hasResigned);
                    strcat(winReason, bar);

                    strcpy(lossReason, gameOver);
                    strcat(lossReason, lengthOfRest);
                    strcat(lossReason, bar);
                    strcat(lossReason, win);
                    strcat(lossReason, loserName);
                    strcat(lossReason, hasResigned);
                    strcat(lossReason, bar);

                    write(con->fd, lossReason, strlen(lossReason));
                    fdList *otherFd;
                    //the message sent to the winner (by default) is slightly different with the W instead of the L
                    if (con->fd == currentGame->playerOne) {
                        write(currentGame->playerTwo, winReason, strlen(winReason));
                        otherFd = searchFileList(currentGame->playerTwo);
                    } else {
                        write(currentGame->playerOne, winReason, strlen(winReason));
                        otherFd = searchFileList(currentGame->playerOne);
                    }

                    pthread_mutex_lock(&lock);
                    yourFd->finished = 1;
                    otherFd->finished = 1;
                                deleteGame(currentGame, gameList);

                    pthread_mutex_unlock(&lock);
                    close(currentGame->playerOne);
                    close(currentGame->playerTwo);
                    ingame = 0;
                    linePos = 0;
                    buffer[bytes] = '\0';
                    break; 



// DRAW Depending on the message, this indicates that the player is suggesting a draw (S), or is
    // accepting (A) or rejecting (R) a draw proposed by their opponent.
    // Note that DRAW A or DRAW R can only be sent in response to receiving a DRAW S from the server.

// DRAW -> 2 -> S -> NULL
                } else if (strcmp("DRAW", current->data) == 0) {
                    if ((ingame == 0) || (current->next == NULL) || (current->next->next == NULL)|| (current->next->next->next != NULL)){ // err - game hasn't started
                        list = freeRL(list);
                        char *reason = "INVL|16|Invalid command|";
                        write(con->fd, reason, strlen(reason));
                        if (yourFd->ingame == 1){
                            Game *thisGame = findGame(gameList, con->fd);
                            char *whatHappened = "OVER|24|W|Opponent has resigned|";
                            fdList *otherFd;
                            if (con->fd == thisGame->playerOne) {
                                write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerTwo);
                            } else { //con->fd is player Two
                                write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerOne);
                            }
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                            close(otherFd->fileDescriptor);
                        } else { //not in a game
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                        }
                        linePos = 0;
                        buffer[bytes] = '\0';
                        break;
                    }
                    searching = 0;
                    if ((currentGame->turn == 1) && (con->fd == currentGame->playerOne)) {
                        list = freeRL(list);
                        char *reason = "INVL|16|Wait your turn!|"; ///////////////////////////////////////////////////////////////////////////
                                              //Wait your turn!
                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    } else if ((currentGame->turn == 0) && (con->fd == currentGame->playerTwo)) {
                        list = freeRL(list);
                        char *reason = "INVL|16|Wait your turn!|"; ///////////////////////////////////////////////////////////////////////////
                                              //Wait your turn!

                        write(con->fd, reason, strlen(reason));
                        linePos = 0;
                        buffer[bytes] = '\0';
                        continue;
                    }

                    //second field
                    current = current->next;
                    //should probably check the length, idk

                    //third field
                    current = current->next;

                    //is draw even being used right?
                    if (strcmp("S", current->data) == 0){
                        if (currentGame->draw == 0) { //draw had not been called yet
                            currentGame->draw = 1;
                            char *reason = "DRAW|2|S|";
                            //sends request to other player
                            if (con->fd == currentGame->playerOne) {
                                write(currentGame->playerTwo, reason, strlen(reason));
                            }
                            else {
                                write(currentGame->playerOne, reason, strlen(reason));
                            }
                            currentGame->olive = con->fd;
                            pthread_mutex_lock(&lock);
                            currentGame->olive = con->fd;
                            if (currentGame->turn == 0) {
                                currentGame->turn = 1;
                            } else {
                                currentGame->turn = 0;
                            }
                            pthread_mutex_unlock(&lock);
                        } else { // error - can't send draw when you have to send either A or R.
                            list = freeRL(list);
                            char *reason = "INVL|20|Draw already called|"; ///////////////////////////////////////////////////////////////////////////
                            write(con->fd, reason, strlen(reason));
                            linePos = 0;
                            buffer[bytes] = '\0';
                            continue;  
                        }
                         //draw had been called
                    } else if (strcmp("A", current->data) == 0 || strcmp("R", current->data) == 0) {
                        //execute draw
                        if (currentGame->draw == 0) { //error - draw had not been called yet
                            char *reason = "INVL|16|Draw not called|"; ///////////////////////////////////////////////////////////////////////////
                            write(con->fd, reason, strlen(reason));
                            linePos = 0;
                            buffer[bytes] = '\0';
                            continue;  
                        } else { //draw had been called
                            char *decision = current->data;
                            if (strcmp("A", decision) == 0) { //the draw was accepted
                                char *reason = "OVER|25|D|A draw has been reached.|";
                                write(con->fd, reason, strlen(reason));
                                fdList *otherFd;
                                if (con->fd == currentGame->playerOne) {
                                    write(currentGame->playerTwo, reason, strlen(reason));
                                    otherFd = searchFileList(currentGame->playerTwo);
                                } else {
                                    write(currentGame->playerOne, reason, strlen(reason));
                                    otherFd = searchFileList(currentGame->playerOne);
                                }
                                pthread_mutex_lock(&lock);
                                yourFd->finished = 1;
                                otherFd->finished = 1;
                                deleteGame(currentGame, gameList);

                                pthread_mutex_unlock(&lock);
                                close(yourFd->fileDescriptor);
                                close(otherFd->fileDescriptor);
                            } else { //the draw was denied
                                char *decision = "DRAW|2|R|";
                                write(currentGame->olive, decision, strlen(decision));
                                pthread_mutex_lock(&lock);
                                currentGame->olive = con->fd;
                                if (currentGame->turn == 0) {
                                    currentGame->turn = 1;
                                } else {
                                    currentGame->turn = 0;
                                }
                                pthread_mutex_unlock(&lock);
                            }
                            currentGame->draw = 0;
                            currentGame->olive = 0;
                        }
                    } else {
                        list = freeRL(list);
                        char *reason = "INVL|16|Invalid command|";
                        write(con->fd, reason, strlen(reason));
                        if (yourFd->ingame == 1){
                            Game *thisGame = findGame(gameList, con->fd);
                            char *whatHappened = "OVER|24|W|Opponent has resigned|";
                            fdList *otherFd;
                            if (con->fd == thisGame->playerOne) {
                                write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerTwo);
                            } else { //con->fd is player Two
                                write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                                otherFd = searchFileList(thisGame->playerOne);
                            }
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            otherFd->finished = 1;
                                deleteGame(thisGame, gameList);

                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                            close(otherFd->fileDescriptor);
                        } else { //not in a game
                            pthread_mutex_lock(&lock);
                            yourFd->finished = 1;
                            pthread_mutex_unlock(&lock);
                            close(yourFd->fileDescriptor);
                        }
                        linePos = 0;
                        buffer[bytes] = '\0';
                        break;
                        linePos = 0;
                        buffer[bytes] = '\0';
                        break;
                    }
                    
                    
//None of these commands - return INVL
                } else { // err - not a valid command
                    list = freeRL(list);
                    char *reason = "INVL|16|Invalid command|";
                    write(con->fd, reason, strlen(reason));
                    if (yourFd->ingame == 1){
                        Game *thisGame = findGame(gameList, con->fd);
                        char *whatHappened = "OVER|24|W|Opponent has resigned|";
                        fdList *otherFd;
                        if (con->fd == thisGame->playerOne) {
                            write(thisGame->playerTwo, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerTwo);
                        } else { //con->fd is player Two
                            write(thisGame->playerOne, whatHappened, strlen(whatHappened));
                            otherFd = searchFileList(thisGame->playerOne);
                        }
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        otherFd->finished = 1;
                        deleteGame(thisGame, gameList);
                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                        close(otherFd->fileDescriptor);
                    } else { //not in a game
                        pthread_mutex_lock(&lock);
                        yourFd->finished = 1;
                        pthread_mutex_unlock(&lock);
                        close(yourFd->fileDescriptor);
                    }
                    linePos = 0;
                    buffer[bytes] = '\0';
                    break;
                }

//////////////////////////////////////////////////////////////

                if (list != NULL){
				    list = freeRL(list);
                }

                // fputs("> ", stderr);


				linePos = 0;
			}
		}
        buffer[bytes] = '\0';
    }
    free(lineBuffer);

    fdList *inQuestion = searchFileList(con->fd);
    if (inQuestion && inQuestion->finished == 1) {
        deleteFd(con->fd, fileDescriptors);
        free(con);
        return NULL;  // Early return to prevent double-free
    } else if (bytes == 0) { //file quit
		printf("[%s:%s] got EOF\n", host, port);
        if (ingame == 0){
            if (inQuestion->finished == 1){ //file quit after game finished
                deleteFd(con->fd, fileDescriptors);
                close(con->fd);
            } else { //file left before game started
                deleteFd(con->fd, fileDescriptors);
                close(con->fd);
                traverseFileDescriptors(fileDescriptors);
            }
        } else if (ingame == 1){ //file quit in-game
            Game *currentGame = findGame(gameList, con->fd);
            if (searching == 1){ //quit while searching for game
                if (inQuestion->ingame == 1){ //if disconnect before making a move
                    char *whatHappened = "OVER|24|W|Opponent disconnected|";
                    if (con->fd == currentGame->playerOne) {
                        printf("%s\n", whatHappened);
                        write(currentGame->playerTwo, whatHappened, strlen(whatHappened));
                        deleteFd(currentGame->playerOne, fileDescriptors);
                        close(currentGame->playerOne);
                    } else { //con->fd is player Two
                        printf("%s\n", whatHappened);
                        write(currentGame->playerOne, whatHappened, strlen(whatHappened));
                        deleteFd(currentGame->playerTwo, fileDescriptors);
                        close(currentGame->playerTwo);
                    }
                    close(con->fd);
                    deleteGame(currentGame, gameList);

                } else { //quit while searching
                    deleteGame(currentGame, gameList);
                    deleteFd(con->fd, fileDescriptors);
                    close(con->fd);
                    traverseGames(gameList);
                    traverseFileDescriptors(fileDescriptors);
                }
            } else { //quit after making a move
                char *whatHappened = "OVER|24|W|Opponent disconnected|";
                if (con->fd == currentGame->playerOne) {
                    printf("%s\n", whatHappened);
                    write(currentGame->playerTwo, whatHappened, strlen(whatHappened));
                    close(currentGame->playerOne);
                    deleteFd(currentGame->playerOne, fileDescriptors);
                } else { //con->fd is player Two
                    printf("%s\n", whatHappened);
                    write(currentGame->playerOne, whatHappened, strlen(whatHappened));
                    close(currentGame->playerTwo);
                    deleteFd(currentGame->playerTwo, fileDescriptors);
                }
                close(con->fd);
                deleteGame(currentGame, gameList);

            }
            
        }
	} else if (bytes == -1) {
		printf("[%s:%s] terminating: %s\n", host, port, strerror(errno));
	} else {
		printf("[%s:%s] terminating\n", host, port);
	}
    
    free(con);
    return NULL;
}

// Added cleanup functions before main()
void cleanup_games(void) {
    struct Game *current = gameList;
    while (current != NULL) {
        struct Game *next = current->next;
        if (current->playerOneName) free(current->playerOneName);
        if (current->playerTwoName) free(current->playerTwoName);
        if (current->grid) free(current->grid);
        free(current);
        current = next;
    }
    gameList = NULL;
}

void cleanup_fds(void) {
    struct fdList *current = fileDescriptors;
    while (current != NULL) {
        struct fdList *next = current->next;
        close(current->fileDescriptor);
        free(current);
        current = next;
    }
    fileDescriptors = NULL;
}

int main(int argc, char **argv){
    sigset_t mask;
    struct connection_data *con;
    int error;
    pthread_t tid;

    char *service = argc == 2 ? argv[1] : argv[2];

    if (argc < 2) { 
        puts("Need an argument for port");
	    exit(EXIT_FAILURE);
    } else if (argc > 2) {
        puts("Too many arguments");
	    exit(EXIT_FAILURE);
    }

	install_handlers(&mask);
	
    int listener = open_listener(service, QUEUE_SIZE);
    if (listener < 0) exit(EXIT_FAILURE);
    
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("\nMutex init has failed\n");
        return 1;
    }

    printf("Listening for incoming connections on %s\n", service);
    while (active) {
    	con = (struct connection_data *)malloc(sizeof(struct connection_data));
    	con->addr_len = sizeof(struct sockaddr_storage);
    
        con->fd = accept(listener, (struct sockaddr *)&con->addr, &con->addr_len);

        if (con->fd < 0) {
            if (!active) {  //check if interrupted by signal
                free(con);
                break;
            }
            perror("accept");
            free(con);
            continue;
        }
        // insert(con->fd, linkedList);

        // temporarily disable signals
        // (the worker thread will inherit this mask, ensuring that SIGINT is
        // only delivered to this thread)
        error = pthread_sigmask(SIG_BLOCK, &mask, NULL);
        if (error != 0) {
        	fprintf(stderr, "sigmask: %s\n", strerror(error));
        	exit(EXIT_FAILURE);
        }
        

        error = pthread_create(&tid, NULL, read_data, con);
        if (error != 0) {
        	fprintf(stderr, "pthread_create: %s\n", strerror(error));
        	close(con->fd);
        	free(con);
        	continue;
        }
        
        // automatically clean up child threads once they terminate
        pthread_detach(tid);
        
        // unblock handled signals
        error = pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
        if (error != 0) {
        	fprintf(stderr, "sigmask: %s\n", strerror(error));
        	exit(EXIT_FAILURE);
        }
    }

    puts("Shutting down");
    
    // Fixed cleanup before exit
    cleanup_games();
    cleanup_fds();
    
    // Destroy the mutex
    pthread_mutex_destroy(&lock);
    
    close(listener);
    
    // returning from main() (or calling exit()) immediately terminates all
    // remaining threads

    // to allow threads to run to completion, we can terminate the primary thread
    // without calling exit() or returning from main:
    //   pthread_exit(NULL);
    // child threads will terminate once they check the value of active, but
    // there is a risk that read() will block indefinitely, preventing the
    // thread (and process) from terminating
    
	// to get a timely shut-down of all threads, including those blocked by
	// read(), we will could maintain a global list of all active thread IDs
	// and use pthread_cancel() or pthread_kill() to wake each one

    return EXIT_SUCCESS;
}
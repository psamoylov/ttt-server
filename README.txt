(Should anything have a [] in it, that means there is a lock in use)
(If not explicitly specified, if an edge case occurs, it just sends invalid command to that client and cancels the command)

*insertFdList[]:
    Sub is calloc'd and it initializes a fdlist by setting the fd to fileDescriptor and every other value to 0. Those
    values with be messed with later. If no fdList was made yet, it sets the head to the sub. Otherwise, it looks for
    the next NULL spot and sets that to the sub.

*insertGame[]:
    This does one of 3 things. If current is NULL, meaning there's no game that exists yet, then it makes a game.
    It starts by callocing sub to be used for initialization purposes, sets the game number to the current game
    and then increments it. The fd is set to be player1 (for identification purposes), the name is set to what
    was given, and the namesize given becomes playerOneSize. player2 is set to 0 as well as draw (because it can't
    be called before a game is even made) and olive which exists purely for DRAW (The naming scheme behind olive was
    an olive branch since that play is asking for a draw and bringing peace to both players, heehee). The grid is made
    entirely composed of periods and a null space. It sets the turn to 0 (player 1 has to go first), sets the grid to be
    the one for the game and creates space for another game and sets it to NULL. The fd's "start" is set to 1 to show that
    it's starting up a game, head->next is set to the sub (this game) and the head is returned.
    If it sees that there isn't a player2 (meaning that player2 == 0), it will register player2's info (their fd, name and
    nameSize). Afterwards, it will send both players BEGN, which has their mark and it shows the name of their opponent. It
    will then set their ingames to 1 to say their in a game and start values to 0 as a game has started.
    If the head isn't null and a player 2 exists for the game it is looking at, then a whole new game will be created, going
    through the process in the first paragraph.

findGame:
    Looks for the game where the fd specified is. If it finds and fd in one of the games equal to player1 or 2's
    fd, it returns the game they're in, otherwise it returns NULL, signaling that the fd isn't in any existing games.

findDuplicateName:
    Checks to see if there are two of the same name in a game. If there is, it returns 1. This checks player1
    everytime it's run, but will only check player2 if player 2 exists.

deleteGame[]:
    Deletes a game from the list of games in order to conserve memory. This is done whenever a game concludes
    for whatever reason.

*read_data[]:
    It takes any input from the clients and compares it to the commands. If there's a match it runs
    that command, which are all listed below in all caps. 
    Should the client's input not match any commands perfectly, it kicks them out of the server. 
    It also checks for when either client leaves without a command. 
    If that happens, either the game is terminated, 
        or that client is just removed if it happened while they were looking for a game, 
            OR if they never were in a game to begin with.

PLAY:
    For PLAY, we make sure that a game isn't already running, we ensure that the parameters
    exist (is there a length and a name), and we make sure it doesn't have too many arguments.
    Any of these being caught will prevent the command from working. This check at the beginning
    is standard for the other 3 commands. Then the name is checked for it's length, as it should
    be at most 50 characters, and then if there's a duplicate name, as that is not allowed. Once
    this is done, searching and ingame are set to 1. Searching is 1 so it's clarified that that fd
    is looking for a game while ingame is 1 mainly to ensure PLAY isn't called again. The client is
    sent "WAIT|0|" and it either creates a game that waits for another player, or it joins a game
    that already has someone waiting in it.

MOVE[]:
    Same checks as before, but there's now an extra one to make sure that DRAW wasn't called. This
    is because no other commands should work after DRAW was called. This check wasn't needed for PLAY
    because of ingame. The other specifics for DRAW will be explained later. It will then check if a
    mark is input and that it is one of the valid marks (X or O). Once that is done, it checks to make
    sure that player 1 is the one using X and player 2 is the one using O. Then it checks the coordinates
    of the next move, making sure that they were input properly (number then comma then number) and then
    the numbers are taken and checked to see if they are between 1 and 3 inclusive to avoid out of bounds
    issues. After that check it has to make sure the move is made in the correct spot. The first number
    given determines the row, so it will check that number and place it in the target row immediately
    by setting a variable called sum to a specific number. Then it takes the second number, subtracts it
    by 1 (again, to avoid bounds issues) and adds the result to sum, and that spot is checked to see if it's empty.
    (ex: if the first number is 3, then the sum, which is where the mark will ultimately go, starts at 6,
    so the only possible values it can be are 6, 7, and 8.)
    0 1 2
    3 4 5
    6 7 8
    Above is how the board should be visualized for this part.
    Upon confirmation no other mark is there, the move is made and MOVD with the grid is sent to both players
    to show where the move was. Then it checks to see if the game is won with checkForWin. If it finds that there
    is a win, the game sends OVER to both players, specifices who won and lost with W and L and says the name of
    the winner followed by " has won." If the game hasn't been won, it'll check to see if any moves can be made with
    checkForDraw, and if THAT succeeds, then it sends OVER to both, gives them D to say that there's a draw
    and clarifies that no more moves can be made. If neither of those conditions are fulfilled it will alternate the 
    turns as the game is still going.

RSGN:
    This has the least checks out of all of them since the command is the simplest of the 4. It only checks if draw had been
    called and if there are too many arguments, to which it replies with invalid command. 
    But after that, it takes the player's name who resigned and sends OVER to both players stating that that player resigned. 
    The game is then closed and deleted exactly how it's done in MOVE.

DRAW[]:
    DRAW is special as it has to be called twice in order for it to be fulfilled. It makes sure none of the checks are empty, 
    then it what was given, S, A or R. If S was given, it makes sure that draw wasn't called before as the only commands that 
    can be accepted after draw was called is DRAW|2|A and DRAW|2|R. Once it has determined that this is the first time draw was
    called (at least before A and R become concerns) it sends the request to draw to the other player and alternates the turns.
    Then it sets the current game's draw to 1, which deactivates the other commands until DRAW|2|A| or DRAW|2|R| is recieved. When
    it gets either of those, it checks if draw WAS called (because if it wasn't then it won't work since letting it work would 
    allow the player whose turn it is to draw the game whenever they want.). After it determines draw was called, it checks which
    of the 2 replies was recieved. If it gets R, then it sends DRAW|2|R| to the player who sent the request and alternates the turns
    again. If it gets A, it ends the game, sends both players OVER, states that it's a draw and says specifically "A draw has been
    reached.". It closes the fds and deletes the game all the same here.

*main:
    Takes an argument manually: the port number.
    ./ttts - Need an "argument for port"
    ./ttts 15000 will call main and work as intended - "Listening for incoming connections on 15000"
    ./ttts 15000 15000 - "Too many arguments"
    Main installs the handlers, sets up the server, and initializes the lock (in order to prevent any shenanigans 
    with the global variables updating incorrectly), while making sure nothing went wrong with the lock itself initiating. It listens
    for clients and everytime a client is found, it creates and joins a thread for them. From there, everything is handled in read_data.
    But once the server is ready to shut down, it detaches all the thread ids, it destroys the lock and it states that it is shutting 
    down. Then the listener is closed and the program exits.
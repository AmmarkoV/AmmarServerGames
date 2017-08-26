#ifndef ENGINE_H_INCLUDED
#define ENGINE_H_INCLUDED


struct gameSession
{
    int started;
    int playersJoined;
    int playerAWantsToStop;
    int playerBWantsToStop;

    char playerA[256];
    char playerB[256];
    char playerAName[256];
    char playerBName[256];

    int activePlayer;

    int width;
    int height;
    int board[10*10];
};


extern struct gameSession games[100];


int initializeGame(struct gameSession * g , unsigned int id);

int playerJoinedGame(struct gameSession * g , char *  playerName , unsigned int gameID);

int isItTurnOfPlayerToMakeMove(struct gameSession * g , char * playerName , unsigned int gameID);

#endif // ENGINE_H_INCLUDED

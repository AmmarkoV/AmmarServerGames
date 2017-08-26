#include "engine.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

struct gameSession games[100]={0};

int initializeGame(struct gameSession * g , unsigned int id)
{
  g[id].width=10;
  g[id].height=10;
  g[id].activePlayer=1;
  return 1;
}


int playerJoinedGame(struct gameSession * g , char * playerName , unsigned int gameID)
{
   if (strcmp(playerName,g[gameID].playerA)==0)
   {
     //Already Joined as player A ..
     return 1;
   } else
   if (strcmp(playerName,g[gameID].playerB)==0)
   {
     //Already Joined as player B ..
     return 1;
   } else
   {
     //Now joining for first time..
     if
        (
          (g[gameID].playerA[0]!=0) &&
          (g[gameID].playerB[0]!=0)
         )
     {
        //Game is full
        return 0;
     } else
     {
       if (g[gameID].playerA[0]==0)
           {
             ++g[gameID].playersJoined;
             snprintf(g[gameID].playerA,128,"%s",playerName);
             return 1;
           } else
       if (g[gameID].playerB[0]==0)
           {
             ++g[gameID].playersJoined;
             snprintf(g[gameID].playerB,128,"%s",playerName);
             return 1;
           }

     }
   }

 return 0;
}


int isItTurnOfPlayerToMakeMove(struct gameSession * g , char * playerName , unsigned int gameID)
{
   if (strcmp(playerName,g[gameID].playerA)==0)
   {
     if (g[gameID].activePlayer==1) { return 1; }
   } else
   if (strcmp(playerName,g[gameID].playerB)==0)
   {
     if (g[gameID].activePlayer==2) { return 1; }
   }

  return 0;
}

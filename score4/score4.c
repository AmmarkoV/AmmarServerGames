/*
AmmarServer , main executable

URLs: http://ammar.gr
Written by Ammar Qammaz a.k.a. AmmarkoV 2012

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../../AmmServerlib/AmmServerlib.h"

#include "engine.h"



#define logEcho() fprintf(stderr," Reached %s , %u \n ", __FILE__, __LINE__);


#define MAX_BINDING_PORT 65534
#define DEFAULT_BINDING_PORT 8080  // <--- Change this to 80 if you want to bind to the default http port..!

char webserver_root[MAX_FILE_PATH]="src/Games/score4/res/";
char templates_root[MAX_FILE_PATH]="public_html/templates/";

struct AmmServer_MemoryHandler * viewGameHTML=0;
//The decleration of some dynamic content resources..
struct AmmServer_Instance  * default_server=0;


struct AmmServer_RH_Context indexPage={0};
struct AmmServer_RH_Context viewGame={0};
struct AmmServer_RH_Context prepareGame={0};
struct AmmServer_RH_Context findGame={0};
struct AmmServer_RH_Context makeAMove={0};


void * pleaseLogin(struct AmmServer_DynamicRequest  * rqst)
{
    rqst->content[0]=0;
    strcat(rqst->content,"<html>\n<body>\n");
    strcat(rqst->content,"Please login\n");
    strcat(rqst->content,"</body>\n</html>\n");
    rqst->contentSize=strlen(rqst->content);
}

void * wrongGame(struct AmmServer_DynamicRequest  * rqst)
{
    rqst->content[0]=0;
    strcat(rqst->content,"<html>\n<body>\n");
    strcat(rqst->content,"Wrong Game Request\n");
    strcat(rqst->content,"</body>\n</html>\n");
    rqst->contentSize=strlen(rqst->content);
}

void * waitForGameToStart(struct AmmServer_DynamicRequest  * rqst)
{

  char playerID[128]={0};
  char tableID[128]={0};

  if (
        ( _GET(default_server,rqst,"p",playerID,128) ) &&
        ( _GET(default_server,rqst,"g",tableID,128) )
     )
  {
    unsigned int gameID=atoi(tableID);

    playerJoinedGame(games, playerID , gameID);

    rqst->content[0]=0;
    char buf[256];
    strcat(rqst->content,"<html>\n");
    strcat(rqst->content,"<head>\n");
    strcat(rqst->content,"<meta http-equiv=\"refresh\" content=\"5; url=");
    snprintf(buf,256,"view.html?p=%s&g=%s&t=%u",playerID,tableID,rand()%10000);
    strcat(rqst->content,buf);
    strcat(rqst->content,"\"></head><body>\n");
    strcat(rqst->content,"Waiting for Game to Start\n");


    snprintf(buf,256,"<br> %u players joined<br>",games[gameID].playersJoined);
    strcat(rqst->content,buf);

    strcat(rqst->content,"</body>\n</html>\n");
    rqst->contentSize=strlen(rqst->content);
  } else
  {
    pleaseLogin(rqst);
  }
 return 0;
}

void * find_game_callback(struct AmmServer_DynamicRequest  * rqst)
{
   rqst->content[0]=0;
  char playerID[128]={0};
 if ( _GET(default_server,rqst,"p",playerID,128) )
   {
   strcat(rqst->content,"<html>\n<body>\n");
   int i=0;
   int emptyGamesShown=0;
   for (i=0; i<50; i++)
   {
       char buf[256];
       buf[0]=0;

       if (games[i].started)
       {
         snprintf(buf,256,"<a href=\"view.html?p=%s&g=%u\">See Game %u ( %s vs %s )</a><br>",playerID,i,i,games[i].playerAName,games[i].playerBName );
       } else
       {
         if (games[i].playersJoined>0)
         {
          snprintf(buf,256,"<a href=\"view.html?p=%s&g=%u\">Join New Game %u ( %s vs %s )</a><br>",playerID,i,i,games[i].playerAName,games[i].playerBName);
         } else
         if ( (emptyGamesShown==0) && (games[i].playersJoined==0) )
         {
          ++emptyGamesShown;
          snprintf(buf,256,"<a href=\"view.html?p=%s&g=%u\">Join New Game %u</a><br>",playerID,i,i,games[i].playerAName,games[i].playerBName);
         }
       }

       if (buf[0]!=0)
         { strcat(rqst->content,buf); }
       buf[0]=0;
   }
   strcat(rqst->content,"</body>\n</html>\n");
   rqst->contentSize=strlen(rqst->content);
   } else
   {
     pleaseLogin(rqst);
   }

 return 0;
}






void * prepare_game_callback(struct AmmServer_DynamicRequest  * rqst)
{
  char playerAID[128]={0};
  char playerBID[128]={0};
  char tableID[128]={0};
  if ( _GET(default_server,rqst,"pA",playerAID,128) )
     {
     }
  if ( _GET(default_server,rqst,"pB",playerBID,128) )
     {
     }
  if ( _GET(default_server,rqst,"g",tableID,128) )
     {
     }
  return 0;
}

//This function prepares the content of  stats context , ( stats.content )
void * prepare_view_content_callback(struct AmmServer_DynamicRequest  * rqst)
{
  char playerID[128]={0};
  char tableID[128]={0};
  unsigned int gameID;

  if (
        ( _GET(default_server,rqst,"p",playerID,128) ) &&
        ( _GET(default_server,rqst,"g",tableID,128) )
     )
  {
   gameID=atoi(tableID);

   if (gameID>50)
   {
       wrongGame(rqst);
   } else
   {

    if ( games[gameID].playersJoined<2 )
    {
       waitForGameToStart(rqst);
    } else
    {
   unsigned int id=0;
   time_t t = time(NULL);
   struct tm tm = *localtime(&t);
   char buf[4096];

   struct AmmServer_MemoryHandler * viewGameHTMLWriteable = AmmServer_CopyMemoryHandler(viewGameHTML);

   snprintf(buf,4096,"view.html?p=%s&g=%s&t=%u",playerID,tableID,rand()%10000);
   AmmServer_ReplaceAllVarsInMemoryHandler(viewGameHTMLWriteable,1,"$REFRESH_LINK_FOR_PARTICULAR_PERSON$", buf);

   char * boardRendering = (char*) malloc(sizeof(char) * 149000);
   if (boardRendering!=0)
   {
    char * boardRenderingPTR = boardRendering;   *boardRenderingPTR=0;

    strcat(boardRendering,"  <table width=\"1000\" border=1>\n");
    unsigned int x,y,i=0;
    for (y=0; y<games[id].height; y++)
    {
      strcat(boardRendering,"   <tr>\n");
      for (x=0; x<games[id].width; x++)
      {
         strcat(boardRendering,"     <td width=90>\n");
          if ( games[id].board[i]==1 ) { snprintf(buf,4096,"      <img src=\"textures/circle.png\" width=90>\n",i); } else
          if ( games[id].board[i]==2 ) { snprintf(buf,4096,"      <img src=\"textures/x.png\" width=90>\n",i);      } else
                                       {
                                         snprintf(buf,4096,"      <a href=\"makeMove.html?p=%s&g=%s&s=%u&t=%02d-%02d-%02d_%02d:%02d:%02d\"><img src=\"textures/empty.png\" width=90></a>\n",playerID,tableID,i,
                                         tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900,   tm.tm_hour, tm.tm_min, tm.tm_sec);
                                       }

         strcat(boardRendering,buf);
         strcat(boardRendering,"     </td>\n");
         ++i;
      }
      strcat(boardRendering,"   </td>\n");
    }
    strcat(boardRendering,"  </table>\n");


    AmmServer_ReplaceAllVarsInMemoryHandler(viewGameHTMLWriteable,1,"$BOARD$", boardRendering);

    free(boardRendering);

    snprintf(rqst->content,rqst->MAXcontentSize,"%s", viewGameHTMLWriteable->content );
    rqst->contentSize=strlen(rqst->content);
    AmmServer_FreeMemoryHandler(&viewGameHTMLWriteable);
    }
   }
   }

  } else
  {
     pleaseLogin(rqst);
  }

  return 0;
}



void * prepare_move_content_callback(struct AmmServer_DynamicRequest  * rqst)
{
  unsigned int id=0;
  char moveRequested[128]={0};
  unsigned int move=1000000;



  char playerID[128]={0};
  char tableID[128]={0};
  unsigned int gameID;

  if (
        ( _GET(default_server,rqst,"p",playerID,128) ) &&
        ( _GET(default_server,rqst,"g",tableID,128) )
     )
  {
   gameID=atoi(tableID);

   if (isItTurnOfPlayerToMakeMove(games, playerID, gameID) )
   {
    if ( _GET(default_server,rqst,"s",moveRequested,128) )
     {
      fprintf(stderr,"Requested move %s for player %u\n",moveRequested,games[id].activePlayer);
      move=atoi(moveRequested);


      if (move<games[id].width*games[id].height)
      {
        if (games[id].board[move]==0)
         {
           games[id].board[move]=games[id].activePlayer;

           if (games[id].activePlayer==1) { games[id].activePlayer=2; } else
           if (games[id].activePlayer==2) { games[id].activePlayer=1; }
         }
      }
     }
   }
  }
  return prepare_view_content_callback(rqst);
}



//This function adds a Resource Handler for the pages stats.html and formtest.html and associates stats , form and their callback functions
void init_dynamic_content()
{

  if (! AmmServer_AddResourceHandler(default_server,&viewGame,"/view.html",webserver_root,150000,0,&prepare_view_content_callback,DIFFERENT_PAGE_FOR_EACH_CLIENT) )       { AmmServer_Warning("Failed adding stats page\n"); }
  if (! AmmServer_AddResourceHandler(default_server,&makeAMove,"/makeMove.html",webserver_root,150000,0,&prepare_move_content_callback,DIFFERENT_PAGE_FOR_EACH_CLIENT) )  { AmmServer_Warning("Failed adding stats page\n"); }
  if (! AmmServer_AddResourceHandler(default_server,&prepareGame,"/prepareGame.html",webserver_root,45000,0,&prepare_game_callback,DIFFERENT_PAGE_FOR_EACH_CLIENT) )      { AmmServer_Warning("Failed adding stats page\n"); }
  if (! AmmServer_AddResourceHandler(default_server,&findGame,"/index.html",webserver_root,145000,0,&find_game_callback,DIFFERENT_PAGE_FOR_EACH_CLIENT) )                  { AmmServer_Warning("Failed adding stats page\n"); }



  AmmServer_DoNOTCacheResourceHandler(default_server,&viewGame);
  AmmServer_DoNOTCacheResourceHandler(default_server,&indexPage);
  AmmServer_DoNOTCacheResourceHandler(default_server,&makeAMove);


  viewGameHTML=AmmServer_ReadFileToMemoryHandler("src/Games/score4/res/board.html");

  initializeGame(games,0);

}

//This function destroys all Resource Handlers and free's all allocated memory..!
void close_dynamic_content()
{
    AmmServer_RemoveResourceHandler(default_server,&viewGame,1);
    AmmServer_RemoveResourceHandler(default_server,&indexPage,1);
    AmmServer_RemoveResourceHandler(default_server,&makeAMove,1);


    AmmServer_FreeMemoryHandler(&viewGameHTML);
}
/*! Dynamic content code ..! END ------------------------*/




int main(int argc, char *argv[])
{
    printf("\nAmmar Server %s starting up..\n",AmmServer_Version());
   //If we have a command line arguments we overwrite our buffers

    //Check binary and header spec
    AmmServer_CheckIfHeaderBinaryAreTheSame(AMMAR_SERVER_HTTP_HEADER_SPEC);
    //Register termination signal for when we receive SIGKILL etc
    AmmServer_RegisterTerminationSignal(&close_dynamic_content);

    char bindIP[MAX_IP_STRING_SIZE];
    strncpy(bindIP,"0.0.0.0",MAX_IP_STRING_SIZE);

    unsigned int port=DEFAULT_BINDING_PORT;


    default_server = AmmServer_StartWithArgs(
                                             "ammarserver",
                                              argc,argv , //The internal server will use the arguments to change settings
                                              //If you don't want this look at the AmmServer_Start call
                                              bindIP,
                                              port,
                                              0, /*This means we don't want a specific configuration file*/
                                              webserver_root,
                                              templates_root
                                              );

    if (!default_server) { AmmServer_Error("Could not start server , shutting down everything.."); exit(1); }

    //Create dynamic content allocations and associate context to the correct files
    init_dynamic_content();
    //stats.html and formtest.html should be availiable from now on..!


         while ( (AmmServer_Running(default_server))  )
           {
             //Main thread should just sleep and let the background threads do the hard work..!
             //In other applications the programmer could use the main thread to do anything he likes..
             //The only caveat is that he would takeup more CPU time from the server and that he would have to poll
             //the AmmServer_Running() call once in a while to make sure everything is in order
             //usleep(60000);
             sleep(1);
           }


    //Delete dynamic content allocations and remove stats.html and formtest.html from the server
    close_dynamic_content();

    //Stop the server and clean state
    AmmServer_Stop(default_server);
    AmmServer_Warning("Ammar Server stopped\n");

    return 0;
}

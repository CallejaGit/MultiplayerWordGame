#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>

#include "socket.h"
#include "gameplay.h"


#ifndef PORT
    #define PORT 54855 
#endif
#define MAX_QUEUE 5


void add_player(struct client **top, int fd, struct in_addr addr);
void remove_player(struct client **top, int fd);

/* These are some of the function prototypes that we used in our solution 
 * You are not required to write functions that match these prototypes, but
 * you may find the helpful when thinking about operations in your program.
 */
/* Send the message in outbuf to all clients */
void broadcast(struct game_state *game, char *outbuf);
void announce_turn(struct game_state *game);
void announce_winner(struct game_state *game, struct client *winner);
/* Move the has_next_turn pointer to the next active client */
//void advance_turn(struct game_state *game);


/* The set of socket descriptors for select to monitor.
 * This is a global variable because we need to remove socket descriptors
 * from allset when a write to a socket fails.
 */
fd_set allset;

void advance_turn(struct game_state *game, struct client **curr) {
    if (game->has_next_turn != NULL) {
        // first game...probably
        if (*curr == NULL) {
            *curr = game->has_next_turn;
            announce_turn(game);
            char msg[MAX_MSG];
            strcpy(msg, status_message(msg, game));
        write(game->has_next_turn->fd, "What is your guess?\r\n", 21);
        } else if (strcmp((*curr)->name, game->has_next_turn->name) != 0) {
            
            announce_turn(game);
            char msg[MAX_MSG];
            strcpy(msg, status_message(msg, game));
printf("%s\n", msg);
        write(game->has_next_turn->fd, "What is your guess?\r\n", 21);
        }
    }
    
    
    // get status_message
    // broadcast status_message
}
void announce_turn(struct game_state *game) {
    
    char *turn_msg = TURN_MSG;
    char *broadcast_turn_msg = malloc(strlen(game->has_next_turn->name) + strlen(turn_msg) + 2);
    strcpy(broadcast_turn_msg, game->has_next_turn->name);
    strcat(broadcast_turn_msg, turn_msg);
    strncat(broadcast_turn_msg, "\r\n", 2);
    broadcast(game, broadcast_turn_msg);
    
}
/** Removes client from new players to active players
 */
void become_active(struct game_state *game, struct client **top, int fd, char* buf) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Client %d %s is an active player\n", fd, inet_ntoa((*p)->ipaddr));
        add_player(&game->head, fd, (*p)->ipaddr); 

        // first player gets first turn
        if (game->has_next_turn == NULL) {
           game->has_next_turn = game->head;
        } 
        // set up client for active player
        strncpy(game->head->name, buf, MAX_NAME);
printf("%s will be the next turn\n", game->has_next_turn->name);
        char *enter_notice = ENTERED_BROADMSG;
        char *msg = malloc(strlen(game->head->name) + strlen(enter_notice) + 2);
        strcpy(msg, game->head->name);
        strcat(msg, enter_notice);
        strncat(msg, "\r\n", 2);
        broadcast(game, msg);

        free(msg); 
        free(*p);
        *p = t;
    }
}

void broadcast(struct game_state *game, char *outbuf) {
 
    
    struct client * b = game->head;
    while(b!=NULL) {
printf("attempting to write to %s at fd %d\n", b->name, b->fd);
        if(write(b->fd, outbuf, strlen(outbuf)) == -1) {
            fprintf(stderr, "Write to client %s failed\n", inet_ntoa(b->ipaddr));
            remove_player(&(game->head), b->fd);
        }
        b = b->next;
    }
    
        
}
/* Add a client to the head of the linked list
 */
void add_player(struct client **top, int fd, struct in_addr addr) {
    struct client *p = malloc(sizeof(struct client));

    if (!p) {
        perror("malloc");
        exit(1);
    }

    printf("Adding client %s\n", inet_ntoa(addr));

    p->fd = fd;
    p->ipaddr = addr;
    p->name[0] = '\0';
    p->in_ptr = p->inbuf;
    p->inbuf[0] = '\0';
    p->next = *top;
    *top = p;
}

/* Removes client from the linked list and closes its socket.
 * Also removes socket descriptor from allset 
 */
void remove_player(struct client **top, int fd) {
    struct client **p;

    for (p = top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    // Now, p points to (1) top, or (2) a pointer to another client
    // This avoids a special case for removing the head of the list
    if (*p) {
        struct client *t = (*p)->next;
        printf("Removing client %d %s\n", fd, inet_ntoa((*p)->ipaddr));
        FD_CLR((*p)->fd, &allset);
        close((*p)->fd);
        free(*p);
        *p = t;
    } else {
        fprintf(stderr, "Trying to remove fd %d, but I don't know about it\n",
                 fd);
    }
}


int main(int argc, char **argv) {
    int clientfd, maxfd, nready;
    struct client *p;
    struct sockaddr_in q;
    fd_set rset;
    
    if(argc != 2){
        fprintf(stderr,"Usage: %s <dictionary filename>\n", argv[0]);
        exit(1);
    }
    
    // Create and initialize the game state
    struct game_state game;

    srandom((unsigned int)time(NULL));
    // Set up the file pointer outside of init_game because we want to 
    // just rewind the file when we need to pick a new word
    game.dict.fp = NULL;
    game.dict.size = get_file_length(argv[1]);

    init_game(&game, argv[1]);
    
    // head and has_next_turn also don't change when a subsequent game is
    // started so we initialize them here.
    game.head = NULL;
    game.has_next_turn = NULL;
    
    /* A list of client who have not yet entered their name.  This list is
     * kept separate from the list of active players in the game, because
     * until the new playrs have entered a name, they should not have a turn
     * or receive broadcast messages.  In other words, they can't play until
     * they have a name.
     */
    struct client *new_players = NULL;
    
    struct sockaddr_in *server = init_server_addr(PORT);
    int listenfd = set_up_server_socket(server, MAX_QUEUE);
    
    // initialize allset and add listenfd to the
    // set of file descriptors passed into select
    FD_ZERO(&allset);
    FD_SET(listenfd, &allset);
    // maxfd identifies how far into the set to search
    maxfd = listenfd;
    
    struct client *cur = NULL;
    
    while (1) {
        advance_turn(&game, &cur);
        // make a copy of the set before we pass it into select
        rset = allset;
        nready = select(maxfd + 1, &rset, NULL, NULL, NULL);
        if (nready == -1) {
            perror("select");
            continue;
        }     
        
        if (FD_ISSET(listenfd, &rset)){
            printf("A new client is connecting\n");
            clientfd = accept_connection(listenfd);

            FD_SET(clientfd, &allset);
            if (clientfd > maxfd) {
                maxfd = clientfd;
            }
            printf("Connection from %s\n", inet_ntoa(q.sin_addr));
            add_player(&new_players, clientfd, q.sin_addr);
            char *greeting = WELCOME_MSG;
            if(write(clientfd, greeting, strlen(greeting)) == -1) {
                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                remove_player(&(game.head), p->fd);
            };
        }
        
        /* Check which other socket descriptors have something ready to read.
         * The reason we iterate over the rset descriptors at the top level and
         * search through the two lists of clients each time is that it is
         * possible that a client will be removed in the middle of one of the
         * operations. This is also why we call break after handling the input.
         * If a client has been removed the loop variables may not longer be 
         * valid.
         */
        int cur_fd;
        for(cur_fd = 0; cur_fd <= maxfd; cur_fd++) {
            if(FD_ISSET(cur_fd, &rset)) {
                // Check if this socket descriptor is an active player
                for(p = game.head; p != NULL; p = p->next) {
                    if(cur_fd == p->fd) {
                        //TODO - handle input from an active client
                        char buf[MAX_BUF];
                        int nread = 0;
                        if ((nread = read(cur_fd, buf, MAX_BUF)) < 0) {
                            perror("read");
                            exit(1);
                        } else if (nread == 0) {
                            // connection is closed. Need to remove from
                            // the linklist of active players
printf("signaleed\n");
                            remove_player(&game.head, cur_fd);
                        } else if(game.has_next_turn !=NULL && strcmp(game.has_next_turn->name, p->name) == 0) {
printf("signal to %s that it's their turn\n", p->name); 
                            char buf[MAX_BUF];
                            read(p->fd, buf, MAX_BUF);
                            for(int i = 0; i<MAX_BUF; i++) {
                                if(buf[i] == '\r'){ 
                                    buf[i] = '\0';
                                    memset(&buf[i+1], 0, MAX_BUF - strlen(buf));
                                } 
                            }
printf("my guy guess %s\n", buf);


                        }
                        
                         
                        
                        break;
                    }
                }
        
                // Check if any new players are entering their names
                for(p = new_players; p != NULL; p = p->next) {
                    if(cur_fd == p->fd) {
                        // TODO - handle input from an new client who has
                        // not entered an acceptable name.
                        char buf[MAX_BUF];
                        memset(buf, 0, MAX_BUF);
                        int nread = 0;
                        if ( (nread = read(cur_fd, buf, MAX_BUF)) < 0) {
                            perror("read");
                            exit(1);
                        } else if (nread == 0) {  //TCP connection is closed
                            remove_player(&new_players, cur_fd); 

                        }  else if (nread == 2) { // empty string is entered
                            char *msg = STRING_MSG;
printf("empry\n");
                            if(write(cur_fd, msg, strlen(msg)) == -1) {
                                fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                                remove_player(&new_players, p->fd);
                            }
                        } else {
                            int valid = 1;
                            for(int i = 0; i < MAX_NAME; i++) {
                                if (buf[i] == '\r'){
                                    buf[i] = '\0';
                                    memset(&buf[i+1], 0, MAX_NAME - strlen(buf));
                                    break;
                                }
                            }
                            if (strlen(buf)>29) {
                                char *msg = STRING_MSG;
                                if(write(cur_fd, msg, strlen(msg)) == -1) {
                                    fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                                    remove_player(&new_players, p->fd);
                                }

                                valid = 0;
                            }
                            struct client * a = game.head;
                            while (a!=NULL) {
                                if (strcmp(a->name, buf)==0){
                                    valid = 0;

                                    char *msg = REGISTERED_MSG;
                                    if(write(cur_fd, msg, strlen(msg)) == -1) {
                                        fprintf(stderr, "Write to client %s failed\n", inet_ntoa(q.sin_addr));
                                        remove_player(&new_players, p->fd);
                                    }
                                }
                                a = a->next;
                            }
                            if (valid) { 
                                become_active(&game, &new_players, cur_fd, buf);
                                
                            } 
                            break;
                        }
                        
                        break;
                    } 
                }
            }
        }
    }
    return 0;
}



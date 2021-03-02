#include <stdio.h>
#include <stdlib.h>
/* You will to add includes here */
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <regex.h>
#include <pthread.h>

static int uid = 10;

void printIpAddr(struct sockaddr_in addr)
{
  printf("%d.%d.%d.%d",
         addr.sin_addr.s_addr & 0xff,
         (addr.sin_addr.s_addr & 0xff00) >> 8,
         (addr.sin_addr.s_addr & 0xff0000) >> 16,
         (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

typedef struct
{
  struct sockaddr_in address;
  int clientSock;
  int uid;
  char message[255];
  char name[12];
} clientDetails;

typedef struct
{
  clientDetails *array;
  size_t used;
  size_t size;
} Array;

Array clients;

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

void initArray(Array *arr, size_t initialSize)
{
  arr->array = malloc(initialSize * sizeof(clientDetails));
  arr->used = 0;
  arr->size = initialSize;
}

void insertClient(Array *arr, clientDetails client)
{
  pthread_mutex_lock(&clients_mutex);
  if (arr->used == arr->size)
  {
    arr->size += 5;
    arr->array = realloc(arr->array, arr->size * sizeof(clientDetails));
  }
  arr->array[arr->used++] = client;

  pthread_mutex_unlock(&clients_mutex);
}

void removeClient(Array *arr, int uid)
{
  pthread_mutex_lock(&clients_mutex);

  for (int i = 0; i < arr->used; i++)
  {
    if (arr->array[i].uid == uid)
    {
      for (int j = i; j < (arr->used - 1); j++)
      {
        arr->array[j] = arr->array[j + 1];
      }
      arr->used--;
    }
  }

  pthread_mutex_unlock(&clients_mutex);
}

void freeArray(Array *arr)
{
  pthread_mutex_lock(&clients_mutex);
  free(arr->array);
  arr->array = NULL;
  arr->used = arr->size = 0;
  pthread_mutex_unlock(&clients_mutex);
}

void sendMessage(char *message, int uid)
{
  pthread_mutex_lock(&clients_mutex);

  int message_len = strlen(message);

  printf("Message: %s", message);

  for (int i = 0; i < clients.used; i++)
  {
    if (clients.array[i].uid != uid)
    {
      if (write(clients.array[i].clientSock, message, message_len) == -1)
      {
        printf("Write failed!\n");
        break;
      }
    }
  }

  pthread_mutex_unlock(&clients_mutex);
}

void *handle_client(void* arg)
{
  char protocol[20];
  char okOrError[20];
  memset(protocol, 0, 20);
  strcpy(protocol, "Hello 1\n");
  int leave_flag = 0;

  clientDetails *currentClient = (clientDetails *)arg;

  //Send Protocol
  if ((send(currentClient->clientSock, &protocol, sizeof(protocol), 0)) == -1)
  {
    printf("Send failed!\n");
    leave_flag = 1;
  }

  //Recieve nickname and check if it's good or not
  if (recv(currentClient->clientSock, &currentClient->name, sizeof(currentClient->name), 0) == -1)
  {
    printf("Recieve failed!\n");
  }
  else
  {
    /* This is to test nicknames */
    char *expression = "^[A-Za-z_]+$";
    regex_t regularexpression;
    int reti;

    reti = regcomp(&regularexpression, expression, REG_EXTENDED);
    if (reti)
    {
      fprintf(stderr, "Could not compile regex.\n");
      exit(1);
    }

    int matches = 0;
    regmatch_t items;

    printf("Testing nickname. \n");
    for (int i = 0; i < clients.used; i++)
    {
      if (clients.array[i].name == currentClient->name)
      {
        matches++;
      }
    }
    if (strlen(currentClient->name) < 12 && matches == 0)
    {
      reti = regexec(&regularexpression, currentClient->name, matches, &items, 0);
      if (!reti)
      {
        printf("Nick %s is accepted.\n", currentClient->name);
        //Send ok to client
        memset(okOrError, 0, 20);
        strcpy(okOrError, "OK\n");
        if (send(currentClient->clientSock, &okOrError, sizeof(okOrError), 0) == -1)
        {
          printf("Sending OK failed!\n");
        }

        insertClient(&clients, *currentClient);
      }
      else
      {
        printf("%s is not accepted.\n", currentClient->name);
        //Send no to client
        memset(okOrError, 0, 20);
        strcpy(okOrError, "ERR\n");
        if (send(currentClient->clientSock, &okOrError, sizeof(okOrError), 0) == -1)
        {
          printf("Sending ERR failed!\n");
        }
      }
    }
    else
    {
      printf("Name %s too long or already exists.\n", currentClient->name);
      //Send no to client
      memset(okOrError, 0, 20);
      strcpy(okOrError, "ERR\n");
      if (send(currentClient->clientSock, &okOrError, sizeof(okOrError), 0) == -1)
      {
        printf("Sending ERR failed!\n");
      }
    }
    regfree(&regularexpression);
  }

  while (1)
  {
    if (leave_flag)
    {
      break;
    }
    memset(currentClient->message, 0, 255);
    int recieve = recv(currentClient->clientSock, &currentClient->message, sizeof(currentClient->message), 0);
    if (recieve == -1)
    {
      printf("Recieve failed\n");
      leave_flag = 1;
    }
    else if((strcmp(currentClient->message, "") != 0))
    {
      char temp[255];
      strcpy(temp, currentClient->message);
      sprintf(currentClient->message, "%s: %s", currentClient->name, temp);
      sendMessage(currentClient->message, currentClient->uid);
    }
  }
  close(currentClient->clientSock);
  removeClient(&clients, currentClient->uid);
  free(currentClient);
  pthread_detach(pthread_self());

  return NULL;
}

#define DEBUG
int main(int argc, char *argv[])
{

  /* Do more magic */
  if (argc != 2)
  {
    printf("Usage: %s <ip>:<port> \n", argv[0]);
    exit(1);
  }
  /*
    Read first input, assumes <ip>:<port> syntax, convert into one string (Desthost) and one integer (port). 
     Atm, works only on dotted notation, i.e. IPv4 and DNS. IPv6 does not work if its using ':'. 
  */
  char delim[] = ":";
  char *Desthost = strtok(argv[1], delim);
  char *Destport = strtok(NULL, delim);
  if (Desthost == NULL || Destport == NULL)
  {
    printf("Usage: %s <ip>:<port> \n", argv[0]);
    exit(1);
  }
  // *Desthost now points to a sting holding whatever came before the delimiter, ':'.
  // *Dstport points to whatever string came after the delimiter.

  /* Do magic */
  int port = atoi(Destport);
#ifdef DEBUG
  printf("Host %s, and port %d.\n", Desthost, port);
#endif

  int backLogSize = 10;
  int yes = 1;

  struct addrinfo hint, *servinfo, *p;
  int rv;
  int serverSock;
  pthread_t tid;

  memset(&hint, 0, sizeof(hint));
  hint.ai_family = AF_UNSPEC;
  hint.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(Desthost, Destport, &hint, &servinfo)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }
  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((serverSock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
    {
      printf("Socket creation failed.\n");
      continue;
    }
    printf("Socket Created.\n");
    rv = bind(serverSock, p->ai_addr, p->ai_addrlen);
    if (rv == -1)
    {
      perror("Bind failed!\n");
      close(serverSock);
      continue;
    }
    break;
  }

  freeaddrinfo(servinfo);

  if (p == NULL)
  {
    fprintf(stderr, "Server failed to create an apporpriate socket.\n");
    exit(1);
  }

  if (setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
  {
    perror("setsockopt failed!\n");
    exit(1);
  }

  printf("Lyssnar på %s:%d \n", Desthost, port);

  rv = listen(serverSock, backLogSize);
  if (rv == -1)
  {
    perror("Listen failed!\n");
    exit(1);
  }

  struct sockaddr_in clientAddr;
  socklen_t client_size = sizeof(clientAddr);

  initArray(&clients, 5);

  int clientSock = 0;

  while (1)
  {
    clientSock = accept(serverSock, (struct sockaddr *)&clientAddr, &client_size);
    if (clientSock == -1)
    {
      perror("Accept failed!\n");
    }

    printIpAddr(clientAddr);

    clientDetails *currentClient = (clientDetails *)malloc(sizeof(clientDetails));
    currentClient->address = clientAddr;
    currentClient->clientSock = clientSock;
    currentClient->uid = uid++;

    pthread_create(&tid, NULL, &handle_client, (void *)currentClient);
    //Reduce CPU usage
    sleep(1);
  }

  freeArray(&clients);
  return 0;
}

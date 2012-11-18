#include <assert.h>
#include <constants.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <proxy.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int queue[Q_SIZE];
int num = 0;
int add = 0;
int rem = 0;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;
pthread_cond_t full = PTHREAD_COND_INITIALIZER;

int reqs = 0;
pthread_mutex_t reqs_lock = PTHREAD_MUTEX_INITIALIZER;
void *boss(void *data);
void *worker(void *data);
void printUsage(void);

int port_num = DEFAULT_PROXY_ADDR;
int num_threads = NUM_THREADS_SERVER;

int main(int argc, char *argv[])
{
  int c;
  opterr = 0;
  while((c = getopt(argc, argv, "p:t:")) != -1)
  {
    switch(c)
    {
    case 'p':
      port_num = atoi(optarg);
      break;
    case't':
      num_threads = atoi(optarg);
      break;
    case '?':
      if(optopt == 'p' || optopt == 't')
	printf("Option -%c requires an argument\n", optopt);
      else
	printf("Unknown option -%c\n", optopt);
    default:
      printf("Usage: %s [-p port_num] [-t num_threads]\n", argv[0]);
      return 0;
    }
  }
  if(port_num < 1)
  {
    printf("Invalid port number\n");
    return 0;
  }
  if(num_threads < 1)
  {
    printf("Invalid number of threads\n");
    return 0;
  }
  
  server_create();
  return 0;
}

void server_create(void)
{
  pthread_t boss_thread;
  pthread_t workers[num_threads];
  int e, i;
  e = pthread_create(&boss_thread, NULL, boss, NULL);
  assert(e == 0);

  for(i = 0; i < num_threads; i++)
  {
    e = pthread_create(&workers[i], NULL, worker, NULL);
    assert(e == 0);
  }

  e = pthread_join(boss_thread, NULL);
  assert(e == 0);

  for(i = 0; i < num_threads; i++)
  {
    e = pthread_join(workers[i], NULL);
    assert(e == 0);
  }
}

void *boss(void *data)
{
  int hSocket, hServerSocket; /* handle to socket */
  struct sockaddr_in address; /* Internet socket address struct */
  int nAddressSize = sizeof(struct sockaddr_in);
  int nHostPort = port_num;

  hServerSocket = socket(AF_INET, SOCK_STREAM, 0);
  if(hServerSocket == SOCKET_ERROR)
  {
    printf("Could not make a socket\n");
    return NULL;
  }

  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(nHostPort);
  address.sin_family = AF_INET;

  if(bind(hServerSocket, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
  {
    printf("Could not connect to host\n");
    return NULL;
  }

  if(listen(hServerSocket, Q_SIZE) == SOCKET_ERROR)
  {
    printf("Could not listen\n");
    return NULL;
  }
  printf("Listening...\n");
  while(1)
  {
    hSocket = accept(hServerSocket, (struct sockaddr *)&address, (socklen_t *)&nAddressSize);
    pthread_mutex_lock(&lock);
    while(num == Q_SIZE)
      pthread_cond_wait(&full, &lock);
    queue[add] = hSocket;
    add = (add + 1) % Q_SIZE;
    num++;
    pthread_mutex_unlock(&lock);
    pthread_cond_signal(&empty);
  }
  return NULL;
}

void *worker(void *data)
{
  int hSocket;
  char pBuffer[BUFFER_SIZE];
  char method[BUFFER_SIZE];
  char path[BUFFER_SIZE];
  char *scheme;
  char url[BUFFER_SIZE];
  char *hostname;
  while(1)
    {
      pthread_mutex_lock(&lock);
      while(num == 0)
	pthread_cond_wait(&empty, &lock);
      hSocket = queue[rem];
      rem = (rem + 1) % Q_SIZE;
      num--;
      pthread_mutex_unlock(&lock);
      pthread_cond_signal(&full);
      
      /* Process information */ 
      read(hSocket, pBuffer, BUFFER_SIZE);
      
      if(sscanf(pBuffer, "%[^ ] %[^ ]", method, url) < 2)
      {
	send_error(hSocket, BAD_REQUEST, "Not the accepted protocol");
	continue;
      }
      
      if(strcasecmp(method, "get") != 0)
      {
	send_error(hSocket, NOT_IMPLEMENTED, "Only implemented GET");
	continue;
      }

      int req_port = parse_url(url, &scheme, &hostname, path);
      if(strcasecmp(scheme, "http") != 0)
      {
	send_error(hSocket, NOT_IMPLEMENTED, "Only implemented http");
	continue;
      }

      /* Connect to server */
      int fwdSocket; /* handle to socket */
      struct hostent *pHostInfo; /* holds info about machine */
      struct sockaddr_in address; /* Internet socket address struct */
      long nHostAddress;
      char strHostName[HOST_NAME_SIZE];
      int nHostPort;
      char output[BUFFER_SIZE];
  
      strcpy(strHostName, hostname);
      nHostPort = req_port;
 
      pthread_mutex_lock(&lock);
      pHostInfo = gethostbyname(strHostName);
      pthread_mutex_unlock(&lock);

      memcpy(&nHostAddress, pHostInfo->h_addr, pHostInfo->h_length);
      
      address.sin_addr.s_addr = nHostAddress;
      address.sin_port = htons(nHostPort);
      address.sin_family = AF_INET;
      
      fwdSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
      if(fwdSocket == SOCKET_ERROR)
      {
	send_error(hSocket, INTERNAL_ERROR, "Unable to create connection");
	continue;
      }
    
      if(connect(fwdSocket, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
      {
	send_error(hSocket, INTERNAL_ERROR, "Could not connect to host machine");
	continue;
      }

      // forward request
      write(fwdSocket, pBuffer, strlen(pBuffer));
      
      // read response
      int bytesRead = read(fwdSocket, output, BUFFER_SIZE);
      while(bytesRead > 0)
      {
	write(hSocket, output, bytesRead);
	bytesRead = read(fwdSocket, output, BUFFER_SIZE);
      }

      if(close(fwdSocket) == SOCKET_ERROR)
	printf("Could not close fwdSocket\n");
      
      if(close(hSocket) == SOCKET_ERROR)
	printf("Could not close hSocket\n");
    }
  return NULL;
}

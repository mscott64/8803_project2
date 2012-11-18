#include <assert.h>
#include <client.h>
#include <constants.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

char *hostname = DEFAULT_MACHINE;
int port_num = DEFAULT_PORT_NUM;
int num_requests = DEFAULT_REQUESTS;
int num_threads = NUM_THREADS_CLIENT;
int num_files = DEFAULT_NUM_FILES;
int total_bytes = 0;
double total_response_time = 0;
int total_num_requests = 0;
double total_time = 0;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t start = PTHREAD_COND_INITIALIZER;
int should_start = 0;

void *load(void *data);

int main(int argc, char *argv[])
{
  int c;
  opterr = 0;
  while((c = getopt(argc, argv, "uh:p:r:t:f:")) != -1)
  {
    switch(c)
    {
    case 'h':
      hostname = optarg;
      break;
    case 'p':
      port_num = atoi(optarg);
      break;
    case 'r':
      num_requests = atoi(optarg);
      break;
    case 't':
      num_threads = atoi(optarg);
      break;
    case 'f':
      num_files = atoi(optarg);
      break;
    case '?':
      if(optopt == 'p' || optopt == 'h' || optopt == 'r' || optopt == 't' || optopt == 'f')
	printf("Option -%c requires an argument", optopt);
      else
	printf("Unknown option -%c\n", optopt);
    case 'u': 
    default:
      printf("Usage %s [-h hostname][-p port_num][-r num_requests][-t num_threads][-f num_files]\n %s -u prints the usage\n", argv[0], argv[0]);
      return 0;
    }
  }
  if(port_num < 1)
  {
    printf("Invalid port number\n");
    return 0;
  }

  if(num_requests < 1)
  {
    printf("Invalid number of requests\n");
    return 0;
  }
  
  if(num_threads < 1)
  {
    printf("Invalid number of threads\n");
    return 0;
  }
  
  if(num_files < 1)
  {
    printf("Invalid number of files\n");
    return 0;
  }
  client_create();
  return 0;
}

void client_create(void)
{
  pthread_t load_threads[num_threads];
  int i, e;
  for(i = 0; i < num_threads; i++)
  {
    e = pthread_create(&load_threads[i], NULL, load, NULL);
    assert(e == 0);
  }
  pthread_mutex_lock(&start_lock);
  should_start = 1;
  pthread_mutex_unlock(&start_lock);
  pthread_cond_broadcast(&start);
  
  for(i = 0; i < num_threads; i++)
  {
    e = pthread_join(load_threads[i], NULL);
    assert(e == 0);
  }

  printf("------------Stats----------\n");
  printf("Total bytes: %d\n", total_bytes);
  printf("Total time: %.4f\n", total_response_time);
  printf("Total failed requests: %d\n", (num_threads * num_requests) - total_num_requests);
  printf("Bytes/sec: %.4f\n", total_bytes / total_response_time);
  printf("Requests/sec: %.4f\n", total_num_requests / total_response_time);
  printf("Avg response time: %.4f\n", total_response_time / total_num_requests);
}

void *load(void *data)
{
  pthread_mutex_lock(&start_lock);
  while(should_start == 0)
    pthread_cond_wait(&start, &start_lock);
  pthread_mutex_unlock(&start_lock);
  
  int i;
  int hSocket; /* handle to socket */
  struct hostent *pHostInfo; /* holds info about machine */
  struct sockaddr_in address; /* Internet socket address struct */
  long nHostAddress;
  char strHostName[HOST_NAME_SIZE];
  int nHostPort;
  char request[HOST_NAME_SIZE];
  char output[BUFFER_SIZE];
  
  strcpy(strHostName, hostname);
  nHostPort = port_num;
 
  pthread_mutex_lock(&lock);
  pHostInfo = gethostbyname(strHostName);
  pthread_mutex_unlock(&lock);

  memcpy(&nHostAddress, pHostInfo->h_addr, pHostInfo->h_length);
  
  address.sin_addr.s_addr = nHostAddress;
  address.sin_port = htons(nHostPort);
  address.sin_family = AF_INET;

  for(i = 0; i < num_requests; i++)
  {
    hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(hSocket == SOCKET_ERROR)
    {
      printf("Could not make socket\n");
      return NULL;
    }
    
    if(connect(hSocket, (struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
    {
      printf("Could not connect to host\n");
      return NULL;
    }
    int file_num = (rand() % num_files) + 1;
    sprintf(request, "%s%d.html", GET, file_num);
    time_t start_time = time(NULL);
    write(hSocket, request, strlen(request));
    int bytes_read = read(hSocket, output, BUFFER_SIZE);
    while(bytes_read > 0)
    {
      printf("%s\n", output);
      bytes_read = read(hSocket, output, BUFFER_SIZE);
    }
    time_t end_time = time(NULL);

    if(close(hSocket) == SOCKET_ERROR)
    {
      printf("Could not close socket\n");
      return NULL;
    }
    
    pthread_mutex_lock(&stats_lock);
    total_bytes += bytes_read;
    total_response_time += difftime(end_time, start_time);
    total_num_requests++;
    pthread_mutex_unlock(&stats_lock);
  }
  return NULL;
}

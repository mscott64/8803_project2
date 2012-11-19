#include <arpa/inet.h>
#include <assert.h>
#include <constants.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <proxy.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SVSHM_MODE SHM_R | SHM_W | (SHM_R>>3) | (SHM_W>>3) | (SHM_R>>6) | (SHM_W>>6)

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
void delete_shared_mem(int);

int port_num = DEFAULT_PROXY_ADDR;
int num_threads = NUM_THREADS_SERVER;
int local_port = DEFAULT_PORT_NUM;
int shared_mem = 0;

pthread_mutex_t shmem_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t shmem_full = PTHREAD_COND_INITIALIZER;
int shmem_segs[NUM_SHMEM_SEGS];
int shmem_num = 0;
int shmem_curr = 0;
/* Each shared memory segment has the following */
// pthread_mutex_t lock
// int write_done
// int length

int main(int argc, char *argv[])
{
  int c;
  opterr = 0;
  while((c = getopt(argc, argv, "mp:t:s:")) != -1)
  {
    switch(c)
    {
    case 'p':
      port_num = atoi(optarg);
      break;
    case't':
      num_threads = atoi(optarg);
      break;
    case 's':
      local_port = atoi(optarg);
      break;
    case 'm':
      shared_mem = 1;
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
  int i;
  if(shared_mem)
    for(i = 0; i < NUM_SHMEM_SEGS; i++)
      shmem_segs[i] = -1;
  
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
  /* Add signal handler for shared memory deletion */
  struct sigaction delete;
  delete.sa_handler = delete_shared_mem;
  sigemptyset(&delete.sa_mask);
  delete.sa_flags = 0;
  sigaction(SIGQUIT, &delete, NULL);

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
  while(1)
    {
      char pBuffer[BUFFER_SIZE];
      char method[BUFFER_SIZE];
      char path[BUFFER_SIZE];
      char *scheme;
      char url[BUFFER_SIZE];
      char *hostname;
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

      // local web server or not
      int local_ip;
      inet_pton(AF_INET, LOCAL_IP_ADDR, &local_ip);
      int is_local = local_ip == nHostAddress && req_port == local_port;
  
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

      if(is_local && shared_mem)
      {
	key_t key;
	int loc;
	int prev;
	
	/* Find unused memory segments */
	pthread_mutex_lock(&shmem_lock);
	while(shmem_num == NUM_SHMEM_SEGS)
	  pthread_cond_wait(&shmem_full, &shmem_lock);
	
	while(1)
	{
	  if(shmem_segs[shmem_curr] < 1)
	  {
	    loc = shmem_curr;
	    prev = shmem_segs[shmem_curr];
	    shmem_num++;
	    shmem_curr = (shmem_curr + 1) % NUM_SHMEM_SEGS;
	    break;
	  }
	  shmem_curr = (shmem_curr + 1) % NUM_SHMEM_SEGS;
	}
	pthread_mutex_unlock(&shmem_lock);

	key = ftok(SHMEM_PATH, loc);
	if(key < 0)
	{
	  send_error(hSocket, INTERNAL_ERROR, "Invalid shared memory key");
	  pthread_mutex_lock(&shmem_lock);
	  shmem_segs[loc] = 0;
	  shmem_num--;
	  pthread_mutex_unlock(&shmem_lock);
	  continue;
	}
	
	int id = shmget(key, BUFFER_SIZE, SVSHM_MODE | IPC_CREAT);
	if(id < 0)
	{
	  send_error(hSocket, INTERNAL_ERROR, "Invalid shared memory id");
	  pthread_mutex_lock(&shmem_lock);
	  shmem_segs[loc] = 0;
	  shmem_num--;
	  pthread_mutex_unlock(&shmem_lock);
	  continue;
	}

	void *shmem = shmat(id, NULL, 0);
	char *shmem_ptr = (char *)shmem;

	pthread_mutex_t *mem_lock;
	int *write_done;
	int *write_length;
	
	if(prev < 0)
	{ /* Set metadata in shared memory */
	  pthread_mutex_t lock_mem = PTHREAD_MUTEX_INITIALIZER;
	  int done_writing = 0;
	  int length = 0;
	  
	  memcpy(shmem_ptr, &lock_mem, sizeof(pthread_mutex_t));
	  mem_lock = (pthread_mutex_t *)shmem_ptr;
	  shmem_ptr = shmem_ptr + sizeof(pthread_mutex_t);
	  
	  memcpy(shmem_ptr, &done_writing, sizeof(int));
	  write_done = (int *)shmem_ptr;
	  shmem_ptr = shmem_ptr + sizeof(int);
	  
	  memcpy(shmem_ptr, &length, sizeof(int));
	  write_length = (int *)shmem_ptr;
	  shmem_ptr = shmem_ptr + sizeof(int);
	} else { /* Otherwise just set the pointers */
	  mem_lock = (pthread_mutex_t *)shmem_ptr;
	  shmem_ptr = shmem_ptr + sizeof(pthread_mutex_t);

	  write_done = (int *)shmem_ptr;
	  shmem_ptr = shmem_ptr + sizeof(int);
	  
	  write_length = (int *)shmem_ptr;
	  shmem_ptr = shmem_ptr + sizeof(int);
	}

	/* Write local GET request */
	sprintf(url, "%s://%s:%d/%s", scheme, hostname, req_port, path);
	sprintf(pBuffer, "%s %s %d", LOCAL_GET, url, key);
	write(fwdSocket, pBuffer, strlen(pBuffer));

	/* Read outcome status from socket */
	int read_bytes = read(fwdSocket, output, BUFFER_SIZE);
	
	if(strcmp(output, SUCCESS) != 0) {
	  // forward error response
	  write(hSocket, output, read_bytes);
	} else {
	  // copy file from memory
	  pthread_mutex_lock(mem_lock);
	  if(*write_done == 1)
	    memcpy(output, shmem_ptr, *write_length);
	  pthread_mutex_unlock(mem_lock);
	  write(hSocket, output, *write_length);
	}
	
	/* Make memory segment available */
	pthread_mutex_lock(&shmem_lock);
	shmem_num--;
	shmem_segs[loc] = 0;
	pthread_mutex_unlock(&shmem_lock);
	pthread_cond_signal(&shmem_full);

	shmdt(shmem);
      }
      else
      {
	// forward request
	write(fwdSocket, pBuffer, strlen(pBuffer));
	
	// read response
	int bytesRead = read(fwdSocket, output, BUFFER_SIZE);
	while(bytesRead > 0)
	{
	  write(hSocket, output, bytesRead);
	  bytesRead = read(fwdSocket, output, BUFFER_SIZE);
	}
      }

      if(close(fwdSocket) == SOCKET_ERROR)
	printf("Could not close fwdSocket\n");
      
      if(close(hSocket) == SOCKET_ERROR)
	printf("Could not close hSocket\n");
    }
  return NULL;
}

void delete_shared_mem(int signum)
{
  int i;
  if(shared_mem)
  {
    printf("Delete shared memory\n");
    for(i = 0; i < NUM_SHMEM_SEGS; i++)
    {
      int id = shmget(ftok(SHMEM_PATH, i), 0, 0);
      shmctl(id, IPC_RMID, NULL);
    }
  }
}

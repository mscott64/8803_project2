#include <assert.h>
#include <constants.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <server.h>
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

int port_num = DEFAULT_PORT_NUM;
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
      
      int is_local = strcasecmp(method, "local_get") == 0;
      
      if(strcasecmp(method, "get") != 0 && !is_local)
      {
	send_error(hSocket, NOT_IMPLEMENTED, "Only implemented GET");
	continue;
      }

      parse_url(url, &scheme, &hostname, path);
      
      void *shmem;
      void *shmem_ptr;
      pthread_mutex_t *mem_lock;
      int *write_done;
      int *write_length;
      
      if(is_local) 
      { /* Get shared memory segment */
	key_t key;
	if(sscanf(pBuffer, "%*[^ ] %*[^ ] %d", &key) < 1)
	{
	  send_error(hSocket, BAD_REQUEST, "Not the accepted LOCAL_GET protocol");
	  continue;
	}
	
	
	int id = shmget(key, 0, SVSHM_MODE);
	if(id < 0)
	{
	  send_error(hSocket, INTERNAL_ERROR, "Invalid id for shared memory");
	  continue;
	}
	
	shmem = shmat(id, NULL, 0);
	shmem_ptr = (char *)shmem;

	mem_lock = (pthread_mutex_t *)shmem_ptr;
	shmem_ptr = shmem_ptr + sizeof(pthread_mutex_t);

	write_done = (int *)shmem_ptr;
	shmem_ptr = shmem_ptr + sizeof(int);

	write_length = (int *)shmem_ptr;
	shmem_ptr = shmem_ptr + sizeof(int);
      }

      char *path_ptr = path;
      if(path[0] == '/')
	path_ptr = &(path[1]);
      
      int len = strlen(path_ptr);
      if(*path_ptr == '/' || strcmp(path_ptr, "..") == 0 || strncmp(path_ptr, "../", 3) == 0 || strstr(path_ptr, "/../") != NULL || strcmp(&(path_ptr[len-3]), "/..") == 0)
      {
	send_error(hSocket, BAD_REQUEST, "Tried to access a private file");
	continue;
      }
      
      FILE *f = fopen(path_ptr, "r");
      int total_length = 0;
      if(f)
      {
	while(fgets(pBuffer, BUFFER_SIZE, f) != NULL)
	{
	  if(is_local) {
	    memcpy(shmem_ptr, pBuffer, strlen(pBuffer));
	    shmem_ptr = shmem_ptr + strlen(pBuffer);
	    total_length += strlen(pBuffer);
	  } else {
	    write(hSocket, pBuffer, strlen(pBuffer));
	  }
	}
	if(is_local) { // Send write completion success
	  pthread_mutex_lock(mem_lock);
	  *write_done = 1;
	  *write_length = total_length;
	  pthread_mutex_unlock(mem_lock);
	  sprintf(pBuffer, "%s", SUCCESS);
	  write(hSocket, pBuffer, strlen(pBuffer));
	}
      }
      else
      {
	send_error(hSocket, NOT_FOUND, "Unable to locate file");
	continue;
      } 
     
      fclose(f);
      if(close(hSocket) == SOCKET_ERROR)
      {
	printf("Could not close socket\n");
	continue;
      }
    }
  return NULL;
}

#ifndef SERVER_H
#define SERVER_H

void server_create(void);

void send_error(int hSocket, char *error_msg, char *descrip);

int parse_url(char *url, char **scheme, char **hostname, char *path);

#endif /* SERVER_H */

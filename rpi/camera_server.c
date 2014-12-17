#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <getopt.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <strings.h>
#include <string.h>

#include <stdio.h>

#define CAM_CMD "/usr/local/bin/camera_streamer.sh"

#define BUF_SIZE 1024 //receiving buffer
int portno = 1035;

char cmd[256];
int verbose = 1;
int background = 0;
int stop = 0;

int cam_active = 0;

void print_usage() {
	printf("-d run in background\n");
	printf("-p [port] port to listen on (defaults to %i)\n",portno);
}

void catch_signal(int sig)
{
	if (verbose) printf("Signal: %i\n",sig);
	stop = 1;
}

int getMsgSize(unsigned char *b) {
	int tmp;
	memcpy(&tmp,b,4);
	int ret = ntohl(tmp);
	if (verbose) printf("Awaiting %i bytes from client\n",ret);
	return ret;
}

void startCam(unsigned char ip[4],int port) {
	if (cam_active) {
		if (verbose) printf("Camera is already streaming!\n");
		return;
	}
	int ret;
	memset(cmd, '\0', 256);
	sprintf(cmd, "%s start %i.%i.%i.%i %i",CAM_CMD, ip[0],ip[1],ip[2],ip[3],port);
	if (verbose) printf("Executing: %s\n",cmd);
	ret=system(cmd);
	
	if (ret==0) cam_active = 1;

	if (verbose) printf("Starting camera_streamer returned: %i\n",ret);
}

void stopCam() {
	if (!cam_active) {
		return;
	}
	int ret;
	memset(cmd, '\0', 256);
	sprintf(cmd, "%s stop",CAM_CMD);
	if (verbose) printf("Executing: %s\n",cmd);
	ret = system(cmd);
	if (ret==0) cam_active = 0;
	if (verbose) printf("Stoping camera_streamer returned: %i\n",ret);
}

void processMsg(unsigned char *buf, int len, unsigned char *bufout, int *bufout_len) {	
	unsigned char ip[4];
	int port;
	int tmp;
	int type;
	*bufout_len = 0;

	type = buf[0];
	if (verbose) printf("Received type: %i\n",type);

	if (type==1) { //disconnect
		stopCam();
		return;
	}

	memcpy(ip,buf+1,4);

	memcpy(&tmp,buf+5,4);
	port = ntohl(tmp);

	startCam(ip,port);

/*
	ret = htonl(ret);
	memcpy(bufout,&ret,4);
	*bufout_len = 4;
*/
}

int main(int argc, char **argv)
{
	int sock,client,max_fd;
	int msgSize = 0;
	int ret;
	struct sockaddr_in address;
	unsigned char bufin[BUF_SIZE];
	unsigned short buf_c = 0; //counter
	unsigned char bufout[BUF_SIZE];
	struct timeval timeout;
	fd_set readfds;

	int option;

	while ((option = getopt(argc, argv,"dp:")) != -1) {
		switch (option)  {
			case 'd': background = 1; verbose=0; break;
			case 'p': portno = atoi(optarg);  break;
			default:
				  print_usage();
				  return -1;
		}
	}

	signal(SIGTERM, catch_signal);
	signal(SIGINT, catch_signal);

	buf_c = 0;
	client = 0;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("opening socket");
		exit(1);
	}


	/* Create name. */
	bzero((char *) &address, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(portno);

	if (bind(sock, (struct sockaddr *) &address, sizeof(struct sockaddr_in))) {
		perror("binding stream socket");
		exit(1);
	}
	if (verbose) printf("Socket created on port %i\n", portno);

	if (listen(sock,1) < 0) {
		perror("listen");
		stop=1;
	}

	if (background) {
		if (daemon(0,1) < 0) { 
			perror("daemon");
			return -1;
		}
		if (verbose) printf("Running in the background\n");
	}


	if (verbose) printf("Starting main loop\n");
	while (!stop) {
		FD_ZERO(&readfds);
		max_fd = 0;
		if (client) {
			FD_SET(client, &readfds);
			FD_SET(sock, &readfds);
			max_fd = sock>client?sock:client;
		} else {
			FD_SET(sock, &readfds);
			max_fd = sock;
		}

		timeout.tv_sec = 0;
		timeout.tv_usec = 1000*1000L; //every sec 
		int sel = select( max_fd + 1 , &readfds , NULL , NULL , &timeout);
		if ((sel<0) && (errno!=EINTR)) {
			perror("select");
			stop=1;
		}
		//If something happened on the master socket , then its an incoming connection
		if (!stop && FD_ISSET(sock, &readfds)) {
			int t = accept(sock, 0, 0);
			if (t<0) {
				perror("accept");
				continue;
			}
			client = t;
			buf_c = 0;
			msgSize = 0;
		} 

		if (!stop && FD_ISSET(client, &readfds)) {
			ret = read(client , bufin+buf_c, BUF_SIZE - buf_c); 
			if (ret < 0) {
				perror("Reading error");
				close(client);
				client = 0;
			}	
			else if (ret == 0) {	//client disconnected
				if (verbose) printf("Client disconnected.\n");
				close(client);
				client = 0;
				stopCam();	
			} else buf_c += ret;
			
			if (buf_c>=4 && !msgSize) msgSize = getMsgSize(bufin);
			if (msgSize && buf_c>=msgSize) { //full message received
				processMsg(bufin+4,buf_c-4,bufout,&ret);
				msgSize = 0;
				buf_c = 0;
				/*
				if (verbose) printf("Sending response...\n");
				if (ret) ret = send(client, bufout, ret, MSG_NOSIGNAL );
				if (ret == -1) {
					if (verbose) printf("Lost connection to client.\n");
					close(client);
					client = 0;
				}
				*/
			}
		}
	}

	if (verbose) {
		printf("closing\n");
		fflush(NULL);
	}

	stopCam();

	sleep(1);

	if (client) close(client);
	close(sock);
}


//============================================================================
// Name        : frankaJointVelocity.cpp
// Author      : Dean Martinovič
// Version     : 1.0.0
// Copyright   : LARICS 2022
// Description : TODO
//============================================================================


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netinet/in.h> /* INET constants and stuff */
#include <arpa/inet.h>
#include <sys/socket.h> /* socket specific definitions */
#include <fcntl.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h> /* defines STDIN_FILENO, system calls,etc */
#include <errno.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <iostream>
#include <cstdlib>

#include <signal.h> // Add this header for signal handling

#include "ros/ros.h"
#include "sriod_ros_publisher/SriodData.h"

#define RX_BUFF_SZ			64
#define KB_POLL_CYC			5

const std::string LOCAL_ADDRESS = "192.168.0.2";
const std::string REMOTE_ADDRESS = "192.168.0.1";

volatile sig_atomic_t g_stop_signal = 0;  //definiramo stop signal, volatile compileru govori da ne optimizira

// Signal handler function to handle SIGINT (Ctrl + C)
void handle_sigint(int signum) {
    // Set the flag to indicate Ctrl + C is pressed
    g_stop_signal = 1;
}

int main(int argc, char **argv)
{
	int netStatVar, len;
	int udpFd;
	int udpSoChecksumDisable = 1;
	struct sockaddr_in server, remoteHost;
	static char rxBuf[RX_BUFF_SZ];
	static unsigned int calibrating = 0;
	
	ros::init(argc, argv, "sriod_publisher_node");
	/*defining ros publisher as nh*/
	ros::NodeHandle nh;
	ros::Publisher publisher = nh.advertise<sriod_ros_publisher::SriodData>("sriod_data", 1); //queue is 1 because only newest data is relevant //
	
	puts("I am the FRANKA client------------\n");

	const unsigned int CMDARGS = 11;
	if ( (argc != 1) && (argc != CMDARGS) )
	{
		printf("Syntax is ./frankaJointVelocity or ./frankaJointVelocity Ki  Kud  KpF  Klr  KyF  KrF cicMin  cicGl  vLim  shifts.\nFRANKA client: I go out\n");
		// ./frankaJointVelocity 12e-9  25e-9  300e-9  15e-9  80e-9  60e-9  15e3  15e6  0.2  4
		return 0;
	}

	/* make stdin non-blocking */
	if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) < 0)
	{
		printf("fcntl(). %s. %i\n", strerror(errno), errno);
		return EXIT_FAILURE;
	}

	/* create UDP socket for communication with the gradiometer */
	udpFd = socket( AF_INET, SOCK_DGRAM /*| SOCK_NONBLOCK*/, 0 /*IPPROTO_UDP*/ );
	if( udpFd < 0 )
	{
		printf("socket(). %s. %i\n", strerror(errno), errno);
		return EXIT_FAILURE;
	}

	/* disable checksum calculation */
	if (setsockopt(udpFd, SOL_SOCKET, SO_NO_CHECK, &udpSoChecksumDisable, sizeof(udpSoChecksumDisable)) == -1)
	{
		printf("setsockopt(). %s. %i\n", strerror(errno), errno);
		close(udpFd);
		return EXIT_FAILURE;
	}

	/* make socket listen to local address */
	bzero(&server, sizeof(server));
	server.sin_family=AF_INET;
	server.sin_addr.s_addr= inet_addr(LOCAL_ADDRESS.c_str());
	server.sin_port = htons(27016);
	/* bind socket to local address */
	netStatVar = bind(udpFd, (struct sockaddr*)(&server), sizeof(server));
	if( netStatVar != 0 )
	{
		printf("bind(). %s. %i\n", strerror(errno), errno);
		close(udpFd);
		return EXIT_FAILURE;
	}

	/* connect to remote host for sending data */
	/* attempt to connect to remote host */
	bzero(&remoteHost, sizeof(remoteHost));
	remoteHost.sin_family=AF_INET;
	remoteHost.sin_addr.s_addr= inet_addr(REMOTE_ADDRESS.c_str());
	remoteHost.sin_port = htons(27016);
	netStatVar = connect(udpFd, (struct sockaddr*)(&remoteHost), sizeof(remoteHost));
	if( netStatVar != 0 )
	{
		printf("connect(). %s. %i\n", strerror(errno), errno);
		close(udpFd);
		return EXIT_FAILURE;
	}
	
	/* wait for keyboard command to launch the Finder, to leave or to start auto-calibration */
	std::cout << "You have three options:\n"
				  << " 1) c+return --> command the gradiometer to calibrate itself. This is typically only once needed after power up.\n"
				  << " 2) n+return --> start magnetic tracking. Tell people to step back! Make sure to have the user stop button at hand! Use m+return to stop tracking at any time.\n"
				  << " 3) m+return --> leave program. You can use this command at any time." << std::endl;

	if (signal(SIGINT, handle_sigint) == SIG_ERR) {
        printf("Error setting up signal handler for SIGINT.\n");
        return EXIT_FAILURE;
    }

	while(!g_stop_signal)
	{
		sleep(1);
		len = read(STDIN_FILENO, rxBuf, 1);
		if ( len < 0 )
		{
		}
		else if ( len == 1 && rxBuf[0] == 'c' )
		{
			char txByte = 10;
			if ( send(udpFd, &txByte, 1, 0) < 0 )
			{
				printf("send(). %s. %i\n", strerror(errno), errno);
				fflush(stdout);
				continue;
			}
			puts("Auto-Calibration request sent to Finder\n");

			/* wait for confirmation */
			calibrating = 1;
			while( calibrating > 0 )
			{
				len = recv(udpFd, rxBuf, RX_BUFF_SZ, 0);
				if ( len < 0 )
				{
					printf("recv(). %s. %i\n", strerror(errno), errno);
					fflush(stdout);
					sleep(1);
					continue;
				}
				else if ( len == 1 && rxBuf[0] == 10 )
				{
					/* confirmed */
					calibrating = 0;
				}
				else
				{
					rxBuf[len] = '\0';
					printf("%s", rxBuf);
					fflush(stdout);
				}
			} /* while( calibrating > 0 ) */
		}
		else if ( len == 1 && rxBuf[0] == 'n' )
		{
			char txByte = 11;
			if ( send(udpFd, &txByte, 1, 0) < 0 )
			{
				printf("send(). %s. %i\n", strerror(errno), errno);
				fflush(stdout);
				continue;
			}
			puts("Launch Finder\n");
			break;
		}
	} 


	/* variables yielding the tensor contractions received from Finder */
	struct
	{
		std::mutex mutex;
		unsigned int ctl, ctr, ctu, ctd, ckl, ckr, cku, ckd, cic, errCode, cul, cur, cdl, cdr;
	}finderData{};
	/* shutdown command from keyboard received */
	std::atomic_uint doExitProg{0};

	/* variables yielding the gain scheduling configurations */
	struct
	{
		double Ki, Kud, KpF, Klr, KyF, KrF;
		unsigned int cicMin, cicGl;
		double vLim;
		unsigned int shifts;
	}gainSchedCgf{};

	if( argc == CMDARGS )
	{
		gainSchedCgf.Ki = std::stod(argv[1]);
		gainSchedCgf.Kud = std::stod(argv[2]);
		gainSchedCgf.KpF = std::stod(argv[3]);
		gainSchedCgf.Klr = std::stod(argv[4]);
		gainSchedCgf.KyF = std::stod(argv[5]);
		gainSchedCgf.KrF = std::stod(argv[6]);
		double tmpV = std::stod(argv[7]);
		gainSchedCgf.cicMin = (unsigned int)tmpV;
		tmpV = std::stod(argv[8]);
		gainSchedCgf.cicGl = (unsigned int)tmpV;
		gainSchedCgf.vLim = std::stod(argv[9]);
		tmpV = std::stod(argv[10]);
		gainSchedCgf.shifts = (unsigned int)tmpV;

		printf("Gains %.9lf\t %.9lf\t %.9lf\t %.9lf\t %.9lf\n", gainSchedCgf.Ki, gainSchedCgf.Kud, gainSchedCgf.KpF, gainSchedCgf.Klr, gainSchedCgf.KyF);
		printf("Confg %u\t %.9lf\t %u\n", gainSchedCgf.cicGl, gainSchedCgf.vLim, gainSchedCgf.shifts);
		fflush(stdout);
	}
	else
	{
		/* use default values if user input is not useful */
		gainSchedCgf.Ki = 12e-9;
		gainSchedCgf.Kud = 25e-9;
		gainSchedCgf.KpF = 300e-9;
		gainSchedCgf.Klr = 15e-9;
		gainSchedCgf.KyF = 80e-9;
		gainSchedCgf.KrF = 60e-9;
		gainSchedCgf.cicMin = 15e3;
		gainSchedCgf.cicGl = 15e6;
		gainSchedCgf.vLim = 0.2;
		gainSchedCgf.shifts = 4;
	}

	/* start low-prio thread for receiving data from Finder */
	std::thread print_thread([&udpFd, &finderData, &doExitProg, &publisher]() //&publisher is added to reference it
	{
		static unsigned int kbPollCntTrd = KB_POLL_CYC;
		char rxBufTrd[RX_BUFF_SZ];
		int lenTrd;

		while (1)
		{
			/* check for stop signal from keyboard */
			if ( 0 == kbPollCntTrd-- )
			{
				/* reset counter */
				kbPollCntTrd = KB_POLL_CYC;

				/* read from stdin */
				lenTrd = read(STDIN_FILENO, rxBufTrd, 1);
				if ( lenTrd < 0 )
				{
	//				printf("read keyboard. %s. %i\n", strerror(errno), errno);
	//				fflush(stdout);
				}
				else if ( lenTrd == 1 && rxBufTrd[0] == 'm' )
				{
					char txByte = 22;
					if ( send(udpFd, &txByte, 1, 0) < 0 )
					{
						printf("send(). %s. %i\n", strerror(errno), errno);
						fflush(stdout);
					}
					doExitProg = 1;
					puts("FRANKA client: Shutdown Finder\n");
					/* TODO: stop motion */
					break;
				}
			} /* if ( 0 == kbPollCnt-- ) */

			/* wait for the gradient tensor contraction data */
			lenTrd = recv(udpFd, rxBufTrd, RX_BUFF_SZ, 0);
			if ( lenTrd < 0 )
			{
				printf("recv(). %s. %i\n", strerror(errno), errno);
				fflush(stdout);
				continue;
			}
			else if ( lenTrd == 60 )
			{
				unsigned int* ptr = (unsigned int*)rxBufTrd;

				/* Try to lock data to avoid read write collisions */
				if (finderData.mutex.try_lock())
				{
					finderData.ctl = *(ptr + 0);
					finderData.ctr = *(ptr + 1);
					finderData.ctu = *(ptr + 2);
					finderData.ctd = *(ptr + 3);
					finderData.ckl = *(ptr + 4);
					finderData.ckr = *(ptr + 5);
					finderData.cku = *(ptr + 6);
					finderData.ckd = *(ptr + 7);
					finderData.cic = *(ptr + 8);
					finderData.errCode = *(ptr + 9);
					finderData.cul = *(ptr + 10);
					finderData.cur = *(ptr + 11);
					finderData.cdl = *(ptr + 12);
					finderData.cdr = *(ptr + 13);
					
										
					//publishing data
					sriod_ros_publisher::SriodData msg; //constructing msg object which will publish data to a topic
					msg.ctl = *(ptr + 0);
					msg.ctr = *(ptr + 1);
					msg.ctu = *(ptr + 2);
					msg.ctd = *(ptr + 3);
					msg.cic = *(ptr + 8);
					publisher.publish(msg);

					finderData.mutex.unlock();
					
				}
			}
			else
			{
				rxBufTrd[lenTrd] = '\0';
				printf("Rx %i, %s\n", lenTrd, rxBufTrd);
				fflush(stdout);
				continue;
			} /* if ( lenTrd < 0 ) */

		} /* while(1) */
	}); /* low-prio thread */


	/* wait for low-prio thread to exit */
	if (print_thread.joinable())
	{
		print_thread.join();
	}

	puts("FRANKA client: I go out with success.");
	close(udpFd);

	return EXIT_SUCCESS;
}









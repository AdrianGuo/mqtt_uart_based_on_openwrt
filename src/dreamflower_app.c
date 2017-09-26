/*
 * Copyright (c) 2006,2007 Stefan Bethke <stb@lassitu.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

static const char __rcsid[] =
	"$Header: /cvsroot/serialconsole/sc/sc.c,v 1.12 2007/12/04 02:34:46 doj Exp $";

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sysexits.h>
#include <termios.h>
#include <unistd.h>

#include "debugfl.h"
#include "Daemon.h"
#include "uart1.h"
#include "uart_1_app.h"
#include "fdebug.h"
#include "calendar.h"
#include "tcpdump.h"
#include "mqtt_publish.h"
#include "mqtt_subscribe.h"


/* functions */
int parse_options(int argc, char *argv[]);

#if !defined(SC_VERSION)
#define SC_VERSION "0.9-dev"
#endif
#if !defined(DEFAULTDEVICE)
#define DEFAULTDEVICE	"cuad0"
#endif
#if !defined(DEFAULTSPEED)
#define DEFAULTSPEED	"9600"
#endif
#if !defined(DEFAULTPARMS)
#define DEFAULTPARMS	"8n1"
#endif
#if !defined(PATH_DEV)
#define PATH_DEV "/dev"
#endif

#if B2400 == 2400 && B9600 == 9600 && B38400 == 38400
#define TERMIOS_SPEED_IS_INT
#endif

#if !defined(TERMIOS_SPEED_IS_INT)
struct termios_speed {
	long code;
	long speed;
};
struct termios_speed termios_speeds[] = {
	{ B50, 50 },
	{ B75, 75 },
	{ B110, 110 },
	{ B134, 134 },
	{ B150, 150 },
	{ B200, 200 },
	{ B300, 300 },
	{ B600, 600 },
	{ B1200, 1200 },
	{ B1800, 1800 },
	{ B2400, 2400 },
	{ B4800, 4800 },
#if defined(B7200)
	{ B7200, 7200 },
#endif
	{ B9600, 9600 },
#if defined(B14400)
	{ B14400, 14400 },
#endif
	{ B19200, 19200 },
#if defined(B28800)
	{ B28800, 28800 },
#endif
	{ B38400, 38400 },
#if defined(B57600)
	{ B57600, 57600 },
#endif
#if defined(B76800)
	{ B76800, 76800 },
#endif
#if defined(B115200)
	{ B115200, 115200 },
#endif
#if defined(B153600)
	{ B153600, 153600 },
#endif
#if defined(B230400)
	{ B230400, 230400 },
#endif
#if defined(B307200)
	{ B307200, 307200 },
#endif
#if defined(B460800)
	{ B460800, 460800 },
#endif
#if defined(B500000)
	{ B500000, 500000 },
#endif
#if defined(B576000)
	{ B576000, 576000 },
#endif
#if defined(B921600)
	{ B921600, 921600 },
#endif
#if defined(B1000000)
	{ B1000000, 1000000 },
#endif
#if defined(B1152000)
	{ B1152000, 1152000 },
#endif
#if defined(B1500000)
	{ B1500000, 1500000 },
#endif
#if defined(B2000000)
	{ B2000000, 2000000 },
#endif
#if defined(B2500000)
	{ B2500000, 2500000 },
#endif
#if defined(B3000000)
	{ B3000000, 3000000 },
#endif
#if defined(B3500000)
	{ B3500000, 3500000 },
#endif
#if defined(B4000000)
	{ B4000000, 4000000 },
#endif
	{ 0, 0 }
};
#endif


enum escapestates {
	ESCAPESTATE_WAITFORCR = 0,
	ESCAPESTATE_WAITFOREC,
	ESCAPESTATE_PROCESSCMD,
	ESCAPESTATE_WAITFOR1STHEXDIGIT,
	ESCAPESTATE_WAITFOR2NDHEXDIGIT,
};


static int scrunning = 1;
static char *path_dev = PATH_DEV "/";
static int qflag = 0;

///////////////////////////////////////////////////////////////////////////////
extern volatile sig_atomic_t _running;



///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
char		run_flag	= 0;	//-0表示正常运行		1表示进入调试模式,在终端的监控下运行
char		test_branch	= 0;	//-0



///////////////////////////////////////////////////////////////////////////////

#ifdef __CYGWIN__
static int
cfmakeraw(struct termios *termios_p)
{
  termios_p->c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
  termios_p->c_oflag &= ~OPOST;
  termios_p->c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
  termios_p->c_cflag &= ~(CSIZE|PARENB);
  termios_p->c_cflag |= CS8;
  return 0;
}

static int
cfsetspeed(struct termios *termios_p, speed_t speed)
{
  int r=cfsetospeed(termios_p, speed);
  if(r<0) return r;
  return cfsetispeed(termios_p, speed);
}
#endif

static void
sighandler(int sig)
{
	scrunning = 0;
}


static speed_t
parsespeed(char *speed)
{
	long s;
	char *ep;
#if !defined(TERMIOS_SPEED_IS_INT)
	struct termios_speed *ts = termios_speeds;
#endif

	s = strtol(speed, &ep, 0);
	if (ep == speed || ep[0] != '\0') {
		warnx("Unable to parse speed \"%s\"", speed);
		return(B9600);
	}
#if defined(TERMIOS_SPEED_IS_INT)
	return s;
#else
	while(ts->speed != 0) {
		if (ts->speed == s)
			return ts->code;
		ts++;
	}
	warnx("Undefined speed \"%s\"", speed);
	return(B9600);
#endif
}


static int
parseparms(tcflag_t *c, char *p, int f, int m)
{
	if (strlen(p) != 3) {
		warnx("Invalid parameter specification \"%s\"", p);
		return 1;
	}
	*c &= ~CSIZE;
	switch(p[0]) {
		case '5':	*c |= CS5; break;
		case '6':	*c |= CS6; break;
		case '7':	*c |= CS7; break;
		case '8':	*c |= CS8; break;
		default:
			warnx("Invalid character size \"%c\": must be 5, 6, 7, or 8",
					p[0]);
			return 1;
	}
	switch(tolower(p[1])) {
		case 'e':	*c |= PARENB; *c &= ~PARODD; break;
		case 'n':	*c &= ~PARENB;               break;
		case 'o':	*c |= PARENB | PARODD;       break;
		default:
			warnx("Invalid parity \"%c\": must be E, N, or O", p[1]);
			return 1;
	}
	switch(p[2]) {
		case '1':	*c &= ~CSTOPB; break;
		case '2':	*c |= CSTOPB;  break;
		default:
			warnx("Invalid stop bit \"%c\": must be 1 or 2", p[2]);
			return 1;
	}
	*c &= ~CRTSCTS;
	if (f) *c |= CRTSCTS;
	*c |= CLOCAL;
	if (m) *c &= ~CLOCAL;
	return 0;
}


static void
printparms(struct termios *ti, char *tty)
{
	long sp = 0;
	char bits, parity, stops;
#if !defined(TERMIOS_SPEED_IS_INT)
	struct termios_speed *ts = termios_speeds;
	speed_t sc;
#endif

#if defined(TERMIOS_SPEED_IS_INT)
	sp = cfgetispeed(ti);
#else
	sc = cfgetispeed(ti);
	while (ts->speed != 0) {
		if (ts->code == sc) {
			sp = ts->speed;
			break;
		}
		ts++;
	}
#endif
	switch(ti->c_cflag & CSIZE) {
		case CS5: bits = '5'; break;
		case CS6: bits = '6'; break;
		case CS7: bits = '7'; break;
		case CS8: bits = '8'; break;
		default:
			bits ='?';
	}
	if (ti->c_cflag & PARENB) {
		parity = ti->c_cflag & PARODD ? 'O' : 'E';
	} else {
		parity = 'N';
	}
	stops = ti->c_cflag & CSTOPB ? '2' : '1';

	fprintf(stderr, "Connected to %s at %ld %c%c%c, modem status %s, %shardware handshake\n",
		tty, sp, bits, parity, stops,
		ti->c_cflag & CLOCAL ? "ignored" : "observed",
		ti->c_cflag & CRTSCTS ? "" : "no ");
}

static int
hex2dec(char c)
{
  switch(c)
    {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': return 10;
    case 'b': return 11;
    case 'c': return 12;
    case 'd': return 13;
    case 'e': return 14;
    case 'f': return 15;
    case 'A': return 10;
    case 'B': return 11;
    case 'C': return 12;
    case 'D': return 13;
    case 'E': return 14;
    case 'F': return 15;
    }
  return -1;
}

static int
loop(int sfd, int escchr, int msdelay)
{
	enum escapestates escapestate = ESCAPESTATE_WAITFOREC;
	unsigned char escapedigit;
	int i;
	char c;
#if defined(HAS_BROKEN_POLL)
	/* use select(2) */
	fd_set fds[2];

	FD_ZERO(fds+1);
	FD_SET(STDIN_FILENO, fds+1);
	FD_SET(sfd, fds+1);
	while (scrunning) {
		bcopy(fds+1, fds, sizeof(*fds));
		if ((i = select(sfd+1, fds, NULL, NULL, NULL)) < 0
				&& errno != EINTR) {
			warn("select()");
			return EX_OSERR;
		}
#else
	struct pollfd pfds[2];

	bzero(pfds, sizeof(pfds));
	pfds[0].fd = STDIN_FILENO;
	pfds[0].events = POLLIN;
	pfds[1].fd = sfd;
	pfds[1].events = POLLIN;
	while (scrunning) {
		if ((i = poll(pfds, sizeof(pfds)/sizeof(pfds[0]), -1)) < 0
				&& errno != EINTR) {
			warn("poll()");
			return EX_OSERR;
		}
		if ((pfds[0].revents | pfds[1].revents) & POLLNVAL) {
			warnx("poll() does not support devices");
			return EX_OSERR;
		}
		if (pfds[0].revents & (POLLERR|POLLHUP)) {
			read(STDIN_FILENO, &c, 1);
			warn("poll mask %04x read(tty)", pfds[0].revents);
			return(EX_OSERR);
		}
		if (pfds[1].revents & (POLLERR|POLLHUP)) {
			read(sfd, &c, 1);
			warn("poll mask %04x read(serial)", pfds[1].revents);
			return(EX_OSERR);
		}
#endif
#if defined(HAS_BROKEN_POLL)
		if (FD_ISSET(STDIN_FILENO, fds)) {
#else
		if (pfds[0].revents & POLLIN) {
#endif
			if ((i = read(STDIN_FILENO, &c, 1)) > 0) {
				switch (escapestate) {
					case ESCAPESTATE_WAITFORCR:
						if (c == '\r') {
							escapestate = ESCAPESTATE_WAITFOREC;
						}
						break;

					case ESCAPESTATE_WAITFOREC:
						if (escchr != -1 && ((unsigned char)c) == escchr) {
							escapestate = ESCAPESTATE_PROCESSCMD;
							continue;
						}
						if (c != '\r') {
							escapestate = ESCAPESTATE_WAITFORCR;
						}
						break;

					case ESCAPESTATE_PROCESSCMD:
						escapestate = ESCAPESTATE_WAITFORCR;
						switch (c) {
							case '.':
								scrunning = 0;
								continue;

							case 'b':
							case 'B':
								if(!qflag)
									fprintf(stderr, "->sending a break<-\r\n");
								tcsendbreak(sfd, 0);
								continue;

							case 'x':
							case 'X':
								escapestate = ESCAPESTATE_WAITFOR1STHEXDIGIT;
								continue;

							default:
								if (((unsigned char)c) != escchr) {
									write(sfd, &escchr, 1);
								}
						}
						break;

					case ESCAPESTATE_WAITFOR1STHEXDIGIT:
						if (isxdigit(c)) {
							escapedigit = hex2dec(c) * 16;
							escapestate = ESCAPESTATE_WAITFOR2NDHEXDIGIT;
						} else {
							escapestate = ESCAPESTATE_WAITFORCR;
							if(!qflag)
								fprintf(stderr, "->invalid hex digit '%c'<-\r\n", c);
						}
						continue;

					case ESCAPESTATE_WAITFOR2NDHEXDIGIT:
						escapestate = ESCAPESTATE_WAITFORCR;
						if(isxdigit(c)) {
							escapedigit += hex2dec(c);
							write(sfd, &escapedigit, 1);
							if(!qflag)
								fprintf(stderr, "->wrote 0x%02X character '%c'<-\r\n", escapedigit, isprint(escapedigit)?escapedigit:'.');
						} else {
							if(!qflag)
								fprintf(stderr, "->invalid hex digit '%c'<-\r\n", c);
						}
						continue;
				}
				i = write(sfd, &c, 1);
				if(c == '\n' && msdelay > 0)
					usleep(msdelay*1000);
			}
			if (i < 0) {
				warn("read/write");
				return(EX_OSERR);
			}
		}
#if defined(HAS_BROKEN_POLL)
		if (FD_ISSET(sfd, fds)) {
#else
		if (pfds[1].revents & POLLIN) {
#endif
			if ((i = read(sfd, &c, 1)) > 0) {
				i = write(STDOUT_FILENO, &c, 1);
			}
			if (i < 0) {
				warn("read/write");
				return(EX_OSERR);
			}
		}
	}
	return(0);
}


static void
modemcontrol(int sfd, int dtr)
{
#if defined(TIOCSDTR)
	ioctl(sfd, dtr ? TIOCSDTR : TIOCCDTR);
#elif defined(TIOCMSET) && defined(TIOCM_DTR)
	int flags;
	if (ioctl(sfd, TIOCMGET, &flags) >= 0) {
		if (dtr)
			flags |= TIOCM_DTR;
		else
			flags &= ~TIOCM_DTR;
		ioctl(sfd, TIOCMSET, &flags);
	}
#endif
}


static void
usage(void)
{
	fprintf(stderr, "Connect to a serial device, using this system as a console. Version %s.\n"
			"usage:\tsc [-fmq] [-d ms] [-e escape] [-p parms] [-s speed] device\n"
			"\t-f: use hardware flow control (CRTSCTS)\n"
			"\t-m: use modem lines (!CLOCAL)\n"
			"\t-q: don't show connect, disconnect and escape action messages\n"
 			"\t-d: delay in milliseconds after each newline character\n"
			"\t-e: escape char or \"none\", default '~'\n"
			"\t-p: bits per char, parity, stop bits, default \"%s\"\n"
			"\t-s: speed, default \"%s\"\n"
			"\tdevice, default \"%s\"\n",
			SC_VERSION, DEFAULTPARMS, DEFAULTSPEED, DEFAULTDEVICE);
	fprintf(stderr, "escape actions are started with the 3 character combination: CR + ~ +\n"
		        "\t~ - send '~' character\n"
		        "\t. - disconnect\n"
		        "\tb - send break\n"
   		        "\tx<2 hex digits> - send decoded character\n");
#if defined(TERMIOS_SPEED_IS_INT)
	fprintf(stderr, "available speeds depend on device\n");
#else
	{
		struct termios_speed *ts = termios_speeds;

		fprintf(stderr, "available speeds: ");
		while (ts->speed != 0) {
			fprintf(stderr, "%ld ", ts->speed);
			ts++;
		}
		fprintf(stderr, "\n");
	}
#endif
	exit(EX_USAGE);
}

///////////////////////////////////////////////////////////////////////////////


/*
最原始的一个main函数，这个简单的可以预示着程序可以正常运行即可。
比如在终端输出一个hello word！

1.增加一个功能到现有的框架之中整个测试都在一个新的文件之中完成，实现函数
2.关于命令行参数 argc 和 argv的说明
argc 以空格为界技术命令行有几个参数从1开始
argv 指针数据,偏移量从0开始,分界还是空格
*/
int
main(int argc,char *argv[])
{
	int fd_uart1;
	
  printf("Hello World!\n");
  
  //-首先对接收到的命令进行解析,然后根据命令进行程序运行.
  if (parse_options(argc, argv) != 0)
		goto close;
#if 1
//-打印出输入命令
    int32_t i = 0;
	printf("argc: %d  \n",argc);
    for(i=0; i<argc; i++)
        DEBUG("argv[%d]:%s\n", i, argv[i]);
#endif
  
  //-首先对接收到的命令进行解析,然后根据命令进行程序运行.
  if (parse_options(argc, argv) != 0)
		goto close;
	//-下面首先进行系列初始化工作
	if(run_flag == 0)
	{//-下面进入正常模式,就是使用守护进程,脱离终端控制
		daemon_init();
	}
	
	//-开始的测试代码可以从这里开始
  fd_uart1 = uart1_sub(argc-1, &argv[1]);	//-测试串口功能

  if(test_branch == 2)
	calendar_sub(argc-1, &argv[1]);	//-临时测试用,实现读取时间/执行时间功能
  else if(test_branch == 3)
    sniffer_sub(argc-1, &argv[1]);	//-临时测试用,实现网络报文的抓取和过滤
  else if(test_branch == 4)
    thread_sub(argc-1, &argv[1]);	//-临时测试用,实现多线程的功能
  else if(test_branch == 5)
    mqtt_publish_sub(argc-1, &argv[1]);	//-临时测试用,实现MQTT通讯协议-发送
  else if(test_branch == 6)
    mqtt_subscribe_sub(argc-1, &argv[1]);	//-临时测试用,实现MQTT通讯协议-接收

  
  char buf[100] = {'0'}; 
  sprintf(buf, "%d", fd_uart1);
  f_debug(buf);

  //-下面进入程序的主循环部分
  while(_running)	//-程序一但运行起来就有周期执行的地方.
  {
  	
  	if(fd_uart1 >= 0)
  		uart_1_Main(fd_uart1);
  	
  }
  
close:  
  return 0;

}



int parse_options(int argc, char *argv[])
{
	int c;
	char *pLen;

	while ((c = getopt(argc, argv, "a:b:DTSXMR")) != -1) 
	{
		switch(c) 
		{
			case 'a':
				//-port_opts.bus_addr = strtoul(optarg, NULL, 0);
				break;

			case 'b':
				//-port_opts.baudRate = serial_get_baud(strtoul(optarg, NULL, 0));
				
				break;
			
			case 'D':
				run_flag = 1;				
				break;
			case 'T':
				test_branch = 2;				
				break;
			case 'S':
				test_branch = 3;				
				break;
			case 'X':
				test_branch = 4;				
				break;
			case 'M':
				test_branch = 5;				
				break;
			case 'R':
				test_branch = 6;				
				break;		

			case 'h':
				usage();
				exit(0);	//?这里值得思考

      default: 
				break;
		}
	}
		

	//-if (!wr && verify) {
	//-	fprintf(stderr, "ERROR: Invalid usage, -v is only valid when writing\n");
	//-	show_help(argv[0]);
	//-	return 1;	//-出现了错误,程序需要终止了,返回1
	//-}

	return 0;	//-正常结束,可以继续运行等于0
}






///////////////////////////////////////////////////////////////////////////////
int
main_one(int argc, char **argv)
{
	int escchr = '~';
	char *tty = DEFAULTDEVICE;
	char *speed = DEFAULTSPEED;
	char *parms = DEFAULTPARMS;
	int fflag = 0;
	int mflag = 0;
	int sfd = -1;
	char buffer[PATH_MAX+1];
	struct termios serialti, consoleti, tempti;
	int ec = 0;
	int msdelay = 0;
	int i;
	char c;

	while ((c = getopt(argc, argv, "d:e:fhmp:qs:?")) != -1) {
		switch (c) {
			case 'd':
				msdelay=atoi(optarg);
				if(msdelay <= 0)
					fprintf(stderr, "warning: ignoring negative or zero delay: %i\n", msdelay);
				break;
			case 'e':
				if (strcmp(optarg, "none") == 0) {
					escchr = -1;
				} else if (strlen(optarg) == 1) {
					escchr = (unsigned char)optarg[0];
				} else if (strlen(optarg) == 2 && optarg[0] == '^' &&
						toupper(optarg[1]) >= '@' && toupper(optarg[1]) <= '_') {
					escchr = toupper(optarg[1]) & 0x1f;
				} else {
					errx(EX_USAGE, "Invalid escape character \"%s\"", optarg);
				}
				break;
			case 'f':
				fflag = 1;
				break;
			case 'm':
				mflag = 1;
				break;
			case 'p':
				parms = optarg;
				break;
			case 'q':
				qflag = 1;
			case 's':
				speed = optarg;
				break;
			case 'h':
			case '?':
			default:
				usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc == 1) {
		tty = argv[0];
	}
	if (argc > 1) {
		usage();
	}

	if (strchr(tty, '/') == NULL) {
		if (strlen(path_dev) + strlen(tty) > PATH_MAX) {
			errx(EX_USAGE, "Device name \"%s\" is too long.", tty);
		}
		bcopy(path_dev, buffer, strlen(path_dev)+1);
		bcopy(tty, buffer+strlen(path_dev), strlen(tty)+1);
		tty = buffer;
	}
	sfd = open(tty, O_RDWR);
	if (sfd < 0) {
		err(EX_OSERR, "open %s", tty);
	}
	/* save tty configuration */
	if (tcgetattr(STDIN_FILENO, &consoleti)) {
		close(sfd);
		err(EX_OSERR, "tcgetattr() tty");
	}
	/* save serial port configuration */
	if (tcgetattr(sfd, &serialti)) {
		close(sfd);
		err(EX_OSERR, "tcgetattr(%s)", tty);
	}
	/* configure serial port */
	bcopy(&serialti, &tempti, sizeof(tempti));
	cfmakeraw(&tempti);
	tempti.c_cc[VMIN] = 1;
	tempti.c_cc[VTIME] = 0;
	if (cfsetspeed(&tempti, parsespeed(speed))) {
		ec = EX_OSERR;
		warn("cfsetspeed(%s)", tty);
		goto error;
	}
	if (parseparms(&tempti.c_cflag, parms, fflag, mflag)) {
		ec = EX_USAGE;
		goto error;
	}
	if (tcsetattr(sfd, TCSANOW, &tempti)) {
		ec = EX_OSERR;
		warn("tcsetattr(%s)", tty);
		goto error;
	}
	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGQUIT, sighandler);
	signal(SIGTERM, sighandler);

	if (!qflag) {
		/* re-read serial port configuration */
		if (tcgetattr(sfd, &tempti)) {
			close(sfd);
			err(EX_OSERR, "tcgetattr(%s)", tty);
		}
		printparms(&tempti, tty);
		fflush(stderr);
	}
	/* put tty into raw mode */
	i = fcntl(STDIN_FILENO, F_GETFL);
	if (i == -1 || fcntl(STDIN_FILENO, F_SETFL, i | O_NONBLOCK)) {
		close(sfd);
		err(EX_OSERR, "fcntl() tty");
	}
	bcopy(&consoleti, &tempti, sizeof(tempti));
	cfmakeraw(&tempti);
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &tempti)) {
		ec = EX_OSERR;
		warn("tcsetattr() tty");
		goto error;
	}
	modemcontrol(sfd, 1);

	ec = loop(sfd, escchr, msdelay);

error:
	if (sfd >= 0) {
		modemcontrol(sfd, 0);
		tcsetattr(sfd, TCSAFLUSH, &serialti);
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &consoleti);
		close(sfd);
	}
	fprintf(stderr, "\n");
	if (!qflag) fprintf(stderr, "Connection closed.\n");
	return ec;
}

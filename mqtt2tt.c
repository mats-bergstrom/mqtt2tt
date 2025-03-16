/*                               -*- Mode: C -*- 
 * Copyright (C) 2023, Mats Bergstrom
 * $Id$
 * 
 * File name       : mqtt2tt.c
 * Description     : Sends data to ticktockdb using http
 * 
 * Author          : Mats Bergstrom
 * Created On      : Sun Dec 31 12:16:05 2023
 * 
 * Last Modified By: Mats Bergstrom
 * Last Modified On: Sun Mar 16 11:29:59 2025
 * Update Count    : 96
 */


/*
 * Topics logged:
 *
 * hus/test		: # <payload>
 * han/raw		: not logged.
 * han/#		: H <topic> <payload>
 * hus/vatten/abs	: V <payload>
 * hus/fv/abs		: F <payload>
 * hus/TH/#		: T <payload>
 *
 */


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <mosquitto.h>



char* mqtt_broker = 0;
int   mqtt_port   = -1;

char* ttdb_host = 0;
int   ttdb_port = -1;

char* mqttid = 0;

#define PUT_BUF_MAX (256)

char put_buf[PUT_BUF_MAX];
unsigned put_buf_len;


int opt_n = 0;
int opt_v = 0;


typedef struct {
    const char* topic;
    const char* metric;
    const char* tagval;
} trans_t;

#define MAX_TOPIC	(256)
unsigned n_ttab = 0;
trans_t ttab[ MAX_TOPIC ] = { {0,0,0} };

#define MAX_SUBSCRIBE	(32)
unsigned n_sub_str = 0;
char* sub_str[ MAX_SUBSCRIBE ] = { 0 };

#if 0
/*
 * <topic> <val>	=> put <metric> <time> <val> <tag-val>
 */
trans_t ttab[] = {
		  {"han/E/Ut",		"E",	"dir=ut" },
		  {"han/E/In",		"E",	"dir=in" },
		  
		  {"han/P/A/Net",	"P",	"dir=net fas=A" },
		  {"han/P/1/Net",	"P",	"dir=net fas=1" },
		  {"han/P/2/Net",	"P",	"dir=net fas=2" },
		  {"han/P/3/Net",	"P",	"dir=net fas=3" },

		  {"han/U/1",		"U",	"fas=1" },
		  {"han/U/2",		"U",	"fas=2" },
		  {"han/U/3",		"U",	"fas=3" },

		  {"han/I/1",		"I",	"fas=1" },
		  {"han/I/2",		"I",	"fas=2" },
		  {"han/I/3",		"I",	"fas=3" },

		  {"hus/TH00/T",	"temp",	"loc=vrum" },
		  {"hus/TH00/H",	"hum",	"loc=vrum" },
		  {"hus/TH01/T",	"temp",	"loc=tvst" },
		  {"hus/TH01/H",	"hum",	"loc=tvst" },
		  {"hus/T00/0/T",	"temp",	"loc=bod" },
		  {"hus/T00/1/T",	"temp",	"loc=ute" },
		  {0,0,0}
};
#endif

int
match(const char* topic)
{
    int i = 0;
    while( ttab[i].topic ) {
	if ( !strcmp(ttab[i].topic,topic) )
	    return i;
	++i;
    }
    return -1;
}

/* ************************************************************************** */

#define BUF_SIZE 8192

int		tt_fd = -1;
unsigned	tt_open_failed_ctr = 0;

int
tcp_setup()
{
    if (opt_n) return 0;

    fprintf(stderr,"Opening socket...\n");

    tt_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tt_fd < 0)
    {
        fprintf(stderr, "socket error: %d\n", errno);
        return 1;
    }

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    if (inet_pton(AF_INET, ttdb_host, &addr.sin_addr) <= 0)
    {
        fprintf(stderr, "invalid host: %s\n", ttdb_host);
        return 2;
    }
    addr.sin_port = htons(ttdb_port);

    if (connect(tt_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0)
    {
        fprintf(stderr, "connect error: %d\n", errno);
        return 3;
    }

    fprintf(stderr,"socket OK\n");

    return 0;
}

void
tcp_cleanup()
{
    if (tt_fd >= 0)
    {
        close(tt_fd);
        tt_fd = -1;
    }
}

int
tcp_post(const char *body)
{
    // trim first
    while ((*body == ' ') || (*body == '\r') || (*body == '\n')) body++;

    char buff[BUF_SIZE + 512];
    int length = strlen(body);
    int sent_total = 0;

    if (length <= 4) return 0;

    // send POST request

    int n =
	snprintf(buff, sizeof(buff),
        "POST /api/put HTTP/1.1\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: keep-alive\r\n\r\n%s\r\n",
        length+2, body);

    length = strlen(buff);
    if ( n != length ) {
	printf("Lengths differ: %d, %d",n,length);
    }
    if ( opt_n )
	return 0;
    
    while (length > 0)
    {
        int sent = send(tt_fd, &buff[sent_total], length, 0);

        if (sent == -1)
        {
            fprintf(stderr, "tcp_post failed: %d\n", errno);
            return 1;
        }

        length -= sent;
        sent_total += sent;
    }

    if (0)
	printf("send: %d\n",sent_total);
    
    // receive response
    int ret = recv(tt_fd, buff, sizeof(buff), 0);
    if(0)
	printf("recv=%d\n",ret);
    if (ret == -1)
    {
        fprintf(stderr, "recv() failed: %d\n", errno);
        return 2;
    }
    if (0)
	printf(">>%s<<\n\n",buff);

    return 0;
}




/* ************************************************************************** */


void
mqtt_subscribe(struct mosquitto *mqc) {
    int n;
    for ( n = 0; n < n_sub_str; ++n ) {
	printf(".. Subscribe \"%s\"\n", sub_str[n]);
	mosquitto_subscribe(mqc, NULL, sub_str[n], 0);
    }
}


void
connect_callback(struct mosquitto *mqc, void *obj, int result)
{
    printf("Connected: %d\n", result);
    mqtt_subscribe(mqc);
}


void
disconnect_callback(struct mosquitto *mqc, void *obj, int result)
{
    printf("Disonnected: %d\n", result);
}


void
message_callback(struct mosquitto *mqc, void *obj,
		 const struct mosquitto_message *msg)
{
    const char*    topic = msg->topic;
    const char*    pload = msg->payload;
    struct timespec ts;

    int i;
    int t;

    do {

	/* Try to reopen the tt connection, if it is closed. */
	if ( tt_fd < 0 ) {
	    i = tcp_setup();
	    if ( i ) {
		fprintf(stderr,"Failed to open tcp \n");
		++tt_open_failed_ctr;
		if ( tt_open_failed_ctr > 20 ) {
		    fprintf(stderr,
			    "Too many attempts to open TCP, quitting!\n");
		    exit( EXIT_FAILURE );
		}
		return;
	    }
	    tt_open_failed_ctr = 0;
	}
	
	t = match(topic);
	if ( t < 0 ) {
	    static int bad_count = 25;
	    if ( bad_count ) {
		printf("Ignored topic(%d): %s\n",bad_count,topic);
		--bad_count;
	    }
	    break;
	}

	i =  clock_gettime(CLOCK_REALTIME, &ts);
	if ( i ) {
	    perror("clock_gettime: ");
	    exit( EXIT_FAILURE );
	}

	put_buf_len = 0;
	sprintf(put_buf,
		"put %s %ld %s %s",
		ttab[t].metric,
		ts.tv_sec,
		pload,
		ttab[t].tagval);

	/* Dry run, just print to stdout */
	if ( opt_v )
	    printf("%s\r\n",put_buf);
	
	if ( opt_n )
	    break;

	i = tcp_post( put_buf );
	if ( i ) {
	    /* post failed. */
	    /* Try to recover, by closing the connection and reopen */
	    tcp_cleanup();
	}
	
    } while(0);
    
}


/* ************************************************************************** */

struct mosquitto* mqc = 0;		/* mosquitto client */

void
mq_init()
{
    int i;
    i = mosquitto_lib_init();
    if ( i != MOSQ_ERR_SUCCESS) {
	perror("mosquitto_lib_init: ");
	exit( EXIT_FAILURE );
    }
    
    mqc = mosquitto_new( mqttid, true, 0);
    if ( !mqc ) {
	perror("mosquitto_new: ");
	exit( EXIT_FAILURE );
    }

}


void
mq_fini()
{
    int i;

    if ( mqc ) {
	mosquitto_destroy(mqc);
	mqc = 0;
    }

    i = mosquitto_lib_cleanup();
    if ( i != MOSQ_ERR_SUCCESS) {
	perror("mosquitto_lib_cleanup: ");
	exit( EXIT_FAILURE );
    }

    
}

/* ************************************************************************** */

#define FBUF_MAX (512)



int
parse_broker(char* b, char* c, char* d) /* broker <s> host <s> <port> */
{

    if ( !b || !c || d )
	return -1;

    if ( mqtt_broker )
	free( mqtt_broker );

    mqtt_broker = strdup(b);

    mqtt_port = atoi( c );

    printf("Using broker: %s %d\n", mqtt_broker, mqtt_port );
    return 0;
}

int
parse_ttdb(char* b, char* c, char* d)/* ttdb <s> host <s> <port> */
{
    if ( !b || !c || d )
	return -1;

    if ( ttdb_host ) 
	free( ttdb_host );
    ttdb_host = strdup(b);
    
    ttdb_port = atoi( c );

    printf("Using ttdb: %s %d\n", ttdb_host, ttdb_port );
    return 0;
}

int
parse_topic(char* b, char* c, char* d)
{
    if ( !b || !c || !d || !*b || !*c || !*d )
	return -1;
    
    if ( n_ttab >= MAX_TOPIC-2 ) {
	printf("Too many topics!\n");
	exit( EXIT_FAILURE );
    }

    ttab[ n_ttab ].topic  = strdup(b);
    ttab[ n_ttab ].metric = strdup(c);
    ttab[ n_ttab ].tagval = strdup(d);

    if ( !ttab[ n_ttab ].topic ||
	 !ttab[ n_ttab ].metric ||
	 !ttab[ n_ttab ].tagval ) {
	printf("Failed to create topic!\n");
	exit( EXIT_FAILURE );
    }
    ++n_ttab;

    ttab[ n_ttab ].topic  = 0;
    ttab[ n_ttab ].metric = 0;
    ttab[ n_ttab ].tagval = 0;

    printf("Adding topic: \"%s\" \"%s\" \"%s\"\n",b,c,d);

    return 0;
}

int
parse_subscribe(char* b, char* c, char* d) /* subscribe <topic> */
{
    char* s = b;
    if ( !b || c || d || !*b )
	return -1;

    /* Replace any '*' with '#'  */
    while ( *s ) {
	if ( *s == '*' )
	    *s = '#';
	++s;
    }
    
    if ( n_sub_str >= MAX_SUBSCRIBE ) {
	printf("Too many subscribe\n");
	exit( EXIT_FAILURE );
    }

    sub_str[ n_sub_str ] = strdup(b);
    if ( !sub_str[ n_sub_str ] ) {
	printf("Failed to add subscribe\n");
	exit( EXIT_FAILURE );
    }


    ++n_sub_str;

    sub_str[ n_sub_str ] = 0;

    printf("Adding subscription: \"%s\"\n",sub_str[ n_sub_str-1 ]);

    return 0;
}

int
parse_mqttid(char* b, char* c, char* d)
{
    if ( !b || c || d || !*b )
	return -1;

    if ( mqttid )
	free( mqttid );
    mqttid = strdup(b);

    printf("mqttid: \"%s\"\n",mqttid);

    return 0;
}


#define SKIP_WS(s)do{while ( *s && isspace(*s) )++s;}while(0)
#define SKIP_BS(s)do{while ( *s && !isspace(*s) )++s;}while(0)

int
parse_line(char* s)
{
    char* a = 0;			/* points at broker,ttdb,topic */
    char* b = 0;			/* points at 2'nd part */
    char* c = 0;			/* points at 3'rd part */
    char* d = 0;			/* points at 4'th part */

    do {
	/* Remove initial space and quick return on comment line. */
	SKIP_WS(s);
	if ( !*s ) break;
	if ( *s == '#' ) break;

	a = s;				/* Start of part1 */

	SKIP_BS(s);
        if ( !*s ) break;
	*s = 0;	++s;

	SKIP_WS(s);
	if ( !*s ) break;

	b = s;				/* Start of part 2 */

	SKIP_BS(s);
        if ( !*s ) break;
	*s = 0; ++s;

	SKIP_WS(s);
	if ( !*s ) break;

	c = s;				/* Start of part 3 */

	SKIP_BS(s);
        if ( !*s ) break;
	*s = 0; ++s;

	SKIP_WS(s);
	if ( !*s ) break;

	d = s;				/* Start of part 4 */

	while( *s && (*s != '\n') )	/* Terminate at eol */
	    ++s;
	if ( !*s )
	    break;
	*s = 0;

    } while(0);

    if ( !a )
	return 0;

    if ( a ) printf("a=\"%s\"\n",a);
    if ( b ) printf("b=\"%s\"\n",b);
    if ( c ) printf("c=\"%s\"\n",c);
    if ( d ) printf("d=\"%s\"\n",d);

    if ( !strncmp("broker",a,6) ) return parse_broker(b,c,d);
    if ( !strncmp("ttdb",  a,4) ) return parse_ttdb(b,c,d);
    if ( !strncmp("topic", a,5) ) return parse_topic(b,c,d);
    if ( !strncmp("subscribe", a,9) ) return parse_subscribe(b,c,d);
    if ( !strncmp("mqttid", a,6) ) return parse_mqttid(b,c,d);

    return -1;
}

void
read_cfg(const char* cfg)
{
    FILE* f = fopen(cfg,"r");
    unsigned int n = 0;
    int i;
    while ( f && !feof(f) ) {
	char fbuf[FBUF_MAX];
	char* s = fgets( fbuf, FBUF_MAX-2, f );
	if ( !s )
	    break;
	++n;
	printf("parsing: %d\n",n);
	i = parse_line(s);
	if ( i ) {
	    printf("Bad config line: %u, ignored\n", n);
	}
    }
    fclose(f);
}


/* ************************************************************************** */

int
main(int argc, const char** argv)
{
    setbuf(stdout, 0);
    printf("Starting.\n");

    --argc;
    ++argv;
    if ( !argc ) {
	fprintf(stderr,"Config file name missing.\n");
	fprintf(stderr,"Usage: mqtt2tt <config-file>\n");
	exit( EXIT_FAILURE );
    }
    read_cfg( *argv );

    tcp_setup();
    
    mq_init();

    mosquitto_connect_callback_set(mqc, connect_callback);
    mosquitto_disconnect_callback_set(mqc, disconnect_callback);
    mosquitto_message_callback_set(mqc, message_callback);

    do {
	int i;
	i = mosquitto_connect(mqc, mqtt_broker, mqtt_port, 60);
	if ( i != MOSQ_ERR_SUCCESS) {
	    perror("mosquitto_connect: ");
	    exit( EXIT_FAILURE );
	}

	
	do {
	    printf("Entering loop.\n");
	    i = mosquitto_loop_forever(mqc, 60000, 1);
	    while ( i != MOSQ_ERR_SUCCESS ) {
		printf("Disconnected.\n");
		sleep(10);
		i = mosquitto_reconnect(mqc);
	    }
	} while(1);

	
    } while(1);


    mq_fini();
    tcp_cleanup();

    return EXIT_SUCCESS;
}

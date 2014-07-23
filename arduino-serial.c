#include <stdio.h>    // Standard input/output definitions 
#include <stdlib.h> 
#include <string.h>   // String function definitions 
#include <unistd.h>   // for usleep()
#include <getopt.h>
#include <mysql/mysql.h>
#include "arduino-serial-lib.h"
#include <fcntl.h>
#include <unistd.h>

//
void usage(void)
{
    printf("Usage: arduino-serial -b <bps> -p <serialport> [OPTIONS]\n"
    "\n"
    "Options:\n"
    "  -h, --help                 Print this help message\n"
    "  -b, --baud=baudrate        Baudrate (bps) of Arduino (default 115200)\n" 
    "  -p, --port=serialport      Serial port Arduino is connected to\n"
    "  -s, --send=string          Send string to Arduino\n"
    "  -S, --sendline=string      Send string with newline to Arduino\n"
    "  -r, --receive              Receive string from Arduino & print it out\n"
    "  -m,  --mysql                Send arduino ouput to mysql server\n"
    "  -n  --num=num              Send a number as a single byte\n"
    "  -F  --flush                Flush serial port buffers for fresh reading\n"
    "  -d  --delay=millis         Delay for specified milliseconds\n"
    "  -e  --eolchar=char         Specify EOL char for reads (default '\\n')\n"
    "  -t  --timeout=millis       Timeout for reads in millisecs (default 5000)\n"
    "  -q  --quiet                Don't print out as much info\n"
    "\n"
    "Note: Order is important. Set '-b' baudrate before opening port'-p'. \n"
    "      Used to make series of actions: '-d 2000 -s hello -d 100 -r' \n"
    "      means 'wait 2secs, send 'hello', wait 100msec, get reply'\n"
    "\n");
    exit(EXIT_SUCCESS);
}

//
void error(char* msg)
{
    fprintf(stderr, "%s\n",msg);
    exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) 
{
    const int buf_max = 256;
    int fd = -1;
    char serialport[buf_max],buf[buf_max],db_buf[buf_max],stat_db[buf_max],code_db[buf_max];
    int baudrate = 115200;  // default
    char quiet=0;
    char eolchar = '\n';
    int timeout = 5000;
    int rc,n;
    FILE *fp;
    if (argc==1) {
        usage();
    }

    /* parse options */
    int option_index = 0, opt;
    static struct option loptions[] = {
        {"help",       no_argument,       0, 'h'},
        {"mysql",      no_argument,       0, 'm'},
        {"port",       required_argument, 0, 'p'},
        {"baud",       required_argument, 0, 'b'},
        {"send",       required_argument, 0, 's'},
        {"sendline",   required_argument, 0, 'S'},
        {"receive",    no_argument,       0, 'r'},
        {"flush",      no_argument,       0, 'F'},
        {"num",        required_argument, 0, 'n'},
        {"delay",      required_argument, 0, 'd'},
        {"eolchar",    required_argument, 0, 'e'},
        {"timeout",    required_argument, 0, 't'},
        {"quiet",      no_argument,       0, 'q'},
        {NULL,         0,                 0, 0}
    };
    
    while(1) {
        opt = getopt_long (argc, argv, "hp:b:s:S:rmFn:d:qe:t:",
                           loptions, &option_index);
        if (opt==-1) break;
        switch (opt) {
        case '0': break;
        case 'q':
            quiet = 1;
            break;
        case 'e':
            eolchar = optarg[0];
            if(!quiet) printf("eolchar set to '%c'\n",eolchar);
            break;
        case 't':
            timeout = strtol(optarg,NULL,10);
            if( !quiet ) printf("timeout set to %d millisecs\n",timeout);
            break;
        case 'd':
            n = strtol(optarg,NULL,10);
            if( !quiet ) printf("sleep %d millisecs\n",n);
            usleep(n * 1000 ); // sleep milliseconds
            break;
        case 'h':
            usage();
            break;
        case 'b':
            baudrate = strtol(optarg,NULL,10);
            break;
        case 'p':
            if( fd!=-1 ) {
                serialport_close(fd);
                if(!quiet) printf("closed port %s\n",serialport);
            }
            strcpy(serialport,optarg); /* Cpoy the option from cmd (optarg) to serialport */
            fd = serialport_init(optarg, baudrate);
            if( fd==-1 ) error("couldn't open port");
            if(!quiet) printf("opened port %s\n",serialport);
            serialport_flush(fd);
            break;
        case 'n':
            if( fd == -1 ) error("serial port not opened");
            n = strtol(optarg, NULL, 10); // convert string to number
            rc = serialport_writebyte(fd, (uint8_t)n);
            if(rc==-1) error("error writing");
            break;
        case 'S':
        case 's':
            if( fd == -1 ) error("serial port not opened");
            sprintf(buf, (opt=='S' ? "%s\n" : "%s"), optarg);
            if( !quiet ) printf("send string:%s\n", buf);
            rc = serialport_write(fd, buf);
            if(rc==-1) error("error writing");
            break;
        case 'r':
            if( fd == -1 ) error("serial port not opened");
            memset(buf,0,buf_max);  // 
            serialport_read_until(fd, buf, eolchar, buf_max, timeout);
            if( !quiet ) printf("read string:");
            printf("%s\n", buf);
            fp = fopen("out.txt","a"); /* Append to out.txt */
            if (fp == NULL){printf("I couldn't open out.txt for writing.\n");
            exit(0);}
            fprintf(fp, "%s", buf); /*Write ouput in file*/
            fclose(fp);
	    break;
	case 'm':
            if( fd == -1 ) error("serial port not opened");
            memset(buf,0,buf_max);  //
            serialport_read_until(fd, buf, eolchar, buf_max, timeout);
	    sprintf(stat_db, "%c", buf[0]);/* Get first char from arduino ouput as status (1 or 0) in stat_db */
	    if( !quiet ) printf("%s\n", stat_db); /* Debugging purposes */
	    strncpy(code_db, buf+1,(strlen(buf)));/* Get arduino entire ouput without first char, as code_db */
	    //strncpy(code_db, buf+1,5); /* Reads only last 5 chars, without the status (1 or 0), as code_db */
	    if( !quiet ) printf("Modificat: %s\n", code_db); /* Debugging purposes */
		/*Connect to MYSQL and send data*/
   		MYSQL *conn;
   		//MYSQL_RES *res;
   		//MYSQL_ROW row;
   		char *server = "localhost";
   		char *user = "test";
   		char *password = "test"; /* set me first */
   		char *database = "pihome";
   		conn = mysql_init(NULL);
   		if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {/* Connect to database */
      		fprintf(stderr, "%s\n", mysql_error(conn));
      		exit(1);}
		sprintf(db_buf, "update pi_devices set status='%s' where code='%s';", stat_db, code_db); /* Puts entire query in a string*/
		if( !quiet ) printf("%s\n", db_buf);/* Debugging purposes */
   		if (mysql_query(conn, db_buf)) {/* send SQL query */
      		fprintf(stderr, "%s\n", mysql_error(conn));
      		exit(1);}
   		//res = mysql_use_result(conn);
   		//printf("MySQL Tables in mysql database:\n");/* output table name */
   		//while ((row = mysql_fetch_row(res)) != NULL)
      		//printf("%s \n", row[0]);
   		//mysql_free_result(res);/* close connection */
   		mysql_close(conn);
	    break;
        case 'F':
            if( fd == -1 ) error("serial port not opened");
            if( !quiet ) printf("flushing receive buffer\n");
            serialport_flush(fd);
            break;

        }
    }

    exit(EXIT_SUCCESS);    
} // end main
    

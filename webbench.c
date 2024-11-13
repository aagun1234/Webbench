/*
 * (C) Radim Kolar 1997-2004
 * This is free software, see GNU Public License version 2 for
 * details.
 *
 * Simple forking WWW Server benchmark:
 *
 * Usage:
 *   webbench --help
 *
 * Return codes:
 *    0 - sucess
 *    1 - benchmark failed (server is not on-line)
 *    2 - bad param
 *    3 - internal error, fork failed
 * 
 */ 
#include "socket.c"
#include <unistd.h>
#include <sys/param.h>
#include <rpc/types.h>
#include <getopt.h>
#include <strings.h>
#include <time.h>
#include <signal.h>

/* values */
volatile int timerexpired=0;
int speed=0;
int failed=0;
int resp20x=0;
int resp40x=0;
int resp50x=0;
int resp30x=0;
int respxxx=0;
int bytes=0;
/* globals */
int http10=1; /* 0 - http/0.9, 1 - http/1.0, 2 - http/1.1 */
/* Allow: GET, HEAD, OPTIONS, TRACE */
#define METHOD_GET 0
#define METHOD_HEAD 1
#define METHOD_OPTIONS 2
#define METHOD_TRACE 3
#define PROGRAM_VERSION "1.5.1"
int method=METHOD_GET;
int clients=1;
int force=0;
int debug=0;
int force_reload=0;
int proxyport=80;
char *proxyhost=NULL;
int benchtime=30;
int timeout=30;
/* internal */
int mypipe[2];
char host[MAXHOSTNAMELEN];
#define REQUEST_SIZE 2048
char request[REQUEST_SIZE];

static const struct option long_options[]=
{
 {"force",no_argument,&force,1},
 {"debug",no_argument,&debug,1},
 {"reload",no_argument,&force_reload,1},
 {"time",required_argument,NULL,'t'},
 {"timeout",required_argument,NULL,'o'},
 {"help",no_argument,NULL,'?'},
 {"http09",no_argument,NULL,'9'},
 {"http10",no_argument,NULL,'1'},
 {"http11",no_argument,NULL,'2'},
 {"get",no_argument,&method,METHOD_GET},
 {"head",no_argument,&method,METHOD_HEAD},
 {"options",no_argument,&method,METHOD_OPTIONS},
 {"trace",no_argument,&method,METHOD_TRACE},
 {"version",no_argument,NULL,'V'},
 {"proxy",required_argument,NULL,'p'},
 {"clients",required_argument,NULL,'c'},
 {NULL,0,NULL,0}
};

/* prototypes */
static void benchcore(const char* host,const int port, const char *request);
static int bench(void);
static void build_request(const char *url);

static void alarm_handler(int signal)
{
   timerexpired=1;
}	

static void usage(void)
{
   fprintf(stderr,
	"webbench [option]... URL\n"
	"  -f|--force               Don't wait for reply from server.\n"
	"  -d|--debug               Some debug output.\n"
	"  -r|--reload              Send reload request - Pragma: no-cache.\n"
	"  -t|--time <sec>          Run benchmark for <sec> seconds. Default 30.\n"
	"  -o|--timeout <sec>          Socket timeout <sec> seconds. Default 30.\n"
	"  -p|--proxy <server:port> Use proxy server for request.\n"
	"  -c|--clients <n>         Run <n> HTTP clients at once. Default one.\n"
	"  -9|--http09              Use HTTP/0.9 style requests.\n"
	"  -1|--http10              Use HTTP/1.0 protocol.\n"
	"  -2|--http11              Use HTTP/1.1 protocol.\n"
	"  --get                    Use GET request method.\n"
	"  --head                   Use HEAD request method.\n"
	"  --options                Use OPTIONS request method.\n"
	"  --trace                  Use TRACE request method.\n"
	"  -?|-h|--help             This information.\n"
	"  -V|--version             Display program version.\n"
	);
};
int main(int argc, char *argv[])
{
 int opt=0;
 int options_index=0;
 char *tmp=NULL;

 if(argc==1)
 {
	  usage();
          return 2;
 } 

 while((opt=getopt_long(argc,argv,"912Vfdrt:p:c:o:?h",long_options,&options_index))!=EOF )
 {
  switch(opt)
  {
   case  0 : break;
   case 'f': force=1;break;
   case 'd': debug=1;break;
   case 'r': force_reload=1;break; 
   case '9': http10=0;break;
   case '1': http10=1;break;
   case '2': http10=2;break;
   case 'V': printf(PROGRAM_VERSION"\n");exit(0);
   case 't': benchtime=atoi(optarg);break;	     
   case 'o': timeout=atoi(optarg);break;	     
   case 'p': 
	     /* proxy server parsing server:port */
	     tmp=strrchr(optarg,':');
	     proxyhost=optarg;
	     if(tmp==NULL)
	     {
		     break;
	     }
	     if(tmp==optarg)
	     {
		     fprintf(stderr,"Error in option --proxy %s: Missing hostname.\n",optarg);
		     return 2;
	     }
	     if(tmp==optarg+strlen(optarg)-1)
	     {
		     fprintf(stderr,"Error in option --proxy %s Port number is missing.\n",optarg);
		     return 2;
	     }
	     *tmp='\0';
	     proxyport=atoi(tmp+1);break;
   case ':':
   case 'h':
   case '?': usage();return 2;break;
   case 'c': clients=atoi(optarg);break;
  }
 }
 
 if(optind==argc) {
                      fprintf(stderr,"webbench: Missing URL!\n");
		      usage();
		      return 2;
                    }

 if(clients==0) clients=1;
 if(benchtime==0) benchtime=60;
 /* Copyright */
 fprintf(stderr,"Webbench - Simple Web Benchmark "PROGRAM_VERSION" mod by minuteman\n"
	 "Copyright (c) Radim Kolar 1997-2004, GPL Open Source Software.\n"
	 );
 build_request(argv[optind]);
 /* print bench info */
 printf("\nBenchmarking: ");
 switch(method)
 {
	 case METHOD_GET:
	 default:
		 printf("GET");break;
	 case METHOD_OPTIONS:
		 printf("OPTIONS");break;
	 case METHOD_HEAD:
		 printf("HEAD");break;
	 case METHOD_TRACE:
		 printf("TRACE");break;
 }
 printf(" %s",argv[optind]);
 switch(http10)
 {
	 case 0: printf(" (using HTTP/0.9)");break;
	 case 2: printf(" (using HTTP/1.1)");break;
 }
 printf("\n");
 if(clients==1) printf("1 client");
 else
   printf("%d clients",clients);

 printf(", running %d sec, socket timeout %d sec", benchtime,timeout);
 if(force) printf(", early socket close");
 if(debug) printf(", some debug error output");
 if(proxyhost!=NULL) printf(", via proxy server %s:%d",proxyhost,proxyport);
 if(force_reload) printf(", forcing reload");
 printf(".\n");
 return bench();
}

void build_request(const char *url)
{
  char tmp[10];
  int i;

  bzero(host,MAXHOSTNAMELEN);
  bzero(request,REQUEST_SIZE);

  if(force_reload && proxyhost!=NULL && http10<1) http10=1;
  if(method==METHOD_HEAD && http10<1) http10=1;
  if(method==METHOD_OPTIONS && http10<2) http10=2;
  if(method==METHOD_TRACE && http10<2) http10=2;

  switch(method)
  {
	  default:
	  case METHOD_GET: strcpy(request,"GET");break;
	  case METHOD_HEAD: strcpy(request,"HEAD");break;
	  case METHOD_OPTIONS: strcpy(request,"OPTIONS");break;
	  case METHOD_TRACE: strcpy(request,"TRACE");break;
  }
		  
  strcat(request," ");

  if(NULL==strstr(url,"://"))
  {
	  fprintf(stderr, "\n%s: is not a valid URL.\n",url);
	  exit(2);
  }
  if(strlen(url)>1500)
  {
         fprintf(stderr,"URL is too long.\n");
	 exit(2);
  }
  if(proxyhost==NULL)
	   if (0!=strncasecmp("http://",url,7)) 
	   { fprintf(stderr,"\nOnly HTTP protocol is directly supported, set --proxy for others.\n");
             exit(2);
           }
  /* protocol/host delimiter */
  i=strstr(url,"://")-url+3;
  /* printf("%d\n",i); */

  if(strchr(url+i,'/')==NULL) {
                                fprintf(stderr,"\nInvalid URL syntax - hostname don't ends with '/'.\n");
                                exit(2);
                              }
  if(proxyhost==NULL)
  {
   /* get port from hostname */
   if(index(url+i,':')!=NULL &&
      index(url+i,':')<index(url+i,'/'))
   {
	   strncpy(host,url+i,strchr(url+i,':')-url-i);
	   bzero(tmp,10);
	   strncpy(tmp,index(url+i,':')+1,strchr(url+i,'/')-index(url+i,':')-1);
	   /* printf("tmp=%s\n",tmp); */
	   proxyport=atoi(tmp);
	   if(proxyport==0) proxyport=80;
   } else
   {
     strncpy(host,url+i,strcspn(url+i,"/"));
   }
   // printf("Host=%s\n",host);
   strcat(request+strlen(request),url+i+strcspn(url+i,"/"));
  } else
  {
   // printf("ProxyHost=%s\nProxyPort=%d\n",proxyhost,proxyport);
   strcat(request,url);
  }
  if(http10==1)
	  strcat(request," HTTP/1.0");
  else if (http10==2)
	  strcat(request," HTTP/1.1");
  strcat(request,"\r\n");
  if(http10>0)
	  strcat(request,"User-Agent: WebBench "PROGRAM_VERSION"\r\n");
  if(proxyhost==NULL && http10>0)
  {
	  strcat(request,"Host: ");
	  strcat(request,host);
	  strcat(request,"\r\n");
  }
  if(force_reload && proxyhost!=NULL)
  {
	  strcat(request,"Pragma: no-cache\r\n");
  }
  if(http10>1)
	  strcat(request,"Connection: close\r\n");
  /* add empty line at end */
  if(http10>0) strcat(request,"\r\n"); 
  // printf("Req=%s\n",request);
}

/* vraci system rc error kod */
static int bench(void)
{
  int i,j,k,l,m,n,o,p;	
  pid_t pid=0;
  FILE *f;

  /* check avaibility of target server */
  fprintf(stderr,"\nTesting connect to server ...\n");
  i=Socket(proxyhost==NULL?host:proxyhost,proxyport,timeout);
  if(i<0) { 
	   fprintf(stderr,"\nConnect to server failed. Aborting benchmark.\n");
           return 1;
         }
  fprintf(stderr,"Success.\n");  
  close(i);
  /* create pipe */
  if(pipe(mypipe))
  {
	  perror("pipe failed.");
	  return 3;
  }

  /* not needed, since we have alarm() in childrens */
  /* wait 4 next system clock tick */
  /*
  cas=time(NULL);
  while(time(NULL)==cas)
        sched_yield();
  */

  /* fork childs */
  fprintf(stderr,"Forking clients and runing webbench ... ");
  for(i=0;i<clients;i++)
  {
	   pid=fork();
	   
	   if(pid <= (pid_t) 0)
	   {
		   /* child process or error*/
	           sleep(1); /* make childs faster */
		   break;
	   }
  }
 
  
  if( pid< (pid_t) 0)
  {
          fprintf(stderr,"\nproblems forking worker no. %d\n",i);
	  perror("fork failed.");
	  return 3;
  }

  if(pid== (pid_t) 0)
  {
    /* I am a child */
    
    if(proxyhost==NULL)
      benchcore(host,proxyport,request);
         else
      benchcore(proxyhost,proxyport,request);

         /* write results to pipe */
	 f=fdopen(mypipe[1],"w");
	 if(f==NULL)
	 {
		 perror("open pipe for writing failed.");
		 return 3;
	 }
	 /* fprintf(stderr,"Child - %d %d\n",speed,failed); */
	 fprintf(f,"%d %d %d %d %d %d %d %d\n",speed,failed,bytes,resp20x,resp30x,resp40x,resp50x,respxxx);
	 fclose(f);
	 return 0;
  } else
  {
	  
	  f=fdopen(mypipe[0],"r");
	  if(f==NULL) 
	  {
		  perror("open pipe for reading failed.");
		  return 3;
	  }
	  setvbuf(f,NULL,_IONBF,0);
	  speed=0;
      failed=0;
      bytes=0;
      resp50x=0;
      respxxx=0;
      resp20x=0;
      resp30x=0;
      resp40x=0;
      
	  while(1)
	  {
		  pid=fscanf(f,"%d %d %d %d %d %d %d %d",&i,&j,&k,&l,&m,&n,&o,&p);
		  if(pid<2)
                  {
                       fprintf(stderr,"Some of our childrens died.\n");
                       break;
                  }
		  speed+=i;
		  failed+=j;
		  bytes+=k;
		  resp20x+=l;
		  resp30x+=m;
		  resp40x+=n;
		  resp50x+=o;
		  respxxx+=p;
		  /* fprintf(stderr,"*Knock* %d %d read=%d\n",speed,failed,pid); */
		  if(--clients==0) break;
	  }
	  fclose(f);

  printf("\nSpeed=%d pages/min, %d bytes/sec.\nRequests: %d susceed, %d failed.\n",
		  (int)((speed+failed)/(benchtime/60.0f)),
		  (int)(bytes/(float)benchtime),
		  speed,
		  failed);
  printf("Response Code:\n200=%d , 3XX=%d , 4XX=%d , 5XX=%d , Other=%d.\n", resp20x,resp30x,resp40x,resp50x,respxxx);
  }
  return i;
}

void benchcore(const char *host,const int port,const char *req)
{
 int rlen;
 char buf[1500],buf1[100],*ptr;
 int s,i,j;
 int buflen,cdone;
 int checking;
 struct sigaction sa;

 /* setup alarm signal handler */
 sa.sa_handler=alarm_handler;
 sa.sa_flags=0;
 if(sigaction(SIGALRM,&sa,NULL))
    exit(3);
 alarm(benchtime);

 rlen=strlen(req);
 nexttry:while(1)
 {
    if(timerexpired)
    {
       if(failed>0)
       {
          
          failed--;
          //if(debug) fprintf(stderr,"Correcting failed by signal\n");
       }
       return;
    }
    s=Socket(host,port,timeout);                          
	    checking=1;
	    ptr=buf1;
	    cdone=0;
	    buflen=0;
    if(s<0) { failed++;continue;} 
    if(rlen!=write(s,req,rlen)) {if(debug) fprintf(stderr,"Request send failed.\n");failed++;close(s);continue;}
    if(http10==0) 
	    if(shutdown(s,1)) { failed++;close(s);continue;}
    if(force==0) 
    {
            /* read all available data from socket */
	    while(1)
	    {
          if(timerexpired) break; 
	      i=read(s,buf,1499);
              /* fprintf(stderr,"%d\n",i); */
       
	      if(i<0) 
          { 
                 failed++;
                 close(s);
                 goto nexttry;
          }
	      else
	      {
		     if(i==0)
		     {
		         if(cdone==0)
		         {
		            if(debug)
		                fprintf(stderr,"Other(NO respCode): %s , buflen: %d\n",buf1,buflen);
		            respxxx++;
		            speed++;
		            if(buflen==0 && debug)
		                fprintf(stderr,"Empty Response\n");
		         }
		         
		         break;
		     }
		     else
		     {
               bytes+=i;
               buf[i]=0;
		       if(checking)
               {
                 //strncpy(ptr, buf,(1499-buflen));
                 if(cdone==0 && buflen<20)
                 {
                     for(j=0;j<i;j++)
                     {
                         buf1[buflen]=buf[j];
                         if(buf[j]=='\n' || buf[j]=='\r')
                         {
                             cdone=1;
                             buf1[buflen]=0;
                         }
                         buflen++;
                     }
                     //strncpy(ptr, buf, 20);
                     //ptr+=(i>=20?20:i);
                     //ptr[0]=0;
                     //buflen+=i;
                     
                 }
                 if(cdone)
                 {
                     if (strstr(buf1, "HTTP/") != NULL)
                     {
                         ptr=buf1+9;
                         
                         if (ptr[0]=='2' && ptr[1]=='0' && ptr[2]=='0' && ptr[3]==' '  && ptr[4]=='O' ) {
                             speed++;
                             resp20x++;
                         } else if (ptr[0]=='3') {
                             //if(debug) fprintf(stderr,"3XX: %s\n",ptr);
                             resp30x++;
                             speed++;
		                 } else if (ptr[0]=='4') {
		                     //if(debug) fprintf(stderr,"4XX: %s\n",ptr);
                             resp40x++;
                             speed++;
	                     } else if (ptr[0]=='5') {
	                         //if(debug) fprintf(stderr,"5XX: %s\n",ptr);
			                 resp50x++;
			                 speed++;
		                 } else {
		                     if(debug) fprintf(stderr,"Other(Code): %s\n",buf1);
		                     respxxx++;
		                     speed++;
		                 }
		             } else {
		                     if(debug) fprintf(stderr,"Other(NO HTTP): %s\n",buf1);
		                     respxxx++;
		                     speed++;
		             }
                     checking=0;
		         }  		       
               }
	         }
          }
        }
        close(s);
    }
    else
    {
       speed++; 
       if(close(s)) {if(debug) fprintf(stderr,"Close failed\n");failed++;continue;}
    }
 }
}

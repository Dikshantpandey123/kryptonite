#include<unistd.h>
#include<sys/types.h>
#include<linux/fcntl.h>
#include<pthread.h>
#include<stdio.h>
#include<bits/pthreadtypes.h>
#include<stdlib.h>
#include<pthread.h>
#include<errno.h>
#include<linux/types.h>
#include<malloc.h>

pthread_t thd1,thd2;
pthread_attr_t tha1;
int fd;

void* thread1_func(void *arg)
{
	char buff[10] = "Hello from User Space";
	int byte1,byte2,fd1;
	printf("Thread  writing \n");
	byte1=write(fd,buff,sizeof(buff));

	if(byte1==-1)
	{
		  perror("error in writing \n");
	       	  exit(1);
	}
	printf("No. of bytes written is %d\n",byte1);
}

void *thread2_func(void *arg)
{
	
	int byte1,byte2;
	char buff [10];
	printf("reading thread  \n");
        byte1=read(fd,buff,sizeof(buff));
        if(byte1==-1)
       {
	       perror("error in writing \n");
	       exit(1);
       }
       write(STDOUT_FILENO, buff, byte1); 	 
      //    printf("%s\n",buff);  
}

	
int main()
{
	int fd,ret,buf[1024],buf1;
	struct sched_param p;

	fd=open("/dev/custom_serial_dev0",O_RDWR);
	if(fd==-1)
	{
		perror("error in opening");
		exit(1);
	}
	//printf("value of fd is %d\n",fd);
	pthread_attr_init(&tha1);

	
//	ret=lseek(fd,0,SEEK_SET);

	ret=pthread_create(&thd1,&tha1,thread1_func,&fd);
	//printf("1...the number characters returned is %d\n",ret);

	if(ret)
	{
		perror("error in pthread_create");
	}
	
	ret=pthread_create(&thd2,&tha1,thread2_func,&fd);
	
	if(ret)
	{
		perror("error in pthread_create");
		exit(1);
	}
	
        
	pthread_join(thd1,NULL);
	pthread_join(thd2,NULL);

	return 0;
}


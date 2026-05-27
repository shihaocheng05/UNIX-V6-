#include <stdio.h>
#include<sys.h>

int main1()
{
	int ppid=getppid();
	printf("ppid is:%d\n",ppid);
	return 0;
}

#include <stdio.h>
#include <sys.h>

int main1()
{
	unsigned int b,c;
	int success=getpids(&b,&c);
	if(success!=0)
	{
		printf("getpids failed\n");
		return -1;
	}
	printf("pid is:%u,ppid is:%u\n",b,c);
	return 0;
}

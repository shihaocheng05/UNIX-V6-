#include <stdio.h>

int version=1;

main1()
{
	int a,b,result;
	a = 1;
	b = 2;
	result = sum(a,b);
	printf("result=%d\n",result);
	printf("the address of main1 = %0x\n",&main1);
	printf("the address of sum = %0x\n",&sum);
	printf("the address of a = %0x\n",&a);
	printf("the address of b = %0x\n",&b);
	printf("the address of result = %0x\n",&result);
	printf("the address of printf = %0x\n",&printf);
}

int sum(var1, var2)
{
	int count;
	version = 2;
	count = var1 + var2;
	printf("the address of count = %0x\n",&count);
	return(count);
}

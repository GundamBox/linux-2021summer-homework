#include <stdio.h>
#include <unistd.h>

int main()
{
    fork();

    printf("Hello world!n");
    sleep(600000);  // 停十分鐘

    return 0;
}
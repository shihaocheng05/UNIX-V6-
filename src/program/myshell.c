#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define NULL (void *)0
#define MAX_CMD_LEN 256
#define MAX_ARGS 16

// 解析函数
void parse(char *cmdLine, char **argv) {
    int i = 0;
    // 跳过开头空白
    while (*cmdLine == ' ' || *cmdLine == '\t') cmdLine++;
    // 分割参数
    while (*cmdLine != '\0' && i < MAX_ARGS - 1) {
        argv[i++] = cmdLine;
        // 跳过当前参数内容
        while (*cmdLine != ' ' && *cmdLine != '\t' && *cmdLine != '\0') cmdLine++;
        if (*cmdLine != '\0') *cmdLine++ = '\0'; // 替换分隔符为结束符
        // 跳过下一个空白
        while (*cmdLine == ' ' || *cmdLine == '\t') cmdLine++;
    }
    argv[i] = NULL;
}

int main1() {
    char cmdLine[MAX_CMD_LEN];
    char *argv[MAX_ARGS];
    int i;

    while (1) {
        // 1. 输出提示符
        printf("$ ");

        // 2. 读取命令（用scanf简化，注意：scanf("%s")会自动跳过空白，但不支持带空格的参数）
        gets(cmdLine);

        // 3. 解析命令行
        parse(cmdLine, argv);
        if (argv[0] == NULL) continue;

        // 4. 执行内部命令
        if (strcmp(argv[0], "cd") == 0) {
            // 实现cd：改变当前目录（UNIX V6用chdir系统调用）
            if (argv[1] != NULL) {
                if (chdir(argv[1]) < 0) printf("cd error\n");
            }
            continue;
        }
        if (strcmp(argv[0], "logout") == 0) {
            printf("shell exiting...\n");
            exit(0);
        }

        // 5. 执行外部命令
        i = fork();
        if (i > 0) {
            // 父进程：等待子进程
            wait();
        } else if (i == 0) {
            // 子进程：执行命令（UNIX V6的exec系统调用）
            execv(argv[0], argv);
            // exec失败
            printf("exec error");
            exit(1);
        } else {
        	printf("fork error");
        }
    }
    return 0;
}

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <ctype.h>
#include <process.h>

#define MAX_COMMAND_LENGTH 1024
#define MAX_ARGC 10

typedef struct Command {
    char* argv[MAX_ARGC];
    int argc;
    int period;
    int duration;
    int repeat;
    int pid;
    char type; // 'F' for FG, 'B' for BG
    struct Command* next;
} Command;

int current_pid = 0;
Command* dq_head = NULL;
Command* wq_head = NULL;
HANDLE monitor_thread;
CRITICAL_SECTION cs;

void print_queues();
DWORD WINAPI monitor(LPVOID arg);

void enqueue(Command** head, Command* cmd) {
    cmd->next = *head;
    *head = cmd;
}

Command* dequeue(Command** head) {
    if (*head == NULL) return NULL;
    Command* cmd = *head;
    *head = (*head)->next;
    return cmd;
}

void print_queues() {
    EnterCriticalSection(&cs);
    printf("---------------------------\n");
    printf("DQ: ");
    Command* curr = dq_head;
    while (curr) {
        printf("[%d%c] ", curr->pid, curr->type);
        curr = curr->next;
    }
    printf("(bottom/top)\n");
    printf("---------------------------\n");
    printf("WQ: ");
    curr = wq_head;
    while (curr) {
        printf("[%d%c:%d] ", curr->pid, curr->type, curr->duration);
        curr = curr->next;
    }
    printf("\n...\n");
    LeaveCriticalSection(&cs);
}

// 명령어 실행 함수
void execute_command(Command* cmd) {
    for (int i = 0; i < cmd->repeat; i++) {
        if (cmd->type == 'B') {
            EnterCriticalSection(&cs);
            printf("Running: [%d%c]\n", cmd->pid, cmd->type);
            LeaveCriticalSection(&cs);
        }

        if (strcmp(cmd->argv[0], "echo") == 0) {
            if (cmd->argv[1]) printf("%s\n", cmd->argv[1]);
        }
        else if (strcmp(cmd->argv[0], "dummy") == 0) {
            Sleep(1000);
        }
        else if (strcmp(cmd->argv[0], "gcd") == 0 && cmd->argv[1] && cmd->argv[2]) {
            int x = atoi(cmd->argv[1]);
            int y = atoi(cmd->argv[2]);
            while (y != 0) {
                int t = y;
                y = x % y;
                x = t;
            }
            printf("GCD: %d\n", x);
        }
        else if (strcmp(cmd->argv[0], "prime") == 0 && cmd->argv[1]) {
            int x = atoi(cmd->argv[1]);
            int count = 0;
            char* sieve = (char*)malloc(x + 1);
            memset(sieve, 1, x + 1);
            for (int i = 2; i <= x; i++) {
                if (sieve[i]) {
                    count++;
                    for (int j = i * 2; j <= x; j += i) {
                        sieve[j] = 0;
                    }
                }
            }
            free(sieve);
            printf("Number of primes: %d\n", count);
        }
        else if (strcmp(cmd->argv[0], "sum") == 0 && cmd->argv[1]) {
            int x = atoi(cmd->argv[1]);
            long long sum = 0;
            for (int i = 1; i <= x; i++) {
                sum += i;
            }
            printf("Sum: %lld\n", sum % 1000000);
        }
        else {
            fprintf(stderr, "Unknown command: %s\n", cmd->argv[0]);
        }

        if (cmd->period > 0) {
            Sleep(cmd->period * 1000);
        }
    }
}

// 백그라운드 명령어 실행 쓰레드 함수
DWORD WINAPI background_command(LPVOID arg) {
    Command* cmd = (Command*)arg;
    execute_command(cmd);
    EnterCriticalSection(&cs);
    // 명령어 완료 후 DQ에서 제거
    Command** indirect = &dq_head;
    while (*indirect) {
        if (*indirect == cmd) {
            *indirect = cmd->next;
            break;
        }
        indirect = &(*indirect)->next;
    }
    LeaveCriticalSection(&cs);
    free(cmd);
    return 0;
}

void parse_command(char* command_str, Command* cmd) {
    char* token = strtok(command_str, " ");
    cmd->argc = 0;
    cmd->period = 0;
    cmd->duration = 0;
    cmd->repeat = 1;
    cmd->pid = current_pid++;
    cmd->next = NULL;

    while (token != NULL && cmd->argc < MAX_ARGC - 1) {
        if (strcmp(token, "-n") == 0) {
            token = strtok(NULL, " ");
            cmd->repeat = atoi(token);
        }
        else if (strcmp(token, "-d") == 0) {
            token = strtok(NULL, " ");
            cmd->duration = atoi(token);
        }
        else if (strcmp(token, "-p") == 0) {
            token = strtok(NULL, " ");
            cmd->period = atoi(token);
        }
        else {
            cmd->argv[cmd->argc++] = token;
        }
        token = strtok(NULL, " ");
    }
    cmd->argv[cmd->argc] = NULL;
}

void parse_and_execute(char* line) {
    char* command_str = strtok(line, ";");
    while (command_str != NULL) {
        while (isspace((unsigned char)*command_str)) command_str++;
        if (*command_str == '\0') {
            command_str = strtok(NULL, ";");
            continue;
        }
        char* end = command_str + strlen(command_str) - 1;
        while (end > command_str && isspace((unsigned char)*end)) end--;
        *(end + 1) = '\0';

        Command* cmd = (Command*)malloc(sizeof(Command));
        parse_command(command_str, cmd);

        EnterCriticalSection(&cs);
        enqueue(&dq_head, cmd);
        LeaveCriticalSection(&cs);

        if (cmd->argv[0][0] == '&') {
            cmd->type = 'B';
            cmd->argv[0] = cmd->argv[0] + 1;
            HANDLE thread;
            thread = CreateThread(NULL, 0, background_command, cmd, 0, NULL);
            CloseHandle(thread);
            EnterCriticalSection(&cs);
            printf("Running: [%dB]\n", cmd->pid);
            LeaveCriticalSection(&cs);
        }
        else {
            cmd->type = 'F';
            execute_command(cmd);
            EnterCriticalSection(&cs);
            // 명령어 완료 후 DQ에서 제거
            Command** indirect = &dq_head;
            while (*indirect) {
                if (*indirect == cmd) {
                    *indirect = cmd->next;
                    break;
                }
                indirect = &(*indirect)->next;
            }
            LeaveCriticalSection(&cs);
            free(cmd);
        }

        command_str = strtok(NULL, ";");
    }
}

DWORD WINAPI monitor(LPVOID arg) {
    while (1) {
        Sleep(3000); // X초마다 상태 출력
        print_queues();
    }
    return 0;
}

int main() {
    InitializeCriticalSection(&cs);
    monitor_thread = CreateThread(NULL, 0, monitor, NULL, 0, NULL);

    FILE* file = fopen("commands.txt", "r");
    if (file == NULL) {
        perror("Failed to open commands.txt");
        return 1;
    }

    char line[MAX_COMMAND_LENGTH];
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        printf("prompt> %s\n", line);
        parse_and_execute(line);
        Sleep(1000); // Y초 동안 대기 .(사용자 입력 모사)
    }

    fclose(file);
    DeleteCriticalSection(&cs);
    return 0;
}

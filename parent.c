#include <windows.h>
#include <stdlib.h>

#define MAX_STR_LEN 1024
#define MAX_SLOTS   1000
#define SHM_SIZE    (sizeof(SharedQueue))

typedef struct {
    volatile LONG write_index;
    volatile LONG read_index;
    char buffer[MAX_SLOTS][MAX_STR_LEN];
} SharedQueue;

DWORD my_strlen(const char *s) {
    DWORD len = 0;
    while (s[len]) len++;
    return len;
}

int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

DWORD read_line(char *buf, DWORD max) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD read = 0;
    if (!ReadFile(hIn, buf, max - 1, &read, NULL)) return 0;
    while (read > 0 && (buf[read-1] == '\n' || buf[read-1] == '\r')) {
        read--;
    }
    buf[read] = '\0';
    return read;
}

void print(const char *s) {
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), s, my_strlen(s), &written, NULL);
}

char* my_strcat(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
    return dst;
}

char* my_ulong_to_str(char *buf, DWORD val) {
    char tmp[32];
    int i = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return buf; }
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
    return buf;
}

void write_to_shm(SharedQueue *q, HANDLE hSem, const char *str) {
    WaitForSingleObject(hSem, INFINITE);

    LONG idx = q->write_index;
    DWORD len = my_strlen(str);
    if (len >= MAX_STR_LEN) len = MAX_STR_LEN - 1;
    
    for (DWORD i = 0; i < len; i++) q->buffer[idx][i] = str[i];
    q->buffer[idx][len] = '\0';

    q->write_index = (idx + 1) % MAX_SLOTS;
}

void build_cmd(char *cmd, const char *shm, const char *sem, const char *file, int id) {
    char *p = cmd;
    p = my_strcat(p, "child.exe ");
    p = my_strcat(p, shm);
    *p++ = ' ';
    p = my_strcat(p, sem);
    *p++ = ' ';
    p = my_strcat(p, file);
    *p++ = ' ';
    char idBuf[16];
    my_ulong_to_str(idBuf, (DWORD)id);
    my_strcat(p, idBuf);
}

int main() {
    srand(GetTickCount());

    DWORD pid = GetCurrentProcessId();
    char pidStr[32];
    my_ulong_to_str(pidStr, pid);

    char shmName1[256], shmName2[256];
    char semName1[256], semName2[256];

    char *p = my_strcat(shmName1, "Local\\SHM_Pipe1_"); my_strcat(p, pidStr);
    p = my_strcat(shmName2, "Local\\SHM_Pipe2_"); my_strcat(p, pidStr);
    p = my_strcat(semName1, "Local\\SEM_Pipe1_"); my_strcat(p, pidStr);
    p = my_strcat(semName2, "Local\\SEM_Pipe2_"); my_strcat(p, pidStr);

    HANDLE hMap1 = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHM_SIZE, shmName1);
    HANDLE hMap2 = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, SHM_SIZE, shmName2);
    if (!hMap1 || !hMap2) { print("Err creating SHM.\n"); return 1; }

    SharedQueue *q1 = (SharedQueue*)MapViewOfFile(hMap1, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
    SharedQueue *q2 = (SharedQueue*)MapViewOfFile(hMap2, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
    if (!q1 || !q2) { print("Err mapping SHM.\n"); return 1; }

    q1->read_index = 0; q1->write_index = 0;
    q2->read_index = 0; q2->write_index = 0;

    HANDLE hSem1 = CreateSemaphoreA(NULL, MAX_SLOTS, MAX_SLOTS, semName1);
    HANDLE hSem2 = CreateSemaphoreA(NULL, MAX_SLOTS, MAX_SLOTS, semName2);
    if (!hSem1 || !hSem2) { print("Err creating semaphores.\n"); return 1; }

    char file1[MAX_PATH], file2[MAX_PATH];
    print("Enter first file name: ");
    read_line(file1, MAX_PATH);
    print("Enter second file name: ");
    read_line(file2, MAX_PATH);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi1, pi2;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    char cmd1[1024];
    build_cmd(cmd1, shmName1, semName1, file1, 1);
    if (!CreateProcessA(NULL, cmd1, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi1)) {
        print("Err starting first proccess.\n");
        return 1;
    }

    char cmd2[1024];
    build_cmd(cmd2, shmName2, semName2, file2, 2);
    if (!CreateProcessA(NULL, cmd2, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi2)) {
        print("Err starting second proccess.\n");
        return 1;
    }

    CloseHandle(pi1.hThread);
    CloseHandle(pi2.hThread);

    print("\nEnter strings (for exit: exit):\n");

    char input[1024];
    while (1) {
        print("> ");
        DWORD len = read_line(input, sizeof(input));
        if (len == 0) continue;

        if (my_strcmp(input, "exit") == 0) {
            write_to_shm(q1, hSem1, "__EXIT__");
            write_to_shm(q2, hSem2, "__EXIT__");
            break;
        }

        int r = rand() % 100;
        if (r < 80) {
            write_to_shm(q1, hSem1, input);
        } else {
            write_to_shm(q2, hSem2, input);
        }
    }

    WaitForSingleObject(pi1.hProcess, INFINITE);
    WaitForSingleObject(pi2.hProcess, INFINITE);

    CloseHandle(pi1.hProcess);
    CloseHandle(pi2.hProcess);

    UnmapViewOfFile(q1);
    UnmapViewOfFile(q2);
    CloseHandle(hMap1);
    CloseHandle(hMap2);
    CloseHandle(hSem1);
    CloseHandle(hSem2);

    print("Exiting.\n");
    return 0;
}
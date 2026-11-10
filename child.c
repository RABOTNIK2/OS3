#include <windows.h>
#include <stdlib.h>

#define MAX_STR_LEN 1024
#define MAX_SLOTS   1000

typedef struct {
    volatile LONG write_index;
    volatile LONG read_index;
    char buffer[MAX_SLOTS][MAX_STR_LEN];
} SharedQueue;

char my_tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int is_vowel(char c) {
    c = my_tolower(c);
    return (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' || c == 'y');
}

DWORD my_strlen(const char *s) {
    DWORD len = 0;
    while (s[len]) len++;
    return len;
}

int my_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

void print_err(const char *s) {
    DWORD written;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, my_strlen(s), &written, NULL);
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

int main(int argc, char *argv[]) {
    if (argc < 5) {
        print_err("Err args.\n");
        return 1;
    }

    char *shmName = argv[1];
    char *semName = argv[2];
    char *filename = argv[3];
    int childId = atoi(argv[4]);

    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmName);
    if (hMap == NULL) {
        print_err("Err open SHM.\n");
        return 1;
    }
    SharedQueue *q = (SharedQueue*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedQueue));
    if (!q) {
        print_err("Err map SHM.\n");
        CloseHandle(hMap);
        return 1;
    }

    HANDLE hSem = OpenSemaphoreA(SEMAPHORE_MODIFY_STATE | SYNCHRONIZE, FALSE, semName);
    if (hSem == NULL) {
        print_err("Err open semaphore.\n");
        UnmapViewOfFile(q);
        CloseHandle(hMap);
        return 1;
    }

    HANDLE hFile = CreateFileA(
        filename,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        print_err("Err openning file.\n");
        return 1;
    }

    char result[MAX_STR_LEN];
    int running = 1;

    while (running) {
        if (q->read_index == q->write_index) {
            Sleep(10);
            continue;
        }

        LONG idx = q->read_index;
        char *input = q->buffer[idx];

        if (my_strcmp(input, "__EXIT__") == 0) {
            running = 0;
        } else {
            DWORD j = 0;
            for (DWORD i = 0; input[i] != '\0'; i++) {
                if (!is_vowel(input[i])) {
                    result[j++] = input[i];
                }
            }
            result[j] = '\0';

            char msg[MAX_STR_LEN + 64];
            char idBuf[16];
            my_ulong_to_str(idBuf, (DWORD)childId);

            char *p = msg;
            p = my_strcat(p, "[Child ");
            p = my_strcat(p, idBuf);
            p = my_strcat(p, "] Result: ");
            p = my_strcat(p, result);
            p = my_strcat(p, "\n");

            DWORD written;
            WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), msg, my_strlen(msg), &written, NULL);

            WriteFile(hFile, result, j, &written, NULL);
            WriteFile(hFile, "\n", 1, &written, NULL);
        }

        q->read_index = (idx + 1) % MAX_SLOTS;
        ReleaseSemaphore(hSem, 1, NULL);
    }

    CloseHandle(hFile);
    UnmapViewOfFile(q);
    CloseHandle(hMap);
    CloseHandle(hSem);
    return 0;
}
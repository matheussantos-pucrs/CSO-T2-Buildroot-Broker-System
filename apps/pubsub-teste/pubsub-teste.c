
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/pubsub"
#define BUFFER_SIZE 256

static int send_command(FILE *device, const char *command)
{
    size_t len = strlen(command);

    if (fwrite(command, 1, len, device) != len) {
        perror("Failed to write command");
        return -1;
    }

    if (fflush(device) != 0) {
        perror("Failed to flush command");
        return -1;
    }

    return 0;
}

static int read_message(FILE *device)
{
    char buffer[BUFFER_SIZE];
    ssize_t len;

    len = read(fileno(device), buffer, sizeof(buffer) - 1);

    if (len < 0) {
        perror("Failed to read message");
        return -1;
    }

    if (len == 0) {
        printf("No pending messages\n");
        return 0;
    }

    buffer[len] = '\0';

    printf("Received message: %s\n", buffer);

    return 0;
}

static void print_help(void)
{
    printf("\nAvailable commands:\n");
    printf("  /subscribe <topic>\n");
    printf("  /unsubscribe <topic>\n");
    printf("  /publish <topic> \"<message>\"\n");
    printf("  /fetch <topic>\n");
    printf("  /read\n");
    printf("  /help\n");
    printf("  /exit\n\n");
}

int main(void)
{
    FILE *device;
    char command[BUFFER_SIZE];
    size_t len;
    int ch;

    device = fopen(DEVICE_PATH, "r+");

    if (device == NULL) {
        perror("Failed to open device");
        return 1;
    }

    printf("Publish/Subscribe client - PID %d\n", getpid());

    print_help();

    while (1) {
        if (isatty(STDIN_FILENO)) {
            printf("> ");
            fflush(stdout);
        }

        if (fgets(command, sizeof(command), stdin) == NULL)
            break;

        len = strlen(command);

        if (len > 0 && command[len - 1] == '\n') {
            command[--len] = '\0';
        } else if (!feof(stdin)) {
            printf("Command too long\n");

            while ((ch = getchar()) != '\n' && ch != EOF)
                ;

            continue;
        }

        if (len > 0 && command[len - 1] == '\r')
            command[--len] = '\0';

        if (len == 0)
            continue;

        if (strcmp(command, "/exit") == 0)
            break;

        if (strcmp(command, "/help") == 0) {
            print_help();
            continue;
        }

        if (strcmp(command, "/read") == 0) {
            read_message(device);
            continue;
        }

        if (strncmp(command, "/subscribe ", 11) == 0 ||
            strncmp(command, "/unsubscribe ", 13) == 0 ||
            strncmp(command, "/publish ", 9) == 0 ||
            strncmp(command, "/fetch ", 7) == 0) {

            send_command(device, command);
            continue;
        }

        printf("Unknown command: %s\n", command);
    }

    fclose(device);

    printf("Publish/Subscribe client closed\n");

    return 0;
}

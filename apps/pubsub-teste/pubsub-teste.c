
#include <stdio.h>
#include <string.h>

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

int main(void)
{
    FILE *device;
    char buffer[BUFFER_SIZE];
    const char *text = "Hello World!";
    size_t len;

    device = fopen(DEVICE_PATH, "r+");

    if (device == NULL) {
        perror("Failed to open device");
        return 1;
    }

    if (send_command(device, "/subscribe teste") != 0)
        goto error;

    if (send_command(device, "/publish teste \"Hello World!\"") != 0)
        goto error;

    if (send_command(device, "/fetch teste") != 0)
        goto error;

    len = fread(buffer, 1, strlen(text), device);

    if (len != strlen(text)) {
        fprintf(stderr, "Failed to read complete message\n");
        goto error;
    }

    buffer[len] = '\0';

    printf("Received message: %s\n", buffer);

    fclose(device);

    return 0;

error:
    fclose(device);
    return 1;
}

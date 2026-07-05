#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <termios.h>
#include <stdbool.h>

// -------------------------------------------------------------------------------------------------
// [SECTION] Extendable
// -------------------------------------------------------------------------------------------------

#define KEYMAP \
    X(0x45, KEY_F) \
    X(0x1A, KEY_PLAYPAUSE) \
    X(0x0D, KEY_NEXTSONG) \
    X(0x11, KEY_PREVIOUSSONG) \
    X(0x51, KEY_VOLUMEDOWN) \
    X(0x4D, KEY_VOLUMEUP) \
    X(0x19, KEY_MUTE) \
    X(0x06, KEY_UP) \
    X(0x16, KEY_DOWN) \
    X(0x5A, KEY_LEFT) \
    X(0x1B, KEY_RIGHT) \
    X(0x05, KEY_ESC) \
    X(0x17, KEY_ENTER) \
    X(0x13, KEY_BACKSPACE) \
    X(0x0F, KEY_0) \
    X(0x52, KEY_1) \
    X(0x50, KEY_2) \
    X(0x10, KEY_3) \
    X(0x56, KEY_4) \
    X(0x54, KEY_5) \
    X(0x14, KEY_6) \
    X(0x4E, KEY_7) \
    X(0x4C, KEY_8) \
    X(0x0C, KEY_9)


// -------------------------------------------------------------------------------------------------
// [SECTION] Logic
// -------------------------------------------------------------------------------------------------

#define RED "\e[31m"
#define GREEN "\e[32m"
#define BLUE "\e[34m"
#define RESET "\e[0m"

typedef enum {
    LOG_KEY,
    LOG_INFO,
    LOG_ERROR,
} LogLevel;

void logm(LogLevel level, const char *message, ...) __attribute__((format(printf, 2, 3)));

void logm(LogLevel level, const char *message, ...)
{
    bool supports_color = isatty(STDOUT_FILENO);
    if (supports_color) {
        switch (level) {
        case LOG_KEY:
            printf(BLUE "[KEY]" RESET " ");
            break;
        case LOG_INFO:
            printf(GREEN "[INF]" RESET " ");
            break;
        case LOG_ERROR:
            printf(RED "[ERR]" RESET " ");
            break;
        }
    } else {
        switch (level) {
        case LOG_KEY:
            printf("[KEY] ");
            break;
        case LOG_INFO:
            printf("[INF] ");
            break;
        case LOG_ERROR:
            printf("[ERR] ");
            break;
        }
    }

    va_list args;
    va_start(args, message);
    vprintf(message, args);
    va_end(args);
    printf("\n");
}

int open_virtual_keyboard(void)
{
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/uinput (Are you running as root?)");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTALT);
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTSHIFT);
    ioctl(fd, UI_SET_KEYBIT, KEY_TAB);
    #define X(HEX, KEY) ioctl(fd, UI_SET_KEYBIT, KEY);
    KEYMAP
    #undef X

    struct uinput_setup usetup = {
        .id.bustype = BUS_USB,
        .id.vendor  = 0x1234, // Arbitrary Vendor ID
        .id.product = 0x5678, // Arbitrary Product ID
    };
    strcpy(usetup.name, "TV-like remote");
    ioctl(fd, UI_DEV_SETUP, &usetup);

    if (ioctl(fd, UI_DEV_CREATE) < 0) {
        perror("Failed to create uinput device");
        close(fd);
        return -1;
    }

    return fd;
}

int open_serial(const char* port_name)
{
    int serial_fd = open(port_name, O_RDWR | O_NOCTTY);
    if (serial_fd < 0) {
        return -1;
    }

    struct termios tty;
    if (tcgetattr(serial_fd, &tty) != 0) {
        perror("Error from tcgetattr");
        close(serial_fd);
        return -1;
    }

    cfsetospeed(&tty, B9600);
    cfsetispeed(&tty, B9600);

    // Set 8N1 (8 bits, no parity, 1 stop bit)
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    
    // Disable hardware flow control & enable reading
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // Set Canonical Mode (reads line-by-line, waiting for '\n')
    tty.c_lflag |= ICANON;
    
    // Apply settings
    tcsetattr(serial_fd, TCSANOW, &tty);

    return serial_fd;
}

typedef enum {
    VAL_UP = 0,
    VAL_DOWN = 1,
    VAL_REPEAT = 2,
} EmitValue;

void emit_raw(int fd, int type, int code, EmitValue val)
{
    struct input_event ie = {0};
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(fd, &ie, sizeof(ie));
}

void emit_down(int fd, int code)
{
    emit_raw(fd, EV_KEY, code, VAL_DOWN);
    emit_raw(fd, EV_SYN, SYN_REPORT, VAL_UP);
}

void emit_up(int fd, int code)
{
    emit_raw(fd, EV_KEY, code, VAL_UP);
    emit_raw(fd, EV_SYN, SYN_REPORT, VAL_UP);
}

void emit(int fd, int code)
{
    emit_down(fd, code);
    emit_up(fd, code);
}

int parse_hex(const char *repr)
{
    int result = 0;
    for (const char *ch = repr; *ch; ch++) {
        int digit;
        if (isdigit(*ch)) {
            digit = *ch - '0';
        } else if (*ch >= 'A' && *ch <= 'F') {
            digit = *ch - 'A' + 10;
        } else if (*ch >= 'a' && *ch <= 'f') {
            digit = *ch - 'A' + 10;
        } else {
            return -1;
        }

        result *= 16;
        result += digit;
    }

    return result;
}

void emulate_keyboard(int kb, int serial)
{
    char buffer[256];
    while (true) {
        int n = read(serial, buffer, sizeof(buffer) - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("Serial read error (disconnected?)");
            return;
        }
        if (n == 0) return;

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (buffer[0] == '\0') continue;

        int value = parse_hex(buffer);
        switch (value) {

        #define X(HEX, KEY) \
        case HEX: \
            logm(LOG_KEY, #HEX " -> [" #KEY "]"); \
            emit(kb, KEY); \
            break;
        KEYMAP
        #undef X

        case 0:
            logm(LOG_KEY, "Receiver miss");
            break;
        case -1:
            if (strcmp(buffer, "Started.") == 0) {
                logm(LOG_KEY, "\"%s\" -> Receiver started", buffer);
            } else {
                logm(LOG_KEY, "%s -> (unparsable)", buffer);
            }
            break;
        default:
            logm(LOG_KEY, "0x%02x -> (unknown)", value);
            break;
        }
    }
}

#define MS 1000

int main(int argc, char **argv)
{
    if (argc != 1) {
        fprintf(stderr, "USAGE: %s\n", argv[0]);
        return 1;
    }

    logm(LOG_INFO, "Remote daemon started.");

    int kb = open_virtual_keyboard();
    if (kb < 0) return 1;
    usleep(500 * MS);

    while (1) {
        logm(LOG_INFO, "Waiting for user to connect the receiver...");

        char device[16];
        int serial;
        while (1) {
            for (int n = 0; n < 256; n++) {
                snprintf(device, sizeof(device)/sizeof(*device), "/dev/ttyUSB%d", n);
                serial = open_serial(device);
                if (serial >= 0) goto connected;
            }
            usleep(100 * MS);
        }

    connected:
        logm(LOG_INFO, "Connected to %s", device);
        emulate_keyboard(kb, serial);
        logm(LOG_INFO, "Disconnected from %s", device);
        close(serial);
    }

    ioctl(kb, UI_DEV_DESTROY);
    close(kb);
    return 0;
}

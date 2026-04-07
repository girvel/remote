#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <linux/uinput.h>
#include <termios.h>
#include <stdbool.h>

void emit(int fd, int code);

// -------------------------------------------------------------------------------------------------
// [SECTION] Extendable
// -------------------------------------------------------------------------------------------------

void capabilities(int fd) {
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTMETA);
    ioctl(fd, UI_SET_KEYBIT, KEY_PLAYPAUSE);
    ioctl(fd, UI_SET_KEYBIT, KEY_ENTER);
    ioctl(fd, UI_SET_KEYBIT, KEY_NEXTSONG);
    ioctl(fd, UI_SET_KEYBIT, KEY_PREVIOUSSONG);
    ioctl(fd, UI_SET_KEYBIT, KEY_VOLUMEUP);
    ioctl(fd, UI_SET_KEYBIT, KEY_VOLUMEDOWN);
    ioctl(fd, UI_SET_KEYBIT, KEY_MUTE);
}

void actions(int value, int kb) {
    switch (value) {
    case 0x1C:
        printf("[WIN]");
        emit(kb, KEY_LEFTMETA);
        break;
    case 0x43:
        printf("[PLAY/PAUSE]");
        emit(kb, KEY_PLAYPAUSE);
        break;
    case 0x40:
        printf("[NEXT TRACK]");
        emit(kb, KEY_NEXTSONG);
        break;
    case 0x44:
        printf("[PREV TRACK]");
        emit(kb, KEY_PREVIOUSSONG);
        break;
    case 0x7:
        printf("[VOLUME DOWN]");
        emit(kb, KEY_VOLUMEDOWN);
        break;
    case 0x15:
        printf("[VOLUME UP]");
        emit(kb, KEY_VOLUMEUP);
        break;
    case 0x9:
        printf("[MUTE]");
        emit(kb, KEY_MUTE);
        break;
    case 0:
        printf("NO ACTION");
        break;
    case -1:
        printf("UNPARSABLE");
        break;
    default:
        printf("UNKNOWN");
        break;
    }
}

// -------------------------------------------------------------------------------------------------
// [SECTION] Logic
// -------------------------------------------------------------------------------------------------

int open_virtual_keyboard() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/uinput (Are you running as root?)");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    capabilities(fd);

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

int open_serial(const char* port_name) {
    int serial_fd = open(port_name, O_RDWR | O_NOCTTY);
    if (serial_fd < 0) {
        perror("Failed to open serial port");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(serial_fd, &tty) != 0) {
        perror("Error from tcgetattr");
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

void emit_raw(int fd, int type, int code, EmitValue val) {
    struct input_event ie = {0};
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(fd, &ie, sizeof(ie));
}

void emit(int fd, int code) {
    emit_raw(fd, EV_KEY, code, VAL_DOWN);
    emit_raw(fd, EV_SYN, SYN_REPORT, VAL_UP);
    emit_raw(fd, EV_KEY, code, VAL_UP);
    emit_raw(fd, EV_SYN, SYN_REPORT, VAL_UP);
}

int parse_hex(const char *repr) {
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

#define MS 1000

int main(int argc, char **argv) {
    printf("Remote daemon started.\n");

    if (argc != 2) {
        printf("USAGE: %s [serial device]\n", argv[0]);
        return 1;
    }

    int kb = open_virtual_keyboard();
    usleep(500 * MS);

    int serial = open_serial(argv[1]);
    if (serial < 0) {
        goto kb_close;
        return 1;
    }

    char buffer[256];
    while (true) {
        int n = read(serial, buffer, sizeof(buffer) - 1);
        if (n == 0) continue;

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        int value = parse_hex(buffer);
        printf("0x%s (%d) -> ", buffer, value);
        actions(value, kb);
        printf("\n");
    }

    close(serial);

kb_close:
    ioctl(kb, UI_DEV_DESTROY);
    close(kb);
    return 0;
}

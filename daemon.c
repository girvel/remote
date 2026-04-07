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

// -------------------------------------------------------------------------------------------------
// [SECTION] Extendable
// -------------------------------------------------------------------------------------------------

#define KEYMAP \
    X(0x1C, KEY_LEFTMETA) \
    X(0x43, KEY_PLAYPAUSE) \
    X(0x40, KEY_NEXTSONG) \
    X(0x44, KEY_PREVIOUSSONG) \
    X(0x7,  KEY_VOLUMEDOWN) \
    X(0x15, KEY_VOLUMEUP) \
    X(0x9,  KEY_MUTE) \
    X(0x18, KEY_UP) \
    X(0x52, KEY_DOWN) \
    X(0x8,  KEY_LEFT) \
    X(0x5A, KEY_RIGHT) \
    X(0x16, KEY_ENTER) \
    X(0x19, KEY_SPACE) \
    X(0xD,  KEY_F11)

#define ALT_TAB_TOGGLE 0x46
#define ALT_TAB_PREV 0x45
#define ALT_TAB_NEXT 0x47

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

void emit_down(int fd, int code) {
    emit_raw(fd, EV_KEY, code, VAL_DOWN);
    emit_raw(fd, EV_SYN, SYN_REPORT, VAL_UP);
}

void emit_up(int fd, int code) {
    emit_raw(fd, EV_KEY, code, VAL_UP);
    emit_raw(fd, EV_SYN, SYN_REPORT, VAL_UP);
}

void emit(int fd, int code) {
    emit_down(fd, code);
    emit_up(fd, code);
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

bool alt_activated = false;

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("USAGE: %s [serial device]\n", argv[0]);
        return 1;
    }

    printf("Remote daemon started.\n");

    int kb = open_virtual_keyboard();
    if (kb < 0) return 1;
    usleep(500 * MS);

    int serial = open_serial(argv[1]);
    if (serial < 0) {
        goto kb_close;
        return 1;
    }

    printf("Devices initialized.\n");

    char buffer[256];
    while (true) {
        int n = read(serial, buffer, sizeof(buffer) - 1);
        if (n == 0) continue;

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (buffer[0] == '\0') continue;

        int value = parse_hex(buffer);
        printf("0x%s -> ", buffer);
        switch (value) {
        #define X(HEX, KEY) \
        case HEX: \
            printf("[" #KEY "]"); \
            emit(kb, KEY); \
            break;
        KEYMAP
        #undef X
        case ALT_TAB_TOGGLE:
            alt_activated ^= true;
            if (alt_activated) {
                printf("OPEN ALT TAB MENU");
                emit_down(kb, KEY_LEFTALT);
                emit(kb, KEY_TAB);
                emit_down(kb, KEY_LEFTSHIFT);
                emit(kb, KEY_TAB);
                emit_up(kb, KEY_LEFTSHIFT);
            } else {
                printf("CLOSE ALT TAB MENU");
                emit_up(kb, KEY_LEFTALT);
            }
            break;
        case ALT_TAB_NEXT:
            printf("NEXT WINDOW");
            emit(kb, KEY_TAB);
            break;
        case ALT_TAB_PREV:
            printf("PREV WINDOW");
            emit_down(kb, KEY_LEFTSHIFT);
            emit(kb, KEY_TAB);
            emit_up(kb, KEY_LEFTSHIFT);
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
        printf("\n");
    }

    close(serial);

kb_close:
    ioctl(kb, UI_DEV_DESTROY);
    close(kb);
    return 0;
}

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/uinput.h>
#include <linux/uinput.h>

int open_virtual_keyboard() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Failed to open /dev/uinput (Are you running as root?)");
        return -1;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_ENTER);
    ioctl(fd, UI_SET_KEYBIT, KEY_LEFTMETA);

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

typedef enum {
    VAL_UP = 0,
    VAL_DOWN = 1,
    VAL_REPEAT = 2,
} EmitValue;

void emit(int fd, int type, int code, EmitValue val) {
    struct input_event ie = {
      .type = type,
      .code = code,
      .value = val,
    };
    
    write(fd, &ie, sizeof(ie));
}

#define MS 1000

int main() {
    printf("Remote daemon started.\n");
    int kb = open_virtual_keyboard();

    usleep(500 * MS);

    emit(kb, EV_KEY, KEY_LEFTMETA, 1); // Key DOWN
    emit(kb, EV_SYN, SYN_REPORT, 0); // SYNC

    return 0;
}

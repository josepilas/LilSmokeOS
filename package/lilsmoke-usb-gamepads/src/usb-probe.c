#include <libusb.h>
#include <stdio.h>
int main(void) {
    libusb_context *ctx = NULL;
    int error = libusb_init(&ctx);
    printf("USB_PROBE init=%d name=%s\n", error, libusb_error_name(error));
    if (error) return 1;
    libusb_device **devices = NULL;
    ssize_t count = libusb_get_device_list(ctx, &devices);
    printf("USB_PROBE devices=%ld\n", (long)count);
    for (ssize_t i = 0; i < count && i < 64; ++i) {
        struct libusb_device_descriptor descriptor;
        if (libusb_get_device_descriptor(devices[i], &descriptor)) continue;
        printf("USB_PROBE bus=%u address=%u vendor=%04x product=%04x\n", libusb_get_bus_number(devices[i]),
               libusb_get_device_address(devices[i]), descriptor.idVendor, descriptor.idProduct);
        struct libusb_config_descriptor *config = NULL;
        if (!libusb_get_active_config_descriptor(devices[i], &config)) {
            for (unsigned j = 0; j < config->bNumInterfaces; ++j)
                for (int k = 0; k < config->interface[j].num_altsetting; ++k) {
                    const struct libusb_interface_descriptor *iface = &config->interface[j].altsetting[k];
                    printf("USB_PROBE interface=%u class=%02x subclass=%02x protocol=%02x\n", iface->bInterfaceNumber,
                           iface->bInterfaceClass, iface->bInterfaceSubClass, iface->bInterfaceProtocol);
                }
            libusb_free_config_descriptor(config);
        }
    }
    if (devices) libusb_free_device_list(devices, 1);
    libusb_exit(ctx);
    return count < 0 ? 1 : 0;
}

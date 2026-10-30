/* Match netlink's lock ordering; no monitor thread exists in polling mode. */
static void ls_poll_hotplug(void)
{
    struct libusb_context *ctx;
    usbi_mutex_static_lock(&linux_hotplug_lock);
    usbi_mutex_static_lock(&active_contexts_lock);
    for_each_context(ctx) {
        for (unsigned budget = 0; budget < 64; ++budget) {
            struct libusb_device *dev, *removed = NULL;
            usbi_mutex_lock(&ctx->usb_devs_lock);
            for_each_device(ctx, dev) {
                struct linux_device_priv *priv = usbi_get_device_priv(dev);
                uint8_t bus, address;
                if (!priv->sysfs_dir) continue;
                int result = linux_get_device_address(ctx, 0, &bus, &address, NULL, priv->sysfs_dir, -1);
                if (result == LIBUSB_ERROR_NO_DEVICE ||
                    (result == 0 && (bus != dev->bus_number || address != dev->device_address))) {
                    removed = libusb_ref_device(dev);
                    break;
                }
            }
            usbi_mutex_unlock(&ctx->usb_devs_lock);
            if (!removed) break;
            usbi_disconnect_device(removed);
            libusb_unref_device(removed);
        }
        (void)linux_default_scan_devices(ctx);
    }
    usbi_mutex_static_unlock(&active_contexts_lock);
    usbi_mutex_static_unlock(&linux_hotplug_lock);
}

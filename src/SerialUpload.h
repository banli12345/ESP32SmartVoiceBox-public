#ifndef SERIAL_UPLOAD_H
#define SERIAL_UPLOAD_H
// Run after mounting SD, before fonts, UI, audio and network tasks start.
void serial_upload_boot_window(void);
// Normal loop only detects the ASCII entry command, then reboots into upload.
void serial_upload_poll(void);
#endif

// Minimal stand-ins for the lib_darktable globals the harnessed sources
// reference (the `darktable` state, the dt_print_ext() logger and the GUI's
// dt_control_log() toast), so they can be linked without the GTK application.
// None of them is parser code.
#include <stdarg.h>
#include <stdio.h>
#include "common/darktable.h"

// Process-global darktable state; only `unmuted` is read (by dt_print()).
// It is set once to DT_DEBUG_IMAGEIO (so the image-I/O debug messages are
// formatted too) and never changed, so each input's result depends only on it.
darktable_t darktable;

void dt_shim_init(void)
{
  darktable.unmuted = DT_DEBUG_IMAGEIO;
}

// darktable's logger formats the message and writes it to stdout; format it
// into a scratch buffer instead so fuzzing stays quiet.
void dt_print_ext(const char *msg, ...)
{
  char buf[2048];
  va_list ap;
  va_start(ap, msg);
  vsnprintf(buf, sizeof(buf), msg, ap);
  va_end(ap);
  (void)buf;
}

// darktable's toast message in the GUI (src/control/control.c); there is no
// GUI here, so the message is formatted and dropped like dt_print_ext().
void dt_control_log(const char *msg, ...)
{
  char buf[2048];
  va_list ap;
  va_start(ap, msg);
  vsnprintf(buf, sizeof(buf), msg, ap);
  va_end(ap);
  (void)buf;
}

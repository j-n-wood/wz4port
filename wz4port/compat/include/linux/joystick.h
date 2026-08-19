/****************************************************************************/
/***                                                                      ***/
/***   <linux/joystick.h> compatibility stub — wz4port                    ***/
/***                                                                      ***/
/****************************************************************************/
//
// altona/main/base/system_linux.cpp includes <linux/joystick.h>
// unconditionally (line 28) and uses it for sLinuxJoypad, which reads
// /dev/input/js* via the Linux joystick API. There is no config guard
// around that code.
//
// Rather than patch upstream or fork the 2,274-line system_linux.cpp, we
// supply the handful of declarations it needs. The behaviour on macOS is
// correct without any further work: sInitJoypads() scans for /dev/input/js*,
// finds nothing, and registers no joypads. The ioctls below are therefore
// never issued.
//
// Values match the Linux UAPI headers so that, on Linux, the real header is
// found first on the include path and this file is never consulted.
//
// Input devices are out of scope for wz4port (see docs/00-overview.md), so
// this stub is expected to remain a stub.

#ifndef FILE_WZ4PORT_COMPAT_LINUX_JOYSTICK_H
#define FILE_WZ4PORT_COMPAT_LINUX_JOYSTICK_H

#include <stdint.h>
#include <sys/ioctl.h>

/****************************************************************************/
/***   Event interface (linux/joystick.h)                                 ***/
/****************************************************************************/

#define JS_EVENT_BUTTON  0x01    /* button pressed/released */
#define JS_EVENT_AXIS    0x02    /* joystick moved */
#define JS_EVENT_INIT    0x80    /* initial state of device */

struct js_event
{
  uint32_t time;                 /* event timestamp in milliseconds */
  int16_t  value;                /* value */
  uint8_t  type;                 /* event type */
  uint8_t  number;               /* axis/button number */
};

#define JSIOCGVERSION       _IOR('j', 0x01, uint32_t)
#define JSIOCGAXES          _IOR('j', 0x11, uint8_t)
#define JSIOCGBUTTONS       _IOR('j', 0x12, uint8_t)
#define JSIOCGNAME(len)     _IOC(_IOC_READ, 'j', 0x13, len)
#define JSIOCGAXMAP         _IOR('j', 0x32, uint8_t[ABS_MAX + 1])

/****************************************************************************/
/***   Absolute axis codes (linux/input-event-codes.h)                    ***/
/****************************************************************************/

#define ABS_X        0x00
#define ABS_HAT0X    0x10
#define ABS_HAT3Y    0x17
#define ABS_MAX      0x3f

/****************************************************************************/

#endif  // FILE_WZ4PORT_COMPAT_LINUX_JOYSTICK_H

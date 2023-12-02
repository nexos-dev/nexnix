# NexNix
NexNix is an attempt to create a modern, multi-server, microkernel based Unix. It is based off the POSIX API and will be compatible with the Unix programs we know and love, but would will be more modern in design.

In the use of a microkernel based system, we will achieve the following goals:

**Understandable** - NexNix will have an understandable codebase, as it will will be split into many smaller layers
**Modular** - NexNix will be modular and extensible
**Secure** - by keeping the kernel footprint low, NexNix will have less of a risk of kernel mode abuse and instead will have a powerful security architecture enforced on both drivers and programs
**Stable** - NexNix will be more stable, as a bug in a driver generally will result in simple a local failure in that part of the system, and not a general system-wide crash.

## What about performance?
Microkernel-based systems are traditionally slower than their monolithic counterparts. NexNix will overcome this through the following measures:
**Well-defined layers** - by defining the system layers in a way to reduce the number of messages being sent, the message-passing overhead will be alleviated
**Simple, efficient message-passing** - Messaging in NexNix will be a simple operation, designed not to cost a ton of CPU cycles. It will be highly optimized and will not be expensive, unlike traditional microkernel-based systems.
**Take advantage of CPU / hardware functionality** - modern hardware contains many features that could potentially make messaging and system call overhead lower, and hence make the microkernel design paradigm more practical for general-purpose OSes.

## Index of Repos
**Base system** - in this repo<br>
**LibNex** - a library containing basic functions such as reference counting, type management, linked lists, endian conversions, bit operation helpers, and many other useful functions - https://github.com/nexos-dev/libnex.git<br>
**LibConf** - a library that implements the standard NexNix configuration file format -
https://github.com/nexos-dev/libconf.git<br>
**Nnpkg** - an efficient, simple, package manager with an emphasis on ease-of-use and performance - https://github.com/nexos-dev/nnpkg.git

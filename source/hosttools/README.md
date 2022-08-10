# Host tools
The host tools consist of programs that are not shipped with NexNix, but are required to build it. They include:
- nnbuild, a dependency manager to build packages with differing build systems in the right order
- nnelf2efi, a program to convert relocatable ELF images to PE images
- and nnimage, a program to manage image files effeciently and portably

Both programs use libconf to work with configuration files

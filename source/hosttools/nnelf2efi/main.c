/*
    main.c - contains nnelf2efi core
    Copyright 2022, 2023 The NexNix Project

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

         http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/// @file main.c

#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <libnex.h>

// FIXME: this code is half endian respecting, half not
// Should be fine until we run on a big-endian host...

// Output file
static const char* outputFile = NULL;

// Input file
static const char* inputFile = NULL;

// Make ELF structures consistent
#ifdef IS_ELF32
#define Elf_Ehdr   Elf32_Ehdr
#define Elf_Shdr   Elf32_Shdr
#define Elf_Rel    Elf32_Rel
#define Elf_Rela   Elf32_Rela
#define ELF_R_TYPE ELF32_R_TYPE
#else
#define Elf_Ehdr   Elf64_Ehdr
#define Elf_Shdr   Elf64_Shdr
#define Elf_Rel    Elf64_Rel
#define Elf_Rela   Elf64_Rela
#define ELF_R_TYPE ELF64_R_TYPE
#endif

// PE Headers
typedef struct _dosHeader
{
    uint16_t sig;          // Signature of "MZ"
    uint8_t unused[58];    // Unused old MZ fields
    uint32_t peHdrOff;     // Offset to PE header
    uint8_t pad[232];      // Pad out
} __attribute__ ((packed)) dosHeader_t;

#define MZ_MAGIC 0x5A4D
#define MZ_PAD   232

typedef struct _coffHeader
{
    uint16_t machine;        // Machine type of COFF image
    uint16_t numSections;    // Contains number of sections in image
    uint32_t timestamp;      // Time stamp of image
    uint32_t symTabOff;      // Offset to symbol table
    uint32_t numSymbols;     // Number of symbols in above table
    uint16_t optHdrSz;       // Size of optional header
    uint16_t flags;          // Flags of image
} __attribute__ ((packed)) coffHeader_t;

// Machine types
#define COFF_MACHINE_NONE    0
#define COFF_MACHINE_386     0x14C
#define COFF_MACHINE_AMD64   0x8664
#define COFF_MACHINE_ARM     0x1C0
#define COFF_MACHINE_ARM64   0xAA64
#define COFF_MACHINE_RISCV32 0x5032
#define COFF_MACHINE_RISCV64 0x5064

// COFF flags that are relevant to us
#define COFF_FLAG_IMAGE 0x2       // File is image
#define COFF_FLAG_IS32  0x100     // File is 32 bit
#define COFF_FLAG_DLL   0x2000    // File is DLL

// PE pointer size
#ifdef IS_ELF32
#define peptr_t uint32_t
#else
#define peptr_t uint64_t
#endif

typedef struct _peOptHeader
{
    uint16_t magic;         // Contains 0x10B for PE32, 0x20B for PE32+
    uint8_t linkVerMaj;     // Major linker version
    uint8_t linkVerMin;     // Minor linker version
    uint32_t codeSz;        // Size of text region
    uint32_t initDataSz;    // Size of initialized data
    uint32_t zeroDataSz;    // Size of uninitialized data
    uint32_t entryPoint;    // Address of entry point
    uint32_t codeBase;      // Base of code region
#ifdef IS_ELF32
    uint32_t dataBase;    // Base of data region
#endif
    peptr_t imgBase;       // Base address of image
    uint32_t secAlign;     // Alignment of sections in memory
    uint32_t fileAlign;    // Aligment of sections in file
    uint16_t majOsVer;     // Tons of version fields
    uint16_t minOsVer;
    uint16_t majImgVer;
    uint16_t minImgVer;
    uint16_t majSubSysVer;
    uint16_t minSubSysVer;
    uint32_t win32Ver;     // Unused, we are not Win32 :)
    uint32_t imageSz;      // Size of image file
    uint32_t headerSz;     // Size of all headers
    uint32_t checksum;     // Checksum of image
    uint16_t subsystem;    // Subsystem of image
    uint16_t dllFlags;     // DLL flags
    peptr_t stackResvd;
    peptr_t stackCommit;
    peptr_t heapResvd;
    peptr_t heapCommit;
    uint32_t resvd;
    uint32_t numDataDirs;    // Number of data directories
} __attribute__ ((packed)) peOptHeader_t;

// PE magic numbers
#define PE_MAGIC_32     0x10B
#define PE_MAGIC_32PLUS 0x20B

// Subsystems
#define PE_SUBSYSTEM_EFIIMG     10
#define PE_SUBSYSTEM_EFIBOOTDRV 11
#define PE_SUBSYSTEM_EFIRTDRV   12
#define PE_SUBSYSTEM_EFIFW      13

// DLL flags
#define PE_DLLFLAG_DYNBASE 0x40

// Alignment value
#define PE_ALIGN_FILE 0x200
#define PE_ALIGN_MEM  0x1000

// Number of data directories
#define PE_DATADIR_COUNT 16

// Data directory types
#define PE_DATADIR_RELOC 5

typedef struct _peHeader
{
    uint8_t sig[4];          // Contains "PE\0\0"
    coffHeader_t coffHdr;    // COFF header
    peOptHeader_t optHdr;    // The not-so-optional header
} __attribute__ ((packed)) peHeader_t;

typedef struct _dataDir
{
    uint32_t addr;    // RVA of data directory
    uint32_t size;    // Size of data directory
} __attribute__ ((packed)) peDataDir_t;

// Section header
typedef struct _secHeader
{
    uint8_t name[8];           // Name of section
    uint32_t size;             // Size of section in memory
    uint32_t addr;             // Address in memory
    uint32_t rawSize;          // Size in file
    uint32_t rawOffset;        // Offset in file
    uint32_t relocOffset;      // Offset to relocations (unused)
    uint32_t lineNumOffset;    // Offest to line numbers (unused)
    uint16_t numRelocs;        // The number of relocations
    uint16_t numLineNums;      // The number of line numbers
    uint32_t flags;            // Flags of header
} __attribute__ ((packed)) peSectionHeader_t;

// Section flags
#define PE_SECFLAG_CODE     0x20
#define PE_SECFLAG_INITDATA 0x40
#define PE_SECFLAG_ZERODATA 0x80
#define PE_SECFLAG_EXE      0x20000000
#define PE_SECFLAG_READABLE 0x40000000
#define PE_SECFLAG_WRITABLE 0x80000000
#define PE_SECFLAG_NOPAGE   0x08000000
#define PE_SECFLAG_DISCARD  0x02000000

// Base relocation block header
typedef struct _relocBlock
{
    uint32_t pageRva;    // Page to which these relocations apply
    uint32_t size;       // Number of bytes in this block, including this header
} __attribute__ ((packed)) relocBlock_t;

// Relocation types
#define PE_REL_ABS         0
#define PE_REL_16          2
#define PE_REL_32          3
#define PE_REL_64          10
#define PE_REL_RISCV_HI20  5
#define PE_REL_RISCV_LO20I 7
#define PE_REL_RISCV_LO20S 8

// Machine type
static int machineType = 0;

// Word size of machine
static int wordSz = 0;

// Main PE header
static peHeader_t peHdr;

// Section list
static ListHead_t* sectionList = NULL;

// Relocation list
static ListHead_t* relocList = NULL;

// Size of relocation table needed
static size_t relocTabSz = 0;

// Actual relocation table
static void* relocTab = NULL;

// Relocation data directory
static peDataDir_t relDataDir;

// Section string table
static uint8_t* secStrTable = NULL;
static uint64_t secStrSize = 0;

// Number of section
static uint16_t numSections = 0;

// Code base and size
static uint32_t codeBase = 0, codeSize = 0;

// Data base and size
static uint32_t dataBase = 0;
static uint32_t initDataSz = 0, zeroDataSz = 0;

// Size of image
static uint32_t imageSize = 0;

// Next section base
static uint32_t nextSection = 0;

// Intermediate section structure
typedef struct _tmpSec
{
    peSectionHeader_t secHdr;    // Real PE section header
    void* data;                  // Pointer to in memory copy of data
    uint64_t size;               // Size of section data
} tempSection_t;

// Intermediate reloction structure
typedef struct _reloc
{
    uint32_t addr;    // Offset at which to compute relocation
    uint8_t type;     // Type of relocation
} relocation_t;

// Relocation section header
static tempSection_t* relocSec = NULL;

// Alignment helpers
static inline uint32_t peAlignFile (uint32_t val)
{
    return (val & (PE_ALIGN_FILE - 1))
               ? ((val + PE_ALIGN_FILE) & ~(PE_ALIGN_FILE - 1))
               : val;
}

static inline uint32_t peAlignMem (uint32_t val)
{
    return (val & (PE_ALIGN_MEM - 1)) ? ((val + PE_ALIGN_MEM) & ~(PE_ALIGN_MEM - 1))
                                      : val;
}

static void destroyEntry (const void* data)
{
    free ((void*) data);
}

// Reads a string from an ELF file
static uint8_t* readElfString (uint32_t idx)
{
    // Bounds check
    if (idx >= secStrSize)
    {
        error ("section name string out of bounds");
        return NULL;
    }
    return &secStrTable[idx];
}

// Parse relocations
static bool processElfRelRelocations (void* base, Elf_Shdr* relocSec)
{
    if (!relocList)
    {
        relocList = ListCreate ("relocation_t", false, 0);
        if (!relocList)
            return false;
        ListSetDestroy (relocList, destroyEntry);
    }
    // Loop through relocations in section
    uint32_t numRelocs = relocSec->sh_size / sizeof (Elf_Rel);
    Elf_Rel* relocs = relocSec->sh_offset + base;
    // Set initial relocation table size
    relocTabSz = sizeof (relocBlock_t);
    uint32_t lastPage = relocs->r_offset;
    uint16_t numRelocsInPage = 0;
    for (int i = 0; i < numRelocs; ++i)
    {
        // Check if we need to add a new relocation block header
        if (relocs[i].r_offset >= (lastPage + 0x1000))
        {
            // Check if padding needs to be added to preserve alignment
            if (numRelocsInPage % 2)
                relocTabSz += 2;
            relocTabSz += sizeof (relocBlock_t);
            lastPage += 0x1000;
        }
        // Create new relocation
        relocation_t* rel = calloc_s (sizeof (relocation_t));
        if (!rel)
        {
            ListDestroy (relocList);
            return false;
        }
        ListAddBack (relocList, rel, 0);
        rel->addr = relocs[i].r_offset;
        // Figure out type
        uint16_t type = ELF_R_TYPE (relocs[i].r_info);
        if (machineType == EM_386)
        {
            switch (type)
            {
                case R_386_16:
                    rel->type = PE_REL_16;
                    break;
                case R_386_32:
                    rel->type = PE_REL_32;
                    break;
                case R_386_PC32:
                case R_386_PC16:
                case R_386_NONE:
                    rel->type = PE_REL_ABS;
                    break;
                default:
                    error ("unsupported relocation %d", type);
                    ListDestroy (relocList);
                    return false;
            }
        }
        else if (machineType == EM_X86_64)
        {
            switch (type)
            {
                case R_X86_64_16:
                    rel->type = PE_REL_16;
                    break;
                case R_X86_64_32:
                case R_X86_64_32S:
                    rel->type = PE_REL_32;
                    break;
                case R_X86_64_64:
                    rel->type = PE_REL_64;
                    break;
                case R_X86_64_NONE:
                case R_X86_64_PC16:
                case R_X86_64_PC32:
                case R_X86_64_PC64:
                    rel->type = PE_REL_ABS;
                    break;
                default:
                    error ("unsupported relocation %d", type);
                    ListDestroy (relocList);
                    return false;
            }
        }
        relocTabSz += 2;
        ++numRelocsInPage;
    }
    return true;
}

static bool processElfRelaRelocations (void* base, Elf_Shdr* relocSec)
{
    if (!relocList)
    {
        relocList = ListCreate ("relocation_t", false, 0);
        if (!relocList)
            return false;
        ListSetDestroy (relocList, destroyEntry);
    }
    // Loop through relocations in section
    uint32_t numRelocs = relocSec->sh_size / sizeof (Elf_Rela);
    Elf_Rela* relocs = relocSec->sh_offset + base;
    // Set initial relocation table size
    relocTabSz = sizeof (relocBlock_t);
    uint32_t lastPage = relocs->r_offset;
    uint16_t numRelocsInPage = 0;
    for (int i = 0; i < numRelocs; ++i)
    {
        // Check if we need to add a new relocation block header
        if (relocs[i].r_offset >= (lastPage + 0x1000))
        {
            // Check if padding needs to be added to preserve alignment
            if (numRelocsInPage % 2)
                relocTabSz += 2;
            relocTabSz += sizeof (relocBlock_t);
            lastPage += 0x1000;
        }
        // Create new relocation
        relocation_t* rel = calloc_s (sizeof (relocation_t));
        if (!rel)
        {
            ListDestroy (relocList);
            return false;
        }
        ListAddBack (relocList, rel, 0);
        rel->addr = relocs[i].r_offset;
        // Figure out type
        uint16_t type = ELF_R_TYPE (relocs[i].r_info);
        if (machineType == EM_386)
        {
            switch (type)
            {
                case R_386_16:
                    rel->type = PE_REL_16;
                    break;
                case R_386_32:
                    rel->type = PE_REL_32;
                    break;
                case R_386_PC32:
                case R_386_PC16:
                case R_386_NONE:
                    rel->type = PE_REL_ABS;
                    break;
                default:
                    error ("unsupported relocation %d", type);
                    ListDestroy (relocList);
                    return false;
            }
        }
        else if (machineType == EM_X86_64)
        {
            switch (type)
            {
                case R_X86_64_16:
                    rel->type = PE_REL_16;
                    break;
                case R_X86_64_32:
                case R_X86_64_32S:
                    rel->type = PE_REL_32;
                    break;
                case R_X86_64_64:
                    rel->type = PE_REL_64;
                    break;
                case R_X86_64_NONE:
                case R_X86_64_PC16:
                case R_X86_64_PC32:
                case R_X86_64_PC64:
                    rel->type = PE_REL_ABS;
                    break;
                default:
                    error ("unsupported relocation %d", type);
                    ListDestroy (relocList);
                    return false;
            }
        }
        relocTabSz += 2;
        ++numRelocsInPage;
    }
    return true;
}

// Creates relocation section
static bool createRelocSection()
{
    if (!relocList)
    {
        error ("relocation table not found");
        return false;
    }
    // Allocate relocation table
    relocTab = calloc_s (peAlignFile (relocTabSz));
    if (!relocTab)
        return false;
    // Iterate through relocation list
    uint32_t pageBase = 0;
    ListEntry_t* entry = ListFront (relocList);
    int i = 0;
    relocBlock_t* block = relocTab;
    void* oRelocTab = relocTab;
    while (entry)
    {
        relocation_t* reloc = ListEntryData (entry);
        // Check if we need to start a new block
        if (reloc->addr >= (pageBase + 0x1000))
        {
            // Move to aligned area
            if (i % 2)
            {
                block->size += 2;
                relocTab += 2;
            }
            // Increase page base
            pageBase += 0x1000;
            block = (relocBlock_t*) relocTab;
            block->pageRva = pageBase;
            block->size = 8;
            relocTab += 8;
        }
        // Set up relocation
        uint16_t* relocEnt = (uint16_t*) relocTab;
        *relocEnt = ((reloc->addr - pageBase) & 0x0FFF) | (reloc->type << 12);
        // Move to next entry
        relocTab += 2;
        ++i;
        block->size += 2;
        entry = ListIterate (entry);
    }
    // Prepare section
    relocSec = calloc_s (sizeof (tempSection_t));
    relocSec->data = oRelocTab;
    relocSec->size = relocTabSz;
    relocSec->secHdr.flags = PE_SECFLAG_READABLE | PE_SECFLAG_DISCARD |
                             PE_SECFLAG_INITDATA | PE_SECFLAG_NOPAGE;
    memcpy (relocSec->secHdr.name, ".reloc\0\0", 8);
    relocSec->secHdr.addr = nextSection;
    relocSec->secHdr.rawSize = peAlignFile (relocSec->size);
    relocSec->secHdr.size = relocSec->size;
    ListAddBack (sectionList, relocSec, 0);
    ++numSections;
    nextSection = relocSec->secHdr.addr + peAlignMem (relocSec->size);
    imageSize += peAlignMem (relocSec->size);
    // Set up data directory
    relDataDir.addr = relocSec->secHdr.addr;
    relDataDir.size = relocSec->secHdr.size;
    // Increase intialized data size
    initDataSz += relDataDir.size;
    return true;
}

// Processes all sections in an ELF file, adding them to the sections list
static bool processElfSections (Elf_Ehdr* elf)
{
    sectionList = ListCreate ("tempSection_t", false, 0);
    if (!sectionList)
        return false;
    ListSetDestroy (sectionList, destroyEntry);
    // Get section table
    if (!elf->e_shoff)
    {
        error ("input ELF must contain section header");
        return false;
    }
    Elf_Shdr* sec = (Elf_Shdr*) (((void*) elf) + elf->e_shoff);
    // Get section name string table
    if (!elf->e_shstrndx || sec[elf->e_shstrndx].sh_type != SHT_STRTAB)
    {
        error ("no section name string table found");
        return false;
    }
    // Get base and size of table
    secStrTable = sec[elf->e_shstrndx].sh_offset + (void*) elf;
    secStrSize = sec[elf->e_shstrndx].sh_size;
    for (int i = 0; i < elf->e_shnum; ++i)
    {
        // Obtain section name
        uint8_t* secName = readElfString (sec[i].sh_name);
        // Check contents of section to see if we can skip it
        if (sec[i].sh_type == SHT_NULL || sec[i].sh_type == SHT_SHLIB ||
            sec[i].sh_type == SHT_NOTE || sec[i].sh_type == SHT_DYNAMIC ||
            sec[i].sh_type == SHT_HASH)
        {
            // Drop this section
            warn ("dropping section %s with unrecognized type", secName);
            continue;
        }
        // Check if this is a relocation section
        else if (sec[i].sh_type == SHT_REL)
        {
            // Process relocations
            if (!processElfRelRelocations (elf, &sec[i]))
                return false;
        }
        else if (sec[i].sh_type == SHT_RELA)
        {
            if (!processElfRelaRelocations (elf, &sec[i]))
                return false;
        }
        else
        {
            // Check alignment. All sections neccesary to be copied have 4K
            // alignment
            if (sec[i].sh_addralign != PE_ALIGN_MEM)
            {
                warn ("dropping unaligned section \"%s\"", secName);
                continue;
            }
            // Create a new section list entry
            tempSection_t* peSec =
                (tempSection_t*) calloc_s (sizeof (tempSection_t));
            if (!peSec)
                return false;
            // Add to list
            ListAddBack (sectionList, peSec, 0);
            // Initailize data start and size
            peSec->data = sec[i].sh_offset + (void*) elf;
            peSec->size = (sec[i].sh_type == SHT_NOBITS) ? 0 : sec[i].sh_size;
            // Translate to PE values
            peSec->secHdr.size = EndianChange32 (sec[i].sh_size, ENDIAN_LITTLE);
            if (sec[i].sh_type == SHT_NOBITS)
                peSec->secHdr.rawSize = 0;
            else
                peSec->secHdr.rawSize =
                    peAlignFile (EndianChange32 (sec[i].sh_size, ENDIAN_LITTLE));
            // Set up address and stuff
            peSec->secHdr.addr = EndianChange32 (sec[i].sh_addr, ENDIAN_LITTLE);
            // Add flags
            uint32_t flags = PE_SECFLAG_READABLE;
            if (BitMask (sec[i].sh_flags, SHF_EXECINSTR))
            {
                // Set code base and size
                codeBase = peSec->secHdr.addr;
                codeSize = peSec->secHdr.size;
                // Set flags
                flags = flags | PE_SECFLAG_CODE | PE_SECFLAG_EXE | PE_SECFLAG_NOPAGE;
            }
            else if (sec[i].sh_type == SHT_PROGBITS)
            {
                // Set data base if it hasn't been set
                if (!dataBase)
                    dataBase = sec[i].sh_addr;
                initDataSz += peSec->secHdr.size;
                flags |= PE_SECFLAG_INITDATA;
                if (BitMask (sec[i].sh_flags, SHF_WRITE))
                    flags |= PE_SECFLAG_WRITABLE;
                flags |= PE_SECFLAG_NOPAGE;
            }
            else if (sec[i].sh_type == SHT_NOBITS)
            {
                zeroDataSz += peSec->secHdr.size;
                flags |= PE_SECFLAG_ZERODATA;
                if (BitMask (sec[i].sh_flags, SHF_WRITE))
                    flags |= PE_SECFLAG_WRITABLE;
                flags |= PE_SECFLAG_NOPAGE;
            }
            imageSize += peAlignMem (peSec->secHdr.size);
            nextSection = peSec->secHdr.addr + peAlignMem (peSec->secHdr.size);
            peSec->secHdr.flags = flags;
            // Set name
            size_t nameLen = strlen (secName);
            memcpy (peSec->secHdr.name, secName, (nameLen > 8) ? 8 : nameLen);
            numSections++;
        }
    }
    return true;
}

// Initializes PE section headers
static void createPeSections (peSectionHeader_t* secHdrBuf)
{
    ListEntry_t* entry = ListFront (sectionList);
    uint32_t offset = PE_ALIGN_MEM;
    for (int i = 0; i < numSections; ++i)
    {
        assert (entry);
        tempSection_t* sec = ListEntryData (entry);
        // Set raw offset
        if (sec->secHdr.rawSize)
        {
            sec->secHdr.rawOffset = offset;
            offset += sec->secHdr.rawSize;
        }
        memcpy (&secHdrBuf[i], &sec->secHdr, sizeof (peSectionHeader_t));
        entry = ListIterate (entry);
    }
}

// Writes out all sections
bool writePeSections (int outputFd)
{
    ListEntry_t* entry = ListFront (sectionList);
    while (entry)
    {
        tempSection_t* sec = ListEntryData (entry);
        if (sec->secHdr.rawSize)
        {
            // Allocate buffer size of aligned section
            uint8_t* sectBuf = calloc_s (sec->secHdr.rawSize);
            if (!sectBuf)
                return false;
            // Copy over raw data from ELF
            memcpy (sectBuf, sec->data, sec->size);
            // Make sure file offest and current offest pointer are equal
            assert (sec->secHdr.rawOffset == lseek (outputFd, 0, SEEK_CUR));
            // Write it out
            write (outputFd, sectBuf, sec->secHdr.rawSize);
            free (sectBuf);
        }
        entry = ListIterate (entry);
    }
    return true;
}

// Parses arguments
static bool parseArgs (int argc, char** argv)
{
#define VALIDOPTS "ho:"
    int arg = 0;
    while ((arg = getopt (argc, argv, VALIDOPTS)) != -1)
    {
        switch (arg)
        {
            case 'h':
                printf ("\
%s - converts an ELF image to an EFI image\n\
Usage: %s [-h] [-o OUTPUT] INPUT\n\
Valid Arguments:\n\
  -h\n\
          prints this menu\n\
  -o\n\
          specifies output file\n",
                        argv[0],
                        argv[0]);
                return false;
            case 'o':
                outputFile = optarg;
                break;
            case '?':
                error ("unknown argument '%c'", optopt);
                return false;
        }
    }
    return true;
}

// Entry point to program
int main (int argc, char** argv)
{
    setprogname (argv[0]);
    if (!parseArgs (argc, argv))
        return false;
    // Ensure required arguments were passed
    if (!outputFile)
    {
        error ("output file not specified");
        return 1;
    }
    inputFile = argv[optind];
    if (!inputFile)
    {
        error ("input file not specified");
        return 1;
    }

    // Get size of file
    struct stat st;
    if (stat (inputFile, &st) == -1)
    {
        error ("%s: %s", inputFile, strerror (errno));
        return 1;
    }
    // Map ELF to memory
    int inputFd = open (inputFile, O_RDONLY);
    if (inputFd == -1)
    {
        error ("%s: %s", inputFile, strerror (errno));
        return 1;
    }
    Elf_Ehdr* hdr =
        (Elf_Ehdr*) mmap (NULL, st.st_size, PROT_READ, MAP_PRIVATE, inputFd, 0);
    if (hdr == MAP_FAILED)
    {
        error ("%s: %s", inputFile, strerror (errno));
        munmap ((void*) hdr, st.st_size);
        return 1;
    }
    close (inputFd);
    // Make sure it's an ELF file
    if (hdr->e_ident[EI_MAG0] != ELFMAG0 || hdr->e_ident[EI_MAG1] != ELFMAG1 ||
        hdr->e_ident[EI_MAG2] != ELFMAG2 || hdr->e_ident[EI_MAG3] != ELFMAG3)
    {
        error ("input is not valid ELF file");
        munmap ((void*) hdr, st.st_size);
        return 1;
    }
    // Create output
    int outputFd = open (outputFile, O_WRONLY | O_TRUNC | O_CREAT, 0755);
    if (outputFd == -1)
    {
        error ("%s: %s", outputFile, strerror (errno));
        return 1;
    }
    // Get basic information
    machineType = hdr->e_machine;
    wordSz = hdr->e_ident[EI_CLASS];
    // Ensure file is little endian
    if (hdr->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        error ("only little endian ELFs are supproted");
        return 1;
    }

    // Initialize DOS header
    dosHeader_t dosHdr;
    memset (&dosHdr, 0, sizeof (dosHeader_t));
    dosHdr.sig = EndianChange16 (MZ_MAGIC, ENDIAN_LITTLE);
    dosHdr.peHdrOff = sizeof (dosHeader_t);

    // Initialize PE header
    memset (&peHdr, 0, sizeof (peHeader_t));
    memcpy (&peHdr.sig, "PE\0\0", 4);
    // Initalize COFF part
    peHdr.coffHdr.timestamp = EndianChange32 (time (NULL), ENDIAN_LITTLE);
    peHdr.coffHdr.optHdrSz = EndianChange16 (
        sizeof (peOptHeader_t) + (PE_DATADIR_COUNT * sizeof (peDataDir_t)),
        ENDIAN_LITTLE);
    peHdr.coffHdr.flags = COFF_FLAG_IMAGE;
#ifdef IS_ELF32
    peHdr.coffHdr.flags |= COFF_FLAG_IS32;
#endif
    peHdr.coffHdr.flags |= COFF_FLAG_DLL;
    peHdr.coffHdr.flags = EndianChange16 (peHdr.coffHdr.flags, ENDIAN_LITTLE);
    // Set machine type
#ifdef IS_ELF32
    if (machineType == EM_386)
        peHdr.coffHdr.machine = EndianChange16 (COFF_MACHINE_386, ENDIAN_LITTLE);
    else
    {
        error ("unknown machine type in ELF header");
        close (outputFd);
        munmap ((void*) hdr, st.st_size);
        return 1;
    }
#else
    if (machineType == EM_X86_64)
        peHdr.coffHdr.machine = EndianChange16 (COFF_MACHINE_AMD64, ENDIAN_LITTLE);
    else if (machineType == EM_AARCH64)
        peHdr.coffHdr.machine = EndianChange16 (COFF_MACHINE_ARM64, ENDIAN_LITTLE);
    else if (machineType == EM_RISCV && wordSz == ELFCLASS64)
        peHdr.coffHdr.machine = EndianChange16 (COFF_MACHINE_RISCV64, ENDIAN_LITTLE);
    else
    {
        error ("unknown machine type in ELF header");
        close (outputFd);
        munmap ((void*) hdr, st.st_size);
        return 1;
    }
#endif
    // Set optional header fields
#ifdef IS_ELF32
    peHdr.optHdr.magic = EndianChange16 (PE_MAGIC_32, ENDIAN_LITTLE);
#else
    peHdr.optHdr.magic = EndianChange16 (PE_MAGIC_32PLUS, ENDIAN_LITTLE);
#endif
    peHdr.optHdr.fileAlign = EndianChange32 (PE_ALIGN_FILE, ENDIAN_LITTLE);
    peHdr.optHdr.linkVerMaj = 1;
    peHdr.optHdr.linkVerMin = 0;
    peHdr.optHdr.majImgVer = 0;
    peHdr.optHdr.minImgVer = 1;
    peHdr.optHdr.numDataDirs = EndianChange32 (PE_DATADIR_COUNT, ENDIAN_LITTLE);
    peHdr.optHdr.secAlign = EndianChange32 (PE_ALIGN_MEM, ENDIAN_LITTLE);
    peHdr.optHdr.subsystem = EndianChange16 (PE_SUBSYSTEM_EFIIMG, ENDIAN_LITTLE);
    peHdr.optHdr.entryPoint = hdr->e_entry;

    // Process ELF sections
    if (!processElfSections (hdr))
    {
        close (outputFd);
        munmap ((void*) hdr, st.st_size);
        return 1;
    }
    // Create relocation table section
    if (!createRelocSection())
    {
        close (outputFd);
        munmap ((void*) hdr, st.st_size);
        return 1;
    }
    peHdr.coffHdr.numSections = numSections;
    // Set header size
    peHdr.optHdr.headerSz = EndianChange32 (
        peAlignMem (sizeof (peHeader_t) + sizeof (dosHeader_t) +
                    (PE_DATADIR_COUNT * sizeof (peDataDir_t)) +
                    (peHdr.coffHdr.numSections * sizeof (peSectionHeader_t))),
        ENDIAN_LITTLE);

    // Set bases and sizes in PE header
    peHdr.optHdr.codeBase = codeBase;
    peHdr.optHdr.codeSz = codeSize;
#ifdef IS_ELF32
    peHdr.optHdr.dataBase = dataBase;
#endif
    peHdr.optHdr.initDataSz = initDataSz;
    peHdr.optHdr.zeroDataSz = zeroDataSz;
    // Write out PE headers, first creating buffer to contain all the headers
    uint8_t* hdrBuf = calloc_s (PE_ALIGN_MEM);
    if (!hdrBuf)
    {
        close (outputFd);
        munmap ((void*) hdr, st.st_size);
        ListDestroy (sectionList);
        ListDestroy (relocList);
        return 1;
    }
    // Copy out DOS header
    memcpy (hdrBuf, &dosHdr, sizeof (dosHeader_t));
    // Copy data directories
    memcpy (hdrBuf + sizeof (dosHeader_t) + sizeof (peHeader_t) +
                (PE_DATADIR_RELOC * sizeof (peDataDir_t)),
            &relDataDir,
            sizeof (peDataDir_t));
    // Create section tables
    createPeSections (
        (peSectionHeader_t*) (hdrBuf + sizeof (dosHeader_t) + sizeof (peHeader_t) +
                              (PE_DATADIR_COUNT * sizeof (peDataDir_t))));
    imageSize += peHdr.optHdr.headerSz;
    peHdr.optHdr.imageSz = imageSize;
    // Copy out PE header
    memcpy (hdrBuf + sizeof (dosHeader_t), &peHdr, sizeof (peHeader_t));
    // Write out headers
    write (outputFd, hdrBuf, PE_ALIGN_MEM);
    // Write out sections
    writePeSections (outputFd);
    // Clean everything up
    ListDestroy (sectionList);
    ListDestroy (relocList);
    free (hdrBuf);
    close (outputFd);
    munmap ((void*) hdr, st.st_size);
    return 0;
}

/*
babelfish - self-propagating Just-In-Time IOS patcher

Copyright (C) 2008-2011		Haxx Enterprises <bushing@gmail.com>
Copyright (C) 2026		nitr8

This code is licensed to you under the terms of the GNU GPL, version 2;
see file COPYING or http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt

This code lives at http://gitweb.bootmii.org/?p=babelfish.git
*/

#include "types.h"
#include "utils.h"
#include "hollywood.h"
#include "elf.h"
#include "gecko.h"
#include "vectors_bin.h"
#include "ff.h"
#include "string.h"
#include "sdhc.h"


#define DEBUG				0


// Known issues:
// MEMHOLE_ADDR was picked by looking at the ELF headers for a few versions of IOS
// some spot that wasn't used; it would be better to programmatically determine
// this while reloading IOS, and move it around as necessary.
// 
// PPC patching doesn't work, see below.  Half of the code here is for PPC patching,
// it'd sure be nice if it worked (or we should get rid of it if we can't fix it)
// 
// syscall numbering might change between IOS versions, need to check this
// 
// Need to hook two different places for patching, depending on the IOS version --
// we should either find a better place, or automatically choose the correct one
// 
// We freak out if we try to reload MINI.
// 
// Big chunks of this code could be cleaned up and/or refactored, probably.


// CONFIG OPTIONS

// as opposed to hooking xchange_osvers -- only works on newer versions of IOS?
#define USE_RELOAD_IOS			1

// XXX this shouldn't be hardcoded, and if you change it it must also change in start.S and babelfish.ld!
// This is where we save a copy of ourselves to use when we reload into a new IOS
#define MEMHOLE_ADDR			0x13A80000

// We should be able to use this framework to patch PPC code, but I can't get it to work.
// Either it thinks it's patched code (but the PPC doesn't see the updated code), or it hangs the PPC.
// I spent a couple of weeks trying to get this to work, but failed -- 
// I'd really like it if someone fixed this. :)
#define PPCHAX				0

// END CONFIG OPTIONS

// these are the syscalls we want to hook, either for debug spew or to actually change behavior
// FIXME -- can these change between IOS versions?
#define SYSCALL_CREATE_THREAD		0x0
#define SYSCALL_JOIN_THREAD		0x1
#define SYSCALL_DESTROY_THREAD		0x2
#define SYSCALL_GET_THREAD_ID		0x3
#define SYSCALL_GET_PROCESS_ID		0x4
#define SYSCALL_START_THREAD		0x5
#define SYSCALL_STOP_THREAD		0x6
#define SYSCALL_YIELD_THREAD		0x7
#define SYSCALL_GET_THREAD_PRIO		0x8
#define SYSCALL_SET_THREAD_PRIO		0x9
#define SYSCALL_CREATE_MSG_Q		0xA
#define SYSCALL_DESTROY_MSG_Q		0xB
#define SYSCALL_SEND_MSG		0xC
#define SYSCALL_JAM_MSG			0xD
#define SYSCALL_RCV_MSG			0xE
#define SYSCALL_HANDLE_EV		0xF
#define SYSCALL_UNHANDLE_EV		0x10
#define SYSCALL_CREATE_TIMER		0x11
#define SYSCALL_RESTART_TIMER		0x12
#define SYSCALL_STOP_TIMER		0x13
#define SYSCALL_DESTROY_TIMER		0x14
#define SYSCALL_GET_TIMER		0x15
#define SYSCALL_CREATE_HEAP		0x16
#define SYSCALL_DESTROY_HEAP		0x17
#define SYSCALL_ALLOC			0x18
#define SYSCALL_ALLOC_ALIGNED		0x19
#define SYSCALL_FREE			0x1a
#define SYSCALL_REGISTER_RM		0x1b
#define SYSCALL_OPEN			0x1c
#define SYSCALL_CLOSE			0x1d
#define SYSCALL_READ			0x1e
#define SYSCALL_WRITE			0x1f
#define SYSCALL_SEEK			0x20
#define SYSCALL_IOCTL			0x21
#define SYSCALL_IOCTLV			0x22
#define SYSCALL_OPEN_ASYNC		0x23
#define SYSCALL_CLOSE_ASYNC		0x24
#define SYSCALL_READ_ASYNC		0x25
#define SYSCALL_WRITE_ASYNC		0x26
#define SYSCALL_SEEK_ASYNC		0x27
#define SYSCALL_IOCTL_ASYNC		0x28
#define SYSCALL_IOCTLV_ASYNC		0x29
#define SYSCALL_RESOURCEREPLY		0x2a
#define SYSCALL_SET_UID			0x2b
#define SYSCALL_GET_UID			0x2c
#define SYSCALL_SET_GID			0x2d
#define SYSCALL_GET_GID			0x2e
#define SYSCALL_FLUSHMEM		0x2f
#define SYSCALL_INVALRDB		0x30
#define SYSCALL_CLRENIPCIOPINTR		0x31
#define SYSCALL_CLRENDIINTR		0x32
#define SYSCALL_CLRENSDINTR		0x33
#define SYSCALL_CLRENEVT		0x34
#define SYSCALL_ACC_IOBPOOL		0x35
#define SYSCALL_ALLOCIOB		0x36
#define SYSCALL_FREEIOB			0x37
#define SYSCALL_DBGDUMPIOBFREEHDRLIST	0x38
#define SYSCALL_DBGDUMPIOBFREEBUFLIST	0x39
#define SYSCALL_PUTIOB			0x3a
#define SYSCALL_PUSHIOB			0x3b
#define SYSCALL_PULLIOB			0x3c
#define SYSCALL_VALIDIOB		0x3d
#define SYSCALL_CLONEIOB		0x3e
#define SYSCALL_INVALDCACHE		0x3f
#define SYSCALL_FLUSHDCACHE		0x40
#define SYSCALL_LAUNCHELF		0x41
#define SYSCALL_LAUNCHOS		0x42
#define SYSCALL_LAUNCHOSFROMMEM		0x43
#define SYSCALL_RESETDI			0x44
#define SYSCALL_RELEASEDI		0x45
#define SYSCALL_ISDIRESET		0x46
#define SYSCALL_GETOSVER		0x47
#define SYSCALL_GETBOOTVER		0x48
#define SYSCALL_GETDDRVENID		0x49
#define SYSCALL_GETHWID			0x4a
#define SYSCALL_GETUSAGE		0x4b
#define SYSCALL_SETLOMEMOSVER		0x4c
#define SYSCALL_GETLOMEMOSVER		0x4d
#define SYSCALL_SETDISPINUP		0x4e
#define SYSCALL_VTOP			0x4f
#define SYSCALL_SETDVDREADDIS		0x50
#define SYSCALL_GETDVDREADDIS		0x51
#define SYSCALL_SETENAHBPI2DI		0x52
#define SYSCALL_GETENAHBPI2DI		0x53
#define SYSCALL_SETPPCACRPERMS		0x54
#define SYSCALL_GETCORECLK		0x55
#define SYSCALL_ACRREGWR		0x56
#define SYSCALL_DDRREGWR		0x57
#define SYSCALL_OUTPUTLED		0x58
#define SYSCALL_SETIPCACCRIGHTS		0x59
#define SYSCALL_LAUNCHRM		0x5a

#if DEBUG
#define dprintf				printf
#else
#define dprintf(...)
#endif

#define BOOT2_LOADER_HEADER_SIZE	0x10


// save space if we're not going to try to patch PPC code
#if PPCHAX

// This is a patch to __fwrite in the Nintendo SDK to redirect all output to USBGecko
static u32 fwrite_patch[] = {
	0x9421ffd0, 0x7c0802a6, 0x90010034, 0xbf210014,
	0x7c9b2378, 0x7cdc3378, 0x7C7A1B78, 0x7CB92B78,
	0x38800000, 0x7F83E378, 0x4800B2A9, 0x2C030000,
	0x40820010, 0x7F83E378, 0x3880FFFF, 0x4800B295,
	0x7FDBC9D7, 0x4182001C, 0x881C000A, 0x2C000000,
	0x40820010, 0x801C0004, 0x5400577F, 0x4082000C,
	0x38600000, 0x48000290, 0x28000002, 0x40820008,
	0x48000FDD, 0x807C0004, 0x3BE00001, 0x38800000,
	0x54606FFF, 0x41820010, 0x54603FBE, 0x28000002,
	0x7C8521D7, 0x40810084, 0x3CE0CD00, 0x3D40CD00,
	0x3D60CD00, 0x60E76814, 0x614A6824, 0x616B6820,
	0x38C00000, 0x7C0618AE, 0x5400A016, 0x6408B000,
	0x380000D0, 0x90070000, 0x7C0006AC, 0x910A0000,
	0x7C0006AC, 0x38000019, 0x900B0000, 0x7C0006AC,
	0x800B0000, 0x7C0004AC, 0x70090001, 0x4082FFF4,
	0x800A0000, 0x7C0004AC, 0x39200000, 0x91270000,
	0x7C0006AC, 0x74090400, 0x4182FFB8, 0x38C60001,
	0x7F862000, 0x409EFFA0, 0x7CA32B78, 0x4E800020
/*
	0x7c8429d6, 0x39400000, 0x9421fff0, 0x93e1000c,
	0x7f8a2000, 0x409c0064, 0x3d00cd00, 0x3d60cd00,
	0x3d20cd00, 0x61086814, 0x616b6824, 0x61296820,
	0x398000d0, 0x38c00019, 0x38e00000, 0x91880000,
	0x7c0350ae, 0x5400a016, 0x6400b000, 0x900b0000,
	0x90c90000, 0x80090000, 0x701f0001, 0x4082fff8,
	0x800b0000, 0x90e80000, 0x540037fe, 0x7d4a0214,
	0x7f8a2000, 0x419cffc8, 0x7ca32b78, 0x83e1000c,
	0x38210010, 0x4e800020
*/
};

static u32 sig_fwrite[] = {
	0x9421FFD0, 0x7C0802A6, 0x90010034, 0xBF210014,
	0x7C9B2378, 0x7CDC3378, 0x7C7A1B78, 0x7CB92B78
};
#endif
// keep track of how many times we think we patched the code
//static u32 fwrite_count = 0;

// we don't actually use this struct, but we probably should :(
typedef struct
{
	u32 hdrsize;
	u32 loadersize;
	u32 elfsize;
	u32 argument;
} ioshdr;

typedef void(*reload_ios_func)(u32 *, u32);
reload_ios_func reload_ios = NULL;

typedef void(*stuff_EXI_stub_func)(u32, u32 *, u32);
stuff_EXI_stub_func stuff_EXI_stub = NULL;

typedef void(*powerpc_reset_func)(void);
powerpc_reset_func powerpc_reset = NULL;

enum context
{
	C_UNKNOWN = -1,
	C_BOOT0 = 0,
	C_BOOT1,
	C_BOOT2L,
	C_IOSLDR,
	C_KERNEL,
	C_CRYPTO,
	C_FS_V,
	C_ES_V,
	C_DI_V,
	C_STM_V,
	C_SDI_V,
	C_OH0,
	C_OH1,
	C_KD,
	C_WD,
	C_WL,
	C_NCD,
	C_USBETH,
	C_KBD,
	C_SSL,
	C_BOOTMII,

	// [nitr8]: New stuff
	C_MEM2,
	C_SDBOOT,
	C_SO,
	C_PS,
	C_STM_P,
	C_IOBUFS,
	C_RESERVED,
	C_SYSCALL,
	C_SDI_P,
	C_FS_P,
	C_ES_P,
	C_DI_P,
	C_SHARED,
	C_PPCBOOT,
	C_EHC,
	C_EHC_DMA,
	C_OH1_DMA,
	C_OH0_DMA,
	C_KERNEL_DATA,
	C_P2P,
	C_APPS,
	C_DI_DMA,
	C_REALLOC_PPC,
	C_KD_VF,
	C_USB,
	C_USBMSC
};

volatile u32 babelfish_starlet_syscall_lr = 0;
u32 babelfish_starlet_syscall_handler_orig = 0;
extern void babelfish_starlet_syscall_shim(void);

static inline __attribute__((always_inline)) u32 bf_get_lr(void)
{
	u32 lr;

	__asm__ volatile("mov %0, lr" : "=l"(lr));

	return lr;
}

static inline __attribute__((always_inline)) u32 bf_norm_pc(u32 pc)
{
	pc &= ~1U;

	if ((pc >> 17) == 0x7ff0)
		pc = (pc & 0x1ffff) | 0x0d400000;

	return pc;
}

static int get_context_id_from_pc(u32 pc)
{
	switch (pc >> 16)
	{
		case 0x0002:
			return C_BOOT2L;
		case 0x0d40:
			return C_BOOT1;
		case 0x1000:
			return C_MEM2;
		case 0x1010:
			return C_IOSLDR;
		case 0x1040:
			return C_BOOTMII;
		case 0x1347:
		case 0x1348:
			return C_REALLOC_PPC;
		case 0x135c:
			return C_SDBOOT;
		case 0x1360:
			return C_DI_DMA;
		case 0x1362:
			return C_APPS;
		case 0x1365:
		case 0x1366:
		case 0x1367:
		case 0x1370:
		case 0x1373:
			return C_USB;
		case 0x1375:
			return C_P2P;
		case 0x1385:
		case 0x1386:
			return C_KERNEL_DATA;
		case 0x1387:
			return C_OH0_DMA;
		case 0x1388:
			return C_OH1_DMA;
		case 0x1389:
			return C_EHC_DMA;
		case 0x138a:
			return C_OH0;
		case 0x138b:
			return C_OH1;
		case 0x138c:
			return C_EHC;
		case 0x138e:
			return C_PPCBOOT;
		case 0x138f:
			return C_SHARED;
		case 0x139b:
			return C_DI_P;
		case 0x139f:
			return C_ES_P;
		case 0x13a1:
			return C_FS_P;
		case 0x13a7:
			return C_CRYPTO;
		case 0x13a9:
			return C_SDI_P;
		case 0x13aa:
			return C_USBETH;
		case 0x13ac:
			return C_SYSCALL;
		case 0x13ae:
			return C_RESERVED;
		case 0x13b4:
		case 0x13b5:
		case 0x13b6:
			return C_SO;
		case 0x13c4:
			return C_IOBUFS;
		case 0x13cc:
		case 0x13cd:
		case 0x13ce:
		case 0x13cf:
		case 0x13d0:
		case 0x13d1:
		case 0x13d2:
		case 0x13d6:
			return C_SSL;
		case 0x13d8:
			return C_STM_P;
		case 0x13d9:
		case 0x13da:
			return C_NCD;
		case 0x13db:
		case 0x13dc:
		case 0x13dd:
		case 0x13de:
		case 0x13df:
		case 0x13e0:
		case 0x13e2:
		case 0x13e8:
		case 0x13e9:
			return C_KD_VF;
		case 0x13eb:
			return C_WD;
		case 0x13ed:
		case 0x13ee:
		case 0x13ef:
		case 0x13f0:
		case 0x13f1:
		case 0x13f2:
		case 0x13f3:
			return C_WL;
		case 0x13ff:
			return C_PS;
		case 0x1401:
			return C_USBMSC;
		case 0x1764:
			return C_APPS;
		case 0x1765:
		case 0x1768:
		case 0x1769:
		case 0x176a:
		case 0x176b:
		case 0x176c:
		case 0x176d:
		case 0x176e:
		case 0x176f:
		case 0x1770:
			return C_KBD;
		case 0x2000:
			return C_FS_V;
		case 0x2010:
			return C_ES_V;
		case 0x2020:
			return C_DI_V;
		case 0x2030:
			return C_STM_V;
		case 0x2040:
			return C_SDI_V;
		default:
			return C_UNKNOWN;
	}
}

static void babelfish_print_context_tag(int ctx, u32 pc)
{
	switch (ctx)
	{
		case C_BOOT0:
			printf("[BOOT0] ");
			break;
		case C_BOOT1:
			printf("[BOOT1] ");
			break;
		case C_BOOT2L:
			printf("[BOOT2L] ");
			break;
		case C_IOSLDR:
			printf("[IOSLDR] ");
			break;
		case C_KERNEL:
			printf("[KERNEL] ");
			break;
		case C_CRYPTO:
			printf("[CRYPTO] ");
			break;
		case C_FS_V:
			printf("[FS_V] ");
			break;
		case C_ES_V:
			printf("[ES_V] ");
			break;
		case C_DI_V:
			printf("[DI_V] ");
			break;
		case C_STM_V:
			printf("[STM_V] ");
			break;
		case C_SDI_V:
			printf("[SDI_V] ");
			break;
		case C_OH0:
			printf("[OH0] ");
			break;
		case C_OH1:
			printf("[OH1] ");
			break;
		case C_KD:
			printf("[KD] ");
			break;
		case C_WD:
			printf("[WD] ");
			break;
		case C_WL:
			printf("[WL] ");
			break;
		case C_NCD:
			printf("[NCD] ");
			break;
		case C_USBETH:
			printf("[USBETH] ");
			break;
		case C_KBD:
			printf("[KBD] ");
			break;
		case C_SSL:
			printf("[SSL] ");
			break;
		case C_BOOTMII:
			printf("[BOOTMII] ");
			break;
		case C_MEM2:
			printf("[MEM2] ");
			break;
		case C_SDBOOT:
			printf("[SDBOOT] ");
			break;
		case C_SO:
			printf("[SO] ");
			break;
		case C_PS:
			printf("[PS] ");
			break;
		case C_STM_P:
			printf("[STM_P] ");
			break;
		case C_IOBUFS:
			printf("[IOBUFS] ");
			break;
		case C_RESERVED:
			printf("[RESERVE] ");
			break;
		case C_SYSCALL:
			printf("[SYSCALL] ");
			break;
		case C_SDI_P:
			printf("[SDI_P] ");
			break;
		case C_FS_P:
			printf("[FS_P] ");
			break;
		case C_ES_P:
			printf("[ES_P] ");
			break;
		case C_DI_P:
			printf("[DI_P] ");
			break;
		case C_SHARED:
			printf("[SHARED] ");
			break;
		case C_PPCBOOT:
			printf("[PPCBOOT] ");
			break;
		case C_EHC:
			printf("[EHC] ");
			break;
		case C_EHC_DMA:
			printf("[EHC_DMA] ");
			break;
		case C_OH1_DMA:
			printf("[OH1_DMA] ");
			break;
		case C_OH0_DMA:
			printf("[OH0_DMA] ");
			break;
		case C_KERNEL_DATA:
			printf("[KERNEL_DATA] ");
			break;
		case C_P2P:
			printf("[P2P] ");
			break;
		case C_APPS:
			printf("[APPS] ");
			break;
		case C_DI_DMA:
			printf("[DI_DMA] ");
			break;
		case C_REALLOC_PPC:
			printf("[REALLOC_PPC] ");
			break;
		case C_KD_VF:
			printf("[KD-VF] ");
			break;
		case C_USB:
			printf("[USB] ");
			break;
		case C_USBMSC:
			printf("[USBMSC] ");
			break;
		default:
			printf("[0x%04x] ", pc >> 16);
			break;
	}
}

void replace_ios_loader(u32 *buffer)
{
	u32 cookie = irq_kill();
	u32 *me = (u32 *)MEMHOLE_ADDR;

	printf("new hdr length = %x\n", buffer[0]);
	printf("new ELF offset = %x\n", buffer[1]);
	printf("new ELF length = %x\n", buffer[2]);
	printf("new param = %x\n", buffer[3]);

	printf("our hdr length = %x\n", me[0]);
	printf("our ELF offset = %x\n", me[1]);
	printf("our ELF length = %x\n", me[2]);
	printf("our param = %x\n", me[3]);

	// problem:  when IOS is loaded from NAND, it's 3 parts: IOS header, ELF loader, ELF
	// we want to overwrite the ELF loader with our own code, but it's probably bigger
	// than the existing IOS ELF loader
	//
	// solution: move the ELF file over in memory and modify the IOS header to give ourselves enough
	// space to copy in babelfish as the new ELF loader

	// buffer[1] = ELF loader size -- we use this to detect if we already patched ourselves into this or whatever

	// should never happen, but can happen with e.g. MINI
	if (buffer[1] > me[1])
	{
		dprintf("wtf, their loader is bigger than ours :(\n");
		irq_restore(cookie);
		panic(0x40);
	}

	if (buffer[1] == me[1])
	{
		dprintf("already using our loader, nothing to hax\n");
	}
	// buffer[1] < me[1]
	else
	{
		u32 *old_elf_start = buffer + ((buffer[1] + buffer[0]) / 4);
		u32 *new_elf_start = buffer + ((me[1] + me[0]) / 4);

		dprintf("moving %x bytes from %x to %x\n", buffer[2], (u32)old_elf_start, (u32)new_elf_start);

		// copy in backwards order because these buffers overlap
		memcpyr(new_elf_start, old_elf_start, buffer[2]);

		dprintf("move complete\n");
		dprintf("copying in loader from %x to %x\n", (u32)&me[4], (u32)&buffer[4]);

		memcpyr(&buffer[4], &me[4], me[1]);
		dprintf("done\n");

		// copy over ELF offset and size
		buffer[1] = me[1];
		buffer[2] = me[2];
		dprintf("pwnt\n");
	}

	irq_restore(cookie);
}

// here is where the magic happens -- note that this is really just reimplementing most of reload_ios
// from IOS, because there was no easy way to just hook it

// option 1:
// here is where the magic happens -- note that this is really just reimplementing most of reload_ios
// from IOS, because there was no easy way to just hook it.  This option is better than option 2,
// because we don't need to hardcode the value of buffer; IIRC, this code is not present on early versions of IOS.
#if USE_RELOAD_IOS
void reload_ios_wrapper(u32 *buffer, u32 version)
{
	u32 cookie = irq_kill();

	printf("reload_ios(%x, %x)\n", (u32)buffer, version);

	// XXX: WHAT ABOUT RESTORING THE IRQ COOKIE???
	set32(HW_ARMIRQMASK, 0);

	replace_ios_loader(buffer);

	printf("Here goes nothing...\n");

	// magic pokes from reload_ios()
	dc_flush();
	disable_icache_dcache_mmu();
	write32(0x3118, 0x04000000);
	write32(0x311C, 0x04000000);
	write32(0x3120, 0x93400000);
	write32(0x3124, 0x90000800);
	write32(0x3128, 0x933E0000);
	write32(0x3130, 0x933E0000);
	write32(0x3134, 0x93400000);
	write32(0x3140, version);
	write32(0x3148, 0x93400000);
	write32(0x314C, 0x93420000);
	write32(0x0d010018, 1);
	jump_to_r0(buffer);

	// should not be reached
	printf("wtf am I here\n");
	irq_restore(cookie);
	panic(0x14);
}

#else // !USE_RELOAD_IOS

// option 2:
// we really need to figure out how to find the buffer we need at runtime if we intend on using this
u32 xchange_osvers_and_patch(u32 version)
{
	// safe to hardcode?  NO --
	u32 *buffer = (u32 *)0x10100000;

	u32 oldvers = read32(0x3140);

	printf("xchange_osvers_and_patch(%x)\n", version);

	if (version && (version != oldvers))
	{
//		printf("! Set OS version to %u !\n", version);
		write32(0x3140, version);
//		dc_flush();
	}

	replace_ios_loader(buffer);

	printf("! Returning old OS version %x !\n", oldvers);

	// I don't remember why I tried this, but this would immediately execute the new IOS
//	jump_to_r0(buffer);

	return oldvers;
}
#endif

#if PPCHAX
// This code is a mess because I was flailing around trying to get PPC patching
// to work, sorry ... this code *doesn't* work.
void stuff_EXI_stub_wrapper(u32 which_stub, u32 *insns, u32 len)
{
	u32 addr;
	u32 *mem1 = (u32 *)0x1330000;

	printf("stuff_EXI_stub(%x, %x, %x)\n", which_stub, (u32)insns, len);
	printf("Looking for fwrite\n");
	printf("81330000: %08x %08x %08x %08x\n", mem1[0], mem1[1], mem1[2], mem1[3]);

	for (addr = 0; addr < 0x1800000; addr += 4)
	{
		if (!memcmp((void *)addr, sig_fwrite, sizeof(sig_fwrite)))
		{
			int i;
			u32 *ptr = (u32 *)addr;

			printf("found fwrite at %x, patching\n", addr);

			for (i = 0; i < 16; i += 4)
			{
				printf("%08x: %08x %08x %08x %08x\n",
					((u32)addr) + (i * 4), ptr[i], ptr[i + 1], ptr[i + 2], ptr[i + 3]);
			}

//			memcpyr((void *)addr, fwrite_patch, sizeof fwrite_patch);
//			break;
		}
	}

	stuff_EXI_stub(which_stub, insns, len);
	printf("stuff_EXI_stub done\n");
}

void powerpc_reset_wrapper(void)
{
	u32 addr;
	u32 time_next;
//	u32 *mem1 = (u32 *)0x1330000;
	u32 *hw_ppcirqmask = (u32 *)0x0D800034;
	u32 *hw_reset = (u32 *)0x0D800194;
	u32 *hw_timer = (u32 *)0x0D800010;

//	printf("powerpc_reset()\n");
//	printf("Looking for fwrite\n");
//	printf("81330000: %08x %08x %08x %08x\n", mem1[0], mem1[1], mem1[2], mem1[3]);
//	powerpc_reset();

	for (addr = 0; addr < 0x1800000; addr += 4)
	{
		if (!memcmp((void *)addr, sig_fwrite, sizeof(sig_fwrite)))
		{
			u32 i;
			u32 *ptr = (u32 *)addr;

//			fwrite_count++;
			printf("found fwrite at %x, patching\n", addr);

			for (i = 0; i < 16; i += 4)
			{
				printf("%08x: %08x %08x %08x %08x\n",
					((u32)addr) + (i * 4), ptr[i], ptr[i + 1], ptr[i + 2], ptr[i + 3]);
			}

//			memcpyr((void *)addr, fwrite_patch, sizeof(fwrite_patch));

			for (i = 0; i < (sizeof(fwrite_patch) / 4); i++)
			{
				ptr[i] = fwrite_patch[i];
			}

			break;
		}
	}

//	dc_flushall();
	*hw_ppcirqmask = 0x40000000;
	*hw_reset &= ~0x30;

	// sleep. this isn't exactly right...
	for (time_next = *hw_timer + 0xF; time_next < *hw_timer;);

	*hw_reset |= 0x20;

	// sleep
	for (time_next = *hw_timer + 0x96; time_next < *hw_timer;);

	*hw_reset |= 0x10;

//	printf("powerpc_reset done\n");
}

u32 *find_ppcreset(void)
{
	int i;
	u32 magic[] = { 0x0d800034, 0x0d800194 };
	u32 *kernel = (u32 *)0xFFFF0000;

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if ((kernel[i] == magic[0]) && (kernel[i + 1] == magic[1]))
		{
			for (; i >= 0; i--)
			{
				// push {R4-R6,LR}, prolog
				if ((kernel[i] >> 16) == 0xB570)
				{
					return (kernel + i);
				}
			}

			return NULL;
		}
	}

	return NULL;
}

void find_powerpc_reset(void)
{
	int i;
	u32 magic[] = { 0x0d800034, 0x0d800194 };
	u32 *kernel = (u32 *)0xFFFF0000;

	printf("Looking for powerpc_reset magic\n\n");

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if ((kernel[i] == magic[0]) && (kernel[i + 1] == magic[1]))
		{
			printf("Found powerpc_reset magic at %x\n", 0xFFFF0000 + (i * 4));
			break;
		}
	}

	if (i == (0x10000 / 4))
	{
		printf("Couldn't find powerpc_reset magic\n");
		return;
	}

	for (; i > 0; i--)
	{
		if (kernel[i] == 0xB5704646)
		{
			printf("Found powerpc_reset start at %x\n", 0xFFFF0000 + (i * 4));
			break;
		}
	}

	if (i == 0x10000)
	{
		printf("Couldn't find powerpc_reset start\n");
		return;
	}

	powerpc_reset = (void *)(0xFFFF0000 + (i * 4) + 1);

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if (kernel[i] == (u32)powerpc_reset)
		{
			printf("Found powerpc_reset reference at %x\n", 0xFFFF0000 + (i * 4));
			break;
		}
	}

	if (i == 0x10000)
	{
		printf("Couldn't find powerpc_reset reference\n");
		return;
	}

//	kernel[i] = (u32)&powerpc_reset_wrapper;
	printf("Replaced powerpc_reset reference with %x\n", kernel[i]);
}
#endif

#if USE_RELOAD_IOS

// look for the reload_ios function in a newly-loaded IOS kernel
u32 *find_reload_ios(void)
{
	int i;

	// these numbers are pretty much guaranteed to exist -- see above implementation of reload_ios
	u32 magic[] = { 0x93400000, 0x93420000 };
	u32 *kernel = (u32 *)0xFFFF0000;
	u32 cookie = irq_kill();

	dprintf("Looking for reload_ios\n");

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if ((kernel[i] == magic[0]) && (kernel[i + 1] == magic[1]))
		{
			dprintf("Found reload_ios marker at %x\n", 0xFFFF0000 + (i * 4));

			for (; i >= 0; i--)
			{
				if ((kernel[i] >> 16) == 0xB570)
				{
					dprintf("Found function prolog at %x\n", 0xFFFF0000 + (i * 4));
					irq_restore(cookie);
					return (u32 *)(&kernel[i]);
				}
			}

			// could not find reload_ios, so we're screwed
			dprintf("sry, i haz fail\n");

			irq_restore(cookie);
			return NULL;
		}
	}

	irq_restore(cookie);
	return NULL;
}

#else // !USE_RELOAD_IOS

// look for the xchange_os_version function in a newly-loaded IOS kernel
// kernel:FFFF5A24 B5 30                       PUSH    {R4,R5,LR}
// kernel:FFFF5A26 23 C4 01 9B                 MOVS    R3, 0x3100
// kernel:FFFF5A2A 6C 1D                       LDR     R5, [R3,#0x40]

u32 *find_xchange_osvers(void)
{
	int i;
	u32 magic[] = { 0xB53023C4 };
	u32 *kernel = (u32 *)0xFFFF0000;

	printf("Looking for xchange_osvers\n");

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if (kernel[i] == magic[0])
		{
			printf("Found xchange_osvers start at %x\n", 0xFFFF0000 + (i * 4));
			return (u32 *)(0xFFFF0000 + (i * 4));
		}
	}

	printf("sry, I haz fail! :(\n");
	return NULL;
}
#endif

// this is where we patch teh kernel to add our hooks
void do_kernel_patches(u32 size)
{
	u32 i;
	u32 *addr;

	// this is just to make life easier
	u32 *kernel_mem32 = (u32 *)0xFFFF0000;
	u16 *kernel_mem16 = (u16 *)0xFFFF0000;

	u32 cookie = irq_kill();
#if 0
	printf("do_kernel_patches(%x)\n", size);
#else
	(void)size;
#endif
	// sanity check of the syscall vector table
	if (kernel_mem32[1] != 0xe59ff018)
	{
		printf("ohnoes, unexpected offset to starlet_syscall_handler %x\n", kernel_mem32[1]);
		irq_restore(cookie);
		return;
	}

	if (kernel_mem32[2] != 0xe59ff018)
	{
		printf("ohnoes, unexpected offset to arm_syscall_handler %x\n", kernel_mem32[2]);
		irq_restore(cookie);
		return;
	}
#if 0
	printf("starlet_syscall_handler vector: %x\n", kernel_mem32[9]);
	printf("svc_handler vector2: %x\n", kernel_mem32[10]);
#endif	

	/*
	 * Hook the real Starlet syscall vector early enough to preserve the
	 * original exception LR before IOS dispatch routes through Babelfish
	 * syscall-table wrappers.
	 */
	if (kernel_mem32[9] != (u32)babelfish_starlet_syscall_shim)
	{
		babelfish_starlet_syscall_handler_orig = kernel_mem32[9];
		kernel_mem32[9] = (u32)babelfish_starlet_syscall_shim;
	}

	// SVC patch to get debug output over USBGecko -- we copy the code from vectors.s over some unused
	// code present in all IOSes to make life easier -- SVC 4 is the only one used, but they include
	// functions to call over SVC handlers which we can just blow away. thanks ninty

	// scan from 0xFFFF0000 ... 0xFFFFFFFF looking for SVC 05 instruction
	for (i = 0; i < (0x10000 / 2); i++)
	{
		if ((kernel_mem16[i + 0] == 0x4672) && (kernel_mem16[i + 1] == 0x1c01) && (kernel_mem16[i + 2] == 0x2005))
		{
#if 0
			dprintf("SVC 5 caller found at %0x\n", 0xffff0000 + (i * 2));
#endif
			// copy in SVC vector code
			memcpyr(&kernel_mem16[i], vectors_bin, vectors_bin_size);

			// change SVC vector pointer to point to this new code
			kernel_mem32[10] = (u32)&kernel_mem16[i];
#if 0
			dprintf("patch done\n");
#endif
		}

		// while we're here, look for the mem2 protection code and disable it
		if ((kernel_mem16[i + 0] == 0xB500) && (kernel_mem16[i + 1] == 0x4B09) && (kernel_mem16[i + 2] == 0x2201) &&
			(kernel_mem16[i + 3] == 0x801A) && (kernel_mem16[i + 4] == 0x22F0))
		{
#if 0
			dprintf("Found MEM2 patch at %x\n", 0xffff0000);
#endif
			kernel_mem16[i + 2] = 0x2200;
		}
	}

#if USE_RELOAD_IOS
	// patch reload_ios so that we can infect any IOS we reload
	addr = find_reload_ios();

	// overwrite reload_ios with a jump to our wrapper
	if (addr)
	{
		// ldr r3, $+4 / bx r3
		addr[0] = 0x4B004718;
		addr[1] = (u32)reload_ios_wrapper;
	}

#else // !USE_RELOAD_IOS	

	// patch xchange_osvers so that we can infect any IOS we reload
	addr = find_xchange_osvers();

	// overwrite xchange_osvers with a jump to our wrapper
	if (addr)
	{
		// ldr r3, $+4 / bx r3
		addr[0] = 0x4B004718;

		addr[1] = (u32)xchange_osvers_and_patch;
//		dprintf("wrote %08x %08x to %08x\n", addr[0], addr[1], (u32)addr);
	}
#endif

#if PPCHAX
	find_powerpc_reset();
	addr = find_ppcreset();
	printf("ppcreset = %x\n", (u32)addr);

	// overwrite ppcreboot with a jump to our wrapper
	if (addr)
	{
		addr[0] = 0x4B004718;
		addr[1] = (u32)powerpc_reset_wrapper;
	}
#endif
#if 0
	dprintf("\ndo_kernel_patches done\n");
#endif
	irq_restore(cookie);
}

// this gross patch is necessary to get WC24 debug spew
void do_kd_patch(u8 *buffer, u32 size)
{
	u32 i;
	u32 cookie = irq_kill();

	// I don't remember what this first patch was, sorry
	// u8 match[] = { 0x13, 0xdf, 0x5f, 0xcd, 0x13, 0xdf, 0x5f, 0xa9 };
	// u8 replace[] = { 0x13, 0xdf, 0x5f, 0x45, 0x13, 0xdf, 0x5f, 0x29 };

	u8 match[] = { 0x30, 0x01, 0x28, 0x7f, 0xd9, 0x01, 0x23, 0x00 };
	u8 replace[] = { 0x49, 0x16, 0x20, 0x04, 0xdf, 0xab, 0x23, 0x00 };
#if 0
	dprintf("Looking for can_haz_debug(%x, %x)\n", (u32)buffer, size);
#endif
	for (i = 0; i < (size - 16); i++)
	{
		if (!memcmp(match, buffer + i, 8))
		{
#if 0
			dprintf("Found match @ %x\n", (u32)&buffer[i]);
#endif
			memcpyr(buffer + i, replace, 8);
			irq_restore(cookie);
			return;
		}
	}

	irq_restore(cookie);
}

typedef void (*entryproc)(u32);

typedef struct
{
	u8 *base;
	u32 length;
} iovec;

typedef struct
{
	const u8 *path;
	u32 flags;
	u32 uid;
	u16 gid;
} ioresopen;

typedef struct
{
	u8 *outPtr;
	u32 outLen;
} ioresread;

typedef struct
{
	u8 *inPtr;
	u32 inLen;
} ioreswrite;

typedef struct
{
	s32 offset;
	u32 whence;
} ioresseek;

typedef struct
{
	u32 cmd;
	u8 *inPtr;
	u32 inLen;
	u8 *outPtr;
	u32 outLen;
} ioresioctl;

typedef struct
{
	u32 cmd;
	u32 readCount;
	u32 writeCount;
	iovec *vector;
} ioresioctlv;

typedef struct
{
	u32 cmd;
	s32 status;
	u32 handle;

	union
	{
		ioresopen open;
		ioresread read;
		ioreswrite write;
		ioresseek seek;
		ioresioctl ioctl;
		ioresioctlv ioctlv;
	} args;
} ioresreq;

enum iobpoolid_enum {DEFAULT, TEST1};
typedef enum iobpoolid_enum iosiobpoolid;

typedef struct iosiobuf
{
	// next iobuf in chain
	struct iosiobuf *next;

	// queue of iobuf chains
	struct iosiobuf *link;

	// process private
	void *priv;

	 // head of iobuf
	u8 *head;

	// start of data within iobuf
	u8 *data;

	// length of buffer
	u16 bufLen;

	// data length
	u16 dataLen;

	// flags
	u16 flags;

	// some users need a prority field
	u8 priority;

	// users count
	u8 users;

	// network subsystem private
	void *net;

	// ID tag for verification
	u16 id;

	// reserved
	u16 reserved;

	// id of pool from which buffer is allocated
	u32 __pool;
} iosiobuf;

// these are the function pointers we will use to hook IOS syscalls -
// they will point at the real (old) IOS syscall handlers.  Make a new type
// for each syscall you hook
typedef s32(*ios_createthread_func)(entryproc, void *, void *, u32, u32, u32);
ios_createthread_func ios_createthread = NULL;

typedef s32(*ios_jointhread_func)(u32, void **);
ios_jointhread_func ios_jointhread = NULL;

typedef s32(*ios_destroythread_func)(u32, void *);
ios_destroythread_func ios_destroythread = NULL;

typedef s32(*ios_getthreadid_func)(void);
ios_getthreadid_func ios_getthreadid = NULL;

typedef s32(*ios_getprocessid_func)(void);
ios_getprocessid_func ios_getprocessid = NULL;

typedef s32(*ios_startthread_func)(s32);
ios_startthread_func ios_startthread = NULL;

typedef s32(*ios_stopthread_func)(s32);
ios_stopthread_func ios_stopthread = NULL;

typedef void(*ios_yieldthread_func)(void);
ios_yieldthread_func ios_yieldthread = NULL;

typedef s32(*ios_getthreadprio_func)(s32);
ios_getthreadprio_func ios_getthreadprio = NULL;

typedef s32(*ios_setthreadprio_func)(s32, u32);
ios_setthreadprio_func ios_setthreadprio = NULL;

typedef s32(*ios_createmessagequeue_func)(s32 *, u32);
ios_createmessagequeue_func ios_createmessagequeue = NULL;

typedef s32(*ios_destroymessagequeue_func)(s32);
ios_destroymessagequeue_func ios_destroymessagequeue = NULL;

typedef s32(*ios_sendmessage_func)(s32, s32, u32);
ios_sendmessage_func ios_sendmessage = NULL;

typedef s32(*ios_jammessage_func)(s32, s32, u32);
ios_jammessage_func ios_jammessage = NULL;

typedef s32(*ios_rcvmessage_func)(s32, s32 *, u32);
ios_rcvmessage_func ios_rcvmessage = NULL;

typedef s32(*ios_handleev_func)(u32, s32, s32);
ios_handleev_func ios_handleev = NULL;

typedef s32(*ios_unhandleev_func)(u32);
ios_unhandleev_func ios_unhandleev = NULL;

typedef s32(*ios_createtimer_func)(u32, u32, s32, s32);
ios_createtimer_func ios_createtimer = NULL;

typedef s32(*ios_restarttimer_func)(s32, u32, u32);
ios_restarttimer_func ios_restarttimer = NULL;

typedef s32(*ios_stoptimer_func)(s32);
ios_stoptimer_func ios_stoptimer = NULL;

typedef s32(*ios_destroytimer_func)(s32);
ios_destroytimer_func ios_destroytimer = NULL;

typedef u32(*ios_gettimer_func)(void);
ios_gettimer_func ios_gettimer = NULL;

typedef s32(*ios_createheap_func)(void *, u32);
ios_createheap_func ios_createheap = NULL;

typedef s32(*ios_destroyheap_func)(s32);
ios_destroyheap_func ios_destroyheap = NULL;

typedef void *(*ios_alloc_func)(s32, u32);
ios_alloc_func ios_alloc = NULL;

typedef void *(*ios_allocaligned_func)(s32, u32, u32);
ios_allocaligned_func ios_allocaligned = NULL;

typedef s32(*ios_free_func)(s32, void *);
ios_free_func ios_free = NULL;

typedef s32(*ios_registerrm_func)(char *, s32);
ios_registerrm_func ios_registerrm = NULL;

typedef s32(*ios_open_func)(const char *, u32);
ios_open_func ios_open = NULL;

typedef s32(*ios_close_func)(s32);
ios_close_func ios_close = NULL;

typedef s32(*ios_read_func)(s32, void *, u32);
ios_read_func ios_read = NULL;

typedef s32(*ios_write_func)(s32, void *, u32);
ios_write_func ios_write = NULL;

typedef s32(*ios_seek_func)(s32, s32, u32);
ios_seek_func ios_seek = NULL;

typedef s32(*ios_ioctl_func)(s32, s32, void *, u32, void *, u32);
ios_ioctl_func ios_ioctl = NULL;

typedef s32(*ios_ioctlv_func)(s32, s32, u32, u32, iovec *);
ios_ioctlv_func ios_ioctlv = NULL;

typedef s32(*ios_openasync_func)(const char *, u32, s32, ioresreq *);
ios_openasync_func ios_openasync = NULL;

typedef s32(*ios_closeasync_func)(s32, s32, ioresreq *);
ios_closeasync_func ios_closeasync = NULL;

typedef s32(*ios_readasync_func)(s32, void *, u32, s32, ioresreq *);
ios_readasync_func ios_readasync = NULL;

typedef s32(*ios_writeasync_func)(s32, void *, u32, s32, ioresreq *);
ios_writeasync_func ios_writeasync = NULL;

typedef s32(*ios_seekasync_func)(s32, s32, u32, s32, ioresreq *);
ios_seekasync_func ios_seekasync = NULL;

typedef s32(*ios_ioctlasync_func)(s32, s32, void *, u32, void *, u32, s32, ioresreq *);
ios_ioctlasync_func ios_ioctlasync = NULL;

typedef s32(*ios_ioctlvasync_func)(s32, s32, u32, u32, iovec *, s32, ioresreq *);
ios_ioctlvasync_func ios_ioctlvasync = NULL;

typedef s32(*ios_resourcereply_func)(ioresreq *, s32);
ios_resourcereply_func ios_resourcereply = NULL;

typedef s32(*ios_setuid_func)(s32, u32);
ios_setuid_func ios_setuid = NULL;

typedef u32(*ios_getuid_func)(void);
ios_getuid_func ios_getuid = NULL;

typedef s32(*ios_setgid_func)(s32, u16);
ios_setgid_func ios_setgid = NULL;

typedef u16(*ios_getgid_func)(void);
ios_getgid_func ios_getgid = NULL;

typedef void(*ios_flushmem_func)(s32);
ios_flushmem_func ios_flushmem = NULL;

typedef void(*ios_invalrdb_func)(s32);
ios_invalrdb_func ios_invalrdb = NULL;

typedef s32(*ios_clrenipciopintr_func)(void);
ios_clrenipciopintr_func ios_clrenipciopintr = NULL;

typedef s32(*ios_clrendiintr_func)(void);
ios_clrendiintr_func ios_clrendiintr = NULL;

typedef s32(*ios_clrensdintr_func)(u8);
ios_clrensdintr_func ios_clrensdintr = NULL;

typedef s32(*ios_clrenevt_func)(u32);
ios_clrenevt_func ios_clrenevt = NULL;

typedef s32(*ios_accessiobpool_func)(u32);
ios_accessiobpool_func ios_accessiobpool = NULL;

typedef iosiobuf*(*ios_allociob_func)(u32, u32, u32);
ios_allociob_func ios_allociob = NULL;

typedef s32(*ios_freeiob_func)(iosiobuf *);
ios_freeiob_func ios_freeiob = NULL;

typedef void(*ios_dbgdumpiobfreehdrlist_func)(void);
ios_dbgdumpiobfreehdrlist_func ios_dbgdumpiobfreehdrlist = NULL;

typedef void(*ios_dbgdumpiobfreebuflist_func)(void);
ios_dbgdumpiobfreebuflist_func ios_dbgdumpiobfreebuflist = NULL;

typedef u8 *(*ios_putiob_func)(iosiobuf *, u16);
ios_putiob_func ios_putiob = NULL;

typedef u8 *(*ios_pushiob_func)(iosiobuf *, u16);
ios_pushiob_func ios_pushiob = NULL;

typedef u8 *(*ios_pulliob_func)(iosiobuf *, u16);
ios_pulliob_func ios_pulliob = NULL;

typedef s32(*ios_validiob_func)(iosiobuf *);
ios_validiob_func ios_validiob = NULL;

typedef iosiobuf *(*ios_cloneiob_func)(iosiobuf *);
ios_cloneiob_func ios_cloneiob = NULL;

typedef void(*ios_invaldcache_func)(void *, u32);
ios_invaldcache_func ios_invaldcache = NULL;

typedef void(*ios_flushdcache_func)(void *, u32);
ios_flushdcache_func ios_flushdcache = NULL;

typedef void(*ios_launchosfrommem_func)(u32, u32);
ios_launchosfrommem_func ios_launchosfrommem = NULL;

typedef s32(*ios_resetdi_func)(void);
ios_resetdi_func ios_resetdi = NULL;

typedef s32(*ios_releasedi_func)(void);
ios_releasedi_func ios_releasedi = NULL;

typedef u8(*ios_isdireset_func)(void);
ios_isdireset_func ios_isdireset = NULL;

typedef void(*ios_getosver_func)(u32 *, u16 *);
ios_getosver_func ios_getosver = NULL;

typedef void(*ios_getbootver_func)(u32 *, u16 *);
ios_getbootver_func ios_getbootver = NULL;

typedef u32(*ios_getddrvenids_func)(void);
ios_getddrvenids_func ios_getddrvenids = NULL;

typedef u32(*ios_gethwid_func)(void);
ios_gethwid_func ios_gethwid = NULL;

typedef void(*ios_getusage_func)(u32);
ios_getusage_func ios_getusage = NULL;

typedef s32(*ios_setlomemosver_func)(u32);
ios_setlomemosver_func ios_setlomemosver = NULL;

typedef u32(*ios_getlomemosver_func)(u32);
ios_getlomemosver_func ios_getlomemosver = NULL;

typedef s32(*ios_setdispinup_func)(u32);
ios_setdispinup_func ios_setdispinup = NULL;

typedef void *(*ios_vtop_func)(void *);
ios_vtop_func ios_vtop = NULL;

typedef s32(*ios_setdvdrddis_func)(u8);
ios_setdvdrddis_func ios_setdvdrddis = NULL;

typedef u8(*ios_getdvdrddis_func)(void);
ios_getdvdrddis_func ios_getdvdrddis = NULL;

typedef s32(*ios_setenahbpi2di_func)(u8);
ios_setenahbpi2di_func ios_setenahbpi2di = NULL;

typedef u8(*ios_getenahbpi2di_func)(void);
ios_getenahbpi2di_func ios_getenahbpi2di = NULL;

typedef s32(*ios_setppcacrperms_func)(u8);
ios_setppcacrperms_func ios_setppcacrperms = NULL;

typedef u32(*ios_getcoreclk_func)(void);
ios_getcoreclk_func ios_getcoreclk = NULL;

typedef s32(*ios_acrregwr_func)(u32, u32);
ios_acrregwr_func ios_acrregwr = NULL;

typedef s32(*ios_ddrregwr_func)(u32, u32);
ios_ddrregwr_func ios_ddrregwr = NULL;

typedef void(*ios_outputled_func)(u8);
ios_outputled_func ios_outputled = NULL;

typedef s32(*ios_setipcaccrights_func)(u8 *);
ios_setipcaccrights_func ios_setipcaccrights = NULL;

typedef s32(*ios_launchelf_func)(const char *);
ios_launchelf_func ios_launchelf = NULL;

typedef s32(*ios_launchrm_func)(const char *);
ios_launchrm_func ios_launchrm = NULL;

typedef s32(*ios_launchos_func)(const char *, int, u32);
ios_launchos_func ios_launchos = NULL;

// wrapper functions for hooked syscalls
s32 IOS_CreateThread(entryproc entry, void *arg, void *stack, u32 stacksize, u32 prio, u32 attr)
{
	s32 retval;
	u32 cookie = irq_kill();

	// gross hack -- on later modular IOSes, we can't patch KD when it's loaded
	// because only the kernel gets loaded by our ELF loader/patcher -- so instead,
	// we wait until the module is actually started to do our patch
	if ((((u32)entry) >> 16) == 0x13db)
	{
		do_kd_patch((u8 *)0x13db0000, 0x57000);
	}

	retval = ios_createthread(entry, arg, stack, stacksize, prio, attr);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
	printf("%s(0x%08x, 0x%x, 0x%08x, stacksize: 0x%x, prio: 0x%x, attr: 0x%x)=%d\n", __FUNCTION__, (u32)entry, (u32)arg, (u32)stack, stacksize, prio, attr, retval);
	return retval;
}

s32 IOS_JoinThread(u32 id, void **arg)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_jointhread(id, arg);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(id: 0x%08x, val: 0x%08x)=%d\n", __FUNCTION__, id, (u32)arg, retval);
	return retval;
}

s32 IOS_DestroyThread(u32 id, void *arg)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_destroythread(id, arg);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, %d)=%d\n", __FUNCTION__, id, (u32)arg, retval);
	return retval;
}

s32 IOS_GetThreadId(void)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_getthreadid();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_GetProcessId(void)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_getprocessid();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_StartThread(s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_startthread(id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, id, retval);
	return retval;
}

s32 IOS_StopThread(s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_stopthread(id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(id: 0x%08x)=%d\n", __FUNCTION__, id, retval);
	return retval;
}

void IOS_YieldThread(void)
{
	u32 cookie = irq_kill();
	ios_yieldthread();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()\n", __FUNCTION__);
}

s32 IOS_GetThreadPriority(s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_getthreadprio(id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(id: %d)=%d\n", __FUNCTION__, id, retval);
	return retval;
}

s32 IOS_SetThreadPriority(s32 id, u32 prio)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setthreadprio(id, prio);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(id: %d, prio: %d)=%d\n", __FUNCTION__, id, prio, retval);
	return retval;
}

s32 IOS_CreateMessageQueue(s32 *msg, u32 size)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_createmessagequeue(msg, size);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(0x%08x, cnt: %d)=%d\n", __FUNCTION__, (u32)msg, size, retval);
	return retval;
}

s32 IOS_DestroyMessageQueue(s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_destroymessagequeue(id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(id: %d)=%d\n", __FUNCTION__, id, retval);
	return retval;
}

s32 IOS_SendMessage(s32 id, s32 msg, u32 flag)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_sendmessage(id, msg, flag);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(0x%08x, %08x, flag: %08x)=%d\n", __FUNCTION__, id, msg, flag, retval);
	return retval;
}

s32 IOS_JamMessage(s32 id, s32 msg, u32 flag)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_jammessage(id, msg, flag);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(0x%08x, %d, flag: %d)=%d\n", __FUNCTION__, id, msg, flag, retval);
	return retval;
}

// SPAM
/*
s32 IOS_ReceiveMessage(s32 id, s32 *msg, u32 flag)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_rcvmessage(id, msg, flag);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%08x, %d)=%d\n", __FUNCTION__, id, msg, flag, retval);
	return retval;
}
*/

s32 IOS_HandleEvent(u32 ev, s32 id, s32 msg)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_handleev(ev, id, msg);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, %d, 0x%08x)=%d\n", __FUNCTION__, ev, id, msg, retval);
	return retval;
}

s32 IOS_UnhandleEvent(u32 ev)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_unhandleev(ev);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(evt: %d)=%d\n", __FUNCTION__, ev, retval);
	return retval;
}

s32 IOS_CreateTimer(u32 val, u32 interval, s32 mqid, s32 msg)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_createtimer(val, interval, mqid, msg);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, %d, %d, 0x%08x)=%d\n", __FUNCTION__, val, interval, mqid, msg, retval);
	return retval;
}

s32 IOS_RestartTimer(s32 id, u32 val, u32 interval)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_restarttimer(id, val, interval);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, %d, %d)=%d\n", __FUNCTION__, id, val, interval, retval);
	return retval;
}

s32 IOS_StopTimer(s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_stoptimer(id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, id, retval);
	return retval;
}

s32 IOS_DestroyTimer(s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_destroytimer(id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, id, retval);
	return retval;
}

u32 IOS_GetTimer(void)
{
	u32 retval;
	u32 cookie = irq_kill();
	retval = ios_gettimer();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_CreateHeap(void *ptr, u32 size)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_createheap(ptr, size);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(0x%08x, 0x%x)=%d\n", __FUNCTION__, (u32)ptr, size, retval);
	return retval;
}

s32 IOS_DestroyHeap(s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_destroyheap(id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(id: %d)=%d\n", __FUNCTION__, id, retval);
	return retval;
}

void *IOS_Alloc(s32 id, u32 size)
{
	void *retval;
	u32 cookie = irq_kill();
	retval = ios_alloc(id, size);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%x)=%p\n", __FUNCTION__, id, size, retval);
	return retval;
}

void *IOS_AllocAligned(s32 id, u32 size, u32 align)
{
	void *retval;
	u32 cookie = irq_kill();
	retval = ios_allocaligned(id, size, align);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%x, %d)=%p\n", __FUNCTION__, id, size, align, retval);
	return retval;
}

s32 IOS_Free(s32 id, void *ptr)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_free(id, ptr);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%08x)=%d\n", __FUNCTION__, id, (u32)ptr, retval);
	return retval;
}

s32 IOS_RegisterResourceManager(char *path, s32 id)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_registerrm(path, id);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%s, %d)=%d\n", __FUNCTION__, path, id, retval);
	return retval;
}

s32 IOS_Open(const char *filename, u32 mode)
{
	s32 retval;
	u32 cookie = irq_kill();

	// This is really just for debug spew
	retval = ios_open(filename, mode);
#if 0
	printf("[%s] %s(%s, %d)=%d\n", get_context(), __FUNCTION__, filename, mode, retval);
#else
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%s, %d)=%d\n", __FUNCTION__, filename, mode, retval);
#endif
	return retval;
}

s32 IOS_Close(s32 fd)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_close(fd);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, fd, retval);
	return retval;
}

s32 IOS_Read(s32 fd, void *buf, u32 count)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_read(fd, buf, count);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%08x, 0x%x)=%d\n", __FUNCTION__, fd, (u32)buf, count, retval);
	return retval;
}

s32 IOS_Write(s32 fd, void *buf, u32 count)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_write(fd, buf, count);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%08x, 0x%x)=%d\n", __FUNCTION__, fd, (u32)buf, count, retval);
	return retval;
}

s32 IOS_Seek(s32 fd, s32 offset, u32 whence)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_seek(fd, offset, whence);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, %d, %d)=%d\n", __FUNCTION__, fd, offset, whence, retval);
	return retval;
}

s32 IOS_Ioctl(s32 fd, s32 cmd, void *in, u32 inlen, void *out, u32 outlen)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_ioctl(fd, cmd, in, inlen, out, outlen);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%x, 0x%08x, 0x%x, %08x, 0x%x)=%d\n", __FUNCTION__, fd, cmd, (u32)in, inlen, (u32)out, outlen, retval);
	return retval;
}

s32 IOS_Ioctlv(s32 fd, s32 cmd, u32 read, u32 written, iovec *vec)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_ioctlv(fd, cmd, read, written, vec);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%x, %d, %d, 0x%08x)=%d\n", __FUNCTION__, fd, cmd, read, written, (u32)vec, retval);
	return retval;
}

s32 IOS_OpenAsync(const char *filename, u32 mode, s32 id, ioresreq *reply)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_openasync(filename, mode, id, reply);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%s, %d, %08x, %08x)=%d\n", __FUNCTION__, filename, mode, id, (u32)reply, retval);
	return retval;
}

s32 IOS_CloseAsync(s32 fd, s32 id, ioresreq *reply)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_closeasync(fd, id, reply);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, %08x, %08x)=%d\n", __FUNCTION__, fd, id, (u32)reply, retval);
	return retval;
}

s32 IOS_ReadAsync(s32 fd, void *buf, u32 count, s32 id, ioresreq *reply)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_readasync(fd, buf, count, id, reply);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%08x, 0x%x, %08x, %08x)=%d\n", __FUNCTION__, fd, (u32)buf, count, id, (u32)reply, retval);
	return retval;
}

s32 IOS_WriteAsync(s32 fd, void *buf, u32 count, s32 id, ioresreq *reply)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_writeasync(fd, buf, count, id, reply);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%08x, 0x%x, %08x, %08x)=%d\n", __FUNCTION__, fd, (u32)buf, count, id, (u32)reply, retval);
	return retval;
}

s32 IOS_SeekAsync(s32 fd, s32 offset, u32 whence, s32 id, ioresreq *reply)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_seekasync(fd, offset, whence, id, reply);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, %d, %d, %08x, %08x)=%d\n", __FUNCTION__, fd, offset, whence, id, (u32)reply, retval);
	return retval;
}

s32 IOS_IoctlAsync(s32 fd, s32 cmd, void *in, u32 inlen, void *out, u32 outlen, s32 id, ioresreq *reply)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_ioctlasync(fd, cmd, in, inlen, out, outlen, id, reply);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%x, 0x%08x, 0x%x, %08x, 0x%x, %08x, %08x)=%d\n", __FUNCTION__, fd, cmd, (u32)in, inlen, (u32)out, outlen, id, (u32)reply, retval);
	return retval;
}

s32 IOS_IoctlvAsync(s32 fd, s32 cmd, u32 read, u32 written, iovec *vec, s32 id, ioresreq *reply)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_ioctlvasync(fd, cmd, read, written, vec, id, reply);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d, 0x%x, %d, %d, 0x%08x, %08x, %08x)=%d\n", __FUNCTION__, fd, cmd, read, written, (u32)vec, id, (u32)reply, retval);
	return retval;
}

s32 IOS_ResourceReply(ioresreq *reply, s32 status)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_resourcereply(reply, status);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(0x%08x, 0x%x)=%d\n", __FUNCTION__, (u32)reply, status, retval);
	return retval;
}

s32 IOS_SetUid(s32 id, u32 uid)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setuid(id, uid);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(pid: %d, uid: %d)=%d\n", __FUNCTION__, id, uid, retval);
	return retval;
}

u32 IOS_GetUid(void)
{
	u32 retval;
	u32 cookie = irq_kill();
	retval = ios_getuid();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_SetGid(s32 id, u16 gid)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setgid(id, gid);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(pid: %d, gid: %d)=%d\n", __FUNCTION__, id, gid, retval);
	return retval;
}

u16 IOS_GetGid(void)
{
	u16 retval;
	u32 cookie = irq_kill();
	retval = ios_getgid();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

// SPAM
/*
void IOS_FlushMem(s32 grp)
{
	u32 cookie = irq_kill();
	ios_flushmem(grp);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)\n", __FUNCTION__, grp);
}
*/

// SPAM
/*
void IOS_InvalidateRdb(s32 buf)
{
	u32 cookie = irq_kill();
	ios_invalrdb(buf);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)\n", __FUNCTION__, buf);
}
*/

s32 IOS_ClearAndEnableIPCIOPIntr(void)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_clrenipciopintr();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_ClearAndEnableDIIntr(void)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_clrendiintr();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_ClearAndEnableSDIntr(u8 num)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_clrensdintr(num);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, num, retval);
	return retval;
}

s32 IOS_ClearAndEnableEvent(u32 evt)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_clrenevt(evt);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, evt, retval);
	return retval;
}

s32 IOS_AccessIobPool(iosiobpoolid pool)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_accessiobpool(pool);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, pool, retval);
	return retval;
}

iosiobuf *IOS_AllocIob(u32 pool, u32 size, u32 dbg)
{
	iosiobuf *retval;
	u32 cookie = irq_kill();
	retval = ios_allociob(pool, size, dbg);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(pool: 0x%08x, size: %d, debug: %d)=%p\n", __FUNCTION__, pool, size, dbg, retval);
	return retval;
}

s32 IOS_FreeIob(iosiobuf *ptr)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_freeiob(ptr);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, (u32)ptr, retval);
	return retval;
}

void IOS_DebugDumpIobFreeHdrsList(void)
{
	u32 cookie = irq_kill();
	ios_dbgdumpiobfreehdrlist();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()\n", __FUNCTION__);
}

void IOS_DebugDumpIobFreeBufsList(void)
{
	u32 cookie = irq_kill();
	ios_dbgdumpiobfreebuflist();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()\n", __FUNCTION__);
}

u8 *IOS_PutIob(iosiobuf *iob, u16 len)
{
	u8 *retval;
	u32 cookie = irq_kill();
	retval = ios_putiob(iob, len);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(buf: 0x%08x, len: %d)=%p\n", __FUNCTION__, (u32)iob, len, retval);
	return retval;
}

u8 *IOS_PushIob(iosiobuf *iob, u16 len)
{
	u8 *retval;
	u32 cookie = irq_kill();
	retval = ios_pushiob(iob, len);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(buf: 0x%08x, len: %d)=%p\n", __FUNCTION__, (u32)iob, len, retval);
	return retval;
}

u8 *IOS_PullIob(iosiobuf *iob, u16 len)
{
	u8 *retval;
	u32 cookie = irq_kill();
	retval = ios_pulliob(iob, len);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(buf: 0x%08x, len: %d)=%p\n", __FUNCTION__, (u32)iob, len, retval);
	return retval;
}

s32 IOS_IsValidIob(iosiobuf *iob)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_validiob(iob);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%08x)=%d\n", __FUNCTION__, (u32)iob, retval);
	return retval;
}

iosiobuf *IOS_CloneIob(iosiobuf *iob)
{
	iosiobuf *retval;
	u32 cookie = irq_kill();
	retval = ios_cloneiob(iob);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%08x)=%p\n", __FUNCTION__, (u32)iob, retval);
	return retval;
}

// SPAM
/*
void IOS_InvalidateDCache(void *ptr, u32 size)
{
	u32 cookie = irq_kill();
	ios_invaldcache(ptr, size);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(0x%08x, 0x%x)\n", __FUNCTION__, ptr, size);
}
*/

// SPAM
/*
void IOS_FlushDCache(void *ptr, u32 size)
{
	u32 cookie = irq_kill();
	ios_flushdcache(ptr, size);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(0x%08x, 0x%x)\n", __FUNCTION__, ptr, size);
}
*/

void IOS_LaunchOSFromMemory(u32 addr, u32 ver)
{
	u32 cookie = irq_kill();
	ios_launchosfrommem(addr, ver);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(addr: 0x%08x, version: %d)\n", __FUNCTION__, addr, ver);
}

s32 IOS_ResetDI(void)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_resetdi();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_ReleaseDIReset(void)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_releasedi();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

u8 IOS_IsDIReset(void)
{
	u8 retval;
	u32 cookie = irq_kill();
	retval = ios_isdireset();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

void IOS_GetOSVersion(u32 *major, u16 *minor)
{
	u32 cookie = irq_kill();
	ios_getosver(major, minor);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(major: %p, minor: %p)\n", __FUNCTION__, major, minor);
}

void IOS_GetBootVersion(u32 *major, u16 *minor)
{
	u32 cookie = irq_kill();
	ios_getbootver(major, minor);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(major: %p, minor: %p)\n", __FUNCTION__, major, minor);
}

u32 IOS_GetDDRVendorIds(void)
{
	u32 retval;
	u32 cookie = irq_kill();
	retval = ios_getddrvenids();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

u32 IOS_GetHollywoodId(void)
{
	u32 retval;
	u32 cookie = irq_kill();
	retval = ios_gethwid();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

void IOS_GetUsage(u32 usage)
{
	u32 cookie = irq_kill();
	ios_getusage(usage);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)\n", __FUNCTION__, usage);
}

s32 IOS_SetLoMemOSVersion(u32 ver)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setlomemosver(ver);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, ver, retval);
	return retval;
}

u32 IOS_GetLoMemOSVersion(u32 ver)
{
	u32 retval;
	u32 cookie = irq_kill();
	retval = ios_getlomemosver(ver);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, ver, retval);
	return retval;
}

s32 IOS_SetDiSpinup(u32 s)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setdispinup(s);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, s, retval);
	return retval;
}

// SPAM
/*
void *IOS_VirtualToPhysical(void *virt)
{
	void *retval;
	u32 cookie = irq_kill();
	retval = ios_vtop(virt);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%08x)=%08x\n", __FUNCTION__, virt, retval);
	return retval;
}
*/

s32 IOS_SetDVDReadDisable(u8 disable)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setdvdrddis(disable);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(state = %d)=%d\n", __FUNCTION__, disable, retval);
	return retval;
}

u8 IOS_GetDVDReadDisable(void)
{
	u8 retval;
	u32 cookie = irq_kill();
	retval = ios_getdvdrddis();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_SetEnableAHBPI2DI(u8 enable)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setenahbpi2di(enable);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, enable, retval);
	return retval;
}

u8 IOS_GetEnableAHBPI2DI(void)
{
	u8 retval;
	u32 cookie = irq_kill();
	retval = ios_getenahbpi2di();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

s32 IOS_SetPPCACRPerms(u8 enable)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setppcacrperms(enable);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)=%d\n", __FUNCTION__, enable, retval);
	return retval;
}

u32 IOS_GetCoreClk(void)
{
	u32 retval;
	u32 cookie = irq_kill();
	retval = ios_getcoreclk();
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s()=%d\n", __FUNCTION__, retval);
	return retval;
}

// SPAM
/*
s32 IOS_ACRRegWrite(u32 offset, u32 value)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_acrregwr(offset, value);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(offset: 0x%08x, value: %d)=%d\n", __FUNCTION__, offset, value, retval);
	return retval;
}
*/

// SPAM
/*
s32 IOS_DDRRegWrite(u32 offset, u32 value)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_ddrregwr(offset, value);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(offset: 0x%08x, value: %d)=%d\n", __FUNCTION__, offset, value, retval);
	return retval;
}
*/

void IOS_OutputLed(u8 value)
{
	u32 cookie = irq_kill();
	ios_outputled(value);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%d)\n", __FUNCTION__, value);
}

s32 IOS_SetIpcAccessRights(u8 *rights)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_setipcaccrights(rights);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%08x)=%d\n", __FUNCTION__, (u32)rights, retval);
	return retval;
}

// it would be nice to figure out how to do patching of the loaded PPC content here
s32 IOS_LaunchElf(const char *filename)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_launchelf(filename);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%s)=%d\n", __FUNCTION__,filename,retval);
	return retval;
}

// This is called by modular IOS's kernel to load each module from NAND -- could probably
// do patching here instead of in ios_createthread, but you have to figure out whether or not
// this is the right module to patch somehow
s32 IOS_LaunchRM(const char *filename)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_launchrm(filename);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%s)=%d\n", __FUNCTION__, filename,retval);
	return retval;
}

// mostly just for logging at this point, but maybe would be better to do patches here?
s32 IOS_LaunchOS(const char *filename, int r1, u32 filesize)
{
	s32 retval;
	u32 cookie = irq_kill();
	retval = ios_launchos(filename, r1, filesize);
	irq_restore(cookie);
	babelfish_print_context_tag(get_context_id_from_pc(bf_norm_pc(babelfish_starlet_syscall_lr - 4)), bf_norm_pc(babelfish_starlet_syscall_lr - 4));
printf("%s(%s, reset: %d, version: %d)=%d\n", __FUNCTION__, filename, r1, filesize,retval);
	return retval;
}

// perform patching of syscall table to install our hooks
void handle_syscall_table(u32 *syscall_table, u32 size)
{
	u32 cookie;
#if 0
	u32 i;
	u32 num_syscalls;

	for (i = 0; i < (size / 4); i++)
	{
		if ((syscall_table[i] >> 16) == 0)
		{
			break;
		}
	}

	num_syscalls = i;
	printf("Syscall table @ 0x%x: %x entries\n", (u32)syscall_table, num_syscalls);
#else
	(void)size;
#endif
	cookie = irq_kill();

	// We assume a specifc syscall ordering here, oops.   grab the existing function pointers
	ios_createthread = (void *)syscall_table[SYSCALL_CREATE_THREAD];
	ios_jointhread = (void *)syscall_table[SYSCALL_JOIN_THREAD];
	ios_destroythread = (void *)syscall_table[SYSCALL_DESTROY_THREAD];
	ios_getthreadid = (void *)syscall_table[SYSCALL_GET_THREAD_ID];
	ios_getprocessid = (void *)syscall_table[SYSCALL_GET_PROCESS_ID];
	ios_startthread = (void *)syscall_table[SYSCALL_START_THREAD];
	ios_stopthread = (void *)syscall_table[SYSCALL_STOP_THREAD];
	ios_yieldthread = (void *)syscall_table[SYSCALL_YIELD_THREAD];
	ios_getthreadprio = (void *)syscall_table[SYSCALL_GET_THREAD_PRIO];
	ios_setthreadprio = (void *)syscall_table[SYSCALL_SET_THREAD_PRIO];
	ios_createmessagequeue = (void *)syscall_table[SYSCALL_CREATE_MSG_Q];
	ios_destroymessagequeue = (void *)syscall_table[SYSCALL_DESTROY_MSG_Q];
	ios_sendmessage = (void *)syscall_table[SYSCALL_SEND_MSG];
	ios_jammessage = (void *)syscall_table[SYSCALL_JAM_MSG];

	// SPAM
//	ios_rcvmessage = (void *)syscall_table[SYSCALL_RCV_MSG];

	ios_handleev = (void *)syscall_table[SYSCALL_HANDLE_EV];
	ios_unhandleev = (void *)syscall_table[SYSCALL_UNHANDLE_EV];
	ios_createtimer = (void *)syscall_table[SYSCALL_CREATE_TIMER];
	ios_restarttimer = (void *)syscall_table[SYSCALL_RESTART_TIMER];
	ios_stoptimer = (void *)syscall_table[SYSCALL_STOP_TIMER];
	ios_destroytimer = (void *)syscall_table[SYSCALL_DESTROY_TIMER];
	ios_gettimer = (void *)syscall_table[SYSCALL_GET_TIMER];
	ios_createheap = (void *)syscall_table[SYSCALL_CREATE_HEAP];
	ios_destroyheap = (void *)syscall_table[SYSCALL_DESTROY_HEAP];
	ios_alloc = (void *)syscall_table[SYSCALL_ALLOC];
	ios_allocaligned = (void *)syscall_table[SYSCALL_ALLOC_ALIGNED];
	ios_free = (void *)syscall_table[SYSCALL_FREE];
	ios_registerrm = (void *)syscall_table[SYSCALL_REGISTER_RM];
	ios_open = (void *)syscall_table[SYSCALL_OPEN];
	ios_close = (void *)syscall_table[SYSCALL_CLOSE];
	ios_read = (void *)syscall_table[SYSCALL_READ];
	ios_write = (void *)syscall_table[SYSCALL_WRITE];
	ios_seek = (void *)syscall_table[SYSCALL_SEEK];
	ios_ioctl = (void *)syscall_table[SYSCALL_IOCTL];
	ios_ioctlv = (void *)syscall_table[SYSCALL_IOCTLV];
	ios_openasync = (void *)syscall_table[SYSCALL_OPEN_ASYNC];
	ios_closeasync = (void *)syscall_table[SYSCALL_CLOSE_ASYNC];
	ios_readasync = (void *)syscall_table[SYSCALL_READ_ASYNC];
	ios_writeasync = (void *)syscall_table[SYSCALL_WRITE_ASYNC];
	ios_seekasync = (void *)syscall_table[SYSCALL_SEEK_ASYNC];
	ios_ioctlasync = (void *)syscall_table[SYSCALL_IOCTL_ASYNC];
	ios_ioctlvasync = (void *)syscall_table[SYSCALL_IOCTLV_ASYNC];
	ios_resourcereply = (void *)syscall_table[SYSCALL_RESOURCEREPLY];
	ios_setuid = (void *)syscall_table[SYSCALL_SET_UID];
	ios_getuid = (void *)syscall_table[SYSCALL_GET_UID];
	ios_setgid = (void *)syscall_table[SYSCALL_SET_GID];
	ios_getgid = (void *)syscall_table[SYSCALL_GET_GID];

	// SPAM
//	ios_flushmem = (void *)syscall_table[SYSCALL_FLUSHMEM];

	// SPAM
//	ios_invalrdb = (void *)syscall_table[SYSCALL_INVALRDB];

	ios_clrenipciopintr = (void *)syscall_table[SYSCALL_CLRENIPCIOPINTR];
	ios_clrendiintr = (void *)syscall_table[SYSCALL_CLRENDIINTR];
	ios_clrensdintr = (void *)syscall_table[SYSCALL_CLRENSDINTR];
	ios_clrenevt = (void *)syscall_table[SYSCALL_CLRENEVT];
	ios_accessiobpool = (void *)syscall_table[SYSCALL_ACC_IOBPOOL];
	ios_allociob = (void *)syscall_table[SYSCALL_ALLOCIOB];
	ios_freeiob = (void *)syscall_table[SYSCALL_FREEIOB];
	ios_dbgdumpiobfreehdrlist = (void *)syscall_table[SYSCALL_DBGDUMPIOBFREEHDRLIST];
	ios_dbgdumpiobfreebuflist = (void *)syscall_table[SYSCALL_DBGDUMPIOBFREEBUFLIST];
	ios_putiob = (void *)syscall_table[SYSCALL_PUTIOB];
	ios_pushiob = (void *)syscall_table[SYSCALL_PUSHIOB];
	ios_pulliob = (void *)syscall_table[SYSCALL_PULLIOB];
	ios_validiob = (void *)syscall_table[SYSCALL_VALIDIOB];
	ios_cloneiob = (void *)syscall_table[SYSCALL_CLONEIOB];

	// SPAM
//	ios_invaldcache = (void *)syscall_table[SYSCALL_INVALDCACHE];

	// SPAM
//	ios_flushdcache = (void *)syscall_table[SYSCALL_FLUSHDCACHE];

	ios_launchelf = (void *)syscall_table[SYSCALL_LAUNCHELF];
	ios_launchos = (void *)syscall_table[SYSCALL_LAUNCHOS];
	ios_launchosfrommem = (void *)syscall_table[SYSCALL_LAUNCHOSFROMMEM];
	ios_resetdi = (void *)syscall_table[SYSCALL_RESETDI];
	ios_releasedi = (void *)syscall_table[SYSCALL_RELEASEDI];
	ios_isdireset = (void *)syscall_table[SYSCALL_ISDIRESET];
	ios_getosver = (void *)syscall_table[SYSCALL_GETOSVER];
	ios_getbootver = (void *)syscall_table[SYSCALL_GETBOOTVER];
	ios_getddrvenids = (void *)syscall_table[SYSCALL_GETDDRVENID];
	ios_gethwid = (void *)syscall_table[SYSCALL_GETHWID];
	ios_getusage = (void *)syscall_table[SYSCALL_GETUSAGE];
	ios_setlomemosver = (void *)syscall_table[SYSCALL_SETLOMEMOSVER];
	ios_getlomemosver = (void *)syscall_table[SYSCALL_GETLOMEMOSVER];
	ios_setdispinup = (void *)syscall_table[SYSCALL_SETDISPINUP];

	// SPAM
//	ios_vtop = (void *)syscall_table[SYSCALL_VTOP];

	ios_setdvdrddis = (void *)syscall_table[SYSCALL_SETDVDREADDIS];
	ios_getdvdrddis = (void *)syscall_table[SYSCALL_GETDVDREADDIS];
	ios_setenahbpi2di = (void *)syscall_table[SYSCALL_SETENAHBPI2DI];
	ios_getenahbpi2di = (void *)syscall_table[SYSCALL_GETENAHBPI2DI];
	ios_setppcacrperms = (void *)syscall_table[SYSCALL_SETPPCACRPERMS];
	ios_getcoreclk = (void *)syscall_table[SYSCALL_GETCORECLK];

	// SPAM
//	ios_acrregwr = (void *)syscall_table[SYSCALL_ACRREGWR];

	// SPAM
//	ios_ddrregwr = (void *)syscall_table[SYSCALL_DDRREGWR];

	ios_outputled = (void *)syscall_table[SYSCALL_OUTPUTLED];
	ios_setipcaccrights = (void *)syscall_table[SYSCALL_SETIPCACCRIGHTS];
	ios_launchrm = (void *)syscall_table[SYSCALL_LAUNCHRM];

	// modify the table to point to our wrapper functions
	syscall_table[SYSCALL_CREATE_THREAD] = (u32)IOS_CreateThread;
	syscall_table[SYSCALL_JOIN_THREAD] = (u32)IOS_JoinThread;
	syscall_table[SYSCALL_DESTROY_THREAD] = (u32)IOS_DestroyThread;
	syscall_table[SYSCALL_GET_THREAD_ID] = (u32)IOS_GetThreadId;
	syscall_table[SYSCALL_GET_PROCESS_ID] = (u32)IOS_GetProcessId;
	syscall_table[SYSCALL_START_THREAD] = (u32)IOS_StartThread;
	syscall_table[SYSCALL_STOP_THREAD] = (u32)IOS_StopThread;
	syscall_table[SYSCALL_YIELD_THREAD] = (u32)IOS_YieldThread;
	syscall_table[SYSCALL_GET_THREAD_PRIO] = (u32)IOS_GetThreadPriority;
	syscall_table[SYSCALL_SET_THREAD_PRIO] = (u32)IOS_SetThreadPriority;
	syscall_table[SYSCALL_CREATE_MSG_Q] = (u32)IOS_CreateMessageQueue;
	syscall_table[SYSCALL_DESTROY_MSG_Q] = (u32)IOS_DestroyMessageQueue;
	syscall_table[SYSCALL_SEND_MSG] = (u32)IOS_SendMessage;
	syscall_table[SYSCALL_JAM_MSG] = (u32)IOS_JamMessage;

	// SPAM
//	syscall_table[SYSCALL_RCV_MSG] = (u32)IOS_ReceiveMessage;

	syscall_table[SYSCALL_HANDLE_EV] = (u32)IOS_HandleEvent;
	syscall_table[SYSCALL_UNHANDLE_EV] = (u32)IOS_UnhandleEvent;
	syscall_table[SYSCALL_CREATE_TIMER] = (u32)IOS_CreateTimer;
	syscall_table[SYSCALL_RESTART_TIMER] = (u32)IOS_RestartTimer;
	syscall_table[SYSCALL_STOP_TIMER] = (u32)IOS_StopTimer;
	syscall_table[SYSCALL_DESTROY_TIMER] = (u32)IOS_DestroyTimer;
	syscall_table[SYSCALL_GET_TIMER] = (u32)IOS_GetTimer;
	syscall_table[SYSCALL_CREATE_HEAP] = (u32)IOS_CreateHeap;
	syscall_table[SYSCALL_DESTROY_HEAP] = (u32)IOS_DestroyHeap;
	syscall_table[SYSCALL_ALLOC] = (u32)IOS_Alloc;
	syscall_table[SYSCALL_ALLOC_ALIGNED] = (u32)IOS_AllocAligned;
	syscall_table[SYSCALL_FREE] = (u32)IOS_Free;
	syscall_table[SYSCALL_REGISTER_RM] = (u32)IOS_RegisterResourceManager;
	syscall_table[SYSCALL_OPEN] = (u32)IOS_Open;
	syscall_table[SYSCALL_CLOSE] = (u32)IOS_Close;
	syscall_table[SYSCALL_READ] = (u32)IOS_Read;
	syscall_table[SYSCALL_WRITE] = (u32)IOS_Write;
	syscall_table[SYSCALL_SEEK] = (u32)IOS_Seek;
	syscall_table[SYSCALL_IOCTL] = (u32)IOS_Ioctl;
	syscall_table[SYSCALL_IOCTLV] = (u32)IOS_Ioctlv;
	syscall_table[SYSCALL_OPEN_ASYNC] = (u32)IOS_OpenAsync;
	syscall_table[SYSCALL_CLOSE_ASYNC] = (u32)IOS_CloseAsync;
	syscall_table[SYSCALL_READ_ASYNC] = (u32)IOS_ReadAsync;
	syscall_table[SYSCALL_WRITE_ASYNC] = (u32)IOS_WriteAsync;
	syscall_table[SYSCALL_SEEK_ASYNC] = (u32)IOS_SeekAsync;
	syscall_table[SYSCALL_IOCTL_ASYNC] = (u32)IOS_IoctlAsync;
	syscall_table[SYSCALL_IOCTLV_ASYNC] = (u32)IOS_IoctlvAsync;
	syscall_table[SYSCALL_RESOURCEREPLY] = (u32)IOS_ResourceReply;
	syscall_table[SYSCALL_SET_UID] = (u32)IOS_SetUid;
	syscall_table[SYSCALL_GET_UID] = (u32)IOS_GetUid;
	syscall_table[SYSCALL_SET_GID] = (u32)IOS_SetGid;
	syscall_table[SYSCALL_GET_GID] = (u32)IOS_GetGid;

	// SPAM
//	syscall_table[SYSCALL_FLUSHMEM] = (u32)IOS_FlushMem;

	// SPAM
//	syscall_table[SYSCALL_INVALRDB] = (u32)IOS_InvalidateRdb;

	syscall_table[SYSCALL_CLRENIPCIOPINTR] = (u32)IOS_ClearAndEnableIPCIOPIntr;
	syscall_table[SYSCALL_CLRENDIINTR] = (u32)IOS_ClearAndEnableDIIntr;
	syscall_table[SYSCALL_CLRENSDINTR] = (u32)IOS_ClearAndEnableSDIntr;
	syscall_table[SYSCALL_CLRENEVT] = (u32)IOS_ClearAndEnableEvent;
	syscall_table[SYSCALL_ACC_IOBPOOL] = (u32)IOS_AccessIobPool;
	syscall_table[SYSCALL_ALLOCIOB] = (u32)IOS_AllocIob;
	syscall_table[SYSCALL_FREEIOB] = (u32)IOS_FreeIob;
	syscall_table[SYSCALL_DBGDUMPIOBFREEHDRLIST] = (u32)IOS_DebugDumpIobFreeHdrsList;
	syscall_table[SYSCALL_DBGDUMPIOBFREEBUFLIST] = (u32)IOS_DebugDumpIobFreeBufsList;
	syscall_table[SYSCALL_PUTIOB] = (u32)IOS_PutIob;
	syscall_table[SYSCALL_PUSHIOB] = (u32)IOS_PushIob;
	syscall_table[SYSCALL_PULLIOB] = (u32)IOS_PullIob;
	syscall_table[SYSCALL_VALIDIOB] = (u32)IOS_IsValidIob;
	syscall_table[SYSCALL_CLONEIOB] = (u32)IOS_CloneIob;

	// SPAM
//	syscall_table[SYSCALL_INVALDCACHE] = (u32)IOS_InvalidateDCache;

	// SPAM
//	syscall_table[SYSCALL_FLUSHDCACHE] = (u32)IOS_FlushDCache;

	syscall_table[SYSCALL_LAUNCHELF] = (u32)IOS_LaunchElf;
	syscall_table[SYSCALL_LAUNCHOS] = (u32)IOS_LaunchOS;
	syscall_table[SYSCALL_LAUNCHOSFROMMEM] = (u32)IOS_LaunchOSFromMemory;
	syscall_table[SYSCALL_RESETDI] = (u32)IOS_ResetDI;
	syscall_table[SYSCALL_RELEASEDI] = (u32)IOS_ReleaseDIReset;
	syscall_table[SYSCALL_ISDIRESET] = (u32)IOS_IsDIReset;
	syscall_table[SYSCALL_GETOSVER] = (u32)IOS_GetOSVersion;
	syscall_table[SYSCALL_GETBOOTVER] = (u32)IOS_GetBootVersion;
	syscall_table[SYSCALL_GETDDRVENID] = (u32)IOS_GetDDRVendorIds;
	syscall_table[SYSCALL_GETHWID] = (u32)IOS_GetHollywoodId;
	syscall_table[SYSCALL_GETUSAGE] = (u32)IOS_GetUsage;
	syscall_table[SYSCALL_SETLOMEMOSVER] = (u32)IOS_SetLoMemOSVersion;
	syscall_table[SYSCALL_GETLOMEMOSVER] = (u32)IOS_GetLoMemOSVersion;
	syscall_table[SYSCALL_SETDISPINUP] = (u32)IOS_SetDiSpinup;

	// SPAM
//	syscall_table[SYSCALL_VTOP] = (u32)IOS_VirtualToPhysical;

	syscall_table[SYSCALL_SETDVDREADDIS] = (u32)IOS_SetDVDReadDisable;
	syscall_table[SYSCALL_GETDVDREADDIS] = (u32)IOS_GetDVDReadDisable;
	syscall_table[SYSCALL_SETENAHBPI2DI] = (u32)IOS_SetEnableAHBPI2DI;
	syscall_table[SYSCALL_GETENAHBPI2DI] = (u32)IOS_GetEnableAHBPI2DI;
	syscall_table[SYSCALL_SETPPCACRPERMS] = (u32)IOS_SetPPCACRPerms;
	syscall_table[SYSCALL_GETCORECLK] = (u32)IOS_GetCoreClk;

	// SPAM
//	syscall_table[SYSCALL_ACRREGWR] = (u32)IOS_ACRRegWrite;

	// SPAM
//	syscall_table[SYSCALL_DDRREGWR] = (u32)IOS_DDRRegWrite;

	syscall_table[SYSCALL_OUTPUTLED] = (u32)IOS_OutputLed;
	syscall_table[SYSCALL_SETIPCACCRIGHTS] = (u32)IOS_SetIpcAccessRights;

	syscall_table[SYSCALL_LAUNCHRM] = (u32)IOS_LaunchRM;
#if 0
	// this is just to give me the warm fuzzies
	printf("\nnew device_open: %x\n", syscall_table[SYSCALL_OPEN]);

	printf("\nNew syscall table:\n");

	for (i = 0; i < num_syscalls; i++)
	{
		printf("%x: %x\n", i, syscall_table[i]);
	}
#endif
	irq_restore(cookie);
}	

#if PPCHAX
void *find_stuff_EXI_stub(void)
{
	int i;
	u32 magic[] = { 0x7C631A78, 0x6463D7B0 };
	u32 *kernel = (u32 *)0xFFFF0000;
	u32 ppc_stub1_addr = 0;

	printf("Looking for ppc_stub1\n\n");

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if ((kernel[i] == magic[0]) && (kernel[i + 1] == magic[1]))
		{
			printf("Found ppc_stub1 at %x\n", 0xFFFF0000 + (i * 4));
			break;
		}
	}

	if (i == (0x10000 / 4))
	{
		printf("Couldn't find ppc_stub1\n");
		return NULL;
	}

	ppc_stub1_addr = 0xFFFF0000 + (i * 4);

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if (kernel[i] == ppc_stub1_addr)
		{
			printf("Found ppc_stub1 reference at %x\n",0xFFFF0000 + (i * 4));
			break;
		}
	}

	if (i == (0x10000 / 4))
	{
		printf("Couldn't find ppc_stub ref\n");
		return NULL;
	}

	for (; i > 0; i--)
	{
		if (kernel[i] == 0xB5001C03)
		{
			printf("Found stuff_EXI_stub start at %x\n", 0xFFFF0000 + (i * 4));
			break;
		}
	}

	if (i == 0x10000)
	{
		printf("Couldn't find stuff_EXI_stub start\n");
		return NULL;
	}

	// thumb offset
	stuff_EXI_stub = (void *)(0xFFFF0000 + (i * 4) + 1);

	for (i = 0; i < (0x10000 / 4); i++)
	{
		if (kernel[i] == (u32)stuff_EXI_stub)
		{
			printf("Found stuff_EXI_stub_addr reference at %x\n", 0xFFFF0000 + (i * 4));
			break;
		}
	}

	if (i == 0x10000)
	{
		printf("Couldn't find stuff_EXI_stub reference\n");
		return NULL;
	}

	kernel[i] = (u32)&stuff_EXI_stub_wrapper;
	printf("Replaced stuff_EXI_stub reference with %x\n", kernel[i]);
	return NULL;
}
#endif

// ye olde ELF loader, written in C for great justice
// also patches too
void *_loadelf(const u8 *elf)
{
	int count;
	Elf32_Phdr *phdr;
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf;
	u32 cookie = irq_kill();
/*
	if (memcmp("\x7F" "ELF\x01\x02\x01", elf, 7))
	{
		panic(0xE3);
	}
*/
	dprintf("ELF magic ok\n");

	if (ehdr->e_phoff == 0)
	{
		panic(0xE4);
	}

	dprintf("e_phoff=%x\n", ehdr->e_phoff);

	count = ehdr->e_phnum;
	phdr = (Elf32_Phdr *)(elf + ehdr->e_phoff);

	while (count--)
	{
#if 0
		dprintf("count=%x   phdr type=%x paddr=%x offset=%x filesz=%x\n",
			count, (u32)phdr->p_type, (u32)phdr->p_paddr, phdr->p_offset, (u32)phdr->p_filesz);
#endif		
		if ((phdr->p_type == PT_LOAD) && (phdr->p_filesz != 0))
		{
			const void *src = elf + phdr->p_offset;

			// could be memcpy, but trying to save code space
			memcpyr(phdr->p_paddr, src, phdr->p_filesz);

			if (phdr->p_paddr == (u32 *)0xFFFF0000)
			{
				do_kernel_patches(phdr->p_filesz);
			}

			// assumes syscall table will always be last phdr
			if (count == 1)
			{
				handle_syscall_table(phdr->p_paddr, phdr->p_filesz);
			}

			// this patch needs to be done when the kernel loads the module, not when loading the kernel
			do_kd_patch(phdr->p_paddr, phdr->p_filesz);
		}

		phdr++;
	}

	dprintf("done, entrypt = %x\n", (u32)ehdr->e_entry);

#if PPCHAX
	find_stuff_EXI_stub();
#endif
	irq_restore(cookie);

	return ehdr->e_entry;
}

static inline void disable_boot0(void)
{
	set32(HW_BOOT0, 0x1000);
}

static inline void mem_setswap(void)
{
	set32(HW_MEMMIRR, 0x20);
}

static FATFS fatfs;

#define BOOT2V4_ELF_SIZE	0x27674

void *_main(void *base)
{
	u8 *elf;
	void *entry;
	FRESULT fres;
	FIL fd;
	u32 read;
	ioshdr *hdr = (ioshdr *)base;
	unsigned int boot2_elf_offset = 0;

	elf = (u8 *)base;
	elf += (hdr->hdrsize + hdr->loadersize);

	debug_output(0xF1);
	mem_setswap();
	disable_boot0();
	gecko_init();

	u32 cookie = irq_kill();

	if (memcmp("\x7F" "ELF\x01\x02\x01", elf, 7))
	{
		dprintf("Initializing SDHC...\n");
		sdhc_init();

		dprintf("Mounting SD...\n");
		fres = f_mount(0, &fatfs);

		if (fres != FR_OK)
		{
			dprintf("mount failed\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("mount ok\n");

		fres = f_open(&fd, "/bootmii/boot2v4.bin", FA_READ);

		if (fres != FR_OK)
		{
			dprintf("f_open failed\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("open ok\n");

		fres = f_lseek(&fd, 0x4);

		if (fres != FR_OK)
		{
			dprintf("f_lseek failed\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("seek ok\n");

		fres = f_read(&fd, &boot2_elf_offset, 4, &read);

		if (fres != FR_OK)
		{
			dprintf("f_read failed (read)\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("read ok\n");

		boot2_elf_offset += BOOT2_LOADER_HEADER_SIZE;

		fres = f_lseek(&fd, boot2_elf_offset);

		if (fres != FR_OK)
		{
			dprintf("f_lseek failed\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("seek ok\n");

		//
		// How to get around data arrays here to keep the babelfish binary small...?
		// How to PREVENT USAGE of malloc which AIN'T available for the WIIDEV ARM toolchain...?
		//
		// Here it is: "f_read" the boot2v4 ELF content from the file descriptor
		// "fd" on the SD-Card into memory covered by "elf" DIRECTLY.
		//
		// Works like a charm...
		//
		fres = f_read(&fd, elf, BOOT2V4_ELF_SIZE, &read);

		if (fres != FR_OK)
		{
			dprintf("f_read failed (read)\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("read ok\n");

		if (read != BOOT2V4_ELF_SIZE)
		{
			dprintf("f_read failed (wrong size)\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("size ok\n");

		f_close(&fd);

		if (fres != FR_OK)
		{
			dprintf("f_close failed\n");
			irq_restore(cookie);
			return NULL;
		}

		dprintf("close ok\n");
	}

	dprintf("elfloader elf=%x\n", (u32)elf);
	entry = _loadelf(elf);
	dprintf("loadelf done\n");
	irq_restore(cookie);
	debug_output(0xC1);
	return entry;
}


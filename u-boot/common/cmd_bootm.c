/*
 * (C) Copyright 2000-2006
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#define DEBUG

/*
 * Boot support
 */
#include <common.h>
#include <watchdog.h>
#include <command.h>
#include <image.h>
#include <malloc.h>
#include <zlib.h>
#include <bzlib.h>
#include <LzmaWrapper.h> /* cu570m */
#include <environment.h>
#include <asm/byteorder.h>

#ifdef CFG_HUSH_PARSER
#include <hush.h>
#endif

DECLARE_GLOBAL_DATA_PTR;

extern int gunzip (void *dst, int dstlen, unsigned char *src, unsigned long *lenp);
#ifndef CFG_BOOTM_LEN
#define CFG_BOOTM_LEN	0x800000	/* use 8MByte as default max gunzip size */
#endif

#ifdef CONFIG_BZIP2
extern void bz_internal_error(int);
#endif

#if defined(CONFIG_CMD_IMI)
static int image_info (unsigned long addr);
#endif

#if defined(CONFIG_CMD_IMLS)
#include <flash.h>
extern flash_info_t flash_info[]; /* info for FLASH chips */
static int do_imls (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);
#endif

#ifdef CONFIG_SILENT_CONSOLE
static void fixup_silent_linux (void);
#endif

static void *get_kernel (cmd_tbl_t *cmdtp, int flag,
		int argc, char *argv[], int verify,
		bootm_headers_t *images,
		ulong *os_data, ulong *os_len);
extern int do_reset (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);

/*
 *  Continue booting an OS image; caller already has:
 *  - copied image header to global variable `header'
 *  - checked header magic number, checksums (both header & image),
 *  - verified image architecture (PPC) and type (KERNEL or MULTI),
 *  - loaded (first part of) image to header load address,
 *  - disabled interrupts.
 */
typedef void boot_os_fn (cmd_tbl_t *cmdtp, int flag,
			int argc, char *argv[],
			bootm_headers_t *images,/* pointers to os/initrd/fdt */
			int verify);		/* getenv("verify")[0] != 'n' */

extern boot_os_fn do_bootm_linux;
static boot_os_fn do_bootm_netbsd;
#if defined(CONFIG_LYNXKDI)
static boot_os_fn do_bootm_lynxkdi;
extern void lynxkdi_boot (image_header_t *);
#endif
static boot_os_fn do_bootm_rtems;
#if defined(CONFIG_CMD_ELF)
static boot_os_fn do_bootm_vxworks;
static boot_os_fn do_bootm_qnxelf;
int do_bootvx (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);
int do_bootelf (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);
#endif
#if defined(CONFIG_ARTOS) && defined(CONFIG_PPC)
extern uchar (*env_get_char)(int); /* Returns a character from the environment */
static boot_os_fn do_bootm_artos;
#endif

ulong load_addr = CFG_LOAD_ADDR;	/* Default Load Address */
static bootm_headers_t images;		/* pointers to os/initrd/fdt images */

/* cu570m start */
#ifdef FW_RECOVERY
	ushort fw_recovery = 0;
#endif

#define CONFIG_LZMA 1

/* changed by lqm, 18Jan08 */
#include "tpLinuxTag.h"		/* support TP-LINK Linux Tag */

// TODO: pass these values via an external MACRO /* cu570m */
LINUX_FLASH_STRUCT linuxFlash =
						{
							0x000000,	/* boot loader 	*/
							0x01fc00,	/* mac address	*/
							0x01fe00,	/* pin address	*/
							0x020000,	/* kernel		*/
							0x120000,	/* root fs		*/
							0x3e0000,	/* config		*/
							0x3f0000,	/* radio		*/
						};

/* added by lqm, 18Jan08, copy from fake_zimage_header() */
image_header_t *fake_image_header(image_header_t *hdr, ulong kernelTextAddr, ulong entryPoint, int size)
{
	ulong checksum = 0;

	memset(hdr, 0, sizeof(image_header_t));

	/* Build new header */
	hdr->ih_magic = htonl(IH_MAGIC);
	hdr->ih_time  = 0;
	hdr->ih_size  = htonl(size);
	hdr->ih_load  = htonl(kernelTextAddr);
	hdr->ih_ep    = htonl(entryPoint);
	hdr->ih_dcrc  = htonl(checksum);
	hdr->ih_os    = IH_OS_LINUX;
	hdr->ih_arch  = IH_ARCH_MIPS;
	hdr->ih_type  = IH_TYPE_KERNEL;
	hdr->ih_comp  = IH_COMP_GZIP;

	strncpy((char *)hdr->ih_name, "Kernel Image", IH_NMLEN);

	hdr->ih_hcrc = htonl(checksum);

	return hdr;
}
/* cu570m end */


/*******************************************************************/
/* bootm - boot application image from image in memory */
/*******************************************************************/
int do_bootm (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	ulong		iflag;
	const char	*type_name;
	uint		unc_len = CFG_BOOTM_LEN;
	// int		verify = getenv_verify();		/* cu570m removal */
	int		verify = 0;				/* cu570m addition */
	uint8_t	comp, type, os;

	void		*os_hdr;
	ulong		os_data, os_len;

	ulong		image_start, image_end;
	ulong		load_start, load_end;

	memset ((void *)&images, 0, sizeof (images));

	/* get kernel image header, start address and length */
	os_hdr = get_kernel (cmdtp, flag, argc, argv, verify,
			&images, &os_data, &os_len);
	// if (os_len == 0)					/* cu570m removal */
 	//	return 1;					/* cu570m removal */

	show_boot_progress (6);

	/* get image parameters */
/* cu570m removal 1 - start */
#if 0
	switch (gen_image_get_format (os_hdr)) {
	case IMAGE_FORMAT_LEGACY:
#endif /* cu570m removal 1 - end */

		type = image_get_type (os_hdr);
		comp = image_get_comp (os_hdr);
		os = image_get_os (os_hdr);

		image_end = image_get_image_end (os_hdr);
		load_start = image_get_load (os_hdr);

/* cu570m removal 2 - start */
#if 0
		break;
#if defined(CONFIG_FIT)
	case IMAGE_FORMAT_FIT:
		fit_unsupported ("bootm");
		return 1;
#endif
	default:
		puts ("ERROR: unknown image format type!\n");
		return 1;
	}
#endif /* cu570m removal 2 - end */

	image_start = (ulong)os_hdr;
	load_end = 0;
	type_name = image_get_type_name (type);

	/*
	 * We have reached the point of no return: we are going to
	 * overwrite all exception vector code, so we cannot easily
	 * recover from any failures any more...
	 */
	iflag = disable_interrupts();

#ifdef CONFIG_AMIGAONEG3SE
	/*
	 * We've possible left the caches enabled during
	 * bios emulation, so turn them off again
	 */
	icache_disable();
	invalidate_l1_instruction_cache();
	flush_data_cache();
	dcache_disable();
#endif

/* cu570m start */
#if defined(CONFIG_AR7100) || defined(CONFIG_AR7240) || defined(CONFIG_ATHEROS)
	/*
	 * Flush everything, restore caches for linux
	 */
	mips_cache_flush();
	mips_icache_flush_ix();

	/* XXX - this causes problems when booting from flash */
	/* dcache_disable(); */
#endif
/* cu570m end */

/* cu570m big removal 3 - start */
#if 0
	switch (comp) {
	case IH_COMP_NONE:
		if (load_start == (ulong)os_hdr) {
			printf ("   XIP %s ... ", type_name);
		} else {
			printf ("   Loading %s ... ", type_name);

			memmove_wd ((void *)load_start,
				   (void *)os_data, os_len, CHUNKSZ);

			load_end = load_start + unc_len;
			puts("OK\n");
		}
		break;

#ifndef COMPRESSED_UBOOT /* cu570m */
	case IH_COMP_GZIP:
		printf ("   Uncompressing %s ... ", type_name);
		if (gunzip ((void *)load_start, unc_len,
					(uchar *)os_data, &os_len) != 0) {
			puts ("GUNZIP ERROR - must RESET board to recover\n");
			show_boot_progress (-6);
			do_reset (cmdtp, flag, argc, argv);
		}

		load_end = load_start + unc_len;
		break;
#ifdef CONFIG_BZIP2
	case IH_COMP_BZIP2:
		printf ("   Uncompressing %s ... ", type_name);
		/*
		 * If we've got less than 4 MB of malloc() space,
		 * use slower decompression algorithm which requires
		 * at most 2300 KB of memory.
		 */
		int i = BZ2_bzBuffToBuffDecompress ((char*)load_start,
					&unc_len, (char *)os_data, os_len,
					CFG_MALLOC_LEN < (4096 * 1024), 0);
		if (i != BZ_OK) {
			printf ("BUNZIP2 ERROR %d - must RESET board to recover\n", i);
			show_boot_progress (-6);
			do_reset (cmdtp, flag, argc, argv);
		}

		load_end = load_start + unc_len;
		break;
#endif /* CONFIG_BZIP2 */
#endif /* #ifndef COMPRESSED_UBOOT */ /* cu570m */
#endif /* cu570m big removal 3 - end */

#ifdef CONFIG_LZMA /* cu570m start */
	// case IH_COMP_LZMA: /* cu570m removal */
		printf ("   Uncompressing %s ... ", type_name);
		int i = lzma_inflate ((uchar *)os_data, os_len, (unsigned char*)load_start, &unc_len);
		if (i != LZMA_RESULT_OK) {
			printf ("LZMA ERROR %d - must RESET board to recover\n", i);
			show_boot_progress (-6);
			// udelay(100000); /* cu570m removal */
			do_reset (cmdtp, flag, argc, argv);
		}

		load_end = load_start + unc_len;
		// break; /* cu570m removal */
#endif /* CONFIG_LZMA */ /* cu570m end */

/* cu570m big removal 4 - start */
#if 0
	default:
		if (iflag)
			enable_interrupts();
		printf ("Unimplemented compression type %d\n", comp);
		show_boot_progress (-7);
		return 1;
	}
#endif
/* cu570m big removal 4 - end */

	puts ("OK\n");
	debug ("   kernel loaded at 0x%08lx, end = 0x%08lx\n", load_start, load_end);
	show_boot_progress (7);

	if ((load_start < image_end) && (load_end > image_start)) {
		debug ("image_start = 0x%lX, image_end = 0x%lx\n", image_start, image_end);
		debug ("load_start = 0x%lx, load_end = 0x%lx\n", load_start, load_end);

		puts ("ERROR: image overwritten - must RESET the board to recover.\n");
		do_reset (cmdtp, flag, argc, argv);
	}

/* cu570m big removal 5 - start */
#if 0
	show_boot_progress (8);

	switch (os) {
	default:			/* handled by (original) Linux case */
	case IH_OS_LINUX:
#endif /* cu570m big removal 5 - end */

#ifdef CONFIG_SILENT_CONSOLE
	    fixup_silent_linux();
#endif
	    do_bootm_linux (cmdtp, flag, argc, argv, &images, verify);

/* cu570m big removal 6 - start */
#if 0
	    break;

	case IH_OS_NETBSD:
	    do_bootm_netbsd (cmdtp, flag, argc, argv, &images, verify);
	    break;

#ifdef CONFIG_LYNXKDI
	case IH_OS_LYNXOS:
	    do_bootm_lynxkdi (cmdtp, flag, argc, argv, &images, verify);
	    break;
#endif

	case IH_OS_RTEMS:
	    do_bootm_rtems (cmdtp, flag, argc, argv, &images, verify);
	    break;

#if defined(CONFIG_CMD_ELF)
	case IH_OS_VXWORKS:
	    do_bootm_vxworks (cmdtp, flag, argc, argv, &images, verify);
	    break;
	case IH_OS_QNX:
	    do_bootm_qnxelf (cmdtp, flag, argc, argv, &images, verify);
	    break;
#endif

#ifdef CONFIG_ARTOS
	case IH_OS_ARTOS:
	    do_bootm_artos (cmdtp, flag, argc, argv, &images, verify);
	    break;
#endif
	}
#endif /* cu570m big removal 6 - end */

	show_boot_progress (-9);
#ifdef DEBUG
	puts ("\n## Control returned to monitor - resetting...\n");
	do_reset (cmdtp, flag, argc, argv);
#endif
	return 1;
}

/**
 * get_kernel - find kernel image
 * @os_data: pointer to a ulong variable, will hold os data start address
 * @os_len: pointer to a ulong variable, will hold os data length
 *
 * get_kernel() tries to find a kernel image, verifies its integrity
 * and locates kernel data.
 *
 * returns:
 *     pointer to image header if valid image was found, plus kernel start
 *     address and length, otherwise NULL
 */
static void *get_kernel (cmd_tbl_t *cmdtp, int flag,
		int argc, char *argv[], int verify,
		bootm_headers_t *images,
		ulong *os_data, ulong *os_len)
{
	image_header_t	*hdr;
	ulong		img_addr;
	ulong	kernelTextAddr, kernelEntryPoint, kernelLen;	/* cu570m addition */
#if defined(CONFIG_FIT)
	void		*fit_hdr;
	const char	*fit_uname_config = NULL;
	const char	*fit_uname_kernel = NULL;
#endif

	/* find out kernel image address */
	if (argc < 2) {
		img_addr = load_addr;
		debug ("*  kernel: default image load address = 0x%08lx\n",
				load_addr);
#if defined(CONFIG_FIT)
	} else if (fit_parse_conf (argv[1], load_addr, &img_addr,
							&fit_uname_config)) {
		debug ("*  kernel: config '%s' from image at 0x%08lx\n",
				fit_uname_config, img_addr);
	} else if (fit_parse_subimage (argv[1], load_addr, &img_addr,
							&fit_uname_kernel)) {
		debug ("*  kernel: subimage '%s' from image at 0x%08lx\n",
				fit_uname_kernel, img_addr);
#endif
	} else {
		img_addr = simple_strtoul(argv[1], NULL, 16);
		debug ("*  kernel: cmdline image address = 0x%08lx\n", img_addr);
	}

	show_boot_progress (1);
	printf ("## Booting kernel image at %08lx ...\n", img_addr);

/* cu570m big removal 1 - start */
#if 0
	/* copy from dataflash if needed */
	img_addr = gen_get_image (img_addr);

	/* check image type, for FIT images get FIT kernel node */
	switch (gen_image_get_format ((void *)img_addr)) {
	case IMAGE_FORMAT_LEGACY:

		debug ("*  kernel: legacy format image\n");
		hdr = (image_header_t *)img_addr;

		if (!image_check_magic(hdr)) {
			puts ("Bad Magic Number\n");
			show_boot_progress (-1);
			return NULL;
		}
		show_boot_progress (2);

		if (!image_check_hcrc (hdr)) {
			puts ("Bad Header Checksum\n");
			show_boot_progress (-2);
			return NULL;
		}
		show_boot_progress (3);

#endif /* custom big removal 1 - end */

/* cu570m start */
	kernelTextAddr = *(ulong *)(img_addr+116);
	kernelEntryPoint = *(ulong *)(img_addr+120);
	kernelLen = *(ulong *)(img_addr+132);

	fake_image_header(hdr, kernelTextAddr, kernelEntryPoint, kernelLen);
/* cu570m end */

	/* uImage is in a system RAM, pointed to by hdr */
	// image_print_contents (hdr);			/* cu570m removal */

	// TODO: check the magic number and checksum of fileTag /* cu570m */
	show_boot_progress (2);			/* cu570m addition */

/* cu570m big removal 2 - start */
#if 0
		if (verify) {
			puts ("   Verifying Checksum at 0x%p ...", image_get_data (hdr)); /* cu570m */
			if (!image_check_dcrc (hdr)) {
				printf ("Bad Data CRC\n");
				show_boot_progress (-3);
				return NULL;
			}
			puts ("OK\n");
		}
		show_boot_progress (4);

		if (!image_check_target_arch (hdr)) {
			printf ("Unsupported Architecture 0x%x\n", image_get_arch (hdr));
			show_boot_progress (-4);
			return NULL;
		}
		show_boot_progress (5);

		switch (image_get_type (hdr)) {
		case IH_TYPE_KERNEL:
			*os_data = image_get_data (hdr);
			*os_len = image_get_data_size (hdr);
			break;
		case IH_TYPE_MULTI:
			image_multi_getimg (hdr, 0, os_data, os_len);
			break;
		default:
			printf ("Wrong Image Type for %s command\n", cmdtp->name);
			show_boot_progress (-5);
			return NULL;
		}
		images->legacy_hdr_os = hdr;
		images->legacy_hdr_valid = 1;

		break;
#if defined(CONFIG_FIT)
	case IMAGE_FORMAT_FIT:
		fit_hdr = (void *)img_addr;
		debug ("*  kernel: FIT format image\n");
		fit_unsupported ("kernel");
		return NULL;
#endif
	default:
		printf ("Wrong Image Format for %s command\n", cmdtp->name);
 		return NULL;
 	}
#endif /* cu570m big removal 2 - end */

/* cu570m start */
	*os_data = img_addr + 512;
	*os_len = image_get_data_size (hdr);
/* cu570m end */

	debug ("   kernel data at 0x%08lx, end = 0x%08lx\n",
			*os_data, *os_data + *os_len);

	// return (void *)img_addr;	/* cu570m removal */
	return hdr;			/* cu570m addition */
}

U_BOOT_CMD(
	bootm,	CFG_MAXARGS,	1,	do_bootm,
	"bootm   - boot application image from memory\n",
	"[addr [arg ...]]\n    - boot application image stored in memory\n"
	"\tpassing arguments 'arg ...'; when booting a Linux kernel,\n"
	"\t'arg' can be the address of an initrd image\n"
#if defined(CONFIG_OF_LIBFDT)
	"\tWhen booting a Linux kernel which requires a flat device-tree\n"
	"\ta third argument is required which is the address of the\n"
	"\tdevice-tree blob. To boot that kernel without an initrd image,\n"
	"\tuse a '-' for the second argument. If you do not pass a third\n"
	"\ta bd_info struct will be passed instead\n"
#endif
);

/*******************************************************************/
/* bootd - boot default image */
/*******************************************************************/
#if defined(CONFIG_CMD_BOOTD)
int do_bootd (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	int rcode = 0;

#ifndef CFG_HUSH_PARSER
	if (run_command (getenv ("bootcmd"), flag) < 0)
		rcode = 1;
#else
	if (parse_string_outer (getenv ("bootcmd"),
			FLAG_PARSE_SEMICOLON | FLAG_EXIT_FROM_LOOP) != 0)
		rcode = 1;
#endif
	return rcode;
}

U_BOOT_CMD(
	boot,	1,	1,	do_bootd,
	"boot    - boot default, i.e., run 'bootcmd'\n",
	NULL
);

/* keep old command name "bootd" for backward compatibility */
U_BOOT_CMD(
	bootd, 1,	1,	do_bootd,
	"bootd   - boot default, i.e., run 'bootcmd'\n",
	NULL
);

#endif


/*******************************************************************/
/* iminfo - print header info for a requested image */
/*******************************************************************/
#if defined(CONFIG_CMD_IMI)
int do_iminfo (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	int	arg;
	ulong	addr;
	int	rcode = 0;

	if (argc < 2) {
		return image_info (load_addr);
	}

	for (arg = 1; arg < argc; ++arg) {
		addr = simple_strtoul (argv[arg], NULL, 16);
		if (image_info (addr) != 0)
			rcode = 1;
	}
	return rcode;
}

static int image_info (ulong addr)
{
	void *hdr = (void *)addr;

	printf ("\n## Checking Image at %08lx ...\n", addr);

	switch (gen_image_get_format (hdr)) {
	case IMAGE_FORMAT_LEGACY:
		puts ("   Legacy image found\n");
		if (!image_check_magic (hdr)) {
			puts ("   Bad Magic Number\n");
			return 1;
		}

		if (!image_check_hcrc (hdr)) {
			puts ("   Bad Header Checksum\n");
			return 1;
		}

		image_print_contents (hdr);

		puts ("   Verifying Checksum ... ");
		if (!image_check_dcrc (hdr)) {
			puts ("   Bad Data CRC\n");
			return 1;
		}
		puts ("OK\n");
		return 0;
#if defined(CONFIG_FIT)
	case IMAGE_FORMAT_FIT:
		puts ("   FIT image found\n");
		fit_unsupported ("iminfo");
		return 0;
#endif
	default:
		puts ("Unknown image format!\n");
		break;
	}

	return 1;
}

U_BOOT_CMD(
	iminfo,	CFG_MAXARGS,	1,	do_iminfo,
	"iminfo  - print header information for application image\n",
	"addr [addr ...]\n"
	"    - print header information for application image starting at\n"
	"      address 'addr' in memory; this includes verification of the\n"
	"      image contents (magic number, header and payload checksums)\n"
);
#endif


/*******************************************************************/
/* imls - list all images found in flash */
/*******************************************************************/
#if defined(CONFIG_CMD_IMLS)
int do_imls (cmd_tbl_t *cmdtp, int flag, int argc, char *argv[])
{
	flash_info_t *info;
	int i, j;
	void *hdr;

	for (i = 0, info = &flash_info[0];
		i < CFG_MAX_FLASH_BANKS; ++i, ++info) {

		if (info->flash_id == FLASH_UNKNOWN)
			goto next_bank;
		for (j = 0; j < info->sector_count; ++j) {

			hdr = (void *)info->start[j];
			if (!hdr)
				goto next_sector;

			switch (gen_image_get_format (hdr)) {
			case IMAGE_FORMAT_LEGACY:
				if (!image_check_magic (hdr))
					goto next_sector;

				if (!image_check_hcrc (hdr))
					goto next_sector;

				printf ("Legacy Image at %08lX:\n", (ulong)hdr);
				image_print_contents (hdr);

				puts ("   Verifying Checksum ... ");
				if (!image_check_dcrc (hdr)) {
					puts ("Bad Data CRC\n");
				} else {
					puts ("OK\n");
				}
				break;
#if defined(CONFIG_FIT)
			case IMAGE_FORMAT_FIT:
				printf ("FIT Image at %08lX:\n", (ulong)hdr);
				fit_unsupported ("imls");
				break;
#endif
			default:
				goto next_sector;
			}

next_sector:		;
		}
next_bank:	;
	}

	return (0);
}

U_BOOT_CMD(
	imls,	1,		1,	do_imls,
	"imls    - list all images found in flash\n",
	"\n"
	"    - Prints information about all images found at sector\n"
	"      boundaries in flash.\n"
);
#endif

/*******************************************************************/
/* helper routines */
/*******************************************************************/
#ifdef CONFIG_SILENT_CONSOLE
static void fixup_silent_linux ()
{
	char buf[256], *start, *end;
	char *cmdline = getenv ("bootargs");

	/* Only fix cmdline when requested */
	if (!(gd->flags & GD_FLG_SILENT))
		return;

	debug ("before silent fix-up: %s\n", cmdline);
	if (cmdline) {
		if ((start = strstr (cmdline, "console=")) != NULL) {
			end = strchr (start, ' ');
			strncpy (buf, cmdline, (start - cmdline + 8));
			if (end)
				strcpy (buf + (start - cmdline + 8), end);
			else
				buf[start - cmdline + 8] = '\0';
		} else {
			strcpy (buf, cmdline);
			strcat (buf, " console=");
		}
	} else {
		strcpy (buf, "console=");
	}

	setenv ("bootargs", buf);
	debug ("after silent fix-up: %s\n", buf);
}
#endif /* CONFIG_SILENT_CONSOLE */


/*******************************************************************/
/* OS booting routines */
/*******************************************************************/

static void do_bootm_netbsd (cmd_tbl_t *cmdtp, int flag,
			    int argc, char *argv[],
			    bootm_headers_t *images, int verify)
{
	void (*loader)(bd_t *, image_header_t *, char *, char *);
	image_header_t *os_hdr, *hdr;
	ulong kernel_data, kernel_len;
	char *consdev;
	char *cmdline;

#if defined(CONFIG_FIT)
	if (!images->legacy_hdr_valid) {
		fit_unsupported_reset ("NetBSD");
		do_reset (cmdtp, flag, argc, argv);
	}
#endif
	hdr = images->legacy_hdr_os;

	/*
	 * Booting a (NetBSD) kernel image
	 *
	 * This process is pretty similar to a standalone application:
	 * The (first part of an multi-) image must be a stage-2 loader,
	 * which in turn is responsible for loading & invoking the actual
	 * kernel.  The only differences are the parameters being passed:
	 * besides the board info strucure, the loader expects a command
	 * line, the name of the console device, and (optionally) the
	 * address of the original image header.
	 */
	os_hdr = NULL;
	if (image_check_type (hdr, IH_TYPE_MULTI)) {
		image_multi_getimg (hdr, 1, &kernel_data, &kernel_len);
		if (kernel_len)
			os_hdr = hdr;
	}

	consdev = "";
#if   defined (CONFIG_8xx_CONS_SMC1)
	consdev = "smc1";
#elif defined (CONFIG_8xx_CONS_SMC2)
	consdev = "smc2";
#elif defined (CONFIG_8xx_CONS_SCC2)
	consdev = "scc2";
#elif defined (CONFIG_8xx_CONS_SCC3)
	consdev = "scc3";
#endif

	if (argc > 2) {
		ulong len;
		int   i;

		for (i = 2, len = 0; i < argc; i += 1)
			len += strlen (argv[i]) + 1;
		cmdline = malloc (len);

		for (i = 2, len = 0; i < argc; i += 1) {
			if (i > 2)
				cmdline[len++] = ' ';
			strcpy (&cmdline[len], argv[i]);
			len += strlen (argv[i]);
		}
	} else if ((cmdline = getenv ("bootargs")) == NULL) {
		cmdline = "";
	}

	loader = (void (*)(bd_t *, image_header_t *, char *, char *))image_get_ep (hdr);

	printf ("## Transferring control to NetBSD stage-2 loader (at address %08lx) ...\n",
		(ulong)loader);

	show_boot_progress (15);

	/*
	 * NetBSD Stage-2 Loader Parameters:
	 *   r3: ptr to board info data
	 *   r4: image address
	 *   r5: console device
	 *   r6: boot args string
	 */
	(*loader) (gd->bd, os_hdr, consdev, cmdline);
}

#ifdef CONFIG_LYNXKDI
static void do_bootm_lynxkdi (cmd_tbl_t *cmdtp, int flag,
			     int argc, char *argv[],
			     bootm_headers_t *images, int verify)
{
	image_header_t *hdr = images->legacy_hdr_os;

#if defined(CONFIG_FIT)
	if (!images->legacy_hdr_valid) {
		fit_unsupported_reset ("Lynx");
		do_reset (cmdtp, flag, argc, argv);
	}
#endif

	lynxkdi_boot ((image_header_t *)hdr);
}
#endif /* CONFIG_LYNXKDI */

static void do_bootm_rtems (cmd_tbl_t *cmdtp, int flag,
			   int argc, char *argv[],
			   bootm_headers_t *images, int verify)
{
	image_header_t *hdr = images->legacy_hdr_os;
	void (*entry_point)(bd_t *);

#if defined(CONFIG_FIT)
	if (!images->legacy_hdr_valid) {
		fit_unsupported_reset ("RTEMS");
		do_reset (cmdtp, flag, argc, argv);
	}
#endif

	entry_point = (void (*)(bd_t *))image_get_ep (hdr);

	printf ("## Transferring control to RTEMS (at address %08lx) ...\n",
		(ulong)entry_point);

	show_boot_progress (15);

	/*
	 * RTEMS Parameters:
	 *   r3: ptr to board info data
	 */
	(*entry_point)(gd->bd);
}

#if defined(CONFIG_CMD_ELF)
static void do_bootm_vxworks (cmd_tbl_t *cmdtp, int flag,
			     int argc, char *argv[],
			     bootm_headers_t *images, int verify)
{
	char str[80];
	image_header_t *hdr = images->legacy_hdr_os;

#if defined(CONFIG_FIT)
	if (hdr == NULL) {
		fit_unsupported_reset ("VxWorks");
		do_reset (cmdtp, flag, argc, argv);
	}
#endif

	sprintf(str, "%x", image_get_ep (hdr)); /* write entry-point into string */
	setenv("loadaddr", str);
	do_bootvx(cmdtp, 0, 0, NULL);
}

static void do_bootm_qnxelf(cmd_tbl_t *cmdtp, int flag,
			    int argc, char *argv[],
			    bootm_headers_t *images, int verify)
{
	char *local_args[2];
	char str[16];
	image_header_t *hdr = images->legacy_hdr_os;

#if defined(CONFIG_FIT)
	if (!images->legacy_hdr_valid) {
		fit_unsupported_reset ("QNX");
		do_reset (cmdtp, flag, argc, argv);
	}
#endif

	sprintf(str, "%x", image_get_ep (hdr)); /* write entry-point into string */
	local_args[0] = argv[0];
	local_args[1] = str;	/* and provide it via the arguments */
	do_bootelf(cmdtp, 0, 2, local_args);
}
#endif

#if defined(CONFIG_ARTOS) && defined(CONFIG_PPC)
static void do_bootm_artos (cmd_tbl_t *cmdtp, int flag,
			   int argc, char *argv[],
			   bootm_headers_t *images, int verify)
{
	ulong top;
	char *s, *cmdline;
	char **fwenv, **ss;
	int i, j, nxt, len, envno, envsz;
	bd_t *kbd;
	void (*entry)(bd_t *bd, char *cmdline, char **fwenv, ulong top);
	image_header_t *hdr = images->legacy_hdr_os;

#if defined(CONFIG_FIT)
	if (!images->legacy_hdr_valid) {
		fit_unsupported_reset ("ARTOS");
		do_reset (cmdtp, flag, argc, argv);
	}
#endif

	/*
	 * Booting an ARTOS kernel image + application
	 */

	/* this used to be the top of memory, but was wrong... */
#ifdef CONFIG_PPC
	/* get stack pointer */
	asm volatile ("mr %0,1" : "=r"(top) );
#endif
	debug ("## Current stack ends at 0x%08lX ", top);

	top -= 2048;		/* just to be sure */
	if (top > CFG_BOOTMAPSZ)
		top = CFG_BOOTMAPSZ;
	top &= ~0xF;

	debug ("=> set upper limit to 0x%08lX\n", top);

	/* first check the artos specific boot args, then the linux args*/
	if ((s = getenv( "abootargs")) == NULL && (s = getenv ("bootargs")) == NULL)
		s = "";

	/* get length of cmdline, and place it */
	len = strlen (s);
	top = (top - (len + 1)) & ~0xF;
	cmdline = (char *)top;
	debug ("## cmdline at 0x%08lX ", top);
	strcpy (cmdline, s);

	/* copy bdinfo */
	top = (top - sizeof (bd_t)) & ~0xF;
	debug ("## bd at 0x%08lX ", top);
	kbd = (bd_t *)top;
	memcpy (kbd, gd->bd, sizeof (bd_t));

	/* first find number of env entries, and their size */
	envno = 0;
	envsz = 0;
	for (i = 0; env_get_char (i) != '\0'; i = nxt + 1) {
		for (nxt = i; env_get_char (nxt) != '\0'; ++nxt)
			;
		envno++;
		envsz += (nxt - i) + 1;	/* plus trailing zero */
	}
	envno++;	/* plus the terminating zero */
	debug ("## %u envvars total size %u ", envno, envsz);

	top = (top - sizeof (char **) * envno) & ~0xF;
	fwenv = (char **)top;
	debug ("## fwenv at 0x%08lX ", top);

	top = (top - envsz) & ~0xF;
	s = (char *)top;
	ss = fwenv;

	/* now copy them */
	for (i = 0; env_get_char (i) != '\0'; i = nxt + 1) {
		for (nxt = i; env_get_char (nxt) != '\0'; ++nxt)
			;
		*ss++ = s;
		for (j = i; j < nxt; ++j)
			*s++ = env_get_char (j);
		*s++ = '\0';
	}
	*ss++ = NULL;	/* terminate */

	entry = (void (*)(bd_t *, char *, char **, ulong))image_get_ep (hdr);
	(*entry) (kbd, cmdline, fwenv, top);
}
#endif

// ##############################################################################################
// #                                                                                            #
// # Automated tool to create file ARMBOOT.BIN with BABELFISH included for use with BootMii     #
// #                                                                                            #
// # Eventually requires an unencrypted dump of boot2v4 as file "boot2v4.bin" in this directory #
// #                                                                                            #
// # Written (c) 2026 - nitr8                                                                   #
// #                                                                                            #
// ##############################################################################################


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//
// This will keep the babelfish binary (armboot.bin in the end) pretty small for more of our own code to it
//
// Solution: we load the boot2v4 ELF from the boot2 binary dump on the SD-Card instead which just works out
//
#define WITH_BOOT2V4_ELF		0

#define BOOT2_LOADER_HEADER_SIZE	0x10


static unsigned int swap32(unsigned int val)
{
	val = ((val >> 24) & 0xFF) | ((val << 8) & 0xFF0000) | ((val >> 8) & 0xFF00) | ((val << 24) & 0xFF000000);

	return val;
}

int main(int argc, char *argv[])
{
	int ret = 0;

#if WITH_BOOT2V4_ELF
	unsigned int boot2_elf_length;
	unsigned int boot2_elf_offset;
#endif

	unsigned int babelfish_loader_size;
	unsigned int babelfish_loader_size_with_stack;

	int fill_bytes;
	long pos;

	unsigned char *babelfish_buf = NULL;
	unsigned char *fill_buf = NULL;

#if WITH_BOOT2V4_ELF
	unsigned char *boot2_buf = NULL;
	FILE *f_in = NULL;
#endif

	FILE *f_out = NULL;
	FILE *f_babelfish = NULL;

	(void)argc;
	(void)argv;

#if WITH_BOOT2V4_ELF
	f_in = fopen("boot2v4.bin", "rb");

	if (!f_in)
	{
		printf("couldn't open input file\n");
		ret = 1;
	}
#endif

	f_out = fopen("armboot.bin", "w+b");

	if (!f_out)
	{
		printf("couldn't open output file\n");
		ret = 2;
		goto err_f_out;
	}

	f_babelfish = fopen("babelfish.bin", "rb");

	if (!f_babelfish)
	{
		printf("couldn't open babelfish file\n");
		ret = 3;
		goto err_f_babelfish;
	}

	if (fseek(f_babelfish, 0, SEEK_END) != 0)
	{
		printf("Error couldn't seek to end of babelfish binary\n");
		ret = 4;
		goto err_babelfish_buf;
	}

	pos = ftell(f_babelfish);

	if (pos <= 0)
	{
		printf("Error couldn't get length of babelfish binary\n");
		ret = 5;
		goto err_babelfish_buf;
	}

	babelfish_loader_size = (unsigned int)pos;

	if (fseek(f_babelfish, 0x4, SEEK_SET) != 0)
	{
		printf("Error couldn't seek %d bytes into babelfish binary\n", 4);
		ret = 6;
		goto err_babelfish_buf;
	}

	if (fread(&babelfish_loader_size_with_stack, 1, sizeof(babelfish_loader_size_with_stack), f_babelfish) != sizeof(babelfish_loader_size_with_stack))
	{
		printf("Error reading %zu bytes from babelfish binary\n", sizeof(babelfish_loader_size_with_stack));
		ret = 7;
		goto err_babelfish_buf;
	}

	babelfish_loader_size_with_stack = swap32(babelfish_loader_size_with_stack);

//	printf("babelfish loader elf length = %08x\n", babelfish_loader_size_with_stack);

#if WITH_BOOT2V4_ELF
	if (fseek(f_in, 0x4, SEEK_SET) != 0)
	{
		printf("Error couldn't seek %d bytes into boot2 binary\n", 4);
		ret = 8;
		goto err_babelfish_buf;
	}

	if (fread(&boot2_elf_offset, 1, sizeof(boot2_elf_offset), f_in) != sizeof(boot2_elf_offset))
	{
		printf("Error reading %zu bytes from boot2 binary\n", sizeof(boot2_elf_offset));
		ret = 9;
		goto err_babelfish_buf;
	}

	boot2_elf_offset = swap32(boot2_elf_offset) + BOOT2_LOADER_HEADER_SIZE;

//	printf("boot2     loader elf offset = %08x\n", boot2_elf_offset);

	if (fseek(f_in, 0x8, SEEK_SET) != 0)
	{
		printf("Error couldn't seek %d bytes into boot2 binary\n", 8);
		ret = 10;
		goto err_babelfish_buf;
	}

	if (fread(&boot2_elf_length, 1, sizeof(boot2_elf_length), f_in) != sizeof(boot2_elf_length))
	{
		printf("Error reading %zu bytes from boot2 binary\n", sizeof(boot2_elf_length));
		ret = 11;
		goto err_babelfish_buf;
	}

	boot2_elf_length = swap32(boot2_elf_length);

//	printf("boot2     loader elf length = %08x\n", boot2_elf_length);
#endif

	if (fseek(f_babelfish, 0, SEEK_SET) != 0)
	{
		printf("Error couldn't seek to start of babelbish binary\n");
		ret = 12;
		goto err_babelfish_buf;
	}

	babelfish_buf = malloc(babelfish_loader_size);

	if (!babelfish_buf)
	{
		printf("couldn't allocate %u bytes\n", babelfish_loader_size);
		ret = 13;
		goto err_babelfish_buf;
	}

	if (fread(babelfish_buf, 1, babelfish_loader_size, f_babelfish) != babelfish_loader_size)
	{
		printf("Error reading %u bytes from babelfish binary\n", babelfish_loader_size);
		ret = 14;
		goto err_fill_buf;
	}

	if (fwrite(babelfish_buf, 1, babelfish_loader_size, f_out) != babelfish_loader_size)
	{
		printf("Error writing %u bytes to armboot binary\n", babelfish_loader_size);
		ret = 15;
		goto err_fill_buf;
	}

	if ((babelfish_loader_size_with_stack + BOOT2_LOADER_HEADER_SIZE) < babelfish_loader_size)
	{
		printf("Error invalid stack size\n");
		ret = 16;
		goto err_fill_buf;
	}

	fill_bytes = babelfish_loader_size_with_stack - babelfish_loader_size + BOOT2_LOADER_HEADER_SIZE;

	if (fill_bytes <= 0)
	{
		printf("Error couldn't get amount of bytes to fill up stack area in armboot (target) binary\n");
		ret = 17;
		goto err_fill_buf;
	}

	fill_buf = malloc(fill_bytes);

	if (!fill_buf)
	{
		printf("couldn't allocate %d bytes\n", fill_bytes);
		ret = 18;
		goto err_fill_buf;
	}

	memset(fill_buf, 0, fill_bytes);

	if (fwrite(fill_buf, 1, fill_bytes, f_out) != (unsigned int)fill_bytes)
	{
		printf("Error writing %d bytes to armboot binary\n", fill_bytes);
		ret = 19;
		goto err_boot2_buf;
	}

#if WITH_BOOT2V4_ELF
	if (fseek(f_in, boot2_elf_offset, SEEK_SET) != 0)
	{
		printf("Error couldn't seek %u bytes into boot2 binary\n", boot2_elf_offset);
		ret = 20;
		goto err_boot2_buf;
	}

	boot2_buf = malloc(boot2_elf_length);

	if (!boot2_buf)
	{
		printf("couldn't allocate %u bytes\n", boot2_elf_length);
		ret = 21;
		goto err_boot2_buf;
	}

	if (fread(boot2_buf, 1, boot2_elf_length, f_in) != boot2_elf_length)
	{
		printf("Error reading %u bytes from boot2 binary\n", boot2_elf_length);
		ret = 22;
		goto err;
	}

	if (fwrite(boot2_buf, 1, boot2_elf_length, f_out) != boot2_elf_length)
	{
		printf("Error writing %u bytes to armboot binary\n", boot2_elf_length);
		ret = 23;
		goto err;
	}

	if (fseek(f_out, 0x8, SEEK_SET) != 0)
	{
		printf("Error couldn't seek %d bytes into armboot (target) binary\n", 8);
		ret = 24;
		goto err;
	}

	boot2_elf_length = swap32(boot2_elf_length);

	if (fwrite(&boot2_elf_length, 1, sizeof(boot2_elf_length), f_out) != sizeof(boot2_elf_length))
	{
		printf("Error writing %zu bytes to armboot binary\n", sizeof(boot2_elf_length));
		ret = 25;
		goto err;
	}

err:

	if (boot2_buf)
		free(boot2_buf);
#endif

err_boot2_buf:

	if (fill_buf)
		free(fill_buf);

err_fill_buf:

	if (babelfish_buf)
		free(babelfish_buf);

err_babelfish_buf:

	if (f_babelfish)
		fclose(f_babelfish);

err_f_babelfish:

	if (f_out)
		fclose(f_out);

err_f_out:

#if WITH_BOOT2V4_ELF
	if (f_in)
		fclose(f_in);
#endif

//	printf("babelfish armboot.bin created\n");

	return ret;
}


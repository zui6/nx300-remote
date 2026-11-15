/*
 * devmemdump.c —— 通过 mmap 从 /dev/mem 抓取物理内存到文件
 *
 * 用法: devmemdump <物理地址(hex)> <长度(hex)> <输出文件>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>

#define PAGE_SIZE 4096

int main(int argc, char **argv)
{
    unsigned long addr, len, page_addr, offset, map_len;
    int fd;
    void *map;
    FILE *out;

    if (argc < 4) {
        fprintf(stderr, "usage: %s <addr_hex> <len_hex> <outfile>\n", argv[0]);
        return 2;
    }
    addr = strtoul(argv[1], NULL, 16);
    len = strtoul(argv[2], NULL, 16);

    page_addr = addr & ~(unsigned long) (PAGE_SIZE - 1);
    offset = addr - page_addr;
    map_len = len + offset;

    fd = open("/dev/mem", O_RDONLY);
    if (fd < 0) {
        perror("open /dev/mem");
        return 1;
    }

    map = mmap(NULL, map_len, PROT_READ, MAP_SHARED, fd, (off_t) page_addr);
    if (map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    out = fopen(argv[3], "wb");
    if (out == NULL) {
        perror("fopen");
        munmap(map, map_len);
        close(fd);
        return 1;
    }

    if (fwrite((char *) map + offset, 1, len, out) != len) {
        fprintf(stderr, "short write\n");
    }
    fclose(out);
    munmap(map, map_len);
    close(fd);

    printf("wrote %lu bytes from 0x%08lx to %s\n", len, addr, argv[3]);
    return 0;
}

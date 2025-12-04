#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

static FILE* fp;
static uint8_t csum;

uint8_t ReadByte(void)
{
    int c = fgetc(fp);
    if (c == EOF) {
        fprintf(stderr, "Unexpected EOF at %lu\n", ftell(fp));
        exit(3);
    }
    csum += (uint8_t)c;
    return (uint8_t)c;
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s rom\n", argv[0]);
        exit(1);
    }
    fp = fopen(argv[1], "r+b");
    if (!fp) {
        fprintf(stderr, "Could not open: %s\n", argv[1]);
        exit(1);
    }
    if (ReadByte() != 0x55 || ReadByte() != 0xAA) {
        fprintf(stderr, "Not a valid ROM file: %s\n", argv[1]);
        exit(1);
    }
    const uint32_t size = ReadByte() << 9;
    for (uint32_t i = 0; i < size - 4; ++i)
        (void)ReadByte();
    printf("%s: Updating ROM checksum to 0x%02X\n", argv[1], (uint8_t)-csum);
    fseek(fp, size-1, SEEK_SET);
    fputc((uint8_t)-csum, fp);

    fclose(fp);
    return 0;
}

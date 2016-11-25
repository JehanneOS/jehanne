#!/bin/bash
set -e

if [ "$JEHANNE" = "" ]; then
        echo createSIPIHeader.sh requires the shell started by ./hacking/devshell.sh
        exit 1
fi

cd $JEHANNE/sys/src/kern/amd64

gcc -c -O0 -static -fplan9-extensions -mno-red-zone -ffreestanding -fno-builtin -mcmodel=kernel l64sipi.S
ld -Ttext 0x00003000 l64sipi.o -o l64sipi
objcopy -O binary -j .text l64sipi l64sipi.out

echo 'uint8_t sipihandler[]={' > sipi.h
cat l64sipi.out | hexdump -v -e '7/1 "0x%02x, " 1/1 " 0x%02x,\n"' | sed '$s/0x  ,/0x00,/g'>> sipi.h
echo '};' >> sipi.h
rm l64sipi.out

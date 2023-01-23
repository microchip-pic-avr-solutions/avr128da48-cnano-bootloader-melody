REM Fill Unimplemented Flash Space with 0xFF
hexmate r0-FFFFFFFF,%2 -O%2 -FILL=w1:0xFF@0x4000:0x1FFFF

if %1 == CRC16 REM Calculate CRC-16 and store at footer
if %1 == CRC16 hexmate %2 -O%2 +-CK=4000-1FFFD@1FFFE+FFFFg5w-2p1021

if %1 == CRC32 REM Calculate CRC-32 and store at footer
if %1 == CRC32 hexmate %2 -O%2 +-CK=4000-1FFFB@1FFFC+FFFFFFFFg-5w-4p04C11DB7
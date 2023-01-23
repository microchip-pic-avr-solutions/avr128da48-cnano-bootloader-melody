REM Fill Unimplemented Flash Space with 0xFF
hexmate r0-FFFFFFFF,%1 -O%1 -FILL=w1:0xFF@0x4000:0x1FFFF

REM Calculate Checksum and store at footer
hexmate %1 -O%1 +-CK=4000-1FFFD@1FFFEg2w-2
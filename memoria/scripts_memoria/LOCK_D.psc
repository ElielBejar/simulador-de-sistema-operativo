SET AX 1
WAIT RD
IO_GEN_SLEEP ESPERA 1
WAIT RA
SET BX 1
SUM AX BX
IO_GEN_SLEEP ESPERA 1
SIGNAL RD
SIGNAL RA
EXIT
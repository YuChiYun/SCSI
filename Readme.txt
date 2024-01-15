Goals
• To understand how to send SCSI command under WINDOWS environment .
• Create a program to send read/write SCSI command to use the SSD .
• Know the differences between Lab1: Uart command and Lab2: SCSI command.

Requirement
Argument parser (30%):
--disk + %d: to select disk (You can get the info under Windows: 磁碟管理)
--write and --read: to select operation to perform
--lba + %d: to specify starting logical block address to perform operation on SSD
--sector_cnt + %d: the data length from starting logical block address to end
--data + %x: set the pattern to be write into SSD (e.g., --data FF will write FF into SSD from
start to end for the length of sector_cnt)
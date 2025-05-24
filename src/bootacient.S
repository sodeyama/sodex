/*
 *  @File        bootacient.S
 *  @Brief       The initial boot section of Sodex
 *  
 *  @Author      Sodex
 *  @Revision    0.1
 *  @License     suspension
 *  @Date        create: 2007/04/05  update: 2007/04/26
 *      
 *  Copyright (C) 2007 Sodex
 */

BOOTSEG         = 0x07C0
INITSEG         = 0x9000
KERNEL_SEG       = 0x0100       
KERNEL_STACK_SEG = 0x8000

# We won't have to increase the BOOT_SECTS to more 18
ACIENT_SECTS    = 1
MIDDLE_SECTS    = 4

MIDDLE_FIRST    = 64        # The num of 8blocks
KERNEL_FIRST    = 72        # The num of 9blocks

# This is the default size of general FD formatted at 1.44MB.       
SECTORS         = 18        

.code16
.text
    
.global _start, printstr
_start: 
    
        jmpl    $BOOTSEG, $start2
        
start2:
        xorw    %si, %si
        xorw    %di, %di
        movw    $BOOTSEG, %ax
        movw    %ax, %ds
        movw    $INITSEG, %ax
        movw    %ax, %es
        movw    $0x100, %cx
        cld
        rep     movsw
        jmpl    $INITSEG, $next

next:
        movw    %ax, %ds
        movw    $KERNEL_STACK_SEG, %ax
        movw    %ax, %ss
        xorw    %sp, %sp

        
# Clear the display and the message is printed
# before we load the boot_middle and kernel
        
        movb    $0x3, %al       # Clear the display
        movb    $0, %ah
        int     $0x10
        movw    $loader_mes, %si
        call    printstr

# Reset FD      
        call    reset_fd
        jmp     read_middleboot

/* Read next boot section from floppy drive.
 * The data is read from fd and It is set at ES:BX of the memory point.
 * The reserved memory at first is the parameter of FD.
 *  "secotr:" is sectors of current track ( 1 <= sector <= 18 )
 *  "head:" is current head ( 0 <= head <= 1 )
 *  "track:" is current track ( 0 <= track <= 79 )
 *
 * The first parameter is for middle boot.(64 logical sector)
 */
        sector: .byte 11
        track:  .byte 1
        head:   .byte 1
        nread:  .byte 0
        over64k:.byte 0
        ecount: .byte 5

read_middleboot:                        
        movw    $0x200, %bx     # "es" is already set at 0x9000

        movb    $0x2, %ah           # AH is BIOS COMMAND, The no.2 indicates
                                    # reading disk sectors

        movb    $MIDDLE_SECTS, %al  # AL is Num of sectors to read
        movb    (sector), %cl       # CL is the sector
        movb    (track), %ch        # CH is the track
        movb    (head), %dh         # DH is the head
        movb    $0, %dl             # DL is the drive(always 0)
        int     $0x13

        jnc     _read0
        call    read_error
        jmp     forever

_read0:
        addb    %al, (sector)
        
/* Read the kernel section from FD.
 * The data is read from fd and It is set at ES:BX.
 */
read0:   
        xorw    %bx, %bx
        movw    $KERNEL_SEG, %ax
        movw    %ax, %es            # ES:BX = 0x0100:0x0000(0x1000)
        movw    $KERNEL_SECTS, %di  # set the num of kernel sects to di
		#addw	$0x1, %di

        # set the param for kernel sects (72sects)
        movb    $1, (sector)       # CL is the sector
        movb    $2, (track)        # CH is the track
        movb    $0, (head)         # DH is the head

        call    read_kernel

        call    reset_fd
		
        ljmp    $0x9000, $0x0200


/* Read FD's kernel data
 * Before using this function, we must set the following parameter
 *  ES:BX  -  sent address
 *  DI     -  Num of read sectors
 *  sector, track, head, nread -  FD parameters             
 */     
read_kernel:
/* decide nread */      
        movb    (sector), %al       # get sector num
        subb    $1, %al
        movb    $18, %dl
        subb    %al, %dl            # dl = 18 - al (= 18 - (sector-1))
        xorb    %dh, %dh
        cmpw    %dx, %di            # if ( di >= dx )       
        jae     read_itr1
        movw    %di, %dx
        
read_itr1:
        movw    %es, %cx
        shl     $4, %cx             # es = es<<4
        movw    %bx, %ax
        addw    %ax, %cx            # es = es + bx
        movw    %cx, %si
        movw    %dx, %ax
        shl     $9, %ax             # ax = bx * 0x200
        addw    %ax, %cx            # es = es + bx + (nread)*0x200

        jc      read_itr1_over64k
        movw    %dx, (nread)        
        jmp     read_itr2       
        
read_itr1_over64k:
        movw    $1, (over64k)
        movw    $0xffff, %ax
        subw    %si, %ax            # si = es + bx
        addw    $1, %ax             # ax = 0x10000 - (es + bx)
        shr     $9, %ax             # ax = ax/0x200
        movb    %al, (nread)

        movw    $fd_over64k_error_mes, %si
        call    printstr

/* end deciding nread */        

/* just read from fd */             
read_itr2:
        movb    (nread), %al
        movb    (sector), %cl
        movb    (track), %ch
        movb    (head), %dh
        movb    $0x02, %ah
        movb    $0, %dl
        int     $0x13
        jnc     read_itr3
        
        # error occur
        decb    (ecount)
        jz      forever
        call    read_error
        pusha
        call    reset_fd
        popa
        jmp     read_itr2
/* end reading from fd */
        
/* post-processing */               
read_itr3:
        movb    %al, (nread)
        movb    (over64k), %cl              
        cmp     $1, %cl
        jz      read_itr3_over64k
        addb    %al, %bh
        addb    %al, %bh            # bx += al * 512
        jmp     read_itr4
        
read_itr3_over64k:                      
        movw    %es, %ax
        andw    $0xf000, %ax
        addw    $0x1000, %ax
        
        movw    %ax, %es            # es = es&0xf000+0x1000
        xorw    %bx, %bx
        movb    $0, (over64k)

read_itr4:          
        movb    (nread), %al
        movb    (sector), %cl
        addb    %cl, %al
        cmp     $SECTORS, %al
        ja      read_itr4_over1track
        movb    %al, (sector)
        jmp     read_itr5

read_itr4_over1track:
        movb    $1, (sector)
        movb    (head), %al
        cmp     $0, %al
        jz      read_itr4_over1track_head0
        movb    $0, (head)          # head == 1
        addb    $1, (track)
        jmp     read_itr5
        
read_itr4_over1track_head0:     
        movb    $1, (head)

read_itr5:              
        movb    (nread), %al
        xorb    %ah, %ah
        subw    %ax, %di
/* end of post-processing */        
#debug        
        #cmpw    $0, %di
        cmpw     $0, %di
        ja      read_kernel
        ret     



/* Reset floppy drive
 * Set AH = 0, which indicate to reset drive
 * Set AL = 0, which indicate first floppy drive.
 * INT 0x13: BIOS Disk services
 */
reset_fd:
        xorw    %ax, %ax        
        xorb    %dl, %dl        # dl is num of drive
        int     $0x13
        jnc     fd_reset_ok
        movw    $fd_reset_error_mes, %si
        call    printstr
        jmp     reset_fd
fd_reset_ok:                    
        ret     

read_error:     
        movw    $fd_read_error_mes, %si
        call    printstr
        ret
        
printstr:
        pusha
printstr_start:
        lodsb
        cmpb    $0, %al
        jz      printstr_end
        movb    $0x0e, %ah
        movb    $0, %bh
        int     $0x10
        jmp     printstr_start
printstr_end:
        popa
        ret

forever:           
        jmp     forever

loader_mes: 
        .ascii  "Loading...\r\n\0"
fd_reset_error_mes:     
        .ascii  "The floppy drive failed reset..\r\n\0"
fd_read_error_mes:      
        .ascii  "read error from FD..\r\n\0"
fd_over64k_error_mes:       
        .ascii  "over 64k..\r\n\0"      

        .org 510
        .word 0xAA55


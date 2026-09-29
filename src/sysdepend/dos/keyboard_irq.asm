; CauseWay protected-mode IRQ1 handler. Keep this routine short and call-free.
.386p

_DATA SEGMENT DWORD PUBLIC 'DATA'
        EXTRN   _keyboard_queue:BYTE
_DATA ENDS

_TEXT SEGMENT BYTE PUBLIC 'CODE'
        ASSUME  cs:_TEXT
        PUBLIC  keyboard_irq_

keyboard_irq_:
        pushad
        push    ds
        mov     bx,_DATA
        mov     ds,bx

        ; Read the scan byte, pulse port 61h to acknowledge the keyboard,
        ; then queue the byte for translation in the main event loop.
        mov     dx,60h
        in      al,dx
        mov     bh,al
        mov     dx,61h
        in      al,dx
        mov     bl,al
        or      al,80h
        out     dx,al
        mov     al,bl
        out     dx,al

        movzx   eax,BYTE PTR _keyboard_queue+80h
        mov     edx,eax
        inc     dl
        and     dl,7fh
        cmp     dl,BYTE PTR _keyboard_queue+81h
        je      queue_full
        mov     BYTE PTR _keyboard_queue[eax],bh
        mov     BYTE PTR _keyboard_queue+80h,dl
        jmp     queue_done

queue_full:
        inc     DWORD PTR _keyboard_queue+84h
queue_done:
        mov     dx,20h
        mov     al,20h
        out     dx,al

        pop     ds
        popad
        iretd

_TEXT ENDS
END

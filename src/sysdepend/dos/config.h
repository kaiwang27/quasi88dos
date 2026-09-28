#ifndef CONFIG_H_INCLUDED
#define CONFIG_H_INCLUDED

/* Initial Open Watcom/CauseWay target. See dos/README.md for current scope. */
#if !defined(__WATCOMC__) || !defined(__386__) || !defined(__DOS__)
#error This backend requires Open Watcom targeting 32-bit DOS
#endif
#define QUASI88_DOS
#define LSB_FIRST
#define Q_COMMENT "DOS port (bring-up)"
#define INLINE static __inline
/* Reserved for the first framebuffer backend; no display exists yet. */
#define SUPPORT_8BPP

#endif
